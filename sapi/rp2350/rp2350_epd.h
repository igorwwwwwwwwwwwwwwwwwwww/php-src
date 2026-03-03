#ifndef RP2350_EPD_H
#define RP2350_EPD_H

#include <stdbool.h>

bool rp2350_epd_init(void);
bool rp2350_epd_fill(bool black);
bool rp2350_epd_clear(bool black);
bool rp2350_epd_set_pixel(int x, int y, bool black);
bool rp2350_epd_update(void);

#endif /* RP2350_EPD_H */
