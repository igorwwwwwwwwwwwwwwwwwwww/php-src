#ifndef RP2350_TRANSPORT_H
#define RP2350_TRANSPORT_H

#include <stddef.h>

size_t rp2350_platform_write(const char *buf, size_t len);
void rp2350_platform_flush(void);
void rp2350_platform_log(const char *msg);
size_t rp2350_platform_readline(char *buf, size_t max_len);

#endif /* RP2350_TRANSPORT_H */
