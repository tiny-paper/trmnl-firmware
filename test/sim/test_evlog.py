"""The event log on the simulated X (docs/apps.md "Event log"): TAPP EVLOG lists the events oldest first and survives
a restart; TAPP EVLOG CLEAR empties it."""
import re

from test_apps import keys, long_press, must_log, since, start_app


def evlog(sim):
    """The lines of one TAPP EVLOG, in order"""
    t = since(sim)
    sim.api("POST", "/serial", b"TAPP EVLOG\n")
    must_log(sim, r"TAPP EVLOG END", t)
    lines = sim.api("GET", "/console?since=%d" % t)["lines"]
    out = []
    for line in lines:
        if "TAPP EVLOG END" in line["text"]:
            break
        m = re.search(r"TAPP EVLOG (#\d+ .*)", line["text"])
        if m:
            out.append(m.group(1))
    return out


def boot_of(line):
    return int(re.match(r"#(\d+) ", line).group(1))


def test_the_event_log_lists_events_across_a_restart_and_clears(device, elfs):
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "hello")
    must_log(sim, r"app hello: hello: run 1", start)
    first = evlog(sim)
    starts = [x for x in first if "Apps: starting /apps/hello.elf" in x]
    assert starts, first
    assert re.match(r"#\d+ \d+\.\d{3}s (-|\d{4}-\d\d-\d\d \d\d:\d\d:\d\dZ) Apps: starting", starts[-1]), starts

    t = since(sim)
    keys(sim, b"a")  # input reached it: resumed after the restart
    must_log(sim, r"hello: key down 'a'", t)
    assert not sim.wait(17, console=r"never", since=t).get("ok")  # past the write gap and APP_RESUME_STARTED_MS
    t = since(sim)
    sim.api("POST", "/reset")
    must_log(sim, r"Apps: resuming /apps/hello.elf", t, 120)
    must_log(sim, r"app hello: hello: run 2", t)
    after = evlog(sim)
    old = [x for x in after if "Apps: starting /apps/hello.elf" in x]
    assert old and boot_of(old[0]) == boot_of(starts[-1]), after  # the event from before the restart is kept
    resumed = [x for x in after if "Apps: resuming /apps/hello.elf" in x]
    assert resumed and boot_of(resumed[-1]) == boot_of(starts[-1]) + 1, after
    assert after.index(old[0]) < after.index(resumed[-1]), after  # oldest first

    t = since(sim)
    sim.api("POST", "/serial", b"TAPP EVLOG CLEAR\n")
    must_log(sim, r"TAPP EVLOG CLEARED", t)
    assert evlog(sim) == []


def test_a_crash_in_an_app_is_logged_with_its_pc_after_the_reset(device, elfs):
    """Device: the writer went back to TRMNL by itself after a BLE disconnect, and nothing said why. A panic in an app
    leaves its record in RTC memory: the next boot logs exception, cause, PC and the PC's offset into the app (for
    tools/app_addr2line.py), after the events before it."""
    sim = device({"/apps/crash.elf": elfs["crash"], "/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["crash", "hello"], "crash")
    must_log(sim, r"crash: crashing now", start)
    entry = re.search(r"entry at (0x[0-9a-f]+)", must_log(sim, r"Apps: crash entry at", start)).group(1)
    must_log(sim, r"Guru Meditation", start)
    crash_line = must_log(sim, r"Event log: crash: ", start, 120)
    assert re.search(r"crash: StoreProhibited at app\+0x[0-9a-f]+", crash_line), crash_line
    assert "app crash entry %s" % entry in crash_line, crash_line
    start = start_app(sim, ["crash", "hello"], "hello")
    must_log(sim, r"app hello: hello: run 1", start)
    log = evlog(sim)
    i_start = max(i for i, x in enumerate(log) if "Apps: starting /apps/crash.elf" in x)
    i_crash = max(i for i, x in enumerate(log) if "crash: StoreProhibited at app+0x" in x)
    i_boot = max(i for i, x in enumerate(log) if "boot: crash (panic)" in x)
    assert i_start < i_crash < i_boot, log
    assert boot_of(log[i_crash]) == boot_of(log[i_start]) and boot_of(log[i_boot]) == boot_of(log[i_crash]) + 1, log


def test_app_ends_say_why(device, elfs):
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "hello")
    must_log(sim, r"app hello: hello: run 1", start)
    t = since(sim)
    long_press(sim)
    must_log(sim, r"Apps: hello ended with 0 \(after a long press\)", t)
    start = start_app(sim, ["hello"], "hello")  # back from TRMNL: again, and one that fails
    must_log(sim, r"app hello: hello: run", start)
    assert any("Apps: hello ended with 0 (after a long press)" in x for x in evlog(sim))


def test_an_app_error_end_says_error(device, elfs):
    sim = device({"/apps/fail.elf": elfs["fail"]})
    start = start_app(sim, ["fail"], "fail")
    must_log(sim, r"Apps: fail ended with 7 \(error\)", start)
