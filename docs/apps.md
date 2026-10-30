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
| keyboards (API 2) | `keyboard_enable(APP_KBD_USB \| APP_KBD_BLE)`, `keyboard_status()`: keys of a USB or Bluetooth LE keyboard with their HID usage, `APP_EVENT_KEYBOARD` when one comes or goes (below) |
| files | `file_open/read/write/seek/close/list/rename/remove/mkdir`, paths relative to `/appdata/<app>/` |
| time | `millis`, `wall_time` (Unix s, 0 if unset), `sleep_ms` (yields, returns early on an event) |
| log | `log(fmt, ...)` |
| net | `net_up` (saved 2.4 GHz network; 5 GHz goes through the modem: not yet), `http_request` (method, URL, headers, body; response into the app's buffer; TLS with the IDF certificate bundle) |
| settings | `setting_get/set`: up to 256 bytes per key, NVS, per app |
| exit | `exit(code)`: ends the app now |
| USB drive (API 2) | `usb_drive_start(timeout)`, `usb_drive_state`, `usb_drive_stop(result)`: below |
| Wi-Fi page (API 2) | `webshare_start(mode)`, `webshare_stop`, `webshare_status`: see below |
| battery, time (API 2) | `battery_percent(&charging)`: the fuel gauge's percent at the last wake (APP_EIO unreadable), charging now; `local_time(&t)`: the clock in the X's time zone (NVS `app_tz`, a POSIX TZ string). TRMNL's firmware keeps no time zone today: until one is stored the fields are UTC with `zone_known` 0; APP_ENOCLOCK before the clock was set |
| event log (API 2.1) | `event_log_lines(buf, len, n)`: the last n lines of the "Event log", oldest first; check `api->size >= APP_API_SLOT(35)` first (`APP_API_MINOR`) |

Errors are negative `APP_E*` values.

## Event log

Recent device events kept on the X across restarts and deep sleep (`src/evlog.cpp`, ring `lib/trmnl/include/evlog_ring.h`), in `/evlog.bin` (`event_log.bytes`, the oldest go first).

| | |
|---|---|
| logged | boot and its reset reason (not deep sleep wakes); BLE keyboard connected, ready, disconnected (reason code and words), connect failed, failed; USB keyboard connected, removed, host mode dropped with a keyboard on, charger unreadable, boost not on; app start, resume, end, errors, restarts; `Net:` lines of `net_up` and `http_request`. Firmware code: `evlog_add(fmt, ...)` or `Log_event(fmt, ...)` (serial log too) |
| line | `#<boot> <uptime s> <UTC time or -> <text>`; the same text again in a row: ` (xN)` |
| writes | only on events, at most every `event_log.flush_gap_ms`; at once before the firmware's own restarts and sleep. A crash loses at most that gap. A damaged file: empty log, "the saved log was damaged" |
| read | `TAPP EVLOG` + newline on the serial port while an app runs: `TAPP EVLOG <line>` each, oldest first, then `TAPP EVLOG END`. `TAPP EVLOG CLEAR`: `TAPP EVLOG CLEARED`. An app: `event_log_lines` |

## Keyboards

| | |
|---|---|
| keys | KEY_DOWN / KEY_UP with `hid_usage` (HID keyboard page) and `modifiers` (`APP_MOD_ALTGR` = right Alt). No layout in the firmware: Enter, Escape, Backspace, Tab, Delete, arrows, Home, End, Page Up/Down, F1..F12 get their `app_key_t`, every other key is `APP_KEY_HID` with an empty `utf8`; the app maps its layout (`lib/trmnl/src/hid_keys.cpp`) |
| removed mid-typing | KEY_UP for every key still held, then `APP_EVENT_KEYBOARD` with the bits still connected |
| Bluetooth LE | `src/ble_keyboard.cpp` on NimBLE (central only); rules in `lib/trmnl`: `ble_pair.h` (pairing, bond, reconnect), `ble_hogp.h` (security, then HID over GATT), `hid_report_map.h` (report protocol to boot format). Table below |

### Bluetooth LE keyboard

The radio is on only while an app has `APP_KBD_BLE` enabled; TRMNL's own refreshes run with it off.

| Case | Firmware |
|---|---|
| app enables BLE, no bond | pairing screen over the app (its pixels kept in PSRAM, its refreshes skipped): keyboards found (HID service 0x1812 or keyboard appearance; name, dBm), left/right choose, middle pairs, long press still ends the app |
| picked | connect, then security first (bonding, LE Secure Connections, the X is DisplayOnly): the passkey in big digits, typed on the keyboard; a keyboard without keys for it pairs just-works |
| encrypted | then (not before) HID service, characteristics, descriptors, report map, report references; report protocol if the map has a keyboard input report, else boot protocol (Protocol Mode 0); notifications on |
| typing | reports as boot reports into `hid_keys.h`: KEY_DOWN / KEY_UP with HID usage and modifiers as on USB; `APP_EVENT_KEYBOARD` with `APP_KBD_BLE` when ready and when gone |
| pairing screen gone | the app's pixels back with a full refresh; an app that drew meanwhile redraws on `APP_EVENT_KEYBOARD` |
| bond | NimBLE's keys in NVS (`nimble_bond`), the keyboard's name and identity address in NVS `ble_kbd`; one keyboard: older bonds are deleted |
| bonded, app enables BLE (also after sleep or a restart) | connect directly, no screen; nothing answers in 8 s: again after 2 s, until the app ends |
| link lost while typing (sleep, range, Easy-Switch to another channel) | KEY_UP for keys held, `APP_EVENT_KEYBOARD`, connect again at once |
| bonded keyboard reconnects and asks for security itself | NimBLE encrypts with the stored key; our own start then gives EALREADY and waits for that encryption (`hogp_secure_started`); log `BLE kbd: ...` on connect, disconnect (reason code and words) and ready |
| keyboard lost the bond (paired elsewhere): key missing, or it asks to pair again | our bond deleted, the list again with "The keyboard forgot this X: pair it again" |
| wrong passkey / refused | the list again, "Pairing failed: try again" |
| passkey not typed in 40 s (SMP gives up at 30 s) | the list again, "The passkey was not typed in time" |
| not a keyboard (no HID service, no keyboard report, no boot input) | the list again, "That is not a keyboard" |
| Apps menu | "Forget <name>" while a keyboard is bonded: both records cleared, the next app pairs again |
| Wi-Fi at the same time | software coexistence (`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`, a build error without it); Wi-Fi modem sleep while BLE is on (during and after an app's `net_up` join, and when BLE starts with Wi-Fi up); scan window 30 ms of 50 ms for listing and for connecting (NimBLE's default connect scan holds the radio the whole attempt); rules in `radio_share.h`, test `test_radio_share` |
| `net_up` log | one line per call: `Net: up in N ms (already/joined, rssi, ble ...)` or `Net: failed: <reason> after N ms, wl_status, rssi, ble ..., internal RAM N free`; an HTTP request that fails to open logs `Net: <method> request failed to open: <error>` (apps report both as no network) |

| Memory (TRMNL_X build) | Bluetooth off (before) | on |
|---|---|---|
| app image (3 MB slot) | 1,708,000 B | 1,883,424 B (+175 KB; 60 % of the slot) |
| internal RAM, static (IRAM + .data + .bss, always) | | +19,152 B (IRAM +14,920, .data +968, .bss +3,264) |
| internal RAM left for the heap at boot (esp_idf_size DIRAM remain) | 182,561 B | 163,409 B |
| while an app uses BLE: controller heap (internal), NimBLE host task 4 KB, keyboard task 8 KB | | log `BLE keys: radio on; internal RAM N bytes used` (device check) |
| NimBLE pools, scan list, message queue, the app's saved screen (up to 1.3 MB in gray) | | PSRAM |
| TRMNL's Wi-Fi + TLS (10 static RX buffers 16 KB, dynamic RX/TX, mbedTLS 16 KB in + 4 KB out internal) | fits as before | BLE is off then: only the static cost above applies |

## USB port roles

The S3 has one USB PHY. `src/usb_port_role.cpp` owns it (rules in `lib/trmnl/include/usb_port.h`, tested in `test/test_usb_port`); the keyboard and the drive take it and give it back there, one at a time.

| Role | Who | PHY |
|---|---|---|
| serial (default) | always, and after every switch-back | USB serial/JTAG: serial log, flashing |
| USB host | `usb_keyboard.cpp`, host mode below | OTG controller, host |
| USB drive | `usb_drive.cpp` while the drive shows | OTG controller, TinyUSB device |
| the other role asks | refused: `usb_drive_start` returns `APP_EBUSY` while a keyboard is a host; no host probe while the drive shows | unchanged |
| switch-back | `usb_port_serial_now`: boot constructor and `wake_init.cpp`, `goToSleep`, the restart/panic shutdown handler, each feature's end, the runner after an app ends (long press included) | serial; ROM forced download boot cleared |

Keyboard host mode (`usb_port_step`, applied by `src/usb_keyboard.cpp`): device mode (charging, serial, flashing) is the default.

| Case | Port |
|---|---|
| no app asked for USB keys, or the app ended | device mode (the app's end waits for it) |
| a charger or computer powers the port (charger PG, IOX pin 0), or PG unreadable | device mode, in the same 100 ms poll |
| a panel refresh holds the expander (PG not readable then) | the last reading stays, host mode too; read again after the refresh. Unreadable only after 30 s (a hang) |
| on battery for 1 s, app wants USB keys | host mode: 5 V boost on (IOX pin 1), USB host + `espressif/usb_host_hid` (boot keyboard) |
| host mode, no keyboard within 3 s / keyboard removed | device mode, next probe after 10 s |
| restart, crash, deep sleep | device mode: the switch-back of "USB port roles" |
| the USB drive shows | device mode, no probe until it ends |
| USB stack does not stop within 1 s, or the boost cannot be switched off within 8 s (a refresh holds the expander) | restart (which starts in device mode) |

## Wi-Fi page (last resort for moving files)

From any phone browser, no app: list, download (one file or a zip), upload, rename and delete the app's
`documents/<folder>/<title>.md|.txt`, and add `settings/fonts/*.ttf|.otf`. Anything else in the app folder
(`settings/settings.json`, other names, hidden files) is never listed, read, written or removed. Logic in
`lib/trmnl/src/webshare_*`, server in `src/webshare.cpp`, page in `src/webshare_page.h`, AP and probe URLs shared
with the Wi-Fi setup portal in `lib/wificaptive/src/captive_ap.cpp`.
Names, limits and times below are the defaults of "Configuration".

| Rule | How |
|---|---|
| Off by default | only `webshare_start(APP_WEBSHARE_AP or APP_WEBSHARE_HOME)` from the app; off when the app ends (runner), after 15 min without a request, after 15 wrong codes |
| Default (`APP_WEBSHARE_AUTO`) | the saved home network, so the phone stays on its Wi-Fi; only if none is reachable the access point. `status.mode` says which. `APP_WEBSHARE_HOME` alone does not fall back (`APP_ENONET`) |
| One-time link | `status.url` ("Online at ...") and `status.login_url` (`/?t=<128-bit token>`, for a QR): one login, 15 min, gone at stop, a wrong token counts like a wrong code, never logged (test reads the source). Typing the code works too. Changed in place: API 2 is not released |
| AP mode | open network "Writer", `http://4.3.2.1`, phones open it as a captive portal |
| Home network | saved 2.4 GHz network, `http://writer.local` (mDNS) and the IP in `status.ip` |
| Code | 6 digits in `status.code`, the app shows it on the X's screen; no API works without a session from `POST /api/login`; 5 wrong codes lock 60 s (doubling), 15 stop the page |
| Changes | need the `X-Writer` header (another site's form cannot send it) and the session cookie (SameSite=Strict) |
| No silent overwrite | upload to `.webshare.tmp` in the target folder, renamed when all announced bytes arrived and the file on disk has that size; same bytes: nothing written; other bytes: saved as `title (web N).md`, the old file untouched; rename onto an existing name is refused |
| Limits | document 1 MB, font 4 MB, 32 KB of the file system stays free, one upload at a time, 30 s without data drops it, leftover temp files are removed at start |
| Zip | stored, made while sending (no copy in memory), up to 128 documents; a read error cuts the connection |

## Configuration

Every deployment value of the app work is in `config/apps.json` (checked in). `tools/gen_apps_config.py` writes
`include/apps_config.h` from it; run it after a change and commit both. TRMNL's own settings are not in it.
Every key is required, unknown keys and values out of range are refused. `test/config` (pytest) fails when the
header is stale, a key is missing, or one of these values (or a `#define` of its macro) is left in our sources.

| Key | Macro | Default | What |
|---|---|---|---|
| `apps.dir` | `APPS_DIR` | `/apps` | where apps are installed and listed |
| `apps.data_dir` | `APPS_DATA_DIR` | `/appdata` | each app's folder is `<data_dir>/<name>` |
| `apps.upload_max_bytes` | `APP_UPLOAD_MAX` | 2097152 | largest app, over USB serial or Wi-Fi |
| `apps.upload_margin_bytes` | `APP_UPLOAD_MARGIN` | 16384 | left free on the file system by an app upload |
| `wifi_page.ap_ssid` | `WS_AP_SSID` | `Writer` | the X's own open access point |
| `wifi_page.host_name` | `WS_HOST` | `writer` | mDNS name (`<host>.local`) and the zip's name `<host>-documents.zip` |
| `wifi_page.page_title` | `WS_PAGE_TITLE` | `Writer files` | the page's title and heading |
| `wifi_page.idle_s` | `WS_IDLE_S` | 900 | the page switches off after this long without a request; the one-time link's life |
| `wifi_page.fails_per_lock` | `WS_FAILS_PER_ROUND` | 5 | wrong codes before a lock |
| `wifi_page.lock_ms` | `WS_LOCK_MS` | 60000 | the first lock, doubled each round |
| `wifi_page.max_fails` | `WS_MAX_FAILS` | 15 | wrong codes in all, then the page stops |
| `wifi_page.doc_max_bytes` | `WS_DOC_MAX` | 1048576 | largest document upload |
| `wifi_page.font_max_bytes` | `WS_FONT_MAX` | 4194304 | largest font upload |
| `wifi_page.free_margin_bytes` | `WS_FREE_MARGIN` | 32768 | left free on the file system by a page upload |
| `wifi_page.upload_stall_ms` | `WS_UPLOAD_STALL_MS` | 30000 | an upload without data this long is dropped |
| `usb_drive.volume_label` | `DRIVE_VOLUME_LABEL` | `TRMNL` | the FAT volume label (A-Z 0-9 space _ -, up to 11) |
| `usb_drive.usb_vid`, `usb_pid` | `DRIVE_USB_VID`, `_PID` | 0x303A, 0x4002 | USB IDs (Espressif's, esp_tinyusb's MSC PID); use your own when you ship |
| `usb_drive.usb_manufacturer`, `usb_product`, `usb_serial`, `usb_interface` | `DRIVE_USB_*` | `TRMNL`, `TRMNL X writer`, `000001`, `Writer drive` | USB string descriptors |
| `usb_drive.scsi_vendor`, `scsi_product` | `DRIVE_SCSI_*` | `TRMNL`, `X writer drive` | SCSI inquiry (8 and 16 characters, padded) |
| `usb_keyboard.clear_ms` | `USB_PORT_CLEAR_MS` | 1000 | no input power this long before host mode |
| `usb_keyboard.probe_ms` | `USB_PORT_PROBE_MS` | 3000 | host mode without a keyboard this long: back to device mode |
| `usb_keyboard.retry_ms` | `USB_PORT_RETRY_MS` | 10000 | then this long before the next probe |
| `event_log.bytes` | `EVLOG_BYTES` | 8192 | the event log's file and RAM (1 to 64 KB) |
| `event_log.flush_gap_ms` | `EVLOG_FLUSH_GAP_MS` | 15000 | at most one event log write this often |

## Install via Wi-Fi (proof of concept)

Menu > Apps > "Install via Wi-Fi" starts the same web server in an apps profile: home Wi-Fi first, else the access
point "Writer". The X shows "Online at http://<ip>/" (or the network to join), the 6-digit code and a QR of the
one-time login link; a long press, 15 min idle, or 15 wrong codes stop it. The page lists `/apps` (name, size, date)
and takes `.elf` files only. No manifest, no permissions, no update URLs.

| Rule | How |
|---|---|
| Only apps | the file name must be `<name>.elf` (letters, digits, `-`, `_`, 24 at most); size up to 2 MB |
| Checked first | the file goes to `apps/.webshare.tmp`, then `app_elf_check` (the loader's own checker) runs over it; a refused file is deleted and the page shows the checker's reason; the installed app is not touched |
| Replace | an app of the same name becomes `<name>.elf.bak` (an older `.bak` of a never started install stays: it is the last version that worked), then the temp file is renamed into place |
| First start | `.bak` is deleted once the new app ran 5 s or returned 0. If it does not load, returns an error, or crashes while starting (`appi_start` in NVS, checked at the next wake), `.bak` is put back and the error screen / the page says so. A power loss between the two renames is repaired at the next wake |
| Delete | the page's Delete removes `<name>.elf` and its `.bak` |
| curl | `curl -F file=@writer.elf "http://<ip>/apps?t=<token>"` (the token is the one in the QR link, good for one call) or `curl -F file=@writer.elf "http://<ip>/apps?code=123456"` (as often as needed; 5 wrong codes lock it). The answer is `{"ok":true,"name":"writer.elf"}` or `{"error":"refused: file is truncated"}`. Without a credential: 401 |
| Browser | cookie after the QR link or the code, plus the `X-Writer` header on changes |

Code: `src/webshare_install.cpp` (routes, page in `webshare_install_page.h`), `lib/trmnl/src/webshare_apps.cpp`
(checks, `.bak`), `lib/trmnl/src/app_install.cpp` (start bookkeeping, hooks in `src/app_runner.cpp`), QR by
`lib/qrcodegen` (Nayuki, MIT, vendored unmodified).

## Rules the firmware enforces

| Case | Result |
|---|---|
| file missing, truncated, not ELF, not Xtensa 32-bit | error screen, back to TRMNL |
| no `app_info`, bad magic, newer API version than the firmware | error screen |
| sections larger than the largest free PSRAM block, or a relocation the loader cannot apply | error screen |
| long press on the touch bar | `APP_EVENT_EXIT`; after `APP_EXIT_GRACE_MS` (2 s) the device restarts into TRMNL |
| TRMNL's sleep while an app runs | refused (`app_sleep_gate.h`, log `Sleep: refused, the app <name> is running`): the runner owns the device until the app exits |
| app ran 5 s, then the device restarted | resumed (path in NVS `app_last`, flag `app_resume`) |
| app crashes while starting, or any panic or watchdog reset | not resumed: the flag is cleared before each start and set only after 5 s |
| no touch or key reached the app before the restart (NVS `app_input`) | not resumed: "App stopped ... not resumed" on screen, then TRMNL (a dead touch bar must not trap the user) |
| `TAPP NORESUME` + newline on the serial port (115200) while an app runs | resume cleared, restart into TRMNL |
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
`src/usb_port_role.cpp`, `src/drive_littlefs.cpp`; TinyUSB 0.21 (`espressif/tinyusb`, config in `src/tusb/`).

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
| noise | the X skips bytes before `TAPP ` on a line (CR/LF both fine) and answers nothing to a line without it; a bad header is answered `TAPP ERR bad header: got <first 32 bytes, hex>`; upload.py opens the port with DTR/RTS low and sends the header a second time if no READY comes |
| data | 64-byte blocks, each answered `TAPP ACK <bytes>` (one USB packet; the serial RX buffer drops what does not fit) |
| end | `TAPP OK <name>.elf`, or `TAPP ERR short` (no byte for 5 s) / `bad crc` / `write failed`: the old app stays |
| writes | `/apps/.upload.tmp`; after size and CRC match, `app_install_commit` (the one install path, shared with Install via Wi-Fi): the loader's ELF check (`TAPP ERR refused: <reason>`), the old app kept as `.bak` until the new one started, then the rename. A leftover temp file is removed at the next start |

## Crash in an app

The runner logs `Apps: <name> entry at 0x...`. With the panic's `Backtrace:` line in the same log,
`tools/app_addr2line.py <app.elf> <log>` names the app's frames. Build that ELF with the app's normal flags but
without `-Wl,--strip-all` and the final strip (the code must be byte for byte the same; `-g` changes it).
The touch bar has one input path while awake, `touchbar_input.h`: TRMNL's menu (acts on a touch start), the firmware
screens and the app (a tap on release, a long press after 1.5 s). Each event is logged once:
`Touch: iqs323 chN down / tap / long press (N ms) -> <consumer>`. A mode switch of the IQS323 reads and publishes its
status: the switch can take the window of a touch event, and that touch then never reached the snapshot.

## Tests

| What | Where | Run |
|---|---|---|
| ELF checks (truncation, arch, tables, relocations, app_info, memory, random damage) | `test/test_app_elf_check` | `pio test -e native -f test_app_elf_check` |
| paths, resume state, serial keys, upload header and CRC | `test/test_app_path`, `test_app_resume`, `test_app_keys`, `test_app_upload` | `pio test -e native -f test_app_*` |
| USB port rules (roles: one at a time, only the owner switches back; power, 1 s wait, probe, removal, charger in host mode, drive showing, app exit, restart), HID reports to key events | `test/test_usb_port`, `test/test_hid_keys` | `pio test -e native -f test_usb_port -f test_hid_keys` |
| battery and local time: unreadable gauge, charging flag, clock never set, no or unusable zone (UTC fallback), DST, west of UTC across midnight | `test/test_app_clock` | `pio test -e native -f test_app_clock` |
| configuration: header up to date, every key required, no configured value left in the sources | `test/config` | `.venv/bin/pytest test/config` |
| Bluetooth keyboard: pairing and reconnect rules; security before discovery, report and boot protocol, encryption failures (key missing, wrong passkey, passkey timeout), step timeouts, bond deleted on the keyboard side; report maps (report IDs, bitmap, rollover, cut-off and random maps) | `test/test_ble_pair`, `test_ble_hogp`, `test_hid_report_map` (the sim has no Bluetooth) | `pio test -e native -f test_ble_pair -f test_ble_hogp -f test_hid_report_map` |
| on the simulated X: menu start, long press, broken apps, stuck app, resume, crash at start, missing file | `test/sim/test_apps.py` (pytest) | below |
| on the simulated X, touch through the emulated IQS323: Install via Wi-Fi and Receive over USB end on a long press, a middle tap and a long press reach an app right after it started; no resume without input, `TAPP NORESUME` | `test/sim/test_screens.py` | below |
| event log: wrap and bounded size, restart, damaged file, repeats, last lines that fit, write rate limit | `test/test_evlog_ring` | `pio test -e native -f test_evlog_ring` |
| event log on the simulated X: `TAPP EVLOG` lists the app's start across a restart, `CLEAR` | `test/sim/test_evlog.py` | below |
| no TRMNL sleep while an app runs | `test/test_app_sleep_gate` | `pio test -e native -f test_app_sleep_gate` |
| on the simulated X: upload over USB serial, other files kept, bad CRC, cut-off upload, refused headers | `test/sim/test_upload.py` | below |
| on the simulated X: the charger pin read with the right polarity, device mode while docked and in a short power gap, serial keys still work | `test/sim/test_keyboards.py` (the sim has no USB-OTG controller: host mode is not simulated) | below |
| USB drive: FAT image (damage, random flips, mtools/dosfstools interop), names, conflicts, full disk, power loss at every write, when a showing takes changes | `test/test_drive_*` | `pio test -e native -f 'test_drive_*'` |
| USB drive on the simulated X: journal finished or dropped on the real LittleFS, power loss in it, a drive no computer takes (the sim has no USB OTG) | `test/sim/test_drive.py` | below |

| paths, resume state, serial keys | `test/test_app_path`, `test_app_resume`, `test_app_keys` | `pio test -e native -f test_app_*` |
| Install via Wi-Fi: only .elf, checker refusal, size limit, .bak kept and restored on a failed or crashed first start, token/code needed | `test/test_webshare_apps` | `pio test -e native -f test_webshare_apps` |
| Wi-Fi page logic: names, code and lock, uploads (full disk, close loses data, pulled connection, power loss, bad names, no overwrite), rename, delete, zip | `test/test_webshare` | `pio test -e native -f test_webshare` |

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

### Wi-Fi page (nothing of this ran on a device or in the simulator)

Needs an app that calls `webshare_start` and shows `status.code`. Take a phone and a PC browser.

| Step | Look for |
|---|---|
| Start with Wi-Fi on (default) | `status.mode` home; phone on its own Wi-Fi opens `status.url`; the QR link logs in at once, the same link a second time asks for the code |
| Start with the router off | falls back to "Writer", `status.mode` AP |
| Start AP mode (forced) | network "Writer" appears, phone opens the page by itself (captive portal; if not, `http://4.3.2.1`), X shows the code |
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

### Device check: Bluetooth keyboard (Logitech POP Icon Keys; serial log at 115200)

| Step | Look for |
|---|---|
| Flash this build, boot, let TRMNL refresh once | refresh works as before (Wi-Fi + TLS); no `BLE keys` lines (radio off) |
| Send `keys.elf`, start Keys, nothing bonded | Keys' screen, then the pairing screen "Bluetooth keyboard", "Looking for keyboards..."; log `BLE keys: radio on; internal RAM N bytes used, M free (largest L)`: write N, M, L here |
| Keyboard: hold an Easy-Switch key 3 s (its light blinks fast) | "POP Icon Keys  -NN dBm" in the list within 5 s; other keyboards nearby listed too, not phones or headphones |
| Right, left (wraps), middle on the POP | "Connecting to POP Icon Keys...", then six big digits |
| Type the digits on the POP, then Enter | within 2 s Keys' own screen back; log `paired with POP Icon Keys`, `ready (report protocol)` (or boot); Keys shows "keyboards now: ble" |
| Type `a`, Shift+`a`, AltGr+`2`, arrows, F1, Fn media keys | `down usage 0x04 code 0x300`, `mods 0x01`, `mods 0x10` with 0x1f, codes 0x100..0x103, 0x108; media keys give nothing (consumer page) and no crash |
| Hold a key, switch the POP to Easy-Switch channel 2 | `up` for the held key, "keyboards now: none", log `keyboard gone` within 2 s |
| Back to channel 1, press a key | "keyboards now: ble" within 3 s, no pairing screen; typing works |
| Leave the POP untouched until it sleeps (about 5 min), then press a key | reconnects as above; the first key may be lost |
| Long press to end Keys, start it again | no pairing screen; typing within 3 s of a key press; log `radio off` at the end with internal RAM back near its start value |
| Press reset with Keys running 10 s | Keys resumes and reconnects without pairing |
| A Wi-Fi sync while typing: an app calling `http_request` in a loop (e.g. the writer's sync), type steadily for 1 min | no key lost or stuck; HTTPS calls succeed; note any lag over 0.5 s |
| Apps menu, "Forget POP Icon Keys", start Keys | the item gone; the pairing screen again |
| On the POP: pair channel 1 with a phone (the POP drops the X's bond), then start Keys on the X | log `failed (1)` then "The keyboard forgot this X: pair it again"; pairing again works |
| Pick the POP, type a wrong passkey + Enter | "Pairing failed: try again", the list again |
| Pick the POP, type nothing for 40 s | "The passkey was not typed in time", the list again |
| Not proven by any test | the X's identity address stays the same (public), so the POP finds it after a restart; a keyboard with a resolvable private address (none of Logitech's so far) may not reconnect |

### Install via Wi-Fi (nothing of this ran on a device or in the simulator)

| Step | Look for |
|---|---|
| Menu > Apps with no app in /apps | the entry "Install via Wi-Fi" is there (it used to hide the whole Apps item) |
| Choose it with the home Wi-Fi on | screen: "Install apps via Wi-Fi", "Online at http://<ip>/", "Code: nnnnnn", a QR on the right, "Long press to stop" |
| Scan the QR with a phone on the same Wi-Fi | the page opens already logged in and lists /apps; scanning the same QR a second time asks for the code |
| Choose it with the router off | text asks to join "Writer" and open http://4.3.2.1/ |
| Upload hello.elf | "installed"; Apps menu lists it; it starts |
| Upload a text file renamed to .elf, a truncated hello.elf, a file named x.bin | refused with the checker's reason / name refused; /apps unchanged |
| Upload a changed hello.elf over the old | page says "older version kept"; after the new one ran 5 s the note is gone (file gone in flash) |
| Upload `test/sim/apps/crash` build under the name of a good app, start it | panic, restart, TRMNL; the good app is back (page shows the note) |
| Upload an app returning 7 over a good one, start it | error screen says "the previous version was put back" |
| `curl -F file=@hello.elf "http://<ip>/apps?code=<code>"` | `{"ok":true,...}`; without `?code`: 401; `?t=<token from the QR link>` works once |
| Cut Wi-Fi during a big upload | no `.webshare.tmp` after the next start, installed app unchanged |
| Long press | network gone, back to TRMNL |
