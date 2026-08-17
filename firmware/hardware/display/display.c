#include "display.h"
#include "board_config.h"
#include "touch.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_sh8601.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "figtree.h"

#include <assert.h>
#include <string.h>

#define HELIOS_LCD_BITS_PER_PIXEL      16
/* 30 -> 45 lines: PSRAM was a dead end for this buffer (see the flush
 * allocation below), so this is the safe lever left - fewer, bigger
 * partial-buffer flushes per full-height redraw (~16 -> ~11) without
 * pushing internal-RAM usage anywhere near the edge. Real numbers from
 * this board: steady-state free heap was 58619 B at 30 lines/DMA-RAM;
 * 45 lines costs ~27 KB more (466 * 15 extra lines * 2 bytes * 2
 * buffers), leaving a healthy ~31 KB margin. See docs/HARDWARE_REFERENCE.md. */
#define HELIOS_LVGL_BUF_LINES          45
#define HELIOS_LVGL_TICK_MS            2
#define HELIOS_LVGL_TASK_STACK         (8 * 1024)
/* Pinned to the APP core (core 1) at display_init() so the render/flush
 * loop never competes with the Wi-Fi stack (which lives on the PRO core,
 * core 0, at a much higher priority): a network burst used to preempt this
 * task mid-frame and drop swipe frames. Priority sits above the app's own
 * background tasks so it also wins its core cleanly. */
#define HELIOS_LVGL_TASK_PRIORITY      4
#define HELIOS_LVGL_TASK_CORE          1
#define HELIOS_LVGL_TASK_MAX_DELAY_MS  500
#define HELIOS_LVGL_TASK_MIN_DELAY_MS  5

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static SemaphoreHandle_t s_flush_done = NULL;
/* Set (from ISR context) the moment the first real frame finishes
 * transferring to the panel - see the brightness note in panel_init(). A
 * plain bool is fine here: one ISR writer, one reader in lvgl_task, and
 * it only ever goes false->true once. */
static volatile bool s_first_flush_done = false;
/* Small internal DMA buffer the flush bounces each area through - the LVGL
 * draw buffers live in PSRAM (freeing internal RAM) but the QSPI DMA can't
 * source from there. */
static uint8_t *s_bounce = NULL;
static bool s_brightness_applied = false;

/* CO5300 init sequence, ported from Waveshare's own factory firmware for
 * this exact board (brought up through the SH8601-compatible QSPI driver -
 * the CO5300's command set matches it in practice). Flagged in
 * docs/HARDWARE_REFERENCE.md for revalidation against the CO5300 datasheet
 * directly. */
static const sh8601_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    /* Brightness starts at 0, not the 0xFF this table originally had here
     * (ported as-is from Waveshare's factory firmware, which apparently
     * doesn't care about the resulting flash of garbage GRAM content).
     * Sleep Out (0x11) and Display On (0x29) below happen with the panel
     * already dark; it only gets ramped up once real content is actually
     * on screen - see the s_first_flush_done handling in lvgl_task(). */
    {0x51, (uint8_t[]){0x00}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x11, (uint8_t[]){0x00}, 0, 100},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &woken);
    s_first_flush_done = true;
    return woken == pdTRUE;
}

static void panel_init(void)
{
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = HELIOS_PIN_LCD_SCLK,
        .data0_io_num = HELIOS_PIN_LCD_DATA0,
        .data1_io_num = HELIOS_PIN_LCD_DATA1,
        .data2_io_num = HELIOS_PIN_LCD_DATA2,
        .data3_io_num = HELIOS_PIN_LCD_DATA3,
        .max_transfer_sz = HELIOS_LCD_H_RES * HELIOS_LCD_V_RES * HELIOS_LCD_BITS_PER_PIXEL / 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(HELIOS_LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = HELIOS_PIN_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        /* 60 -> 80 MHz: the swipe's dominant cost is raw bytes-on-wire time
         * (a full 466x466x16bpp redraw is ~430 KB, already close to one
         * frame's budget), not just flush-call overhead, so the wire clock
         * is a direct lever on it. Watch for any tearing/glitching on real
         * hardware and back off to 60 MHz if so. See docs/HARDWARE_REFERENCE.md. */
        .pclk_hz = 80 * 1000 * 1000,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = { .quad_mode = true },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)HELIOS_LCD_HOST, &io_cfg, &s_io));

    sh8601_vendor_config_t vendor_cfg = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = HELIOS_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = HELIOS_LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(s_io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    /* MADCTL (0x36) = 0xC0 (MY=1, MX=1): rotates the image 180 degrees from
     * the panel's raw default. The panel showed upside down without this on
     * real hardware - this exact value is what Waveshare's own factory
     * firmware sends for this board too, so it should also keep touch
     * mirroring (hardware/display's touch_read_cb) consistent with what it
     * was ported from. See docs/HARDWARE_REFERENCE.md. */
    uint32_t madctl_cmd = 0x36;
    madctl_cmd = (madctl_cmd << 8) | (0x02u << 24);
    uint8_t madctl_param = 0xC0;
    esp_lcd_panel_io_tx_param(s_io, madctl_cmd, &madctl_param, 1);

    /* Brightness is already 0 at this point (set in s_lcd_init_cmds[],
     * before Sleep Out/Display On) and stays there - ramping it up only
     * happens once the first real frame has actually been flushed, see
     * lvgl_task's use of s_first_flush_done below. */
}

