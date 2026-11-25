"""The C counts must agree with the report written by tools/collectibles.py.

  python3 mod/test/test_counts.py COUNTS_TEST_BINARY CONFIG REPORT.json
"""
import json
import subprocess
import sys
from pathlib import Path

binary, config, report = sys.argv[1:4]
rows = json.loads(Path(report).read_text())
ids = {f"NAME_EPISODE_{e}": levels for e, levels in
       enumerate([(1, 2, 3), (4, 5), (9, 10), (11, 12, 13), (14, 15), (16, 17)], 1)}
ids.update({"NAME_EPISODE_SPECIAL_1": (100,), "NAME_EPISODE_SPECIAL_2": (200,)})
ids.update({f"NAME_LEVEL_{n}": (n,) for n in range(1, 18)})

NAMES = {"MANUSCRIPTPAGES": "page", "THERMOS": "thermos", "CANPYRAMIDS": "pyramid",
         "CHESTS": "chest", "RADIOSHOWS": "radio", "TVSHOWS": "TV", "SIGNS": "sign",
         "COCONUTSONGS": "song", "DLC1_ALARMCLOCKS": "clock", "DLC1_CARDBOARDCUTOUTS": "cutout",
         "DLC2_NIGHTSPRINGSVIDEOGAMES": "video game"}


def expected(detail):
    result = {}
    for key, levels in ids.items():
        here = [r for r in rows if int(r["level"].split("_")[1]) in levels]
        if not here:
            continue
        missing = [r["category"] for r in here if not r["found"] and not r["nightmare"]]
        hard = sum(not r["found"] and r["nightmare"] for r in here)
        text = f"{len(missing)} left" if missing else "done"
        parts = [f"{n} {name if n == 1 or name == 'thermos' else name + 's'}"
                 for category, name in NAMES.items() if (n := missing.count(category))]
        if detail and parts:
            text += ": " + ", ".join(parts)
        if hard:
            sep = ";" if detail and parts else ","
            text += f"{sep} +{hard} Nightmare"
        result[key] = f" ({text})"
    return result


for detail in (False, True):
    command = [binary, config] + (["detail"] if detail else [])
    out = subprocess.run(command, capture_output=True, text=True, check=True).stdout
    got = {line.split(" ", 1)[0]: " " + line.split(" ", 1)[1] for line in out.splitlines()}
    want = expected(detail)  # for every entry; label.c decides which entries get the split
    assert got == want, {k: (got.get(k), want.get(k)) for k in set(got) | set(want)
                         if got.get(k) != want.get(k)}
    print(f"counts (detail={detail}) match collectibles.py for {len(got)} menu entries")
for key, value in got.items():
    print(f"  {key}{value}")
