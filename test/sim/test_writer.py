"""The writer (~/workspace/tiny-paper-write) on this firmware in the simulator: started from the Apps menu, typed
into over the serial key path, its text saved and back after a restart, F1 help, a long press back to TRMNL.

Needs writer.elf built against this firmware's include/app_api.h: $WRITER_ELF (default
build/writer/writer.elf). Skips when it is absent. Screenshots go to $WRITER_SHOTS when set."""
import os

import pytest

import flashfs
from test_apps import CRASHED, asleep, count_logs, idle, keys, long_press, must_log, since, start_app

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WRITER_ELF = os.environ.get("WRITER_ELF", os.path.join(ROOT, "build", "writer", "writer.elf"))
SHOTS = os.environ.get("WRITER_SHOTS")
TEXT = b"Integration test line one"

pytestmark = pytest.mark.skipif(not os.path.exists(WRITER_ELF), reason="no writer.elf at %s" % WRITER_ELF)


def shot(sim, name):
    png = sim.api("GET", "/screenshot")
    if SHOTS:
        os.makedirs(SHOTS, exist_ok=True)
        with open(os.path.join(SHOTS, "writer-%s.png" % name), "wb") as f:
            f.write(png)
    return png


def settle(sim, seconds):
    """Lets `seconds` of device time pass (the writer saves 3 s after the last key) and the panel finish"""
    t = since(sim)
    sim.wait(seconds, console=r"never matches", since=t)
    idle(sim)


def documents(flash):
    files = flashfs.read_files(flash, "/appdata/writer")
    return {p: d for p, d in files.items() if "/documents/" in p}


def test_writer_types_saves_shows_help_and_returns_to_trmnl(device):
    elf = open(WRITER_ELF, "rb").read()
    sim = device({"/apps/writer.elf": elf})
    start = start_app(sim, ["writer"], "writer")
    must_log(sim, r"Apps: .* \(/apps/writer.elf\) running", start)
    settle(sim, 5)
    shot(sim, "1-started")

    keys(sim, TEXT)
    settle(sim, 2)
    keys(sim, b"\x13")  # Ctrl+S: save now
    settle(sim, 6)
    typed = shot(sim, "2-typed")

    keys(sim, b"\x1bOP")  # F1
    settle(sim, 3)
    helped = shot(sim, "3-help")
    assert helped != typed, "F1 did not change the screen"
    keys(sim, b"\x1b")  # Esc: back to writing
    settle(sim, 3)

    # A restart while it runs: the writer is resumed and its text is still there
    settle(sim, 6)  # past APP_RESUME_STARTED_MS
    assert count_logs(sim, CRASHED) == 0, "the writer crashed or restarted the device"
    t = since(sim)
    sim.api("POST", "/reset")
    must_log(sim, r"Apps: resuming /apps/writer.elf", t, 120)
    settle(sim, 8)
    back = shot(sim, "4-after-restart")
    assert back != helped

    t = since(sim)
    long_press(sim)
    must_log(sim, r"Apps: writer ended with 0", t)
    asleep(sim)
    shot(sim, "5-trmnl")
    assert count_logs(sim, r"Guru Meditation") == 0, "the writer crashed"
    sim.stop()
    docs = documents(sim.flash)
    assert any(TEXT in d for d in docs.values()), "typed text not saved: %r" % {p: d[:80] for p, d in docs.items()}
