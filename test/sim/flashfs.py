"""A merged TRMNL_X image with files added to its LittleFS partition: for the simulator only. A real X gets
its apps over USB serial (apps/upload.py); flashing a file system image would replace TRMNL's own files."""
import littlefs

BLOCK = 4096


def add_files(fs_bytes, files):
    """The LittleFS image `fs_bytes` with `files` ({"/apps/hello.elf": bytes}) added"""
    fs = littlefs.LittleFS(block_size=BLOCK, block_count=len(fs_bytes) // BLOCK, mount=False)
    fs.context.buffer = bytearray(fs_bytes)
    fs.mount()
    for path, data in files.items():
        parts = path.strip("/").split("/")
        for i in range(1, len(parts)):
            try:
                fs.mkdir("/" + "/".join(parts[:i]))
            except FileExistsError:
                pass
        with fs.open(path, "wb") as f:
            f.write(data)
    fs.unmount()
    return bytes(fs.context.buffer)


FS_OFFSET = 0x620000  # boards/trmnl_x_partitions.csv: spiffs, LittleFS on the X
FS_SIZE = (9898 * 1024) // BLOCK * BLOCK  # whole blocks, as littlefs.bin


def with_files(merged_path, out_path, files):
    """Writes `merged_path` with `files` ({"/apps/hello.elf": bytes}) added to its file system to `out_path`."""
    image = bytearray(open(merged_path, "rb").read())
    fs_bytes = bytes(image[FS_OFFSET:FS_OFFSET + FS_SIZE])
    assert len(fs_bytes) == FS_SIZE, "the merged image holds no full file system"
    image[FS_OFFSET:FS_OFFSET + FS_SIZE] = add_files(fs_bytes, files)
    open(out_path, "wb").write(image)


def read_files(flash_path, under="/"):
    """{path: bytes} of every file below `under` in the LittleFS partition of a sim flash file (read only)"""
    with open(flash_path, "rb") as f:
        f.seek(FS_OFFSET)
        fs_bytes = f.read(FS_SIZE)
    fs = littlefs.LittleFS(block_size=BLOCK, block_count=len(fs_bytes) // BLOCK, mount=False)
    fs.context.buffer = bytearray(fs_bytes)
    fs.mount()
    out = {}
    try:
        for root, _dirs, files in fs.walk(under):
            for name in files:
                path = root.rstrip("/") + "/" + name
                with fs.open(path, "rb") as fh:
                    out[path] = fh.read()
    except FileNotFoundError:
        pass
    fs.unmount()
    return out
