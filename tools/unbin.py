#!/usr/bin/env python3
"""Unpack an Alan Wake resource bundle (*.bin with u32 count, name/size table, zlib data).

Format knowledge from AWTools' unbin.py (https://github.com/Nostritius/AWTools).

  unbin.py BUNDLE OUT_DIR
"""
import struct
import sys
import zlib
from pathlib import Path


def read_bundle(source):
    """Return {file name: bytes} for a bundle given as bytes or a path."""
    d = source if isinstance(source, bytes) else Path(source).read_bytes()
    count = struct.unpack_from("<I", d)[0]
    pos, table = 4, []
    for _ in range(count):
        length = struct.unpack_from("<I", d, pos)[0]
        name = d[pos + 4:pos + 4 + length].decode("ascii")
        size = struct.unpack_from("<I", d, pos + 4 + length)[0]
        table.append((name, size))
        pos += 8 + length
    data = zlib.decompress(d[pos:])
    files, offset = {}, 0
    for name, size in table:
        files[name] = data[offset:offset + size]
        offset += size
    return files


if __name__ == "__main__":
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    for name, data in read_bundle(sys.argv[1]).items():
        if Path(name).name != name or name in (".", ".."):
            raise ValueError(f"unsafe name in bundle: {name!r}")
        (out / name).write_bytes(data)
        print(len(data), name)
