#ifndef RP2350_RTC_H
#define RP2350_RTC_H

#include <stdbool.h>
#include <time.h>

bool rp2350_rtc_get_unix_time(time_t *out_time);
bool rp2350_rtc_sync_system_time(void);
bool rp2350_rtc_set_unix_time(time_t unix_time);

#endif /* RP2350_RTC_H */
