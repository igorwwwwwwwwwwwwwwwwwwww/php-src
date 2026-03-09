cmake_minimum_required(VERSION 3.21)

set(PICO_BOARD "pimoroni_badger2350" CACHE STRING "Pico board type")
set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards" CACHE STRING "Custom board header search path")
set(PICO_PLATFORM "rp2350" CACHE STRING "Pico platform")
set(RP2350_ENABLE_EPD ON CACHE BOOL "Enable Badger/EPD support")
set(RP2350_ENABLE_TFT OFF CACHE BOOL "Enable Tufty/TFT support")
set(PICO_DEFAULT_UART "0" CACHE STRING "Default UART instance for stdio")
set(PICO_DEFAULT_UART_TX_PIN "4" CACHE STRING "Default UART TX pin for stdio")
set(PICO_DEFAULT_UART_RX_PIN "5" CACHE STRING "Default UART RX pin for stdio")
if(PICO_BOARD STREQUAL "pimoroni_tufty2350")
  set(PICO_FLASH_SIZE_BYTES 16777216 CACHE STRING "Flash size" FORCE)
endif()
set(RP2350_WAIT_FOR_USB_SERIAL_MS "10000" CACHE STRING "Wait up to this many milliseconds for USB CDC serial connection before boot logs (0 disables)")
set(RP2350_WIFI_SSID "$ENV{WIFI_SSID}" CACHE STRING "Optional Wi-Fi SSID exposed via getenv('WIFI_SSID')")
set(RP2350_WIFI_PASS "$ENV{WIFI_PASS}" CACHE STRING "Optional Wi-Fi password exposed via getenv('WIFI_PASS')")

set(RP2350_DOTENV_PATH "${CMAKE_CURRENT_LIST_DIR}/wifi.env")
if(EXISTS "${RP2350_DOTENV_PATH}")
  file(STRINGS "${RP2350_DOTENV_PATH}" RP2350_DOTENV_LINES)
  foreach(RP2350_DOTENV_LINE IN LISTS RP2350_DOTENV_LINES)
    string(REGEX REPLACE "\r$" "" RP2350_DOTENV_LINE "${RP2350_DOTENV_LINE}")
    if(RP2350_DOTENV_LINE MATCHES "^[ \t]*#" OR RP2350_DOTENV_LINE STREQUAL "")
      continue()
    endif()
    if(RP2350_DOTENV_LINE MATCHES "^[ \t]*WIFI_SSID=(.*)$")
      set(RP2350_WIFI_SSID "${CMAKE_MATCH_1}")
      string(REGEX REPLACE "^\"(.*)\"$" "\\1" RP2350_WIFI_SSID "${RP2350_WIFI_SSID}")
    elseif(RP2350_DOTENV_LINE MATCHES "^[ \t]*WIFI_PASS=(.*)$")
      set(RP2350_WIFI_PASS "${CMAKE_MATCH_1}")
      string(REGEX REPLACE "^\"(.*)\"$" "\\1" RP2350_WIFI_PASS "${RP2350_WIFI_PASS}")
    endif()
  endforeach()
endif()

string(REPLACE "\\" "\\\\" RP2350_WIFI_SSID_ESC "${RP2350_WIFI_SSID}")
string(REPLACE "\"" "\\\"" RP2350_WIFI_SSID_ESC "${RP2350_WIFI_SSID_ESC}")
string(REPLACE "\\" "\\\\" RP2350_WIFI_PASS_ESC "${RP2350_WIFI_PASS}")
string(REPLACE "\"" "\\\"" RP2350_WIFI_PASS_ESC "${RP2350_WIFI_PASS_ESC}")

if(NOT DEFINED PICO_SDK_PATH)
  set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk")
endif()

if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
  message(FATAL_ERROR
    "pico-sdk not found at ${PICO_SDK_PATH}. "
    "Run ./sapi/rp2350/fetch-deps.sh or set -DPICO_SDK_PATH=/path/to/pico-sdk.")
endif()

include("${PICO_SDK_PATH}/pico_sdk_init.cmake")

