# Alan Wake collectibles tracker (version 0.7.1)

For the **original Alan Wake (2010), not Alan Wake Remastered**. Tested only with the Steam
version, on Windows and on Linux (Steam Deck).

A `d3d9.dll` that the game loads by itself. No game file is changed.

- **Overlay** (bottom left, above the subtitles; see `[position]` in the ini): found / total for the current chapter and, in brackets, its whole
  episode; Nightmare-only pages apart. "Current" means **the chapter of your last checkpoint**
  (read from `savegame.aws`), so it follows when the game saves. The counts follow your
  profile. **F8** shows or hides it.
- **Menu counts:** the episode list shows what is left per type; once an episode's chapter
  list is open, its title shows the total only, e.g. `(N left, +N Nightmare)`, and each
  chapter its own types. Counts follow your profile while the game runs. **F9** shows or
  hides them at once.
  - `menu_small_text=1` (default) draws the counts in lists smaller than the names. Chapter
    list titles stay in normal size. `0` writes everything in the normal size.
  - `menu_detail=0` shows totals everywhere.
  - `menu_hooks=0` leaves the game's menus completely alone.
- **Settings:** `alanwake_collectibles.ini`, created next to the game at the first start, with
  comments. Edits apply at the next game start; deleting the file restores the defaults.

  | Section | Name | Default | Meaning |
  |---|---|---|---|
  | `[options]` | `overlay` | 1 | show the overlay |
  | | `menu_counts` | 1 | show counts in the menus |
  | | `menu_detail` | 1 | list each type left in menu lists; 0 = totals only |
  | | `menu_small_text` | 1 | smaller counts in menu lists; 0 = normal size |
  | | `menu_hooks` | 1 | 0 = leave the menus untouched |
  | `[keys]` | `overlay`, `menu_counts` | F8, F9 | F1 to F24, or `none` |
  | | the three other option names | none | same |
  | `[position]` | `corner` | bottom-left | or top-left, top-right, bottom-right |
  | | `margin_x`, `margin_y` | 1, 9 | distance from the edges, % of the screen (0 to 90) |

  A key switches the option of the same name and saves the choice. It acts at once, except for
  `menu_small_text` and `menu_hooks` (next start). F5 is the game's default manuscript key.
- **Log:** `alanwake_collectibles.log`, next to the game (or in `%TEMP%`).

Install: copy `build/d3d9.dll` next to `AlanWake.exe` (or `./install.sh`). If another mod's
`d3d9.dll` is already there, first rename it to `d3d9_next.dll`: it is then loaded behind this
one.

Remove: delete `d3d9.dll`, the log and the ini there (or `./install.sh remove`), then rename
`d3d9_next.dll`, if you made one, back to `d3d9.dll`.
Steam's "verify integrity" does not remove added files.

If the game crashes in menus: set `menu_small_text=0`, or `menu_hooks=0`, in the ini. If it crashes at startup: delete `d3d9.dll`.

The mod stays loaded once loaded: the game probes Direct3D, calls `FreeLibrary`, then uses
it again, and hooks pointing into an unloaded DLL crashed it at launch (fixed in 0.5.1). Any
other loader file name would need the same.

Licence: MIT (`../LICENSE`). Include that file when sharing the DLL or the source.

Build (Linux or WSL): `./build.sh` (needs `g++-mingw-w64-i686`). `./build.sh test` also runs
the tests and needs WSL, because some are Windows programs. After the collectible catalog
changes: `python3 -I gen_catalog.py <report.json>` (needs the game installed). Release zip:
`./package.sh`.

Supported game program for the menu counts: `AlanWake.exe` of the Steam build dated 2020-05-15
(SHA-256 `ba208225a94eccfd7de4452dfb4df8ca9e60885840edc76a22931416cdbe282e`). The hooks and the addresses they rely
on are described at the top of `src/menu.c`; on any other build they are not installed. The
overlay does not depend on it.

Known limits: Steam only (the profile is found through Steam); English text; long chapter lines
may not fit narrow screens; the small-text alignment was measured at 2560x1440; the automated
Direct3D test runs windowed only and without the Steam overlay.
