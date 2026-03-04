#ifndef RP2350_PHP_CONFIG_TOP_WRAPPER_H
#define RP2350_PHP_CONFIG_TOP_WRAPPER_H

/* Route <php_config.h> through RP2350-specific overrides first. */
#include "main/php_config.h"

/* newlib struct tm does not expose this field on RP2350. */
#undef HAVE_STRUCT_TM_TM_GMTOFF

#endif
