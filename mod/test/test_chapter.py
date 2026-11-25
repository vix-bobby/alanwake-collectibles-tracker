"""Overlay text per chapter against tools/collectibles.py, and checkpoint parsing.

  python3 mod/test/test_chapter.py BINARY CONFIG REPORT.json SAVEGAME
"""
import json
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

binary, config, report, save = sys.argv[1:5]
rows = json.loads(Path(report).read_text())
LABELS = {"MANUSCRIPTPAGES": "Manuscript pages", "THERMOS": "Coffee thermoses",
          "CANPYRAMIDS": "Can pyramids", "CHESTS": "Chests", "RADIOSHOWS": "Radio shows",
          "TVSHOWS": "TV shows", "SIGNS": "Signs", "COCONUTSONGS": "Coconut songs",
          "DLC1_ALARMCLOCKS": "Alarm clocks", "DLC1_CARDBOARDCUTOUTS": "Cardboard cutouts",
          "DLC2_NIGHTSPRINGSVIDEOGAMES": "Video games"}
EPISODES = [(1, 2, 3), (4, 5), (9, 10), (11, 12, 13), (14, 15), (16, 17), (100,), (200,)]


def score(levels, category, hard):
    items = [r for r in rows if int(r["level"][6:]) in levels
             and r["nightmare"] == hard and (hard or r["category"] == category)]
    return f"{sum(r['found'] for r in items)} / {len(items)}" if items else None


for episode in EPISODES:
    for level in episode:
        out = subprocess.run([binary, config, str(level)], capture_output=True, text=True,
                             check=True).stdout.splitlines()
        got = dict(line.split(":  ", 1) for line in out if ":  " in line)
        want = {}
        for category, hard in [(c, False) for c in LABELS] + [(None, True)]:
            total = score(episode, category, hard)
            if total:
                value = score((level,), category, hard) or "none here"
                if level < 100:
                    value += f"  ({total})"
                want["Nightmare pages" if hard else LABELS[category]] = value
        assert got == want, (level, got, want)
        assert len(out) >= 2, out
print("chapter text matches collectibles.py for every level")
print(subprocess.run([binary, config, "5"], capture_output=True, text=True).stdout)


def level_of(path):
    return int(subprocess.run([binary, "--level", str(path)], capture_output=True, text=True,
                              check=True, timeout=20).stdout)


data = Path(save).read_bytes()
expected = int(re.search(rb"level_(\d+)", data[:128])[1])
assert level_of(save) == expected, (level_of(save), expected)
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / "savegame.aws"
    random.seed(0)
    cases = [data[:n] for n in range(0, 128)]
    for _ in range(500):
        broken = bytearray(data[:128])
        for _ in range(random.randint(1, 6)):
            broken[random.randrange(40)] = random.randrange(256)
        cases.append(bytes(broken))
    for case in cases:
        path.write_bytes(case)
        assert 0 <= level_of(path) <= 999
print(f"checkpoint level {expected} read; {len(cases)} damaged headers handled")
