#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include "mbedtls/ssl.h"
#include "nghttp2/nghttp2.h"
#include "picohttpparser.h"

#include "Zend/zend.h"
#include "Zend/zend_API.h"
#include "Zend/zend_compile.h"
#include "Zend/zend_smart_str.h"
#include "SAPI.h"
#include "main/php_memory_streams.h"
#include "main/php_streams.h"

#include "src/rp2350_ca_bundle.h"
#include "rp2350_eval.h"
#include "rp2350_http_stream.h"
#include "rp2350_wifi.h"

static struct altcp_tls_config *s_http_tls_config = NULL;
static struct altcp_tls_config *s_h2_tls_config = NULL;
static bool rp2350_parse_http_url(const char *url, char *host, size_t host_size, u16_t *port, const char **path, bool *is_https);
static struct altcp_pcb *rp2350_altcp_tls_alloc_with_sni(void *arg, u8_t ip_type);
bool rp2350_http_tls_config_ready(void)
{
	static const char *s_http1_alpn[] = { "http/1.1", NULL };

	if (s_http_tls_config != NULL) {
		return true;
	}

	cyw43_arch_lwip_begin();
	s_http_tls_config = altcp_tls_create_config_client(rp2350_ca_bundle_pem, rp2350_ca_bundle_pem_len);
	if (s_http_tls_config != NULL) {
		if (altcp_tls_configure_alpn_protocols(s_http_tls_config, s_http1_alpn) != 0) {
			altcp_tls_free_config(s_http_tls_config);
			s_http_tls_config = NULL;
		}
	}
	cyw43_arch_lwip_end();

	return s_http_tls_config != NULL;
}

bool rp2350_h2_tls_config_ready(void)
{
	static const char *s_h2_alpn[] = { "h2", "http/1.1", NULL };

	if (s_h2_tls_config != NULL) {
		return true;
	}

	cyw43_arch_lwip_begin();
	s_h2_tls_config = altcp_tls_create_config_client(rp2350_ca_bundle_pem, rp2350_ca_bundle_pem_len);
	if (s_h2_tls_config != NULL) {
		if (altcp_tls_configure_alpn_protocols(s_h2_tls_config, s_h2_alpn) != 0) {
			altcp_tls_free_config(s_h2_tls_config);
			s_h2_tls_config = NULL;
		}
	}
	cyw43_arch_lwip_end();

	return s_h2_tls_config != NULL;
}

typedef struct {
	volatile bool done;
	volatile bool ok;
	ip_addr_t addr;
} rp2350_dns_query_t;

typedef struct {
	struct tcp_pcb *pcb;
	char *rx;
	size_t rx_len;
	size_t rx_cap;
	volatile bool connected;
	volatile bool done;
	volatile bool had_error;
	volatile bool truncated;
	err_t last_err;
} rp2350_tcp_ctx_t;

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

static void rp2350_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
	rp2350_dns_query_t *q = (rp2350_dns_query_t *)arg;
	(void)name;
	if (q == NULL) {
		return;
	}
	if (ipaddr != NULL) {
		q->addr = *ipaddr;
		q->ok = true;
	}
	q->done = true;
}

static bool rp2350_lwip_wait_until(absolute_time_t deadline, volatile bool *flag)
{
	while (!(*flag)) {
		if (time_reached(deadline)) {
			return false;
		}
		sleep_ms(1);
	}
	return true;
}

static bool rp2350_dns_resolve(const char *host, uint32_t timeout_ms, ip_addr_t *out_addr)
{
	rp2350_dns_query_t q;
	err_t err;
	absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

	memset(&q, 0, sizeof(q));

	cyw43_arch_lwip_begin();
	err = dns_gethostbyname(host, &q.addr, rp2350_dns_found_cb, &q);
	cyw43_arch_lwip_end();

	if (err == ERR_OK) {
		*out_addr = q.addr;
		return true;
	}
	if (err != ERR_INPROGRESS) {
		return false;
	}
	if (!rp2350_lwip_wait_until(deadline, &q.done) || !q.ok) {
		return false;
	}
	*out_addr = q.addr;
	return true;
}

static err_t rp2350_tcp_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
	rp2350_tcp_ctx_t *ctx = (rp2350_tcp_ctx_t *)arg;
	(void)tpcb;
	if (ctx == NULL) {
		return ERR_OK;
	}
	ctx->last_err = err;
	if (err == ERR_OK) {
		ctx->connected = true;
	} else {
		ctx->had_error = true;
		ctx->done = true;
	}
	return ERR_OK;
}

static err_t rp2350_tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
	rp2350_tcp_ctx_t *ctx = (rp2350_tcp_ctx_t *)arg;
	u16_t recved = 0;

	if (ctx == NULL) {
		if (p != NULL) {
			pbuf_free(p);
		}
		return ERR_OK;
	}
	if (err != ERR_OK) {
		ctx->last_err = err;
		ctx->had_error = true;
		ctx->done = true;
		if (p != NULL) {
			pbuf_free(p);
		}
		return ERR_OK;
	}
	if (p == NULL) {
		ctx->done = true;
		return ERR_OK;
	}

	if (ctx->rx_len < ctx->rx_cap) {
		size_t avail = ctx->rx_cap - ctx->rx_len;
		size_t take = (p->tot_len < avail) ? (size_t)p->tot_len : avail;
		if (take < (size_t)p->tot_len) {
			ctx->truncated = true;
		}
		if (take > 0) {
			pbuf_copy_partial(p, ctx->rx + ctx->rx_len, (u16_t)take, 0);
			ctx->rx_len += take;
		}
	} else {
		ctx->truncated = true;
	}

	recved = p->tot_len;
	tcp_recved(tpcb, recved);
	pbuf_free(p);
	return ERR_OK;
}

static void rp2350_tcp_err_cb(void *arg, err_t err)
{
	rp2350_tcp_ctx_t *ctx = (rp2350_tcp_ctx_t *)arg;
	if (ctx == NULL) {
		return;
	}
	ctx->last_err = err;
	ctx->pcb = NULL;
	ctx->had_error = true;
	ctx->done = true;
}

static void rp2350_tcp_ctx_close(rp2350_tcp_ctx_t *ctx)
{
	if (ctx == NULL || ctx->pcb == NULL) {
		return;
	}
	cyw43_arch_lwip_begin();
	tcp_arg(ctx->pcb, NULL);
	tcp_recv(ctx->pcb, NULL);
	tcp_err(ctx->pcb, NULL);
	{
		err_t err = tcp_close(ctx->pcb);
		if (err != ERR_OK) {
			tcp_abort(ctx->pcb);
		}
	}
	cyw43_arch_lwip_end();
	ctx->pcb = NULL;
}

bool rp2350_net_tcp_request(
	const char *host,
	u16_t port,
	const char *payload,
	size_t payload_len,
	uint32_t timeout_ms,
	size_t max_read,
	char **out,
	size_t *out_len
)
{
	ip_addr_t remote;
	rp2350_tcp_ctx_t ctx;
	absolute_time_t deadline;
	size_t off = 0;

	*out = NULL;
	*out_len = 0;

	if (!rp2350_wifi_init_once()) {
		return false;
	}
	if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
		return false;
	}
	if (!rp2350_dns_resolve(host, timeout_ms, &remote)) {
		return false;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.rx_cap = max_read;
	ctx.rx = (char *)malloc(ctx.rx_cap + 1);
	if (ctx.rx == NULL) {
		return false;
	}

	deadline = make_timeout_time_ms(timeout_ms);

	cyw43_arch_lwip_begin();
	ctx.pcb = tcp_new_ip_type(IP_GET_TYPE(&remote));
	if (ctx.pcb != NULL) {
		tcp_arg(ctx.pcb, &ctx);
		tcp_recv(ctx.pcb, rp2350_tcp_recv_cb);
		tcp_err(ctx.pcb, rp2350_tcp_err_cb);
		ctx.last_err = tcp_connect(ctx.pcb, &remote, port, rp2350_tcp_connected_cb);
		if (ctx.last_err != ERR_OK) {
			tcp_arg(ctx.pcb, NULL);
			tcp_recv(ctx.pcb, NULL);
			tcp_err(ctx.pcb, NULL);
			tcp_abort(ctx.pcb);
			ctx.pcb = NULL;
		}
	}
	cyw43_arch_lwip_end();
	if (ctx.pcb == NULL) {
		free(ctx.rx);
		return false;
	}

	if (!rp2350_lwip_wait_until(deadline, &ctx.connected) || ctx.had_error) {
		rp2350_tcp_ctx_close(&ctx);
		free(ctx.rx);
		return false;
	}

	while (off < payload_len) {
		bool wrote = false;
		cyw43_arch_lwip_begin();
		if (ctx.pcb != NULL) {
			u16_t snd = tcp_sndbuf(ctx.pcb);
			if (snd > 0) {
				u16_t chunk = (u16_t)((payload_len - off) < snd ? (payload_len - off) : snd);
				err_t w = tcp_write(ctx.pcb, payload + off, chunk, TCP_WRITE_FLAG_COPY);
				if (w == ERR_OK) {
					off += chunk;
					(void)tcp_output(ctx.pcb);
					wrote = true;
				} else if (w != ERR_MEM) {
					ctx.had_error = true;
					ctx.last_err = w;
				}
			}
		}
		cyw43_arch_lwip_end();
		if (ctx.had_error || ctx.pcb == NULL) {
			rp2350_tcp_ctx_close(&ctx);
			free(ctx.rx);
			return false;
		}
		if (!wrote) {
			if (time_reached(deadline)) {
				rp2350_tcp_ctx_close(&ctx);
				free(ctx.rx);
				return false;
			}
			sleep_ms(1);
		}
	}

	while (!ctx.done && !ctx.had_error) {
		if (time_reached(deadline)) {
			break;
		}
		sleep_ms(1);
	}

	rp2350_tcp_ctx_close(&ctx);
	ctx.rx[ctx.rx_len] = '\0';
	*out = ctx.rx;
	*out_len = ctx.rx_len;
	return true;
}

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

