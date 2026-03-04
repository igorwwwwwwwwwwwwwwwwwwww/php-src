#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/sync.h"

#include "Zend/zend.h"
#include "Zend/zend_API.h"
#include "Zend/zend_compile.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_execute.h"
#include "Zend/zend_observer.h"
#include "Zend/zend_smart_str.h"
#include "Zend/zend_smart_string.h"
#include "Zend/zend_stream.h"

#include "SAPI.h"
#include "main/php_main.h"
#include "main/php_globals.h"
#include "main/php_variables.h"
#include "ext/standard/file.h"

#include "rp2350_eval.h"
#include "rp2350_epd.h"
#include "rp2350_psram.h"
#include "rp2350_transport.h"
#include "rp2350_vfs.h"

static bool rp2350_zend_started = false;
static bool rp2350_sapi_started = false;
static const char *rp2350_eval_error = "ok";
static char rp2350_eval_error_detail[256];
static char rp2350_last_zend_error[192];
static bool rp2350_stream_open_seen = false;
static char rp2350_stream_open_last_in[96];
static char rp2350_stream_open_last_path[96];
static const char *rp2350_stream_open_last_reason = "none";
static bool s_buttons_init = false;
static bool s_leds_init = false;
static volatile uint32_t s_button_state_mask = 0;
static volatile uint32_t s_button_irq_pending_mask = 0;
static volatile uint32_t s_button_irq_generation = 0;
static void (*s_prev_zend_interrupt_function)(zend_execute_data *execute_data) = NULL;

extern void php_printf_to_smart_string(smart_string *buf, const char *format, va_list ap);
extern void php_printf_to_smart_str(smart_str *buf, const char *format, va_list ap);

typedef struct {
	const char *src;
	size_t len;
	size_t pos;
} rp2350_vfs_stream_t;

static const rp2350_vfs_file_t *rp2350_vfs_find(const char *path)
{
	size_t i;
	for (i = 0; i < rp2350_vfs_files_count; i++) {
		if (strcmp(rp2350_vfs_files[i].path, path) == 0) {
			return &rp2350_vfs_files[i];
		}
	}
	return NULL;
}

static bool rp2350_vfs_resolve_candidate(const char *input, char *out, size_t out_size)
{
	if (strncmp(input, "file://", 7) == 0) {
		input += 7;
	}
	if (input[0] == '/') {
		snprintf(out, out_size, "%s", input);
		return true;
	}
	if (strncmp(input, "./", 2) == 0) {
		snprintf(out, out_size, "/%s", input + 2);
		return true;
	}
	{
		zend_string *exec_file = zend_get_executed_filename_ex();
		const char *base = exec_file ? ZSTR_VAL(exec_file) : "/main.php";
		const char *slash = strrchr(base, '/');
		size_t dir_len = slash ? (size_t)(slash - base) : 0;
		if (dir_len == 0) {
			snprintf(out, out_size, "/%s", input);
			return true;
		}
		snprintf(out, out_size, "%.*s/%s", (int)dir_len, base, input);
		return true;
	}
}

static ssize_t rp2350_vfs_reader(void *handle, char *buf, size_t len)
{
	rp2350_vfs_stream_t *s = (rp2350_vfs_stream_t *)handle;
	size_t remaining;
	size_t n;

	if (!s || s->pos >= s->len) {
		return 0;
	}
	remaining = s->len - s->pos;
	n = len < remaining ? len : remaining;
	memcpy(buf, s->src + s->pos, n);
	s->pos += n;
	return (ssize_t)n;
}

static size_t rp2350_vfs_fsizer(void *handle)
{
	rp2350_vfs_stream_t *s = (rp2350_vfs_stream_t *)handle;
	return s ? s->len : 0;
}

static void rp2350_vfs_closer(void *handle)
{
	free(handle);
}

