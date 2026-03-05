#ifndef RP2350_MBEDTLS_CONFIG_H
#define RP2350_MBEDTLS_CONFIG_H

#include "mbedtls/mbedtls_config.h"

#ifndef MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_NO_PLATFORM_ENTROPY
#endif

#ifndef MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#endif

#ifndef MBEDTLS_ALLOW_PRIVATE_ACCESS
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#endif

#ifndef MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES
#endif

/*
 * Bare-metal RP2350 has no standard libc wall-clock implementation that
 * mbedTLS can use directly; TLS cert-time validation is out of scope for now.
 */
#ifdef MBEDTLS_HAVE_TIME
#undef MBEDTLS_HAVE_TIME
#endif
#ifdef MBEDTLS_HAVE_TIME_DATE
#undef MBEDTLS_HAVE_TIME_DATE
#endif
#ifdef MBEDTLS_TIMING_C
#undef MBEDTLS_TIMING_C
#endif
#ifdef MBEDTLS_NET_C
#undef MBEDTLS_NET_C
#endif
#ifdef MBEDTLS_SHA3_C
#undef MBEDTLS_SHA3_C
#endif
#ifdef MBEDTLS_SSL_PROTO_TLS1_3
#undef MBEDTLS_SSL_PROTO_TLS1_3
#endif

#endif
