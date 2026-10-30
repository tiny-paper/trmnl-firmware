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
| USB drive (API 2) | `usb_drive_start(timeout)`, `usb_drive_state`, `usb_drive_stop(result)`: below |

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

## USB drive (API version 2)

The app's writer files appear on a computer as a USB drive (Freewrite style). Only the layout moves:
`documents/<folder>/<title>.md|.txt`, `settings/settings.json`, `settings/fonts/<file>`.

| Step | What happens |
|---|---|
| `usb_drive_start` | finishes any interrupted import, exports the layout files to a FAT16 image in PSRAM (up to 6 MB), gives the USB PHY to TinyUSB mass storage: serial and flashing over the cable are off |
| while shown | the computer reads and writes only the image, never LittleFS; the app's file writes return `APP_EBUSY` |
| end | eject, unplug (VBUS gone or bus suspended 3 s), no computer within the timeout, `usb_drive_stop`, or the app ending; serial comes back first |
| import | after an eject, a sync of the last write, or 2 s without writes; else nothing is taken (`cut`) |

| Rule | How |
|---|---|
| nothing overwritten silently | a file changed on the computer only replaces the X's; changed on both: the computer's is kept as `<title> (computer).md` beside it and the pair listed in `.drive/conflicts.txt` (tab separated) for the app to let the user choose |
| deletes | not taken: the X keeps the file (`kept_deleted`) |
| other files | outside the layout, hidden (`._x.md`, `.Trashes`), too long (over 64 bytes) or bad names: left alone, counted |
| whole or nothing | a damaged or reformatted image takes nothing; every write is staged under `.drive/stage/` and read back, then one rename of `.drive/plan` commits; a power cut before it drops the stage, after it the next app start finishes the renames |
| full X | checked before staging, and a failed staging write undoes all (`APP_ENOSPC`) |
| serial back | on every end; a restart's shutdown handler and a constructor at every start (panic, watchdog) move the PHY back to the USB-Serial-JTAG; a long press asks the app to exit and restarts after 2 s if it does not. BOOT-button ROM flashing is never affected |

Code: `lib/trmnl/src/drive_{fat,layout,export,import,session}.cpp` (host tested), `src/usb_drive*.cpp`,
`src/usb_port.cpp`, `src/drive_littlefs.cpp`; TinyUSB 0.21 (`espressif/tinyusb`, config in `src/tusb/`).

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
| USB drive: FAT image (damage, random flips, mtools/dosfstools interop), names, conflicts, full disk, power loss at every write, when a showing takes changes | `test/test_drive_*` | `pio test -e native -f 'test_drive_*'` |
| USB drive on the simulated X: journal finished or dropped on the real LittleFS, power loss in it, a drive no computer takes (the sim has no USB OTG) | `test/sim/test_drive.py` | below |

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

### Device check: USB drive

Not proven without hardware: TinyUSB on the X's OTG port, the PHY switch both ways, unplug detection,
real computers' FAT writes.

| Step | Look for |
|---|---|
| An app calling `usb_drive_start(60000)` (the writer's Ctrl+P "Show as drive"), cable to a Mac, Windows and Linux computer in turn | a drive "TRMNL" with `documents/` and `settings/`; log `USB drive: <app> shows N files` on UART0 |
| While shown | no serial port on the computer; `esptool` over the cable fails |
| Edit a document, add one, copy a 2 MB font, eject | serial port back within 2 s; log `import 0: 1 added, 1 updated`; files on the X |
| Edit the same document on the X first (before showing), then on the computer, eject | both versions: `<title> (computer).md` and a line in `.drive/conflicts.txt` |
| Pull the cable during a large copy | `unplugged during a write: nothing taken`; the X's files unchanged; serial back after replugging |
| Pull the cable 5 s after a copy finished (no eject) | the copy is taken |
| Show the drive, press reset; show it, hold the touch bar 4 s | serial back after each; nothing taken |
| Hold BOOT and press reset while the drive shows | ROM download mode, flashing works |
| Laptop goes to sleep while the drive shows | the drive ends (`ended (2)`), changes after the last sync not taken unless 2 s quiet |
