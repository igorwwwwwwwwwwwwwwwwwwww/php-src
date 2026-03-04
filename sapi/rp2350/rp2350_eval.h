#ifndef RP2350_EVAL_H
#define RP2350_EVAL_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

int rp2350_eval_startup(void);
int rp2350_eval_execute(const char *code, size_t len);
int rp2350_eval_execute_file(const char *path);
const char *rp2350_eval_last_error(void);
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

#endif /* RP2350_EVAL_H */
