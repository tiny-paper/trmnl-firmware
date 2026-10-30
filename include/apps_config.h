// Generated from config/apps.json by tools/gen_apps_config.py: do not edit (docs/apps.md "Configuration").
#pragma once

// apps
#define APPS_DIR "/apps" // apps.dir
#define APPS_DIR_REL "apps" // the same without the leading /
#define APPS_DATA_DIR "/appdata" // apps.data_dir
#define APPS_DATA_DIR_REL "appdata" // the same without the leading /
#define APP_UPLOAD_MAX 2097152u // apps.upload_max_bytes
#define APP_UPLOAD_MARGIN 16384u // apps.upload_margin_bytes
// wifi_page
#define WS_AP_SSID "Writer" // wifi_page.ap_ssid
#define WS_HOST "writer" // wifi_page.host_name
#define WS_PAGE_TITLE "Writer files" // wifi_page.page_title
#define WS_IDLE_S 900u // wifi_page.idle_s
#define WS_FAILS_PER_ROUND 5u // wifi_page.fails_per_lock
#define WS_LOCK_MS 60000u // wifi_page.lock_ms
#define WS_MAX_FAILS 15u // wifi_page.max_fails
#define WS_DOC_MAX 1048576u // wifi_page.doc_max_bytes
#define WS_FONT_MAX 4194304u // wifi_page.font_max_bytes
#define WS_FREE_MARGIN 32768u // wifi_page.free_margin_bytes
#define WS_UPLOAD_STALL_MS 30000u // wifi_page.upload_stall_ms
// usb_drive
#define DRIVE_VOLUME_LABEL "TRMNL" // usb_drive.volume_label
#define DRIVE_USB_VID 12346u // usb_drive.usb_vid
#define DRIVE_USB_PID 16386u // usb_drive.usb_pid
#define DRIVE_USB_MANUFACTURER "TRMNL" // usb_drive.usb_manufacturer
#define DRIVE_USB_PRODUCT "TRMNL X writer" // usb_drive.usb_product
#define DRIVE_USB_SERIAL "000001" // usb_drive.usb_serial
#define DRIVE_USB_INTERFACE "Writer drive" // usb_drive.usb_interface
#define DRIVE_SCSI_VENDOR "TRMNL   " // usb_drive.scsi_vendor
#define DRIVE_SCSI_PRODUCT "X writer drive  " // usb_drive.scsi_product
// usb_keyboard
#define USB_PORT_CLEAR_MS 1000u // usb_keyboard.clear_ms
#define USB_PORT_PROBE_MS 3000u // usb_keyboard.probe_ms
#define USB_PORT_RETRY_MS 10000u // usb_keyboard.retry_ms
