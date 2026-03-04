# RP2350 SAPI (experimental)

PHP as an embedded firmware runtime on the RP2350. Target board: **Pimoroni Badger 2350**.

## Current status

- Zend engine compiles directly into firmware; executes embedded `main.php`
- Known-good runtime path prints `tick` from `main.php` loop on UART
- PSRAM hardware init (QPI/XIP on CS1) is working
- Zend allocator now uses a PSRAM-backed `mmap()` arena (`rp2350_mmap.c`)
- POSIX shims stub out unsupported host APIs
- MCU builtins include button/LED helpers and EPD drawing helpers
- MCU builtins now include Wi-Fi helpers (init/connect/disconnect/status/IP)
- Zend observer runtime is disabled during bring-up to reduce crash surface
- Embedded fake filesystem is enabled for script loading (`/main.php`, `/lib.php`, etc)
- Board I/O via overridable weak symbols:
  - `rp2350_platform_write()` / `rp2350_platform_flush()`
  - `rp2350_platform_log()` / `rp2350_platform_readline()`
- Pico SDK adapter in `rp2350_transport_pico.c`

## Reference firmware

- Stock/reference firmware tree is at `../badger2350`.
- Do not modify `../badger2350` from this project.
- This firmware is fully self-contained under `sapi/rp2350`.

## Files

| File | Purpose |
|------|---------|
| `src/main.c` | Firmware entry point: init PSRAM + stdio, start Zend, run `/main.php` |
| `fs/*.php` | Embedded VFS files for `require` / `include` |
| `rp2350_eval_zend.c` | Zend bring-up, utility callbacks, script/file execution |
| `rp2350_psram.c` / `.h` | PSRAM QPI/XIP init and status (`rp2350_psram_init`) |
| `rp2350_mmap.c` | PSRAM-backed `mmap/munmap/mprotect` used by Zend MM |
| `rp2350_transport_pico.c` | Pico SDK strong overrides for platform I/O |
| `rp2350_transport.h` | Platform I/O declarations |
| `rp2350_posix_stubs.c` | POSIX/newlib compatibility stubs |
| `rp2350_zend_fibers_stub.c` | Fiber entry points stubbed out |
| `rp2350_zend_optimizer_stub.c` | Optimizer no-ops |
| `include/` | Compat headers: `sys/mman.h`, `dirent.h`, `rp2350_posix_compat.h`, `main/php_config.h` |
| `boards/pimoroni_badger2350.h` | Board header (UART GPIO 4/5, PSRAM CS GPIO 8) |
| `lwipopts.h` | lwIP configuration used by CYW43/Pico Wi-Fi stack |
| `CMakeLists.txt` | Firmware build |
| `main.php` | Embedded entry script |
| `fetch-deps.sh` | Clone pico-sdk + picotool into `third_party/` |

## Build

```bash
# One-time: fetch pico-sdk and picotool
./sapi/rp2350/fetch-deps.sh

# Configure
cmake -S sapi/rp2350 -B sapi/rp2350/build_badger -DPICO_BOARD=pimoroni_badger2350

# Optional: wait up to 5000ms for USB serial connection before boot logs
cmake -S sapi/rp2350 -B sapi/rp2350/build_badger \
  -DPICO_BOARD=pimoroni_badger2350 \
  -DRP2350_WAIT_FOR_USB_SERIAL_MS=5000

# Build
cmake --build sapi/rp2350/build_badger -j
```

Artifacts:
- `sapi/rp2350/build_badger/php_mcu_firmware.uf2`
- `sapi/rp2350/build_badger/php_mcu_firmware.elf`

## Flash

BOOTSEL mass-storage:

```bash
cp sapi/rp2350/build_badger/php_mcu_firmware.uf2 /Volumes/RP2350/
```

`picotool` (requires board visible in BOOTSEL mode):

```bash
picotool load -x sapi/rp2350/build_badger/php_mcu_firmware.uf2 -f
```

Absolute-path variant:

```bash
picotool load -x /Users/igor/code/php-src/sapi/rp2350/build_badger/php_mcu_firmware.uf2 -f
```

## Serial output

```bash
picocom -b 115200 --imap lfcrlf /dev/cu.usbmodem101
```

If one-shot boot logs are easy to miss, set `-DRP2350_WAIT_FOR_USB_SERIAL_MS=<ms>` during configure.

If PHP execution fails and you see repeated `[halt] php main: ...`, the halt line includes the last captured Zend error (`zend=...`) for quick triage.

## Fast dev loop

```bash
cmake --build sapi/rp2350/build_badger -j8
picotool load -x /Users/igor/code/php-src/sapi/rp2350/build_badger/php_mcu_firmware.uf2 -f
picocom -b 115200 --imap lfcrlf /dev/cu.usbmodem101
```

## Wi-Fi foundation

Credentials source:
- Place credentials in `sapi/rp2350/wifi.env`:
  - `WIFI_SSID=YourSSID`
  - `WIFI_PASS=YourPassword`
- Quoted values are allowed (for spaces), e.g. `WIFI_SSID="Hot Signals In Your Area"`.
- Rebuild after editing `wifi.env` so values are compiled into firmware.

Current startup smoke path:
- `main.php` reads `getenv('WIFI_SSID')` / `getenv('WIFI_PASS')`.
- If SSID is non-empty, it runs Wi-Fi connect on boot and logs:
  - `wifi:init`
  - `wifi:ok:<0|1> status:<code> ip:<addr|none>`

