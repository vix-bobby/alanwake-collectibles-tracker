# How the game works

Reverse engineering notes on the original Alan Wake (2010) on PC, Steam version.

| File | Contents |
|---|---|
| [game-data.md](game-data.md) | data archives, bundles, scripts, collectibles, strings |
| [profile.md](profile.md) | the profile file `config` and the checkpoint `savegame.aws` |
| [executable.md](executable.md) | the program: loading a mod, supported build, menus |
| [radar.md](radar.md) | the radar, game object ids, positions, saving |

Addresses belong to one build of `AlanWake.exe`, described in [executable.md](executable.md).

Marks used in places: (v) seen in the disassembly or data; (i) inferred; (?) not established.

## Tools

In this repository:

- `tools/rmdp.py`: lists and extracts files from the data archives.
- `tools/unbin.py`: unpacks a resource bundle.
- `tools/collectibles.py`: lists collectibles from the game data and the profile.

Elsewhere:

- [AWTools](https://github.com/Nostritius/AWTools) (Python, GPL-3.0): the format reference for
  `rmdp.py` and `unbin.py`.
- [neat](https://github.com/tomevin/neat) (.NET 10): a possible alternative; only its README was
  read.
- [torchbearer](https://github.com/fvrlo/torchbearer): lists the archives as RMDP v2; only its
  README was read.

No published documentation was found for `.aws` files or `cid_collectible.bin`.
