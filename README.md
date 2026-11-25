# Alan Wake collectibles

For the **original Alan Wake (2010) on PC, not Alan Wake Remastered**. Tested only with the
Steam version, on Windows and on Linux (Steam Deck).

- `mod/`: an in-game mod showing which collectibles are left, per chapter and episode.
  See [mod/README.md](mod/README.md).
- `tools/`: a script listing every collectible you are missing, from your profile file.

Both only read the game's files and your profile. No game data or game files are included.

## Which collectibles am I missing?

Needs Python 3.8 or later, no packages, and the game installed. Work on a copy of your
profile, found at `<Steam>/userdata/<account number>/108710/remote/config`.

```sh
mkdir -p samples reports
cp "<Steam>/userdata/<account number>/108710/remote/config" samples/
python3 -I tools/collectibles.py --config samples/config > reports/missing.txt
```

Options: `--all` (found items too), `--lang fr` (episode and chapter names),
`--json reports/collectibles.json`, `--data DIR` (the game's `data` folder; the default is the
usual Steam location seen from WSL).

## How the game tracks collectibles

- **Profile** `config`: per category, a list of found item numbers. `savegame.aws` is only the
  current checkpoint; its first bytes name the level.
- **Categories and totals:** `global/cid_collectible.bin`; names resolve in `global/dp_global.bin`.
- **Items:** each level bundle (`worlds/*/{episodes,levels}/*/*.bin`) holds script objects
  (`cid_*script.bin`: id, instruction count, code ref, member values) and bytecode
  (`dp_bytecode.bin`). Pickup scripts call `game:UnlockCollectible(category, number)`; the
  number is a constant or a member value of the object.
- **Episodes:** `globaldb/menu.xml` maps `level_NN` to episodes.
- Thermos numbers run 1 to 101 with no 90, for 100 thermoses.
- **Manuscript pages:** `global/cid_notebook_page.bin` gives 106 pages (numbers 0 to 105) with
  a Nightmare-only flag (15 pages). Scripts hand them out with `AddNoteToInventory(page)`.
  `config` holds two lists of found page numbers just before the collectible block. What
  distinguishes them is unknown; both the script and the mod count a page found in either.

## Regenerating what is not in the repository

- Reports and your own files: `reports/` and `samples/`, as above.
- The mod's item list `mod/src/catalog.h`:
  `python3 -I mod/gen_catalog.py reports/collectibles.json`
- Test fixtures: `python3 -I mod/test/make_fixtures.py`
- Optional reference for the archive formats:
  `git clone https://github.com/Nostritius/AWTools vendor/AWTools` (GPL-3.0).
  `tools/rmdp.py` and `tools/unbin.py` follow the formats described by its code.

## Licence

[MIT](LICENSE), copyright vix-bobby, except `tools/rmdp.py` and `tools/unbin.py`, which follow
the formats as implemented by [AWTools](https://github.com/Nostritius/AWTools) and are under
[GPL-3.0 or later](tools/LICENSE.GPL-3.0). The other scripts that import them are MIT, but a
copy distributed together with those two files is subject to the GPL as a whole. The mod's
`d3d9.dll` and C source contain none of that code.