static bool rp2350_h2_buf_append(char **buf, size_t *len, size_t *cap, const uint8_t *src, size_t n)
{
	size_t need;
	size_t new_cap;
	char *new_buf;

	if (n == 0) {
		return true;
	}
	need = *len + n;
	if (need <= *cap) {
		memcpy(*buf + *len, src, n);
		*len += n;
		return true;
	}

	new_cap = (*cap == 0) ? 1024 : *cap;
	while (new_cap < need) {
		if (new_cap > (SIZE_MAX / 2)) {
			return false;
		}
		new_cap *= 2;
	}
	new_buf = (char *)realloc(*buf, new_cap);
	if (!new_buf) {
		return false;
	}
	*buf = new_buf;
	*cap = new_cap;
	memcpy(*buf + *len, src, n);
	*len += n;
	return true;
}

static ssize_t rp2350_h2_send_cb(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data)
{
	rp2350_h2_ctx_t *ctx = (rp2350_h2_ctx_t *)user_data;
	(void)session;
	(void)flags;
	if (!rp2350_h2_buf_append(&ctx->tx, &ctx->tx_len, &ctx->tx_cap, data, length)) {
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	}
	return (ssize_t)length;
}

static ssize_t rp2350_h2_read_data_cb(
	nghttp2_session *session,
	int32_t stream_id,
	uint8_t *buf,
	size_t length,
	uint32_t *data_flags,
	nghttp2_data_source *source,
	void *user_data
)
{
	rp2350_h2_ctx_t *ctx = (rp2350_h2_ctx_t *)user_data;
	size_t remain;
	size_t n;
	(void)session;
	(void)stream_id;
	(void)source;

	if (!ctx || !ctx->req_body || ctx->req_body_off >= ctx->req_body_len) {
		*data_flags |= NGHTTP2_DATA_FLAG_EOF;
		return 0;
	}

	remain = ctx->req_body_len - ctx->req_body_off;
	n = remain < length ? remain : length;
	memcpy(buf, ctx->req_body + ctx->req_body_off, n);
	ctx->req_body_off += n;
	if (ctx->req_body_off >= ctx->req_body_len) {
		*data_flags |= NGHTTP2_DATA_FLAG_EOF;
	}
	return (ssize_t)n;
}

static int rp2350_h2_on_header_cb(nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name, size_t namelen, const uint8_t *value, size_t valuelen, uint8_t flags, void *user_data)
{
	rp2350_h2_ctx_t *ctx = (rp2350_h2_ctx_t *)user_data;
	(void)session;
	(void)flags;

	if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
		return 0;
	}
	if (frame->hd.stream_id != ctx->stream_id) {
		return 0;
	}
	if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
		char tmp[4];
		size_t n = (valuelen < sizeof(tmp) - 1) ? valuelen : (sizeof(tmp) - 1);
		memcpy(tmp, value, n);
		tmp[n] = '\0';
		ctx->status_code = atoi(tmp);
	}
	return 0;
}

static int rp2350_h2_on_data_chunk_recv_cb(nghttp2_session *session, uint8_t flags, int32_t stream_id, const uint8_t *data, size_t len, void *user_data)
{
	rp2350_h2_ctx_t *ctx = (rp2350_h2_ctx_t *)user_data;
	size_t avail;
	size_t take;
	(void)session;
	(void)flags;

	if (stream_id != ctx->stream_id || len == 0) {
		return 0;
	}

	if (ctx->body_dynamic) {
		if (ctx->body_limit > 0 && ctx->body_len >= ctx->body_limit) {
			ctx->body_truncated = true;
			return 0;
		}
		take = len;
		if (ctx->body_limit > 0) {
			size_t remain = ctx->body_limit - ctx->body_len;
			if (take > remain) {
				take = remain;
				ctx->body_truncated = true;
			}
		}
		if (take > 0 && !rp2350_h2_buf_append(&ctx->body, &ctx->body_len, &ctx->body_cap, data, take)) {
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		}
		return 0;
	}

	if (ctx->body_len >= ctx->body_cap) {
		ctx->body_truncated = true;
		return 0;
	}

	avail = ctx->body_cap - ctx->body_len;
	take = (len < avail) ? len : avail;
	if (take < len) {
		ctx->body_truncated = true;
	}
	memcpy(ctx->body + ctx->body_len, data, take);
	ctx->body_len += take;
	return 0;
}

static int rp2350_h2_on_stream_close_cb(nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *user_data)
{
	rp2350_h2_ctx_t *ctx = (rp2350_h2_ctx_t *)user_data;
	(void)session;
	if (ctx && stream_id == ctx->stream_id) {
		ctx->stream_closed = true;
		ctx->stream_error_code = error_code;
	}
	return 0;
}

static bool rp2350_h2_flush_tx(rp2350_h2_ctx_t *h2, rp2350_altcp_ctx_t *net, absolute_time_t deadline)
{
	while (h2->tx_off < h2->tx_len) {
		bool progressed = false;
		cyw43_arch_lwip_begin();
		if (net->pcb != NULL) {
			u16_t snd = altcp_sndbuf(net->pcb);
			if (snd > 0) {
				size_t rem = h2->tx_len - h2->tx_off;
				u16_t chunk = (u16_t)(rem < snd ? rem : snd);
				err_t w = altcp_write(net->pcb, h2->tx + h2->tx_off, chunk, TCP_WRITE_FLAG_COPY);
				if (w == ERR_OK) {
					h2->tx_off += chunk;
					(void)altcp_output(net->pcb);
					progressed = true;
				} else if (w != ERR_MEM) {
					net->had_error = true;
					net->last_err = w;
				}
			}
		}
		cyw43_arch_lwip_end();

		if (net->had_error || net->pcb == NULL) {
			return false;
		}
		if (!progressed) {
			if (time_reached(deadline)) {
				return false;
			}
			sleep_ms(1);
		}
	}

	h2->tx_len = 0;
	h2->tx_off = 0;
	return true;
}

typedef struct {
	struct altcp_tls_config *config;
	const char *hostname;
} rp2350_h2_tls_alloc_ctx_t;

static struct altcp_pcb *rp2350_h2_tls_alloc_with_sni(void *arg, u8_t ip_type)
{
	rp2350_h2_tls_alloc_ctx_t *ctx = (rp2350_h2_tls_alloc_ctx_t *)arg;
	struct altcp_pcb *pcb;

	if (!ctx || !ctx->config) {
		return NULL;
	}

	pcb = altcp_tls_alloc(ctx->config, ip_type);
	if (!pcb) {
		return NULL;
	}

	if (ctx->hostname && ctx->hostname[0] != '\0') {
		void *tls = altcp_tls_context(pcb);
		if (tls) {
			(void)mbedtls_ssl_set_hostname((mbedtls_ssl_context *)tls, ctx->hostname);
		}
	}
	return pcb;
}

static err_t rp2350_h2_altcp_connected_cb(void *arg, struct altcp_pcb *conn, err_t err)
{
	rp2350_altcp_ctx_t *ctx = (rp2350_altcp_ctx_t *)arg;
	(void)conn;
	if (!ctx) {
		return ERR_ARG;
	}
	if (err == ERR_OK) {
		ctx->connected = true;
	} else {
		ctx->had_error = true;
		ctx->last_err = err;
		ctx->done = true;
	}
	return ERR_OK;
}

static err_t rp2350_h2_altcp_recv_cb(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err)
{
	rp2350_altcp_ctx_t *ctx = (rp2350_altcp_ctx_t *)arg;
	size_t avail;
	size_t take;
	u16_t recvd;

	if (!ctx) {
		if (p) {
			pbuf_free(p);
		}
		return ERR_OK;
	}
	if (err != ERR_OK) {
		if (p) {
			pbuf_free(p);
		}
		ctx->had_error = true;
		ctx->last_err = err;
		ctx->done = true;
		return err;
	}
	if (!p) {
		ctx->done = true;
		return ERR_OK;
	}

	if (ctx->rx_len < ctx->rx_cap) {
		avail = ctx->rx_cap - ctx->rx_len;
		take = (p->tot_len < avail) ? (size_t)p->tot_len : avail;
		if (take < (size_t)p->tot_len) {
			ctx->truncated = true;
		}
		if (take > 0) {
			pbuf_copy_partial(p, ctx->rx + ctx->rx_len, (u16_t)take, 0);
			ctx->rx_len += take;
		}
	} else {
		ctx->truncated = true;
	}

	recvd = p->tot_len;
	altcp_recved(conn, recvd);
	pbuf_free(p);
	ctx->last_rx_time = get_absolute_time();
	return ERR_OK;
}

static void rp2350_h2_altcp_err_cb(void *arg, err_t err)
{
	rp2350_altcp_ctx_t *ctx = (rp2350_altcp_ctx_t *)arg;
	if (!ctx) {
		return;
	}
	ctx->last_err = err;
	ctx->pcb = NULL;
	ctx->had_error = true;
	ctx->done = true;
}

static void rp2350_h2_altcp_close(rp2350_altcp_ctx_t *ctx)
{
	if (!ctx || !ctx->pcb) {
		return;
	}
	cyw43_arch_lwip_begin();
	altcp_arg(ctx->pcb, NULL);
	altcp_recv(ctx->pcb, NULL);
	altcp_err(ctx->pcb, NULL);
	{
		err_t err = altcp_close(ctx->pcb);
		if (err != ERR_OK) {
			altcp_abort(ctx->pcb);
		}
	}
	cyw43_arch_lwip_end();
	ctx->pcb = NULL;
}

