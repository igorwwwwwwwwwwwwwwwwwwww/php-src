cmake_minimum_required(VERSION 3.21)

set(PICO_BOARD "pimoroni_badger2350" CACHE STRING "Pico board type")
set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards" CACHE STRING "Custom board header search path")
set(PICO_PLATFORM "rp2350" CACHE STRING "Pico platform")
set(PICO_DEFAULT_UART "0" CACHE STRING "Default UART instance for stdio")
set(PICO_DEFAULT_UART_TX_PIN "4" CACHE STRING "Default UART TX pin for stdio")
set(PICO_DEFAULT_UART_RX_PIN "5" CACHE STRING "Default UART RX pin for stdio")

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
set(RP2350_MAIN_PHP_PATH "${CMAKE_CURRENT_LIST_DIR}/main.php")
set(RP2350_MAIN_PHP_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/rp2350_main_php.h")

file(READ "${RP2350_MAIN_PHP_PATH}" RP2350_MAIN_PHP_RAW)
string(REPLACE "\\" "\\\\" RP2350_MAIN_PHP_ESC "${RP2350_MAIN_PHP_RAW}")
string(REPLACE "\"" "\\\"" RP2350_MAIN_PHP_ESC "${RP2350_MAIN_PHP_ESC}")
string(REPLACE "\n" "\\n\"\n\"" RP2350_MAIN_PHP_ESC "${RP2350_MAIN_PHP_ESC}")
configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/src/rp2350_main_php.h.in"
  "${RP2350_MAIN_PHP_HEADER}"
  @ONLY
)

file(GLOB ZEND_ENGINE_SOURCES "${PHP_SRC_ROOT}/Zend/*.c")
list(REMOVE_ITEM ZEND_ENGINE_SOURCES
  "${PHP_SRC_ROOT}/Zend/zend_dtrace.c"
  "${PHP_SRC_ROOT}/Zend/zend_fibers.c"
  "${PHP_SRC_ROOT}/Zend/zend_system_id.c"
)

add_executable(php_mcu_firmware
  src/main.c
  rp2350_eval_zend.c
  rp2350_psram.c
  rp2350_zend_fibers_stub.c
  rp2350_zend_optimizer_stub.c
  rp2350_posix_stubs.c
  rp2350_transport_pico.c
  ${PHP_SRC_ROOT}/TSRM/TSRM.c
  ${ZEND_ENGINE_SOURCES}
)

target_include_directories(php_mcu_firmware PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/include
  ${CMAKE_CURRENT_BINARY_DIR}/generated
  ${PHP_SRC_ROOT}
  ${PHP_SRC_ROOT}/main
  ${PHP_SRC_ROOT}/TSRM
  ${PHP_SRC_ROOT}/Zend
)

target_compile_definitions(php_mcu_firmware PRIVATE
  ZEND_ENABLE_STATIC_TSRMLS_CACHE=1
  ZEND_EMBEDDED_NO_PHP_HEADERS=1
  # Give pico-sdk's newlib heap enough room for USB TinyUSB init, alarm pools,
  # mutexes etc.  The default 2 KB is too small when the full Zend BSS is present.
  PICO_HEAP_SIZE=65536
)

target_link_libraries(php_mcu_firmware
  pico_stdlib
  hardware_gpio
  hardware_xip_cache
)

target_compile_options(php_mcu_firmware PRIVATE
  $<$<COMPILE_LANGUAGE:C>:-include rp2350_posix_compat.h>
)

pico_enable_stdio_usb(php_mcu_firmware 1)
pico_enable_stdio_uart(php_mcu_firmware 1)

pico_add_extra_outputs(php_mcu_firmware)
