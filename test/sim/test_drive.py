"""The USB drive's firmware side on the simulated X (docs/apps.md, "USB drive"). The sim has no USB
OTG controller, so no computer: these cover the journal on the real LittleFS (an import cut by a power
loss is finished or dropped before the app reads its files) and a drive start that fails."""
from test_apps import CRASHED, asleep, must_log, start_app

APP = "/appdata/drivecheck/"
PLAN = b".drive/stage/0\tdocuments/n/a.md\nend\n"


def files(elfs, extra):
    out = {"/apps/drivecheck.elf": elfs["drivecheck"], APP + "documents/n/a.md": b"old\n"}
    out.update({APP + k: v for k, v in extra.items()})
    return out


def test_an_import_cut_after_its_commit_is_finished_before_the_app_reads(device, elfs):
    sim = device(files(elfs, {".drive/stage/0": b"new\n", ".drive/plan": PLAN}))
    start = start_app(sim, ["drivecheck"], "drivecheck")
    must_log(sim, r"drivecheck: a.md=new plan=0 stage=0 plantmp=0", start)


def test_an_import_cut_before_its_commit_is_dropped(device, elfs):
    sim = device(files(elfs, {".drive/stage/0": b"new\n", ".drive/plan.tmp": PLAN}))
    start = start_app(sim, ["drivecheck"], "drivecheck")
    must_log(sim, r"drivecheck: a.md=old plan=0 stage=0 plantmp=0", start)


def test_power_lost_while_finishing_an_import_finishes_it_next_time(device, elfs):
    sim = device(files(elfs, {".drive/stage/0": b"new\n", ".drive/plan": PLAN}))
    asleep(sim)
    # the first flash program in the file system after this point: the recovery's rename
    sim.api("POST", "/faults", {"power_loss": {"partition": "spiffs", "op": "program", "nth": 1, "cut": "torn"}})
    start = start_app(sim, ["drivecheck"], "drivecheck")
    assert sim.wait_log(r"\[sim\] power lost", since=start, timeout=120), sim.log_tail()
    start = start_app(sim, ["drivecheck"], "drivecheck")
    must_log(sim, r"drivecheck: a.md=new plan=0 stage=0 plantmp=0", start)


def test_a_drive_that_cannot_start_fails_cleanly(device, elfs):
    # The sim's S3 has no USB OTG controller, so TinyUSB's start fails: the app gets an error, the
    # serial port and its files stay usable, nothing crashes. (Shown, timeout and import: device check.)
    sim = device(files(elfs, {"drive-on": b"1"}))
    start = start_app(sim, ["drivecheck"], "drivecheck")
    must_log(sim, r"USB drive: drivecheck not shown \(-4\)", start)
    must_log(sim, r"drivecheck: start -4 state 0", start)
    must_log(sim, r"drivecheck: write while shown allowed", start)  # never shown
    must_log(sim, r"drivecheck: stop 0 ended_by 0 shown 1 taken 0", start)
    must_log(sim, r"Apps: drivecheck ended with 0", start)
    assert not sim.wait_log(CRASHED, since=start, timeout=1)
