#!/usr/bin/env python3
"""Report which Alan Wake collectibles are missing, per episode. Read-only on all inputs.

The list of collectibles is read from the installed game:
  * global/cid_collectible.bin  categories and their totals
  * global/cid_notebook_page.bin  manuscript pages, with the Nightmare-only flag
  * level bundles               scripts calling UnlockCollectible(category, number)
                                or AddNoteToInventory(page)
  * globaldb/menu.xml           which level belongs to which episode
The numbers already found are read from the profile file `config`.

  collectibles.py --config PATH [--data DIR] [--lang en] [--all] [--json OUT]
"""
import argparse
import collections
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rmdp import entries  # noqa: E402
from unbin import read_bundle  # noqa: E402

PAGES = "MANUSCRIPTPAGES"
NIGHTMARE = " (Nightmare only)"
DEFAULT_DATA = "/mnt/c/Program Files (x86)/Steam/steamapps/common/Alan Wake/data"


class Archives:
    """Files of all RMDP archives in a directory; later (patch) archives win."""

    def __init__(self, data_dir):
        self.files, self.versions = {}, []
        for bin_path in sorted(Path(data_dir).glob("ep999-*.bin")):
            for path, offset, size, _crc in entries(bin_path):
                self.files[path] = (bin_path.with_suffix(".rmdp"), offset, size)
                self.versions.append((path, self.files[path]))
        if not self.files:
            raise SystemExit(f"no game archives found in {data_dir}")

    def read(self, path):
        return self.read_entry(self.files[path])

    @staticmethod
    def read_entry(entry):
        archive, offset, size = entry
        with archive.open("rb") as f:
            f.seek(offset)
            return f.read(size)


class Dp:
    """A dp_*.bin blob: references are resolved against its trailing data section."""

    def __init__(self, blob, size_field=8):
        self.blob = blob
        self.base = len(blob) - struct.unpack_from("<I", blob, size_field)[0]

    def offset(self, ref):
        return self.base + (ref >> 8) * 8 + (4 if ref & 0x80 else 0)

    def string(self, ref):
        start = self.offset(ref)
        if not self.base <= start < len(self.blob):
            return None
        return self.blob[start:self.blob.find(b"\0", start)].decode("latin1")


def categories(game):
    """{gid hash: (name, total)} from cid_collectible.bin."""
    cid = game.read("global/cid_collectible.bin")
    strings = Dp(game.read("global/dp_global.bin"))
    count = struct.unpack_from("<I", cid, 8)[0]
    found = {}
    # records start with the GID type 0x35, then hash, name ref, ref, total
    for pos in range(16, len(cid) - 19):
        gid_type, gid, name_ref, _, total = struct.unpack_from("<5I", cid, pos)
        if gid_type != 0x35 or total > 255:
            continue
        found[gid] = (name_ref, total)
    if len(found) != count:
        raise SystemExit(f"expected {count} categories, parsed {len(found)}")
    return {gid: (strings.string(ref), total) for gid, (ref, total) in found.items()}, strings


def gid_names(game):
    names = {}
    # every version: level scripts still use ids that patched registries dropped
    for path, entry in game.versions:
        if path.endswith("GIDRegistry.txt"):
            for line in game.read_entry(entry).decode("latin1").splitlines():
                parts = line.split(",")
                if len(parts) >= 4:
                    names[int(parts[0]), int(parts[1], 16)] = parts[2]
    return names


def script_records(bundle, code, names):
    """{gid: (instruction count, code ref, member values)} from cid_*script.bin."""
    records = {}
    for name, blob in bundle.items():
        if not (name.startswith("cid_") and name.endswith("script.bin")):
            continue
        for pos in range(len(blob) - 31):
            kind, gid, n_code, code_ref, _, _, n_vals, val_ref = struct.unpack_from("<8I", blob, pos)
            if (kind, gid) in names and code_ref & 0x7F == 0x0B and 0 < n_code < 100000 and n_vals < 2000:
                start = code.offset(val_ref)
                values = ()
                if n_vals and start + 4 * n_vals <= len(code.blob):
                    values = struct.unpack_from(f"<{n_vals}I", code.blob, start)
                records[kind, gid] = (n_code, code_ref, values)
    return records


