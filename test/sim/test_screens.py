"""Ways out on the simulated X without a working app: every firmware screen ends on a long press, an app that no
input reached is not resumed after a restart, and a serial command clears the resume (docs/apps.md).

The touch bar is driven through the sim's IQS323 (POST /touch: its status registers and RDY windows), the path the
hardware uses. The presses come right after the screen or app starts, without waiting for the panel: the middle tap
that chose it was read by the menu's last mode switch and never published, so on 68248ca the next middle press
showed no start and was swallowed (no tap, no long press: the owner's dead touch bar in apps)."""
from test_apps import CRASHED, apps_menu, asleep, choose, count_logs, idle, keys, long_press, MAIN_MENU, must_log, \
    since, start_app


def test_install_via_wifi_ends_on_a_long_press(device, elfs):
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "Install via Wi-Fi")
    must_log(sim, r"Apps: install screen shown", start, 120)  # its touch loop runs from here
    idle(sim)
    t = since(sim)
    long_press(sim)
    must_log(sim, r"Touch: iqs323 ch1 long press .* -> Install via Wi-Fi", t)
    asleep(sim)  # back to TRMNL: its image and sleep
    assert count_logs(sim, CRASHED) == 0


def test_install_via_wifi_shows_a_big_code_inside_the_panel(device, elfs):
    """The code and address must read from a distance (owner 2026-10-30) and nothing may run past the panel edge"""
    import io
    import os
    from PIL import Image
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "Install via Wi-Fi")
    must_log(sim, r"Apps: install screen shown", start, 120)
    idle(sim)
    png = sim.api("GET", "/screenshot")
    shots = os.environ.get("INSTALL_SHOTS")
    if shots:
        os.makedirs(shots, exist_ok=True)
        with open(os.path.join(shots, "install-screen.png"), "wb") as f:
            f.write(png)
    img = Image.open(io.BytesIO(png)).convert("L")
    w, h = img.size
    ink = img.point(lambda v: 255 if v < 128 else 0)
    box = ink.getbbox()
    assert box, "blank screen"
    margin = 16
    assert box[0] >= margin and box[1] >= margin and box[2] <= w - margin and box[3] <= h - margin, box
    # the widest ink-free-band rows: the code is the tallest text line, at least 3x the old 18 px text
    rows = [any(ink.getpixel((x, y)) for x in range(0, w, 2)) for y in range(h)]
    tallest = run = 0
    for r in rows:
        run = run + 1 if r else 0
        tallest = max(tallest, run)
    assert tallest >= 54, "tallest ink band %d px" % tallest


def test_install_via_wifi_ends_on_a_long_press_at_once(device, elfs):
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "Install via Wi-Fi")
    must_log(sim, r"Apps: install screen shown", start, 120)
    t = since(sim)
    long_press(sim)
    must_log(sim, r"Touch: iqs323 ch1 long press .* -> Install via Wi-Fi", t)
    asleep(sim)


def test_a_middle_tap_reaches_an_app_right_after_it_started(device, elfs):
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "hello")
    must_log(sim, r"Apps: .* running", start)
    t = since(sim)
    sim.touch("center")
    must_log(sim, r"hello: touch middle tap", t)


def test_a_running_app_ends_on_a_long_press_at_once(device, elfs):
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "hello")
    must_log(sim, r"Apps: .* running", start)
    t = since(sim)
    long_press(sim)
    must_log(sim, r"Apps: hello ended with 0", t)
    asleep(sim)


def test_receive_over_usb_ends_on_a_long_press(device, elfs):
    sim = device({"/apps/hello.elf": elfs["hello"]})
    asleep(sim)
    sim.api("POST", "/turbo", {"on": False})
    start = since(sim)
    sim.touch("center")
    must_log(sim, r"menu_show_screen end", start)
    choose(sim, MAIN_MENU, "Apps...")
    must_log(sim, r"menu_show_screen end", start)
    choose(sim, apps_menu(["hello"]), "Receive over USB...")
    must_log(sim, r"Apps: waiting for an upload on the USB serial port", start)
    t = since(sim)
    long_press(sim)
    must_log(sim, r"Apps: upload mode ended", t)


def test_an_app_no_input_reached_is_not_resumed(device, elfs):
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "hello")
    must_log(sim, r"app hello: hello: run 1", start)
    assert not sim.wait(8, console=r"never", since=start).get("ok")  # past APP_RESUME_STARTED_MS, no input
    assert sim.prefs().get("app_resume") == "1", sim.prefs()
    t = since(sim)
    sim.api("POST", "/reset")
    must_log(sim, r"Apps: not resuming /apps/hello.elf: no touch or key reached it", t, 120)
    asleep(sim)
    assert count_logs(sim, r"Apps: resuming") == 0
    assert count_logs(sim, r"hello: run 2") == 0
    assert sim.prefs().get("app_resume") == "0"


def test_a_serial_command_clears_the_resume(device, elfs):
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "hello")
    must_log(sim, r"app hello: hello: run 1", start)
    t = since(sim)
    keys(sim, b"x")  # input reached it: due for resuming
    must_log(sim, r"hello: key down 'x'", t)
    assert not sim.wait(8, console=r"never", since=start).get("ok")
    assert sim.prefs().get("app_resume") == "1", sim.prefs()
    t = since(sim)
    sim.api("POST", "/serial", b"TAPP NORESUME\n")
    must_log(sim, r"Apps: resume cleared by serial command", t)
    asleep(sim, 180)
    assert count_logs(sim, r"Apps: resuming") == 0
    assert sim.prefs().get("app_resume") == "0"


def test_a_touch_bar_reset_while_an_app_runs_is_recovered(device, elfs):
    """Device log, 68248ca with the writer: after one IQS323 reset the task re-initialised it several times a second
    for good ("Unexpected reset detected" / "Reinit successful" / "INIT_VERIFY_PRODUCT"), so no touch arrived and the
    app could not be left. The stale "new data" flag ended each init after its first step, before the reset was
    acknowledged."""
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "hello")
    must_log(sim, r"Apps: .* running", start)
    t = since(sim)
    sim.api("POST", "/faults", {"touch_bar": "reset"})
    must_log(sim, r"IQS323 Task: Unexpected reset detected", t)
    must_log(sim, r"IQS323_INIT_DONE", t)
    sim.wait(3, console=r"never", since=t)
    assert count_logs(sim, r"IQS323 Task: Unexpected reset detected") <= 2, "reinit loop"
    t = since(sim)
    long_press(sim)
    must_log(sim, r"Apps: hello ended with 0", t)


def test_serial_lines_never_reach_an_app_as_keys(device, elfs):
    """Device, writer on b0ff008: "TAPP EVLOG" sent to read the event log was typed into the document. Only a
    "TKEY " line types; TAPP commands and any other bytes do not."""
    sim = device({"/apps/hello.elf": elfs["hello"]})
    start = start_app(sim, ["hello"], "hello")
    must_log(sim, r"app hello: hello: run 1", start)
    t = since(sim)
    sim.api("POST", "/serial", b"TAPP EVLOG\n")
    must_log(sim, r"TAPP EVLOG END", t)
    sim.api("POST", "/serial", b"TAPP NOSUCH\nzq\r\n")
    keys(sim, b"a\x1b[A")
    must_log(sim, r"hello: key down 'a'", t)
    must_log(sim, r"hello: key down code 256", t)  # the arrow up of the escape sequence
    assert not sim.wait(3, console=r"hello: key down '[TAPEVLOGNSUCHzqK ]'", since=t).get("ok"), sim.log_tail()
