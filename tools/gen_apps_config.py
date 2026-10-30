#!/usr/bin/env python3
"""config/apps.json -> include/apps_config.h: every deployment value of the app work (docs/apps.md "Configuration").

  tools/gen_apps_config.py              writes include/apps_config.h
  tools/gen_apps_config.py --check      fails when the header is not what the config gives
Every key in KEYS is required; unknown keys are refused too (a typo must not fall back to nothing)."""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "config/apps.json"
HEADER = ROOT / "include/apps_config.h"

# (section, key, macro, type, limit): str limit = max length; int limit = (min, max)
KEYS = [
    ("apps", "dir", "APPS_DIR", "path", 15),
    ("apps", "data_dir", "APPS_DATA_DIR", "path", 15),
    ("apps", "upload_max_bytes", "APP_UPLOAD_MAX", "int", (4096, 8 << 20)),
    ("apps", "upload_margin_bytes", "APP_UPLOAD_MARGIN", "int", (0, 1 << 20)),
    ("wifi_page", "ap_ssid", "WS_AP_SSID", "str", 32),
    ("wifi_page", "host_name", "WS_HOST", "host", 24),
    ("wifi_page", "page_title", "WS_PAGE_TITLE", "str", 40),
    ("wifi_page", "idle_s", "WS_IDLE_S", "int", (60, 24 * 3600)),
    ("wifi_page", "fails_per_lock", "WS_FAILS_PER_ROUND", "int", (1, 100)),
    ("wifi_page", "lock_ms", "WS_LOCK_MS", "int", (1000, 3600000)),
    ("wifi_page", "max_fails", "WS_MAX_FAILS", "int", (1, 1000)),
    ("wifi_page", "doc_max_bytes", "WS_DOC_MAX", "int", (1024, 8 << 20)),
    ("wifi_page", "font_max_bytes", "WS_FONT_MAX", "int", (1024, 8 << 20)),
    ("wifi_page", "free_margin_bytes", "WS_FREE_MARGIN", "int", (0, 1 << 20)),
    ("wifi_page", "upload_stall_ms", "WS_UPLOAD_STALL_MS", "int", (1000, 600000)),
    ("usb_drive", "volume_label", "DRIVE_VOLUME_LABEL", "label", 11),
    ("usb_drive", "usb_vid", "DRIVE_USB_VID", "int", (0, 0xFFFF)),
    ("usb_drive", "usb_pid", "DRIVE_USB_PID", "int", (0, 0xFFFF)),
    ("usb_drive", "usb_manufacturer", "DRIVE_USB_MANUFACTURER", "str", 31),
    ("usb_drive", "usb_product", "DRIVE_USB_PRODUCT", "str", 31),
    ("usb_drive", "usb_serial", "DRIVE_USB_SERIAL", "str", 31),
    ("usb_drive", "usb_interface", "DRIVE_USB_INTERFACE", "str", 31),
    ("usb_drive", "scsi_vendor", "DRIVE_SCSI_VENDOR", "scsi", 8),
    ("usb_drive", "scsi_product", "DRIVE_SCSI_PRODUCT", "scsi", 16),
    ("usb_keyboard", "clear_ms", "USB_PORT_CLEAR_MS", "int", (100, 60000)),
    ("usb_keyboard", "probe_ms", "USB_PORT_PROBE_MS", "int", (500, 60000)),
    ("usb_keyboard", "retry_ms", "USB_PORT_RETRY_MS", "int", (1000, 600000)),
]


def check_value(where, typ, limit, v):
    if typ == "int":
        if not isinstance(v, int) or isinstance(v, bool) or not limit[0] <= v <= limit[1]:
            raise ValueError("%s: a whole number from %d to %d" % (where, limit[0], limit[1]))
        return
    if not isinstance(v, str) or not v or len(v.encode()) > limit or not all(0x20 <= ord(c) < 0x7F for c in v):
        raise ValueError("%s: printable ASCII text of 1 to %d bytes" % (where, limit))
    if '"' in v or "\\" in v:
        raise ValueError("%s: no quote or backslash" % where)
    if typ == "path" and not re.fullmatch(r"/[a-z0-9_]+", v):
        raise ValueError("%s: one folder at the root, like /apps" % where)
    if typ == "host" and not re.fullmatch(r"[a-z0-9]([a-z0-9-]*[a-z0-9])?", v):
        raise ValueError("%s: a host name (a-z, 0-9, -)" % where)
    if typ == "label" and not re.fullmatch(r"[A-Z0-9_ -]+", v):
        raise ValueError("%s: a FAT label (A-Z, 0-9, space, _ -)" % where)


def load(path=CONFIG):
    cfg = json.loads(Path(path).read_text())
    if not isinstance(cfg, dict):
        raise ValueError("%s: an object of sections" % path)
    known = {}
    for sec, key, _m, _t, _l in KEYS:
        known.setdefault(sec, set()).add(key)
    for sec, keys in cfg.items():
        if sec not in known or not isinstance(keys, dict):
            raise ValueError("unknown section %r" % sec)
        for k in keys:
            if k not in known[sec]:
                raise ValueError("unknown key %s.%s" % (sec, k))
    out = []
    for sec, key, macro, typ, limit in KEYS:
        if key not in cfg.get(sec, {}):
            raise ValueError("missing key %s.%s" % (sec, key))
        v = cfg[sec][key]
        check_value("%s.%s" % (sec, key), typ, limit, v)
        out.append((sec, key, macro, typ, v))
    return out


def render(values):
    lines = [
        "// Generated from config/apps.json by tools/gen_apps_config.py: do not edit (docs/apps.md \"Configuration\").",
        "#pragma once",
        "",
    ]
    sec_now = None
    for sec, key, macro, typ, v in values:
        if sec != sec_now:
            lines.append("// %s" % sec)
            sec_now = sec
        if typ == "int":
            val = "%du" % v
        elif typ == "scsi":
            n = 8 if macro == "DRIVE_SCSI_VENDOR" else 16
            val = '"%s"' % v.ljust(n)  # SCSI inquiry fields: space padded, no NUL
        else:
            val = '"%s"' % v
        lines.append("#define %s %s // %s.%s" % (macro, val, sec, key))
        if typ == "path":
            lines.append('#define %s_REL "%s" // the same without the leading /' % (macro, v[1:]))
    return "\n".join(lines) + "\n"


def main(argv):
    text = render(load())
    if "--check" in argv:
        if not HEADER.exists() or HEADER.read_text() != text:
            print("FAIL: %s is not what %s gives: run tools/gen_apps_config.py" % (HEADER, CONFIG))
            return 1
        return 0
    HEADER.write_text(text)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except (ValueError, json.JSONDecodeError) as e:
        print("FAIL: %s" % e)
        sys.exit(1)
