#!/usr/bin/env python3
"""Write the test fixtures: a made-up profile and save header, and the matching report.

Run once after the collectible catalog changes (needs the game installed):
  python3 -I mod/test/make_fixtures.py
Nothing here comes from a real player's files.
"""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent / "fixtures"
TOOL = HERE.parents[2] / "tools" / "collectibles.py"

# (category, items in the game): every category the profile lists
CATEGORIES = [("CANPYRAMIDS", 12), ("CHESTS", 30), ("COCONUTSONGS", 2), ("DLC1_ALARMCLOCKS", 10),
              ("DLC1_CARDBOARDCUTOUTS", 6), ("DLC2_NIGHTSPRINGSVIDEOGAMES", 10),
              ("RADIOSHOWS", 11), ("SIGNS", 25), ("THERMOS", 101), ("TVSHOWS", 14)]


def numbers(found):
    return bytes([len(found)]) + bytes(found)


# some found, some not, so both show up in every test
pages = [n for n in range(106) if n % 4]
config = bytearray(16)  # stands for the unrelated data before the lists
config += numbers(pages) * 2
config.append(len(CATEGORIES))
for index, (name, last) in enumerate(CATEGORIES):
    found = [n for n in range(1, last + 1) if (n + index) % 3 and n != 90]
    if name == "DLC2_NIGHTSPRINGSVIDEOGAMES":
        found = []
    config += struct.pack("<I", len(name)) + name.encode() + numbers(found)

HERE.mkdir(exist_ok=True)
(HERE / "config").write_bytes(config)

level = b"gameworld:level_05"
(HERE / "savegame.aws").write_bytes(struct.pack("<III", 80, 0, len(level)) + level + bytes(200))

with tempfile.TemporaryDirectory() as tmp:
    full = Path(tmp) / "report.json"
    subprocess.run([sys.executable, "-I", str(TOOL), "--config", str(HERE / "config"),
                    "--json", str(full)], check=True, stdout=subprocess.DEVNULL)
    keep = ("category", "number", "level", "nightmare", "found")
    rows = [{key: row[key] for key in keep} for row in json.loads(full.read_text())]
(HERE / "report.json").write_text("[\n" + ",\n".join(json.dumps(r) for r in rows) + "\n]\n")
print(len(rows), "items written to", HERE)
