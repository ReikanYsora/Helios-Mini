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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#include <assert.h>

#define HELIOS_LCD_BITS_PER_PIXEL      16
#define HELIOS_LVGL_BUF_LINES          30
#define HELIOS_LVGL_TICK_MS            2
#define HELIOS_LVGL_TASK_STACK         (8 * 1024)
#define HELIOS_LVGL_TASK_PRIORITY      2
#define HELIOS_LVGL_TASK_MAX_DELAY_MS  500
#define HELIOS_LVGL_TASK_MIN_DELAY_MS  5

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static SemaphoreHandle_t s_flush_done = NULL;

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
    {0x51, (uint8_t[]){0xFF}, 1, 0},
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
        .pclk_hz = 40 * 1000 * 1000,
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

    display_set_brightness(0xFF);
}

void display_set_brightness(uint8_t level)
{
    uint32_t cmd = 0x51;
    cmd = (cmd << 8) | (0x02u << 24);
    esp_lcd_panel_io_tx_param(s_io, cmd, &level, 1);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p)
{
    lv_draw_sw_rgb565_swap(color_p, lv_area_get_width(area) * lv_area_get_height(area));

    /* This panel's framebuffer is offset by 6 columns relative to the
     * physical drawable area - ported as-is from Waveshare's reference
     * firmware, which applies the same +6/+7 offset. */
    esp_lcd_panel_draw_bitmap(s_panel, area->x1 + 6, area->y1, area->x2 + 7, area->y2 + 1, color_p);
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

static void lvgl_task(void *arg)
{
    uint32_t delay_ms = HELIOS_LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        if (display_lock(-1)) {
            delay_ms = lv_timer_handler();
            display_unlock();
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

    size_t buf_size = HELIOS_LCD_H_RES * HELIOS_LVGL_BUF_LINES * LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    assert(buf1 && buf2);
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

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

    xTaskCreate(lvgl_task, "lvgl", HELIOS_LVGL_TASK_STACK, NULL, HELIOS_LVGL_TASK_PRIORITY, NULL);
}
