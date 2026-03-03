#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

#include "rp2350_epd.h"

#define EPD_WIDTH 264
#define EPD_HEIGHT 176
#define EPD_RAM_BYTES ((EPD_WIDTH * EPD_HEIGHT) / 8)

#define EPD_PIN_CS    17
#define EPD_PIN_DC    20
#define EPD_PIN_SCK   18
#define EPD_PIN_MOSI  19
#define EPD_PIN_BUSY  16
#define EPD_PIN_RESET 21

enum epd_reg {
	EPD_DOC = 0x01,
	EPD_GDVC = 0x03,
	EPD_SDVC = 0x04,
	EPD_BTST = 0x0C,
	EPD_DEM = 0x11,
	EPD_SWR = 0x12,
	EPD_ADUS = 0x20,
	EPD_DUC2 = 0x22,
	EPD_WRAM_BW = 0x24,
	EPD_WRAM_R = 0x26,
	EPD_WVCOM = 0x2C,
	EPD_WLR = 0x32,
	EPD_EOPT = 0x3F,
	EPD_SRX = 0x44,
	EPD_SRY = 0x45,
	EPD_SRXC = 0x4E,
	EPD_SRYC = 0x4F,
};

static const uint8_t EPD_X_START = 0x00;
static const uint8_t EPD_X_END = 0x15;
static const uint8_t EPD_Y_START_H = 0x01;
static const uint8_t EPD_Y_START_L = 0x07;
static const uint8_t EPD_Y_END_H = 0x00;
static const uint8_t EPD_Y_END_L = 0x00;

static bool s_epd_inited = false;
static uint8_t s_tx_buf[EPD_RAM_BYTES];
static uint8_t s_framebuffer[EPD_RAM_BYTES];

static inline bool epd_is_busy(void)
{
	return gpio_get(EPD_PIN_BUSY);
}

static void epd_busy_wait(void)
{
	while (epd_is_busy()) {
		tight_loop_contents();
	}
}

static void epd_command(uint8_t reg, const uint8_t *data, size_t len)
{
	gpio_put(EPD_PIN_CS, 0);

	gpio_put(EPD_PIN_DC, 0);
	spi_write_blocking(spi0, &reg, 1);

	if (len > 0 && data) {
		gpio_put(EPD_PIN_DC, 1);
		spi_write_blocking(spi0, data, len);
	}

	gpio_put(EPD_PIN_CS, 1);
}

static void epd_data(const uint8_t *data, size_t len)
{
	gpio_put(EPD_PIN_CS, 0);
	gpio_put(EPD_PIN_DC, 1);
	spi_write_blocking(spi0, data, len);
	gpio_put(EPD_PIN_CS, 1);
}

static void epd_reset(void)
{
	gpio_put(EPD_PIN_RESET, 0);
	sleep_ms(10);
	gpio_put(EPD_PIN_RESET, 1);
	sleep_ms(10);
	epd_busy_wait();
}

static void epd_setup(void)
{
	uint8_t data_doc[3] = {EPD_Y_START_L, EPD_Y_START_H, 0x00};
	uint8_t data_dem[1] = {0x01};
	uint8_t data_srx[2] = {EPD_X_START, EPD_X_END};
	uint8_t data_sry[4] = {EPD_Y_START_L, EPD_Y_START_H, EPD_Y_END_L, EPD_Y_END_H};

	epd_reset();
	epd_command(EPD_SWR, NULL, 0);
	epd_busy_wait();

	epd_command(EPD_DOC, data_doc, sizeof(data_doc));
	epd_command(EPD_DEM, data_dem, sizeof(data_dem));
	epd_command(EPD_SRX, data_srx, sizeof(data_srx));
	epd_command(EPD_SRY, data_sry, sizeof(data_sry));
	epd_busy_wait();
}

