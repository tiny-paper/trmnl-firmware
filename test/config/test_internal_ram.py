"""Internal RAM during an app's HTTPS request with the BLE keyboard on (docs/apps.md "Memory for HTTPS").
Device, 843639c: the writer's sync logged "out of memory: caps 0x8, 1370 bytes failed" twice (caps 0x8 is
MALLOC_CAP_DMA). With mbedTLS in PSRAM, hardware AES copies every record through internal DMA bounce buffers
(up to 1600 B in and out per call, esp_aes_dma_core.c), and the default heap, Wi-Fi and lwIP took internal RAM first.
The budget below counts the parts the config decides; the device run calibrates it (no headroom on 843639c). Run: pytest test/config (no device, no build)."""
from test_tls_memory import config

# IRAM the two Wi-Fi options take from the S3's shared internal RAM, measured: esp_idf_size DIRAM remain
# 144,125 B on 843639c, 162,125 B with both off (DIRAM .text 117,043 -> 99,179 B)
WIFI_IRAM_BYTES = 18_000
WIFI_BUF = 1600
AES_BOUNCE = 2 * 1600 + 64  # in + out chunk and descriptors per AES call on a PSRAM record
APP_SMALL_HEAP = 8 * 1024  # an app's malloc'd blocks of 4 to 16 KB (writer: glyph scratch, file text), estimate
# On 843639c the request ran internal RAM out (1370 B failed with the reserve in use): the peak need there equals
# what was free, so the headroom now is what the config takes off that peak. It must cover one more TLS record's
# worth of Wi-Fi buffers and a safety margin of the reserve size.
NEEDED_HEADROOM = 32 * 1024


def internal_peak(c):
    """Internal RAM the config-dependent parts take at the peak of an HTTPS request: {what: bytes}"""
    use = {}
    in_iram = c.get("CONFIG_ESP_WIFI_IRAM_OPT") == "y" or c.get("CONFIG_ESP_WIFI_RX_IRAM_OPT") == "y"
    use["Wi-Fi code in IRAM"] = WIFI_IRAM_BYTES if in_iram else 0
    # one TCP send window queued in Wi-Fi TX buffers and lwIP pbufs, plus out-of-order RX pbufs
    tcp = int(c["CONFIG_LWIP_TCP_SND_BUF_DEFAULT"]) + int(c["CONFIG_LWIP_TCP_OOSEQ_MAX_PBUFS"]) * WIFI_BUF
    use["Wi-Fi TX + lwIP pbufs"] = 0 if c.get("CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP") == "y" else tcp
    use["AES DMA bounce"] = AES_BOUNCE if c.get("CONFIG_MBEDTLS_HARDWARE_AES") == "y" else 0
    use["4-16 KB mallocs"] = APP_SMALL_HEAP if int(c["CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL"]) > 4096 else 0
    return use


OLD_843639C = {"CONFIG_MBEDTLS_HARDWARE_AES": "y", "CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP": "n",
               "CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL": "16384", "CONFIG_ESP_WIFI_IRAM_OPT": "y",
               "CONFIG_ESP_WIFI_RX_IRAM_OPT": "y"}


def headroom(c):
    return sum(internal_peak(dict(c, **OLD_843639C)).values()) - sum(internal_peak(c).values())


def test_no_hardware_aes_bounce_with_records_in_psram():
    c = config()
    # hardware AES on a PSRAM record allocates internal DMA bounce buffers per call: the 1370 B caps 0x8 failure
    assert not (c.get("CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC") == "y" and c.get("CONFIG_MBEDTLS_HARDWARE_AES") == "y")


def test_wifi_and_lwip_prefer_psram():
    assert config().get("CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP") == "y"


def test_default_heap_takes_psram_above_4k():
    assert int(config()["CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL"]) <= 4096


def test_internal_reserve_kept():
    # internal RAM malloc() never takes: for DMA, stacks and explicit internal allocations
    assert int(config()["CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL"]) >= 32 * 1024


def test_wifi_code_in_flash():
    # both options put Wi-Fi code in IRAM, which shares the S3's internal RAM with the heap
    c = config()
    assert c.get("CONFIG_ESP_WIFI_IRAM_OPT") != "y"
    assert c.get("CONFIG_ESP_WIFI_RX_IRAM_OPT") != "y"


def test_budget_headroom_over_843639c():
    c = config()
    assert headroom(c) >= NEEDED_HEADROOM, (headroom(c), internal_peak(c))


def test_budget_counts_each_part():
    # the estimate itself: 843639c has no headroom, each change adds its part
    c = config()
    old = dict(c, **OLD_843639C)
    assert headroom(old) == 0
    assert headroom(dict(old, CONFIG_MBEDTLS_HARDWARE_AES="n")) == AES_BOUNCE
    assert headroom(dict(old, CONFIG_ESP_WIFI_IRAM_OPT="n", CONFIG_ESP_WIFI_RX_IRAM_OPT="n")) == WIFI_IRAM_BYTES


def test_oom_line_names_internal_ram_and_fits():
    # the event log's out-of-memory text: caps and size first, then internal free/largest, within EVLOG_TEXT_MAX
    import re
    from test_tls_memory import ROOT
    src = (ROOT / "src" / "evlog.cpp").read_text()
    fmt = re.search(r'evlog_add\("(out of memory:[^"]*)"', src).group(1)
    assert fmt.index("caps") < fmt.index("bytes") < fmt.index("int ")
    widest = fmt.replace("0x%lx", "0x" + "f" * 8).replace("%lu", "4294967295")
    limit = int(re.search(r"#define EVLOG_TEXT_MAX (\d+)", (ROOT / "lib/trmnl/include/evlog_ring.h").read_text()).group(1))
    assert "%" not in widest and len(widest) <= limit, (len(widest), widest)