project(php_mcu_firmware C CXX ASM)

pico_sdk_init()

set(PHP_SRC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(RP2350_VFS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/rp2350_vfs.h")
set(RP2350_VFS_FILES "${CMAKE_CURRENT_LIST_DIR}/main.php")
file(GLOB RP2350_VFS_EXTRA_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/fs/*")
list(APPEND RP2350_VFS_FILES ${RP2350_VFS_EXTRA_FILES})
list(SORT RP2350_VFS_FILES)
list(REMOVE_DUPLICATES RP2350_VFS_FILES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RP2350_VFS_FILES})

set(RP2350_VFS_TEXT
"#ifndef RP2350_VFS_H\n#define RP2350_VFS_H\n\n#include <stddef.h>\n\ntypedef struct {\n  const char *path;\n  const char *source;\n  size_t len;\n} rp2350_vfs_file_t;\n\n")

set(RP2350_VFS_INDEX 0)
set(RP2350_VFS_TABLE_ENTRIES "")
foreach(RP2350_VFS_FILE ${RP2350_VFS_FILES})
  if(IS_DIRECTORY "${RP2350_VFS_FILE}")
    continue()
  endif()
  if(RP2350_VFS_FILE STREQUAL "${CMAKE_CURRENT_LIST_DIR}/main.php")
    set(RP2350_VFS_PATH "/main.php")
  else()
    get_filename_component(RP2350_VFS_NAME "${RP2350_VFS_FILE}" NAME)
    set(RP2350_VFS_PATH "/${RP2350_VFS_NAME}")
  endif()
  file(READ "${RP2350_VFS_FILE}" RP2350_VFS_HEX HEX)
  file(SIZE "${RP2350_VFS_FILE}" RP2350_VFS_SIZE)
  string(LENGTH "${RP2350_VFS_HEX}" RP2350_VFS_HEX_LEN)
  if(RP2350_VFS_HEX_LEN EQUAL 0)
    set(RP2350_VFS_BYTES "")
  else()
    string(REGEX REPLACE "(..)" "0x\\1, " RP2350_VFS_BYTES "${RP2350_VFS_HEX}")
  endif()
  string(APPEND RP2350_VFS_TEXT
    "static const unsigned char rp2350_vfs_src_${RP2350_VFS_INDEX}[] = { ${RP2350_VFS_BYTES} };\n\n")
  string(APPEND RP2350_VFS_TABLE_ENTRIES
    "  { \"${RP2350_VFS_PATH}\", (const char *)rp2350_vfs_src_${RP2350_VFS_INDEX}, ${RP2350_VFS_SIZE} },\n")
  math(EXPR RP2350_VFS_INDEX "${RP2350_VFS_INDEX} + 1")
endforeach()

string(APPEND RP2350_VFS_TEXT
  "static const rp2350_vfs_file_t rp2350_vfs_files[] = {\n${RP2350_VFS_TABLE_ENTRIES}};\n\n"
  "static const size_t rp2350_vfs_files_count = sizeof(rp2350_vfs_files) / sizeof(rp2350_vfs_files[0]);\n\n"
  "#endif /* RP2350_VFS_H */\n")
file(WRITE "${RP2350_VFS_HEADER}" "${RP2350_VFS_TEXT}")

file(GLOB ZEND_ENGINE_SOURCES "${PHP_SRC_ROOT}/Zend/*.c")
list(REMOVE_ITEM ZEND_ENGINE_SOURCES
  "${PHP_SRC_ROOT}/Zend/zend_dtrace.c"
  "${PHP_SRC_ROOT}/Zend/zend_fibers.c"
  "${PHP_SRC_ROOT}/Zend/zend_system_id.c"
)

file(GLOB RP2350_STANDARD_SOURCES "${PHP_SRC_ROOT}/ext/standard/*.c")
list(REMOVE_ITEM RP2350_STANDARD_SOURCES
  "${PHP_SRC_ROOT}/ext/standard/dns_win32.c"
  "${PHP_SRC_ROOT}/ext/standard/dns.c"
)
file(GLOB RP2350_RANDOM_SOURCES "${PHP_SRC_ROOT}/ext/random/*.c")
file(GLOB RP2350_HASH_SOURCES "${PHP_SRC_ROOT}/ext/hash/*.c")
list(REMOVE_ITEM RP2350_HASH_SOURCES
  "${PHP_SRC_ROOT}/ext/hash/hash_sha_ni.c"
  "${PHP_SRC_ROOT}/ext/hash/hash_sha_sse2.c"
)
set(RP2350_HASH_EXTRA_SOURCES
  ${PHP_SRC_ROOT}/ext/hash/murmur/PMurHash.c
  ${PHP_SRC_ROOT}/ext/hash/murmur/PMurHash128.c
)
set_source_files_properties(${PHP_SRC_ROOT}/ext/hash/hash_sha3.c PROPERTIES
  COMPILE_DEFINITIONS "HAVE_SLOW_HASH3=1"
  COMPILE_OPTIONS "-Wno-error=incompatible-pointer-types"
)
set(RP2350_JSON_SOURCES
  ${PHP_SRC_ROOT}/ext/json/json.c
  ${PHP_SRC_ROOT}/ext/json/json_encoder.c
  ${PHP_SRC_ROOT}/ext/json/json_parser.tab.c
  ${PHP_SRC_ROOT}/ext/json/json_scanner.c
)
file(GLOB RP2350_URI_SOURCES "${PHP_SRC_ROOT}/ext/uri/*.c")
file(GLOB RP2350_URIPARSER_SOURCES "${PHP_SRC_ROOT}/ext/uri/uriparser/src/*.c")
set(RP2350_DATE_SOURCES
  ${PHP_SRC_ROOT}/ext/date/php_date.c
  ${PHP_SRC_ROOT}/ext/date/lib/astro.c
  ${PHP_SRC_ROOT}/ext/date/lib/dow.c
  ${PHP_SRC_ROOT}/ext/date/lib/parse_date.c
  ${PHP_SRC_ROOT}/ext/date/lib/parse_tz.c
  ${PHP_SRC_ROOT}/ext/date/lib/parse_posix.c
  ${PHP_SRC_ROOT}/ext/date/lib/timelib.c
  ${PHP_SRC_ROOT}/ext/date/lib/tm2unixtime.c
  ${PHP_SRC_ROOT}/ext/date/lib/unixtime2tm.c
  ${PHP_SRC_ROOT}/ext/date/lib/parse_iso_intervals.c
  ${PHP_SRC_ROOT}/ext/date/lib/interval.c
)
set(RP2350_PCRE_SOURCES
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_auto_possess.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_chartables.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_chkdint.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_compile.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_config.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_context.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_convert.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_dfa_match.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_error.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_extuni.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_find_bracket.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_maketables.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_match_data.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_match.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_newline.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_ord2utf.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_pattern_info.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_script_run.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_serialize.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_string_utils.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_study.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_substitute.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_substring.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_tables.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_ucd.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_valid_utf.c
  ${PHP_SRC_ROOT}/ext/pcre/pcre2lib/pcre2_xclass.c
  ${PHP_SRC_ROOT}/ext/pcre/php_pcre.c
)
set_source_files_properties(${RP2350_PCRE_SOURCES} PROPERTIES
  COMPILE_DEFINITIONS "HAVE_CONFIG_H;HAVE_BUNDLED_PCRE=1;PCRE2_STATIC=1;PCRE2_CODE_UNIT_WIDTH=8"
  COMPILE_OPTIONS "-Wno-error=incompatible-pointer-types"
)
set(RP2350_LEXBOR_SOURCES
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/array_obj.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/array.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/avl.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/bst.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/conv.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/diyfp.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/dobject.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/dtoa.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/hash.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/mem.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/mraw.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/plog.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/print.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/serialize.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/shs.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/str.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/strtod.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/core/utils.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/encoding/decode.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/encoding/encode.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/encoding/encoding.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/encoding/multi.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/encoding/range.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/encoding/res.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/encoding/single.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/ports/posix/lexbor/core/memory.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/punycode/punycode.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/unicode/idna.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/unicode/unicode.c
  ${PHP_SRC_ROOT}/ext/lexbor/lexbor/url/url.c
)
set(RP2350_NGHTTP2_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_pq.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_map.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_queue.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_frame.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_buf.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_stream.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_outbound_item.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_session.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_submit.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_helper.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_alpn.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_hd.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_hd_huffman.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_hd_huffman_data.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_version.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_priority_spec.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_option.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_callbacks.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_mem.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_http.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_rcbuf.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_extpri.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_ratelim.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_time.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/nghttp2_debug.c
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/sfparse.c
)
set(RP2350_PICOHTTPPARSER_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/third_party/picohttpparser/picohttpparser.c
  ${PICO_SDK_PATH}/lib/lwip/src/apps/sntp/sntp.c
)

set(RP2350_DISPLAY_SOURCES)
if(RP2350_ENABLE_EPD)
  list(APPEND RP2350_DISPLAY_SOURCES
    rp2350_epd.cpp
    third_party/ssd1680/ssd1680.cpp
  )
endif()
if(RP2350_ENABLE_TFT)
  list(APPEND RP2350_DISPLAY_SOURCES
    rp2350_tft.cpp
  )
endif()

add_executable(php_mcu_firmware
  src/main.c
  src/fault_handlers.c
  src/rp2350_dns_stubs.c
  src/rp2350_http_stream.c
  src/rp2350_http_h1.c
  src/rp2350_http_h2.c
  src/rp2350_wifi.c
  src/rp2350_vfs_syscalls.c
  src/rp2350_ca_bundle.c
  rp2350_rtc.c
  ${RP2350_DISPLAY_SOURCES}
  src/php_runtime_stubs.c
  rp2350_eval_zend.c
  rp2350_psram.c
  rp2350_zend_fibers_stub.c
  rp2350_zend_optimizer_stub.c
  rp2350_posix_stubs.c
  rp2350_mmap.c
  rp2350_sbrk_psram.c
  rp2350_transport_pico.c
  ${PHP_SRC_ROOT}/main/streams/cast.c
  ${PHP_SRC_ROOT}/main/streams/filter.c
  ${PHP_SRC_ROOT}/main/streams/glob_wrapper.c
  ${PHP_SRC_ROOT}/main/streams/memory.c
  ${PHP_SRC_ROOT}/main/streams/mmap.c
  ${PHP_SRC_ROOT}/main/streams/plain_wrapper.c
  ${PHP_SRC_ROOT}/main/streams/streams.c
  ${PHP_SRC_ROOT}/main/streams/userspace.c
  ${PHP_SRC_ROOT}/main/SAPI.c
  ${PHP_SRC_ROOT}/main/main.c
  src/rp2350_network_stubs.c
  ${PHP_SRC_ROOT}/main/getopt.c
  ${PHP_SRC_ROOT}/main/explicit_bzero.c
  ${PHP_SRC_ROOT}/main/safe_bcmp.c
  ${PHP_SRC_ROOT}/main/output.c
  ${PHP_SRC_ROOT}/main/php_content_types.c
  ${PHP_SRC_ROOT}/main/php_ini.c
  ${PHP_SRC_ROOT}/main/php_ticks.c
  ${PHP_SRC_ROOT}/main/php_variables.c
  ${PHP_SRC_ROOT}/main/fopen_wrappers.c
  ${PHP_SRC_ROOT}/main/php_open_temporary_file.c
  ${PHP_SRC_ROOT}/main/strlcat.c
  ${PHP_SRC_ROOT}/main/snprintf.c
  ${PHP_SRC_ROOT}/main/spprintf.c
  ${PHP_SRC_ROOT}/main/strlcpy.c
  ${RP2350_STANDARD_SOURCES}
  ${PHP_SRC_ROOT}/ext/standard/libavifinfo/avifinfo.c
  ${RP2350_RANDOM_SOURCES}
  ${RP2350_HASH_SOURCES}
  ${RP2350_HASH_EXTRA_SOURCES}
  ${RP2350_JSON_SOURCES}
  ${RP2350_PCRE_SOURCES}
  ${RP2350_URI_SOURCES}
  ${RP2350_URIPARSER_SOURCES}
  ${RP2350_DATE_SOURCES}
  ${PHP_SRC_ROOT}/ext/lexbor/php_lexbor.c
  ${RP2350_LEXBOR_SOURCES}
  ${RP2350_NGHTTP2_SOURCES}
  ${RP2350_PICOHTTPPARSER_SOURCES}
  ${PHP_SRC_ROOT}/TSRM/TSRM.c
  ${ZEND_ENGINE_SOURCES}
)

target_include_directories(php_mcu_firmware PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/include
  ${CMAKE_CURRENT_BINARY_DIR}/generated
  ${PICO_SDK_PATH}/lib/lwip/src/include/compat/posix
  ${PICO_SDK_PATH}/lib/mbedtls/include
  ${PICO_SDK_PATH}/lib/mbedtls/include/mbedtls
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib/includes
  ${CMAKE_CURRENT_LIST_DIR}/third_party/nghttp2/lib
  ${CMAKE_CURRENT_LIST_DIR}/third_party/picohttpparser
  ${PHP_SRC_ROOT}
  ${PHP_SRC_ROOT}/main
  ${PHP_SRC_ROOT}/TSRM
  ${PHP_SRC_ROOT}/Zend
  ${PHP_SRC_ROOT}/ext/date
  ${PHP_SRC_ROOT}/ext/date/lib
  ${PHP_SRC_ROOT}/ext/lexbor
  ${PHP_SRC_ROOT}/ext/uri/uriparser/include
)

target_compile_definitions(php_mcu_firmware PRIVATE
  ZEND_ENABLE_STATIC_TSRMLS_CACHE=1
  ZEND_EMBEDDED_NO_PHP_HEADERS=1
  RP2350_WAIT_FOR_USB_SERIAL_MS=${RP2350_WAIT_FOR_USB_SERIAL_MS}
  HAVE_INT32_T=1
  HAVE_UINT32_T=1
  _POSIX_TIMERS=1
  _POSIX_MONOTONIC_CLOCK=1
  HAVE_TIMELIB_CONFIG_H=1
  HAVE_LEXBOR=1
  HAVE_BUNDLED_PCRE=1
  PCRE2_CODE_UNIT_WIDTH=8
  LEXBOR_VERSION=\"2.5.0\"
  URI_ENABLE_ANSI=1
  URI_NO_UNICODE=1
  URI_STATIC_BUILD=1
  HAVE_ARPA_INET_H=1
  BUILDING_NGHTTP2=1
  RP2350_FULL_STANDARD=1
  PICO_MALLOC_PANIC=0
  RP2350_WIFI_SSID=\"${RP2350_WIFI_SSID_ESC}\"
  RP2350_WIFI_PASS=\"${RP2350_WIFI_PASS_ESC}\"
  RP2350_ENABLE_EPD=$<BOOL:${RP2350_ENABLE_EPD}>
  RP2350_ENABLE_TFT=$<BOOL:${RP2350_ENABLE_TFT}>
  # Keep SRAM .heap modest; newlib sbrk is redirected to a PSRAM window.
  PICO_HEAP_SIZE=16384
)

target_link_libraries(php_mcu_firmware
  pico_stdlib
  pico_cyw43_arch_lwip_threadsafe_background
  pico_mbedtls
  pico_lwip_mbedtls
  hardware_adc
  hardware_gpio
  hardware_i2c
  hardware_pwm
  hardware_spi
  hardware_xip_cache
)

target_compile_options(php_mcu_firmware PRIVATE
  $<$<COMPILE_LANGUAGE:C>:-include rp2350_posix_compat.h>
)

pico_enable_stdio_usb(php_mcu_firmware 1)
pico_enable_stdio_uart(php_mcu_firmware 1)

pico_add_extra_outputs(php_mcu_firmware)
