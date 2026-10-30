"""Fixtures for the app loader's sim tests (docs/apps.md, "Sim tests").

Needs: `pio run -e TRMNL_X` (the merged image and its ELF), the trmnl-sim release build in $TRMNLSIM_DIR, and a
venv with pytest and littlefs-python. A set-up device (Wi-Fi joined, the built-in mock server, the touch bar
tutorial done) is made once per session; each test starts from a copy of its flash.
"""
import glob
import json
import os
import shutil
import subprocess
import sys
import urllib.request

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import flashfs  # noqa: E402
import simx  # noqa: E402

BUILD = os.path.join(ROOT, ".pio", "build", "TRMNL_X")


def pytest_sessionstart(session):
    if not os.path.exists(simx.SIM_BIN):
        pytest.exit("missing %s: cargo build --release in %s" % (simx.SIM_BIN, simx.SIM_DIR), 2)
    if not merged_image():
        pytest.exit("missing the merged image: pio run -e TRMNL_X", 2)


def merged_image():
    found = [p for p in glob.glob(os.path.join(BUILD, "FW*-TRMNL_X.bin")) if not p.endswith("-ota.bin")]
    return found[0] if found else None


@pytest.fixture(scope="session")
def tmp(tmp_path_factory):
    return str(tmp_path_factory.mktemp("apps-sim"))


@pytest.fixture(scope="session")
def elfs(tmp):
    """The apps, built: hello and keys from apps/, the misbehaving ones from test/sim/apps/"""
    out = os.path.join(tmp, "elfs")
    dirs = [os.path.join(ROOT, "apps", a) for a in ("hello", "keys")] + sorted(glob.glob(os.path.join(HERE, "apps", "*")))
    subprocess.run([os.path.join(ROOT, "apps", "build.sh")] + dirs, check=True, env=dict(os.environ, OUT=out),
                   stdout=subprocess.DEVNULL)
    return {os.path.basename(p)[:-4]: open(p, "rb").read() for p in glob.glob(os.path.join(out, "*.elf"))}


@pytest.fixture(scope="session")
def mock_port():
    return simx.free_port()


@pytest.fixture(scope="session")
def base_flash(tmp, mock_port):
    """A flash file of a set-up X, asleep: shipped, joined to TRMNL-Sim, its server the sim's mock server."""
    keep = os.environ.get("APPS_SIM_BASE_FLASH")  # reuse a set-up device between runs (same NVS layout)
    if keep and os.path.exists(keep):
        return keep
    flash = keep or os.path.join(tmp, "base-flash.bin")
    elf = os.path.join(BUILD, "firmware.elf")
    sim = simx.Sim(merged_image(), elf, flash, tmp, mock_port, erase=True)
    try:
        # A fresh X flashes its modem, then waits to be unplugged and docked again (shipment mode)
        assert sim.wait_log(r"Waiting for USB plug-off", timeout=240), sim.log_tail()
        sim.api("POST", "/dock", {"docked": False})
        assert sim.wait_log(r"Entering shipment mode", timeout=120), sim.log_tail()
        sim.api("POST", "/dock", {"docked": True})
        assert sim.wait(180, portal=True).get("ok"), sim.log_tail()
        sim.api("POST", "/wifi", {"portal_client": False})
        req = urllib.request.Request("http://127.0.0.1:%d/connect" % sim.portal, method="POST",
                                     data=json.dumps({"ssid": "TRMNL-Sim", "pswd": "simpassword",
                                                      "server": "http://10.0.2.2:%d" % mock_port}).encode(),
                                     headers={"Content-Type": "application/json"})
        urllib.request.urlopen(req, timeout=30).read()
        assert sim.wait(300, state="deep_sleep").get("ok"), sim.log_tail()
        sim.set_pref("touch_tutorial", "u8", "1")
    finally:
        sim.stop()
    return flash


@pytest.fixture
def device(tmp, base_flash, mock_port, request):
    """start(files): the set-up X with `files` ({"/apps/x.elf": bytes}) in its file system; stopped by its PID"""
    started = []

    def start(files):
        name = request.node.name
        image = os.path.join(tmp, "%s-TRMNL_X.bin" % name)
        flashfs.with_files(merged_image(), image, files)
        flash = os.path.join(tmp, "%s-flash.bin" % name)
        shutil.copyfile(base_flash, flash)
        sim = simx.Sim(image, os.path.join(BUILD, "firmware.elf"), flash, tmp, mock_port)
        started.append(sim)
        return sim

    yield start
    for sim in started:
        sim.stop()
