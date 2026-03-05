#ifndef RP2350_WIFI_H
#define RP2350_WIFI_H

#include <stdbool.h>
#include <stdint.h>

bool rp2350_wifi_is_initialized(void);
bool rp2350_wifi_init_once(void);

void rp2350_sntp_set_system_time_us(uint32_t sec, uint32_t us);
bool rp2350_sntp_sync_once(const char *server, uint32_t timeout_ms);

#endif /* RP2350_WIFI_H */
