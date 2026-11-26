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
          "TVSHOWS": "TV shows", "SIGNS": "Signs",
          "DLC1_ALARMCLOCKS": "Alarm clocks", "DLC1_CARDBOARDCUTOUTS": "Cardboard cutouts",
          "DLC2_NIGHTSPRINGSVIDEOGAMES": "Video games"}
rows = [r for r in rows if r["category"] in LABELS]  # the mod shows these only
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


def missing(level, mode):
    return subprocess.run([binary, config, str(level), mode], capture_output=True, text=True,
                          check=True).stdout


for level in sorted({int(r["level"][6:]) for r in rows}):
    here = [r for r in rows if int(r["level"][6:]) == level]
    want = {}
    for category in LABELS:
        # cutouts are found in another order than their numbers: 1 and 6, 2 and 3 swap places
        swapped = {1: 6, 6: 1, 2: 3, 3: 2} if category == "DLC1_CARDBOARDCUTOUTS" else {}
        these = sorted((r for r in here if r["category"] == category),
                       key=lambda r: swapped.get(r["number"], r["number"]))
        for position, r in enumerate(these, 1):
            if not r["found"]:
                if category == "MANUSCRIPTPAGES":
                    position = int(r["label"].rsplit("_", 1)[1])
                want.setdefault(LABELS[category], []).append((position, r["nightmare"]))
    for mode in ("numbers", "spoilers"):
        out = missing(level, mode)
        assert out.isascii() and not out.endswith(","), out
        got, label, titled, more = {}, None, 0, 0
        for line in out.splitlines()[1:]:
            if not line.startswith(" "):
                if line.startswith("nothing"):
                    continue
                label, line = line.split(":", 1)
                label = label.split(" (of ")[0]
            if line.startswith("    +"):
                more = int(line.split()[0][1:])
            elif mode == "spoilers" and label == "Manuscript pages":
                if line.strip():
                    titled += 1
                    number, title = line.split(None, 1)
                    got.setdefault(label, []).append((int(number), "(Nightmare)" in title))
            else:
                assert len(line) <= 90, line
                for number, rest in re.findall(r"(?:^| )#?(\d+)((?: \([^)]*\))?)", line):
                    got.setdefault(label, []).append((int(number), rest == " (Nightmare)"))
        expected = dict(want)
        pages = sorted(expected.get("Manuscript pages", []))
        if pages:
            expected["Manuscript pages"] = pages
        if "Manuscript pages" in got:
            got["Manuscript pages"].sort()
        if more:
            assert titled == 16 and titled + more == len(pages), out
            expected["Manuscript pages"] = got["Manuscript pages"]
        assert got == expected, (level, mode, got, expected, out)
        cutouts = want.get("Cardboard cutouts", [])
        named = len(re.findall("Wake|Wheeler|Breaker|Anderson|Weaver",
                               " ".join(out.split("Cardboard cutouts")[1:])))
        assert named == (len(cutouts) if mode == "spoilers" else 0), (level, mode, out)
        if mode == "numbers":  # no names or places without the spoilers
            assert not re.search(r"#\d+ \(", out), (level, out)
print("missing lists match the report for every level")
print(missing(5, "numbers"))
print(missing(5, "spoilers"))
print(missing(100, "numbers"))
print(missing(100, "spoilers"))


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
