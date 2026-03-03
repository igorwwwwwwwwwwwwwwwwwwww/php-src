#include "ssd1680.hpp"

#include <cstring>

namespace pimoroni {
namespace {
constexpr int WIDTH = 264;
constexpr int HEIGHT = 176;

uint32_t __attribute__((section(".uninitialized_data"))) __attribute__((aligned(4))) framebuffer[WIDTH * HEIGHT];
uint8_t __attribute__((section(".uninitialized_data"))) backbuffer[(WIDTH * HEIGHT) / 8];

enum reg {
  DOC = 0x01,
  GDVC = 0x03,
  SDVC = 0x04,
  BTST = 0x0C,
  DEM = 0x11,
  SWR = 0x12,
  ADUS = 0x20,
  DUC2 = 0x22,
  WRAM_BW = 0x24,
  WRAM_R = 0x26,
  WVCOM = 0x2C,
  WLR = 0x32,
  EOPT = 0x3F,
  SRX = 0x44,
  SRY = 0x45,
  SRXC = 0x4E,
  SRYC = 0x4F,
};
}  // namespace

SSD1680::SSD1680() {
  spi_init(spi, 12'000'000);

  gpio_set_function(DC, GPIO_FUNC_SIO);
  gpio_set_dir(DC, GPIO_OUT);

  gpio_set_function(CS, GPIO_FUNC_SIO);
  gpio_set_dir(CS, GPIO_OUT);
  gpio_put(CS, 1);

  gpio_set_function(RESET, GPIO_FUNC_SIO);
  gpio_set_dir(RESET, GPIO_OUT);
  gpio_put(RESET, 1);

  gpio_set_function(BUSY, GPIO_FUNC_SIO);
  gpio_set_dir(BUSY, GPIO_IN);

  gpio_set_function(SCK, GPIO_FUNC_SPI);
  gpio_set_function(MOSI, GPIO_FUNC_SPI);

  setup();
  write_luts();
}

void SSD1680::set_blocking(bool value) { blocking = value; }

bool SSD1680::is_busy() { return gpio_get(BUSY); }

void SSD1680::busy_wait() {
  while (is_busy()) {
    tight_loop_contents();
  }
}

uint32_t* SSD1680::get_framebuffer() { return framebuffer; }

void SSD1680::reset() {
  gpio_put(RESET, 0);
  sleep_ms(10);
  gpio_put(RESET, 1);
  sleep_ms(10);
  busy_wait();
}

bool SSD1680::set_update_speed(int update_speed) {
  lut_repeat_count = static_cast<uint8_t>(3 - (update_speed & 3));
  return true;
}

void SSD1680::write_luts() {
  command(WLR,
          {
              0x40, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0xA0, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0xA8, 0x65, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0xAA, 0x65, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          });

  data({0x02, 0x00, 0x00, 0x05, 0x0A, 0x00});
  data(1, &lut_repeat_count);
  data({0x19, 0x19, 0x00, 0x02, 0x00, 0x00});
  data(1, &lut_repeat_count);
  data({0x05, 0x0A, 0x00, 0x00, 0x00, 0x00});
  data(1, &lut_repeat_count);

  data({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x42, 0x22, 0x22,
        0x23, 0x32, 0x00, 0x00, 0x00});

  command(EOPT, {0x22});
  command(GDVC, {0x17});
  command(SDVC, {0x41, 0xAE, 0x32});
  command(WVCOM, {0x28});

  busy_wait();
}

void SSD1680::setup() {
  reset();

  command(SWR);
  busy_wait();

  command(DOC, {Y_START_L, Y_START_H, 0});
  command(DEM, {0b001});
  command(SRX, {X_START, X_END});
  command(SRY, {Y_START_L, Y_START_H, Y_END_L, Y_END_H});

  busy_wait();
}

void SSD1680::command(uint8_t reg, size_t len, const uint8_t* data) {
  gpio_put(CS, 0);

  gpio_put(DC, 0);
  spi_write_blocking(spi, &reg, 1);

  if (len > 0) {
    gpio_put(DC, 1);
    spi_write_blocking(spi, data, len);
  }

  gpio_put(CS, 1);
}

void SSD1680::data(size_t len, const uint8_t* data) {
  gpio_put(CS, 0);
  gpio_put(DC, 1);
  spi_write_blocking(spi, data, len);
  gpio_put(CS, 1);
}

void SSD1680::data(std::initializer_list<uint8_t> values) {
  data(values.size(), values.begin());
}

void SSD1680::command(uint8_t reg, std::initializer_list<uint8_t> values) {
  command(reg, values.size(), values.begin());
}

void SSD1680::update() {
  busy_wait();
  write_luts();

  command(SRXC, {X_START});
  command(SRYC, {Y_START_L, Y_START_H});

  command(WRAM_R);
  memset(backbuffer, 0, sizeof(backbuffer));
  for (auto y = 0; y < HEIGHT; y++) {
    for (auto x = 0; x < WIDTH; x++) {
      uint bo_d = 7 - (y & 0b111);
      uint32_t fb_src = framebuffer[x + y * WIDTH];
      uint8_t src = ((fb_src & 0x00ff0000) >> 16) | ((fb_src & 0x0000ff00) >> 8) |
                    ((fb_src & 0x000000ff));
      src = ~(src >> 7) & 0b1;
      backbuffer[(y + x * HEIGHT) / 8] |= (src << bo_d);
    }
  }
  data(sizeof(backbuffer), backbuffer);

  command(SRXC, {X_START});
  command(SRYC, {Y_START_L, Y_START_H});

  command(WRAM_BW);
  memset(backbuffer, 0, sizeof(backbuffer));
  for (auto y = 0; y < HEIGHT; y++) {
    for (auto x = 0; x < WIDTH; x++) {
      uint bo_d = 7 - (y & 0b111);
      uint32_t fb_src = framebuffer[x + y * WIDTH];
      uint8_t src = ((fb_src & 0x00ff0000) >> 16) | ((fb_src & 0x0000ff00) >> 8) |
                    ((fb_src & 0x000000ff));
      src = ~(src >> 6) & 0b1;
      backbuffer[(y + x * HEIGHT) / 8] |= (src << bo_d);
    }
  }
  data(sizeof(backbuffer), backbuffer);

  command(BTST);
  command(DUC2, {0xC7});

  busy_wait();

  command(ADUS);

  if (blocking) {
    busy_wait();
  }
}

}  // namespace pimoroni
