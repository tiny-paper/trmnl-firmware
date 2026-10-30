"""Apps copied to /apps over the USB serial port (apps/upload.py, app_upload.h) on the simulated X: the rest of
the file system stays, a broken or cut-off upload keeps the old app and leaves no temporary file."""
import os
import re
import zlib
import sys

import littlefs

import flashfs
from test_apps import CRASHED, MAIN_MENU, apps_menu, asleep, choose, count_logs, idle, long_press, must_log, since

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "apps"))
import upload  # noqa: E402

KEEP = b"a cached screen, not ours to touch\n" * 100


class SimPort:
    """upload.upload()'s write and read_reply on the sim's USB serial"""

    def __init__(self, sim):
        self.sim = sim
        self.pos = since(sim)

    def write(self, data):
        self.sim.api("POST", "/serial", data)

    def read_reply(self, timeout_s):
        r = self.sim.wait(timeout_s, console=r"TAPP (READY|ACK|OK|ERR)", since=self.pos)
        if not r.get("ok"):
            return None
        self.pos = r["line"]["i"] + 1
        text = r["line"]["text"]
        return text[text.index("TAPP "):].strip()


def receive_mode(sim, apps):
    """From sleep: middle tap, Apps..., Receive over USB..."""
    asleep(sim)
    sim.api("POST", "/turbo", {"on": False})
    start = since(sim)
    sim.touch("center")
    must_log(sim, r"menu_show_screen end", start)
    choose(sim, MAIN_MENU, "Apps...")
    must_log(sim, r"menu_show_screen end", start)
    choose(sim, apps_menu(apps), "Receive over USB...")
    must_log(sim, r"Apps: waiting for an upload on the USB serial port", start)
    return SimPort(sim)


def stop_receiving(sim):
    t = since(sim)
    idle(sim)
    sim.touch("center")
    must_log(sim, r"Apps: upload mode ended", t)
    must_log(sim, r"menu_show_screen end", t)


def back_to_sleep(sim):
    """Ends the app; the sim saves its flash file when the device sleeps"""
    t = since(sim)
    # a touch right after the one that chose the app reads as that one, held: let the bar see it up first
    sim.wait(1, console=r"never", since=t)
    idle(sim)
    long_press(sim)
    must_log(sim, r"Apps: hello ended with 0", t)
    asleep(sim)
    assert count_logs(sim, CRASHED) == 0
    sim.stop()


