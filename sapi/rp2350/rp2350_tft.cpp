#include "rp2350_tft.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

namespace {
constexpr int TFT_WIDTH = 320;
constexpr int TFT_HEIGHT = 240;

constexpr uint PIN_BACKLIGHT = BW_LCD_BACKLIGHT;
constexpr uint PIN_LCD_CS = BW_LCD_CS;
constexpr uint PIN_LCD_DC = BW_LCD_DC;
constexpr uint PIN_LCD_WR = BW_LCD_WR;
constexpr uint PIN_LCD_RD = BW_LCD_RD;
constexpr uint PIN_LCD_D0 = BW_LCD_D0;

constexpr uint8_t MADCTL_ROW_ORDER = 0x80;
constexpr uint8_t MADCTL_COL_ORDER = 0x40;
constexpr uint8_t MADCTL_SWAP_XY = 0x20;
constexpr uint8_t MADCTL_SCAN_ORDER = 0x10;

bool s_inited = false;

static inline void data_bus_write(uint8_t value) {
  for (uint i = 0; i < 8; i++) {
    gpio_put(PIN_LCD_D0 + i, (value >> i) & 1u);
  }
}

static inline void wr_strobe() {
  gpio_put(PIN_LCD_WR, 0);
  asm volatile("nop\n nop\n nop\n");
  gpio_put(PIN_LCD_WR, 1);
}

static inline void write8(uint8_t value) {
  data_bus_write(value);
  wr_strobe();
}

static inline void cs_select() {
  gpio_put(PIN_LCD_CS, 0);
}

static inline void cs_deselect() {
  gpio_put(PIN_LCD_CS, 1);
}

static inline void dc_command() {
  gpio_put(PIN_LCD_DC, 0);
}

static inline void dc_data() {
  gpio_put(PIN_LCD_DC, 1);
}

static void write_command(uint8_t cmd) {
  cs_select();
  dc_command();
  write8(cmd);
  cs_deselect();
}

static void write_data_bytes(const uint8_t *data, size_t len) {
  size_t i;
  cs_select();
  dc_data();
  for (i = 0; i < len; i++) {
    write8(data[i]);
  }
  cs_deselect();
}

static void write_command_data(uint8_t cmd, const uint8_t *data, size_t len) {
  write_command(cmd);
  if (data != NULL && len > 0) {
    write_data_bytes(data, len);
  }
}

static void set_backlight_raw(uint16_t level) {
  uint slice = pwm_gpio_to_slice_num(PIN_BACKLIGHT);
  uint chan = pwm_gpio_to_channel(PIN_BACKLIGHT);
  pwm_set_chan_level(slice, chan, level);
}

static void set_window(int x, int y, int w, int h) {
  uint16_t x0 = (uint16_t)x;
  uint16_t x1 = (uint16_t)(x + w - 1);
  uint16_t y0 = (uint16_t)y;
  uint16_t y1 = (uint16_t)(y + h - 1);
  uint8_t buf[4];

  buf[0] = (uint8_t)(x0 >> 8);
  buf[1] = (uint8_t)(x0 & 0xff);
  buf[2] = (uint8_t)(x1 >> 8);
  buf[3] = (uint8_t)(x1 & 0xff);
  write_command_data(0x2A, buf, sizeof(buf));

  buf[0] = (uint8_t)(y0 >> 8);
  buf[1] = (uint8_t)(y0 & 0xff);
  buf[2] = (uint8_t)(y1 >> 8);
  buf[3] = (uint8_t)(y1 & 0xff);
  write_command_data(0x2B, buf, sizeof(buf));

  write_command(0x2C);
}

static void common_init_pins() {
  uint i;

  gpio_init(PIN_LCD_CS);
  gpio_set_dir(PIN_LCD_CS, GPIO_OUT);
  gpio_put(PIN_LCD_CS, 1);

  gpio_init(PIN_LCD_DC);
  gpio_set_dir(PIN_LCD_DC, GPIO_OUT);
  gpio_put(PIN_LCD_DC, 1);

  gpio_init(PIN_LCD_WR);
  gpio_set_dir(PIN_LCD_WR, GPIO_OUT);
  gpio_put(PIN_LCD_WR, 1);

  gpio_init(PIN_LCD_RD);
  gpio_set_dir(PIN_LCD_RD, GPIO_OUT);
  gpio_put(PIN_LCD_RD, 1);

  for (i = 0; i < 8; i++) {
    gpio_init(PIN_LCD_D0 + i);
    gpio_set_dir(PIN_LCD_D0 + i, GPIO_OUT);
    gpio_put(PIN_LCD_D0 + i, 0);
  }

  gpio_set_function(PIN_BACKLIGHT, GPIO_FUNC_PWM);
  {
    pwm_config cfg = pwm_get_default_config();
    pwm_set_wrap(pwm_gpio_to_slice_num(PIN_BACKLIGHT), 65535);
    pwm_init(pwm_gpio_to_slice_num(PIN_BACKLIGHT), &cfg, true);
  }
  set_backlight_raw(0);
}

static void configure_display() {
  uint8_t colmod = 0x05;
  uint8_t porctrl[] = {0x0c, 0x0c, 0x00, 0x33, 0x33};
  uint8_t lcmctrl = 0x2c;
  uint8_t vdvvrhen = 0x01;
  uint8_t vrhs = 0x12;
  uint8_t vdvs = 0x20;
  uint8_t pwctrl1[] = {0xa4, 0xa1};
  uint8_t frctrl2 = 0x0f;
  uint8_t ramctrl[] = {0x00, 0xc0};
  uint8_t gctrl = 0x35;
  uint8_t vcoms = 0x1f;
  uint8_t gmctrp1[] = {0xD0, 0x08, 0x11, 0x08, 0x0C, 0x15, 0x39, 0x33, 0x50, 0x36, 0x13, 0x14, 0x29, 0x2D};
  uint8_t gmctrn1[] = {0xD0, 0x08, 0x10, 0x08, 0x06, 0x06, 0x39, 0x44, 0x51, 0x0B, 0x16, 0x14, 0x2F, 0x31};
  uint8_t madctl = (uint8_t)(MADCTL_ROW_ORDER | MADCTL_SWAP_XY | MADCTL_SCAN_ORDER);

  write_command(0x01);
  sleep_ms(150);

  write_command(0x35);
  write_command_data(0x3A, &colmod, 1);
  write_command_data(0xB2, porctrl, sizeof(porctrl));
  write_command_data(0xC0, &lcmctrl, 1);
  write_command_data(0xC2, &vdvvrhen, 1);
  write_command_data(0xC3, &vrhs, 1);
  write_command_data(0xC4, &vdvs, 1);
  write_command_data(0xD0, pwctrl1, sizeof(pwctrl1));
  write_command_data(0xC6, &frctrl2, 1);
  write_command_data(0xB0, ramctrl, sizeof(ramctrl));
  write_command_data(0xB7, &gctrl, 1);
  write_command_data(0xBB, &vcoms, 1);
  write_command_data(0xE0, gmctrp1, sizeof(gmctrp1));
  write_command_data(0xE1, gmctrn1, sizeof(gmctrn1));

  write_command(0x21);
  write_command(0x11);
  sleep_ms(100);
  write_command(0x29);
  sleep_ms(50);

  write_command_data(0x36, &madctl, 1);
}

static bool ensure_init() {
  if (s_inited) {
    return true;
  }

  common_init_pins();
  configure_display();
  set_backlight_raw(65535);
  s_inited = true;
  return true;
}
}

