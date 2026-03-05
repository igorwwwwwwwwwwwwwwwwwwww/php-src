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
#include "pico/sem.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include "mbedtls/ssl.h"
#include "nghttp2/nghttp2.h"
#include "picohttpparser.h"

#include "Zend/zend.h"
#include "Zend/zend_API.h"
#include "Zend/zend_compile.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_execute.h"
#include "Zend/zend_ini.h"
#include "Zend/zend_observer.h"
#include "Zend/zend_smart_str.h"
#include "Zend/zend_smart_string.h"
#include "Zend/zend_stream.h"

#include "SAPI.h"
#include "main/php_main.h"
#include "main/php_globals.h"
#include "main/php_memory_streams.h"
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
static bool s_power_sense_init = false;
static bool s_wifi_init = false;
static struct altcp_tls_config *s_http_tls_config = NULL;
static struct altcp_tls_config *s_h2_tls_config = NULL;
static bool s_nghttp2_smoke_logged = false;
static semaphore_t s_button_sem;
static volatile uint32_t s_button_state_mask = 0;
static volatile uint32_t s_button_irq_pending_mask = 0;
static volatile uint32_t s_button_irq_generation = 0;
static void (*s_prev_zend_interrupt_function)(zend_execute_data *execute_data) = NULL;
#ifndef RP2350_WIFI_SSID
#define RP2350_WIFI_SSID ""
#endif
#ifndef RP2350_WIFI_PASS
#define RP2350_WIFI_PASS ""
#endif
static const char s_env_wifi_ssid[] = RP2350_WIFI_SSID;
static const char s_env_wifi_pass[] = RP2350_WIFI_PASS;

static bool rp2350_http_tls_config_ready(void);
static bool rp2350_h2_tls_config_ready(void);
static bool rp2350_parse_http_url(const char *url, char *host, size_t host_size, u16_t *port, const char **path, bool *is_https);
static struct altcp_pcb *rp2350_altcp_tls_alloc_with_sni(void *arg, u8_t ip_type);
static const char rp2350_ini_entries[] =
	"html_errors=0\n"
	"display_errors=1\n"
	"display_startup_errors=1\n"
	"log_errors=0\n"
	"error_reporting=32767\n"
	"date.timezone=UTC\n"
	"max_execution_time=0\n"
	"memory_limit=32M\n";

extern int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);
int mbedtls_platform_entropy_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
	return mbedtls_hardware_poll(data, output, len, olen);
}

extern void php_printf_to_smart_string(smart_string *buf, const char *format, va_list ap);
extern void php_printf_to_smart_str(smart_str *buf, const char *format, va_list ap);

typedef struct {
	const char *src;
	size_t len;
	size_t pos;
	bool owned;
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
	rp2350_vfs_stream_t *s = (rp2350_vfs_stream_t *)handle;
	if (s && s->owned && s->src) {
		free((void *)s->src);
	}
	free(s);
}

ZEND_FUNCTION(mcu_button_pressed);
ZEND_FUNCTION(mcu_button_state_mask);
ZEND_FUNCTION(mcu_button_wait);
ZEND_FUNCTION(mcu_led_set);
ZEND_FUNCTION(mcu_led_level);
ZEND_FUNCTION(mcu_epd_fill);
ZEND_FUNCTION(mcu_epd_clear);
ZEND_FUNCTION(mcu_epd_set_pixel);
ZEND_FUNCTION(mcu_epd_update);
ZEND_FUNCTION(mcu_epd_render);
ZEND_FUNCTION(mcu_battery_voltage);
ZEND_FUNCTION(mcu_battery_mv);
ZEND_FUNCTION(mcu_usb_connected);
ZEND_FUNCTION(mcu_battery_raw_vbat);
ZEND_FUNCTION(mcu_battery_raw_vref);
ZEND_FUNCTION(mcu_wifi_init);
ZEND_FUNCTION(mcu_wifi_connect);
ZEND_FUNCTION(mcu_wifi_disconnect);
ZEND_FUNCTION(mcu_wifi_status);
ZEND_FUNCTION(mcu_wifi_ip);
ZEND_FUNCTION(mcu_tcp_request);
PHP_MINIT_FUNCTION(rp2350_mcu);

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

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_led_level, 0, 2, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, index, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, level, IS_LONG, 0)
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

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_battery_voltage, 0, 0, IS_DOUBLE, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_usb_connected, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_battery_mv, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_battery_raw_vbat, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_battery_raw_vref, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_wifi_init, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_wifi_connect, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, ssid, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, password, IS_STRING, 1)
	ZEND_ARG_TYPE_INFO(0, timeout_ms, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_wifi_disconnect, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mcu_wifi_status, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_MASK_EX(arginfo_mcu_wifi_ip, 0, 0, MAY_BE_STRING | MAY_BE_FALSE)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_MASK_EX(arginfo_mcu_tcp_request, 0, 3, MAY_BE_STRING | MAY_BE_FALSE)
	ZEND_ARG_TYPE_INFO(0, host, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, payload, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, timeout_ms, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, max_read, IS_LONG, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry rp2350_mcu_functions[] = {
	ZEND_FE(mcu_button_pressed, arginfo_mcu_button_pressed)
	ZEND_FE(mcu_button_state_mask, arginfo_mcu_button_state_mask)
	ZEND_FE(mcu_button_wait, arginfo_mcu_button_wait)
	ZEND_FE(mcu_led_set, arginfo_mcu_led_set)
	ZEND_FE(mcu_led_level, arginfo_mcu_led_level)
	ZEND_FE(mcu_epd_fill, arginfo_mcu_epd_fill)
	ZEND_FE(mcu_epd_clear, arginfo_mcu_epd_clear)
	ZEND_FE(mcu_epd_set_pixel, arginfo_mcu_epd_set_pixel)
	ZEND_FE(mcu_epd_update, arginfo_mcu_epd_update)
	ZEND_FE(mcu_epd_render, arginfo_mcu_epd_render)
	ZEND_FE(mcu_battery_voltage, arginfo_mcu_battery_voltage)
	ZEND_FE(mcu_battery_mv, arginfo_mcu_battery_mv)
	ZEND_FE(mcu_usb_connected, arginfo_mcu_usb_connected)
	ZEND_FE(mcu_battery_raw_vbat, arginfo_mcu_battery_raw_vbat)
	ZEND_FE(mcu_battery_raw_vref, arginfo_mcu_battery_raw_vref)
	ZEND_FE(mcu_wifi_init, arginfo_mcu_wifi_init)
	ZEND_FE(mcu_wifi_connect, arginfo_mcu_wifi_connect)
	ZEND_FE(mcu_wifi_disconnect, arginfo_mcu_wifi_disconnect)
	ZEND_FE(mcu_wifi_status, arginfo_mcu_wifi_status)
	ZEND_FE(mcu_wifi_ip, arginfo_mcu_wifi_ip)
	ZEND_FE(mcu_tcp_request, arginfo_mcu_tcp_request)
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
		uint slice;
		uint chan;
		gpio_set_function(gpios[i], GPIO_FUNC_PWM);
		slice = pwm_gpio_to_slice_num(gpios[i]);
		chan = pwm_gpio_to_channel(gpios[i]);
		pwm_set_wrap(slice, 65535);
		pwm_set_chan_level(slice, chan, 0);
		pwm_set_enabled(slice, true);
	}
	s_leds_init = true;
}

static void rp2350_power_sense_init(void)
{
	if (s_power_sense_init) {
		return;
	}

	/* Match board bring-up expectations: keep switched power rail enabled. */
	gpio_init(BW_SW_POWER_EN);
	gpio_set_dir(BW_SW_POWER_EN, GPIO_OUT);
	gpio_put(BW_SW_POWER_EN, 1);

	/* VBAT_SENSE and SENSE_1V1 are from Badger 2350 reference firmware. */
	adc_init();
	adc_gpio_init(26); /* VBAT_SENSE */
	adc_gpio_init(28); /* SENSE_1V1 */

	gpio_init(BW_VBUS_DETECT);
	gpio_set_dir(BW_VBUS_DETECT, GPIO_IN);
	gpio_disable_pulls(BW_VBUS_DETECT);

	s_power_sense_init = true;
}

static bool rp2350_wifi_init_once(void)
{
	if (s_wifi_init) {
		return true;
	}
	if (cyw43_arch_init() != 0) {
		return false;
	}
	cyw43_arch_enable_sta_mode();
	s_wifi_init = true;
	/* Prewarm HTTPS config early to reduce later allocation failures. */
	(void)rp2350_http_tls_config_ready();
	(void)rp2350_h2_tls_config_ready();
	return true;
}

static bool rp2350_http_tls_config_ready(void)
{
	static const char *s_http1_alpn[] = { "http/1.1", NULL };

	if (s_http_tls_config != NULL) {
		return true;
	}

	cyw43_arch_lwip_begin();
	s_http_tls_config = altcp_tls_create_config_client(NULL, 0);
	if (s_http_tls_config != NULL) {
		if (altcp_tls_configure_alpn_protocols(s_http_tls_config, s_http1_alpn) != 0) {
			altcp_tls_free_config(s_http_tls_config);
			s_http_tls_config = NULL;
		}
	}
	cyw43_arch_lwip_end();

	return s_http_tls_config != NULL;
}

static bool rp2350_h2_tls_config_ready(void)
{
	static const char *s_h2_alpn[] = { "h2", "http/1.1", NULL };

	if (s_h2_tls_config != NULL) {
		return true;
	}

	cyw43_arch_lwip_begin();
	s_h2_tls_config = altcp_tls_create_config_client(NULL, 0);
	if (s_h2_tls_config != NULL) {
		if (altcp_tls_configure_alpn_protocols(s_h2_tls_config, s_h2_alpn) != 0) {
			altcp_tls_free_config(s_h2_tls_config);
			s_h2_tls_config = NULL;
		}
	}
	cyw43_arch_lwip_end();

	return s_h2_tls_config != NULL;
}

static uint16_t rp2350_adc_read_avg(uint input, uint samples)
{
	uint32_t sum = 0;
	uint i;

	for (i = 0; i < samples; i++) {
		adc_select_input(input);
		sleep_us(5);
		(void)adc_read(); /* throw-away after mux switch */
		sleep_us(5);
		sum += adc_read();
		sleep_us(20);
	}

	return (uint16_t)(sum / samples);
}

typedef struct {
	volatile bool done;
	volatile bool ok;
	ip_addr_t addr;
} rp2350_dns_query_t;

typedef struct {
	struct tcp_pcb *pcb;
	char *rx;
	size_t rx_len;
	size_t rx_cap;
	volatile bool connected;
	volatile bool done;
	volatile bool had_error;
	volatile bool truncated;
	err_t last_err;
} rp2350_tcp_ctx_t;

typedef struct {
	struct altcp_pcb *pcb;
	char *rx;
	size_t rx_len;
	size_t rx_cap;
	absolute_time_t last_rx_time;
	volatile bool connected;
	volatile bool done;
	volatile bool had_error;
	volatile bool truncated;
	err_t last_err;
} rp2350_altcp_ctx_t;

static void rp2350_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
	rp2350_dns_query_t *q = (rp2350_dns_query_t *)arg;
	(void)name;
	if (q == NULL) {
		return;
	}
	if (ipaddr != NULL) {
		q->addr = *ipaddr;
		q->ok = true;
	}
	q->done = true;
}