static bool rp2350_net_tls_request(
	const char *host,
	u16_t port,
	const char *payload,
	size_t payload_len,
	uint32_t timeout_ms,
	size_t max_read,
	char **out,
	size_t *out_len
)
{
	ip_addr_t remote;
	rp2350_altcp_ctx_t ctx;
	absolute_time_t deadline;
	size_t off = 0;
	rp2350_h2_tls_alloc_ctx_t tls_ctx;
	altcp_allocator_t tls_allocator;

	*out = NULL;
	*out_len = 0;

	if (!rp2350_wifi_init_once()) {
		return false;
	}
	if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
		return false;
	}
	if (!rp2350_h2_tls_config_ready()) {
		return false;
	}
	if (!rp2350_dns_resolve(host, timeout_ms, &remote)) {
		return false;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.rx_cap = max_read;
	ctx.rx = (char *)malloc(max_read + 1);
	if (!ctx.rx) {
		return false;
	}
	ctx.last_rx_time = get_absolute_time();

	tls_ctx.config = s_h2_tls_config;
	tls_ctx.hostname = host;
	tls_allocator.alloc = rp2350_h2_tls_alloc_with_sni;
	tls_allocator.arg = &tls_ctx;
	deadline = make_timeout_time_ms(timeout_ms);

	cyw43_arch_lwip_begin();
	ctx.pcb = altcp_new_ip_type(&tls_allocator, IP_GET_TYPE(&remote));
	if (ctx.pcb != NULL) {
		altcp_arg(ctx.pcb, &ctx);
		altcp_recv(ctx.pcb, rp2350_h2_altcp_recv_cb);
		altcp_err(ctx.pcb, rp2350_h2_altcp_err_cb);
		ctx.last_err = altcp_connect(ctx.pcb, &remote, port, rp2350_h2_altcp_connected_cb);
		if (ctx.last_err != ERR_OK) {
			altcp_arg(ctx.pcb, NULL);
			altcp_recv(ctx.pcb, NULL);
			altcp_err(ctx.pcb, NULL);
			altcp_abort(ctx.pcb);
			ctx.pcb = NULL;
		}
	}
	cyw43_arch_lwip_end();
	if (ctx.pcb == NULL) {
		free(ctx.rx);
		return false;
	}

	if (!rp2350_lwip_wait_until(deadline, &ctx.connected) || ctx.had_error) {
		rp2350_h2_altcp_close(&ctx);
		free(ctx.rx);
		return false;
	}

	while (off < payload_len) {
		bool wrote = false;
		cyw43_arch_lwip_begin();
		if (ctx.pcb != NULL) {
			u16_t snd = altcp_sndbuf(ctx.pcb);
			if (snd > 0) {
				u16_t chunk = (u16_t)((payload_len - off) < snd ? (payload_len - off) : snd);
				err_t w = altcp_write(ctx.pcb, payload + off, chunk, TCP_WRITE_FLAG_COPY);
				if (w == ERR_OK) {
					off += chunk;
					(void)altcp_output(ctx.pcb);
					wrote = true;
				} else if (w != ERR_MEM) {
					ctx.had_error = true;
					ctx.last_err = w;
				}
			}
		}
		cyw43_arch_lwip_end();
		if (ctx.had_error || ctx.pcb == NULL) {
			rp2350_h2_altcp_close(&ctx);
			free(ctx.rx);
			return false;
		}
		if (!wrote) {
			if (time_reached(deadline)) {
				rp2350_h2_altcp_close(&ctx);
				free(ctx.rx);
				return false;
			}
			sleep_ms(1);
		}
	}

	/* Signal end-of-request bytes so the peer can finish and close promptly. */
	cyw43_arch_lwip_begin();
	if (ctx.pcb != NULL) {
		(void)altcp_shutdown(ctx.pcb, 0, 1);
	}
	cyw43_arch_lwip_end();

	while (!ctx.done && !ctx.had_error) {
		if (time_reached(deadline)) {
			break;
		}
		sleep_ms(1);
	}

	rp2350_h2_altcp_close(&ctx);
	ctx.rx[ctx.rx_len] = '\0';
	*out = ctx.rx;
	*out_len = ctx.rx_len;
	return true;
}

static bool rp2350_h2_get_body_ex(
	const char *url,
	uint32_t timeout_ms,
	size_t max_read,
	char **body_out,
	size_t *body_len_out,
	bool *alpn_http1_out
)
{
	char host[96];
	u16_t port;
	const char *path;
	bool is_https = false;
	rp2350_h2_ctx_t ctx;
	rp2350_altcp_ctx_t net;
	ip_addr_t remote;
	rp2350_h2_tls_alloc_ctx_t tls_ctx;
	altcp_allocator_t tls_allocator;
	absolute_time_t deadline;
	nghttp2_session_callbacks *callbacks = NULL;
	nghttp2_session *session = NULL;
	nghttp2_nv nva[5];
	size_t parsed_off;
	size_t rx_cap;
	ssize_t parsed;
	int rc;
	const char *alpn = NULL;
	bool ok = false;

	*body_out = NULL;
	*body_len_out = 0;
	if (alpn_http1_out) {
		*alpn_http1_out = false;
	}

	if (!rp2350_parse_http_url(url, host, sizeof(host), &port, &path, &is_https)) {
		php_error_docref(NULL, E_WARNING, "h2 url parse failed: %s", url ? url : "<null>");
		return false;
	}
	if (!is_https) {
		php_error_docref(NULL, E_WARNING, "h2 get currently requires https:// with ALPN h2");
		return false;
	}
	if (max_read < 1) {
		max_read = 1;
	} else if (max_read > 262144) {
		max_read = 262144;
	}
	if (!rp2350_wifi_init_once()) {
		php_error_docref(NULL, E_WARNING, "h2 wifi init failed");
		return false;
	}
	if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
		php_error_docref(NULL, E_WARNING, "h2 wifi not connected");
		return false;
	}
	if (!rp2350_h2_tls_config_ready()) {
		php_error_docref(NULL, E_WARNING, "h2 tls config not ready");
		return false;
	}
	if (!rp2350_dns_resolve(host, timeout_ms, &remote)) {
		php_error_docref(NULL, E_WARNING, "h2 dns resolve failed: host=%s", host);
		return false;
	}

	memset(&ctx, 0, sizeof(ctx));
	memset(&net, 0, sizeof(net));
	ctx.body = (char *)malloc(max_read + 1);
	if (!ctx.body) {
		php_error_docref(NULL, E_WARNING, "h2 body alloc failed: cap=%u", (unsigned)max_read);
		return false;
	}
	ctx.body_cap = max_read;
	ctx.body_limit = max_read;
	ctx.body_dynamic = false;
	rx_cap = max_read + 32768;
	net.rx = (char *)malloc(rx_cap + 1);
	if (!net.rx) {
		php_error_docref(NULL, E_WARNING, "h2 rx alloc failed: cap=%u", (unsigned)rx_cap);
		free(ctx.body);
		return false;
	}
	net.rx_cap = rx_cap;
	net.last_rx_time = get_absolute_time();
	parsed_off = 0;

	rc = nghttp2_session_callbacks_new(&callbacks);
	if (rc != 0) {
		php_error_docref(NULL, E_WARNING, "h2 callbacks alloc failed: %d", rc);
		goto cleanup;
	}
	nghttp2_session_callbacks_set_send_callback(callbacks, rp2350_h2_send_cb);
	nghttp2_session_callbacks_set_on_header_callback(callbacks, rp2350_h2_on_header_cb);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, rp2350_h2_on_data_chunk_recv_cb);
	nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, rp2350_h2_on_stream_close_cb);

	rc = nghttp2_session_client_new(&session, callbacks, &ctx);
	if (rc != 0) {
		php_error_docref(NULL, E_WARNING, "h2 session alloc failed: %d", rc);
		goto cleanup;
	}

	rc = nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, NULL, 0);
	if (rc != 0) {
		php_error_docref(NULL, E_WARNING, "h2 submit settings failed: %d", rc);
		goto cleanup;
	}

	memset(nva, 0, sizeof(nva));
	nva[0].name = (uint8_t *)":method";
	nva[0].value = (uint8_t *)"GET";
	nva[0].namelen = sizeof(":method") - 1;
	nva[0].valuelen = sizeof("GET") - 1;
	nva[1].name = (uint8_t *)":scheme";
	nva[1].value = (uint8_t *)"https";
	nva[1].namelen = sizeof(":scheme") - 1;
	nva[1].valuelen = sizeof("https") - 1;
	nva[2].name = (uint8_t *)":authority";
	nva[2].value = (uint8_t *)host;
	nva[2].namelen = sizeof(":authority") - 1;
	nva[2].valuelen = strlen(host);
	nva[3].name = (uint8_t *)":path";
	nva[3].value = (uint8_t *)path;
	nva[3].namelen = sizeof(":path") - 1;
	nva[3].valuelen = strlen(path);
	nva[4].name = (uint8_t *)"user-agent";
	nva[4].value = (uint8_t *)"rp2350-nghttp2";
	nva[4].namelen = sizeof("user-agent") - 1;
	nva[4].valuelen = sizeof("rp2350-nghttp2") - 1;

	ctx.stream_id = nghttp2_submit_request(session, NULL, nva, 5, NULL, NULL);
	if (ctx.stream_id < 0) {
		php_error_docref(NULL, E_WARNING, "h2 submit request failed: %d", (int)ctx.stream_id);
		goto cleanup;
	}

	rc = nghttp2_session_send(session);
	if (rc != 0) {
		php_error_docref(NULL, E_WARNING, "h2 encode failed: %d", rc);
		goto cleanup;
	}

	tls_ctx.config = s_h2_tls_config;
	tls_ctx.hostname = host;
	tls_allocator.alloc = rp2350_h2_tls_alloc_with_sni;
	tls_allocator.arg = &tls_ctx;
	deadline = make_timeout_time_ms(timeout_ms);

	cyw43_arch_lwip_begin();
	net.pcb = altcp_new_ip_type(&tls_allocator, IP_GET_TYPE(&remote));
	if (net.pcb != NULL) {
		altcp_arg(net.pcb, &net);
		altcp_recv(net.pcb, rp2350_h2_altcp_recv_cb);
		altcp_err(net.pcb, rp2350_h2_altcp_err_cb);
		net.last_err = altcp_connect(net.pcb, &remote, port, rp2350_h2_altcp_connected_cb);
		if (net.last_err != ERR_OK) {
			altcp_arg(net.pcb, NULL);
			altcp_recv(net.pcb, NULL);
			altcp_err(net.pcb, NULL);
			altcp_abort(net.pcb);
			net.pcb = NULL;
		}
	}
	cyw43_arch_lwip_end();
	if (net.pcb == NULL) {
		php_error_docref(NULL, E_WARNING, "h2 connect alloc/connect failed");
		goto cleanup;
	}
	if (!rp2350_lwip_wait_until(deadline, &net.connected) || net.had_error) {
		php_error_docref(NULL, E_WARNING, "h2 connect timeout/error: lwip=%d", (int)net.last_err);
		goto cleanup;
	}

	cyw43_arch_lwip_begin();
	if (net.pcb != NULL) {
		void *tls = altcp_tls_context(net.pcb);
		if (tls != NULL) {
			alpn = mbedtls_ssl_get_alpn_protocol((mbedtls_ssl_context *)tls);
		}
	}
	cyw43_arch_lwip_end();
	if (alpn == NULL || strcmp(alpn, "h2") != 0) {
		if (alpn_http1_out && alpn && strcmp(alpn, "http/1.1") == 0) {
			*alpn_http1_out = true;
		} else {
			php_error_docref(NULL, E_WARNING, "h2 ALPN negotiation failed: got=%s", alpn ? alpn : "<null>");
		}
		goto cleanup;
	}

	while (!time_reached(deadline) && !net.had_error && !ctx.stream_closed) {
		if (!rp2350_h2_flush_tx(&ctx, &net, deadline)) {
			break;
		}
		if (net.rx_len > parsed_off) {
			parsed = nghttp2_session_mem_recv(
				session,
				(const uint8_t *)(net.rx + parsed_off),
				net.rx_len - parsed_off
			);
			if (parsed < 0) {
				php_error_docref(NULL, E_WARNING, "h2 parse failed: %s", nghttp2_strerror((int)parsed));
				goto cleanup;
			}
			parsed_off += (size_t)parsed;

			rc = nghttp2_session_send(session);
			if (rc != 0) {
				php_error_docref(NULL, E_WARNING, "h2 session_send failed: %d", rc);
				goto cleanup;
			}
			continue;
		}
		sleep_ms(1);
	}

	if (net.had_error) {
		php_error_docref(NULL, E_WARNING, "h2 transport error: lwip=%d", (int)net.last_err);
		goto cleanup;
	}
	if (ctx.status_code <= 0) {
		char preview[49];
		size_t n = net.rx_len < 48 ? net.rx_len : 48;
		size_t i;
		for (i = 0; i < n; i++) {
			char c = net.rx[i];
			preview[i] = (c >= 32 && c <= 126) ? c : '.';
		}
		preview[n] = '\0';
		php_error_docref(
			NULL,
			E_WARNING,
			"h2 response missing :status (rx=%u parsed=%u stream_id=%d stream_closed=%d stream_err=0x%x body=%u head=\"%s\")",
			(unsigned)net.rx_len,
			(unsigned)parsed_off,
			(int)ctx.stream_id,
			ctx.stream_closed ? 1 : 0,
			(unsigned)ctx.stream_error_code,
			(unsigned)ctx.body_len,
			preview
		);
		goto cleanup;
	}
	if (ctx.status_code < 200 || ctx.status_code >= 300) {
		php_error_docref(NULL, E_WARNING, "h2 status=%d", ctx.status_code);
		goto cleanup;
	}
	if (ctx.body_len == 0) {
		php_error_docref(NULL, E_WARNING, "h2 empty body: status=%d parsed=%u rx=%u stream_closed=%d",
			ctx.status_code,
			(unsigned)parsed_off,
			(unsigned)net.rx_len,
			ctx.stream_closed ? 1 : 0);
		goto cleanup;
	}
	if (ctx.body_truncated) {
		php_error_docref(NULL, E_WARNING, "h2 body truncated: cap=%u", (unsigned)max_read);
	}

	ctx.body[ctx.body_len] = '\0';
	*body_out = ctx.body;
	*body_len_out = ctx.body_len;
	ctx.body = NULL;
	ok = true;

