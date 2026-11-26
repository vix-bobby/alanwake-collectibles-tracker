# Alan Wake collectibles

For the **original Alan Wake (2010) on PC, not Alan Wake Remastered**. Tested only with the
Steam version, on Windows and on Linux (Steam Deck).

- `mod/`: an in-game mod showing which collectibles are left, per chapter and episode, and
  optionally marking the nearest one on the game's radar. See [mod/README.md](mod/README.md).
- `tools/`: a script listing every collectible you are missing, from your profile file.
- `docs/`: [how the game works](docs/README.md): its data, profile, program and radar.

Both only read the game's files and your profile. No game files are included; the mod's item
list holds a few short names taken from the game (episodes, chapters, manuscript page titles,
cardboard cutouts), and the radar markers use the game's object numbers for the items.

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

The profile file `config` lists the numbers of the items found, per category; which items exist
comes from the game's data. Details: [docs/game-data.md](docs/game-data.md) and
[docs/profile.md](docs/profile.md).

## Regenerating what is not in the repository

- Reports and your own files: `reports/` and `samples/`, as above.
- The mod's item list `mod/src/catalog.h` and radar objects `mod/src/radar_targets.h`:
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