static bool rp2350_lwip_wait_until(absolute_time_t deadline, volatile bool *flag)
{
	while (!(*flag)) {
		if (time_reached(deadline)) {
			return false;
		}
		sleep_ms(1);
	}
	return true;
}

static bool rp2350_dns_resolve(const char *host, uint32_t timeout_ms, ip_addr_t *out_addr)
{
	rp2350_dns_query_t q;
	err_t err;
	absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

	memset(&q, 0, sizeof(q));

	cyw43_arch_lwip_begin();
	err = dns_gethostbyname(host, &q.addr, rp2350_dns_found_cb, &q);
	cyw43_arch_lwip_end();

	if (err == ERR_OK) {
		*out_addr = q.addr;
		return true;
	}
	if (err != ERR_INPROGRESS) {
		return false;
	}
	if (!rp2350_lwip_wait_until(deadline, &q.done) || !q.ok) {
		return false;
	}
	*out_addr = q.addr;
	return true;
}

static err_t rp2350_tcp_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
	rp2350_tcp_ctx_t *ctx = (rp2350_tcp_ctx_t *)arg;
	(void)tpcb;
	if (ctx == NULL) {
		return ERR_OK;
	}
	ctx->last_err = err;
	if (err == ERR_OK) {
		ctx->connected = true;
	} else {
		ctx->had_error = true;
		ctx->done = true;
	}
	return ERR_OK;
}

static err_t rp2350_tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
	rp2350_tcp_ctx_t *ctx = (rp2350_tcp_ctx_t *)arg;
	u16_t recved = 0;

	if (ctx == NULL) {
		if (p != NULL) {
			pbuf_free(p);
		}
		return ERR_OK;
	}
	if (err != ERR_OK) {
		ctx->last_err = err;
		ctx->had_error = true;
		ctx->done = true;
		if (p != NULL) {
			pbuf_free(p);
		}
		return ERR_OK;
	}
	if (p == NULL) {
		ctx->done = true;
		return ERR_OK;
	}

	if (ctx->rx_len < ctx->rx_cap) {
		size_t avail = ctx->rx_cap - ctx->rx_len;
		size_t take = (p->tot_len < avail) ? (size_t)p->tot_len : avail;
		if (take < (size_t)p->tot_len) {
			ctx->truncated = true;
		}
		if (take > 0) {
			pbuf_copy_partial(p, ctx->rx + ctx->rx_len, (u16_t)take, 0);
			ctx->rx_len += take;
		}
	} else {
		ctx->truncated = true;
	}

	recved = p->tot_len;
	tcp_recved(tpcb, recved);
	pbuf_free(p);
	return ERR_OK;
}

static void rp2350_tcp_err_cb(void *arg, err_t err)
{
	rp2350_tcp_ctx_t *ctx = (rp2350_tcp_ctx_t *)arg;
	if (ctx == NULL) {
		return;
	}
	ctx->last_err = err;
	ctx->pcb = NULL;
	ctx->had_error = true;
	ctx->done = true;
}

static void rp2350_tcp_ctx_close(rp2350_tcp_ctx_t *ctx)
{
	if (ctx == NULL || ctx->pcb == NULL) {
		return;
	}
	cyw43_arch_lwip_begin();
	tcp_arg(ctx->pcb, NULL);
	tcp_recv(ctx->pcb, NULL);
	tcp_err(ctx->pcb, NULL);
	{
		err_t err = tcp_close(ctx->pcb);
		if (err != ERR_OK) {
			tcp_abort(ctx->pcb);
		}
	}
	cyw43_arch_lwip_end();
	ctx->pcb = NULL;
}

bool rp2350_net_tcp_request(
	const char *host,
	u16_t port,
	const char *payload,
	size_t payload_len,
	uint32_t timeout_ms,
	size_t max_read,
	char **out,
	size_t *out_len
)
{
	ip_addr_t remote;
	rp2350_tcp_ctx_t ctx;
	absolute_time_t deadline;
	size_t off = 0;

	*out = NULL;
	*out_len = 0;

	if (!rp2350_wifi_init_once()) {
		return false;
	}
	if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
		return false;
	}
	if (!rp2350_dns_resolve(host, timeout_ms, &remote)) {
		return false;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.rx_cap = max_read;
	ctx.rx = (char *)malloc(ctx.rx_cap + 1);
	if (ctx.rx == NULL) {
		return false;
	}

	deadline = make_timeout_time_ms(timeout_ms);

	cyw43_arch_lwip_begin();
	ctx.pcb = tcp_new_ip_type(IP_GET_TYPE(&remote));
	if (ctx.pcb != NULL) {
		tcp_arg(ctx.pcb, &ctx);
		tcp_recv(ctx.pcb, rp2350_tcp_recv_cb);
		tcp_err(ctx.pcb, rp2350_tcp_err_cb);
		ctx.last_err = tcp_connect(ctx.pcb, &remote, port, rp2350_tcp_connected_cb);
		if (ctx.last_err != ERR_OK) {
			tcp_arg(ctx.pcb, NULL);
			tcp_recv(ctx.pcb, NULL);
			tcp_err(ctx.pcb, NULL);
			tcp_abort(ctx.pcb);
			ctx.pcb = NULL;
		}
	}
	cyw43_arch_lwip_end();
	if (ctx.pcb == NULL) {
		free(ctx.rx);
		return false;
	}

	if (!rp2350_lwip_wait_until(deadline, &ctx.connected) || ctx.had_error) {
		rp2350_tcp_ctx_close(&ctx);
		free(ctx.rx);
		return false;
	}

	while (off < payload_len) {
		bool wrote = false;
		cyw43_arch_lwip_begin();
		if (ctx.pcb != NULL) {
			u16_t snd = tcp_sndbuf(ctx.pcb);
			if (snd > 0) {
				u16_t chunk = (u16_t)((payload_len - off) < snd ? (payload_len - off) : snd);
				err_t w = tcp_write(ctx.pcb, payload + off, chunk, TCP_WRITE_FLAG_COPY);
				if (w == ERR_OK) {
					off += chunk;
					(void)tcp_output(ctx.pcb);
					wrote = true;
				} else if (w != ERR_MEM) {
					ctx.had_error = true;
					ctx.last_err = w;
				}
			}
		}
		cyw43_arch_lwip_end();
		if (ctx.had_error || ctx.pcb == NULL) {
			rp2350_tcp_ctx_close(&ctx);
			free(ctx.rx);
			return false;
		}
		if (!wrote) {
			if (time_reached(deadline)) {
				rp2350_tcp_ctx_close(&ctx);
				free(ctx.rx);
				return false;
			}
			sleep_ms(1);
		}
	}

	while (!ctx.done && !ctx.had_error) {
		if (time_reached(deadline)) {
			break;
		}
		sleep_ms(1);
	}

	rp2350_tcp_ctx_close(&ctx);
	ctx.rx[ctx.rx_len] = '\0';
	*out = ctx.rx;
	*out_len = ctx.rx_len;
	return true;
}

typedef struct {
	char *tx;
	size_t tx_len;
	size_t tx_cap;
	size_t tx_off;
	char *body;
	size_t body_len;
	size_t body_cap;
	size_t body_limit;
	bool body_dynamic;
	bool body_truncated;
	int status_code;
	int32_t stream_id;
	bool stream_closed;
	uint32_t stream_error_code;
} rp2350_h2_ctx_t;

static bool rp2350_h2_buf_append(char **buf, size_t *len, size_t *cap, const uint8_t *src, size_t n)
{
	size_t need;
	size_t new_cap;
	char *new_buf;

	if (n == 0) {
		return true;
	}
	need = *len + n;
	if (need <= *cap) {
		memcpy(*buf + *len, src, n);
		*len += n;
		return true;
	}

	new_cap = (*cap == 0) ? 1024 : *cap;
	while (new_cap < need) {
		if (new_cap > (SIZE_MAX / 2)) {
			return false;
		}
		new_cap *= 2;
	}
	new_buf = (char *)realloc(*buf, new_cap);
	if (!new_buf) {
		return false;
	}
	*buf = new_buf;
	*cap = new_cap;
	memcpy(*buf + *len, src, n);
	*len += n;
	return true;
}

static ssize_t rp2350_h2_send_cb(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data)
{
	rp2350_h2_ctx_t *ctx = (rp2350_h2_ctx_t *)user_data;
	(void)session;
	(void)flags;
	if (!rp2350_h2_buf_append(&ctx->tx, &ctx->tx_len, &ctx->tx_cap, data, length)) {
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	}
	return (ssize_t)length;
}

static int rp2350_h2_on_header_cb(nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name, size_t namelen, const uint8_t *value, size_t valuelen, uint8_t flags, void *user_data)
{
	rp2350_h2_ctx_t *ctx = (rp2350_h2_ctx_t *)user_data;
	(void)session;
	(void)flags;

	if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
		return 0;
	}
	if (frame->hd.stream_id != ctx->stream_id) {
		return 0;
	}
	if (namelen == 7 && memcmp(name, ":status", 7) == 0) {
		char tmp[4];
		size_t n = (valuelen < sizeof(tmp) - 1) ? valuelen : (sizeof(tmp) - 1);
		memcpy(tmp, value, n);
		tmp[n] = '\0';
		ctx->status_code = atoi(tmp);
	}
	return 0;
}