cleanup:
	if (net.pcb != NULL) {
		rp2350_h2_altcp_close(&net);
	}
	free(net.rx);
	nghttp2_session_del(session);
	nghttp2_session_callbacks_del(callbacks);
	free(ctx.tx);
	if (ctx.body != NULL) {
		free(ctx.body);
	}
	return ok;
}

static bool rp2350_h2_get_body(const char *url, uint32_t timeout_ms, size_t max_read, char **body_out, size_t *body_len_out)
{
	return rp2350_h2_get_body_ex(url, timeout_ms, max_read, body_out, body_len_out, NULL);
}

static bool rp2350_parse_http_url(const char *url, char *host, size_t host_size, u16_t *port, const char **path, bool *is_https)
{
	const char *p;
	const char *host_start;
	size_t host_len;
	unsigned long parsed_port = 80;
	char *endptr;

	if (strncmp(url, "http://", 7) == 0) {
		p = url + 7;
		*is_https = false;
		parsed_port = 80;
	} else if (strncmp(url, "https://", 8) == 0) {
		p = url + 8;
		*is_https = true;
		parsed_port = 443;
	} else {
		return false;
	}
	host_start = p;
	while (*p && *p != '/' && *p != ':') {
		p++;
	}
	if (p == host_start) {
		return false;
	}

	host_len = (size_t)(p - host_start);
	if (host_len + 1 > host_size) {
		return false;
	}
	memcpy(host, host_start, host_len);
	host[host_len] = '\0';

	if (*p == ':') {
		const char *port_start = p + 1;
		while (*p && *p != '/') {
			p++;
		}
		{
			size_t n = (size_t)(p - port_start);
			char tmp[8];
			if (n == 0 || n >= sizeof(tmp)) {
				return false;
			}
			memcpy(tmp, port_start, n);
			tmp[n] = '\0';
			parsed_port = strtoul(tmp, &endptr, 10);
			if (*endptr != '\0' || parsed_port == 0 || parsed_port > 65535) {
				return false;
			}
		}
	}

	*port = (u16_t)parsed_port;
	*path = (*p == '/') ? p : "/";
	return true;
}

typedef struct {
	struct altcp_tls_config *config;
	const char *hostname;
} rp2350_tls_alloc_ctx_t;

static struct altcp_pcb *rp2350_altcp_tls_alloc_with_sni(void *arg, u8_t ip_type)
{
	rp2350_tls_alloc_ctx_t *ctx = (rp2350_tls_alloc_ctx_t *)arg;
	struct altcp_pcb *pcb;

	if (!ctx || !ctx->config) {
		return NULL;
	}

	pcb = altcp_tls_alloc(ctx->config, ip_type);
	if (!pcb) {
		return NULL;
	}

	if (ctx->hostname && ctx->hostname[0] != '\0') {
		void *tls = altcp_tls_context(pcb);
		if (tls) {
			int rc = mbedtls_ssl_set_hostname((mbedtls_ssl_context *)tls, ctx->hostname);
			if (rc != 0) {
				php_error_docref(NULL, E_WARNING, "https SNI setup failed for host=%s mbedtls=%d", ctx->hostname, rc);
			}
		}
	}

	return pcb;
}

typedef struct {
	char method[16];
	char *headers;
	size_t headers_len;
	char *content;
	size_t content_len;
	char *user_agent;
	uint32_t timeout_ms;
} rp2350_http_request_opts_t;

static void rp2350_http_request_opts_init(rp2350_http_request_opts_t *opts)
{
	memset(opts, 0, sizeof(*opts));
	memcpy(opts->method, "GET", 4);
	opts->timeout_ms = 8000;
}

static void rp2350_http_request_opts_cleanup(rp2350_http_request_opts_t *opts)
{
	free(opts->headers);
	opts->headers = NULL;
	opts->headers_len = 0;
	free(opts->content);
	opts->content = NULL;
	opts->content_len = 0;
	free(opts->user_agent);
	opts->user_agent = NULL;
}

static bool rp2350_strdup_from_zval(zval *zv, char **out, size_t *len_out)
{
	zend_string *zs;
	char *dup;
	size_t len;

	if (!zv || !out || !len_out) {
		return false;
	}

	zs = zval_get_string(zv);
	if (!zs) {
		return false;
	}

	len = ZSTR_LEN(zs);
	dup = (char *)malloc(len + 1);
	if (!dup) {
		zend_string_release(zs);
		return false;
	}
	memcpy(dup, ZSTR_VAL(zs), len);
	dup[len] = '\0';
	*out = dup;
	*len_out = len;
	zend_string_release(zs);
	return true;
}

