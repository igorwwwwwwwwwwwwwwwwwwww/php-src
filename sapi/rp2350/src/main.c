#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"

#include "rp2350_eval.h"
#include "rp2350_main_php.h"
#include "rp2350_psram.h"
#include "rp2350_transport.h"

static void log_line(const char *line)
{
	rp2350_platform_write(line, strlen(line));
	rp2350_platform_flush();
}

int main(void)
{
	stdio_init_all();
	sleep_ms(500);

	log_line("\r\n[boot] php-mcu\r\n");
	if (!rp2350_psram_init(BW_PSRAM_CS)) {
		log_line("[boot] psram init FAIL\r\n");
		while (true) {
			log_line("[halt] psram\r\n");
			sleep_ms(1000);
		}
	}
	log_line("[boot] psram init OK\r\n");

	if (rp2350_eval_startup() != 0) {
		log_line("[boot] zend startup FAIL\r\n");
		while (true) {
			log_line("[halt] zend\r\n");
			sleep_ms(1000);
		}
	}
	log_line("[boot] zend startup OK\r\n");

	log_line("[boot] run main.php\r\n");
	while (true) {
		if (rp2350_eval_execute(rp2350_main_php_source, rp2350_main_php_source_len) != 0) {
			log_line("[php] main.php error\r\n");
			sleep_ms(1000);
		}
	}
}