void display_set_brightness(uint8_t level)
{
    uint32_t cmd = 0x51;
    cmd = (cmd << 8) | (0x02u << 24);
    esp_lcd_panel_io_tx_param(s_io, cmd, &level, 1);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p)
{
    size_t px = lv_area_get_width(area) * lv_area_get_height(area);
    /* color_p is in PSRAM (the LVGL draw buffers); the QSPI DMA can only
     * source from internal RAM, so bounce this area through s_bounce. */
    memcpy(s_bounce, color_p, px * 2);
    lv_draw_sw_rgb565_swap(s_bounce, px);

    /* This panel's framebuffer is offset by 6 columns relative to the
     * physical drawable area - ported as-is from Waveshare's reference
     * firmware, which applies the same +6/+7 offset. */
    esp_lcd_panel_draw_bitmap(s_panel, area->x1 + 6, area->y1, area->x2 + 7, area->y2 + 1, s_bounce);
}

static void rounder_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x = 0, y = 0;
    if (touch_read(&x, &y)) {
        /* The touch digitizer is mounted mirrored relative to the panel on
         * this board - mirror both axes to match, as in the vendor
         * reference firmware. */
        data->point.x = (x < HELIOS_LCD_H_RES) ? (HELIOS_LCD_H_RES - x) : 0;
        data->point.y = (y < HELIOS_LCD_V_RES) ? (HELIOS_LCD_V_RES - y) : 0;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void tick_cb(void *arg)
{
    lv_tick_inc(HELIOS_LVGL_TICK_MS);
}

static void flush_wait_cb(lv_display_t *disp)
{
    xSemaphoreTake(s_flush_done, portMAX_DELAY);
}

bool display_lock(int timeout_ms)
{
    assert(s_lvgl_mutex && "display_init() must be called first");
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mutex, ticks) == pdTRUE;
}

void display_unlock(void)
{
    xSemaphoreGive(s_lvgl_mutex);
}

/* Little-endian field writers for the BMP header below - byte-at-a-time
 * rather than casting (bmp + offset) to a wider int type and dereferencing,
 * which would be unaligned access on some of these offsets. Xtensa can
 * generally get away with that, but this is the portable way to do it and
 * it's cheap either way for a header written once per screenshot. */
static void bmp_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void bmp_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

#define BMP_HEADER_LEN 54 /* 14-byte BITMAPFILEHEADER + 40-byte BITMAPINFOHEADER */

