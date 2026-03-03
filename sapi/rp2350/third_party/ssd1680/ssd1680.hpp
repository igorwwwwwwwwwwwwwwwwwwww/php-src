#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

namespace pimoroni {

class SSD1680 {
  enum RAM_FLAGS {
    X_START = 0x00,
    X_END = 0x15,
    Y_START_H = 0x01,
    Y_START_L = 0x07,
    Y_END_H = 0x00,
    Y_END_L = 0x00,
  };

 private:
  spi_inst_t* spi = spi0;

  const uint CS = 17;
  const uint DC = 20;
  const uint SCK = 18;
  const uint MOSI = 19;
  const uint BUSY = 16;
  const uint RESET = 21;

  uint8_t lut_repeat_count = 1;
  bool blocking = true;

 public:
  SSD1680();

  void update();
  uint32_t* get_framebuffer();

  void busy_wait();
  void reset();

  bool is_busy();
  bool set_update_speed(int update_speed);

  void write_luts();
  void set_blocking(bool value);

  void command(uint8_t reg, size_t len, const uint8_t* data);

 private:
  void setup();

  void command(uint8_t reg, std::initializer_list<uint8_t> values);
  void command(uint8_t reg) { command(reg, 0, nullptr); }
  void data(size_t len, const uint8_t* data);
  void data(std::initializer_list<uint8_t> values);
};

}  // namespace pimoroni
