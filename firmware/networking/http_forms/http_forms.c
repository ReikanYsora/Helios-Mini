#include "http_forms.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define HTTP_FORM_MAX_VALUE_LEN 512

void http_form_url_decode(char *dst, const char *src)
{
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

void http_form_get(const char *body, const char *key, char *out, size_t out_size)
{
    out[0] = '\0';
    size_t key_len = strlen(key);
    const char *p = body;
    while (p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *val = p + key_len + 1;
            const char *end = strchr(val, '&');
            size_t len = end ? (size_t)(end - val) : strlen(val);
            char raw[HTTP_FORM_MAX_VALUE_LEN];
            if (len >= sizeof(raw)) {
                len = sizeof(raw) - 1;
            }
            memcpy(raw, val, len);
            raw[len] = '\0';
            http_form_url_decode(out, raw);
            if (strlen(out) >= out_size) {
                out[out_size - 1] = '\0';
            }
            return;
        }
        p = strchr(p, '&');
        if (p) {
            p++;
        }
    }
}

void http_form_html_escape(const char *src, char *dst, size_t dst_size)
{
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 6 < dst_size; si++) {
        char c = src[si];
        if (c == '&')       { memcpy(&dst[di], "&amp;", 5); di += 5; }
        else if (c == '<')  { memcpy(&dst[di], "&lt;", 4); di += 4; }
        else if (c == '>')  { memcpy(&dst[di], "&gt;", 4); di += 4; }
        else if (c == '"')  { memcpy(&dst[di], "&quot;", 6); di += 6; }
        else if (c == '\'') { memcpy(&dst[di], "&#39;", 5); di += 5; }
        else { dst[di++] = c; }
    }
    dst[di] = '\0';
}

void http_form_url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 4 < dst_size; si++) {
        unsigned char c = (unsigned char)src[si];
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            dst[di++] = (char)c;
        } else {
            dst[di++] = '%';
            dst[di++] = hex[(c >> 4) & 0xF];
            dst[di++] = hex[c & 0xF];
        }
    }
    dst[di] = '\0';
}

void http_form_get_query_param(httpd_req_t *req, const char *key, char *out, size_t out_size)
{
    out[0] = '\0';
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0) {
        return;
    }

    char *query = malloc(qlen + 1);
    if (query == NULL) {
        return;
    }
    if (httpd_req_get_url_query_str(req, query, qlen + 1) == ESP_OK) {
        char raw[HTTP_FORM_MAX_VALUE_LEN];
        if (httpd_query_key_value(query, key, raw, sizeof(raw)) == ESP_OK) {
            http_form_url_decode(out, raw);
            if (strlen(out) >= out_size) {
                out[out_size - 1] = '\0';
            }
        }
    }
    free(query);
}