static bool rp2350_parse_http_context(
	php_stream_context *context,
	rp2350_http_request_opts_t *opts,
	php_stream_wrapper *wrapper,
	int options
)
{
	zval *tmp;

	if (!context) {
		return true;
	}

	tmp = php_stream_context_get_option(context, "http", "method");
	if (tmp != NULL) {
		char *method = NULL;
		size_t method_len = 0;
		size_t i;

		if (!rp2350_strdup_from_zval(tmp, &method, &method_len)) {
			php_stream_wrapper_log_error(wrapper, options, "context parse failed: method");
			return false;
		}
		if (method_len == 0 || method_len >= sizeof(opts->method)) {
			free(method);
			php_stream_wrapper_log_error(wrapper, options, "context invalid method");
			return false;
		}
		for (i = 0; i < method_len; i++) {
			opts->method[i] = (char)toupper((unsigned char)method[i]);
		}
		opts->method[method_len] = '\0';
		free(method);
	}

	tmp = php_stream_context_get_option(context, "http", "timeout");
	if (tmp != NULL) {
		double timeout_s = zval_get_double(tmp);
		if (timeout_s > 0.0) {
			double timeout_ms_d = timeout_s * 1000.0;
			if (timeout_ms_d < 100.0) {
				timeout_ms_d = 100.0;
			} else if (timeout_ms_d > 120000.0) {
				timeout_ms_d = 120000.0;
			}
			opts->timeout_ms = (uint32_t)timeout_ms_d;
		}
	}

	tmp = php_stream_context_get_option(context, "http", "user_agent");
	if (tmp != NULL) {
		size_t user_agent_len = 0;
		if (!rp2350_strdup_from_zval(tmp, &opts->user_agent, &user_agent_len)) {
			php_stream_wrapper_log_error(wrapper, options, "context parse failed: user_agent");
			return false;
		}
	}

	tmp = php_stream_context_get_option(context, "http", "content");
	if (tmp != NULL) {
		if (!rp2350_strdup_from_zval(tmp, &opts->content, &opts->content_len)) {
			php_stream_wrapper_log_error(wrapper, options, "context parse failed: content");
			return false;
		}
	}

	tmp = php_stream_context_get_option(context, "http", "header");
	if (tmp != NULL) {
		if (Z_TYPE_P(tmp) == IS_ARRAY) {
			zval *entry;
			smart_str hdr = {0};
			ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(tmp), entry) {
				zend_string *line = zval_get_string(entry);
				if (!line) {
					smart_str_free(&hdr);
					php_stream_wrapper_log_error(wrapper, options, "context parse failed: header array");
					return false;
				}
				smart_str_appendl(&hdr, ZSTR_VAL(line), ZSTR_LEN(line));
				smart_str_appendl(&hdr, "\r\n", 2);
				zend_string_release(line);
			} ZEND_HASH_FOREACH_END();
			if (hdr.s) {
				smart_str_0(&hdr);
				opts->headers_len = ZSTR_LEN(hdr.s);
				opts->headers = (char *)malloc(opts->headers_len + 1);
				if (!opts->headers) {
					smart_str_free(&hdr);
					php_stream_wrapper_log_error(wrapper, options, "context alloc failed: headers");
					return false;
				}
				memcpy(opts->headers, ZSTR_VAL(hdr.s), opts->headers_len + 1);
				smart_str_free(&hdr);
			}
		} else {
			if (!rp2350_strdup_from_zval(tmp, &opts->headers, &opts->headers_len)) {
				php_stream_wrapper_log_error(wrapper, options, "context parse failed: header");
				return false;
			}
		}
	}

	return true;
}

static bool rp2350_header_name_eq_ci(const char *name, size_t len, const char *lit)
{
	size_t i;
	size_t lit_len = strlen(lit);
	if (len != lit_len) {
		return false;
	}
	for (i = 0; i < len; i++) {
		if ((char)tolower((unsigned char)name[i]) != lit[i]) {
			return false;
		}
	}
	return true;
}

static bool rp2350_h2_header_forbidden(const char *name, size_t len)
{
	return rp2350_header_name_eq_ci(name, len, "connection")
		|| rp2350_header_name_eq_ci(name, len, "proxy-connection")
		|| rp2350_header_name_eq_ci(name, len, "keep-alive")
		|| rp2350_header_name_eq_ci(name, len, "transfer-encoding")
		|| rp2350_header_name_eq_ci(name, len, "upgrade")
		|| rp2350_header_name_eq_ci(name, len, "host")
		|| rp2350_header_name_eq_ci(name, len, "content-length");
}

static size_t rp2350_h2_append_context_headers(char *headers, nghttp2_nv *nva, size_t nvlen, size_t nvcap)
{
	char *p = headers;
	while (p && *p) {
		char *line_end = strchr(p, '\n');
		char *line = p;
		char *colon;
		char *name_start;
		char *name_end;
		char *value_start;
		char *value_end;
		size_t name_len;
		size_t value_len;
		size_t i;

		if (line_end) {
			*line_end = '\0';
			p = line_end + 1;
		} else {
			p = NULL;
		}

		while (*line == ' ' || *line == '\t' || *line == '\r') {
			line++;
		}
		if (*line == '\0') {
			continue;
		}

		colon = strchr(line, ':');
		if (!colon) {
			continue;
		}
		*colon = '\0';
		name_start = line;
		name_end = colon - 1;
		while (name_end >= name_start && (*name_end == ' ' || *name_end == '\t')) {
			*name_end-- = '\0';
		}
		if (name_end < name_start) {
			continue;
		}
		value_start = colon + 1;
		while (*value_start == ' ' || *value_start == '\t') {
			value_start++;
		}
		value_end = value_start + strlen(value_start);
		while (value_end > value_start && (value_end[-1] == '\r' || value_end[-1] == ' ' || value_end[-1] == '\t')) {
			value_end--;
		}
		*value_end = '\0';

		name_len = strlen(name_start);
		value_len = (size_t)(value_end - value_start);
		if (name_len == 0 || value_len == 0) {
			continue;
		}
		if (rp2350_h2_header_forbidden(name_start, name_len)) {
			continue;
		}

		for (i = 0; i < name_len; i++) {
			name_start[i] = (char)tolower((unsigned char)name_start[i]);
		}

		if (nvlen >= nvcap) {
			break;
		}
		nva[nvlen].name = (uint8_t *)name_start;
		nva[nvlen].value = (uint8_t *)value_start;
		nva[nvlen].namelen = name_len;
		nva[nvlen].valuelen = value_len;
		nva[nvlen].flags = NGHTTP2_NV_FLAG_NONE;
		nvlen++;
	}
	return nvlen;
}

static bool rp2350_http_build_request(
	const char *host,
	const char *path,
	const rp2350_http_request_opts_t *opts,
	char **req_out,
	size_t *req_len_out
)
{
	zend_string *zs;
	smart_str req = {0};
	char cl_buf[48];
	int cl_n;
	const char *user_agent = (opts && opts->user_agent && opts->user_agent[0] != '\0')
		? opts->user_agent
		: "rp2350-php";
	const char *method = (opts && opts->method[0] != '\0') ? opts->method : "GET";

	if (!host || !path || !req_out || !req_len_out) {
		return false;
	}

	smart_str_appends(&req, method);
	smart_str_appends(&req, " ");
	smart_str_appends(&req, path);
	smart_str_appends(&req, " HTTP/1.1\r\nHost: ");
	smart_str_appends(&req, host);
	smart_str_appends(&req, "\r\nConnection: close\r\nUser-Agent: ");
	smart_str_appends(&req, user_agent);
	smart_str_appends(&req, "\r\nAccept: */*\r\n");

	if (opts && opts->content_len > 0) {
		cl_n = snprintf(cl_buf, sizeof(cl_buf), "Content-Length: %u\r\n", (unsigned)opts->content_len);
		if (cl_n <= 0 || cl_n >= (int)sizeof(cl_buf)) {
			smart_str_free(&req);
			return false;
		}
		smart_str_appendl(&req, cl_buf, (size_t)cl_n);
	}

	if (opts && opts->headers && opts->headers_len > 0) {
		smart_str_appendl(&req, opts->headers, opts->headers_len);
		if (opts->headers[opts->headers_len - 1] != '\n') {
			smart_str_appendl(&req, "\r\n", 2);
		}
	}

	smart_str_appendl(&req, "\r\n", 2);
	if (opts && opts->content && opts->content_len > 0) {
		smart_str_appendl(&req, opts->content, opts->content_len);
	}

	smart_str_0(&req);
	zs = smart_str_extract(&req);
	if (!zs) {
		return false;
	}
	*req_len_out = ZSTR_LEN(zs);
	*req_out = (char *)malloc(*req_len_out + 1);
	if (!*req_out) {
		zend_string_release(zs);
		return false;
	}
	memcpy(*req_out, ZSTR_VAL(zs), *req_len_out + 1);
	zend_string_release(zs);
	return true;
}

static bool rp2350_http_h1_extract_body(
	const char *resp,
	size_t resp_len,
	size_t max_read,
	char **body_out,
	size_t *body_len_out
)
{
	int minor_version = 0;
	int status = 0;
	const char *msg = NULL;
	size_t msg_len = 0;
	struct phr_header headers[48];
	size_t num_headers = sizeof(headers) / sizeof(headers[0]);
	int parsed;
	char *body;
	size_t body_len;

	*body_out = NULL;
	*body_len_out = 0;
	parsed = phr_parse_response(
		resp,
		resp_len,
		&minor_version,
		&status,
		&msg,
		&msg_len,
		headers,
		&num_headers,
		0
	);
	if (parsed <= 0) {
		php_error_docref(NULL, E_WARNING, "http parse failed: parsed=%d", parsed);
		return false;
	}
	if (status < 100 || status > 599) {
		php_error_docref(NULL, E_WARNING, "http parse status invalid: %d", status);
		return false;
	}
	body_len = resp_len - (size_t)parsed;
	if (body_len > max_read) {
		body_len = max_read;
	}
	body = (char *)malloc(body_len + 1);
	if (!body) {
		return false;
	}
	memcpy(body, resp + parsed, body_len);
	body[body_len] = '\0';
	*body_out = body;
	*body_len_out = body_len;
	return true;
}

