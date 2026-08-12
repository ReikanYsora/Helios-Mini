#pragma once

#include <stddef.h>
#include "esp_http_server.h"

/* Tiny x-www-form-urlencoded helpers shared by every local HTTP form on
 * this device (networking/provisioning, networking/settings_server).
 * Deliberately dependency-free (no JS on the client side, no JSON parser
 * here) - see spec Section 16's "no dev tools" requirement. */

/* Percent/plus decodes `src` into `dst` (`dst` must be at least as large as
 * `src` - decoding only ever shrinks the string). */
void http_form_url_decode(char *dst, const char *src);

/* Writes the decoded value of `key` from a x-www-form-urlencoded `body`
 * into `out` (empty string if absent). `body` must be null-terminated. */
void http_form_get(const char *body, const char *key, char *out, size_t out_size);

/* Escapes &<>"' for safe embedding in an HTML response. */
void http_form_html_escape(const char *src, char *dst, size_t dst_size);

/* Percent-encodes `src` for safe embedding in a URL query string (e.g. a
 * link built from a discovered Home Assistant URL). `dst_size` should be
 * at least 3x strlen(src)+1 for the worst case (every byte escaped). */
void http_form_url_encode(const char *src, char *dst, size_t dst_size);

/* Extracts the decoded value of `key` from an HTTP request's own query
 * string (e.g. "/?ha_url=..."). Empty string if absent or there's no query
 * string at all. */
void http_form_get_query_param(httpd_req_t *req, const char *key, char *out, size_t out_size);
