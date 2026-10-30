# Native apps (TRMNL X)

An app is an ELF file in `/apps/<name>.elf` on the flash file system. The Apps item in the menu lists them; a middle
tap starts one. TRMNL stays the default: when the app returns, fails or is ended, the device goes back to TRMNL.

## Interface (`include/app_api.h`)

The firmware calls `int app_main(const app_api_t *api)`. Return 0 when done, anything else is shown as an error.

| Field | Meaning |
|---|---|
| `app_info` | in section `.trmnl_app_info` (macro `APP_INFO(name, stack)`): magic, API version, stack size, name |
| `api->version`, `api->size` | firmware's API version and table size; new calls go at the end only |

| Group | Calls |
|---|---|
| screen | `screen_width`, `screen_height`, `screen_set_mode` (1-bit or 4-bit gray, returns the framebuffer), `screen_stride`, `screen_refresh` (rectangle or all, partial or full) |
| input | `input_poll(ev, timeout)`: key down/up (code, modifiers, UTF-8) from a terminal on the USB serial port, touch bar taps (zone; swipes not yet), `APP_EVENT_EXIT` |
| files | `file_open/read/write/seek/close/list/rename/remove/mkdir`, paths relative to `/appdata/<app>/` |
| time | `millis`, `wall_time` (Unix s, 0 if unset), `sleep_ms` (yields, returns early on an event) |
| log | `log(fmt, ...)` |
| net | `net_up` (saved 2.4 GHz network; 5 GHz goes through the modem: not yet), `http_request` (method, URL, headers, body; response into the app's buffer; TLS with the IDF certificate bundle) |
| settings | `setting_get/set`: up to 256 bytes per key, NVS, per app |
| exit | `exit(code)`: ends the app now |

Errors are negative `APP_E*` values.

## Rules the firmware enforces

| Case | Result |
|---|---|
| file missing, truncated, not ELF, not Xtensa 32-bit | error screen, back to TRMNL |
| no `app_info`, bad magic, newer API version than the firmware | error screen |
| sections larger than the largest free PSRAM block, or a relocation the loader cannot apply | error screen |
| long press on the touch bar | `APP_EVENT_EXIT`; after `APP_EXIT_GRACE_MS` (2 s) the device restarts into TRMNL |
| app ran 5 s, then the device restarted | resumed (path in NVS `app_last`, flag `app_resume`) |
| app crashes while starting, or any panic or watchdog reset | not resumed: the flag is cleared before each start and set only after 5 s |
| stack over `APP_MAX_STACK` (64 KB, internal RAM) | error screen |

## Build an app

| Step | Command |
|---|---|
| toolchain | `pio run -e TRMNL_X` once (installs `toolchain-xtensa-esp-elf`) |
| build | `apps/build.sh hello` writes `apps/build/hello.elf` (all apps: `apps/build.sh`) |
| flags | elf_loader's `project_elf`: `-fPIC -shared -nostdlib -e app_main`, stripped; `-Wl,--undefined=app_info` keeps the header |

An app includes only `app_api.h` (and `apps/common/draw.h` for text and boxes). The loader resolves libc symbols
(`memcpy`, `strlen`, ...) from its own table; anything else is refused at load ("needs a function the firmware lacks").

## Copy an app to the X

The apps live in the LittleFS partition (`0x620000`). Writing it replaces the cached screens (TRMNL downloads them
again); Wi-Fi and settings stay in NVS.

| Step | Command |
|---|---|
| image | `python3 apps/pack.py .pio/build/TRMNL_X/littlefs.bin littlefs-apps.bin apps/build/hello.elf` (venv with `littlefs-python`) |
| flash | `esptool.py --chip esp32s3 write_flash 0x620000 littlefs-apps.bin` |

## Tests

| What | Where | Run |
|---|---|---|
| ELF checks (truncation, arch, tables, relocations, app_info, memory, random damage) | `test/test_app_elf_check` | `pio test -e native -f test_app_elf_check` |
| paths, resume state, serial keys | `test/test_app_path`, `test_app_resume`, `test_app_keys` | `pio test -e native -f test_app_*` |
| on the simulated X: menu start, long press, broken apps, stuck app, resume, crash at start, missing file | `test/sim` (pytest) | below |

Sim tests: build `pio run -e TRMNL_X`, the simulator (`cargo build --release` in `~/workspace/trmnl-sim`, or
`$TRMNLSIM_DIR`), then `python3 -m venv .venv && .venv/bin/pip install pytest littlefs-python` and
`.venv/bin/pytest test/sim`. A set-up device is made once per session (about 2 min); each test boots a copy.

## Device check

Not proven on a real X: PSRAM code execution on the device's cache settings, the panel output of the app's
framebuffer (1-bit and gray), partial refresh, touch timing on the real IQS323, HTTPS from an app.

| Step | Look for |
|---|---|
| Flash the TRMNL_X build of this branch, then `littlefs-apps.bin` with `hello.elf` (above) | device boots into TRMNL as before |
| Middle tap, the menu | an "Apps..." item between "Touchbar Tutorial" and "Special..." |
| Apps..., hello | "Hello from an app", "Run 1, screen 1872x1404", a box, the long press hint; serial log `Apps: Hello (/apps/hello.elf) running` |
| Tap left, middle, right | one line per tap in the box ("touch left tap", ...), drawn without a full flash |
| Type in a serial terminal (115200) | "key down 'a' code 512 mods 0" lines |
| Hold the touch bar 2 s | back to the TRMNL screen; log `Apps: hello ended with 0` |
| Start hello, wait 10 s, press reset | hello comes back ("Run 2"); log `Apps: resuming /apps/hello.elf` |
| Flash an image with `test/sim/apps/crash` (build with `OUT=apps/build apps/build.sh test/sim/apps/crash`), start it | one panic and restart, then TRMNL; not started again |
