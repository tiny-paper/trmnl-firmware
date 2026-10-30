"""The app loader on the simulated X: started from the menu, ended by a long press, refused when broken,
resumed after a restart, and never resumed into a crash loop (docs/apps.md)."""
import re
import struct

MAIN_MENU = ["Next Screen", "Previous Screen", "Touchbar Tutorial", "Apps...", "Special...", "Settings...", "Close"]


def since(sim):
    return sim.console_total()


def must_log(sim, regex, start, timeout=60):
    line = sim.wait_log(regex, since=start, timeout=timeout)
    assert line, "no log line /%s/ after line %d:\n%s" % (regex, start, sim.log_tail())
    return line


def count_logs(sim, regex):
    lines = sim.api("GET", "/console?since=0")["lines"]
    return sum(1 for line in lines if re.search(regex, line["text"]))


CRASHED = r"Guru Meditation|rst:0x(3|c) "  # a panic, a software restart


def asleep(sim, timeout=120):
    r = sim.wait(timeout, state="deep_sleep")
    assert r.get("ok"), sim.log_tail()


def idle(sim):
    """The panel done with its update: a tap during one can be lost"""
    sim.wait(30, display_idle=True, settle_ms=300)


def tap_until(sim, zone, regex):
    """Taps `zone` until `regex` is logged: the sim's touch bar sometimes drops a tap right after a menu update.
    A tap that only shows late must not be repeated (a second right tap moves one item too far): before tapping
    again, wait for the panel to be idle and give the first tap that long again."""
    start = since(sim)
    for _ in range(3):
        idle(sim)
        sim.touch(zone)
        if sim.wait_log(regex, since=start, timeout=5):
            return
        idle(sim)
        if sim.wait_log(regex, since=start, timeout=5):  # 10 s in all: the menu closes after 15 s
            return


def choose(sim, labels, label):
    """On a menu screen showing `labels`, arrow on the first: moves right to `label` and selects it. Follows the
    arrow by the menu's own log lines, so a tap that only shows late (and then counts twice) is corrected by going
    on around the menu instead of failing; a dropped tap is tapped again after 5 s (the menu closes after 15 s)."""
    current, pos = labels[0], since(sim)
    for _ in range(3 * len(labels)):
        if current == label:
            break
        idle(sim)
        sim.touch("right")
        r = sim.wait(5, console=r"Menu: .* -> ", since=pos)
        while r.get("ok"):  # every move logged so far, late ones included
            current = r["line"]["text"].split(" -> ")[-1].strip()
            pos = r["line"]["i"] + 1
            r = sim.wait(0.3, console=r"Menu: .* -> ", since=pos)
    assert current == label, "menu arrow on %r, not %r:\n%s" % (current, label, sim.log_tail())
    start = since(sim)
    tap_until(sim, "center", r"Menu: chose")
    must_log(sim, r"Menu: chose %s" % re.escape(label), start, 5)


def start_app(sim, apps, name):
    """From sleep: middle tap, Apps..., the app. `apps`: the names in the menu, sorted."""
    asleep(sim)
    sim.api("POST", "/turbo", {"on": False})  # the menu's 15 s timeout runs in virtual time
    start = since(sim)
    sim.touch("center")
    must_log(sim, r"menu_show_screen end", start)
    choose(sim, MAIN_MENU, "Apps...")
    must_log(sim, r"menu_show_screen end", start)
    choose(sim, apps_menu(apps), name)
    return start


def apps_menu(apps):
    """The Apps submenu's items with `apps` in /apps"""
    return sorted(apps) + ["Receive over USB...", "Install via Wi-Fi", "Back"]


def long_press(sim):
    sim.touch("center", 2000)


def test_hello_starts_from_the_menu_and_a_long_press_returns_to_trmnl(device, elfs):
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "hello")
    must_log(sim, r"Apps: Hello \(/apps/hello.elf\) running", start)
    must_log(sim, r"app hello: hello: run 1", start)
    t = since(sim)
    sim.touch("left")
    must_log(sim, r"hello: touch left tap", t)
    t = since(sim)
    sim.api("POST", "/serial", b"a\x1b[A")
    must_log(sim, r"hello: key down 'a' code 512", t)
    must_log(sim, r"hello: key down code 256", t)
    t = since(sim)
    long_press(sim)
    must_log(sim, r"Apps: long press - asking hello to exit", t)
    must_log(sim, r"hello: exit requested", t)
    must_log(sim, r"Apps: hello ended with 0", t)
    asleep(sim)
    assert count_logs(sim, CRASHED) == 0
    assert sim.prefs().get("app_resume") == "0"