static int rp2350_h2_on_data_chunk_recv_cb(nghttp2_session *session, uint8_t flags, int32_t stream_id, const uint8_t *data, size_t len, void *user_data)
{
	rp2350_h2_ctx_t *ctx = (rp2350_h2_ctx_t *)user_data;
	size_t avail;
	size_t take;
	(void)session;
	(void)flags;

	if (stream_id != ctx->stream_id || len == 0) {
		return 0;
	}

	if (ctx->body_dynamic) {
		if (ctx->body_limit > 0 && ctx->body_len >= ctx->body_limit) {
			ctx->body_truncated = true;
			return 0;
		}
		take = len;
		if (ctx->body_limit > 0) {
			size_t remain = ctx->body_limit - ctx->body_len;
			if (take > remain) {
				take = remain;
				ctx->body_truncated = true;
			}
		}
		if (take > 0 && !rp2350_h2_buf_append(&ctx->body, &ctx->body_len, &ctx->body_cap, data, take)) {
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		}
		return 0;
	}

	if (ctx->body_len >= ctx->body_cap) {
		ctx->body_truncated = true;
		return 0;
	}

	avail = ctx->body_cap - ctx->body_len;
	take = (len < avail) ? len : avail;
	if (take < len) {
		ctx->body_truncated = true;
	}
	memcpy(ctx->body + ctx->body_len, data, take);
	ctx->body_len += take;
	return 0;
}

static int rp2350_h2_on_stream_close_cb(nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *user_data)
{
	rp2350_h2_ctx_t *ctx = (rp2350_h2_ctx_t *)user_data;
	(void)session;
	if (ctx && stream_id == ctx->stream_id) {
		ctx->stream_closed = true;
		ctx->stream_error_code = error_code;
	}
	return 0;
}

static bool rp2350_h2_flush_tx(rp2350_h2_ctx_t *h2, rp2350_altcp_ctx_t *net, absolute_time_t deadline)
{
	while (h2->tx_off < h2->tx_len) {
		bool progressed = false;
		cyw43_arch_lwip_begin();
		if (net->pcb != NULL) {
			u16_t snd = altcp_sndbuf(net->pcb);
			if (snd > 0) {
				size_t rem = h2->tx_len - h2->tx_off;
				u16_t chunk = (u16_t)(rem < snd ? rem : snd);
				err_t w = altcp_write(net->pcb, h2->tx + h2->tx_off, chunk, TCP_WRITE_FLAG_COPY);
				if (w == ERR_OK) {
					h2->tx_off += chunk;
					(void)altcp_output(net->pcb);
					progressed = true;
				} else if (w != ERR_MEM) {
					net->had_error = true;
					net->last_err = w;
				}
			}
		}
		cyw43_arch_lwip_end();

		if (net->had_error || net->pcb == NULL) {
			return false;
		}
		if (!progressed) {
			if (time_reached(deadline)) {
				return false;
			}
			sleep_ms(1);
		}
	}

	h2->tx_len = 0;
	h2->tx_off = 0;
	return true;
}

typedef struct {
	struct altcp_tls_config *config;
	const char *hostname;
} rp2350_h2_tls_alloc_ctx_t;

static struct altcp_pcb *rp2350_h2_tls_alloc_with_sni(void *arg, u8_t ip_type)
{
	rp2350_h2_tls_alloc_ctx_t *ctx = (rp2350_h2_tls_alloc_ctx_t *)arg;
	struct altcp_pcb *pcb;

	if (!ctx || !ctx->config) {
		return NULL;
	}

	pcb = altcp_tls_alloc(ctx->config, ip_type);
	if (!pcb) {
		return NULL;
	}

	if (ctx->hostname && ctx->hostname[0] != '\0') {
		void *tls = altcp_tls_context(pcb);
		if (tls) {
			(void)mbedtls_ssl_set_hostname((mbedtls_ssl_context *)tls, ctx->hostname);
		}
	}
	return pcb;
}

static err_t rp2350_h2_altcp_connected_cb(void *arg, struct altcp_pcb *conn, err_t err)
{
	rp2350_altcp_ctx_t *ctx = (rp2350_altcp_ctx_t *)arg;
	(void)conn;
	if (!ctx) {
		return ERR_ARG;
	}
	if (err == ERR_OK) {
		ctx->connected = true;
	} else {
		ctx->had_error = true;
		ctx->last_err = err;
		ctx->done = true;
	}
	return ERR_OK;
}

static err_t rp2350_h2_altcp_recv_cb(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err)
{
	rp2350_altcp_ctx_t *ctx = (rp2350_altcp_ctx_t *)arg;
	size_t avail;
	size_t take;
	u16_t recvd;

	if (!ctx) {
		if (p) {
			pbuf_free(p);
		}
		return ERR_OK;
	}
	if (err != ERR_OK) {
		if (p) {
			pbuf_free(p);
		}
		ctx->had_error = true;
		ctx->last_err = err;
		ctx->done = true;
		return err;
	}
	if (!p) {
		ctx->done = true;
		return ERR_OK;
	}

	if (ctx->rx_len < ctx->rx_cap) {
		avail = ctx->rx_cap - ctx->rx_len;
		take = (p->tot_len < avail) ? (size_t)p->tot_len : avail;
		if (take < (size_t)p->tot_len) {
			ctx->truncated = true;
		}
		if (take > 0) {
			pbuf_copy_partial(p, ctx->rx + ctx->rx_len, (u16_t)take, 0);
			ctx->rx_len += take;
		}
	} else {
		ctx->truncated = true;
	}

	recvd = p->tot_len;
	altcp_recved(conn, recvd);
	pbuf_free(p);
	ctx->last_rx_time = get_absolute_time();
	return ERR_OK;
}

static void rp2350_h2_altcp_err_cb(void *arg, err_t err)
{
	rp2350_altcp_ctx_t *ctx = (rp2350_altcp_ctx_t *)arg;
	if (!ctx) {
		return;
	}
	ctx->last_err = err;
	ctx->pcb = NULL;
	ctx->had_error = true;
	ctx->done = true;
}

static void rp2350_h2_altcp_close(rp2350_altcp_ctx_t *ctx)
{
	if (!ctx || !ctx->pcb) {
		return;
	}
	cyw43_arch_lwip_begin();
	altcp_arg(ctx->pcb, NULL);
	altcp_recv(ctx->pcb, NULL);
	altcp_err(ctx->pcb, NULL);
	{
		err_t err = altcp_close(ctx->pcb);
		if (err != ERR_OK) {
			altcp_abort(ctx->pcb);
		}
	}
	cyw43_arch_lwip_end();
	ctx->pcb = NULL;
}

static bool rp2350_net_tls_request(
	const char *host,
	u16_t port,
	const char *payload,
	size_t payload_len,
	uint32_t timeout_ms,
	size_t max_read,
	char **out,
	size_t *out_len
)
{
	ip_addr_t remote;
	rp2350_altcp_ctx_t ctx;
	absolute_time_t deadline;
	size_t off = 0;
	rp2350_h2_tls_alloc_ctx_t tls_ctx;
	altcp_allocator_t tls_allocator;

	*out = NULL;
	*out_len = 0;

	if (!rp2350_wifi_init_once()) {
		return false;
	}
	if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
		return false;
	}
	if (!rp2350_h2_tls_config_ready()) {
		return false;
	}
	if (!rp2350_dns_resolve(host, timeout_ms, &remote)) {
		return false;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.rx_cap = max_read;
	ctx.rx = (char *)malloc(max_read + 1);
	if (!ctx.rx) {
		return false;
	}
	ctx.last_rx_time = get_absolute_time();

	tls_ctx.config = s_h2_tls_config;
	tls_ctx.hostname = host;
	tls_allocator.alloc = rp2350_h2_tls_alloc_with_sni;
	tls_allocator.arg = &tls_ctx;
	deadline = make_timeout_time_ms(timeout_ms);

	cyw43_arch_lwip_begin();
	ctx.pcb = altcp_new_ip_type(&tls_allocator, IP_GET_TYPE(&remote));
	if (ctx.pcb != NULL) {
		altcp_arg(ctx.pcb, &ctx);
		altcp_recv(ctx.pcb, rp2350_h2_altcp_recv_cb);
		altcp_err(ctx.pcb, rp2350_h2_altcp_err_cb);
		ctx.last_err = altcp_connect(ctx.pcb, &remote, port, rp2350_h2_altcp_connected_cb);
		if (ctx.last_err != ERR_OK) {
			altcp_arg(ctx.pcb, NULL);
			altcp_recv(ctx.pcb, NULL);
			altcp_err(ctx.pcb, NULL);
			altcp_abort(ctx.pcb);
			ctx.pcb = NULL;
		}
	}
	cyw43_arch_lwip_end();
	if (ctx.pcb == NULL) {
		free(ctx.rx);
		return false;
	}

	if (!rp2350_lwip_wait_until(deadline, &ctx.connected) || ctx.had_error) {
		rp2350_h2_altcp_close(&ctx);
		free(ctx.rx);
		return false;
	}

	while (off < payload_len) {
		bool wrote = false;
		cyw43_arch_lwip_begin();
		if (ctx.pcb != NULL) {
			u16_t snd = altcp_sndbuf(ctx.pcb);
			if (snd > 0) {
				u16_t chunk = (u16_t)((payload_len - off) < snd ? (payload_len - off) : snd);
				err_t w = altcp_write(ctx.pcb, payload + off, chunk, TCP_WRITE_FLAG_COPY);
				if (w == ERR_OK) {
					off += chunk;
					(void)altcp_output(ctx.pcb);
					wrote = true;
				} else if (w != ERR_MEM) {
					ctx.had_error = true;
					ctx.last_err = w;
				}
			}
		}
		cyw43_arch_lwip_end();
		if (ctx.had_error || ctx.pcb == NULL) {
			rp2350_h2_altcp_close(&ctx);
			free(ctx.rx);
			return false;
		}
		if (!wrote) {
			if (time_reached(deadline)) {
				rp2350_h2_altcp_close(&ctx);
				free(ctx.rx);
				return false;
			}
			sleep_ms(1);
		}
	}

	/* Signal end-of-request bytes so the peer can finish and close promptly. */
	cyw43_arch_lwip_begin();
	if (ctx.pcb != NULL) {
		(void)altcp_shutdown(ctx.pcb, 0, 1);
	}
	cyw43_arch_lwip_end();

	while (!ctx.done && !ctx.had_error) {
		if (time_reached(deadline)) {
			break;
		}
		sleep_ms(1);
	}

	rp2350_h2_altcp_close(&ctx);
	ctx.rx[ctx.rx_len] = '\0';
	*out = ctx.rx;
	*out_len = ctx.rx_len;
	return true;
}

