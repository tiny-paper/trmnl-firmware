"""The USB port rules for keyboards on the simulated X (usb_port.h, docs/apps.md "Keyboards"): the firmware reads
the charger's input power on the right pin, keeps device mode while powered and for the first second on battery.
The sim has no USB-OTG controller (usb_host_install asserts on its ID register) and no keyboard: host mode itself
is covered by test_usb_port (the rules) and the device check."""
from test_apps import CRASHED, asleep, count_logs, idle, long_press, must_log, since, start_app


def exit_app(sim, name):
    t = since(sim)
    sim.wait(1, console=r"never", since=t)  # the touch that chose the app is up
    idle(sim)
    long_press(sim)
    must_log(sim, r"Apps: %s ended with 0" % name, t)
    asleep(sim)


def start_keys(sim):
    asleep(sim)
    sim.api("POST", "/dock", {"docked": True})
    start = start_app(sim, ["keys"], "keys")
    must_log(sim, r"keys: usb keyboard enable 0", start)
    must_log(sim, r"USB keys: input power: device mode", start)
    return start


def test_powered_port_stays_a_device_and_serial_keys_work(device, elfs):
    sim = device({"/apps/keys.elf": elfs["keys"]})
    start = start_keys(sim)
    t = since(sim)
    sim.api("POST", "/serial", b"a")
    must_log(sim, r"keys: down usage 0x00 code 0x200 mods 0x00 'a'", t)
    r = sim.wait(15, console=r"USB keys: (host mode|no input power)", since=start)
    assert not r.get("ok"), "the dock's power was not seen"
    exit_app(sim, "keys")
    assert count_logs(sim, CRASHED) == 0


def test_a_short_power_gap_does_not_make_the_port_a_host(device, elfs):
    sim = device({"/apps/keys.elf": elfs["keys"]})
    start_keys(sim)
    t = since(sim)
    sim.api("POST", "/dock", {"docked": False})
    must_log(sim, r"USB keys: no input power, checking", t, 5)
    sim.api("POST", "/dock", {"docked": True})
    must_log(sim, r"USB keys: input power: device mode", t, 5)
    r = sim.wait(5, console=r"USB keys: host mode", since=t)
    assert not r.get("ok"), "host mode within USB_PORT_CLEAR_MS of input power"
    exit_app(sim, "keys")
    assert count_logs(sim, CRASHED) == 0