static void epd_write_luts(void)
{
	static const uint8_t lut_head[60] = {
		0x40, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xA0, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xA8, 0x65, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xAA, 0x65, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const uint8_t lut_tail[93] = {
		0x02, 0x00, 0x00, 0x05, 0x0A, 0x00, 0x01,
		0x19, 0x19, 0x00, 0x02, 0x00, 0x00, 0x01,
		0x05, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x44, 0x42, 0x22, 0x22, 0x23, 0x32, 0x00, 0x00, 0x00
	};
	static const uint8_t eopt[1] = {0x22};
	static const uint8_t gdvc[1] = {0x17};
	static const uint8_t sdvc[3] = {0x41, 0xAE, 0x32};
	static const uint8_t vcom[1] = {0x28};

	epd_command(EPD_WLR, lut_head, sizeof(lut_head));
	epd_data(lut_tail, sizeof(lut_tail));
	epd_command(EPD_EOPT, eopt, sizeof(eopt));
	epd_command(EPD_GDVC, gdvc, sizeof(gdvc));
	epd_command(EPD_SDVC, sdvc, sizeof(sdvc));
	epd_command(EPD_WVCOM, vcom, sizeof(vcom));
	epd_busy_wait();
}

static void epd_set_ram_counters(void)
{
	const uint8_t x[1] = {EPD_X_START};
	const uint8_t y[2] = {EPD_Y_START_L, EPD_Y_START_H};
	epd_command(EPD_SRXC, x, sizeof(x));
	epd_command(EPD_SRYC, y, sizeof(y));
}

bool rp2350_epd_init(void)
{
	if (s_epd_inited) {
		return true;
	}

	spi_init(spi0, 12 * 1000 * 1000);

	gpio_set_function(EPD_PIN_DC, GPIO_FUNC_SIO);
	gpio_set_dir(EPD_PIN_DC, GPIO_OUT);

	gpio_set_function(EPD_PIN_CS, GPIO_FUNC_SIO);
	gpio_set_dir(EPD_PIN_CS, GPIO_OUT);
	gpio_put(EPD_PIN_CS, 1);

	gpio_set_function(EPD_PIN_RESET, GPIO_FUNC_SIO);
	gpio_set_dir(EPD_PIN_RESET, GPIO_OUT);
	gpio_put(EPD_PIN_RESET, 1);

	gpio_set_function(EPD_PIN_BUSY, GPIO_FUNC_SIO);
	gpio_set_dir(EPD_PIN_BUSY, GPIO_IN);

	gpio_set_function(EPD_PIN_SCK, GPIO_FUNC_SPI);
	gpio_set_function(EPD_PIN_MOSI, GPIO_FUNC_SPI);

	epd_setup();
	epd_write_luts();
	memset(s_framebuffer, 0x00, sizeof(s_framebuffer));
	s_epd_inited = true;
	return true;
}

bool rp2350_epd_clear(bool black)
{
	if (!rp2350_epd_init()) {
		return false;
	}
	memset(s_framebuffer, black ? 0xFF : 0x00, sizeof(s_framebuffer));
	return true;
}

bool rp2350_epd_set_pixel(int x, int y, bool black)
{
	size_t idx;
	uint8_t bit;

	if (!rp2350_epd_init()) {
		return false;
	}
	if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
		return false;
	}

	idx = (size_t)(y + (x * EPD_HEIGHT)) / 8u;
	bit = (uint8_t)(1u << (7u - ((uint8_t)y & 0x07u)));

	if (black) {
		s_framebuffer[idx] |= bit;
	} else {
		s_framebuffer[idx] &= (uint8_t)~bit;
	}
	return true;
}

bool rp2350_epd_update(void)
{
	const uint8_t duc2[1] = {0xC7};

	if (!rp2350_epd_init()) {
		return false;
	}

	epd_busy_wait();
	epd_write_luts();

	memcpy(s_tx_buf, s_framebuffer, sizeof(s_tx_buf));
	epd_set_ram_counters();
	epd_command(EPD_WRAM_R, NULL, 0);
	epd_data(s_tx_buf, sizeof(s_tx_buf));

	epd_set_ram_counters();
	epd_command(EPD_WRAM_BW, NULL, 0);
	epd_data(s_tx_buf, sizeof(s_tx_buf));

	epd_command(EPD_BTST, NULL, 0);
	epd_command(EPD_DUC2, duc2, sizeof(duc2));
	epd_busy_wait();
	epd_command(EPD_ADUS, NULL, 0);
	epd_busy_wait();
	return true;
}

bool rp2350_epd_fill(bool black)
{
	return rp2350_epd_clear(black) && rp2350_epd_update();
}