static bool rp2350_h2_get_body_ex(
	const char *url,
	uint32_t timeout_ms,
	size_t max_read,
	char **body_out,
	size_t *body_len_out,
	bool *alpn_http1_out
)
{
	char host[96];
	u16_t port;
	const char *path;
	bool is_https = false;
	rp2350_h2_ctx_t ctx;
	rp2350_altcp_ctx_t net;
	ip_addr_t remote;
	rp2350_h2_tls_alloc_ctx_t tls_ctx;
	altcp_allocator_t tls_allocator;
	absolute_time_t deadline;
	nghttp2_session_callbacks *callbacks = NULL;
	nghttp2_session *session = NULL;
	nghttp2_nv nva[5];
	size_t parsed_off;
	size_t rx_cap;
	ssize_t parsed;
	int rc;
	const char *alpn = NULL;
	bool ok = false;

	*body_out = NULL;
	*body_len_out = 0;
	if (alpn_http1_out) {
		*alpn_http1_out = false;
	}

	if (!rp2350_parse_http_url(url, host, sizeof(host), &port, &path, &is_https)) {
		php_error_docref(NULL, E_WARNING, "h2 url parse failed: %s", url ? url : "<null>");
		return false;
	}
	if (!is_https) {
		php_error_docref(NULL, E_WARNING, "h2 get currently requires https:// with ALPN h2");
		return false;
	}
	if (max_read < 1) {
		max_read = 1;
	} else if (max_read > 262144) {
		max_read = 262144;
	}
	if (!rp2350_wifi_init_once()) {
		php_error_docref(NULL, E_WARNING, "h2 wifi init failed");
		return false;
	}
	if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
		php_error_docref(NULL, E_WARNING, "h2 wifi not connected");
		return false;
	}
	if (!rp2350_h2_tls_config_ready()) {
		php_error_docref(NULL, E_WARNING, "h2 tls config not ready");
		return false;
	}
	if (!rp2350_dns_resolve(host, timeout_ms, &remote)) {
		php_error_docref(NULL, E_WARNING, "h2 dns resolve failed: host=%s", host);
		return false;
	}

	memset(&ctx, 0, sizeof(ctx));
	memset(&net, 0, sizeof(net));
	ctx.body = (char *)malloc(max_read + 1);
	if (!ctx.body) {
		php_error_docref(NULL, E_WARNING, "h2 body alloc failed: cap=%u", (unsigned)max_read);
		return false;
	}
	ctx.body_cap = max_read;
	ctx.body_limit = max_read;
	ctx.body_dynamic = false;
	rx_cap = max_read + 32768;
	net.rx = (char *)malloc(rx_cap + 1);
	if (!net.rx) {
		php_error_docref(NULL, E_WARNING, "h2 rx alloc failed: cap=%u", (unsigned)rx_cap);
		free(ctx.body);
		return false;
	}
	net.rx_cap = rx_cap;
	net.last_rx_time = get_absolute_time();
	parsed_off = 0;

	rc = nghttp2_session_callbacks_new(&callbacks);
	if (rc != 0) {
		php_error_docref(NULL, E_WARNING, "h2 callbacks alloc failed: %d", rc);
		goto cleanup;
	}
	nghttp2_session_callbacks_set_send_callback(callbacks, rp2350_h2_send_cb);
	nghttp2_session_callbacks_set_on_header_callback(callbacks, rp2350_h2_on_header_cb);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, rp2350_h2_on_data_chunk_recv_cb);
	nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, rp2350_h2_on_stream_close_cb);

	rc = nghttp2_session_client_new(&session, callbacks, &ctx);
	if (rc != 0) {
		php_error_docref(NULL, E_WARNING, "h2 session alloc failed: %d", rc);
		goto cleanup;
	}

	rc = nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, NULL, 0);
	if (rc != 0) {
		php_error_docref(NULL, E_WARNING, "h2 submit settings failed: %d", rc);
		goto cleanup;
	}

	memset(nva, 0, sizeof(nva));
	nva[0].name = (uint8_t *)":method";
	nva[0].value = (uint8_t *)"GET";
	nva[0].namelen = sizeof(":method") - 1;
	nva[0].valuelen = sizeof("GET") - 1;
	nva[1].name = (uint8_t *)":scheme";
	nva[1].value = (uint8_t *)"https";
	nva[1].namelen = sizeof(":scheme") - 1;
	nva[1].valuelen = sizeof("https") - 1;
	nva[2].name = (uint8_t *)":authority";
	nva[2].value = (uint8_t *)host;
	nva[2].namelen = sizeof(":authority") - 1;
	nva[2].valuelen = strlen(host);
	nva[3].name = (uint8_t *)":path";
	nva[3].value = (uint8_t *)path;
	nva[3].namelen = sizeof(":path") - 1;
	nva[3].valuelen = strlen(path);
	nva[4].name = (uint8_t *)"user-agent";
	nva[4].value = (uint8_t *)"rp2350-nghttp2";
	nva[4].namelen = sizeof("user-agent") - 1;
	nva[4].valuelen = sizeof("rp2350-nghttp2") - 1;

	ctx.stream_id = nghttp2_submit_request(session, NULL, nva, 5, NULL, NULL);
	if (ctx.stream_id < 0) {
		php_error_docref(NULL, E_WARNING, "h2 submit request failed: %d", (int)ctx.stream_id);
		goto cleanup;
	}

	rc = nghttp2_session_send(session);
	if (rc != 0) {
		php_error_docref(NULL, E_WARNING, "h2 encode failed: %d", rc);
		goto cleanup;
	}

	tls_ctx.config = s_h2_tls_config;
	tls_ctx.hostname = host;
	tls_allocator.alloc = rp2350_h2_tls_alloc_with_sni;
	tls_allocator.arg = &tls_ctx;
	deadline = make_timeout_time_ms(timeout_ms);

	cyw43_arch_lwip_begin();
	net.pcb = altcp_new_ip_type(&tls_allocator, IP_GET_TYPE(&remote));
	if (net.pcb != NULL) {
		altcp_arg(net.pcb, &net);
		altcp_recv(net.pcb, rp2350_h2_altcp_recv_cb);
		altcp_err(net.pcb, rp2350_h2_altcp_err_cb);
		net.last_err = altcp_connect(net.pcb, &remote, port, rp2350_h2_altcp_connected_cb);
		if (net.last_err != ERR_OK) {
			altcp_arg(net.pcb, NULL);
			altcp_recv(net.pcb, NULL);
			altcp_err(net.pcb, NULL);
			altcp_abort(net.pcb);
			net.pcb = NULL;
		}
	}
	cyw43_arch_lwip_end();
	if (net.pcb == NULL) {
		php_error_docref(NULL, E_WARNING, "h2 connect alloc/connect failed");
		goto cleanup;
	}
	if (!rp2350_lwip_wait_until(deadline, &net.connected) || net.had_error) {
		php_error_docref(NULL, E_WARNING, "h2 connect timeout/error: lwip=%d", (int)net.last_err);
		goto cleanup;
	}

	cyw43_arch_lwip_begin();
	if (net.pcb != NULL) {
		void *tls = altcp_tls_context(net.pcb);
		if (tls != NULL) {
			alpn = mbedtls_ssl_get_alpn_protocol((mbedtls_ssl_context *)tls);
		}
	}
	cyw43_arch_lwip_end();
	if (alpn == NULL || strcmp(alpn, "h2") != 0) {
		if (alpn_http1_out && alpn && strcmp(alpn, "http/1.1") == 0) {
			*alpn_http1_out = true;
		} else {
			php_error_docref(NULL, E_WARNING, "h2 ALPN negotiation failed: got=%s", alpn ? alpn : "<null>");
		}
		goto cleanup;
	}

	while (!time_reached(deadline) && !net.had_error && !ctx.stream_closed) {
		if (!rp2350_h2_flush_tx(&ctx, &net, deadline)) {
			break;
		}
		if (net.rx_len > parsed_off) {
			parsed = nghttp2_session_mem_recv(
				session,
				(const uint8_t *)(net.rx + parsed_off),
				net.rx_len - parsed_off
			);
			if (parsed < 0) {
				php_error_docref(NULL, E_WARNING, "h2 parse failed: %s", nghttp2_strerror((int)parsed));
				goto cleanup;
			}
			parsed_off += (size_t)parsed;

			rc = nghttp2_session_send(session);
			if (rc != 0) {
				php_error_docref(NULL, E_WARNING, "h2 session_send failed: %d", rc);
				goto cleanup;
			}
			continue;
		}
		sleep_ms(1);
	}

	if (net.had_error) {
		php_error_docref(NULL, E_WARNING, "h2 transport error: lwip=%d", (int)net.last_err);
		goto cleanup;
	}
	if (ctx.status_code <= 0) {
		char preview[49];
		size_t n = net.rx_len < 48 ? net.rx_len : 48;
		size_t i;
		for (i = 0; i < n; i++) {
			char c = net.rx[i];
			preview[i] = (c >= 32 && c <= 126) ? c : '.';
		}
		preview[n] = '\0';
		php_error_docref(
			NULL,
			E_WARNING,
			"h2 response missing :status (rx=%u parsed=%u stream_id=%d stream_closed=%d stream_err=0x%x body=%u head=\"%s\")",
			(unsigned)net.rx_len,
			(unsigned)parsed_off,
			(int)ctx.stream_id,
			ctx.stream_closed ? 1 : 0,
			(unsigned)ctx.stream_error_code,
			(unsigned)ctx.body_len,
			preview
		);
		goto cleanup;
	}
	if (ctx.status_code < 200 || ctx.status_code >= 300) {
		php_error_docref(NULL, E_WARNING, "h2 status=%d", ctx.status_code);
		goto cleanup;
	}
	if (ctx.body_len == 0) {
		php_error_docref(NULL, E_WARNING, "h2 empty body: status=%d parsed=%u rx=%u stream_closed=%d",
			ctx.status_code,
			(unsigned)parsed_off,
			(unsigned)net.rx_len,
			ctx.stream_closed ? 1 : 0);
		goto cleanup;
	}
	if (ctx.body_truncated) {
		php_error_docref(NULL, E_WARNING, "h2 body truncated: cap=%u", (unsigned)max_read);
	}

	ctx.body[ctx.body_len] = '\0';
	*body_out = ctx.body;
	*body_len_out = ctx.body_len;
	ctx.body = NULL;
	ok = true;

cleanup:
	if (net.pcb != NULL) {
		rp2350_h2_altcp_close(&net);
	}
	free(net.rx);
	nghttp2_session_del(session);
	nghttp2_session_callbacks_del(callbacks);
	free(ctx.tx);
	if (ctx.body != NULL) {
		free(ctx.body);
	}
	return ok;
}

static bool rp2350_h2_get_body(const char *url, uint32_t timeout_ms, size_t max_read, char **body_out, size_t *body_len_out)
{
	return rp2350_h2_get_body_ex(url, timeout_ms, max_read, body_out, body_len_out, NULL);
}

