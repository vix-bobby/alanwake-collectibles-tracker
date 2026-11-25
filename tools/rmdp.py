#!/usr/bin/env python3
"""List or selectively extract files from Alan Wake RMDP v2 archives (read-only on input).

Format knowledge from AWTools' unrmdp.py (https://github.com/Nostritius/AWTools).

  rmdp.py list    DATA_DIR REGEX
  rmdp.py extract DATA_DIR REGEX OUT_DIR   -> OUT_DIR/<archive>/<path in archive>
"""
import re
import struct
import sys
import zlib
from pathlib import Path


def entries(bin_path):
    d = bin_path.read_bytes()
    endian = ">" if d[0] == 1 else "<"
    version, n_folders, n_files = struct.unpack_from(endian + "III", d, 1)
    if version != 2:
        raise ValueError(f"{bin_path.name}: unsupported RMDP version {version}")
    name_size = struct.unpack_from(">I", d, 13)[0]
    names = d[-name_size:]
    pos = 17 + 128

    def name_at(off):
        if off == 0xFFFFFFFF:
            return ""
        return names[off:names.index(b"\0", off)].decode("ascii")

    folders = []
    for _ in range(n_folders):
        _crc, _next, parent, _, name_off, _, _ = struct.unpack_from(endian + "7I", d, pos)
        folders.append((name_at(name_off), parent))
        pos += 28

    def folder_path(i):
        parts = []
        while i != 0xFFFFFFFF:
            name, i = folders[i]
            if name:
                parts.append(name)
        return "/".join(reversed(parts))

    for _ in range(n_files):
        _, _, parent, _, name_off, offset, size, crc = struct.unpack_from(endian + "5IQQI", d, pos)
        pos += 40
        yield f"{folder_path(parent)}/{name_at(name_off)}".lstrip("/"), offset, size, crc


def main():
    cmd, data_dir, pattern = sys.argv[1], Path(sys.argv[2]), re.compile(sys.argv[3], re.I)
    out = Path(sys.argv[4]).resolve() if cmd == "extract" else None
    for bin_path in sorted(data_dir.glob("*.bin")):
        for path, offset, size, crc in entries(bin_path):
            if not pattern.search(path):
                continue
            print(f"{bin_path.stem}\t{size}\t{path}")
            if out is None:
                continue
            target = (out / bin_path.stem / path.replace(":", "_")).resolve()
            if out not in target.parents:
                raise ValueError(f"unsafe path in archive: {path!r}")
            with bin_path.with_suffix(".rmdp").open("rb") as f:
                f.seek(offset)
                data = f.read(size)
            # some entries store the checksum byte-swapped
            actual = zlib.crc32(data)
            if crc not in (actual, int.from_bytes(actual.to_bytes(4, "little"), "big")):
                print(f"WARNING CRC mismatch: {bin_path.stem} {path} want {crc:08x} got {zlib.crc32(data):08x} len {len(data)}/{size}", file=sys.stderr)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)


if __name__ == "__main__":
    main()
