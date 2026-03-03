#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "rp2350_rtc.h"

#define PCF85063_REG_SECONDS 0x04

static bool s_rtc_bus_ready = false;

static uint8_t bcd_to_u8(uint8_t v)
{
	return (uint8_t)(((v >> 4u) * 10u) + (v & 0x0fu));
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
	year -= month <= 2;
	{
		const int era = (year >= 0 ? year : year - 399) / 400;
		const unsigned yoe = (unsigned)(year - era * 400);
		const unsigned doy = (153u * (month + (month > 2 ? (unsigned)-3 : 9u)) + 2u) / 5u + day - 1u;
		const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
		return (int64_t)era * 146097 + (int64_t)doe - 719468;
	}
}

static bool rtc_fields_to_unix(
	int year, int month, int day,
	int hour, int minute, int second,
	time_t *out_unix
) {
	int64_t days;
	int64_t seconds;

	if (!out_unix) {
		return false;
	}
	if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31 ||
		hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
		return false;
	}

	days = days_from_civil(year, (unsigned)month, (unsigned)day);
	seconds = days * 86400 + hour * 3600 + minute * 60 + second;
	*out_unix = (time_t)seconds;
	return true;
}

static void rp2350_rtc_bus_init(void)
{
	if (s_rtc_bus_ready) {
		return;
	}

	gpio_init(BW_SW_POWER_EN);
	gpio_set_dir(BW_SW_POWER_EN, GPIO_OUT);
	gpio_put(BW_SW_POWER_EN, 1);
	sleep_ms(5);

	i2c_init(BW_RTC_I2C, 400 * 1000);
	gpio_set_function(BW_RTC_I2C_SDA, GPIO_FUNC_I2C);
	gpio_set_function(BW_RTC_I2C_SCL, GPIO_FUNC_I2C);
	gpio_pull_up(BW_RTC_I2C_SDA);
	gpio_pull_up(BW_RTC_I2C_SCL);

	s_rtc_bus_ready = true;
}

bool rp2350_rtc_get_unix_time(time_t *out_time)
{
	uint8_t reg = PCF85063_REG_SECONDS;
	uint8_t raw[7];
	time_t unix_time = 0;
	int year;
	int month;
	int day;
	int hour;
	int minute;
	int second;

	if (!out_time) {
		return false;
	}

	rp2350_rtc_bus_init();

	if (i2c_write_blocking(BW_RTC_I2C, BW_RTC_ADDR, &reg, 1, true) != 1) {
		return false;
	}
	if (i2c_read_blocking(BW_RTC_I2C, BW_RTC_ADDR, raw, sizeof(raw), false) != (int)sizeof(raw)) {
		return false;
	}

	second = (int)bcd_to_u8(raw[0] & 0x7fu);
	minute = (int)bcd_to_u8(raw[1] & 0x7fu);
	hour = (int)bcd_to_u8(raw[2] & 0x3fu);
	day = (int)bcd_to_u8(raw[3] & 0x3fu);
	month = (int)bcd_to_u8(raw[5] & 0x1fu);
	year = 2000 + (int)bcd_to_u8(raw[6]);

	if (!rtc_fields_to_unix(year, month, day, hour, minute, second, &unix_time)) {
		return false;
	}

	*out_time = unix_time;
	return true;
}

bool rp2350_rtc_sync_system_time(void)
{
	time_t t;
	struct timeval tv;

	if (!rp2350_rtc_get_unix_time(&t)) {
		return false;
	}

	tv.tv_sec = t;
	tv.tv_usec = 0;
	return settimeofday(&tv, NULL) == 0;
}
