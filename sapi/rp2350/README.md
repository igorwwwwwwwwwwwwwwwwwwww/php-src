# RP2350 SAPI (experimental)

PHP as an embedded firmware runtime on the RP2350.  Target board: **Pimoroni Badger 2350**.

## Current status

- Zend engine compiles directly into firmware; executes embedded `main.php` via `zend_eval_stringl()`
- Known-good runtime path prints `tick` from `main.php` loop on UART
- PSRAM hardware init (QPI/XIP on CS1) is working
- Zend MM handoff to PSRAM is temporarily disabled for stability (see TODO below)
- POSIX shims stub out filesystem / signals / fibers / setjmp
- Single builtin: `mcu_sleep_ms(int $ms)` — blocking delay from PHP
- Zend observer runtime is explicitly disabled during bring-up to avoid allocator-path crashes
- Embedded fake filesystem is enabled for script loading (`/main.php`, `/lib.php`, etc)
- Board I/O via overridable weak symbols:
  - `rp2350_platform_write()`  / `rp2350_platform_flush()`
  - `rp2350_platform_log()`    / `rp2350_platform_readline()`
- Pico SDK adapter in `rp2350_transport_pico.c`

## Files

| File | Purpose |
|------|---------|
| `src/main.c` | Firmware entry point: init stdio → init PSRAM → run `main.php` forever |
| `fs/*.php` | Extra embedded VFS files for `require` / `include` (mapped as `/<name>.php`) |
| `rp2350_psram.c` / `.h` | PSRAM QPI init + (currently disabled) Zend MM handoff |
| `rp2350_eval_zend.c` | Zend engine bring-up, `mcu_sleep_ms` registration, `rp2350_eval_execute()` |
| `rp2350_transport_pico.c` | Pico SDK strong overrides for platform I/O |
| `rp2350_transport.h` | Platform I/O declarations |
| `rp2350_posix_stubs.c` | POSIX functions that newlib does not provide |
| `rp2350_zend_fibers_stub.c` | Fiber entry-points stubbed out (no RTOS) |
| `rp2350_zend_optimizer_stub.c` | Optimizer no-ops (no opcache) |
| `include/` | Compat headers: `sys/mman.h`, `dirent.h`, `rp2350_posix_compat.h`, `main/php_config.h` |
| `boards/pimoroni_badger2350.h` | Custom board header (UART on GPIO 4/5, PSRAM CS on GPIO 8) |
| `CMakeLists.txt` | Self-contained firmware build |
| `main.php` | Embedded PHP script — edit and rebuild to change what the board runs |
| `fetch-deps.sh` | Clone pico-sdk + picotool into `third_party/` |

## Build

```bash
# Fetch pico-sdk and picotool (one-time)
./sapi/rp2350/fetch-deps.sh

# Configure
cmake -S sapi/rp2350 -B sapi/rp2350/build_badger -DPICO_BOARD=pimoroni_badger2350

# Build
cmake --build sapi/rp2350/build_badger -j
```

Artifacts:
- `sapi/rp2350/build_badger/php_mcu_firmware.uf2`
- `sapi/rp2350/build_badger/php_mcu_firmware.elf`

## Flash

BOOTSEL mass-storage (drag-and-drop):
```
# hold BOOTSEL while plugging in, then copy:
cp sapi/rp2350/build_badger/php_mcu_firmware.uf2 /Volumes/RP2350/
```

`picotool` (auto-reboot into BOOTSEL and flash):
```
picotool load -x sapi/rp2350/build_badger/php_mcu_firmware.uf2 -f
```

## Serial output

```
picocom -b 115200 --imap lfcrlf /dev/cu.usbmodem101
```

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

Connect with GDB in another terminal:

```bash
arm-none-eabi-gdb -q \
    -ex "file /Users/igor/code/php-src/sapi/rp2350/build_badger/php_mcu_firmware.elf" \
    -ex "target extended-remote :3333" \
    -ex "target extended-remote :3333"
```

Useful GDB command after connect:

```gdb
monitor reset halt
```

Recommended reset + breakpoint workflow for bring-up:

```gdb
delete breakpoints
monitor reset init
set $sp = *(unsigned int*)0x10000000
set $pc = *(unsigned int*)0x10000004

thb main
b rp2350_psram_init
b rp2350_eval_startup
b rp2350_psram_install_zend_mm
b zend_post_startup
b isr_hardfault
b isr_invalid
```

Run sequence:

```gdb
c
# on main: c
# on rp2350_psram_init: finish
# on rp2350_eval_startup: c (or finish)
```

If a fault occurs, capture:

```gdb
bt
info registers
x/16i $pc-16
```

Notes:
- Use the ELF that matches the UF2 you flashed (for example `build/` vs `build_badger/`).
- Useful breakpoints for bring-up:
  - `main`
  - `rp2350_psram_init`
  - `rp2350_eval_startup`

## Customising scripts

Edit `sapi/rp2350/main.php`, then rebuild and reflash.
Additional PHP files can be placed in `sapi/rp2350/fs/*.php` and loaded from
PHP using `require '/name.php';` (absolute VFS path).

## Memory layout

| Region | Size | Used for |
|--------|------|----------|
| Flash (XIP CS0) | 16 MB | Firmware code + rodata |
| SRAM | 512 KB | Stack, pico-sdk, BSS |
| PSRAM (XIP CS1 @ 0x11000000) | 8 MB | Initialised and writable; Zend heap handoff currently disabled |

## Known-good now

- Build with default configuration
- Flash with `picotool load -x sapi/rp2350/build_badger/php_mcu_firmware.uf2 -f`
- UART shows repeated `tick` from `main.php`

## TODO: PSRAM Zend heap reintegration

- Re-enable PSRAM-backed Zend heap with a clean allocator integration path
- Avoid mixed-heap lifetime corruption (startup heap vs post-startup heap switch)
- Re-enable observer runtime only after allocator path is stable

## Configure option (host-side engine build)

```bash
PATH="/opt/homebrew/opt/bison/bin:$PATH" ./buildconf --force
PATH="/opt/homebrew/opt/bison/bin:$PATH" ./configure --disable-all --enable-rp2350 --disable-opcache-jit
PATH="/opt/homebrew/opt/bison/bin:$PATH" make -j8
```

## Important constraints

- No opcache, no JIT, no ZTS, no fibers.
- `configure.ac` auto-discovers `sapi/*/config*.m4` — no manual top-level hook needed.
- UART defaults to GPIO 4/5 (GPIO 0–3 are rear LEDs on Badger 2350).
- PSRAM CS1 GPIO must be configured as `GPIO_FUNC_XIP_CS1` (function 9) before QMI M1 use.
- `flash_range_erase/program` saves and restores QMI M1 timing if `FLASH_DEVINFO` CS1 is
  not set in OTP; firmware re-inits PSRAM on each boot so this is fine.
