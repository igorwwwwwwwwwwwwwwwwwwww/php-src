/*
   Experimental RP2350 SAPI bootstrap.
*/

#include "php.h"
#include "SAPI.h"
#include "main/php_main.h"
#include "main/php_variables.h"
#include "Zend/zend_exceptions.h"

#include "php_rp2350.h"
#include "rp2350_transport.h"

#if defined(PHP_WIN32) && defined(ZTS)
ZEND_TSRMLS_CACHE_DEFINE()
#endif

static const char RP2350_HARDCODED_INI[] =
	"html_errors=0\n"
	"implicit_flush=1\n"
	"output_buffering=0\n"
	"display_errors=1\n"
	"max_execution_time=0\n"
	"max_input_time=-1\n\0";

/*
 * Platform I/O shims.
 * Target firmware can provide strong definitions of these symbols.
 */
#if defined(__GNUC__) || defined(__clang__)
# define RP2350_WEAK __attribute__((weak))
#else
# define RP2350_WEAK
#endif

RP2350_WEAK size_t rp2350_platform_write(const char *buf, size_t len)
{
	if (len > 0) {
		return fwrite(buf, 1, len, stdout);
	}
	return 0;
}

RP2350_WEAK void rp2350_platform_flush(void)
{
	fflush(stdout);
}

RP2350_WEAK void rp2350_platform_log(const char *msg)
{
	fprintf(stderr, "[rp2350] %s\n", msg);
}

RP2350_WEAK size_t rp2350_platform_readline(char *buf, size_t max_len)
{
	if (max_len == 0) {
		return 0;
	}
	if (fgets(buf, (int) max_len, stdin) == NULL) {
		return 0;
	}
	return strlen(buf);
}

static int rp2350_startup(sapi_module_struct *sapi_module)
{
	return php_module_startup(sapi_module, NULL);
}

static size_t rp2350_ub_write(const char *str, size_t str_length)
{
	size_t written = 0;
	const char *ptr = str;
	size_t remaining = str_length;

	while (remaining > 0) {
		size_t n = rp2350_platform_write(ptr, remaining);
		if (n == 0) {
			php_handle_aborted_connection();
			break;
		}
		ptr += n;
		remaining -= n;
		written += n;
	}

	return written;
}

static void rp2350_flush(void *server_context)
{
	(void) server_context;
	rp2350_platform_flush();
}

static char *rp2350_read_cookies(void)
{
	return NULL;
}

static void rp2350_log_message(const char *message, int syslog_type_int)
{
	(void) syslog_type_int;
	rp2350_platform_log(message);
}

static void rp2350_register_variables(zval *track_vars_array)
{
	php_import_environment_variables(track_vars_array);
}

static int rp2350_deactivate(void)
{
	rp2350_platform_flush();
	return SUCCESS;
}

sapi_module_struct rp2350_sapi_module = {
	"rp2350",
	"RP2350 Embedded SAPI",

	rp2350_startup,
	php_module_shutdown_wrapper,

	NULL,
	rp2350_deactivate,

	rp2350_ub_write,
	rp2350_flush,
	NULL,
	NULL,

	php_error,

	NULL,
	NULL,
	NULL,

	NULL,
	rp2350_read_cookies,

	rp2350_register_variables,
	rp2350_log_message,
	NULL,
	NULL,

	STANDARD_SAPI_MODULE_PROPERTIES
};

int main(int argc, char **argv)
{
	char line[512];
	const char prompt[] = "php> ";

#ifdef ZTS
	php_tsrm_startup();
# ifdef PHP_WIN32
	ZEND_TSRMLS_CACHE_UPDATE();
# endif
#endif

	sapi_startup(&rp2350_sapi_module);
	rp2350_sapi_module.ini_entries = RP2350_HARDCODED_INI;
	rp2350_sapi_module.executable_location = (argc > 0) ? argv[0] : NULL;

	if (rp2350_sapi_module.startup(&rp2350_sapi_module) == FAILURE) {
		return FAILURE;
	}

	SG(options) |= SAPI_OPTION_NO_CHDIR;
	SG(request_info).argc = argc;
	SG(request_info).argv = argv;

	if (php_request_startup() == FAILURE) {
		php_module_shutdown();
		sapi_shutdown();
		return FAILURE;
	}

	SG(headers_sent) = 1;
	SG(request_info).no_headers = 1;

	for (;;) {
		size_t len;

		(void) rp2350_platform_write(prompt, sizeof(prompt) - 1);
		rp2350_platform_flush();

		len = rp2350_platform_readline(line, sizeof(line));
		if (len == 0) {
			break;
		}
		if ((len == 5 && memcmp(line, "exit\n", 5) == 0) ||
			(len == 5 && memcmp(line, "quit\n", 5) == 0)) {
			break;
		}
		if (len == 1 && line[0] == '\n') {
			continue;
		}

		if (zend_eval_stringl(line, len, NULL, "rp2350_repl") == FAILURE) {
			php_error_docref(NULL, E_WARNING, "Eval failed");
			zend_clear_exception();
		}
	}

	php_request_shutdown(NULL);
	php_module_shutdown();
	sapi_shutdown();

#ifdef ZTS
	tsrm_shutdown();
#endif

	return SUCCESS;
}