ZEND_FUNCTION(mcu_sleep_ms);
ZEND_FUNCTION(mcu_button_pressed);
ZEND_FUNCTION(mcu_button_state_mask);
ZEND_FUNCTION(mcu_button_wait);
ZEND_FUNCTION(mcu_led_set);
ZEND_FUNCTION(mcu_epd_fill);
ZEND_FUNCTION(mcu_epd_clear);
ZEND_FUNCTION(mcu_epd_set_pixel);
ZEND_FUNCTION(mcu_epd_update);
ZEND_FUNCTION(mcu_epd_render);
PHP_MINIT_FUNCTION(rp2350_mcu);

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_sleep_ms, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, ms, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_button_pressed, 0, 0, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, button, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_button_state_mask, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_MASK_EX(arginfo_mcu_button_wait, 0, 1, MAY_BE_LONG | MAY_BE_FALSE)
	ZEND_ARG_TYPE_INFO(0, timeout_ms, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_led_set, 0, 2, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, index, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, on, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_epd_fill, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, black, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_epd_clear, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, black, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_epd_set_pixel, 0, 3, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, x, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, y, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, black, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_epd_update, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_epd_render, 0, 3, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, bytes, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, width, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, height, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, x, IS_LONG, 1)
	ZEND_ARG_TYPE_INFO(0, y, IS_LONG, 1)
ZEND_END_ARG_INFO()

static const zend_function_entry rp2350_mcu_functions[] = {
	ZEND_FE(mcu_sleep_ms, arginfo_mcu_sleep_ms)
	ZEND_FE(mcu_button_pressed, arginfo_mcu_button_pressed)
	ZEND_FE(mcu_button_state_mask, arginfo_mcu_button_state_mask)
	ZEND_FE(mcu_button_wait, arginfo_mcu_button_wait)
	ZEND_FE(mcu_led_set, arginfo_mcu_led_set)
	ZEND_FE(mcu_epd_fill, arginfo_mcu_epd_fill)
	ZEND_FE(mcu_epd_clear, arginfo_mcu_epd_clear)
	ZEND_FE(mcu_epd_set_pixel, arginfo_mcu_epd_set_pixel)
	ZEND_FE(mcu_epd_update, arginfo_mcu_epd_update)
	ZEND_FE(mcu_epd_render, arginfo_mcu_epd_render)
	ZEND_FE_END
};