static bool rp2350_parse_http_url(const char *url, char *host, size_t host_size, u16_t *port, const char **path, bool *is_https)
{
	const char *p;
	const char *host_start;
	size_t host_len;
	unsigned long parsed_port = 80;
	char *endptr;

	if (strncmp(url, "http://", 7) == 0) {
		p = url + 7;
		*is_https = false;
		parsed_port = 80;
	} else if (strncmp(url, "https://", 8) == 0) {
		p = url + 8;
		*is_https = true;
		parsed_port = 443;
	} else {
		return false;
	}
	host_start = p;
	while (*p && *p != '/' && *p != ':') {
		p++;
	}
	if (p == host_start) {
		return false;
	}

	host_len = (size_t)(p - host_start);
	if (host_len + 1 > host_size) {
		return false;
	}
	memcpy(host, host_start, host_len);
	host[host_len] = '\0';

	if (*p == ':') {
		const char *port_start = p + 1;
		while (*p && *p != '/') {
			p++;
		}
		{
			size_t n = (size_t)(p - port_start);
			char tmp[8];
			if (n == 0 || n >= sizeof(tmp)) {
				return false;
			}
			memcpy(tmp, port_start, n);
			tmp[n] = '\0';
			parsed_port = strtoul(tmp, &endptr, 10);
			if (*endptr != '\0' || parsed_port == 0 || parsed_port > 65535) {
				return false;
			}
		}
	}

	*port = (u16_t)parsed_port;
	*path = (*p == '/') ? p : "/";
	return true;
}

typedef struct {
	struct altcp_tls_config *config;
	const char *hostname;
} rp2350_tls_alloc_ctx_t;

static struct altcp_pcb *rp2350_altcp_tls_alloc_with_sni(void *arg, u8_t ip_type)
{
	rp2350_tls_alloc_ctx_t *ctx = (rp2350_tls_alloc_ctx_t *)arg;
	struct altcp_pcb *pcb;

	if (!ctx || !ctx->config) {
		return NULL;
	}

	pcb = altcp_tls_alloc(ctx->config, ip_type);
	if (!pcb) {
		return NULL;
	}

	if (ctx->hostname && ctx->hostname[0] != '\0') {
		void *tls = altcp_tls_context(pcb);
		if (tls) {
			int rc = mbedtls_ssl_set_hostname((mbedtls_ssl_context *)tls, ctx->hostname);
			if (rc != 0) {
				php_error_docref(NULL, E_WARNING, "https SNI setup failed for host=%s mbedtls=%d", ctx->hostname, rc);
			}
		}
	}

	return pcb;
}

static bool rp2350_http_build_get_request(const char *host, const char *path, char **req_out, size_t *req_len_out)
{
	int n;
	char *req;

	n = snprintf(
		NULL,
		0,
		"GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: rp2350-php\r\nAccept: */*\r\n\r\n",
		path,
		host
	);
	if (n <= 0) {
		return false;
	}
	req = (char *)malloc((size_t)n + 1);
	if (!req) {
		return false;
	}
	snprintf(
		req,
		(size_t)n + 1,
		"GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: rp2350-php\r\nAccept: */*\r\n\r\n",
		path,
		host
	);
	*req_out = req;
	*req_len_out = (size_t)n;
	return true;
}

static bool rp2350_http_h1_extract_body(
	const char *resp,
	size_t resp_len,
	size_t max_read,
	char **body_out,
	size_t *body_len_out
)
{
	int minor_version = 0;
	int status = 0;
	const char *msg = NULL;
	size_t msg_len = 0;
	struct phr_header headers[48];
	size_t num_headers = sizeof(headers) / sizeof(headers[0]);
	int parsed;
	char *body;
	size_t body_len;

	*body_out = NULL;
	*body_len_out = 0;
	parsed = phr_parse_response(
		resp,
		resp_len,
		&minor_version,
		&status,
		&msg,
		&msg_len,
		headers,
		&num_headers,
		0
	);
	if (parsed <= 0) {
		php_error_docref(NULL, E_WARNING, "http parse failed: parsed=%d", parsed);
		return false;
	}
	if (status < 100 || status > 599) {
		php_error_docref(NULL, E_WARNING, "http parse status invalid: %d", status);
		return false;
	}
	body_len = resp_len - (size_t)parsed;
	if (body_len > max_read) {
		body_len = max_read;
	}
	body = (char *)malloc(body_len + 1);
	if (!body) {
		return false;
	}
	memcpy(body, resp + parsed, body_len);
	body[body_len] = '\0';
	*body_out = body;
	*body_len_out = body_len;
	return true;
}

static bool rp2350_net_tls_request_http1(
	const char *host,
	u16_t port,
	const char *payload,
	size_t payload_len,
	uint32_t timeout_ms,
	size_t max_read,
	char **out,
	size_t *out_len
)
{
	ip_addr_t remote;
	rp2350_altcp_ctx_t ctx;
	absolute_time_t deadline;
	size_t off = 0;
	altcp_allocator_t tls_allocator;
	rp2350_tls_alloc_ctx_t tls_ctx;

	*out = NULL;
	*out_len = 0;

	if (!rp2350_wifi_init_once()) {
		return false;
	}
	if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
		return false;
	}
	if (!rp2350_http_tls_config_ready()) {
		return false;
	}
	if (!rp2350_dns_resolve(host, timeout_ms, &remote)) {
		return false;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.rx_cap = max_read;
	ctx.rx = (char *)malloc(ctx.rx_cap + 1);
	if (ctx.rx == NULL) {
		return false;
	}
	ctx.last_rx_time = get_absolute_time();

	tls_ctx.config = s_http_tls_config;
	tls_ctx.hostname = host;
	tls_allocator.alloc = rp2350_altcp_tls_alloc_with_sni;
	tls_allocator.arg = &tls_ctx;
	deadline = make_timeout_time_ms(timeout_ms);

	cyw43_arch_lwip_begin();
	ctx.pcb = altcp_new_ip_type(&tls_allocator, IP_GET_TYPE(&remote));
	if (ctx.pcb != NULL) {
		altcp_arg(ctx.pcb, &ctx);
		altcp_recv(ctx.pcb, rp2350_h2_altcp_recv_cb);
		altcp_err(ctx.pcb, rp2350_h2_altcp_err_cb);
		ctx.last_err = altcp_connect(ctx.pcb, &remote, port, rp2350_h2_altcp_connected_cb);
		if (ctx.last_err != ERR_OK) {
			altcp_arg(ctx.pcb, NULL);
			altcp_recv(ctx.pcb, NULL);
			altcp_err(ctx.pcb, NULL);
			altcp_abort(ctx.pcb);
			ctx.pcb = NULL;
		}
	}
	cyw43_arch_lwip_end();
	if (ctx.pcb == NULL) {
		free(ctx.rx);
		return false;
	}

	if (!rp2350_lwip_wait_until(deadline, &ctx.connected) || ctx.had_error) {
		rp2350_h2_altcp_close(&ctx);
		free(ctx.rx);
		return false;
	}

	while (off < payload_len) {
		bool wrote = false;
		cyw43_arch_lwip_begin();
		if (ctx.pcb != NULL) {
			u16_t snd = altcp_sndbuf(ctx.pcb);
			if (snd > 0) {
				u16_t chunk = (u16_t)((payload_len - off) < snd ? (payload_len - off) : snd);
				err_t w = altcp_write(ctx.pcb, payload + off, chunk, TCP_WRITE_FLAG_COPY);
				if (w == ERR_OK) {
					off += chunk;
					(void)altcp_output(ctx.pcb);
					wrote = true;
				} else if (w != ERR_MEM) {
					ctx.had_error = true;
					ctx.last_err = w;
				}
			}
		}
		cyw43_arch_lwip_end();
		if (ctx.had_error || ctx.pcb == NULL) {
			rp2350_h2_altcp_close(&ctx);
			free(ctx.rx);
			return false;
		}
		if (!wrote) {
			if (time_reached(deadline)) {
				rp2350_h2_altcp_close(&ctx);
				free(ctx.rx);
				return false;
			}
			sleep_ms(1);
		}
	}

	cyw43_arch_lwip_begin();
	if (ctx.pcb != NULL) {
		(void)altcp_shutdown(ctx.pcb, 0, 1);
	}
	cyw43_arch_lwip_end();

	while (!ctx.done && !ctx.had_error) {
		if (time_reached(deadline)) {
			break;
		}
		sleep_ms(1);
	}

	rp2350_h2_altcp_close(&ctx);
	ctx.rx[ctx.rx_len] = '\0';
	*out = ctx.rx;
	*out_len = ctx.rx_len;
	return true;
}

static bool rp2350_http_get_body(const char *url, uint32_t timeout_ms, size_t max_read, char **body_out, size_t *body_len_out)
{
	char host[96];
	u16_t port;
	const char *path;
	bool is_https = false;
	char *request = NULL;
	size_t request_len = 0;
	char *response = NULL;
	size_t response_len = 0;
	bool h2_alpn_http1 = false;

	*body_out = NULL;
	*body_len_out = 0;

	if (!rp2350_parse_http_url(url, host, sizeof(host), &port, &path, &is_https)) {
		php_error_docref(NULL, E_WARNING, "http url parse failed: %s", url ? url : "<null>");
		return false;
	}

	if (is_https) {
		if (rp2350_h2_get_body_ex(url, timeout_ms, max_read, body_out, body_len_out, &h2_alpn_http1)) {
			return true;
		}
		if (!h2_alpn_http1) {
			return false;
		}
	}

	if (!rp2350_http_build_get_request(host, path, &request, &request_len)) {
		php_error_docref(NULL, E_WARNING, "http request build failed");
		return false;
	}

	if (is_https) {
		if (!rp2350_net_tls_request_http1(host, port, request, request_len, timeout_ms, max_read + 8192, &response, &response_len)) {
			free(request);
			return false;
		}
	} else {
		if (!rp2350_net_tcp_request(host, port, request, request_len, timeout_ms, max_read + 8192, &response, &response_len)) {
			free(request);
			return false;
		}
	}
	free(request);

	if (!rp2350_http_h1_extract_body(response, response_len, max_read, body_out, body_len_out)) {
		free(response);
		return false;
	}
	free(response);
	return true;
}

typedef enum {
	RP2350_HTTP_STREAM_MODE_H1 = 0,
	RP2350_HTTP_STREAM_MODE_H2 = 1,
} rp2350_http_stream_mode_t;

