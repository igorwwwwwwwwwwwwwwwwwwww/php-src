PHP_ARG_ENABLE([rp2350],,
  [AS_HELP_STRING([--enable-rp2350],
    [Enable RP2350 experimental embedded SAPI (legacy autotools path)])],
  [no],
  [no])

AC_MSG_CHECKING([for RP2350 SAPI support])

if test "$PHP_RP2350" != "no"; then
  AC_MSG_RESULT([no])
  AC_MSG_ERROR([Legacy autotools RP2350 SAPI build is removed. Use firmware CMake build under sapi/rp2350 (e.g. build_badger2350).])
else
  AC_MSG_RESULT([no])
fi