extern "C" {

bool rp2350_tft_init(void) {
  return ensure_init();
}

bool rp2350_tft_clear(uint16_t rgb565) {
  size_t i;
  uint8_t hi = (uint8_t)(rgb565 >> 8);
  uint8_t lo = (uint8_t)(rgb565 & 0xff);

  if (!ensure_init()) {
    return false;
  }

  set_window(0, 0, TFT_WIDTH, TFT_HEIGHT);
  cs_select();
  dc_data();
  for (i = 0; i < (size_t)TFT_WIDTH * (size_t)TFT_HEIGHT; i++) {
    write8(hi);
    write8(lo);
  }
  cs_deselect();
  return true;
}

bool rp2350_tft_set_pixel(int x, int y, uint16_t rgb565) {
  uint8_t px[2];

  if (!ensure_init()) {
    return false;
  }
  if (x < 0 || x >= TFT_WIDTH || y < 0 || y >= TFT_HEIGHT) {
    return false;
  }

  set_window(x, y, 1, 1);
  px[0] = (uint8_t)(rgb565 >> 8);
  px[1] = (uint8_t)(rgb565 & 0xff);
  write_data_bytes(px, sizeof(px));
  return true;
}

bool rp2350_tft_backlight(uint16_t level) {
  if (!ensure_init()) {
    return false;
  }
  set_backlight_raw(level);
  return true;
}

bool rp2350_tft_render_rgb565_bytes(const uint8_t *data, size_t data_len, int width, int height, int x, int y) {
  size_t bytes_needed;
  size_t i;

  if (!ensure_init() || data == NULL) {
    return false;
  }
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (x < 0 || y < 0 || x + width > TFT_WIDTH || y + height > TFT_HEIGHT) {
    return false;
  }

  bytes_needed = (size_t)width * (size_t)height * 2u;
  if (data_len < bytes_needed) {
    return false;
  }

  set_window(x, y, width, height);
  cs_select();
  dc_data();
  for (i = 0; i < bytes_needed; i++) {
    write8(data[i]);
  }
  cs_deselect();
  return true;
}

int rp2350_tft_width(void) {
  return TFT_WIDTH;
}

int rp2350_tft_height(void) {
  return TFT_HEIGHT;
}

}
