#ifndef RP2350_FAKE_ARPA_INET_H
#define RP2350_FAKE_ARPA_INET_H

#include <stdint.h>
#include <netinet/in.h>

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int inet_pton(int af, const char *src, void *dst);

in_addr_t inet_addr(const char *cp);
char *inet_ntoa(struct in_addr in);

#endif /* RP2350_FAKE_ARPA_INET_H */
