#include "rp2350_tft.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "boards/pimoroni_tufty2350.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "st7789_parallel.pio.h"

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
PIO s_parallel_pio = pio1;
uint s_parallel_sm = 0;
uint s_parallel_offset = 0;
int s_dma_channel = -1;

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

static void write_blocking_dma(const uint8_t *src, size_t len) {
  while (dma_channel_is_busy((uint)s_dma_channel)) {
  }
  dma_channel_set_trans_count((uint)s_dma_channel, len, false);
  dma_channel_set_read_addr((uint)s_dma_channel, src, true);
}

static void write_blocking_parallel(const uint8_t *src, size_t len) {
  write_blocking_dma(src, len);
  dma_channel_wait_for_finish_blocking((uint)s_dma_channel);
  while (!pio_sm_is_tx_fifo_empty(s_parallel_pio, s_parallel_sm)) {
  }
}

static void write_command(uint8_t cmd) {
  cs_select();
  dc_command();
  write_blocking_parallel(&cmd, 1);
  cs_deselect();
}

static void write_data_bytes(const uint8_t *data, size_t len) {
  cs_select();
  dc_data();
  write_blocking_parallel(data, len);
  cs_deselect();
}

static void write_command_data(uint8_t cmd, const uint8_t *data, size_t len) {
  cs_select();
  dc_command();
  write_blocking_parallel(&cmd, 1);
  if (data != NULL && len > 0) {
    dc_data();
    write_blocking_parallel(data, len);
  }
  cs_deselect();
}

static void set_backlight_raw(uint16_t level) {
  pwm_set_gpio_level(PIN_BACKLIGHT, level);
}

static void set_window(int x, int y, int w, int h) {
  uint16_t x0 = __builtin_bswap16((uint16_t)x);
  uint16_t x1 = __builtin_bswap16((uint16_t)(x + w - 1));
  uint16_t y0 = __builtin_bswap16((uint16_t)y);
  uint16_t y1 = __builtin_bswap16((uint16_t)(y + h - 1));
  uint8_t caset[4];
  uint8_t raset[4];

  memcpy(&caset[0], &x0, 2);
  memcpy(&caset[2], &x1, 2);
  memcpy(&raset[0], &y0, 2);
  memcpy(&raset[2], &y1, 2);

  write_command_data(0x2A, caset, sizeof(caset));
  write_command_data(0x2B, raset, sizeof(raset));
  write_command(0x2C);
}

static void common_init_pins() {
  gpio_set_function(PIN_LCD_DC, GPIO_FUNC_SIO);
  gpio_set_dir(PIN_LCD_DC, GPIO_OUT);

  gpio_set_function(PIN_LCD_CS, GPIO_FUNC_SIO);
  gpio_set_dir(PIN_LCD_CS, GPIO_OUT);
  gpio_put(PIN_LCD_CS, 1);

  pio_set_gpio_base(s_parallel_pio, PIN_LCD_D0 + 8 >= 32 ? 16 : 0);
  s_parallel_sm = pio_claim_unused_sm(s_parallel_pio, true);
  s_parallel_offset = pio_add_program(s_parallel_pio, &st7789_parallel_program);

  pio_gpio_init(s_parallel_pio, PIN_LCD_WR);

  gpio_set_function(PIN_LCD_RD, GPIO_FUNC_SIO);
  gpio_set_dir(PIN_LCD_RD, GPIO_OUT);
  gpio_put(PIN_LCD_RD, 1);

  for (uint i = 0; i < 8; i++) {
    pio_gpio_init(s_parallel_pio, PIN_LCD_D0 + i);
  }

  pio_sm_set_consecutive_pindirs(s_parallel_pio, s_parallel_sm, PIN_LCD_D0, 8, true);
  pio_sm_set_consecutive_pindirs(s_parallel_pio, s_parallel_sm, PIN_LCD_WR, 1, true);

  pio_sm_config c = st7789_parallel_program_get_default_config(s_parallel_offset);
  sm_config_set_out_pins(&c, PIN_LCD_D0, 8);
  sm_config_set_sideset_pins(&c, PIN_LCD_WR);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  sm_config_set_out_shift(&c, false, true, 8);
  {
    constexpr uint32_t max_pio_clk = 32u * 1000u * 1000u;
    const uint32_t sys_clk_hz = clock_get_hz(clk_sys);
    const uint32_t clk_div = (sys_clk_hz + max_pio_clk - 1) / max_pio_clk;
    sm_config_set_clkdiv(&c, (float)clk_div);
  }
  pio_sm_init(s_parallel_pio, s_parallel_sm, s_parallel_offset, &c);
  pio_sm_set_enabled(s_parallel_pio, s_parallel_sm, true);

  s_dma_channel = dma_claim_unused_channel(true);
  dma_channel_config config = dma_channel_get_default_config((uint)s_dma_channel);
  channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
  channel_config_set_bswap(&config, false);
  channel_config_set_dreq(&config, pio_get_dreq(s_parallel_pio, s_parallel_sm, true));
  dma_channel_configure((uint)s_dma_channel, &config, &s_parallel_pio->txf[s_parallel_sm], NULL, 0, false);

  {
    pwm_config cfg = pwm_get_default_config();
    pwm_set_wrap(pwm_gpio_to_slice_num(PIN_BACKLIGHT), 65535);
    pwm_init(pwm_gpio_to_slice_num(PIN_BACKLIGHT), &cfg, true);
    gpio_set_function(PIN_BACKLIGHT, GPIO_FUNC_PWM);
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
  write_command(0x29);
  sleep_ms(100);
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
  uint8_t px[2] = {(uint8_t)(rgb565 >> 8), (uint8_t)(rgb565 & 0xff)};
  static uint8_t line[TFT_WIDTH * 2];

  if (!ensure_init()) {
    return false;
  }

  for (int i = 0; i < TFT_WIDTH; i++) {
    line[i * 2] = px[0];
    line[i * 2 + 1] = px[1];
  }

  set_window(0, 0, TFT_WIDTH, TFT_HEIGHT);
  cs_select();
  dc_data();
  for (int y = 0; y < TFT_HEIGHT; y++) {
    write_blocking_parallel(line, sizeof(line));
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
  cs_select();
  dc_data();
  write_blocking_parallel(px, sizeof(px));
  cs_deselect();
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
  write_blocking_parallel(data, bytes_needed);
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
