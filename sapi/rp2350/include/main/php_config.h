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
#undef HAVE_STRUCT_TM_TM_GMTOFF
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
#undef HAVE_STRPTIME
#undef HAVE_ARPA_NAMESER_H
#undef HAVE_RESOLV_H
#undef HAVE_DNS_H
#undef HAVE_DNS_SEARCH
#undef HAVE_DNS_SEARCH_FUNC
#undef HAVE_SYS_STATVFS_H
#undef HAVE_STATVFS
#undef HAVE_SYS_MOUNT_H
#undef HAVE_SYS_STATFS_H
#undef HAVE_STATFS
#undef HAVE_SYS_IPC_H
#undef HAVE_FTOK
#undef HAVE_SYS_UTSNAME_H
#undef HAVE_SYSEXITS_H
#undef HAVE_NET_IF_H
#undef HAVE_IFADDRS_H
#undef HAVE_GETIFADDRS
#undef HAVE_COMMONCRYPTO_COMMONRANDOM_H
#undef HAVE_UTIL_H
#undef HAVE_OPENPTY
#undef PHP_CAN_SUPPORT_PROC_OPEN

/*
 * Host-generated php_config.h is for a 64-bit build machine.
 * RP2350 firmware is 32-bit Arm, so these ABI sizes must be overridden.
 * Mismatches here corrupt Zend operand/literal interpretation.
 */
#undef SIZEOF_LONG
#define SIZEOF_LONG 4

#undef SIZEOF_SIZE_T
#define SIZEOF_SIZE_T 4

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
