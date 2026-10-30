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
| Wi-Fi page (API 2) | `webshare_start(mode)`, `webshare_stop`, `webshare_status`: see below |

Errors are negative `APP_E*` values.

## Wi-Fi page (last resort for moving files)

From any phone browser, no app: list, download (one file or a zip), upload, rename and delete the app's
`documents/<folder>/<title>.md|.txt`, and add `settings/fonts/*.ttf|.otf`. Anything else in the app folder
(`settings/settings.json`, other names, hidden files) is never listed, read, written or removed. Logic in
`lib/trmnl/src/webshare_*`, server in `src/webshare.cpp`, page in `src/webshare_page.h`, AP and probe URLs shared
with the Wi-Fi setup portal in `lib/wificaptive/src/captive_ap.cpp`.

| Rule | How |
|---|---|
| Off by default | only `webshare_start(APP_WEBSHARE_AP or APP_WEBSHARE_HOME)` from the app; off when the app ends (runner), after 15 min without a request, after 15 wrong codes |
| AP mode | open network "Writer", `http://4.3.2.1`, phones open it as a captive portal |
| Home mode | saved 2.4 GHz network, `http://writer.local` (mDNS) and the IP in `status.ip` |
| Code | 6 digits in `status.code`, the app shows it on the X's screen; no API works without a session from `POST /api/login`; 5 wrong codes lock 60 s (doubling), 15 stop the page |
| Changes | need the `X-Writer` header (another site's form cannot send it) and the session cookie (SameSite=Strict) |
| No silent overwrite | upload to `.webshare.tmp` in the target folder, renamed when all announced bytes arrived and the file on disk has that size; same bytes: nothing written; other bytes: saved as `title (web N).md`, the old file untouched; rename onto an existing name is refused |
| Limits | document 1 MB, font 4 MB, 32 KB of the file system stays free, one upload at a time, 30 s without data drops it, leftover temp files are removed at start |
| Zip | stored, made while sending (no copy in memory), up to 128 documents; a read error cuts the connection |

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
| Wi-Fi page logic: names, code and lock, uploads (full disk, close loses data, pulled connection, power loss, bad names, no overwrite), rename, delete, zip | `test/test_webshare` | `pio test -e native -f test_webshare` |
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

### Wi-Fi page (nothing of this ran on a device or in the simulator)

Needs an app that calls `webshare_start` and shows `status.code`. Take a phone and a PC browser.

| Step | Look for |
|---|---|
| Start AP mode | network "Writer" appears, phone opens the page by itself (captive portal; if not, `http://4.3.2.1`), X shows the code |
| Open the page, type a wrong code 5 times | "too many wrong codes", the right code is refused for a minute |
| Right code | list of documents and fonts, free space |
| Upload a .md to a folder, then the same again, then a changed copy | "saved", "already on the X", "kept both, saved as ... (web 1)"; the writer shows both files; the first is unchanged |
| Upload while pulling the phone off Wi-Fi at 50 % | no half file in the writer's list; next start log has no leftover `.webshare.tmp` |
| Upload 1 MB+ and a file with a name like `a:b.md` | refused with a reason |
| Rename onto an existing title | "name already taken" |
| Delete a document | gone from the writer's list |
| Download all as zip | opens on the phone and PC, same files and sizes |
| Add a .ttf | appears under Fonts; writer can use it |
| Leave it 15 min | page stops answering, "Writer" network gone; the app's status says not running |
| Long press (leave the app) while the page is on | network gone |
| Home mode with the home Wi-Fi | `writer.local` opens on PC and iPhone; on Android use the IP |
| Android and iPhone captive pop-up | opens the page (iOS pop-up may block cookies: then open it in Safari via `http://4.3.2.1`) |
| Heap while serving a 100-file zip | no reset; serial log shows no allocation errors |
