"""An HTTPS request on the X must open while the Bluetooth LE keyboard is on (docs/apps.md "Memory for HTTPS").
mbedTLS allocates its record buffers and handshake state from PSRAM; internal RAM is left to Wi-Fi, the BLE
controller and DMA. With mbedTLS in internal RAM, every sync with the keyboard on failed to open (out of memory).
Run: pytest test/config (no device, no build)."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SDKCONFIG = ROOT / "sdkconfigs" / "sdkconfig.TRMNL_X"

# mbedTLS's internal-RAM need per connection with the setting (bytes, IDF 5.5): IN + OUT record buffers
# allocated at setup, plus handshake state (certificate chain, ECDHE, bignums) of about 20 KB
TLS_HANDSHAKE_STATE = 20 * 1024


def config():
    out = {}
    for line in SDKCONFIG.read_text().splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            k, v = line.split("=", 1)
            out[k] = v.strip('"')
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            out[line[2:-len(" is not set")]] = "n"
    return out


def tls_internal_bytes(c):
    """Internal RAM one TLS connection takes from the heap during its handshake"""
    if c.get("CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC") == "y":
        return 0
    if c.get("CONFIG_MBEDTLS_DYNAMIC_BUFFER") == "y":
        records = max(int(c["CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN"]), int(c["CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN"]))
    else:
        records = int(c["CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN"]) + int(c["CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN"])
    return records + TLS_HANDSHAKE_STATE


def test_mbedtls_allocates_from_psram():
    c = config()
    assert c.get("CONFIG_SPIRAM_USE_MALLOC") == "y" or c.get("CONFIG_SPIRAM_USE_CAPS_ALLOC") == "y"
    assert c.get("CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC") == "y", "mbedTLS allocates from internal RAM"
    assert c.get("CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC") != "y"
    assert tls_internal_bytes(c) == 0


def test_mbedtls_record_buffers_are_dynamic():
    # Allocated per record and freed after it: an idle connection holds no 16 KB receive buffer
    assert config().get("CONFIG_MBEDTLS_DYNAMIC_BUFFER") == "y"


def test_nimble_pools_in_psram_and_one_connection():
    c = config()
    assert c.get("CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL") == "y", "NimBLE's pools take internal RAM"
    assert c.get("CONFIG_BT_NIMBLE_MAX_CONNECTIONS") == "1"
    assert int(c["CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT"]) <= 12
    assert int(c["CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT"]) <= 12
    assert int(c["CONFIG_BT_CTRL_BLE_MAX_ACT"]) <= 2


def test_estimate_counts_the_buffers():
    # The estimate itself: 16 KB + 4 KB records + handshake in internal RAM before the fix
    old = {"CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN": "16384", "CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN": "4096"}
    assert tls_internal_bytes(old) == 16384 + 4096 + TLS_HANDSHAKE_STATE
    assert tls_internal_bytes(dict(old, CONFIG_MBEDTLS_DYNAMIC_BUFFER="y")) == 16384 + TLS_HANDSHAKE_STATE
