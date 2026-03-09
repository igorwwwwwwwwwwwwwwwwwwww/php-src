#ifndef RP2350_TFT_H
#define RP2350_TFT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool rp2350_tft_init(void);
bool rp2350_tft_clear(uint16_t rgb565);
bool rp2350_tft_set_pixel(int x, int y, uint16_t rgb565);
bool rp2350_tft_backlight(uint16_t level);
bool rp2350_tft_render_rgb565_bytes(const uint8_t *data, size_t data_len, int width, int height, int x, int y);
int rp2350_tft_width(void);
int rp2350_tft_height(void);

#ifdef __cplusplus
}
#endif

#endif