def script_calls(code, params, records):
    """Yield (script gid, method name, arguments) for each call in the scripts."""
    blob = code.blob
    for script, (n_code, code_ref, _) in records.items():
        pos, stack = code.offset(code_ref), []
        for _ in range(n_code):
            if pos + 4 > len(blob):
                break
            word = struct.unpack_from("<I", blob, pos)[0]
            pos += 4
            op = word >> 24
            if op == 1:  # push constant (number or string reference)
                stack.append(("int", struct.unpack_from("<I", blob, pos)[0]))
                pos += 4
            elif op == 2:  # push object id
                stack.append(("gid", struct.unpack_from("<2I", blob, pos)))
                pos += 8
            elif op in (0x15, 0x1A):  # jumps
                pos += 4
            elif op == 0x10 and stack and stack[-1][0] == "gid":  # read member
                values = records.get(stack.pop()[1], (0, 0, ()))[2]
                member = word & 0xFF
                stack.append(("int", values[member]) if member < len(values) else ("?", None))
            elif op in (3, 4):  # calls: arguments, method name, object
                # global calls count the method name as an argument, object calls do not
                used = (word & 0xFF) + (1 if op == 3 else 2)
                if len(stack) >= used >= 2 and stack[-2][0] == "int":
                    yield script, params.string(stack[-2][1]), stack[-used:-2]
                del stack[-used:]


def manuscript_pages(game, strings):
    """{gid hash: (number, page name, nightmare only)} from cid_notebook_page.bin."""
    cid = game.read("global/cid_notebook_page.bin")
    pages = {}
    for i in range(struct.unpack_from("<I", cid, 8)[0]):
        # GID, name ref, text GID, episode, number, Nightmare flag: 29 bytes
        kind, gid, name_ref, _, _, _, number = struct.unpack_from("<7I", cid, 16 + 29 * i)
        if kind != 0x23:
            raise SystemExit("unexpected cid_notebook_page.bin layout")
        pages[gid] = (number, strings.string(name_ref), bool(cid[16 + 29 * i + 28]))
    return pages


def catalog(game):
    """{category: {"total": n, "items": {number: {(level, object path)}}, ...}}"""
    cats, strings = categories(game)
    pages = manuscript_pages(game, strings)
    names = gid_names(game)
    result = {name: {"total": total, "items": collections.defaultdict(set)}
              for name, total in cats.values()}
    result[PAGES] = {"total": len(pages), "items": collections.defaultdict(set),
                     "labels": {n: name for n, name, _ in pages.values()},
                     "nightmare": {n for n, _, hard in pages.values() if hard}}
    level_bundle = re.compile(r"worlds/[^/]+/(episodes|levels)/[^/]+/[^/]+\.bin$")
    for path in sorted(game.files):
        if not level_bundle.match(path) or Path(path).name.startswith("cid_"):
            continue
        bundle = read_bundle(game.read(path))
        if "dp_bytecode.bin" not in bundle or len(bundle["dp_bytecode.bin"]) < 28:
            continue
        code = Dp(bundle["dp_bytecode.bin"])
        params = Dp(bundle["dp_bytecodeparameters.bin"])
        records = script_records(bundle, code, names)
        for script, method, args in script_calls(code, params, records):
            if method == "UnlockCollectible":
                for first, second in zip(args, args[1:]):
                    if first[0] == "gid" and first[1][0] == 0x35:
                        category, number = cats[first[1][1]][0], second[1]
                        break
                else:
                    raise SystemExit(f"unreadable UnlockCollectible call in {names[script]}")
            elif method == "AddNoteToInventory":
                gids = [a[1][1] for a in args if a[0] == "gid" and a[1][0] == 0x23]
                if not gids:
                    continue
                category, number = PAGES, pages[gids[0]][0]
            else:
                continue
            parts = [p for p in names[script].split("::") if p not in ("prefab", "PublicInterface")]
            result[category]["items"][number].add((parts[0].lstrip("#"), "::".join(parts[1:])))
    return result


def read_profile(path):
    """{category: set of found numbers} from the profile `config`."""
    data = Path(path).read_bytes()
    pos = data.find(struct.pack("<I", 11) + b"CANPYRAMIDS") - 1
    if pos < 0:
        raise SystemExit(f"{path}: collectible block not found")
    # Two lists of manuscript page numbers sit right before the collectible block
    # (what distinguishes them is unknown).
    page_lists, end = [], pos
    for _ in range(2):
        for n in range(min(end - 1, 255), -1, -1):
            values = data[end - n:end]
            if data[end - n - 1] == n and all(a < b for a, b in zip(values, values[1:])):
                break
        else:
            raise SystemExit(f"{path}: manuscript page lists not found")
        page_lists.append(set(values))
        end -= n + 1
    if page_lists[0] != page_lists[1]:
        print("WARNING: the two manuscript lists differ; counting a page found in either")
    found, count, pos = {PAGES: page_lists[0] | page_lists[1]}, data[pos], pos + 1
    for _ in range(count):
        length = struct.unpack_from("<I", data, pos)[0]
        name = data[pos + 4:pos + 4 + length].decode("ascii")
        pos += 4 + length
        n = data[pos]
        found[name] = set(data[pos + 1:pos + 1 + n])
        pos += 1 + n
    return found


