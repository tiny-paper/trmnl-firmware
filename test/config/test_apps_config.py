"""config/apps.json is the one place for the app work's deployment values (docs/apps.md "Configuration"):
the generated header matches it, every key is required, and none of the values is left in our sources.
Run: .venv/bin/pytest test/config (no device, no build)."""
import copy
import json
import re
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import gen_apps_config as gen  # noqa: E402

# The app work's sources (keyboards, USB drive, Wi-Fi page, loader); TRMNL's own files are not ours to change
OURS = ["include/app_api.h", "include/apps.h", "src/app_*", "src/usb_*", "src/webshare*", "src/keyboards*",
        "src/drive_*", "src/tusb/*", "lib/trmnl/src/app_*", "lib/trmnl/src/drive_*", "lib/trmnl/src/webshare_*",
        "lib/trmnl/src/usb_port*", "lib/trmnl/src/hid_keys*", "lib/trmnl/src/ble_pair*", "lib/trmnl/include/app_*",
        "lib/trmnl/include/drive_*", "lib/trmnl/include/webshare_*", "lib/trmnl/include/usb_port*",
        "lib/trmnl/include/hid_keys*", "lib/trmnl/include/ble_pair*"]
# Text that contains a configured word but is not the configured value, with the reason (cut out before the check)
ALLOWED = {
    "X-Writer": "the page's request header against other sites' forms: protocol, never shown",
    "not a TRMNL app (no app_info)": "TRMNL's own name in an error, not the drive's label or USB strings",
    "TRMNL continues at the next refresh.": "TRMNL's own name on the app error screen",
    "Apps: %s did not exit within %d ms - restarting into TRMNL": "TRMNL's own name in a log line",
}


def rel(p):
    return str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else p.name


def sources():
    files = sorted({p for g in OURS for p in ROOT.glob(g) if p.is_file() and p.suffix in (".c", ".cpp", ".h")})
    assert len(files) > 30, "the source list matches almost nothing: %s" % files
    return [p for p in files if p.name != "apps_config.h"]


def literals(text):
    """The string literals of C/C++ source (raw strings included), comments dropped"""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("//", i):
            i = text.find("\n", i) if "\n" in text[i:] else n
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif text.startswith('R"', i):
            m = re.match(r'R"([^(]*)\(', text[i:])
            end = text.find(")" + m.group(1) + '"', i)
            out.append(text[i + len(m.group(0)):end])
            i = end + len(m.group(1)) + 2
        elif text[i] in "'\"":
            q, j = text[i], i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            if q == '"':
                out.append(text[i + 1:j])
            i = j + 1
        else:
            i += 1
    return out


def configured_words(values):
    """Each configured text value, and the folders' forms without the leading /"""
    words = set()
    for _sec, _key, _macro, typ, v in values:
        if typ != "int":
            words.add(v.strip())
            if typ == "path":
                words.add(v[1:] + "/")  # "apps/x.elf", a path relative to the root
    return words


def leftovers(files, values):
    words = configured_words(values)
    found = []
    for p in files:
        text = re.sub(r"^\s*#\s*include\b.*$", "", p.read_text(errors="replace"), flags=re.M)
        for lit in literals(text):
            for allowed in ALLOWED:
                lit = lit.replace(allowed, "")
            hits = [w for w in sorted(words) if re.search(r"(?<![A-Za-z0-9_])%s(?![A-Za-z0-9])" % re.escape(w), lit)]
            if hits:
                found.append("%s: %r has %s" % (rel(p), lit[:60], hits))
    return found


def redefined(files, values):
    found = []
    for p in files:
        text = p.read_text(errors="replace")
        for _s, _k, macro, _t, _v in values:
            if re.search(r"^\s*#\s*define\s+%s\b" % macro, text, re.M):
                found.append("%s defines %s" % (rel(p), macro))
    return found


def test_the_header_is_what_the_config_gives():
    r = subprocess.run([sys.executable, str(ROOT / "tools/gen_apps_config.py"), "--check"], capture_output=True,
                       text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def test_no_configured_value_is_left_in_our_sources():
    values = gen.load()
    assert leftovers(sources(), values) == []
    assert redefined(sources(), values) == []


def test_the_check_sees_a_literal_and_a_define(tmp_path):
    values = gen.load()
    bad = tmp_path / "bad.cpp"
    bad.write_text('// "Writer" in a comment is fine\nstatic const char *ssid = "Writer";\n'
                   '#define WS_LOCK_MS 5\nconst char *p = R"X(<title>Writer files</title>)X";\n'
                   'const char *d = "/apps/%s.elf";\n')
    found = leftovers([bad], values)
    assert len(found) == 3, found
    assert redefined([bad], values) == ["bad.cpp defines WS_LOCK_MS"]


def write(tmp_path, cfg):
    p = tmp_path / "apps.json"
    p.write_text(json.dumps(cfg))
    return p


def test_every_key_is_required(tmp_path):
    cfg = json.loads(gen.CONFIG.read_text())
    for sec, key, _m, _t, _l in gen.KEYS:
        c = copy.deepcopy(cfg)
        del c[sec][key]
        with pytest.raises(ValueError, match="missing key %s.%s" % (sec, key)):
            gen.load(write(tmp_path, c))


def test_unknown_keys_and_bad_values_are_refused(tmp_path):
    cfg = json.loads(gen.CONFIG.read_text())
    for edit, msg in [
        (lambda c: c["wifi_page"].update(ap_sid="x"), "unknown key"),
        (lambda c: c.update(extra={}), "unknown section"),
        (lambda c: c["wifi_page"].update(idle_s="900"), "whole number"),
        (lambda c: c["wifi_page"].update(ap_ssid="x" * 33), "printable ASCII"),
        (lambda c: c["wifi_page"].update(page_title='a"b'), "no quote"),
        (lambda c: c["wifi_page"].update(host_name="My Host"), "host name"),
        (lambda c: c["apps"].update(dir="apps"), "one folder"),
        (lambda c: c["usb_drive"].update(volume_label="lower"), "FAT label"),
        (lambda c: c["usb_drive"].update(scsi_vendor="NINECHARS"), "printable ASCII"),
    ]:
        c = copy.deepcopy(cfg)
        edit(c)
        with pytest.raises(ValueError, match=msg):
            gen.load(write(tmp_path, c))
