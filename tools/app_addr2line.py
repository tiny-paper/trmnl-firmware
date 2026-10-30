#!/usr/bin/env python3
"""Decodes an app's frames in a panic backtrace of the X (docs/apps.md "Crash in an app").

  tools/app_addr2line.py <app.elf with symbols> <log file>
The log needs "Apps: <name> entry at 0x..." (the runner prints it) and a "Backtrace:" line. The app is loaded
at entry - e_entry; frames outside the app are printed as they are (decode them with the firmware's ELF).
Build the app once more without stripping (same flags, no --strip-*, plus -g) to get names and lines."""
import re
import struct
import subprocess
import sys
from pathlib import Path

ADDR2LINE = Path.home() / ".platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line"


def e_entry(elf):
    data = Path(elf).read_bytes()[:28]
    assert data[:4] == b"\x7fELF", "not an ELF file"
    return struct.unpack_from("<I", data, 24)[0]


def main(elf, log):
    text = Path(log).read_text(errors="replace")
    m = re.findall(r"Apps: \S+ entry at 0x([0-9a-fA-F]+)", text)
    bt = re.findall(r"Backtrace:(.*)", text)
    if not m or not bt:
        sys.exit("need an 'Apps: <name> entry at 0x...' line and a 'Backtrace:' line in %s" % log)
    base = int(m[-1], 16) - e_entry(elf)
    size = Path(elf).stat().st_size
    for pc in re.findall(r"0x([0-9a-fA-F]{8}):0x", bt[-1]):
        addr = int(pc, 16)
        off = addr - base
        if 0 <= off < size:
            out = subprocess.run([str(ADDR2LINE), "-pfiaC", "-e", elf, hex(off)], capture_output=True, text=True)
            print("app  0x%08x  %s" % (addr, out.stdout.strip()))
        else:
            print("fw   0x%08x" % addr)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