static bool rp2350_net_tls_request_http1(
	const char *host,
	u16_t port,
	const char *payload,
	size_t payload_len,
	uint32_t timeout_ms,
	size_t max_read,
	char **out,
	size_t *out_len
)
{
	ip_addr_t remote;
	rp2350_altcp_ctx_t ctx;
	absolute_time_t deadline;
	size_t off = 0;
	altcp_allocator_t tls_allocator;
	rp2350_tls_alloc_ctx_t tls_ctx;

	*out = NULL;
	*out_len = 0;

	if (!rp2350_wifi_init_once()) {
		return false;
	}
	if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
		return false;
	}
	if (!rp2350_http_tls_config_ready()) {
		return false;
	}
	if (!rp2350_dns_resolve(host, timeout_ms, &remote)) {
		return false;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.rx_cap = max_read;
	ctx.rx = (char *)malloc(ctx.rx_cap + 1);
	if (ctx.rx == NULL) {
		return false;
	}
	ctx.last_rx_time = get_absolute_time();

	tls_ctx.config = s_http_tls_config;
	tls_ctx.hostname = host;
	tls_allocator.alloc = rp2350_altcp_tls_alloc_with_sni;
	tls_allocator.arg = &tls_ctx;
	deadline = make_timeout_time_ms(timeout_ms);

	cyw43_arch_lwip_begin();
	ctx.pcb = altcp_new_ip_type(&tls_allocator, IP_GET_TYPE(&remote));
	if (ctx.pcb != NULL) {
		altcp_arg(ctx.pcb, &ctx);
		altcp_recv(ctx.pcb, rp2350_h2_altcp_recv_cb);
		altcp_err(ctx.pcb, rp2350_h2_altcp_err_cb);
		ctx.last_err = altcp_connect(ctx.pcb, &remote, port, rp2350_h2_altcp_connected_cb);
		if (ctx.last_err != ERR_OK) {
			altcp_arg(ctx.pcb, NULL);
			altcp_recv(ctx.pcb, NULL);
			altcp_err(ctx.pcb, NULL);
			altcp_abort(ctx.pcb);
			ctx.pcb = NULL;
		}
	}
	cyw43_arch_lwip_end();
	if (ctx.pcb == NULL) {
		free(ctx.rx);
		return false;
	}

	if (!rp2350_lwip_wait_until(deadline, &ctx.connected) || ctx.had_error) {
		rp2350_h2_altcp_close(&ctx);
		free(ctx.rx);
		return false;
	}

	while (off < payload_len) {
		bool wrote = false;
		cyw43_arch_lwip_begin();
		if (ctx.pcb != NULL) {
			u16_t snd = altcp_sndbuf(ctx.pcb);
			if (snd > 0) {
				u16_t chunk = (u16_t)((payload_len - off) < snd ? (payload_len - off) : snd);
				err_t w = altcp_write(ctx.pcb, payload + off, chunk, TCP_WRITE_FLAG_COPY);
				if (w == ERR_OK) {
					off += chunk;
					(void)altcp_output(ctx.pcb);
					wrote = true;
				} else if (w != ERR_MEM) {
					ctx.had_error = true;
					ctx.last_err = w;
				}
			}
		}
		cyw43_arch_lwip_end();
		if (ctx.had_error || ctx.pcb == NULL) {
			rp2350_h2_altcp_close(&ctx);
			free(ctx.rx);
			return false;
		}
		if (!wrote) {
			if (time_reached(deadline)) {
				rp2350_h2_altcp_close(&ctx);
				free(ctx.rx);
				return false;
			}
			sleep_ms(1);
		}
	}

	cyw43_arch_lwip_begin();
	if (ctx.pcb != NULL) {
		(void)altcp_shutdown(ctx.pcb, 0, 1);
	}
	cyw43_arch_lwip_end();

	while (!ctx.done && !ctx.had_error) {
		if (time_reached(deadline)) {
			break;
		}
		sleep_ms(1);
	}

	rp2350_h2_altcp_close(&ctx);
	ctx.rx[ctx.rx_len] = '\0';
	*out = ctx.rx;
	*out_len = ctx.rx_len;
	return true;
}

bool rp2350_http_get_body(const char *url, uint32_t timeout_ms, size_t max_read, char **body_out, size_t *body_len_out)
{
	char host[96];
	u16_t port;
	const char *path;
	bool is_https = false;
	rp2350_http_request_opts_t req_opts;
	char *request = NULL;
	size_t request_len = 0;
	char *response = NULL;
	size_t response_len = 0;
	bool h2_alpn_http1 = false;

	*body_out = NULL;
	*body_len_out = 0;

	if (!rp2350_parse_http_url(url, host, sizeof(host), &port, &path, &is_https)) {
		php_error_docref(NULL, E_WARNING, "http url parse failed: %s", url ? url : "<null>");
		return false;
	}
	rp2350_http_request_opts_init(&req_opts);

	if (is_https) {
		if (rp2350_h2_get_body_ex(url, timeout_ms, max_read, body_out, body_len_out, &h2_alpn_http1)) {
			return true;
		}
		if (!h2_alpn_http1) {
			return false;
		}
	}

	if (!rp2350_http_build_request(host, path, &req_opts, &request, &request_len)) {
		php_error_docref(NULL, E_WARNING, "http request build failed");
		return false;
	}

	if (is_https) {
		if (!rp2350_net_tls_request_http1(host, port, request, request_len, timeout_ms, max_read + 8192, &response, &response_len)) {
			free(request);
			return false;
		}
	} else {
		if (!rp2350_net_tcp_request(host, port, request, request_len, timeout_ms, max_read + 8192, &response, &response_len)) {
			free(request);
			return false;
		}
	}
	free(request);

	if (!rp2350_http_h1_extract_body(response, response_len, max_read, body_out, body_len_out)) {
		free(response);
		return false;
	}
	free(response);
	return true;
}

typedef enum {
	RP2350_HTTP_STREAM_MODE_H1 = 0,
	RP2350_HTTP_STREAM_MODE_H2 = 1,
} rp2350_http_stream_mode_t;

typedef struct {
	rp2350_altcp_ctx_t net;
	uint32_t timeout_ms;
	rp2350_http_stream_mode_t mode;
	bool headers_done;
	bool response_parsed;
	nghttp2_session_callbacks *h2_callbacks;
	nghttp2_session *h2_session;
	rp2350_h2_ctx_t h2;
} rp2350_http_stream_data_t;

static ssize_t rp2350_http_stream_write(php_stream *stream, const char *buf, size_t count)
{
	(void)stream;
	(void)buf;
	(void)count;
	return -1;
}

static ssize_t rp2350_http_stream_read(php_stream *stream, char *buf, size_t count)
{
	rp2350_http_stream_data_t *st = (rp2350_http_stream_data_t *)stream->abstract;
	absolute_time_t deadline;

	if (!st || count == 0) {
		return 0;
	}

	deadline = make_timeout_time_ms(st->timeout_ms);

	if (st->mode == RP2350_HTTP_STREAM_MODE_H2) {
		while (true) {
			bool done = false;
			bool had_error = false;
			ssize_t parsed = 0;

			if (st->h2.body_len > 0) {
				size_t take = (count < st->h2.body_len) ? count : st->h2.body_len;
				memcpy(buf, st->h2.body, take);
				if (take < st->h2.body_len) {
					memmove(st->h2.body, st->h2.body + take, st->h2.body_len - take);
				}
				st->h2.body_len -= take;
				return (ssize_t)take;
			}
			if (st->h2.stream_closed) {
				stream->eof = 1;
				return 0;
			}

			cyw43_arch_lwip_begin();
			done = st->net.done;
			had_error = st->net.had_error;
			if (st->net.rx_len > 0) {
				parsed = nghttp2_session_mem_recv(
					st->h2_session,
					(const uint8_t *)st->net.rx,
					st->net.rx_len
				);
				if (parsed >= 0) {
					size_t used = (size_t)parsed;
					if (used < st->net.rx_len) {
						memmove(st->net.rx, st->net.rx + used, st->net.rx_len - used);
					}
					st->net.rx_len -= used;
				}
			}
			cyw43_arch_lwip_end();

			if (parsed < 0) {
				stream->eof = 1;
				return 0;
			}
			/* Parsed frames may have produced body bytes even if FIN is also seen. */
			if (st->h2.body_len > 0) {
				continue;
			}
			if (had_error) {
				stream->eof = 1;
				return 0;
			}
			if (done && st->net.rx_len == 0) {
				stream->eof = 1;
				return 0;
			}

			if (parsed > 0) {
				int rc = nghttp2_session_send(st->h2_session);
				if (rc != 0) {
					stream->eof = 1;
					return 0;
				}
				if (!rp2350_h2_flush_tx(&st->h2, &st->net, deadline)) {
					stream->eof = 1;
					return 0;
				}
				continue;
			}

			if (time_reached(deadline)) {
				stream->eof = 1;
				return 0;
			}
			sleep_ms(1);
		}
	}

	while (true) {
		size_t rx_len = 0;
		bool done = false;
		bool had_error = false;

		cyw43_arch_lwip_begin();
		rx_len = st->net.rx_len;
		done = st->net.done;
		had_error = st->net.had_error;

		if (!st->headers_done && rx_len > 0) {
			size_t i;
			size_t parsed = 0;
			for (i = 0; i + 3 < rx_len; i++) {
				if (st->net.rx[i] == '\r' && st->net.rx[i + 1] == '\n' && st->net.rx[i + 2] == '\r' && st->net.rx[i + 3] == '\n') {
					parsed = i + 4;
					break;
				}
			}
			if (parsed > 0) {
				if (!st->response_parsed) {
					int minor_version = 0;
					int status = 0;
					const char *msg = NULL;
					size_t msg_len = 0;
					struct phr_header headers[48];
					size_t num_headers = sizeof(headers) / sizeof(headers[0]);
					int rc = phr_parse_response(
						st->net.rx,
						parsed,
						&minor_version,
						&status,
						&msg,
						&msg_len,
						headers,
						&num_headers,
						0
					);
					if (rc <= 0) {
						st->net.had_error = true;
						cyw43_arch_lwip_end();
						stream->eof = 1;
						return 0;
					}
					st->response_parsed = true;
				}
				if (parsed < st->net.rx_len) {
					memmove(st->net.rx, st->net.rx + parsed, st->net.rx_len - parsed);
				}
				st->net.rx_len -= parsed;
				st->headers_done = true;
				rx_len = st->net.rx_len;
			}
		}

		if (st->headers_done && rx_len > 0) {
			size_t take = (count < rx_len) ? count : rx_len;
			memcpy(buf, st->net.rx, take);
			if (take < st->net.rx_len) {
				memmove(st->net.rx, st->net.rx + take, st->net.rx_len - take);
			}
			st->net.rx_len -= take;
			cyw43_arch_lwip_end();
			return (ssize_t)take;
		}
		cyw43_arch_lwip_end();

		if (had_error || (done && st->headers_done)) {
			stream->eof = 1;
			return 0;
		}
		if (done && !st->headers_done) {
			stream->eof = 1;
			return 0;
		}
		if (time_reached(deadline)) {
			stream->eof = 1;
			return 0;
		}
		sleep_ms(1);
	}
}

