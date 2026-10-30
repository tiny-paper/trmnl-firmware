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

Over the USB cable, into `/apps/<name>.elf` only: TRMNL's own files (cached screens, the modem file) stay. Never
flash a file system image to a real X (it replaces those files); `test/sim/flashfs.py` makes one for the simulator only.

| Step | Do |
|---|---|
| on the X | menu, Apps..., Receive over USB... (shows "Waiting for an app on the USB cable"; any tap or 2 min idle ends it) |
| on the computer | `python3 apps/upload.py /dev/ttyACM0 apps/build/hello.elf` (venv with `pyserial`); prints `sent hello.elf` |

| Protocol (`lib/trmnl/include/app_upload.h`) | |
|---|---|
| header | `TAPP PUT <name>.elf <size> <crc32 hex>`; refused: bad name, 0 bytes, over 2 MB, more than the free space less 16 KB |
| data | 64-byte blocks, each answered `TAPP ACK <bytes>` (one USB packet; the serial RX buffer drops what does not fit) |
| end | `TAPP OK <name>.elf`, or `TAPP ERR short` (no byte for 5 s) / `bad crc` / `write failed`: the old app stays |
| writes | `/apps/.upload.tmp`, renamed over the app only after size and CRC match; a leftover one is removed at the next start |

## Tests

| What | Where | Run |
|---|---|---|
| ELF checks (truncation, arch, tables, relocations, app_info, memory, random damage) | `test/test_app_elf_check` | `pio test -e native -f test_app_elf_check` |
| paths, resume state, serial keys, upload header and CRC | `test/test_app_path`, `test_app_resume`, `test_app_keys`, `test_app_upload` | `pio test -e native -f test_app_*` |
| on the simulated X: menu start, long press, broken apps, stuck app, resume, crash at start, missing file | `test/sim/test_apps.py` (pytest) | below |
| on the simulated X: upload over USB serial, other files kept, bad CRC, cut-off upload, refused headers | `test/sim/test_upload.py` | below |

Sim tests: build `pio run -e TRMNL_X`, the simulator (`cargo build --release` in `~/workspace/trmnl-sim`, or
`$TRMNLSIM_DIR`), then `python3 -m venv .venv && .venv/bin/pip install pytest littlefs-python` and
`.venv/bin/pytest test/sim`. A set-up device is made once per session (about 2 min); each test boots a copy.

## Device check

Not proven on a real X: PSRAM code execution on the device's cache settings, the panel output of the app's
framebuffer (1-bit and gray), partial refresh, touch timing on the real IQS323, HTTPS from an app.

| Step | Look for |
|---|---|
| Flash the TRMNL_X build of this branch (app only, `pio run -e TRMNL_X -t upload`; no file system image) | device boots into TRMNL as before, its cached screens still there |
| Middle tap, the menu | an "Apps..." item between "Touchbar Tutorial" and "Special..." |
| Apps..., Receive over USB..., then `apps/upload.py <port> apps/build/hello.elf` | `sent hello.elf`; log `Apps: received /apps/hello.elf`; a tap ends it, hello is listed |
| Pull the cable during an upload of a big file, plug it back, start Receive again | log `upload ... failed: short`; the old app (if any) still starts |
| Apps..., hello | "Hello from an app", "Run 1, screen 1872x1404", a box, the long press hint; serial log `Apps: Hello (/apps/hello.elf) running` |
| Tap left, middle, right | one line per tap in the box ("touch left tap", ...), drawn without a full flash |
| Type in a serial terminal (115200) | "key down 'a' code 512 mods 0" lines |
| Hold the touch bar 2 s | back to the TRMNL screen; log `Apps: hello ended with 0` |
| Start hello, wait 10 s, press reset | hello comes back ("Run 2"); log `Apps: resuming /apps/hello.elf` |
| Send `test/sim/apps/crash` (build with `OUT=apps/build apps/build.sh test/sim/apps/crash`, then upload.py), start it | one panic and restart, then TRMNL; not started again |
