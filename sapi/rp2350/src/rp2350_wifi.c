#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/sntp.h"

#include "../rp2350_rtc.h"
#include "rp2350_http_stream.h"
#include "rp2350_wifi.h"

static bool s_wifi_init = false;
static volatile bool s_sntp_sync_seen = false;
static volatile uint32_t s_sntp_sync_count = 0;

bool rp2350_wifi_is_initialized(void)
{
	return s_wifi_init;
}

bool rp2350_wifi_init_once(void)
{
	if (s_wifi_init) {
		return true;
	}
	if (cyw43_arch_init() != 0) {
		return false;
	}
	cyw43_arch_enable_sta_mode();
	s_wifi_init = true;
	/* Prewarm HTTPS config early to reduce later allocation failures. */
	(void)rp2350_http_tls_config_ready();
	(void)rp2350_h2_tls_config_ready();
	return true;
}

void rp2350_sntp_set_system_time_us(uint32_t sec, uint32_t us)
{
	struct timeval tv;
	time_t t;

	tv.tv_sec = (time_t)sec;
	tv.tv_usec = (suseconds_t)us;
	if (settimeofday(&tv, NULL) == 0) {
		t = (time_t)sec;
		(void)rp2350_rtc_set_unix_time(t);
		s_sntp_sync_seen = true;
		s_sntp_sync_count++;
	}
}

bool rp2350_sntp_sync_once(const char *server, uint32_t timeout_ms)
{
	absolute_time_t deadline;
	uint32_t start_count;
	bool synced = false;

	if (timeout_ms < 100u) {
		timeout_ms = 100u;
	}
	deadline = make_timeout_time_ms(timeout_ms);

	s_sntp_sync_seen = false;
	start_count = s_sntp_sync_count;

	cyw43_arch_lwip_begin();
	sntp_stop();
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, (char *)server);
	sntp_init();
	cyw43_arch_lwip_end();

	while (!time_reached(deadline)) {
		if (s_sntp_sync_seen || s_sntp_sync_count != start_count) {
			synced = true;
			break;
		}
		sleep_ms(10);
	}

	cyw43_arch_lwip_begin();
	sntp_stop();
	cyw43_arch_lwip_end();

	return synced;
}
