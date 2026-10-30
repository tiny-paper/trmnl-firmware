"""Adds apps to the TRMNL X's LittleFS image, for flashing (docs/apps.md, "Copy an app to the X").

  python3 apps/pack.py <littlefs.bin> <out.bin> apps/build/hello.elf [more.elf ...]

<littlefs.bin> is the file system image of the TRMNL_X build (.pio/build/TRMNL_X/littlefs.bin); each ELF goes
to /apps/<name>.elf. Needs littlefs-python (pip install littlefs-python, in a venv).
"""
import os
import sys

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


def main(argv):
    if len(argv) < 4:
        sys.exit(__doc__)
    files = {"/apps/" + os.path.basename(p): open(p, "rb").read() for p in argv[3:]}
    out = add_files(open(argv[1], "rb").read(), files)
    open(argv[2], "wb").write(out)
    print("wrote %s with %s" % (argv[2], ", ".join(sorted(files))))


if __name__ == "__main__":
    main(sys.argv)
