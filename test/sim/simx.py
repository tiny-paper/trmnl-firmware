"""One trmnl-sim process running this firmware's TRMNL_X build (~/workspace/trmnl-sim, its control API).

The sim writes the merged image's partitions (the app and the LittleFS image) into its flash file on every
start and keeps NVS, so a test's apps go into a merged image of its own (flashfs.py) and the device's
set-up state into a flash file copied per test.
"""
import json
import os
import signal
import socket
import subprocess
import time
import urllib.error
import urllib.request

SIM_DIR = os.path.expanduser(os.environ.get("TRMNLSIM_DIR", "~/workspace/trmnl-sim"))
SIM_BIN = os.path.join(SIM_DIR, "target", "release", "trmnl-sim")
MAC = "D8:3B:DA:A0:B1:C2"


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Sim:
    def __init__(self, image, elf, flash, tmp, mock_port, erase=False):
        self.control = free_port()
        self.portal = free_port()
        self.log_path = os.path.join(tmp, "sim-%d.log" % self.control)
        self.log = open(self.log_path, "wb")
        cmd = [SIM_BIN, image, "--elf", elf, "--env", "TRMNL_X", "--headless", "--turbo", "--offline",
               "--flash", flash, "--mac", MAC, "--control", "127.0.0.1:%d" % self.control,
               "--portal-port", str(self.portal), "--mock-server=%d" % mock_port]
        if erase:
            cmd.append("--erase")
        self.proc = subprocess.Popen(cmd, stdout=self.log, stderr=subprocess.STDOUT)
        end = time.time() + 30
        while self._status() is None:
            if self.proc.poll() is not None or time.time() > end:
                raise RuntimeError("sim did not start:\n" + self.log_tail())
            time.sleep(0.2)

    def api(self, method, path, body=None, timeout=10):
        data = json.dumps(body).encode() if isinstance(body, (dict, list)) else body
        req = urllib.request.Request("http://127.0.0.1:%d%s" % (self.control, path), data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                t = r.read()
        except urllib.error.HTTPError as e:
            t = e.read()
            if e.code not in (408, 409):
                raise
        return json.loads(t) if t[:1] in (b"{", b"[") else t

    def _status(self):
        try:
            return self.api("GET", "/status", timeout=2)
        except OSError:
            return None

    def status(self):
        return self.api("GET", "/status")

    def console_total(self):
        return self.status()["console_total"]

    def wait(self, timeout=120, **cond):
        """POST /wait: the reply, with "ok" false on a timeout."""
        cond["timeout_s"] = timeout
        return self.api("POST", "/wait", cond, timeout=timeout + 10)

    def wait_log(self, regex, since=0, timeout=120):
        """The console line matching `regex` after line `since`, or None."""
        r = self.wait(timeout, console=regex, since=since)
        return r["line"]["text"] if r.get("ok") else None

    def touch(self, zone, ms=80):
        return self.api("POST", "/touch", {"zone": zone, "ms": ms}, timeout=60)

    def set_pref(self, key, typ, value, namespace="data"):
        r = self.api("PUT", "/preferences", {"partition": "nvs", "namespace": namespace, "key": key,
                                             "type": typ, "value": value})
        assert r.get("ok"), r

    def prefs(self, namespace="data"):
        return {e["key"]: e["value"] for e in self.api("GET", "/preferences")["entries"]
                if e["namespace"] == namespace}

    def stop(self):
        """By our own PID only."""
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(15)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.log.close()

    def log_tail(self, n=4000):
        with open(self.log_path, "rb") as f:
            return f.read()[-n:].decode(errors="replace")
