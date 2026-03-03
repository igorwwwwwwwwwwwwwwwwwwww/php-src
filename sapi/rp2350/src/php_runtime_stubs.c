#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>

#include "php.h"
#include "ext/standard/basic_functions.h"
#include "main/SAPI.h"
#include "main/php_globals.h"
#include "main/php_glob.h"
#include "main/php_network.h"
#include "main/php_open_temporary_file.h"

#undef vsnprintf
#undef snprintf

#ifndef ZTS
php_basic_globals basic_globals;
php_core_globals core_globals;
sapi_globals_struct sapi_globals;
#else
int basic_globals_id;
#endif

PHPAPI ZEND_COLD void php_verror(const char *docref, const char *params, int type, const char *format, va_list args)
{
	(void)docref;
	(void)params;
	zend_string *msg = vstrpprintf(0, format, args);
	if (msg) {
		zend_error_zstr(type, msg);
		zend_string_release(msg);
	} else {
		zend_error(type, "%s", "php_verror: format failure");
	}
}

PHPAPI ZEND_COLD void php_error_docref(const char *docref, int type, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	php_verror(docref, "", type, format, args);
	va_end(args);
}

PHPAPI ZEND_COLD void php_error_docref_unchecked(const char *docref, int type, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	php_verror(docref, "", type, format, args);
	va_end(args);
}

PHPAPI ZEND_COLD void php_error_docref1(const char *docref, const char *param1, int type, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	php_verror(docref, param1, type, format, args);
	va_end(args);
}

PHPAPI ZEND_COLD void php_error_docref2(const char *docref, const char *param1, const char *param2, int type, const char *format, ...)
{
	(void)param2;
	va_list args;
	va_start(args, format);
	php_verror(docref, param1, type, format, args);
	va_end(args);
}

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
