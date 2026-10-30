#!/usr/bin/env python3
"""Guard: the FastEPD the TRMNL_X env pins leaves rows outside a partial update's [y0,y1] undriven.

The app API's partial refresh (src/app_api_table.cpp) hands the library only the changed rows. Upstream 9a84c35
sent every second skipped row the last data row again (the second DMA row buffer was never cleared), on every pass,
also above the range from the second pass on: earlier text lines got the new line's white pushes and faded.
Fixed upstream in a5aa810 (tiny-paper/FastEPD 6c04c50, "iSkipped < 2").

bbepPartialUpdate and the 1 bit lookup tables are cut from src/FastEPD.inl and compiled on the host over stubs
(fastepd_partial_harness.cpp, from tiny-paper tools/panel): no timing and nothing about the glass.

Source: $FASTEPD_SRC (a FastEPD checkout or its src directory), else the commit platformio.ini pins for TRMNL_X,
fetched once into .pio/fastepd/<commit>. Not fetchable: exit 2, loudly (never a silent pass).

    python3 tools/fastepd/test_fastepd_partial.py      (or: pytest tools/fastepd)
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INI = ROOT / "platformio.ini"
HARNESS = Path(__file__).with_name("fastepd_partial_harness.cpp")
START = "static int IRAM_ATTR bbepPartialUpdate("
END = "} /* bbepPartialUpdate() */"
LUT_START = "    // Create the lookup tables for 1-bit mode"
LUT_END = "    } // for i\nreturn BBEP_SUCCESS;"
CASES = "0 of 16 cases failed"


def pinned(env="TRMNL_X"):
    """(url, sha) of FastEPD in [env:<env>] lib_deps."""
    text = INI.read_text()
    sect = re.search(rf"^\[env:{env}\]\n(.*?)(?=^\[)", text, re.M | re.S).group(1)
    m = re.search(r"^\s*(https://\S+/FastEPD\.git)#([0-9a-f]{40})\s*$", sect, re.M)
    return m.group(1), m.group(2)


def source():
    """The library's src directory, or None when it cannot be had."""
    env = os.environ.get("FASTEPD_SRC")
    if env:
        p = Path(env)
        return p if (p / "FastEPD.inl").exists() else p / "src"
    url, sha = pinned()
    d = ROOT / ".pio/fastepd" / sha
    if not (d / "src/FastEPD.inl").exists():
        tmp = d.with_name(f"{sha}.{os.getpid()}")
        shutil.rmtree(tmp, ignore_errors=True)
        tmp.mkdir(parents=True)
        try:
            for cmd in (["init", "-q"], ["fetch", "-q", "--depth", "1", url, sha], ["checkout", "-q", "FETCH_HEAD"]):
                subprocess.run(["git", "-C", str(tmp), *cmd], check=True, capture_output=True, timeout=120,
                               env={**os.environ, "GIT_TERMINAL_PROMPT": "0"})
            shutil.rmtree(d, ignore_errors=True)
            tmp.rename(d)
        except (subprocess.SubprocessError, OSError) as e:
            shutil.rmtree(tmp, ignore_errors=True)
            print(f"NOT CHECKED: FastEPD {sha[:7]} could not be fetched from {url} ({e})", file=sys.stderr)
            return None
    return d / "src"


def cut(inl):
    """(partial.inc, luts.inc) from FastEPD.inl."""
    a = inl.index(START)
    partial = inl[a:inl.index(END, a) + len(END)] + "\n"
    b = inl.index(LUT_START)
    luts = inl[b:inl.index(LUT_END, b) + len("    } // for i\n")]
    return partial, luts


def run(src):
    """(exit code, output) of the harness over the library in src."""
    partial, luts = cut((src / "FastEPD.inl").read_text())
    with tempfile.TemporaryDirectory() as t:
        Path(t, "partial.inc").write_text(partial)
        Path(t, "luts.inc").write_text(luts)
        exe = Path(t, "harness")
        cc = subprocess.run(["g++", "-std=c++17", "-O1", "-w", f"-I{t}", f"-I{src}", str(HARNESS), "-o", str(exe)],
                            capture_output=True, text=True)
        if cc.returncode:
            return cc.returncode, cc.stderr[-2000:]
        r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
        return r.returncode, r.stdout


def test_pinned_partial_leaves_rows_outside_undriven():
    src = source()
    assert src is not None, "FastEPD source not available"
    rc, out = run(src)
    assert rc == 0 and CASES in out, out


def main():
    src = source()
    if src is None:
        return 2
    rc, out = run(src)
    print(out if rc or CASES not in out else out.splitlines()[-1] + f" (FastEPD partial update, {src})")
    return 1 if rc or CASES not in out else 0


if __name__ == "__main__":
    sys.exit(main())
