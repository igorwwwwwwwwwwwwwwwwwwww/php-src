#include "rp2350_epd.h"

#include <cstdint>
#include "third_party/ssd1680/ssd1680.hpp"

namespace {
constexpr int EPD_WIDTH = 264;
constexpr int EPD_HEIGHT = 176;
constexpr uint32_t PIXEL_BLACK = 0x00000000u;
constexpr uint32_t PIXEL_WHITE = 0x00FFFFFFu;

pimoroni::SSD1680* s_display = nullptr;

bool ensure_display() {
  if (!s_display) {
    s_display = new pimoroni::SSD1680();
    if (!s_display) {
      return false;
    }
  }
  return true;
}

uint32_t* fb() {
  return s_display->get_framebuffer();
}

inline uint32_t color_for(bool black) {
  return black ? PIXEL_BLACK : PIXEL_WHITE;
}
}  // namespace

extern "C" {

bool rp2350_epd_init(void) {
  return ensure_display();
}

bool rp2350_epd_clear(bool black) {
  int i;
  uint32_t color;

  if (!ensure_display()) {
    return false;
  }

  color = color_for(black);
  for (i = 0; i < EPD_WIDTH * EPD_HEIGHT; i++) {
    fb()[i] = color;
  }
  return true;
}

bool rp2350_epd_set_pixel(int x, int y, bool black) {
  if (!ensure_display()) {
    return false;
  }
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
    return false;
  }

  fb()[x + y * EPD_WIDTH] = color_for(black);
  return true;
}

bool rp2350_epd_update(void) {
  if (!ensure_display()) {
    return false;
  }
  s_display->update();
  return true;
}

bool rp2350_epd_render_1bpp(const uint8_t* data, size_t len, int width, int height, int x0, int y0) {
  int x;
  int y;
  size_t stride;

  if (!ensure_display() || !data) {
    return false;
  }
  if (width <= 0 || height <= 0) {
    return false;
  }

  stride = (size_t)(width + 7) / 8u;
  if (len < stride * (size_t)height) {
    return false;
  }

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      size_t idx = (size_t)y * stride + (size_t)(x / 8);
      uint8_t mask = (uint8_t)(1u << (7u - ((uint8_t)x & 7u)));
      if (data[idx] & mask) {
        rp2350_epd_set_pixel(x0 + x, y0 + y, true);
      }
    }
  }

  return true;
}

bool rp2350_epd_fill(bool black) {
  return rp2350_epd_clear(black) && rp2350_epd_update();
}

}  // extern "C"
