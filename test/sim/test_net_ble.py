"""An app's net_up while Bluetooth LE is on (src/app_api_table.cpp, radio_share.h): the saved network joins and
each call logs one line. The sim's Wi-Fi model has no coexistence: the radio sharing itself is a device check."""
from test_apps import CRASHED, asleep, count_logs, must_log, start_app


def test_net_up_joins_with_ble_on_and_logs_each_call(device, elfs):
    sim = device({"/apps/netble.elf": elfs["netble"]})
    start = start_app(sim, ["netble"], "netble")
    must_log(sim, r"BLE keys: radio on", start)
    must_log(sim, r"Net: up in \d+ ms \(joined, rssi -?\d+, ble on", start, 120)
    must_log(sim, r"Net: up in \d+ ms \(already, rssi -?\d+, ble on", start)
    must_log(sim, r"netble: up", start)
    must_log(sim, r"Apps: netble ended with 0", start)
    asleep(sim)
    assert count_logs(sim, CRASHED) == 0