static int rp2350_http_stream_close(php_stream *stream, int close_handle)
{
	rp2350_http_stream_data_t *st = (rp2350_http_stream_data_t *)stream->abstract;
	(void)close_handle;
	if (!st) {
		return 0;
	}
	rp2350_h2_altcp_close(&st->net);
	if (st->net.rx) {
		free(st->net.rx);
		st->net.rx = NULL;
	}
	if (st->h2_session) {
		nghttp2_session_del(st->h2_session);
		st->h2_session = NULL;
	}
	if (st->h2_callbacks) {
		nghttp2_session_callbacks_del(st->h2_callbacks);
		st->h2_callbacks = NULL;
	}
	free(st->h2.tx);
	st->h2.tx = NULL;
	free(st->h2.body);
	st->h2.body = NULL;
	free(st->h2.req_body);
	st->h2.req_body = NULL;
	efree(st);
	stream->abstract = NULL;
	return 0;
}

static int rp2350_http_stream_flush(php_stream *stream)
{
	(void)stream;
	return 0;
}

static int rp2350_http_stream_seek(php_stream *stream, zend_off_t offset, int whence, zend_off_t *newoffs)
{
	(void)stream;
	(void)offset;
	(void)whence;
	(void)newoffs;
	return -1;
}

static const php_stream_ops rp2350_http_stream_ops = {
	rp2350_http_stream_write,
	rp2350_http_stream_read,
	rp2350_http_stream_close,
	rp2350_http_stream_flush,
	"rp2350_http_stream",
	rp2350_http_stream_seek,
	NULL,
	NULL,
	NULL
};