typedef struct {
	rp2350_altcp_ctx_t net;
	uint32_t timeout_ms;
	rp2350_http_stream_mode_t mode;
	bool headers_done;
	bool response_parsed;
	nghttp2_session_callbacks *h2_callbacks;
	nghttp2_session *h2_session;
	rp2350_h2_ctx_t h2;
} rp2350_http_stream_data_t;

static ssize_t rp2350_http_stream_write(php_stream *stream, const char *buf, size_t count)
{
	(void)stream;
	(void)buf;
	(void)count;
	return -1;
}

static ssize_t rp2350_http_stream_read(php_stream *stream, char *buf, size_t count)
{
	rp2350_http_stream_data_t *st = (rp2350_http_stream_data_t *)stream->abstract;
	absolute_time_t deadline;

	if (!st || count == 0) {
		return 0;
	}

	deadline = make_timeout_time_ms(st->timeout_ms);

	if (st->mode == RP2350_HTTP_STREAM_MODE_H2) {
		while (true) {
			bool done = false;
			bool had_error = false;
			ssize_t parsed = 0;

			if (st->h2.body_len > 0) {
				size_t take = (count < st->h2.body_len) ? count : st->h2.body_len;
				memcpy(buf, st->h2.body, take);
				if (take < st->h2.body_len) {
					memmove(st->h2.body, st->h2.body + take, st->h2.body_len - take);
				}
				st->h2.body_len -= take;
				return (ssize_t)take;
			}
			if (st->h2.stream_closed) {
				stream->eof = 1;
				return 0;
			}

			cyw43_arch_lwip_begin();
			done = st->net.done;
			had_error = st->net.had_error;
			if (st->net.rx_len > 0) {
				parsed = nghttp2_session_mem_recv(
					st->h2_session,
					(const uint8_t *)st->net.rx,
					st->net.rx_len
				);
				if (parsed >= 0) {
					size_t used = (size_t)parsed;
					if (used < st->net.rx_len) {
						memmove(st->net.rx, st->net.rx + used, st->net.rx_len - used);
					}
					st->net.rx_len -= used;
				}
			}
			cyw43_arch_lwip_end();

			if (parsed < 0) {
				stream->eof = 1;
				return 0;
			}
			if (had_error || done) {
				stream->eof = 1;
				return 0;
			}

			if (parsed > 0) {
				int rc = nghttp2_session_send(st->h2_session);
				if (rc != 0) {
					stream->eof = 1;
					return 0;
				}
				if (!rp2350_h2_flush_tx(&st->h2, &st->net, deadline)) {
					stream->eof = 1;
					return 0;
				}
				continue;
			}

			if (time_reached(deadline)) {
				stream->eof = 1;
				return 0;
			}
			sleep_ms(1);
		}
	}

	while (true) {
		size_t rx_len = 0;
		bool done = false;
		bool had_error = false;

		cyw43_arch_lwip_begin();
		rx_len = st->net.rx_len;
		done = st->net.done;
		had_error = st->net.had_error;

		if (!st->headers_done && rx_len > 0) {
			size_t i;
			size_t parsed = 0;
			for (i = 0; i + 3 < rx_len; i++) {
				if (st->net.rx[i] == '\r' && st->net.rx[i + 1] == '\n' && st->net.rx[i + 2] == '\r' && st->net.rx[i + 3] == '\n') {
					parsed = i + 4;
					break;
				}
			}
			if (parsed > 0) {
				if (!st->response_parsed) {
					int minor_version = 0;
					int status = 0;
					const char *msg = NULL;
					size_t msg_len = 0;
					struct phr_header headers[48];
					size_t num_headers = sizeof(headers) / sizeof(headers[0]);
					int rc = phr_parse_response(
						st->net.rx,
						parsed,
						&minor_version,
						&status,
						&msg,
						&msg_len,
						headers,
						&num_headers,
						0
					);
					if (rc <= 0) {
						st->net.had_error = true;
						cyw43_arch_lwip_end();
						stream->eof = 1;
						return 0;
					}
					st->response_parsed = true;
				}
				if (parsed < st->net.rx_len) {
					memmove(st->net.rx, st->net.rx + parsed, st->net.rx_len - parsed);
				}
				st->net.rx_len -= parsed;
				st->headers_done = true;
				rx_len = st->net.rx_len;
			}
		}

		if (st->headers_done && rx_len > 0) {
			size_t take = (count < rx_len) ? count : rx_len;
			memcpy(buf, st->net.rx, take);
			if (take < st->net.rx_len) {
				memmove(st->net.rx, st->net.rx + take, st->net.rx_len - take);
			}
			st->net.rx_len -= take;
			cyw43_arch_lwip_end();
			return (ssize_t)take;
		}
		cyw43_arch_lwip_end();

		if (had_error || (done && st->headers_done)) {
			stream->eof = 1;
			return 0;
		}
		if (done && !st->headers_done) {
			stream->eof = 1;
			return 0;
		}
		if (time_reached(deadline)) {
			stream->eof = 1;
			return 0;
		}
		sleep_ms(1);
	}
}

static int rp2350_http_stream_close(php_stream *stream, int close_handle)
{
	rp2350_http_stream_data_t *st = (rp2350_http_stream_data_t *)stream->abstract;
	(void)close_handle;
	if (!st) {
		return 0;
	}
	rp2350_h2_altcp_close(&st->net);
	if (st->net.rx) {
		free(st->net.rx);
		st->net.rx = NULL;
	}
	if (st->h2_session) {
		nghttp2_session_del(st->h2_session);
		st->h2_session = NULL;
	}
	if (st->h2_callbacks) {
		nghttp2_session_callbacks_del(st->h2_callbacks);
		st->h2_callbacks = NULL;
	}
	free(st->h2.tx);
	st->h2.tx = NULL;
	free(st->h2.body);
	st->h2.body = NULL;
	efree(st);
	stream->abstract = NULL;
	return 0;
}

static int rp2350_http_stream_flush(php_stream *stream)
{
	(void)stream;
	return 0;
}

static int rp2350_http_stream_seek(php_stream *stream, zend_off_t offset, int whence, zend_off_t *newoffs)
{
	(void)stream;
	(void)offset;
	(void)whence;
	(void)newoffs;
	return -1;
}

static const php_stream_ops rp2350_http_stream_ops = {
	rp2350_http_stream_write,
	rp2350_http_stream_read,
	rp2350_http_stream_close,
	rp2350_http_stream_flush,
	"rp2350_http_stream",
	rp2350_http_stream_seek,
	NULL,
	NULL,
	NULL
};

