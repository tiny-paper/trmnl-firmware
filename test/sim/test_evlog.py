"""The event log on the simulated X (docs/apps.md "Event log"): TAPP EVLOG lists the events oldest first and survives
a restart; TAPP EVLOG CLEAR empties it."""
import re

from test_apps import keys, must_log, since, start_app


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
