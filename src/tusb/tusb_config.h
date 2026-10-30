// TinyUSB's configuration for the USB drive (src/usb_drive_msc.cpp): one mass storage interface on
// the S3's OTG port, full speed. On TinyUSB's include path through the root CMakeLists.txt, as the
// espressif/tinyusb component expects the project to bring it.
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#define CFG_TUSB_OS OPT_OS_FREERTOS
#define CFG_TUSB_OS_INC_PATH freertos/
#define CFG_TUSB_DEBUG 0

#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUD_DWC2_SLAVE_ENABLE 1

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 1
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_MSC_EP_BUFSIZE 4096

#endif
