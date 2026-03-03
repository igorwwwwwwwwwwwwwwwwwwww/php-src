PHP_ARG_ENABLE([rp2350],,
  [AS_HELP_STRING([--enable-rp2350],
    [Enable RP2350 experimental embedded SAPI])],
  [no],
  [no])

AC_MSG_CHECKING([for RP2350 SAPI support])

if test "$PHP_RP2350" != "no"; then
  AC_MSG_RESULT([yes])
  PHP_ADD_MAKEFILE_FRAGMENT([$abs_srcdir/sapi/rp2350/Makefile.frag])

  AS_CASE([$host_alias],
    [*cygwin*], [SAPI_RP2350_PATH=sapi/rp2350/php-rp2350.exe],
    [SAPI_RP2350_PATH=sapi/rp2350/php-rp2350])

  PHP_SELECT_SAPI([rp2350],
    [program],
    [php_rp2350.c],
    [-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1])

  AS_CASE([$host_alias],
    [*darwin*], [
      BUILD_RP2350="\$(CC) \$(CFLAGS_CLEAN) \$(EXTRA_CFLAGS) \$(EXTRA_LDFLAGS_PROGRAM) \$(LDFLAGS) \$(NATIVE_RPATHS) \$(PHP_GLOBAL_OBJS:.lo=.o) \$(PHP_BINARY_OBJS:.lo=.o) \$(PHP_RP2350_OBJS:.lo=.o) \$(PHP_FRAMEWORKS) \$(EXTRA_LIBS) \$(ZEND_EXTRA_LIBS) -o \$(SAPI_RP2350_PATH)"
    ], [
      BUILD_RP2350="\$(LIBTOOL) --tag=CC --mode=link \$(CC) -export-dynamic \$(CFLAGS_CLEAN) \$(EXTRA_CFLAGS) \$(EXTRA_LDFLAGS_PROGRAM) \$(LDFLAGS) \$(PHP_RPATHS) \$(PHP_GLOBAL_OBJS:.lo=.o) \$(PHP_BINARY_OBJS:.lo=.o) \$(PHP_RP2350_OBJS:.lo=.o) \$(EXTRA_LIBS) \$(ZEND_EXTRA_LIBS) -o \$(SAPI_RP2350_PATH)"
    ])

  PHP_SUBST([SAPI_RP2350_PATH])
  PHP_SUBST([BUILD_RP2350])
else
  AC_MSG_RESULT([no])
fi