static php_stream *rp2350_http_stream_opener(
	php_stream_wrapper *wrapper,
	const char *filename,
	const char *mode,
	int options,
	zend_string **opened_path,
	php_stream_context *context STREAMS_DC
)
{
	char host[96];
	u16_t port;
	const char *path;
	bool is_https = false;
	ip_addr_t remote;
	absolute_time_t deadline;
	rp2350_http_stream_data_t *st = NULL;
	altcp_allocator_t tls_allocator = {0};
	rp2350_h2_tls_alloc_ctx_t tls_ctx = {0};
	php_stream *stream = NULL;
	const char *alpn = NULL;

	(void)context;

	if (!filename || (strncmp(filename, "http://", 7) != 0 && strncmp(filename, "https://", 8) != 0)) {
		return NULL;
	}
	if (!mode || mode[0] != 'r' || mode[1] == '+') {
		if (options & REPORT_ERRORS) {
			php_error_docref(NULL, E_WARNING, "rp2350 http wrapper is read-only");
		}
		return NULL;
	}

	if (!rp2350_parse_http_url(filename, host, sizeof(host), &port, &path, &is_https)) {
		if (options & REPORT_ERRORS) {
			php_error_docref(NULL, E_WARNING, "http url parse failed: %s", filename ? filename : "<null>");
		}
		return NULL;
	}

	if (!rp2350_wifi_init_once()) {
		return NULL;
	}
	if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
		return NULL;
	}
	if (!rp2350_dns_resolve(host, 8000, &remote)) {
		return NULL;
	}

	if (is_https && !rp2350_h2_tls_config_ready()) {
		return NULL;
	}

	st = (rp2350_http_stream_data_t *)ecalloc(1, sizeof(*st));
	if (!st) {
		return NULL;
	}
	st->timeout_ms = 8000;
	st->net.rx_cap = 32768;
	st->net.rx = (char *)malloc(st->net.rx_cap + 1);
	if (!st->net.rx) {
		efree(st);
		return NULL;
	}
	st->net.last_rx_time = get_absolute_time();
	deadline = make_timeout_time_ms(st->timeout_ms);
	st->mode = RP2350_HTTP_STREAM_MODE_H1;

	if (is_https) {
		tls_ctx.config = s_h2_tls_config;
		tls_ctx.hostname = host;
		tls_allocator.alloc = rp2350_h2_tls_alloc_with_sni;
		tls_allocator.arg = &tls_ctx;
	}

	cyw43_arch_lwip_begin();
	st->net.pcb = altcp_new_ip_type(is_https ? &tls_allocator : NULL, IP_GET_TYPE(&remote));
	if (st->net.pcb != NULL) {
		altcp_arg(st->net.pcb, &st->net);
		altcp_recv(st->net.pcb, rp2350_h2_altcp_recv_cb);
		altcp_err(st->net.pcb, rp2350_h2_altcp_err_cb);
		st->net.last_err = altcp_connect(st->net.pcb, &remote, port, rp2350_h2_altcp_connected_cb);
		if (st->net.last_err != ERR_OK) {
			altcp_arg(st->net.pcb, NULL);
			altcp_recv(st->net.pcb, NULL);
			altcp_err(st->net.pcb, NULL);
			altcp_abort(st->net.pcb);
			st->net.pcb = NULL;
		}
	}
	cyw43_arch_lwip_end();
	if (st->net.pcb == NULL) {
		free(st->net.rx);
		efree(st);
		return NULL;
	}

	if (!rp2350_lwip_wait_until(deadline, &st->net.connected) || st->net.had_error) {
		rp2350_h2_altcp_close(&st->net);
		free(st->net.rx);
		efree(st);
		return NULL;
	}

	if (is_https) {
		cyw43_arch_lwip_begin();
		if (st->net.pcb != NULL) {
			void *tls = altcp_tls_context(st->net.pcb);
			if (tls != NULL) {
				alpn = mbedtls_ssl_get_alpn_protocol((mbedtls_ssl_context *)tls);
			}
		}
		cyw43_arch_lwip_end();
		if (alpn != NULL && strcmp(alpn, "h2") == 0) {
			nghttp2_nv nva[5];
			int rc;

			st->mode = RP2350_HTTP_STREAM_MODE_H2;
			st->h2.body_dynamic = true;
			st->h2.body_limit = 131072;

			rc = nghttp2_session_callbacks_new(&st->h2_callbacks);
			if (rc != 0) {
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				efree(st);
				return NULL;
			}
			nghttp2_session_callbacks_set_send_callback(st->h2_callbacks, rp2350_h2_send_cb);
			nghttp2_session_callbacks_set_on_header_callback(st->h2_callbacks, rp2350_h2_on_header_cb);
			nghttp2_session_callbacks_set_on_data_chunk_recv_callback(st->h2_callbacks, rp2350_h2_on_data_chunk_recv_cb);
			nghttp2_session_callbacks_set_on_stream_close_callback(st->h2_callbacks, rp2350_h2_on_stream_close_cb);

			rc = nghttp2_session_client_new(&st->h2_session, st->h2_callbacks, &st->h2);
			if (rc != 0) {
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				nghttp2_session_callbacks_del(st->h2_callbacks);
				efree(st);
				return NULL;
			}

			rc = nghttp2_submit_settings(st->h2_session, NGHTTP2_FLAG_NONE, NULL, 0);
			if (rc != 0) {
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				nghttp2_session_del(st->h2_session);
				nghttp2_session_callbacks_del(st->h2_callbacks);
				efree(st);
				return NULL;
			}

			memset(nva, 0, sizeof(nva));
			nva[0].name = (uint8_t *)":method";
			nva[0].value = (uint8_t *)"GET";
			nva[0].namelen = sizeof(":method") - 1;
			nva[0].valuelen = sizeof("GET") - 1;
			nva[1].name = (uint8_t *)":scheme";
			nva[1].value = (uint8_t *)"https";
			nva[1].namelen = sizeof(":scheme") - 1;
			nva[1].valuelen = sizeof("https") - 1;
			nva[2].name = (uint8_t *)":authority";
			nva[2].value = (uint8_t *)host;
			nva[2].namelen = sizeof(":authority") - 1;
			nva[2].valuelen = strlen(host);
			nva[3].name = (uint8_t *)":path";
			nva[3].value = (uint8_t *)path;
			nva[3].namelen = sizeof(":path") - 1;
			nva[3].valuelen = strlen(path);
			nva[4].name = (uint8_t *)"user-agent";
			nva[4].value = (uint8_t *)"rp2350-php";
			nva[4].namelen = sizeof("user-agent") - 1;
			nva[4].valuelen = sizeof("rp2350-php") - 1;

			st->h2.stream_id = nghttp2_submit_request(st->h2_session, NULL, nva, 5, NULL, NULL);
			if (st->h2.stream_id < 0) {
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				nghttp2_session_del(st->h2_session);
				nghttp2_session_callbacks_del(st->h2_callbacks);
				efree(st);
				return NULL;
			}

			rc = nghttp2_session_send(st->h2_session);
			if (rc != 0 || !rp2350_h2_flush_tx(&st->h2, &st->net, deadline)) {
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				nghttp2_session_del(st->h2_session);
				nghttp2_session_callbacks_del(st->h2_callbacks);
				efree(st);
				return NULL;
			}
		}
	}

	if (st->mode == RP2350_HTTP_STREAM_MODE_H1) {
		char *request = NULL;
		size_t request_len = 0;
		size_t off = 0;

		if (!rp2350_http_build_get_request(host, path, &request, &request_len)) {
			rp2350_h2_altcp_close(&st->net);
			free(st->net.rx);
			efree(st);
			return NULL;
		}

		while (off < request_len) {
			bool wrote = false;
			cyw43_arch_lwip_begin();
			if (st->net.pcb != NULL) {
				u16_t snd = altcp_sndbuf(st->net.pcb);
				if (snd > 0) {
					u16_t chunk = (u16_t)((request_len - off) < snd ? (request_len - off) : snd);
					err_t w = altcp_write(st->net.pcb, request + off, chunk, TCP_WRITE_FLAG_COPY);
					if (w == ERR_OK) {
						off += chunk;
						(void)altcp_output(st->net.pcb);
						wrote = true;
					} else if (w != ERR_MEM) {
						st->net.had_error = true;
						st->net.last_err = w;
					}
				}
			}
			cyw43_arch_lwip_end();
			if (st->net.had_error || st->net.pcb == NULL) {
				rp2350_h2_altcp_close(&st->net);
				free(st->net.rx);
				free(request);
				efree(st);
				return NULL;
			}
			if (!wrote) {
				if (time_reached(deadline)) {
					rp2350_h2_altcp_close(&st->net);
					free(st->net.rx);
					free(request);
					efree(st);
					return NULL;
				}
				sleep_ms(1);
			}
		}
		free(request);

		cyw43_arch_lwip_begin();
		if (st->net.pcb != NULL) {
			(void)altcp_shutdown(st->net.pcb, 0, 1);
		}
		cyw43_arch_lwip_end();
	}

	stream = php_stream_alloc(&rp2350_http_stream_ops, st, 0, mode);
	if (!stream) {
		rp2350_h2_altcp_close(&st->net);
		free(st->net.rx);
		efree(st);
		return NULL;
	}
	stream->wrapper = wrapper;

	if (opened_path) {
		*opened_path = zend_string_init(filename, strlen(filename), 0);
	}
	return stream;
}

