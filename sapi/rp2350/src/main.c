#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/regs/m33.h"
#include "hardware/structs/scb.h"

#include "rp2350_eval.h"
#include "rp2350_powman.h"
#include "rp2350_psram.h"
#include "rp2350_rtc.h"
#include "rp2350_transport.h"

static void log_line(const char *line)
{
	rp2350_platform_write(line, strlen(line));
	rp2350_platform_flush();
}

static void maybe_wait_for_usb_serial(void)
{
#if RP2350_WAIT_FOR_USB_SERIAL_MS > 0
	absolute_time_t deadline = make_timeout_time_ms(RP2350_WAIT_FOR_USB_SERIAL_MS);
	while (!time_reached(deadline)) {
		if (stdio_usb_connected()) {
			break;
		}
		sleep_ms(10);
	}
#endif
}

int main(void)
{
	char build_line[96];
	rp2350_powman_early_init();
	(void)rp2350_powman_maybe_handle_long_press();
	rp2350_powman_after_wake_init();
	/* Avoid HardFault escalation when we can report the original fault class. */
	scb_hw->shcsr |= M33_SHCSR_MEMFAULTENA_BITS
		| M33_SHCSR_BUSFAULTENA_BITS
		| M33_SHCSR_USGFAULTENA_BITS;

	if (!rp2350_psram_init(BW_PSRAM_CS)) {
		stdio_init_all();
		maybe_wait_for_usb_serial();
		sleep_ms(300);
		log_line("\r\n[boot] php-mcu\r\n");
		log_line("[boot] psram init FAIL\r\n");
		while (true) {
			log_line("[halt] psram\r\n");
			sleep_ms(1000);
		}
	}

	stdio_init_all();
	maybe_wait_for_usb_serial();
	sleep_ms(300);

	log_line("\r\n[boot] php-mcu\r\n");
	snprintf(build_line, sizeof(build_line), "[boot] build %s %s\r\n", __DATE__, __TIME__);
	log_line(build_line);
	log_line("[boot] psram init OK\r\n");
	if (rp2350_rtc_sync_system_time()) {
		log_line("[boot] rtc sync OK\r\n");
	} else {
		log_line("[boot] rtc sync FAIL\r\n");
	}

	if (rp2350_eval_startup() != 0) {
		char line[512];
		log_line("[boot] zend startup FAIL\r\n");
		while (true) {
			int n = snprintf(line, sizeof(line), "[halt] zend: %s\r\n", rp2350_eval_last_error());
			if (n < 0 || n >= (int)sizeof(line)) {
				snprintf(line, sizeof(line), "[halt] zend: <truncated>\r\n");
			}
			log_line(line);
			sleep_ms(1000);
		}
	}
	log_line("[boot] zend startup OK\r\n");

	log_line("[boot] run main.php\r\n");
	if (rp2350_eval_execute_file("/main.php") != 0) {
		char line[512];
		log_line("[php] main.php error\r\n");
		while (true) {
			int n = snprintf(line, sizeof(line), "[halt] php main: %s\r\n", rp2350_eval_last_error());
			if (n < 0 || n >= (int)sizeof(line)) {
				snprintf(line, sizeof(line), "[halt] php main: <truncated>\r\n");
			}
			log_line(line);
			sleep_ms(1000);
		}
	}
	while (true) {
		sleep_ms(1000);
	}
}
