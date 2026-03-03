#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>

#include "php.h"
#include "main/SAPI.h"
#include "ext/standard/basic_functions.h"
#include "ext/standard/html.h"
#include "ext/standard/info.h"
#include "ext/random/php_random_csprng.h"
#include "ext/random/php_random_zend_utils.h"
#include "ext/date/php_date.h"
#include "main/php_content_types.h"
#include "main/php_ini.h"
#include "main/php_output.h"
#include "main/php_ticks.h"
#include "main/php_globals.h"
#include "main/php_glob.h"
#include "main/php_network.h"
#include "main/php_open_temporary_file.h"
#include "Zend/zend_system_id.h"
#include "rp2350_transport.h"

#undef vsnprintf
#undef snprintf

#ifndef ZTS
php_basic_globals basic_globals;
#else
int basic_globals_id;
#endif

ZEND_API char zend_system_id[32] = "rp2350";

PHPAPI void php_clear_stat_cache(bool clear_realpath_cache, const char *filename, size_t filename_len)
{
	(void)clear_realpath_cache;
	(void)filename;
	(void)filename_len;
}

int php_get_uid_by_name(const char *name, uid_t *uid)
{
	(void)name;
	if (uid) {
		*uid = 0;
	}
	return FAILURE;
}

int php_get_gid_by_name(const char *name, gid_t *gid)
{
	(void)name;
	if (gid) {
		*gid = 0;
	}
	return FAILURE;
}

PHPAPI char *php_socket_strerror(long err, char *buf, size_t bufsize)
{
	const char *msg = strerror((int)err);
	if (!msg) {
		msg = "socket error";
	}
	if (buf && bufsize > 0) {
		snprintf(buf, bufsize, "%s", msg);
		return buf;
	}
	return estrdup(msg);
}

PHPAPI int php_open_temporary_fd_ex(const char *dir, const char *pfx, zend_string **opened_path_p, uint32_t flags)
{
	(void)dir;
	(void)pfx;
	(void)opened_path_p;
	(void)flags;
	errno = ENOSYS;
	return -1;
}

PHPAPI int php_open_temporary_fd(const char *dir, const char *pfx, zend_string **opened_path_p)
{
	return php_open_temporary_fd_ex(dir, pfx, opened_path_p, 0);
}

PHPAPI int php_glob(const char *pattern, int flags, int (*errfunc)(const char *, int), php_glob_t *pglob)
{
	(void)pattern;
	(void)flags;
	(void)errfunc;
	if (pglob) {
		memset(pglob, 0, sizeof(*pglob));
	}
	return PHP_GLOB_NOMATCH;
}

PHPAPI void php_globfree(php_glob_t *pglob)
{
	(void)pglob;
}

PHP_INI_MH(OnChangeBrowscap)
{
	(void)entry;
	(void)new_value;
	(void)mh_arg1;
	(void)mh_arg2;
	(void)mh_arg3;
	(void)stage;
	return SUCCESS;
}

SAPI_POST_HANDLER_FUNC(rfc1867_post_handler)
{
	(void)content_type_dup;
	(void)arg;
}

PHPAPI int php_register_internal_extensions(void)
{
	return SUCCESS;
}

PHPAPI void destroy_uploaded_files_hash(void) {}

static uint32_t rp2350_stub_prng = 0x9e3779b9u;

ZEND_ATTRIBUTE_NONNULL PHPAPI zend_result php_random_bytes_ex(void *bytes, size_t size, char *errstr, size_t errstr_size)
{
	uint8_t *out = (uint8_t *)bytes;
	size_t i;
	(void)errstr;
	(void)errstr_size;
	for (i = 0; i < size; i++) {
		if ((i & 3u) == 0) {
			rp2350_stub_prng = rp2350_stub_prng * 1664525u + 1013904223u;
		}
		out[i] = (uint8_t)((rp2350_stub_prng >> ((i & 3u) * 8u)) & 0xffu);
	}
	return SUCCESS;
}

ZEND_ATTRIBUTE_NONNULL PHPAPI void php_random_bytes_insecure_for_zend(
	zend_random_bytes_insecure_state *state, void *bytes, size_t size)
{
	(void)state;
	(void)php_random_bytes_ex(bytes, size, NULL, 0);
}

ZEND_API zend_result zend_add_system_entropy(const char *module_name, const char *hook_name, const void *data, size_t size)
{
	(void)module_name;
	(void)hook_name;
	(void)data;
	(void)size;
	return SUCCESS;
}

void zend_startup_system_id(void) {}
void zend_finalize_system_id(void) {}

PHPAPI zend_string *php_escape_html_entities_ex(const unsigned char *old, size_t oldlen, int all, int flags, const char *hint_charset, bool double_encode, bool quiet)
{
	(void)all;
	(void)flags;
	(void)hint_charset;
	(void)double_encode;
	(void)quiet;
	return zend_string_init((const char *)old, oldlen, 0);
}

PHPAPI zend_string *php_format_date(const char *format, size_t format_len, time_t ts, bool localtime)
{
	(void)format;
	(void)format_len;
	(void)ts;
	(void)localtime;
	return zend_string_init("1970-01-01 00:00:00 UTC", sizeof("1970-01-01 00:00:00 UTC") - 1, 0);
}

PHPAPI void php_call_shutdown_functions(void) {}
PHPAPI void php_free_shutdown_functions(void) {}

PHPAPI void php_print_info_htmlhead(void) {}
PHPAPI void php_print_info(int flag) { (void)flag; }
PHPAPI void php_print_style(void) {}
PHPAPI void php_info_print_style(void) {}
PHPAPI void php_info_print_table_colspan_header(int num_cols, const char *header) { (void)num_cols; (void)header; }
PHPAPI void php_info_print_table_header(int num_cols, ...) { (void)num_cols; }
PHPAPI void php_info_print_table_row(int num_cols, ...) { (void)num_cols; }
PHPAPI void php_info_print_table_row_ex(int num_cols, const char *value_class, ...) { (void)num_cols; (void)value_class; }
PHPAPI void php_info_print_table_start(void) {}
PHPAPI void php_info_print_table_end(void) {}
PHPAPI void php_info_print_box_start(int bg) { (void)bg; }
PHPAPI void php_info_print_box_end(void) {}
PHPAPI void php_info_print_hr(void) {}
PHPAPI void php_info_print_module(zend_module_entry *module) { (void)module; }
PHPAPI zend_string *php_get_uname(char mode) { (void)mode; return zend_string_init("rp2350", sizeof("rp2350") - 1, 0); }

PHPAPI HashTable *php_stream_xport_get_hash(void)
{
	static HashTable s_xports;
	static bool s_init = false;
	if (!s_init) {
		zend_hash_init(&s_xports, 0, NULL, NULL, 1);
		s_init = true;
	}
	return &s_xports;
}

PHPAPI int php_stream_xport_register(const char *protocol, php_stream_transport_factory factory)
{
	(void)protocol;
	(void)factory;
	return SUCCESS;
}

PHPAPI php_stream *php_stream_generic_socket_factory(const char *proto, size_t protolen,
		const char *resourcename, size_t resourcenamelen,
		const char *persistent_id, int options, int flags,
		struct timeval *timeout,
		php_stream_context *context STREAMS_DC)
{
	(void)proto;
	(void)protolen;
	(void)resourcename;
	(void)resourcenamelen;
	(void)persistent_id;
	(void)options;
	(void)flags;
	(void)timeout;
	(void)context;
	return NULL;
}