static const php_stream_wrapper_ops rp2350_http_wrapper_ops = {
	rp2350_http_stream_opener,
	NULL,
	NULL,
	NULL,
	NULL,
	"rp2350_http_wrapper",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

static php_stream_wrapper rp2350_http_wrapper = {
	&rp2350_http_wrapper_ops,
	NULL,
	1
};

static void rp2350_register_https_wrapper_runtime(void)
{
	zend_string *protocol = zend_string_init("https", sizeof("https") - 1, 0);
	if (!protocol) {
		php_error_docref(NULL, E_WARNING, "failed to allocate https protocol string");
		return;
	}
	(void)php_unregister_url_stream_wrapper_volatile(protocol);
	if (php_register_url_stream_wrapper_volatile(protocol, &rp2350_http_wrapper) != SUCCESS) {
		php_error_docref(NULL, E_WARNING, "failed to register https stream wrapper");
	}
	zend_string_release(protocol);
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
	sem_release(&s_button_sem);

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

	/* Start from a deterministic state; first IRQ updates the live mask. */
	s_button_state_mask = 0;
	s_button_irq_pending_mask = 0;
	s_button_irq_generation = 0;
	sem_init(&s_button_sem, 0, 255);

	for (i = 0; i < sizeof(gpios) / sizeof(gpios[0]); i++) {
		uint32_t gpio = gpios[i];
		gpio_init(gpio);
		gpio_set_dir(gpio, GPIO_IN);
		gpio_pull_up(gpio);
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
	bool ok;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(timeout_ms)
	ZEND_PARSE_PARAMETERS_END();

	if (timeout_ms < 0) {
		timeout_ms = 0;
	}

	rp2350_buttons_init();

	if (timeout_ms == 0) {
		RETURN_FALSE;
	}

	ok = sem_acquire_timeout_ms(&s_button_sem, (uint32_t)timeout_ms);
	if (!ok) {
		RETURN_FALSE;
	}

	{
		uint32_t irq_state = save_and_disable_interrupts();
		uint32_t mask = s_button_state_mask;
		restore_interrupts(irq_state);
		RETURN_LONG((zend_long)mask);
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
	REGISTER_LONG_CONSTANT("MCU_WIFI_LINK_DOWN", CYW43_LINK_DOWN, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_WIFI_LINK_JOIN", CYW43_LINK_JOIN, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_WIFI_LINK_NOIP", CYW43_LINK_NOIP, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_WIFI_LINK_UP", CYW43_LINK_UP, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_WIFI_LINK_FAIL", CYW43_LINK_FAIL, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_WIFI_LINK_NONET", CYW43_LINK_NONET, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MCU_WIFI_LINK_BADAUTH", CYW43_LINK_BADAUTH, CONST_CS | CONST_PERSISTENT);

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

	{
		uint slice = pwm_gpio_to_slice_num(gpio);
		uint chan = pwm_gpio_to_channel(gpio);
		pwm_set_chan_level(slice, chan, on ? 65535u : 0u);
	}
	RETURN_TRUE;
}

ZEND_FUNCTION(mcu_led_level)
{
	zend_long index = 0;
	zend_long level = 0;
	uint32_t gpio;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_LONG(index)
		Z_PARAM_LONG(level)
	ZEND_PARSE_PARAMETERS_END();

	if (level < 0) {
		level = 0;
	} else if (level > 65535) {
		level = 65535;
	}

	gpio = rp2350_led_gpio_for_index(index);
	if (gpio == UINT32_MAX) {
		zend_argument_value_error(1, "must be between 0 and 3");
		RETURN_THROWS();
	}

	rp2350_leds_init();

	{
		uint slice = pwm_gpio_to_slice_num(gpio);
		uint chan = pwm_gpio_to_channel(gpio);
		pwm_set_chan_level(slice, chan, (uint16_t)level);
	}
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

ZEND_FUNCTION(mcu_usb_connected)
{
	ZEND_PARSE_PARAMETERS_NONE();
	rp2350_power_sense_init();
	RETURN_BOOL(gpio_get(BW_VBUS_DETECT) != 0);
}

ZEND_FUNCTION(mcu_battery_voltage)
{
	uint16_t vbat_raw;
	uint16_t vref_raw;
	double voltage;

	ZEND_PARSE_PARAMETERS_NONE();
	rp2350_power_sense_init();

	/* ADC input 0 = GPIO26 (VBAT_SENSE), input 2 = GPIO28 (SENSE_1V1). */
	vbat_raw = rp2350_adc_read_avg(0u, 10u);
	vref_raw = rp2350_adc_read_avg(2u, 10u);

	/* Prefer calibrated path (matches stock), fallback to 3.3V ADC scale if VREF sense is invalid. */
	if (vref_raw >= 16u) {
		voltage = ((double)vbat_raw / (double)vref_raw) * 2.0 * 1.1;
	} else {
		voltage = ((double)vbat_raw / 4095.0) * 3.3 * 2.0;
	}

	if (voltage < 0.0) {
		voltage = 0.0;
	} else if (voltage > 6.0) {
		voltage = 6.0;
	}
	RETURN_DOUBLE(voltage);
}

ZEND_FUNCTION(mcu_battery_mv)
{
	uint16_t vbat_raw;
	uint16_t vref_raw;
	uint32_t mv;

	ZEND_PARSE_PARAMETERS_NONE();
	rp2350_power_sense_init();

	vbat_raw = rp2350_adc_read_avg(0u, 10u);
	vref_raw = rp2350_adc_read_avg(2u, 10u);
	if (vref_raw == 0u) {
		RETURN_LONG(0);
	}

	mv = ((uint32_t)vbat_raw * 2200u + ((uint32_t)vref_raw / 2u)) / (uint32_t)vref_raw;
	if (mv > 6000u) {
		mv = 6000u;
	}
	RETURN_LONG((zend_long)mv);
}

ZEND_FUNCTION(mcu_battery_raw_vbat)
{
	ZEND_PARSE_PARAMETERS_NONE();
	rp2350_power_sense_init();
	RETURN_LONG((zend_long)rp2350_adc_read_avg(0u, 10u));
}

ZEND_FUNCTION(mcu_battery_raw_vref)
{
	ZEND_PARSE_PARAMETERS_NONE();
	rp2350_power_sense_init();
	RETURN_LONG((zend_long)rp2350_adc_read_avg(2u, 10u));
}

ZEND_FUNCTION(mcu_wifi_init)
{
	ZEND_PARSE_PARAMETERS_NONE();
	RETURN_BOOL(rp2350_wifi_init_once());
}

ZEND_FUNCTION(mcu_wifi_connect)
{
	char *ssid = NULL;
	size_t ssid_len = 0;
	char *password = NULL;
	size_t password_len = 0;
	zend_long timeout_ms = 15000;
	uint32_t auth = CYW43_AUTH_OPEN;
	int rc;

	ZEND_PARSE_PARAMETERS_START(1, 3)
		Z_PARAM_STRING(ssid, ssid_len)
		Z_PARAM_OPTIONAL
		Z_PARAM_STRING_OR_NULL(password, password_len)
		Z_PARAM_LONG(timeout_ms)
	ZEND_PARSE_PARAMETERS_END();

	if (ssid_len == 0) {
		zend_argument_value_error(1, "must not be empty");
		RETURN_THROWS();
	}
	if (timeout_ms < 0) {
		timeout_ms = 0;
	}
	if (!rp2350_wifi_init_once()) {
		RETURN_FALSE;
	}

	if (password != NULL && password_len > 0) {
		auth = CYW43_AUTH_WPA2_AES_PSK;
	}

	rc = cyw43_arch_wifi_connect_timeout_ms(
		ssid,
		password,
		auth,
		(uint32_t)timeout_ms
	);
	RETURN_BOOL(rc == 0);
}

ZEND_FUNCTION(mcu_wifi_disconnect)
{
	int rc;

	ZEND_PARSE_PARAMETERS_NONE();
	if (!s_wifi_init) {
		RETURN_TRUE;
	}
	rc = cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
	RETURN_BOOL(rc == 0);
}

ZEND_FUNCTION(mcu_wifi_status)
{
	int status;

	ZEND_PARSE_PARAMETERS_NONE();
	if (!s_wifi_init) {
		RETURN_LONG(CYW43_LINK_DOWN);
	}
	status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
	RETURN_LONG(status);
}

ZEND_FUNCTION(mcu_wifi_ip)
{
	const ip4_addr_t *addr;
	char text[IP4ADDR_STRLEN_MAX];

	ZEND_PARSE_PARAMETERS_NONE();
	if (!s_wifi_init || netif_default == NULL) {
		RETURN_FALSE;
	}
	addr = netif_ip4_addr(netif_default);
	if (addr == NULL || ip4_addr_isany_val(*addr)) {
		RETURN_FALSE;
	}
	if (ip4addr_ntoa_r(addr, text, sizeof(text)) == NULL) {
		RETURN_FALSE;
	}
	RETURN_STRING(text);
}

ZEND_FUNCTION(mcu_tcp_request)
{
	char *host = NULL;
	size_t host_len = 0;
	zend_long port = 0;
	char *payload = NULL;
	size_t payload_len = 0;
	zend_long timeout_ms = 5000;
	zend_long max_read = 4096;
	char *resp = NULL;
	size_t resp_len = 0;

	ZEND_PARSE_PARAMETERS_START(3, 5)
		Z_PARAM_STRING(host, host_len)
		Z_PARAM_LONG(port)
		Z_PARAM_STRING(payload, payload_len)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(timeout_ms)
		Z_PARAM_LONG(max_read)
	ZEND_PARSE_PARAMETERS_END();

	if (host_len == 0) {
		zend_argument_value_error(1, "must not be empty");
		RETURN_THROWS();
	}
	if (port <= 0 || port > 65535) {
		zend_argument_value_error(2, "must be between 1 and 65535");
		RETURN_THROWS();
	}
	if (timeout_ms < 100) {
		timeout_ms = 100;
	}
	if (max_read < 1) {
		max_read = 1;
	} else if (max_read > 262144) {
		max_read = 262144;
	}

	if (!rp2350_net_tcp_request(
		host,
		(u16_t)port,
		payload,
		payload_len,
		(uint32_t)timeout_ms,
		(size_t)max_read,
		&resp,
		&resp_len
	)) {
		RETURN_FALSE;
	}
	RETVAL_STRINGL(resp, resp_len);
	free(resp);
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

static char *rp2350_sapi_getenv(const char *name, size_t name_len)
{
	if (!name) {
		return NULL;
	}
	if (name_len == (sizeof("WIFI_SSID") - 1) && memcmp(name, "WIFI_SSID", sizeof("WIFI_SSID") - 1) == 0) {
		if (s_env_wifi_ssid[0] == '\0') {
			return NULL;
		}
		return strdup(s_env_wifi_ssid);
	}
	if (name_len == (sizeof("WIFI_PASS") - 1) && memcmp(name, "WIFI_PASS", sizeof("WIFI_PASS") - 1) == 0) {
		if (s_env_wifi_pass[0] == '\0') {
			return NULL;
		}
		return strdup(s_env_wifi_pass);
	}
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

#define RP2350_INI_DEFAULT(name, value) \
	ZVAL_NEW_STR(&tmp, zend_string_init(value, sizeof(value) - 1, 1)); \
	zend_hash_str_update(configuration_hash, name, sizeof(name) - 1, &tmp)

static void rp2350_sapi_ini_defaults(HashTable *configuration_hash)
{
	zval tmp;

	RP2350_INI_DEFAULT("html_errors", "0");
	RP2350_INI_DEFAULT("display_errors", "1");
	RP2350_INI_DEFAULT("display_startup_errors", "1");
	RP2350_INI_DEFAULT("log_errors", "0");
}

static void rp2350_force_plain_errors_runtime(void)
{
	zend_string *name;

	name = zend_string_init("html_errors", sizeof("html_errors") - 1, 0);
	(void)zend_alter_ini_entry_chars(name, "0", 1, PHP_INI_USER, PHP_INI_STAGE_RUNTIME);
	zend_string_release(name);

	name = zend_string_init("display_errors", sizeof("display_errors") - 1, 0);
	(void)zend_alter_ini_entry_chars(name, "1", 1, PHP_INI_USER, PHP_INI_STAGE_RUNTIME);
	zend_string_release(name);

	name = zend_string_init("display_startup_errors", sizeof("display_startup_errors") - 1, 0);
	(void)zend_alter_ini_entry_chars(name, "1", 1, PHP_INI_USER, PHP_INI_STAGE_RUNTIME);
	zend_string_release(name);

	name = zend_string_init("log_errors", sizeof("log_errors") - 1, 0);
	(void)zend_alter_ini_entry_chars(name, "0", 1, PHP_INI_USER, PHP_INI_STAGE_RUNTIME);
	zend_string_release(name);

	/* Force core globals too: some startup paths format before INI settles. */
	PG(html_errors) = 0;
	PG(display_errors) = PHP_DISPLAY_ERRORS_STDOUT;
	PG(display_startup_errors) = 1;
	PG(log_errors) = 0;
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
	rp2350_sapi_getenv,
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
	rp2350_sapi_ini_defaults, /* ini_defaults */
	1, /* phpinfo_as_text */
	rp2350_ini_entries, /* ini_entries */
	NULL, /* additional_functions */
	NULL, /* input_filter_init */
	NULL  /* pre_request_init */
};

static zend_result rp2350_zend_stream_open(zend_file_handle *handle)
{
	char path[192];
	rp2350_vfs_stream_t *stream;
	const rp2350_vfs_file_t *file;
	char *http_body = NULL;
	size_t http_body_len = 0;
	const char *name = NULL;

	rp2350_stream_open_seen = true;
	if (!handle || !handle->filename) {
		rp2350_stream_open_last_reason = "invalid-handle";
		return FAILURE;
	}
	name = ZSTR_VAL(handle->filename);
	snprintf(rp2350_stream_open_last_in, sizeof(rp2350_stream_open_last_in), "%s", name);

	if (strncmp(name, "http://", 7) == 0) {
		snprintf(rp2350_stream_open_last_path, sizeof(rp2350_stream_open_last_path), "%s", name);
		if (!rp2350_http_get_body(name, 8000, 131072, &http_body, &http_body_len)) {
			rp2350_stream_open_last_reason = "http-fail";
			return FAILURE;
		}
		stream = (rp2350_vfs_stream_t *)malloc(sizeof(*stream));
		if (!stream) {
			free(http_body);
			rp2350_stream_open_last_reason = "malloc-fail";
			return FAILURE;
		}
		stream->src = http_body;
		stream->len = http_body_len;
		stream->pos = 0;
		stream->owned = true;

		rp2350_stream_open_last_reason = "http-hit";
		handle->type = ZEND_HANDLE_STREAM;
		handle->handle.stream.handle = stream;
		handle->handle.stream.isatty = 0;
		handle->handle.stream.reader = rp2350_vfs_reader;
		handle->handle.stream.fsizer = rp2350_vfs_fsizer;
		handle->handle.stream.closer = rp2350_vfs_closer;
		handle->opened_path = zend_string_init(name, strlen(name), 0);
		return SUCCESS;
	}

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
	stream->owned = false;

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

static zend_string *rp2350_zend_resolve_path(zend_string *filename)
{
	char path[192];
	const rp2350_vfs_file_t *file;

	if (!filename) {
		return NULL;
	}
	if (strncmp(ZSTR_VAL(filename), "http://", 7) == 0) {
		return zend_string_copy(filename);
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
	if (!s_nghttp2_smoke_logged) {
		const nghttp2_info *info = nghttp2_version(0);
		printf("[boot] nghttp2 %s\n", info ? info->version_str : "<null>");
		s_nghttp2_smoke_logged = true;
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
	rp2350_register_https_wrapper_runtime();
	rp2350_force_plain_errors_runtime();
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
