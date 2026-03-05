#ifndef RP2350_HTTP_STREAM_H
#define RP2350_HTTP_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool rp2350_http_tls_config_ready(void);
bool rp2350_h2_tls_config_ready(void);

bool rp2350_net_tcp_request(
	const char *host,
	uint16_t port,
	const char *payload,
	size_t payload_len,
	uint32_t timeout_ms,
	size_t max_read,
	char **out,
	size_t *out_len
);

bool rp2350_http_get_body(
	const char *url,
	uint32_t timeout_ms,
	size_t max_read,
	char **body_out,
	size_t *body_len_out
);

void rp2350_register_https_wrapper_runtime(void);

#endif /* RP2350_HTTP_STREAM_H */