def string_table(game, lang):
    """{string id: text} of one language."""
    table = game.read(f"locale/{lang}/string_table.bin")
    strings, pos = {}, 4
    for _ in range(struct.unpack_from("<I", table)[0]):
        n = struct.unpack_from("<I", table, pos)[0]
        key = table[pos + 4:pos + 4 + n].decode("ascii")
        pos += 4 + n
        n = struct.unpack_from("<I", table, pos)[0]
        strings[key] = table[pos + 4:pos + 4 + 2 * n].decode("utf-16le")
        pos += 4 + 2 * n
    return strings


def episodes(game, lang):
    """{level id: (sort key, episode title, chapter title)} from menu.xml and strings."""
    strings = string_table(game, lang)
    menu = game.read("globaldb/menu.xml").decode("utf-8", "replace")
    levels = {}
    for block in re.finditer(r'<submenu name="episode(\d)"(.*?)</submenu>', menu, re.S):
        episode = int(block[1])
        for key, level in re.findall(r'text="(NAME_LEVEL_\d+)"[^>]*?:(level_\d+)"', block[2]):
            title = strings.get(f"NAME_EPISODE_{episode}", f"Episode {episode}")
            levels[level] = ((episode, level), title, strings.get(key, key))
    for i, level in ((1, "level_100"), (2, "level_200")):
        title = strings.get(f"NAME_EPISODE_SPECIAL_{i}", f"DLC {i}")
        levels[level] = ((6 + i, level), f"DLC {i}: {title}", "")
    return levels


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--config", required=True, help="profile file `config` (use a copy)")
    ap.add_argument("--data", default=DEFAULT_DATA, help="game data directory")
    ap.add_argument("--lang", default="en", help="language of episode and chapter names")
    ap.add_argument("--all", action="store_true", help="list found items too")
    ap.add_argument("--json", help="also write the full catalog with found flags")
    args = ap.parse_args()

    game = Archives(args.data)
    items, levels, found = catalog(game), episodes(game, args.lang), read_profile(args.config)

    print("Totals (found / total)")
    rows = []
    for category, info in sorted(items.items()):
        have = found.get(category, set())
        known = set(info["items"])
        note = ""
        if len(known) != info["total"]:
            note = f"   WARNING: located {len(known)} of {info['total']}"
        if have - known:
            note += f"   WARNING: profile numbers unknown to the game data: {sorted(have - known)}"
        print(f"  {category:30} {len(have):3} / {info['total']:3}{note}")
        hard = info.get("nightmare", set())
        if hard:
            print(f"    {'any difficulty':28} {len(have - hard):3} / {info['total'] - len(hard):3}")
            print(f"    {'Nightmare only':28} {len(have & hard):3} / {len(hard):3}")
        for number, places in info["items"].items():
            places = sorted(places)
            level = places[0][0]
            rows.append(dict(category=category, number=number, level=level,
                             found=number in have, objects=[p for _, p in places],
                             label=info.get("labels", {}).get(number),
                             nightmare=number in info.get("nightmare", ())))
    rows.sort(key=lambda r: (levels.get(r["level"], ((99, r["level"]),))[0], r["category"], r["number"]))

    episode = None
    for level in dict.fromkeys(r["level"] for r in rows):
        _, title, chapter = levels.get(level, (None, level, ""))
        if title != episode:
            episode = title
            sub = [r for r in rows if levels.get(r["level"], (None, r["level"]))[1] == title]
            print(f"\n=== {title}")
            for category in sorted({r["category"] for r in sub}):
                these = [r for r in sub if r["category"] == category]
                hard = [r for r in these if r["nightmare"]]
                easy = [r for r in these if not r["nightmare"]]
                extra = (f"   (+ {sum(r['found'] for r in hard)} / {len(hard)} Nightmare only)"
                         if hard else "")
                print(f"  {category:30} {sum(r['found'] for r in easy):3} / {len(easy):3}{extra}")
        shown = [r for r in rows if r["level"] == level and (args.all or not r["found"])]
        if shown:
            print(f"  -- {chapter or level} [{level}]")
            for r in shown:
                mark = ("[x] " if r["found"] else "[ ] ") if args.all else ""
                name = r["label"] or f"{r['category']} #{r['number']}"
                tag = NIGHTMARE if r["nightmare"] else ""
                print(f"     {mark}{name}{tag}: {' | '.join(r['objects'])}")

    if args.json:
        Path(args.json).write_text(json.dumps(rows, indent=1))


if __name__ == "__main__":
    main()
