#ifndef RP2350_HTTP_INTERNAL_H
#define RP2350_HTTP_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "pico/time.h"
#include "lwip/ip_addr.h"
#include "lwip/err.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include "nghttp2/nghttp2.h"

typedef struct {
	struct altcp_pcb *pcb;
	char *rx;
	size_t rx_len;
	size_t rx_cap;
	absolute_time_t last_rx_time;
	volatile bool connected;
	volatile bool done;
	volatile bool had_error;
	volatile bool truncated;
	err_t last_err;
} rp2350_altcp_ctx_t;

typedef struct {
	char *tx;
	size_t tx_len;
	size_t tx_cap;
	size_t tx_off;
	char *body;
	size_t body_len;
	size_t body_cap;
	size_t body_limit;
	bool body_dynamic;
	bool body_truncated;
	int status_code;
	int32_t stream_id;
	bool stream_closed;
	uint32_t stream_error_code;
	char *req_body;
	size_t req_body_len;
	size_t req_body_off;
} rp2350_h2_ctx_t;

typedef struct {
	char method[16];
	char *headers;
	size_t headers_len;
	char *content;
	size_t content_len;
	char *user_agent;
	uint32_t timeout_ms;
} rp2350_http_request_opts_t;

extern struct altcp_tls_config *s_http_tls_config;
extern struct altcp_tls_config *s_h2_tls_config;

bool rp2350_http_tls_config_ready(void);
bool rp2350_h2_tls_config_ready(void);

bool rp2350_parse_http_url(const char *url, char *host, size_t host_size, u16_t *port, const char **path, bool *is_https);
bool rp2350_lwip_wait_until(absolute_time_t deadline, volatile bool *flag);
bool rp2350_dns_resolve(const char *host, uint32_t timeout_ms, ip_addr_t *out_addr);

ssize_t rp2350_h2_send_cb(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data);
ssize_t rp2350_h2_read_data_cb(
	nghttp2_session *session,
	int32_t stream_id,
	uint8_t *buf,
	size_t length,
	uint32_t *data_flags,
	nghttp2_data_source *source,
	void *user_data
);
int rp2350_h2_on_header_cb(
	nghttp2_session *session,
	const nghttp2_frame *frame,
	const uint8_t *name,
	size_t namelen,
	const uint8_t *value,
	size_t valuelen,
	uint8_t flags,
	void *user_data
);
int rp2350_h2_on_data_chunk_recv_cb(
	nghttp2_session *session,
	uint8_t flags,
	int32_t stream_id,
	const uint8_t *data,
	size_t len,
	void *user_data
);
int rp2350_h2_on_stream_close_cb(
	nghttp2_session *session,
	int32_t stream_id,
	uint32_t error_code,
	void *user_data
);

err_t rp2350_h2_altcp_connected_cb(void *arg, struct altcp_pcb *conn, err_t err);
err_t rp2350_h2_altcp_recv_cb(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err);
void rp2350_h2_altcp_err_cb(void *arg, err_t err);
void rp2350_h2_altcp_close(rp2350_altcp_ctx_t *ctx);
bool rp2350_h2_flush_tx(rp2350_h2_ctx_t *h2, rp2350_altcp_ctx_t *net, absolute_time_t deadline);
bool rp2350_h2_get_body_ex(
	const char *url,
	uint32_t timeout_ms,
	size_t max_read,
	char **body_out,
	size_t *body_len_out,
	bool *alpn_http1_out
);

bool rp2350_http_h1_extract_body(
	const char *resp,
	size_t resp_len,
	size_t max_read,
	char **body_out,
	size_t *body_len_out
);
bool rp2350_net_tls_request_http1(
	const char *host,
	u16_t port,
	const char *payload,
	size_t payload_len,
	uint32_t timeout_ms,
	size_t max_read,
	char **out,
	size_t *out_len
);

#endif /* RP2350_HTTP_INTERNAL_H */
