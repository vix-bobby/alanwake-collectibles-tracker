# Profile and checkpoint

Both are in `<Steam>/userdata/<account number>/108710/remote/`. Under Proton, `<Steam>` is the
Linux Steam folder (see `mod/src/steam.c` for the places searched). Work on copies.

## `config`: what has been found

A small binary file (under 1 KB in the one examined). Parsers: `read_profile()` in
`tools/collectibles.py` and `mod/src/profile.c`.

- **Collectible block:** u8 category count; per category: u32 name length, ASCII name
  (see [game-data.md](game-data.md)), u8 item count, then that many u8 item numbers.
- The block's offset is not assumed: the parsers search for the name `CANPYRAMIDS` with its
  length; the count byte is just before that. This relies on it being the first category.
- **Manuscript pages:** two lists end where the block begins. Each is a u8 count followed by
  that many increasing page numbers, so the parsers locate them backwards. What distinguishes
  the two lists is unknown; a page found in either counts.
- No episode information is stored.
- (i) The game reads the file through `SteamRemoteStorage()->FileRead("config")` and probably
  writes it through `FileWrite`. It is not updated at the moment of a pickup, so anything that
  reads it can lag behind the game.
- The game's statistics menu agreed with these lists in the categories compared, and a picked up
  item appeared under its own number.
- Unidentified: level lists, possible cinematic ids, eleven u32 counters (one has a suspect
  field width).
- (?) That replaying episodes or playing on Nightmare only adds to the lists.

## `savegame.aws`: the current checkpoint

A snapshot of the world at the last checkpoint. It does not hold the list of found collectibles.

- It starts with two u32, then a u32 length and a string naming the level, such as
  `gameworld:level_05` or `dlc1_world:level_100` (`mod/src/checkpoint.c`).
- It also stores the radar's markers and (i) the difficulty (see [radar.md](radar.md)).