def files_on_flash(flash_path):
    """{path: bytes} of the X's LittleFS in a sim flash file"""
    raw = open(flash_path, "rb").read()[flashfs.FS_OFFSET:flashfs.FS_OFFSET + flashfs.FS_SIZE]
    fs = littlefs.LittleFS(block_size=flashfs.BLOCK, block_count=len(raw) // flashfs.BLOCK, mount=False)
    fs.context.buffer = bytearray(raw)
    fs.mount()
    out = {}
    for root, _dirs, names in fs.walk("/"):
        for n in names:
            p = root.rstrip("/") + "/" + n
            with fs.open(p, "rb") as f:
                out[p] = f.read()
    return out


def test_noise_and_a_partial_line_before_the_header_are_skipped(device, elfs):
    """Device 2026-10-30: `TAPP ERR bad header` for a good header; bytes were on the port before it"""
    sim = device({})
    port = receive_mode(sim, [])
    port.write(b"\x00\xff~~ boot noise\r\nrst:0x1 (POWERON)\n")  # whole noise lines: no reply at all
    port.write(b"x" * 300)  # an over-long partial line (more than the line buffer), no newline
    port.write(b"TAPP PU")  # a partial header, abandoned by a newline: the host starts over
    port.write(b"\r\n")
    assert port.read_reply(3) == "TAPP ERR bad header: got 54 41 50 50 20 50 55"
    port.write(b"zz" + b"y" * 200)  # noise glued to the front of the real header, CR LF line end
    data = elfs["hello"]
    port.write(b"TAPP PUT hello.elf %d %08x\r\n" % (len(data), zlib.crc32(data)))
    assert port.read_reply(10) == "TAPP READY"
    for i in range(0, len(data), upload.BLOCK):
        port.write(data[i:i + upload.BLOCK])
        if i + upload.BLOCK < len(data):
            assert port.read_reply(10) == "TAPP ACK %d" % (i + upload.BLOCK)
    assert port.read_reply(10) == "TAPP OK hello.elf"
    must_log(sim, r'Apps: ignored a line without "TAPP "', 0)
    stop_receiving(sim)


def test_an_app_sent_over_usb_runs_and_the_rest_of_the_file_system_stays(device, elfs):
    sim = device({"/keep/screen.bin": KEEP})
    port = receive_mode(sim, [])
    upload.upload("hello.elf", elfs["hello"], port.write, port.read_reply)
    big = elfs["keys"]  # many blocks
    upload.upload("big.elf", big, port.write, port.read_reply)
    must_log(sim, r"Apps: received /apps/big.elf, %d bytes" % len(big), 0)
    stop_receiving(sim)
    # the Apps submenu lists it now: start it from there
    choose(sim, apps_menu(["big", "hello"]), "hello")
    must_log(sim, r"app hello: hello: run 1", 0)
    back_to_sleep(sim)
    files = files_on_flash(sim.flash)
    assert files.get("/keep/screen.bin") == KEEP, "the upload touched a file that is not an app"
    assert files.get("/apps/hello.elf") == elfs["hello"]
    assert files.get("/apps/big.elf") == big
    assert "/apps/.upload.tmp" not in files


def test_a_bad_or_cut_off_upload_keeps_the_old_app(device, elfs):
    old = elfs["hello"]
    sim = device({"/apps/hello.elf": old})
    port = receive_mode(sim, ["hello"])
    other = os.urandom(3000)
    # wrong checksum: refused after the data
    bad = bytearray(other)
    port.write(b"TAPP PUT hello.elf %d %08x\n" % (len(other), 0x12345678))
    assert port.read_reply(10) == "TAPP READY"
    for i in range(0, len(bad), upload.BLOCK):
        port.write(bytes(bad[i:i + upload.BLOCK]))
        assert re.match(r"TAPP (ACK \d+|ERR bad crc)$", port.read_reply(10) or "")
    # cut off: half the data, then nothing (the cable pulled)
    port.write(b"TAPP PUT hello.elf %d 0\n" % len(other))
    assert port.read_reply(10) == "TAPP READY"
    port.write(other[:upload.BLOCK])
    assert port.read_reply(10) == "TAPP ACK %d" % upload.BLOCK
    assert port.read_reply(30) == "TAPP ERR short"
    # not an app: the same ELF check as Install via Wi-Fi (app_install_commit) refuses it after the data
    port.write(b"TAPP PUT hello.elf %d %08x\n" % (len(other), zlib.crc32(other)))
    assert port.read_reply(10) == "TAPP READY"
    for i in range(0, len(other), upload.BLOCK):
        port.write(other[i:i + upload.BLOCK])
        reply = port.read_reply(10) or ""
        assert re.match(r"TAPP (ACK \d+|ERR refused: not an app.*)$", reply), reply
    assert reply.startswith("TAPP ERR refused: not an app"), reply
    # refused headers
    for header, why in [(b"TAPP PUT ../x.elf 10 0", "bad name"), (b"TAPP PUT hello.elf 0 0", "empty"),
                        (b"TAPP PUT hello.elf 2097153 0", "too big"), (b"TAPP PUT hello.elf", "bad name"),
                        (b"garbage", "bad header")]:
        port.write(header + b"\n")
        assert (port.read_reply(10) or "").startswith("TAPP ERR " + why), header
    # still answers a good one afterwards
    upload.upload("second.elf", elfs["hello"], port.write, port.read_reply)
    stop_receiving(sim)
    choose(sim, apps_menu(["hello", "second"]), "hello")
    must_log(sim, r"app hello: hello: run 1", 0)
    back_to_sleep(sim)
    files = files_on_flash(sim.flash)
    assert files.get("/apps/hello.elf") == old, "a failed upload changed the old app"
    assert "/apps/.upload.tmp" not in files