static zend_module_entry rp2350_mcu_module_entry = {
	STANDARD_MODULE_HEADER,
	"rp2350_mcu",
	rp2350_mcu_functions,
	PHP_MINIT(rp2350_mcu),
	NULL,
	NULL,
	NULL,
	NULL,
	NO_VERSION_YET,
	STANDARD_MODULE_PROPERTIES
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

static bool rp2350_button_gpio_is_pressed(uint32_t gpio)
{
	return gpio_get(gpio) == 0;
}

static uint32_t rp2350_led_gpio_for_index(zend_long index)
{
	switch (index) {
		case 0: return BW_LED_0;
		case 1: return BW_LED_1;
		case 2: return BW_LED_2;
		case 3: return BW_LED_3;
		default: return UINT32_MAX;
	}
}

static uint32_t rp2350_button_gpio_for_id(zend_long button)
{
	switch (button) {
		case 0: return BW_SWITCH_A;
		case 1: return BW_SWITCH_B;
		case 2: return BW_SWITCH_C;
		case 3: return BW_SWITCH_UP;
		case 4: return BW_SWITCH_DOWN;
		case 5: return BW_SWITCH_HOME;
		case 6: return BW_RESET_SW;
		default: return UINT32_MAX;
	}
}

static void rp2350_leds_init(void)
{
	const uint32_t gpios[] = { BW_LED_0, BW_LED_1, BW_LED_2, BW_LED_3 };
	size_t i;

	if (s_leds_init) {
		return;
	}
	for (i = 0; i < sizeof(gpios) / sizeof(gpios[0]); i++) {
		gpio_init(gpios[i]);
		gpio_set_dir(gpios[i], GPIO_OUT);
		gpio_put(gpios[i], 0);
	}
	s_leds_init = true;
}

static void rp2350_zend_interrupt_handler(zend_execute_data *execute_data)
{
	uint32_t pending;
	uint32_t irq_state;

	(void)execute_data;

	irq_state = save_and_disable_interrupts();
	pending = s_button_irq_pending_mask;
	s_button_irq_pending_mask = 0;
	restore_interrupts(irq_state);
	(void)pending;

	if (s_prev_zend_interrupt_function) {
		s_prev_zend_interrupt_function(execute_data);
	}
}

static void rp2350_button_gpio_irq(uint gpio, uint32_t events)
{
	int id = -1;
	bool pressed;

	(void)events;
	switch (gpio) {
		case BW_SWITCH_A: id = 0; break;
		case BW_SWITCH_B: id = 1; break;
		case BW_SWITCH_C: id = 2; break;
		case BW_SWITCH_UP: id = 3; break;
		case BW_SWITCH_DOWN: id = 4; break;
		case BW_SWITCH_HOME: id = 5; break;
		case BW_RESET_SW: id = 6; break;
		default: return;
	}

	pressed = rp2350_button_gpio_is_pressed(gpio);
	if (pressed) {
		s_button_state_mask |= (1u << id);
	} else {
		s_button_state_mask &= ~(1u << id);
	}
	s_button_irq_pending_mask |= (1u << id);
	s_button_irq_generation++;

	if (rp2350_zend_started) {
		zend_atomic_bool_store_ex(&EG(vm_interrupt), true);
	}
}

static void rp2350_buttons_init(void)
{
	const uint32_t gpios[] = {
		BW_SWITCH_A, BW_SWITCH_B, BW_SWITCH_C, BW_SWITCH_UP, BW_SWITCH_DOWN,
		BW_SWITCH_HOME, BW_RESET_SW
	};
	size_t i;
	bool callback_set = false;

	if (s_buttons_init) {
		return;
	}

	for (i = 0; i < sizeof(gpios) / sizeof(gpios[0]); i++) {
		uint32_t gpio = gpios[i];
		gpio_init(gpio);
		gpio_set_dir(gpio, GPIO_IN);
		gpio_pull_up(gpio);
		if (rp2350_button_gpio_is_pressed(gpio)) {
			switch (gpio) {
				case BW_SWITCH_A: s_button_state_mask |= (1u << 0); break;
				case BW_SWITCH_B: s_button_state_mask |= (1u << 1); break;
				case BW_SWITCH_C: s_button_state_mask |= (1u << 2); break;
				case BW_SWITCH_UP: s_button_state_mask |= (1u << 3); break;
				case BW_SWITCH_DOWN: s_button_state_mask |= (1u << 4); break;
				case BW_SWITCH_HOME: s_button_state_mask |= (1u << 5); break;
				case BW_RESET_SW: s_button_state_mask |= (1u << 6); break;
				default: break;
			}
		}
		if (!callback_set) {
			gpio_set_irq_enabled_with_callback(
				gpio,
				GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
				true,
				&rp2350_button_gpio_irq
			);
			callback_set = true;
		} else {
			gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
		}
	}

	s_buttons_init = true;
	s_button_irq_pending_mask = 0xffffffffu; /* force initial LED sync on first interrupt check */
}

ZEND_FUNCTION(mcu_button_pressed)
{
	zend_long button = -1;
	bool button_is_null = true;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG_OR_NULL(button, button_is_null)
	ZEND_PARSE_PARAMETERS_END();

	rp2350_buttons_init();

	if (button_is_null || button < 0) {
		RETURN_BOOL(
			rp2350_button_gpio_is_pressed(BW_SWITCH_A) ||
			rp2350_button_gpio_is_pressed(BW_SWITCH_B) ||
			rp2350_button_gpio_is_pressed(BW_SWITCH_C) ||
			rp2350_button_gpio_is_pressed(BW_SWITCH_UP) ||
			rp2350_button_gpio_is_pressed(BW_SWITCH_DOWN));
	}

	{
		uint32_t gpio = rp2350_button_gpio_for_id(button);
		if (gpio == UINT32_MAX) {
			zend_argument_value_error(1, "must be one of MCU_BTN_A..MCU_BTN_RESET");
			RETURN_THROWS();
		}
		RETURN_BOOL(rp2350_button_gpio_is_pressed(gpio));
	}
}

ZEND_FUNCTION(mcu_button_state_mask)
{
	uint32_t irq_state;
	uint32_t mask;

	ZEND_PARSE_PARAMETERS_NONE();
	rp2350_buttons_init();

	irq_state = save_and_disable_interrupts();
	mask = s_button_state_mask;
	restore_interrupts(irq_state);

	RETURN_LONG((zend_long)mask);
}

ZEND_FUNCTION(mcu_button_wait)
{
	zend_long timeout_ms = 0;
	uint32_t start_us;
	uint32_t baseline_generation;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(timeout_ms)
	ZEND_PARSE_PARAMETERS_END();

	if (timeout_ms < 0) {
		timeout_ms = 0;
	}

	rp2350_buttons_init();

	{
		uint32_t irq_state = save_and_disable_interrupts();
		baseline_generation = s_button_irq_generation;
		restore_interrupts(irq_state);
	}
	start_us = time_us_32();

	for (;;) {
		uint32_t irq_state = save_and_disable_interrupts();
		uint32_t generation = s_button_irq_generation;
		uint32_t mask = s_button_state_mask;
		restore_interrupts(irq_state);

		if (generation != baseline_generation) {
			RETURN_LONG((zend_long)mask);
		}
		if (timeout_ms == 0) {
			RETURN_FALSE;
		}
		if ((time_us_32() - start_us) >= (uint32_t)(timeout_ms * 1000)) {
			RETURN_FALSE;
		}
		sleep_ms(1);
	}
}

PHP_MINIT_FUNCTION(rp2350_mcu)
{
	REGISTER_LONG_CONSTANT("MCU_BTN_A", 0, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_BTN_B", 1, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_BTN_C", 2, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_BTN_UP", 3, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_BTN_DOWN", 4, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_BTN_HOME", 5, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_BTN_RESET", 6, CONST_CS | CONST_PERSISTENT);

	REGISTER_LONG_CONSTANT("MCU_LED_0", 0, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_LED_1", 1, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_LED_2", 2, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_LED_3", 3, CONST_CS | CONST_PERSISTENT);

	rp2350_leds_init();
	rp2350_buttons_init();

	return SUCCESS;
}

ZEND_FUNCTION(mcu_led_set)
{
	zend_long index = 0;
	bool on = false;
	uint32_t gpio;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_LONG(index)
		Z_PARAM_BOOL(on)
	ZEND_PARSE_PARAMETERS_END();

	gpio = rp2350_led_gpio_for_index(index);
	if (gpio == UINT32_MAX) {
		zend_argument_value_error(1, "must be between 0 and 3");
		RETURN_THROWS();
	}

	rp2350_leds_init();

	gpio_put(gpio, on ? 1 : 0);
	RETURN_TRUE;
}

ZEND_FUNCTION(mcu_epd_fill)
{
	bool black = false;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_BOOL(black)
	ZEND_PARSE_PARAMETERS_END();

	RETURN_BOOL(rp2350_epd_fill(black));
}

ZEND_FUNCTION(mcu_epd_clear)
{
	bool black = false;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_BOOL(black)
	ZEND_PARSE_PARAMETERS_END();

	RETURN_BOOL(rp2350_epd_clear(black));
}

ZEND_FUNCTION(mcu_epd_set_pixel)
{
	zend_long x = 0;
	zend_long y = 0;
	bool black = false;

	ZEND_PARSE_PARAMETERS_START(3, 3)
		Z_PARAM_LONG(x)
		Z_PARAM_LONG(y)
		Z_PARAM_BOOL(black)
	ZEND_PARSE_PARAMETERS_END();

	RETURN_BOOL(rp2350_epd_set_pixel((int)x, (int)y, black));
}

ZEND_FUNCTION(mcu_epd_update)
{
	ZEND_PARSE_PARAMETERS_NONE();
	RETURN_BOOL(rp2350_epd_update());
}

ZEND_FUNCTION(mcu_epd_render)
{
	char *bytes = NULL;
	size_t bytes_len = 0;
	zend_long width = 0;
	zend_long height = 0;
	zend_long x = -1;
	zend_long y = -1;
	int draw_x;
	int draw_y;

	ZEND_PARSE_PARAMETERS_START(3, 5)
		Z_PARAM_STRING(bytes, bytes_len)
		Z_PARAM_LONG(width)
		Z_PARAM_LONG(height)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(x)
		Z_PARAM_LONG(y)
	ZEND_PARSE_PARAMETERS_END();

	if (width <= 0 || height <= 0) {
		RETURN_FALSE;
	}

	draw_x = (x >= 0) ? (int)x : (264 - (int)width) / 2;
	draw_y = (y >= 0) ? (int)y : (176 - (int)height) / 2;

	if (!rp2350_epd_clear(false)) {
		RETURN_FALSE;
	}
	if (!rp2350_epd_render_1bpp((const uint8_t *)bytes, bytes_len, (int)width, (int)height, draw_x, draw_y)) {
		RETURN_FALSE;
	}
	RETURN_BOOL(rp2350_epd_update());
}

static void rp2350_zend_error_cb(int type, zend_string *error_filename, const uint32_t error_lineno, zend_string *message)
{
	char line[384];
	const char *file = error_filename ? ZSTR_VAL(error_filename) : "<eval>";
	const char *msg = message ? ZSTR_VAL(message) : "<null>";
	size_t msg_len = message ? ZSTR_LEN(message) : strlen(msg);
	if (msg_len > 140) {
		msg_len = 140;
	}
	int n = snprintf(
		line,
		sizeof(line),
		"[zend:%d] %s:%" PRIu32 " %.*s\r\n",
		type,
		file,
		error_lineno,
		(int)msg_len,
		msg
	);
	if (n > 0) {
		rp2350_platform_write(line, (size_t) n);
	}
	snprintf(
		rp2350_last_zend_error,
		sizeof(rp2350_last_zend_error),
		"type=%d file=%s:%" PRIu32 " msg=%.*s",
		type,
		file,
		error_lineno,
		(int)msg_len,
		msg
	);
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

static int rp2350_sapi_startup(sapi_module_struct *sapi_module)
{
	return php_module_startup(sapi_module, NULL);
}

static size_t rp2350_sapi_ub_write(const char *str, size_t str_length)
{
	return rp2350_platform_write(str, str_length);
}

static void rp2350_sapi_flush(void *server_context)
{
	(void) server_context;
	rp2350_platform_flush();
}

static size_t rp2350_sapi_read_post(char *buffer, size_t count_bytes)
{
	(void)buffer;
	(void)count_bytes;
	return 0;
}

static void rp2350_sapi_treat_data(int arg, char *str, zval *destArray)
{
	(void)str;
	if (destArray) {
		array_init(destArray);
		return;
	}

	switch (arg) {
		case PARSE_GET:
			zval_ptr_dtor_nogc(&PG(http_globals)[TRACK_VARS_GET]);
			array_init(&PG(http_globals)[TRACK_VARS_GET]);
			break;
		case PARSE_POST:
			zval_ptr_dtor_nogc(&PG(http_globals)[TRACK_VARS_POST]);
			array_init(&PG(http_globals)[TRACK_VARS_POST]);
			break;
		case PARSE_COOKIE:
			zval_ptr_dtor_nogc(&PG(http_globals)[TRACK_VARS_COOKIE]);
			array_init(&PG(http_globals)[TRACK_VARS_COOKIE]);
			break;
		default:
			break;
	}
}

static char *rp2350_sapi_read_cookies(void)
{
	return NULL;
}

static void rp2350_sapi_log_message(const char *message, int syslog_type_int)
{
	(void) syslog_type_int;
	if (message) {
		rp2350_platform_write("[php] ", sizeof("[php] ") - 1);
		rp2350_platform_write(message, strlen(message));
		rp2350_platform_write("\r\n", 2);
	}
}

static void rp2350_sapi_register_variables(zval *track_vars_array)
{
	php_import_environment_variables(track_vars_array);
}

static int rp2350_sapi_deactivate(void)
{
	rp2350_platform_flush();
	return SUCCESS;
}

static sapi_module_struct rp2350_sapi_module = {
	"rp2350",
	"RP2350 Embedded SAPI",
	rp2350_sapi_startup,
	php_module_shutdown_wrapper,
	NULL,
	rp2350_sapi_deactivate,
	rp2350_sapi_ub_write,
	rp2350_sapi_flush,
	NULL,
	NULL,
	php_error,
	NULL,
	NULL,
	NULL,
	rp2350_sapi_read_post,
	rp2350_sapi_read_cookies,
	rp2350_sapi_register_variables,
	rp2350_sapi_log_message,
	NULL,
	NULL,
	NULL, /* php_ini_path_override */
	NULL, /* default_post_reader */
	rp2350_sapi_treat_data,
	NULL, /* executable_location */
	0, /* php_ini_ignore */
	0, /* php_ini_ignore_cwd */
	NULL, /* get_fd */
	NULL, /* force_http_10 */
	NULL, /* get_target_uid */
	NULL, /* get_target_gid */
	NULL, /* input_filter */
	NULL, /* ini_defaults */
	0, /* phpinfo_as_text */
	NULL, /* ini_entries */
	NULL, /* additional_functions */
	NULL, /* input_filter_init */
	NULL  /* pre_request_init */
};

static zend_result rp2350_zend_stream_open(zend_file_handle *handle)
{
	char path[192];
	rp2350_vfs_stream_t *stream;
	const rp2350_vfs_file_t *file;

	rp2350_stream_open_seen = true;
	if (!handle || !handle->filename) {
		rp2350_stream_open_last_reason = "invalid-handle";
		return FAILURE;
	}
	snprintf(rp2350_stream_open_last_in, sizeof(rp2350_stream_open_last_in), "%s", ZSTR_VAL(handle->filename));
	if (!rp2350_vfs_resolve_candidate(ZSTR_VAL(handle->filename), path, sizeof(path))) {
		rp2350_stream_open_last_reason = "resolve-fail";
		return FAILURE;
	}
	snprintf(rp2350_stream_open_last_path, sizeof(rp2350_stream_open_last_path), "%s", path);

	file = rp2350_vfs_find(path);
	if (!file) {
		rp2350_stream_open_last_reason = "vfs-miss";
		return FAILURE;
	}
	rp2350_stream_open_last_reason = "vfs-hit";

	stream = (rp2350_vfs_stream_t *)malloc(sizeof(*stream));
	if (!stream) {
		rp2350_stream_open_last_reason = "malloc-fail";
		return FAILURE;
	}
	stream->src = file->source;
	stream->len = file->len;
	stream->pos = 0;

	handle->type = ZEND_HANDLE_STREAM;
	handle->handle.stream.handle = stream;
	handle->handle.stream.isatty = 0;
	handle->handle.stream.reader = rp2350_vfs_reader;
	handle->handle.stream.fsizer = rp2350_vfs_fsizer;
	handle->handle.stream.closer = rp2350_vfs_closer;
	handle->opened_path = zend_string_init(path, strlen(path), 0);
	return SUCCESS;
}

static void rp2350_log_execute_failure(const char *path)
{
	if (EG(exception)) {
		zend_object *ex = EG(exception);
		zval rv_msg;
		zval rv_file;
		zval rv_line;
		zval *zmsg = zend_read_property_ex(ex->ce, ex, ZSTR_KNOWN(ZEND_STR_MESSAGE), 1, &rv_msg);
		zval *zfile = zend_read_property_ex(ex->ce, ex, ZSTR_KNOWN(ZEND_STR_FILE), 1, &rv_file);
		zval *zline = zend_read_property_ex(ex->ce, ex, ZSTR_KNOWN(ZEND_STR_LINE), 1, &rv_line);
		zend_string *smsg = zmsg ? zval_get_string(zmsg) : NULL;
		zend_string *sfile = zfile ? zval_get_string(zfile) : NULL;
		zend_long lineno = zline ? zval_get_long(zline) : 0;

		snprintf(
			rp2350_eval_error_detail,
			sizeof(rp2350_eval_error_detail),
			"execute exception: %s at %s:%ld",
			smsg ? ZSTR_VAL(smsg) : "<null>",
			sfile ? ZSTR_VAL(sfile) : "<null>",
			(long)lineno
		);
		rp2350_eval_error = rp2350_eval_error_detail;

		if (smsg) {
			zend_string_release(smsg);
		}
		if (sfile) {
			zend_string_release(sfile);
		}
	} else {
		zend_string *exec_file = zend_get_executed_filename_ex();
		snprintf(
			rp2350_eval_error_detail,
			sizeof(rp2350_eval_error_detail),
			"execute failure at %s:%u stream=%d reason=%s in=%s path=%s req=%s zend=%s",
			exec_file ? ZSTR_VAL(exec_file) : "<null>",
			(unsigned)zend_get_executed_lineno(),
			rp2350_stream_open_seen ? 1 : 0,
			rp2350_stream_open_last_reason ? rp2350_stream_open_last_reason : "<null>",
			rp2350_stream_open_last_in[0] ? rp2350_stream_open_last_in : "<none>",
			rp2350_stream_open_last_path[0] ? rp2350_stream_open_last_path : "<none>",
			path ? path : "<null>",
			rp2350_last_zend_error[0] ? rp2350_last_zend_error : "<none>"
		);
		rp2350_eval_error = rp2350_eval_error_detail;
	}
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
	char path[192];
	const rp2350_vfs_file_t *file;

	if (!filename) {
		return NULL;
	}
	if (!rp2350_vfs_resolve_candidate(ZSTR_VAL(filename), path, sizeof(path))) {
		return NULL;
	}
	file = rp2350_vfs_find(path);
	if (!file) {
		return NULL;
	}
	return zend_string_init(path, strlen(path), 0);
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
	if (rp2350_zend_started) {
		rp2350_eval_error = "ok";
		return 0;
	}

	if (!rp2350_sapi_started) {
		sapi_startup(&rp2350_sapi_module);
		SG(server_context) = NULL;
		SG(options) |= SAPI_OPTION_NO_CHDIR;
		SG(headers_sent) = 1;
		SG(request_info).no_headers = 1;
		rp2350_sapi_started = true;
	}
	if (php_module_startup(&rp2350_sapi_module, &rp2350_mcu_module_entry) == FAILURE) {
		rp2350_eval_error = "php_module_startup failed";
		return -1;
	}

	s_prev_zend_interrupt_function = zend_interrupt_function;
	zend_interrupt_function = rp2350_zend_interrupt_handler;
	zend_atomic_bool_store_ex(&EG(vm_interrupt), true);

	rp2350_zend_started = true;
	rp2350_eval_error = "ok";
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
			(void)zend_exception_error(EG(exception), E_WARNING);
			zend_clear_exception();
			rp2350_eval_error = "zend_eval_stringl exception";
		} else {
			rp2350_eval_error = "zend_eval_stringl failure";
		}
		return -1;
	}

	rp2350_eval_error = "ok";
	return 0;
}

const char *rp2350_eval_last_error(void)
{
	return rp2350_eval_error;
}

int rp2350_eval_execute_file(const char *path)
{
	zend_file_handle file_handle;
	rp2350_stream_open_seen = false;
	rp2350_stream_open_last_in[0] = '\0';
	rp2350_stream_open_last_path[0] = '\0';
	rp2350_stream_open_last_reason = "none";
	rp2350_last_zend_error[0] = '\0';

	if (rp2350_eval_startup() != 0) {
		rp2350_platform_write("[zend] startup failed\r\n", sizeof("[zend] startup failed\r\n") - 1);
		return -1;
	}

	if (php_request_startup() == FAILURE) {
		rp2350_eval_error = "php_request_startup failed";
		return -1;
	}
	/* Our minimal config stubs don't seed this; zero leads to fread/fgetc zero-byte reads. */
	FG(def_chunk_size) = 8192;
	SG(headers_sent) = 1;
	SG(request_info).no_headers = 1;

	zend_stream_init_filename(&file_handle, path);
	if (php_execute_script(&file_handle) == FAILURE) {
		rp2350_log_execute_failure(path);
		if (EG(exception)) {
			(void)zend_exception_error(EG(exception), E_WARNING);
			zend_clear_exception();
		}
		php_request_shutdown(NULL);
		return -1;
	}

	php_request_shutdown(NULL);
	rp2350_eval_error = "ok";
	return 0;
}
