#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "Zend/zend.h"
#include "Zend/zend_API.h"
#include "Zend/zend_compile.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_execute.h"
#include "Zend/zend_observer.h"
#include "Zend/zend_smart_str.h"
#include "Zend/zend_smart_string.h"

#include "rp2350_eval.h"
#include "rp2350_psram.h"
#include "rp2350_transport.h"

static bool rp2350_zend_started = false;

ZEND_FUNCTION(mcu_sleep_ms);

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_sleep_ms, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, ms, IS_LONG, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry rp2350_mcu_functions[] = {
	ZEND_FE(mcu_sleep_ms, arginfo_mcu_sleep_ms)
	ZEND_FE_END
};

ZEND_FUNCTION(mcu_sleep_ms)
{
	zend_long ms = 0;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(ms)
	ZEND_PARSE_PARAMETERS_END();

	if (ms < 0) {
		ms = 0;
	}
	sleep_ms((uint32_t) ms);
	RETURN_TRUE;
}

static void rp2350_zend_error_cb(int type, zend_string *error_filename, const uint32_t error_lineno, zend_string *message)
{
	char line[384];
	int n = snprintf(
		line,
		sizeof(line),
		"[zend:%d] %s:%" PRIu32 " %s\r\n",
		type,
		error_filename ? ZSTR_VAL(error_filename) : "<eval>",
		error_lineno,
		message ? ZSTR_VAL(message) : "<null>"
	);
	if (n > 0) {
		rp2350_platform_write(line, (size_t) n);
	}
}

static size_t rp2350_zend_printf(const char *format, ...)
{
	char line[384];
	va_list ap;
	int n;

	va_start(ap, format);
	n = vsnprintf(line, sizeof(line), format, ap);
	va_end(ap);

	if (n <= 0) {
		return 0;
	}
	if ((size_t) n > sizeof(line)) {
		n = (int) sizeof(line);
	}
	return rp2350_platform_write(line, (size_t) n);
}

static size_t rp2350_zend_write(const char *str, size_t str_length)
{
	return rp2350_platform_write(str, str_length);
}

static FILE *rp2350_zend_fopen(zend_string *filename, zend_string **opened_path)
{
	(void) filename;
	(void) opened_path;
	return NULL;
}

static void rp2350_zend_message_handler(zend_long message, const void *data)
{
	(void) message;
	(void) data;
}

static zval *rp2350_zend_get_configuration_directive(zend_string *name)
{
	(void) name;
	return NULL;
}

static void rp2350_zend_ticks(int ticks)
{
	(void) ticks;
}

static void rp2350_zend_timeout(int seconds)
{
	(void) seconds;
}

static zend_result rp2350_zend_stream_open(zend_file_handle *handle)
{
	(void) handle;
	return FAILURE;
}

static void rp2350_zend_printf_to_smart_string(smart_string *buf, const char *format, va_list ap)
{
	char line[384];
	int n = vsnprintf(line, sizeof(line), format, ap);
	if (n > 0) {
		if ((size_t) n > sizeof(line)) {
			n = (int) sizeof(line);
		}
		smart_string_appendl(buf, line, (size_t) n);
	}
}

static void rp2350_zend_printf_to_smart_str(smart_str *buf, const char *format, va_list ap)
{
	char line[384];
	int n = vsnprintf(line, sizeof(line), format, ap);
	if (n > 0) {
		if ((size_t) n > sizeof(line)) {
			n = (int) sizeof(line);
		}
		smart_str_appendl(buf, line, (size_t) n);
	}
}

static char *rp2350_zend_getenv(const char *name, size_t name_len)
{
	(void) name;
	(void) name_len;
	return NULL;
}

static zend_string *rp2350_zend_resolve_path(zend_string *filename)
{
	return filename;
}

static uint32_t rp2350_prng_state = 0x12345678u;

static uint32_t rp2350_prng_u32(void)
{
	rp2350_prng_state = (rp2350_prng_state * 1664525u) + 1013904223u;
	return rp2350_prng_state;
}

static zend_result rp2350_zend_random_bytes(void *bytes, size_t size, char *errstr, size_t errstr_size)
{
	uint8_t *out = (uint8_t *) bytes;
	size_t i;

	(void) errstr;
	(void) errstr_size;

	for (i = 0; i < size; i++) {
		if ((i & 3u) == 0) {
			uint32_t r = rp2350_prng_u32();
			out[i] = (uint8_t) (r & 0xffu);
		} else {
			out[i] = (uint8_t) ((rp2350_prng_state >> ((i & 3u) * 8u)) & 0xffu);
		}
	}
	return SUCCESS;
}

static void rp2350_zend_random_bytes_insecure(zend_random_bytes_insecure_state *state, void *bytes, size_t size)
{
	(void) state;
	(void) rp2350_zend_random_bytes(bytes, size, NULL, 0);
}

int rp2350_eval_startup(void)
{
	zend_utility_functions zuf = {0};

	if (rp2350_zend_started) {
		return 0;
	}

	zuf.error_function = rp2350_zend_error_cb;
	zuf.printf_function = rp2350_zend_printf;
	zuf.write_function = rp2350_zend_write;
	zuf.fopen_function = rp2350_zend_fopen;
	zuf.message_handler = rp2350_zend_message_handler;
	zuf.get_configuration_directive = rp2350_zend_get_configuration_directive;
	zuf.ticks_function = rp2350_zend_ticks;
	zuf.on_timeout = rp2350_zend_timeout;
	zuf.stream_open_function = rp2350_zend_stream_open;
	zuf.printf_to_smart_string_function = rp2350_zend_printf_to_smart_string;
	zuf.printf_to_smart_str_function = rp2350_zend_printf_to_smart_str;
	zuf.getenv_function = rp2350_zend_getenv;
	zuf.resolve_path_function = rp2350_zend_resolve_path;
	zuf.random_bytes_function = rp2350_zend_random_bytes;
	zuf.random_bytes_insecure_function = rp2350_zend_random_bytes_insecure;

	zend_startup(&zuf);
	if (!rp2350_psram_install_zend_mm()) {
		rp2350_platform_write("[psram] Zend MM install FAIL\r\n",
			sizeof("[psram] Zend MM install FAIL\r\n") - 1);
		return -1;
	}
	if (zend_register_functions(NULL, rp2350_mcu_functions, NULL, MODULE_PERSISTENT) == FAILURE) {
		return -1;
	}
	if (zend_post_startup() == FAILURE) {
		return -1;
	}

	/* Disable observer machinery for MCU bring-up stability. */
	zend_observer_fcall_op_array_extension = -1;
	zend_observer_fcall_internal_function_extension = -1;
	zend_observer_errors_observed = false;
	zend_observer_function_declared_observed = false;
	zend_observer_class_linked_observed = false;

	zend_activate();
	rp2350_zend_started = true;
	return 0;
}

int rp2350_eval_execute(const char *code, size_t len)
{
	if (rp2350_eval_startup() != 0) {
		rp2350_platform_write("[zend] startup failed\r\n", sizeof("[zend] startup failed\r\n") - 1);
		return -1;
	}

	if (zend_eval_stringl(code, len, NULL, "rp2350_main") == FAILURE) {
		if (EG(exception)) {
			zend_clear_exception();
		}
		return -1;
	}

	return 0;
}
