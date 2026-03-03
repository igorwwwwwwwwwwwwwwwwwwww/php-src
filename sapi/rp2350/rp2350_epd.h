#ifndef RP2350_EPD_H
#define RP2350_EPD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool rp2350_epd_init(void);
bool rp2350_epd_fill(bool black);
bool rp2350_epd_clear(bool black);
bool rp2350_epd_set_pixel(int x, int y, bool black);
bool rp2350_epd_update(void);
bool rp2350_epd_render_1bpp(const uint8_t *data, size_t len, int width, int height, int x, int y);

#ifdef __cplusplus
}
#endif

#endif /* RP2350_EPD_H */
