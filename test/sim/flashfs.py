"""A merged TRMNL_X image with files added to its LittleFS partition (apps/pack.py)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "apps"))
import pack  # noqa: E402

FS_OFFSET = 0x620000  # boards/trmnl_x_partitions.csv: spiffs, LittleFS on the X
FS_SIZE = (9898 * 1024) // pack.BLOCK * pack.BLOCK  # whole blocks, as littlefs.bin


def with_files(merged_path, out_path, files):
    """Writes `merged_path` with `files` ({"/apps/hello.elf": bytes}) added to its file system to `out_path`."""
    image = bytearray(open(merged_path, "rb").read())
    fs_bytes = bytes(image[FS_OFFSET:FS_OFFSET + FS_SIZE])
    assert len(fs_bytes) == FS_SIZE, "the merged image holds no full file system"
    image[FS_OFFSET:FS_OFFSET + FS_SIZE] = pack.add_files(fs_bytes, files)
    open(out_path, "wb").write(image)
