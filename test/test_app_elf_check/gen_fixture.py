"""Writes hello_elf.h, the test fixture, from apps/build/hello.elf (apps/build.sh hello)."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
data = (root / "apps/build/hello.elf").read_bytes()
lines = [", ".join(f"0x{b:02x}" for b in data[i : i + 16]) for i in range(0, len(data), 16)]
out = (
    "// apps/hello built by apps/build.sh, as a test fixture (gen_fixture.py)\n"
    "#pragma once\n#include <stdint.h>\n\nstatic const uint8_t hello_elf[] = {\n"
    + "".join(f"    {line},\n" for line in lines)
    + "};\n"
)
(Path(__file__).parent / "hello_elf.h").write_text(out)
