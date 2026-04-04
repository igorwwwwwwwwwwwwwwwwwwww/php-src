/*
 * rp2350_httpd.c
 *
 * lwIP httpd custom file handler for PHP on RP2350.
 *
 * fs_open_custom() runs PHP synchronously and fills a response buffer.
 * lwIP's httpd requires file->len to be set before returning, so we
 * run the full PHP eval inside fs_open_custom() and set the length.
 *
 * This works because on this target LWIP_ASSERT_CORE_LOCKED() is a
 * no-op and the CYW43 background callbacks run cooperatively in the
 * same thread as main() via cyw43_arch_poll().
 *
 * Routes:
 *   /  /info  /info.php  -> phpinfo()
 *   anything else        -> 404
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "lwipopts.h"
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"

#include "rp2350_eval.h"

#ifndef fs_wait_cb
typedef void (*fs_wait_cb)(void *arg);
#endif

#define RESP_MAX (64 * 1024)

static char   s_resp_buf[RESP_MAX];
static size_t s_resp_len = 0;

static const char s_404[] =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "Content-Length: 47\r\n"
    "\r\n"
    "<html><body><h1>404 Not Found</h1></body></html>";

static bool is_phpinfo_path(const char *name)
{
    return strcmp(name, "/") == 0 ||
           strcmp(name, "/info") == 0 ||
           strcmp(name, "/info.php") == 0;
}

/* --------------------------------------------------------------------------
 * lwIP httpd custom fs callbacks
 * -------------------------------------------------------------------------- */

int fs_open_custom(struct fs_file *file, const char *name)
{
    memset(file, 0, sizeof(*file));
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    if (!is_phpinfo_path(name)) {
        file->data = s_404;
        file->len  = (int)(sizeof(s_404) - 1);
        file->index = file->len;
        return 1;
    }

    /* run PHP synchronously -- safe because CYW43 callbacks are cooperative */
    size_t body_len = 0;
    char *body = rp2350_eval_capture_string("phpinfo();", &body_len);

    int hdr = snprintf(s_resp_buf, sizeof(s_resp_buf),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "Content-Length: %u\r\n"
        "\r\n",
        (unsigned)body_len);

    if (hdr > 0 && body && body_len > 0) {
        size_t space = sizeof(s_resp_buf) - (size_t)hdr;
        size_t copy  = body_len < space ? body_len : space;
        memcpy(s_resp_buf + hdr, body, copy);
        s_resp_len = (size_t)hdr + copy;
    } else {
        s_resp_len = hdr > 0 ? (size_t)hdr : 0;
    }
    if (body) free(body);

    file->data  = s_resp_buf;
    file->len   = (int)s_resp_len;
    file->index = (int)s_resp_len;
    return 1;
}

void fs_close_custom(struct fs_file *file)
{
    (void)file;
}

int fs_read_custom(struct fs_file *file, char *buffer, int count)
{
    (void)file; (void)buffer; (void)count;
    return FS_READ_EOF;
}

u8_t fs_canread_custom(struct fs_file *file)
{
    (void)file;
    return 1;
}

u8_t fs_wait_read_custom(struct fs_file *file, fs_wait_cb cb, void *arg)
{
    (void)file; (void)cb; (void)arg;
    return 1;
}

int fs_read_async_custom(struct fs_file *file, char *buffer, int count,
                          fs_wait_cb cb, void *arg)
{
    (void)cb; (void)arg;
    return fs_read_custom(file, buffer, count);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void rp2350_httpd_poll(void)
{
    /* nothing needed -- PHP runs synchronously in fs_open_custom */
}

void rp2350_httpd_init(void)
{
    httpd_init();
    printf("[httpd] listening on port 80\r\n");
}
