#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <nghttp2/nghttp2.h>

typedef struct {
    SSL *ssl;
    nghttp2_session *session;
    int32_t stream_id;
    int status;
    size_t body_len;
    int stream_closed;
} app_t;

static int on_frame_recv(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
    app_t *app = (app_t *)user_data;
    (void)session;
    printf("[host-h2] frame type=%u stream=%d flags=0x%x len=%u target=%d body=%zu status=%d\n",
        (unsigned)frame->hd.type,
        (int)frame->hd.stream_id,
        (unsigned)frame->hd.flags,
        (unsigned)frame->hd.length,
        (int)app->stream_id,
        app->body_len,
        app->status);
    return 0;
}

static int on_header(nghttp2_session *session, const nghttp2_frame *frame,
                     const uint8_t *name, size_t namelen,
                     const uint8_t *value, size_t valuelen,
                     uint8_t flags, void *user_data) {
    app_t *app = (app_t *)user_data;
    (void)session;
    (void)flags;
    if (frame->hd.stream_id != app->stream_id) {
        return 0;
    }
    printf("[host-h2] hdr %.*s: %.*s\n", (int)namelen, name, (int)valuelen, value);
    if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
        char tmp[4];
        size_t n = valuelen < sizeof(tmp) - 1 ? valuelen : sizeof(tmp) - 1;
        memcpy(tmp, value, n);
        tmp[n] = '\0';
        app->status = atoi(tmp);
    }
    return 0;
}

static int on_data_chunk_recv(nghttp2_session *session, uint8_t flags, int32_t stream_id,
                              const uint8_t *data, size_t len, void *user_data) {
    app_t *app = (app_t *)user_data;
    (void)session;
    (void)flags;
    (void)data;
    if (stream_id != app->stream_id) {
        return 0;
    }
    app->body_len += len;
    printf("[host-h2] data stream=%d len=%zu total=%zu\n", (int)stream_id, len, app->body_len);
    return 0;
}

static int on_stream_close(nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *user_data) {
    app_t *app = (app_t *)user_data;
    (void)session;
    if (stream_id == app->stream_id) {
        app->stream_closed = 1;
        printf("[host-h2] close stream=%d err=0x%x body=%zu status=%d\n", (int)stream_id, error_code, app->body_len, app->status);
    }
    return 0;
}

static ssize_t send_cb(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data) {
    app_t *app = (app_t *)user_data;
    (void)session;
    (void)flags;
    int n = SSL_write(app->ssl, data, (int)length);
    if (n <= 0) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return n;
}

static int tcp_connect_host(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *res = NULL, *rp;
    int fd = -1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    if (getaddrinfo(host, port, &hints, &res) != 0) {
        return -1;
    }
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int main(void) {
    const char *host = "www.php.net";
    int fd;
    SSL_CTX *ctx;
    SSL *ssl;
    nghttp2_session_callbacks *callbacks;
    app_t app;
    uint8_t buf[16384];

    SSL_library_init();
    SSL_load_error_strings();
    OPENSSL_init_ssl(0, NULL);

    fd = tcp_connect_host(host, "443");
    if (fd < 0) {
        fprintf(stderr, "connect failed\n");
        return 1;
    }

    ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_default_verify_paths(ctx);
    ssl = SSL_new(ctx);
    SSL_set_tlsext_host_name(ssl, host);
    SSL_set1_host(ssl, host);
    {
        static const unsigned char alpn[] = { 2, 'h', '2' };
        SSL_set_alpn_protos(ssl, alpn, sizeof(alpn));
    }
    SSL_set_fd(ssl, fd);
    if (SSL_connect(ssl) != 1) {
        ERR_print_errors_fp(stderr);
        return 1;
    }

    memset(&app, 0, sizeof(app));
    app.ssl = ssl;

    nghttp2_session_callbacks_new(&callbacks);
    nghttp2_session_callbacks_set_send_callback(callbacks, send_cb);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close);
    nghttp2_session_client_new(&app.session, callbacks, &app);

    nghttp2_submit_settings(app.session, NGHTTP2_FLAG_NONE, NULL, 0);

    nghttp2_nv hdrs[] = {
        {(uint8_t *)":method", (uint8_t *)"GET", 7, 3, NGHTTP2_NV_FLAG_NONE},
        {(uint8_t *)":scheme", (uint8_t *)"https", 7, 5, NGHTTP2_NV_FLAG_NONE},
        {(uint8_t *)":authority", (uint8_t *)host, 10, strlen(host), NGHTTP2_NV_FLAG_NONE},
        {(uint8_t *)":path", (uint8_t *)"/", 5, 1, NGHTTP2_NV_FLAG_NONE},
        {(uint8_t *)"user-agent", (uint8_t *)"rp2350-php", 10, 10, NGHTTP2_NV_FLAG_NONE},
        {(uint8_t *)"accept", (uint8_t *)"*/*", 6, 3, NGHTTP2_NV_FLAG_NONE},
    };
    app.stream_id = nghttp2_submit_request(app.session, NULL, hdrs, sizeof(hdrs)/sizeof(hdrs[0]), NULL, NULL);
    printf("[host-h2] submitted stream=%d\n", (int)app.stream_id);
    nghttp2_session_send(app.session);

    while (!app.stream_closed) {
        int n = SSL_read(ssl, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        printf("[host-h2] ssl_read=%d\n", n);
        if (nghttp2_session_mem_recv(app.session, buf, (size_t)n) < 0) {
            fprintf(stderr, "nghttp2_session_mem_recv failed\n");
            break;
        }
        if (nghttp2_session_send(app.session) != 0) {
            fprintf(stderr, "nghttp2_session_send failed\n");
            break;
        }
    }

    printf("[host-h2] done status=%d body=%zu closed=%d\n", app.status, app.body_len, app.stream_closed);

    nghttp2_session_del(app.session);
    nghttp2_session_callbacks_del(callbacks);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(fd);
    return 0;
}