bool display_take_screenshot_bmp(uint8_t **out_buf, size_t *out_len)
{
    if (!display_lock(2000)) {
        ESP_LOGE("display", "screenshot: display_lock() timed out");
        return false;
    }

    lv_obj_t *scr = lv_screen_active();
    uint32_t w = (uint32_t)lv_obj_get_width(scr);
    uint32_t h = (uint32_t)lv_obj_get_height(scr);
    uint32_t stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);

    /* lv_snapshot_take() would allocate its own render target out of
     * LVGL's own heap (lv_malloc, LV_MEM_SIZE_KILOBYTES=64) - nowhere near
     * enough for a 466x466 RGB565 buffer (~425KB). Allocate that buffer
     * ourselves straight out of PSRAM instead and hand it to LVGL with
     * lv_snapshot_take_to_draw_buf(), which renders into a caller-owned
     * buffer rather than allocating one. */
    uint32_t snap_data_size = stride * h;
    uint8_t *snap_data = heap_caps_malloc(snap_data_size, MALLOC_CAP_SPIRAM);
    if (snap_data == NULL) {
        ESP_LOGE("display", "screenshot: %u byte PSRAM allocation for the render target failed",
                 (unsigned)snap_data_size);
        display_unlock();
        return false;
    }

    lv_draw_buf_t snap = {0};
    if (lv_draw_buf_init(&snap, w, h, LV_COLOR_FORMAT_RGB565, stride, snap_data, snap_data_size) != LV_RESULT_OK ||
        lv_snapshot_take_to_draw_buf(scr, LV_COLOR_FORMAT_RGB565, &snap) != LV_RESULT_OK) {
        ESP_LOGE("display", "screenshot: lv_snapshot_take_to_draw_buf() failed (heap: internal %u, psram %u)",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        heap_caps_free(snap_data);
        display_unlock();
        return false;
    }
    /* lv_draw_buf_init() may have nudged .data forward from snap_data to
     * satisfy LV_DRAW_BUF_ALIGN - snap.data is the pointer to actually read
     * pixels from, snap_data (still snap_data_size long) is what gets
     * freed below. */

    /* Plain 24bpp BGR, no compression - the most widely-readable BMP
     * variant, worth the 1.5x size over staying at RGB565 for something
     * that only gets downloaded a handful of times for docs/bug reports.
     * Rows are stored bottom-up and padded to a 4-byte boundary, per the
     * BMP spec. */
    uint32_t row_bytes = w * 3;
    uint32_t row_padded = (row_bytes + 3) & ~3u;
    uint32_t pixel_data_size = row_padded * h;
    uint32_t file_size = BMP_HEADER_LEN + pixel_data_size;

    uint8_t *bmp = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
    if (bmp == NULL) {
        ESP_LOGE("display", "screenshot: %u byte PSRAM allocation for the BMP failed", (unsigned)file_size);
        heap_caps_free(snap_data);
        display_unlock();
        return false;
    }
    memset(bmp, 0, BMP_HEADER_LEN);

    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp_put_u32(bmp + 2, file_size);
    bmp_put_u32(bmp + 10, BMP_HEADER_LEN); /* pixel data offset */
    bmp_put_u32(bmp + 14, 40);             /* BITMAPINFOHEADER size */
    bmp_put_u32(bmp + 18, w);
    bmp_put_u32(bmp + 22, h);              /* positive height = bottom-up rows */
    bmp_put_u16(bmp + 26, 1);              /* color planes */
    bmp_put_u16(bmp + 28, 24);             /* bits per pixel */
    bmp_put_u32(bmp + 34, pixel_data_size);

    for (uint32_t y = 0; y < h; y++) {
        const uint16_t *src_row = (const uint16_t *)(snap.data + (h - 1 - y) * snap.header.stride);
        uint8_t *dst_row = bmp + BMP_HEADER_LEN + y * row_padded;
        for (uint32_t x = 0; x < w; x++) {
            uint16_t px = src_row[x];
            uint8_t r5 = (px >> 11) & 0x1F;
            uint8_t g6 = (px >> 5) & 0x3F;
            uint8_t b5 = px & 0x1F;
            dst_row[x * 3 + 0] = (uint8_t)((b5 * 255) / 31); /* B */
            dst_row[x * 3 + 1] = (uint8_t)((g6 * 255) / 63); /* G */
            dst_row[x * 3 + 2] = (uint8_t)((r5 * 255) / 31); /* R */
        }
    }

    heap_caps_free(snap_data);
    display_unlock();

    *out_buf = bmp;
    *out_len = file_size;
    return true;
}

static void lvgl_task(void *arg)
{
    uint32_t delay_ms = HELIOS_LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        if (display_lock(-1)) {
            delay_ms = lv_timer_handler();
            display_unlock();
        }
        if (!s_brightness_applied && s_first_flush_done) {
            /* First real frame is on the panel now - safe to turn the
             * screen on. See the comment in panel_init(). */
            display_set_brightness(0xFF);
            s_brightness_applied = true;
        }
        if (delay_ms > HELIOS_LVGL_TASK_MAX_DELAY_MS) {
            delay_ms = HELIOS_LVGL_TASK_MAX_DELAY_MS;
        } else if (delay_ms < HELIOS_LVGL_TASK_MIN_DELAY_MS) {
            delay_ms = HELIOS_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void display_init(void)
{
    s_flush_done = xSemaphoreCreateBinary();
    s_lvgl_mutex = xSemaphoreCreateMutex();
    assert(s_flush_done && s_lvgl_mutex);

    panel_init();
    touch_init();

    lv_init();

    lv_display_t *disp = lv_display_create(HELIOS_LCD_H_RES, HELIOS_LCD_V_RES);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_flush_wait_cb(disp, flush_wait_cb);
    lv_display_set_user_data(disp, s_panel);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    /* The two LVGL draw buffers live in PSRAM to keep ~tens of KB of scarce
     * internal RAM free (the QSPI DMA can't source from PSRAM directly - it
     * rejects it with a blank screen - so flush_cb bounces each area through
     * s_bounce, one internal-RAM buffer sized to a full flush). */
    size_t buf_size = HELIOS_LCD_H_RES * HELIOS_LVGL_BUF_LINES * LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    s_bounce = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    assert(buf1 && buf2 && s_bounce);
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Figtree (the Helios brand font) as the default for every widget. */
    lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                          lv_palette_main(LV_PALETTE_RED), true, &figtree_16);

    /* Black from the very first rendered frame - before the boot animation
     * even runs - so the lvgl task can't flash the default theme background
     * at power-on. */
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    const esp_timer_create_args_t tick_args = {
        .callback = &tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, HELIOS_LVGL_TICK_MS * 1000));

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", HELIOS_LVGL_TASK_STACK, NULL,
                            HELIOS_LVGL_TASK_PRIORITY, NULL, HELIOS_LVGL_TASK_CORE);
}
