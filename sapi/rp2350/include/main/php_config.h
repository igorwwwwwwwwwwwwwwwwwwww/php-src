#ifndef RP2350_PHP_CONFIG_WRAPPER_H
#define RP2350_PHP_CONFIG_WRAPPER_H

#include "../../../../main/php_config.h"

/*
 * RP2350 firmware build uses newlib/pico-sdk, not the host libc.
 * Override host-detected values that are known to be platform-specific.
 */
#undef HAVE_MACH_ABSOLUTE_TIME
#undef HAVE_MACH_MACH_TIME_H
#undef HAVE_KQUEUE
#undef HAVE_STRUCT_STAT_ST_BIRTHTIMESPEC
#undef HAVE_STRUCT_STAT_ST_BIRTHTIME
#undef HAVE_STRUCT_TM_TM_ZONE
#undef HAVE_STRLCPY
#undef HAVE_STRLCAT
#undef HAVE_FLOCK
#undef HAVE_GETRUSAGE
#undef HAVE_UNIX_H
#undef HAVE_DLFCN_H
#undef HAVE_LIBDL
#undef HAVE_SYSLOG_H
#undef HAVE_CLOCK_GETTIME_NSEC_NP
#undef PHP_HAVE_BUILTIN_CPU_SUPPORTS
#undef PHP_HAVE_BUILTIN_CPU_INIT
#undef HAVE_MMAP
#undef HAVE_SYS_MMAN_H
#undef ZEND_SIGNALS
#undef HAVE_SETITIMER
#undef HAVE_SIGACTION
#undef HAVE_ASPRINTF
#undef HAVE_VASPRINTF

#include <setjmp.h>
#ifndef sigjmp_buf
# define sigjmp_buf jmp_buf
#endif
#ifndef sigsetjmp
# define sigsetjmp(env, savemask) setjmp(env)
#endif
#ifndef siglongjmp
# define siglongjmp(env, val) longjmp(env, val)
#endif

#endif /* RP2350_PHP_CONFIG_WRAPPER_H */
