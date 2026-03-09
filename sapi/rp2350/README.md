# RP2350 SAPI (experimental)

PHP as an embedded firmware runtime on the RP2350. Target board: **Pimoroni Badger 2350**.

## Current status

## TODO

- Use `LIGHT_SENSE` on Badger 2350 too (stock/reference board also has an ambient light sensor; mirror the Tufty light-sense support on the EPD/Badger path).
- Bring in zlib support so runtime PNG decoding / compressed asset loading becomes possible without pre-decoding everything offline.
- Convert remaining RP2350 SAPI C++ sources (`*.cpp`) to C where practical, to reduce mixed-language build/runtime complexity.


- Zend engine compiles directly into firmware; executes embedded `main.php`
- Known-good runtime path prints `tick` from `main.php` loop on UART
- PSRAM hardware init (QPI/XIP on CS1) is working
- Zend allocator now uses a PSRAM-backed `mmap()` arena (`rp2350_mmap.c`)
- POSIX shims stub out unsupported host APIs
- MCU builtins include button/LED helpers and EPD drawing helpers
- MCU builtins now include Wi-Fi helpers (init/connect/disconnect/status/IP)
- PCRE extension (`preg_*`) is included (bundled PCRE2, JIT disabled on target)
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

# Optional: set USB-serial wait window before boot logs (default: 10000ms)
cmake -S sapi/rp2350 -B sapi/rp2350/build_badger \
  -DPICO_BOARD=pimoroni_badger2350 \
  -DRP2350_WAIT_FOR_USB_SERIAL_MS=10000

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
  - `wifi:ok:<0|1> status:<code> ip4:<addr|none> ip6:<addr|none>`

Exposed PHP APIs:
- `mcu_wifi_init(): bool`
- `mcu_wifi_connect(string $ssid, ?string $password = null, int $timeout_ms = 15000): bool`
- `mcu_wifi_disconnect(): bool`
- `mcu_wifi_status(): int`
- `mcu_wifi_ip4(): string|false`
- `mcu_wifi_ip6(): string|false`
- `mcu_ntp_sync(?string $server = "pool.ntp.org", int $timeout_ms = 15000): bool`

Time bootstrap / TLS note:
- Manual wall-clock set (`mcu_set_time`) is intentionally removed.
- Time is now expected to come from RTC-on-boot plus NTP (`mcu_ntp_sync`).
- If both are unavailable/incorrect, TLS certificate validation may fail until time is corrected.

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
- `http://` and `https://` loading is handled by custom RP2350 wrappers:
  - HTTP/1.1 parsing via `picohttpparser`
  - HTTP/2 path via `nghttp2` when ALPN negotiates `h2`
  - HTTPS transport via lwIP `altcp_tls`
  - Supports both `file_get_contents()` and read-stream APIs (`fopen`/`fread`) for HTTP/HTTPS.
- HTTPS uses a bundled Mozilla CA root set (`certs/mozilla-cacert.pem`) with verification required.
- Refresh the embedded CA bundle with:
  - `sapi/rp2350/tools/update-ca-bundle.sh`
- CA chain-selection note (`example.com` / Cloudflare):
  - Different TLS clients can receive different chain variants from the same host.
  - On this target, Cloudflare may serve a chain ending at `AAA Certificate Services` instead of `SSL.com TLS ECC Root CA 2022`.
  - To keep HTTPS verification stable on-device, the curated embedded bundle intentionally includes `AAA Certificate Services` (via `certs/extra/aaa_certificate_services.pem`) in addition to selected Mozilla roots.
- TLS version note:
  - With current Pico lwIP + mbedTLS integration in this firmware, HTTPS negotiates TLS 1.2 in practice (`ver=TLSv1.2`).
  - A TLS-1.3-only client configuration was tested and currently fails to connect on this target.
  - Keep TLS 1.2 enabled as the compatibility baseline for now.

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
- Power/scheduling lifecycle:
  - Refine button interrupt wait/scheduler path:
    - Current `mcu_button_wait()` is semaphore+IRQ wake with timeout and works for responsiveness.
    - Revisit precise 1 Hz scheduling strategy (the prior `hrtime`/deadline variant caused stalls on target).
    - Add low-level button IRQ diagnostics (`irq count`, `wake count`, `timeout count`) to aid tuning.
  - Make EPD updates non-blocking:
    - Explore async e-ink refresh pipeline to avoid blocking PHP execution during render/update.
    - If async is too invasive, evaluate running EPD work on the second RP2350 core with safe handoff/synchronization.
  - Add deep-sleep lifecycle:
    - Add a controlled deep-sleep path with explicit peripheral bring-down before sleep and deterministic bring-up after wake.
    - Define/validate re-init ordering for critical blocks (UART, Wi-Fi/CYW43, PSRAM/QMI, EPD, ADC, timers/IRQs) to avoid wake-time hangs.
- Entropy hardening:
  - Add optional entropy seeding/mixing from external I2C sensor noise as an additional source (defense-in-depth on top of hardware RNG).
- Time sync hardening:
  - SNTP is now wired; add retry/backoff policy and periodic background resync.
  - Add fallback servers and optional DHCP-provided NTP server handling.
- Network diagnostics baseline:
  - Add a simple smoke check (or runtime counter) so ICMP/ICMPv6 responsiveness regressions are visible during bring-up.
- Add mDNS support:
  - Evaluate lwIP mDNS responder/client integration for device discovery on local networks.
  - Define minimal RP2350 surface (hostname announce + lookup) and a basic smoke test (`*.local` resolution).
- Explore JIT feasibility:
  - Investigate whether any constrained/partial opcache+JIT mode is viable on RP2350 (likely off by default, experimental only).
  - Document hard blockers (memory model, executable memory constraints, code cache placement, toolchain requirements).
- Split large sources:
  - Break up oversized RP2350 integration files (especially `rp2350_eval_zend.c`) into focused units (networking, builtins, stream wrappers, runtime bootstrap).
  - Keep interfaces small and testable to reduce bring-up/debug friction.
- EPD geometry constants:
  - Consider exposing panel dimensions as engine constants (for example `MCU_EPD_WIDTH` / `MCU_EPD_HEIGHT`) from C so PHP-side framebuffer/layout code cannot drift from firmware geometry.
- Revisit directory/layout consistency:
  - Normalize placement of RP2350 sources/headers (root vs `src/` vs `include/`) to one clear convention.
  - Document the convention and move files incrementally to avoid churn.
- Clarify lwIP source layering:
  - Document exactly which files are consumed from `third_party/pico-sdk/lib/lwip/src` vs `third_party/pico-sdk/src/rp2_common/pico_lwip`.
  - Capture why the RP2 common shim layer overrides/wraps parts of upstream lwIP and where to patch TLS behavior safely.
- Re-evaluate HTTP client dependency strategy:
  - Revisit whether to keep custom wrapper stack or move toward a higher-level client library (`libcurl` / `wget2`-class) once memory/runtime constraints are better characterized.
  - Compare integration cost vs feature coverage (redirects, auth, richer stream context support, protocol breadth).
- Firmware-visible error signaling:
  - Blink a dedicated LED pattern on PHP warning/error/fatal paths so failures are visible without UART attached.
  - Define stable severity-to-pattern mapping (warning vs fatal) and avoid blocking critical loops.

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
