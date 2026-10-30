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
| keyboards (API 2) | `keyboard_enable(APP_KBD_USB)`, `keyboard_status()`: keys of a USB keyboard with their HID usage, `APP_EVENT_KEYBOARD` when one comes or goes (below) |
| files | `file_open/read/write/seek/close/list/rename/remove/mkdir`, paths relative to `/appdata/<app>/` |
| time | `millis`, `wall_time` (Unix s, 0 if unset), `sleep_ms` (yields, returns early on an event) |
| log | `log(fmt, ...)` |
| net | `net_up` (saved 2.4 GHz network; 5 GHz goes through the modem: not yet), `http_request` (method, URL, headers, body; response into the app's buffer; TLS with the IDF certificate bundle) |
| settings | `setting_get/set`: up to 256 bytes per key, NVS, per app |
| exit | `exit(code)`: ends the app now |
| USB drive (API 2) | `usb_drive_start(timeout)`, `usb_drive_state`, `usb_drive_stop(result)`: below |

Errors are negative `APP_E*` values.

## Keyboards

| | |
|---|---|
| keys | KEY_DOWN / KEY_UP with `hid_usage` (HID keyboard page) and `modifiers` (`APP_MOD_ALTGR` = right Alt). No layout in the firmware: Enter, Escape, Backspace, Tab, Delete, arrows, Home, End, Page Up/Down, F1..F12 get their `app_key_t`, every other key is `APP_KEY_HID` with an empty `utf8`; the app maps its layout (`lib/trmnl/src/hid_keys.cpp`) |
| removed mid-typing | KEY_UP for every key still held, then `APP_EVENT_KEYBOARD` with the bits still connected |
| Bluetooth LE | not yet: `keyboard_enable(APP_KBD_BLE)` returns `APP_ENOTSUP`. Pairing and reconnect rules are in `lib/trmnl/include/ble_pair.h` (tested). Open: BT is off in `sdkconfigs/sdkconfig.TRMNL_X`; IDF 5.5's NimBLE HID host (`esp_hid/src/nimble_hidh.c`) starts service discovery right after connecting and handles no pairing, passkey or encryption events, so a keyboard that needs encryption fails there: it needs a patched copy (security first, then discovery) |

USB port (`lib/trmnl/include/usb_port.h`, applied by `src/usb_keyboard.cpp`): device mode (charging, serial, flashing) is the default.

| Case | Port |
|---|---|
| no app asked for USB keys, or the app ended | device mode (the app's end waits for it) |
| a charger or computer powers the port (charger PG, IOX pin 0), or PG unreadable | device mode, in the same 100 ms poll |
| on battery for 1 s, app wants USB keys | host mode: 5 V boost on (IOX pin 1), USB host + `espressif/usb_host_hid` (boot keyboard) |
| host mode, no keyboard within 3 s / keyboard removed | device mode, next probe after 10 s |
| restart, crash, deep sleep | device mode: the boot (`wake_init.cpp`), a restart handler and `goToSleep` give the PHY back to the USB serial/JTAG and clear the ROM's forced download boot |
| USB stack does not stop within 1 s, or the boost cannot be switched off | restart (which starts in device mode) |

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
| USB port rules (power, 1 s wait, probe, removal, charger in host mode, app exit, restart), HID reports to key events | `test/test_usb_port`, `test/test_hid_keys` | `pio test -e native -f test_usb_port -f test_hid_keys` |
| on the simulated X: menu start, long press, broken apps, stuck app, resume, crash at start, missing file | `test/sim/test_apps.py` (pytest) | below |
| on the simulated X: upload over USB serial, other files kept, bad CRC, cut-off upload, refused headers | `test/sim/test_upload.py` | below |
| on the simulated X: the charger pin read with the right polarity, device mode while docked and in a short power gap, serial keys still work | `test/sim/test_keyboards.py` (the sim has no USB-OTG controller: host mode is not simulated) | below |
| USB drive: FAT image (damage, random flips, mtools/dosfstools interop), names, conflicts, full disk, power loss at every write, when a showing takes changes | `test/test_drive_*` | `pio test -e native -f 'test_drive_*'` |
| USB drive on the simulated X: journal finished or dropped on the real LittleFS, power loss in it, a drive no computer takes (the sim has no USB OTG) | `test/sim/test_drive.py` | below |

Sim tests: build `pio run -e TRMNL_X`, the simulator (`cargo build --release` in `~/workspace/trmnl-sim`, or
`$TRMNLSIM_DIR`), then `python3 -m venv .venv && .venv/bin/pip install pytest littlefs-python` and
`.venv/bin/pytest test/sim`. A set-up device is made once per session (about 2 min); each test boots a copy.

## Device check

Not proven on a real X: everything in USB host mode (boost powers the port, PG while the boost runs, enumeration of
a keyboard through an OTG adapter, clean switch back, flashing afterwards), PSRAM code execution on the device's cache settings, the panel output of the app's
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

### Device check: USB keyboard (needs a USB-C OTG adapter and a wired keyboard)

| Step | Look for |
|---|---|
| Send `keys.elf`, start Keys with the PC cable in | log `USB keys: input power: device mode`; serial log keeps running; no host mode |
| Unplug the PC, plug the OTG adapter + keyboard | within about 1 s: `host mode: looking for a keyboard`, then `keyboard connected`; screen "keyboards now: usb" |
| Type `a`, Shift+`a`, AltGr+`2` (AZERTY ~), arrows, F1 | `down usage 0x04 code 0x300`, `mods 0x01`, `mods 0x10` with usage 0x1f, codes 0x100..0x103, 0x108 |
| Hold a key, pull the keyboard | `up usage ...` for the held key, `keyboard removed`, `no keyboard: device mode`, within 1 s |
| Keyboard in, then plug the PC cable into the adapter or the port | `input power: device mode` within 1 s (unproven: PG may read "no power" while the boost drives VBUS; then the PC is not seen until the keyboard is pulled) |
| Keyboard in, long press to end Keys | `Apps: keys ended with 0` and device mode; plug the PC: `/dev/ttyACM0` appears |
| Keyboard in, press reset | the X boots normally (not ROM download mode), PC sees the serial port, `esptool.py chip_id` works |
| Keyboard in, wait for an app crash or flash `test/sim/apps/crash` style panic in Keys | boots normally, serial port present |
| After all of the above: `pio run -e TRMNL_X -t upload` over the cable | flashing works without holding BOOT |

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