Exposed PHP APIs:
- `mcu_wifi_init(): bool`
- `mcu_wifi_connect(string $ssid, ?string $password = null, int $timeout_ms = 15000): bool`
- `mcu_wifi_disconnect(): bool`
- `mcu_wifi_status(): int`
- `mcu_wifi_ip(): string|false`
- `mcu_tcp_request(string $host, int $port, string $payload, int $timeout_ms = 5000, int $max_read = 4096): string|false`
- `mcu_udp_sendto(string $host, int $port, string $payload, int $timeout_ms = 2000): bool`

Wi-Fi status constants:
- `MCU_WIFI_LINK_DOWN`
- `MCU_WIFI_LINK_JOIN`
- `MCU_WIFI_LINK_NOIP`
- `MCU_WIFI_LINK_UP`
- `MCU_WIFI_LINK_FAIL`
- `MCU_WIFI_LINK_NONET`
- `MCU_WIFI_LINK_BADAUTH`

Current limitation:
- This is connection/status/IP foundation only.
- PHP stream/socket transport is still mostly stubbed in `src/rp2350_network_stubs.c`, so `ext/curl` and generic network streams are not wired yet.
- TCP/UDP helpers above use lwIP raw APIs directly (under `NO_SYS=1`) and are intended as a bring-up path, not a full PHP sockets layer.

## SWD debug (OpenOCD + GDB)

Using Raspberry Pi forked OpenOCD:

```bash
git clone https://github.com/raspberrypi/openocd
cd openocd
./bootstrap
./configure
make -j
./src/openocd -s tcl -f interface/cmsis-dap.cfg -f target/rp2350.cfg
```

Connect with GDB:

```bash
arm-none-eabi-gdb -q \
  -ex "file /Users/igor/code/php-src/sapi/rp2350/build_badger/php_mcu_firmware.elf" \
  -ex "target extended-remote :3333" \
  -ex "target extended-remote :3333"
```

Useful monitor commands:

```gdb
monitor reset halt
monitor reset init
```

Bring-up breakpoint workflow:

```gdb
delete breakpoints
monitor reset init
thb main
b rp2350_psram_init
b rp2350_eval_startup
b isr_hardfault
b isr_invalid
c
```

If a fault occurs:

```gdb
bt
info registers
x/16i $pc-16
```

## VFS behavior

- `main.php` and each file in `fs/*.php` are embedded into flash at build time.
- Use absolute paths from PHP, for example `require '/lib.php';`.
- `stream_open_function` and `resolve_path_function` are wired into Zend for this VFS path.
- libc/newlib `_open()` is still a trap stub unless you implement a real filesystem backend.

## Memory model

| Region | Size | Used for |
|--------|------|----------|
| Flash (XIP CS0) | 16 MB | Firmware code + embedded PHP sources |
| SRAM | ~520 KB | Stacks, BSS, pico-sdk runtime, small libc heap |
| PSRAM (XIP CS1 @ 0x11000000) | 8 MB | PSRAM init verified, Zend `mmap()` arena |

Allocator notes:
- Keep Zend allocator enabled (`USE_ZEND_ALLOC` default path).
- `mmap()` is provided by `rp2350_mmap.c` and allocates from a dedicated PSRAM arena.
- newlib libc heap growth (`_sbrk`) is redirected into a separate PSRAM window.

## Known-good now

- Build from `sapi/rp2350/build_badger`
- Flash with `picotool` or `/Volumes/RP2350`
- UART shows firmware boot lines and script output (`tick` loop)

## TODO

- Replace bring-up PSRAM persistent bump allocator with a proper PSRAM heap allocator:
  - Keep a small startup-persistent arena for module/arginfo/class metadata.
  - Add real `malloc/free/realloc` semantics in PSRAM for long-lived runtime allocations.
  - Route only truly persistent allocations to the persistent arena.
- Refine button interrupt wait/scheduler path:
  - Current `mcu_button_wait()` is semaphore+IRQ wake with timeout and works for responsiveness.
  - Revisit precise 1 Hz scheduling strategy (the prior `hrtime`/deadline variant caused stalls on target).
  - Add low-level button IRQ diagnostics (`irq count`, `wake count`, `timeout count`) to aid tuning.
- Make EPD updates non-blocking:
  - Explore async e-ink refresh pipeline to avoid blocking PHP execution during render/update.
  - If async is too invasive, evaluate running EPD work on the second RP2350 core with safe handoff/synchronization.

## Important constraints

- No opcache, no JIT, no ZTS, no fibers.
- `configure.ac` auto-discovers `sapi/*/config*.m4`.
- UART defaults to GPIO 4/5 (GPIO 0-3 are rear LEDs on Badger 2350).
- PSRAM CS1 GPIO must be `GPIO_FUNC_XIP_CS1` before QMI M1 use.
- `flash_range_erase/program` can disturb QMI CS1 timing; firmware re-inits PSRAM on boot.

## External references

- Stock firmware reference: `../badger2350`
- Pico SDK sources in this repo: `sapi/rp2350/third_party/pico-sdk`
- Picotool in this repo: `sapi/rp2350/third_party/picotool`
- Optional inspiration codebase: `../rp2350/platforms/c`
