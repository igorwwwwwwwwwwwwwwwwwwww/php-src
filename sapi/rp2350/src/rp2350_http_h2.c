#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip_addr.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include "mbedtls/ssl.h"
#include "nghttp2/nghttp2.h"

#include "SAPI.h"

#include "rp2350_http_internal.h"
#include "rp2350_wifi.h"

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

ssize_t rp2350_h2_send_cb(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data)
{
	rp2350_h2_ctx_t *ctx = (rp2350_h2_ctx_t *)user_data;
	(void)session;
	(void)flags;
	if (!rp2350_h2_buf_append(&ctx->tx, &ctx->tx_len, &ctx->tx_cap, data, length)) {
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	}
	return (ssize_t)length;
}

ssize_t rp2350_h2_read_data_cb(
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

int rp2350_h2_on_header_cb(nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name, size_t namelen, const uint8_t *value, size_t valuelen, uint8_t flags, void *user_data)
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

int rp2350_h2_on_data_chunk_recv_cb(nghttp2_session *session, uint8_t flags, int32_t stream_id, const uint8_t *data, size_t len, void *user_data)
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

int rp2350_h2_on_stream_close_cb(nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *user_data)
{
	rp2350_h2_ctx_t *ctx = (rp2350_h2_ctx_t *)user_data;
	(void)session;
	if (ctx && stream_id == ctx->stream_id) {
		ctx->stream_closed = true;
		ctx->stream_error_code = error_code;
	}
	return 0;
}

bool rp2350_h2_flush_tx(rp2350_h2_ctx_t *h2, rp2350_altcp_ctx_t *net, absolute_time_t deadline)
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

err_t rp2350_h2_altcp_connected_cb(void *arg, struct altcp_pcb *conn, err_t err)
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

err_t rp2350_h2_altcp_recv_cb(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err)
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

void rp2350_h2_altcp_err_cb(void *arg, err_t err)
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

void rp2350_h2_altcp_close(rp2350_altcp_ctx_t *ctx)
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

bool rp2350_h2_get_body_ex(
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
