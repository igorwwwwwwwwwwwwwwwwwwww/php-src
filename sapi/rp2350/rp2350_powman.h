#ifndef RP2350_POWMAN_H
#define RP2350_POWMAN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rp2350_powman_early_init(void);
void rp2350_powman_after_wake_init(void);
bool rp2350_powman_maybe_handle_long_press(void);
int rp2350_powman_sleep(void);
int rp2350_powman_shipping_mode(void);
uint8_t rp2350_powman_wake_reason(void);

#ifdef __cplusplus
}
#endif

#endif
