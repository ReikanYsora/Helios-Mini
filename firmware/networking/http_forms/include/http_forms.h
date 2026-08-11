#pragma once

#include <stddef.h>

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