static php_stream *rp2350_http_stream_opener(
	php_stream_wrapper *wrapper,
	const char *filename,
	const char *mode,
	int options,
	zend_string **opened_path,
	php_stream_context *context STREAMS_DC
)
{
	char host[96];
	u16_t port;
	const char *path;
	bool is_https = false;
	ip_addr_t remote;
	absolute_time_t deadline;
	rp2350_http_stream_data_t *st = NULL;
	altcp_allocator_t tls_allocator = {0};
	rp2350_h2_tls_alloc_ctx_t tls_ctx = {0};
	php_stream *stream = NULL;
	const char *alpn = NULL;
	int connect_attempt = 0;
	int connect_attempts = 1;
	rp2350_http_request_opts_t req_opts;

	rp2350_http_request_opts_init(&req_opts);
#define RP2350_HTTP_OPEN_FAIL() do { rp2350_http_request_opts_cleanup(&req_opts); return NULL; } while (0)

	if (!filename || (strncmp(filename, "http://", 7) != 0 && strncmp(filename, "https://", 8) != 0)) {
		RP2350_HTTP_OPEN_FAIL();
	}
	if (!mode || mode[0] != 'r' || mode[1] == '+') {
		if (options & REPORT_ERRORS) {
			php_error_docref(NULL, E_WARNING, "rp2350 http wrapper is read-only");
		}
		RP2350_HTTP_OPEN_FAIL();
	}

	if (!rp2350_parse_http_url(filename, host, sizeof(host), &port, &path, &is_https)) {
		php_stream_wrapper_log_error(wrapper, options, "url parse failed: %s", filename ? filename : "<null>");
		RP2350_HTTP_OPEN_FAIL();
	}
	if (!rp2350_parse_http_context(context, &req_opts, wrapper, options)) {
		RP2350_HTTP_OPEN_FAIL();
	}

	if (!rp2350_wifi_init_once()) {
		php_stream_wrapper_log_error(wrapper, options, "wifi init failed");
		RP2350_HTTP_OPEN_FAIL();
	}
	if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
		php_stream_wrapper_log_error(wrapper, options, "wifi not connected");
		RP2350_HTTP_OPEN_FAIL();
	}
	if (!rp2350_dns_resolve(host, req_opts.timeout_ms, &remote)) {
		php_stream_wrapper_log_error(wrapper, options, "dns resolve failed: %s", host);
		RP2350_HTTP_OPEN_FAIL();
	}

	if (is_https && !rp2350_h2_tls_config_ready()) {
		php_stream_wrapper_log_error(wrapper, options, "https tls config not ready");
		RP2350_HTTP_OPEN_FAIL();
	}

	st = (rp2350_http_stream_data_t *)ecalloc(1, sizeof(*st));
	if (!st) {
		php_stream_wrapper_log_error(wrapper, options, "state alloc failed");
		RP2350_HTTP_OPEN_FAIL();
	}
	st->timeout_ms = req_opts.timeout_ms;
	st->net.rx_cap = 32768;
	st->net.rx = (char *)malloc(st->net.rx_cap + 1);
	if (!st->net.rx) {
		php_stream_wrapper_log_error(wrapper, options, "rx alloc failed");
		efree(st);
		RP2350_HTTP_OPEN_FAIL();
	}
	st->net.last_rx_time = get_absolute_time();
	st->mode = RP2350_HTTP_STREAM_MODE_H1;
	connect_attempts = is_https ? 2 : 1;

	if (is_https) {
		tls_ctx.config = s_h2_tls_config;
		tls_ctx.hostname = host;
		tls_allocator.alloc = rp2350_h2_tls_alloc_with_sni;
		tls_allocator.arg = &tls_ctx;
	}

	for (connect_attempt = 0; connect_attempt < connect_attempts; connect_attempt++) {
		deadline = make_timeout_time_ms(st->timeout_ms);
		st->net.connected = false;
		st->net.had_error = false;
		st->net.done = false;
		st->net.last_err = ERR_OK;
		st->net.pcb = NULL;

		cyw43_arch_lwip_begin();
		st->net.pcb = altcp_new_ip_type(is_https ? &tls_allocator : NULL, IP_GET_TYPE(&remote));
		if (st->net.pcb != NULL) {
			altcp_arg(st->net.pcb, &st->net);
			altcp_recv(st->net.pcb, rp2350_h2_altcp_recv_cb);
			altcp_err(st->net.pcb, rp2350_h2_altcp_err_cb);
			st->net.last_err = altcp_connect(st->net.pcb, &remote, port, rp2350_h2_altcp_connected_cb);
			if (st->net.last_err != ERR_OK) {
				altcp_arg(st->net.pcb, NULL);
				altcp_recv(st->net.pcb, NULL);
				altcp_err(st->net.pcb, NULL);
				altcp_abort(st->net.pcb);
				st->net.pcb = NULL;
			}
		}
		cyw43_arch_lwip_end();

		if (st->net.pcb != NULL && rp2350_lwip_wait_until(deadline, &st->net.connected) && !st->net.had_error) {
			break;
		}

		rp2350_h2_altcp_close(&st->net);
		if (connect_attempt + 1 < connect_attempts) {
			sleep_ms(50);
		}
	}
	if (st->net.pcb == NULL || !st->net.connected || st->net.had_error) {
		if (is_https && st->net.pcb != NULL) {
			const char *tls_alpn = NULL;
			const char *tls_ver = NULL;
			const char *tls_cipher = NULL;
			uint32_t tls_verify = 0;
			bool tls_ctx_ok = false;
			cyw43_arch_lwip_begin();
			{
				void *tls = altcp_tls_context(st->net.pcb);
				if (tls != NULL) {
					mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)tls;
					tls_ctx_ok = true;
					tls_alpn = mbedtls_ssl_get_alpn_protocol(ssl);
					tls_ver = mbedtls_ssl_get_version(ssl);
					tls_cipher = mbedtls_ssl_get_ciphersuite(ssl);
					tls_verify = mbedtls_ssl_get_verify_result(ssl);
				}
			}
			cyw43_arch_lwip_end();
			if (tls_ctx_ok) {
				php_stream_wrapper_log_error(
					wrapper,
					options,
					"connect timeout/error lwip=%d connected=%d had_error=%d verify=0x%08" PRIx32 " ver=%s cipher=%s alpn=%s",
					(int)st->net.last_err,
					st->net.connected ? 1 : 0,
					st->net.had_error ? 1 : 0,
					tls_verify,
					tls_ver ? tls_ver : "<null>",
					tls_cipher ? tls_cipher : "<null>",
					tls_alpn ? tls_alpn : "<null>"
				);
			} else {
				php_stream_wrapper_log_error(
					wrapper,
					options,
					"connect timeout/error lwip=%d connected=%d had_error=%d tls=<null>",
					(int)st->net.last_err,
					st->net.connected ? 1 : 0,
					st->net.had_error ? 1 : 0
				);
			}
		} else {
			php_stream_wrapper_log_error(
				wrapper,
				options,
				"connect timeout/error lwip=%d connected=%d had_error=%d",
				(int)st->net.last_err,
				st->net.connected ? 1 : 0,
				st->net.had_error ? 1 : 0
			);
		}
		rp2350_h2_altcp_close(&st->net);
		free(st->net.rx);
		efree(st);
		RP2350_HTTP_OPEN_FAIL();
	}

	if (is_https) {
		const char *tls_ver = NULL;
		const char *tls_cipher = NULL;
		cyw43_arch_lwip_begin();
		if (st->net.pcb != NULL) {
			void *tls = altcp_tls_context(st->net.pcb);
			if (tls != NULL) {
				mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)tls;
				alpn = mbedtls_ssl_get_alpn_protocol(ssl);
				tls_ver = mbedtls_ssl_get_version(ssl);
				tls_cipher = mbedtls_ssl_get_ciphersuite(ssl);
			}
		}
		cyw43_arch_lwip_end();
		printf(
			"[tls] connected ver=%s cipher=%s alpn=%s\n",
			tls_ver ? tls_ver : "<null>",
			tls_cipher ? tls_cipher : "<null>",
			alpn ? alpn : "<null>"
		);
			if (alpn != NULL && strcmp(alpn, "h2") == 0) {
				nghttp2_nv nva[64];
				size_t nvlen = 0;
				nghttp2_data_provider data_provider;
				nghttp2_data_provider *provider_ptr = NULL;
				int rc;

			st->mode = RP2350_HTTP_STREAM_MODE_H2;
			st->h2.body_dynamic = true;
			st->h2.body_limit = 131072;

			rc = nghttp2_session_callbacks_new(&st->h2_callbacks);
			if (rc != 0) {
				if (options & REPORT_ERRORS) {
					php_error_docref(NULL, E_WARNING, "https wrapper: h2 callbacks alloc failed: %d", rc);
				}
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				efree(st);
				RP2350_HTTP_OPEN_FAIL();
			}
			nghttp2_session_callbacks_set_send_callback(st->h2_callbacks, rp2350_h2_send_cb);
			nghttp2_session_callbacks_set_on_header_callback(st->h2_callbacks, rp2350_h2_on_header_cb);
			nghttp2_session_callbacks_set_on_data_chunk_recv_callback(st->h2_callbacks, rp2350_h2_on_data_chunk_recv_cb);
			nghttp2_session_callbacks_set_on_stream_close_callback(st->h2_callbacks, rp2350_h2_on_stream_close_cb);

			rc = nghttp2_session_client_new(&st->h2_session, st->h2_callbacks, &st->h2);
			if (rc != 0) {
				if (options & REPORT_ERRORS) {
					php_error_docref(NULL, E_WARNING, "https wrapper: h2 session alloc failed: %d", rc);
				}
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				nghttp2_session_callbacks_del(st->h2_callbacks);
				efree(st);
				RP2350_HTTP_OPEN_FAIL();
			}

			rc = nghttp2_submit_settings(st->h2_session, NGHTTP2_FLAG_NONE, NULL, 0);
			if (rc != 0) {
				if (options & REPORT_ERRORS) {
					php_error_docref(NULL, E_WARNING, "https wrapper: h2 submit settings failed: %d", rc);
				}
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				nghttp2_session_del(st->h2_session);
				nghttp2_session_callbacks_del(st->h2_callbacks);
				efree(st);
				RP2350_HTTP_OPEN_FAIL();
			}

			memset(nva, 0, sizeof(nva));
			nva[nvlen].name = (uint8_t *)":method";
			nva[nvlen].value = (uint8_t *)req_opts.method;
			nva[nvlen].namelen = sizeof(":method") - 1;
			nva[nvlen].valuelen = strlen(req_opts.method);
			nvlen++;

			nva[nvlen].name = (uint8_t *)":scheme";
			nva[nvlen].value = (uint8_t *)"https";
			nva[nvlen].namelen = sizeof(":scheme") - 1;
			nva[nvlen].valuelen = sizeof("https") - 1;
			nvlen++;

			nva[nvlen].name = (uint8_t *)":authority";
			nva[nvlen].value = (uint8_t *)host;
			nva[nvlen].namelen = sizeof(":authority") - 1;
			nva[nvlen].valuelen = strlen(host);
			nvlen++;

			nva[nvlen].name = (uint8_t *)":path";
			nva[nvlen].value = (uint8_t *)path;
			nva[nvlen].namelen = sizeof(":path") - 1;
			nva[nvlen].valuelen = strlen(path);
			nvlen++;

			nva[nvlen].name = (uint8_t *)"user-agent";
			nva[nvlen].value = (uint8_t *)((req_opts.user_agent && req_opts.user_agent[0] != '\0') ? req_opts.user_agent : "rp2350-php");
			nva[nvlen].namelen = sizeof("user-agent") - 1;
			nva[nvlen].valuelen = strlen((char *)nva[nvlen].value);
			nvlen++;

			if (req_opts.headers && req_opts.headers_len > 0) {
				nvlen = rp2350_h2_append_context_headers(req_opts.headers, nva, nvlen, sizeof(nva) / sizeof(nva[0]));
			}

			memset(&data_provider, 0, sizeof(data_provider));
			if (req_opts.content && req_opts.content_len > 0) {
				st->h2.req_body = (char *)malloc(req_opts.content_len);
				if (!st->h2.req_body) {
					if (options & REPORT_ERRORS) {
						php_error_docref(NULL, E_WARNING, "https wrapper: h2 request body alloc failed");
					}
					rp2350_h2_altcp_close(&st->net);
					free(st->net.rx);
					nghttp2_session_del(st->h2_session);
					nghttp2_session_callbacks_del(st->h2_callbacks);
					efree(st);
					RP2350_HTTP_OPEN_FAIL();
				}
				memcpy(st->h2.req_body, req_opts.content, req_opts.content_len);
				st->h2.req_body_len = req_opts.content_len;
				st->h2.req_body_off = 0;
				data_provider.source.ptr = &st->h2;
				data_provider.read_callback = rp2350_h2_read_data_cb;
				provider_ptr = &data_provider;
			}

			st->h2.stream_id = nghttp2_submit_request(st->h2_session, NULL, nva, nvlen, provider_ptr, NULL);
			if (st->h2.stream_id < 0) {
				if (options & REPORT_ERRORS) {
					php_error_docref(NULL, E_WARNING, "https wrapper: h2 submit request failed: %d", (int)st->h2.stream_id);
				}
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				nghttp2_session_del(st->h2_session);
				nghttp2_session_callbacks_del(st->h2_callbacks);
				efree(st);
				RP2350_HTTP_OPEN_FAIL();
			}

			rc = nghttp2_session_send(st->h2_session);
			if (rc != 0 || !rp2350_h2_flush_tx(&st->h2, &st->net, deadline)) {
				if (options & REPORT_ERRORS) {
					php_error_docref(NULL, E_WARNING, "https wrapper: h2 send failed: rc=%d lwip=%d", rc, (int)st->net.last_err);
				}
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				nghttp2_session_del(st->h2_session);
				nghttp2_session_callbacks_del(st->h2_callbacks);
				efree(st);
				RP2350_HTTP_OPEN_FAIL();
			}
		}
	}

	if (st->mode == RP2350_HTTP_STREAM_MODE_H1) {
		char *request = NULL;
		size_t request_len = 0;
		size_t off = 0;

			if (!rp2350_http_build_request(host, path, &req_opts, &request, &request_len)) {
				php_stream_wrapper_log_error(wrapper, options, "request build failed");
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				efree(st);
				RP2350_HTTP_OPEN_FAIL();
			}

		while (off < request_len) {
			bool wrote = false;
			cyw43_arch_lwip_begin();
			if (st->net.pcb != NULL) {
				u16_t snd = altcp_sndbuf(st->net.pcb);
				if (snd > 0) {
					u16_t chunk = (u16_t)((request_len - off) < snd ? (request_len - off) : snd);
					err_t w = altcp_write(st->net.pcb, request + off, chunk, TCP_WRITE_FLAG_COPY);
					if (w == ERR_OK) {
						off += chunk;
						(void)altcp_output(st->net.pcb);
						wrote = true;
					} else if (w != ERR_MEM) {
						st->net.had_error = true;
						st->net.last_err = w;
					}
				}
			}
			cyw43_arch_lwip_end();
			if (st->net.had_error || st->net.pcb == NULL) {
				php_stream_wrapper_log_error(wrapper, options, "request write failed lwip=%d", (int)st->net.last_err);
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				free(request);
				efree(st);
				RP2350_HTTP_OPEN_FAIL();
			}
			if (!wrote) {
				if (time_reached(deadline)) {
					php_stream_wrapper_log_error(wrapper, options, "request write timeout");
					rp2350_h2_altcp_close(&st->net);
					free(st->net.rx);
					free(request);
					efree(st);
					RP2350_HTTP_OPEN_FAIL();
				}
				sleep_ms(1);
			}
		}
		free(request);

		cyw43_arch_lwip_begin();
		if (st->net.pcb != NULL) {
			(void)altcp_shutdown(st->net.pcb, 0, 1);
		}
		cyw43_arch_lwip_end();
	}

	stream = php_stream_alloc(&rp2350_http_stream_ops, st, 0, mode);
	if (!stream) {
		php_stream_wrapper_log_error(wrapper, options, "stream alloc failed");
		rp2350_h2_altcp_close(&st->net);
		free(st->net.rx);
		efree(st);
		RP2350_HTTP_OPEN_FAIL();
	}
	stream->wrapper = wrapper;

	if (opened_path) {
		*opened_path = zend_string_init(filename, strlen(filename), 0);
	}
	rp2350_http_request_opts_cleanup(&req_opts);
#undef RP2350_HTTP_OPEN_FAIL
	return stream;
}

static const php_stream_wrapper_ops rp2350_http_wrapper_ops = {
	rp2350_http_stream_opener,
	NULL,
	NULL,
	NULL,
	NULL,
	"rp2350_http_wrapper",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

static php_stream_wrapper rp2350_http_wrapper = {
	&rp2350_http_wrapper_ops,
	NULL,
	1
};

void rp2350_register_https_wrapper_runtime(void)
{
	zend_string *protocol = zend_string_init("https", sizeof("https") - 1, 0);
	if (!protocol) {
		php_error_docref(NULL, E_WARNING, "failed to allocate https protocol string");
		return;
	}
	(void)php_unregister_url_stream_wrapper_volatile(protocol);
	if (php_register_url_stream_wrapper_volatile(protocol, &rp2350_http_wrapper) != SUCCESS) {
		php_error_docref(NULL, E_WARNING, "failed to register https stream wrapper");
	}
	zend_string_release(protocol);
}
