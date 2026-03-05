#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip_addr.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include "mbedtls/ssl.h"
#include "picohttpparser.h"

#include "SAPI.h"

#include "rp2350_eval.h"
#include "rp2350_http_internal.h"
#include "rp2350_wifi.h"

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

bool rp2350_http_h1_extract_body(
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

bool rp2350_net_tls_request_http1(
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
