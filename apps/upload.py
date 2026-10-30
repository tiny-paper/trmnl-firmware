"""Copies apps to /apps on a TRMNL X over its USB serial port (docs/apps.md, "Copy an app to the X").

  python3 apps/upload.py <port> apps/build/hello.elf [more.elf ...]

On the X first: menu, Apps..., Receive over USB... . Only /apps/<name>.elf is written; the file system with
TRMNL's cached screens stays. Protocol: lib/trmnl/include/app_upload.h. Needs pyserial (in a venv).
"""
import os
import sys
import time
import zlib

BLOCK = 64  # APP_UPLOAD_BLOCK
REPLY_S = 10


class UploadError(Exception):
    pass


def upload(name, data, write, read_reply):
    """Sends one file. write(bytes); read_reply(timeout_s) returns the next "TAPP ..." line or None."""
    write(b"TAPP PUT %s %d %08x\n" % (name.encode(), len(data), zlib.crc32(data) & 0xFFFFFFFF))
    expect(read_reply(REPLY_S), "READY")
    sent = 0
    while sent < len(data):
        chunk = data[sent:sent + BLOCK]
        write(chunk)
        sent += len(chunk)
        if sent < len(data):
            expect(read_reply(REPLY_S), "ACK %d" % sent)
    expect(read_reply(REPLY_S), "OK %s" % name)


def expect(line, want):
    if line is None:
        raise UploadError("no reply (wanted %r)" % want)
    if line != "TAPP " + want:
        raise UploadError(line)


def serial_port(path):
    import serial  # pyserial

    port = serial.Serial(path, 115200, timeout=0.2)

    def read_reply(timeout_s):
        end = time.time() + timeout_s
        while time.time() < end:
            line = port.readline().decode(errors="replace").strip()
            if line.startswith("TAPP "):
                return line
        return None

    return port, read_reply


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    port, read_reply = serial_port(argv[1])
    port.reset_input_buffer()
    for path in argv[2:]:
        name = os.path.basename(path)
        upload(name, open(path, "rb").read(), port.write, read_reply)
        print("sent %s" % name)


if __name__ == "__main__":
    try:
        main(sys.argv)
    except UploadError as e:
        sys.exit("upload failed: %s" % e)