def damaged(elf):
    """Broken copies of a good app"""
    wrong_arch = bytearray(elf)
    wrong_arch[18:20] = struct.pack("<H", 243)  # RISC-V
    new_api = bytearray(elf)
    i = new_api.find(struct.pack("<I", 0x50504154))  # app_info's magic
    new_api[i + 4:i + 8] = struct.pack("<I", 99)
    return {
        "arch": (bytes(wrong_arch), "built for another chip"),
        "newapi": (bytes(new_api), "needs a newer firmware"),
        "notelf": (b"this is not an app\n", r"not an app \(no ELF header\)"),
        "trunc": (elf[: len(elf) // 2], "file is truncated"),
    }


def test_broken_apps_give_a_clean_error(device, elfs):
    bad = damaged(elfs["hello"])
    files = {"/apps/%s.elf" % name: data for name, (data, _) in bad.items()}
    files["/apps/huge.elf"] = elfs["huge"]
    files["/apps/fail.elf"] = elfs["fail"]
    expect = {name: text for name, (_, text) in bad.items()}
    expect["huge"] = "too big for free memory"
    expect["fail"] = "ended with error 7"
    names = sorted(n[6:-4] for n in files)
    sim = device(files)
    for name in names:
        start = start_app(sim, names, name)
        must_log(sim, r"Apps: %s: %s" % (name, expect[name]), start)
        asleep(sim)
    assert count_logs(sim, CRASHED) == 0, "a broken app crashed or restarted the device"
    assert sim.prefs().get("app_resume") == "0"
    assert count_logs(sim, r"Apps: fail ended with 7") == 1
    assert count_logs(sim, r"Apps: .* running") == 1  # only fail got that far


def test_an_app_that_ignores_the_long_press_is_ended_by_a_restart(device, elfs):
    sim = device({"/apps/stuck.elf": elfs["stuck"]})
    start = start_app(sim, ["stuck"], "stuck")
    must_log(sim, r"stuck: running", start)
    t = since(sim)
    long_press(sim)
    must_log(sim, r"stuck: ignoring the exit request", t)
    must_log(sim, r"Apps: stuck did not exit within 2000 ms - restarting into TRMNL", t)
    must_log(sim, r"rst:0xc ", t)
    asleep(sim, 300)
    assert count_logs(sim, r"Apps: resuming") == 0
    assert count_logs(sim, r"stuck: running") == 1


def test_a_running_app_is_resumed_after_a_restart(device, elfs):
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "hello")
    must_log(sim, r"app hello: hello: run 1", start)
    t = since(sim)
    sim.api("POST", "/serial", b"a")  # input reached it: the user could leave it, so it may be resumed
    must_log(sim, r"hello: key down 'a'", t)
    r = sim.wait(30, console=r"never", since=start)  # let it run past APP_RESUME_STARTED_MS (5 s)
    assert not r.get("ok")
    assert sim.prefs().get("app_resume") == "1", sim.prefs()
    t = since(sim)
    sim.api("POST", "/reset")
    must_log(sim, r"Apps: resuming /apps/hello.elf", t, 120)
    must_log(sim, r"app hello: hello: run 2", t)
    t = since(sim)
    long_press(sim)
    must_log(sim, r"Apps: hello ended with 0", t)
    asleep(sim)
    assert sim.prefs().get("app_resume") == "0"


def test_an_app_that_crashes_as_it_starts_is_not_started_again(device, elfs):
    sim = device({"/apps/crash.elf": elfs["crash"]})
    # As if it had been running fine before a restart: due for resuming
    asleep(sim)
    sim.set_pref("app_last", "string", "/apps/crash.elf")
    sim.set_pref("app_resume", "u8", "1")
    t = since(sim)
    sim.api("POST", "/reset")
    must_log(sim, r"Apps: resuming /apps/crash.elf", t, 120)
    must_log(sim, r"crash: crashing now", t)
    must_log(sim, r"Guru Meditation", t)
    must_log(sim, r"Apps: reset by a crash", t, 120)
    asleep(sim, 300)
    assert count_logs(sim, r"crash: crashing now") == 1, "crash loop"
    assert count_logs(sim, r"Apps: resuming") == 1
    # From the menu, the same: one crash, then TRMNL
    start = start_app(sim, ["crash"], "crash")
    must_log(sim, r"crash: crashing now", start)
    asleep(sim, 300)
    assert count_logs(sim, r"crash: crashing now") == 2
    assert count_logs(sim, r"Apps: resuming") == 1


def test_a_missing_app_to_resume_gives_a_clean_error(device, elfs):
    sim = device({"/apps/hello.elf": elfs["hello"]})
    asleep(sim)
    sim.set_pref("app_last", "string", "/apps/gone.elf")
    sim.set_pref("app_resume", "u8", "1")
    t = since(sim)
    sim.api("POST", "/reset")
    must_log(sim, r"Apps: gone: file not found", t, 120)
    asleep(sim)
    assert sim.prefs().get("app_resume") == "0"
