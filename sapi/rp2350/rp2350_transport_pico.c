/*
   Pico SDK transport adapter for RP2350 SAPI shims.
   Compile this file into your firmware project to override weak defaults.
*/

#include <stddef.h>

#include "pico/stdlib.h"

#include "rp2350_transport.h"

size_t rp2350_platform_write(const char *buf, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++) {
		putchar_raw(buf[i]);
	}
	return len;
}

void rp2350_platform_flush(void)
{
	stdio_flush();
}

void rp2350_platform_log(const char *msg)
{
	const char *prefix = "[rp2350] ";
	size_t i;

	for (i = 0; prefix[i] != '\0'; i++) {
		putchar_raw(prefix[i]);
	}
	for (i = 0; msg[i] != '\0'; i++) {
		putchar_raw(msg[i]);
	}
	putchar_raw('\n');
}

size_t rp2350_platform_readline(char *buf, size_t max_len)
{
	size_t i = 0;
	absolute_time_t start = get_absolute_time();
	absolute_time_t last_rx = start;
	const int64_t idle_timeout_us = 1000000;
	const int64_t inter_char_timeout_us = 1500000;

	if (max_len == 0) {
		return 0;
	}

	while (i + 1 < max_len) {
		absolute_time_t now;
		int ch = getchar_timeout_us(0);
		if (ch == PICO_ERROR_TIMEOUT) {
			now = get_absolute_time();
			if (i == 0 && absolute_time_diff_us(start, now) > idle_timeout_us) {
				buf[0] = '\0';
				return 0;
			}
			if (i > 0 && absolute_time_diff_us(last_rx, now) > inter_char_timeout_us) {
				/* Drop incomplete/noisy line fragments instead of stalling forever. */
				buf[0] = '\0';
				return 0;
			}
			tight_loop_contents();
			continue;
		}
		last_rx = get_absolute_time();
		if (ch == '\r' || ch == '\n') {
			/* Normalize line termination for parser and terminal display. */
			rp2350_platform_write("\r\n", 2);
			buf[i++] = '\n';
			break;
		}
		if (ch == 0x08 || ch == 0x7f) {
			if (i > 0) {
				i--;
				rp2350_platform_write("\b \b", 3);
			}
			continue;
		}
		/* Echo each typed character. */
		{
			char c = (char) ch;
			rp2350_platform_write(&c, 1);
			buf[i++] = c;
		}
	}

	buf[i] = '\0';
	return i;
}
