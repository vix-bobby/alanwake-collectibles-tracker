# Game data

As read by the scripts in `tools/` and `mod/gen_catalog.py`, which are the exact reference.
Integers are little-endian unless noted. Fields not described here are unidentified or unused.

## Archives (`data/ep999-*`, RMDP v2)

Each archive is a pair: `.bin` is the index, `.rmdp` holds the file contents.

- Index: u8 byte order (1 = big-endian, for the fields below), u32 version (2), u32 folder
  count, u32 file count, u32 name block size (read as big-endian). Records start at offset 145;
  the name block (zero-terminated strings) is at the end of the file.
- Folder record, 28 bytes: seven u32; the third is the parent folder, the fifth the name offset.
  `0xFFFFFFFF` means none.
- File record, 40 bytes: five u32 (third: folder, fifth: name offset), u64 offset in `.rmdp`,
  u64 size, u32 CRC-32.
- Some entries store the checksum byte-swapped. A few English string tables fail it either way
  (cause unknown).
- `global/cid_collectible.bin`, `cid_radio_show.bin` (contents not studied) and
  `cid_notebook_page.bin` are in `ep999-000`; a `GIDRegistry.txt` is in `ep999-003`.
- A path in a later archive (by file name) wins. Level scripts still use object ids that
  patched registries dropped, so read every version of `GIDRegistry.txt`.
- The game loads every `*.rmdp` in the folder, and archives win over loose files. A new
  `ep999-008` should therefore override existing paths (i). No integrity check was found.

## Resource bundles (`*.bin` inside the archives)

u32 file count; per file: u32 name length, ASCII name, u32 size. Then one zlib stream holding
all the files in that order.

- Level bundles: `worlds/*/{episodes,levels}/*/*.bin`. An episode can be split into group
  bundles, loaded as needed.
- `cid_*.bin` files are tables of objects of one type.
- Object positions: `_export_info_log.xml` gives only a grid cell (`cell_x`, `cell_z`) per
  entity. Exact positions are in `cid_dynamicobject.bin` behind data references; decoding them
  has not been done.

## `dp_*.bin`: referenced data

The u32 at offset 8 is the size of the data section at the end of the file. A reference points to
`section start + (ref >> 8) * 8`, plus 4 if bit `0x80` is set. Strings are zero-terminated.

## Object ids

An object id (GID) is a pair `{u32 class, u32 id}`. `GIDRegistry.txt` lines are
`class (decimal),id (hex),path,guid`; the path names the level and the object.

## Scripts

- `cid_*script.bin` records, eight u32: class, id, instruction count, code reference, two
  others, member value count, member values reference. In the records used here the low seven
  bits of the code reference are `0x0B`.
- Bytecode is in `dp_bytecode.bin`; method names are in `dp_bytecodeparameters.bin`.
- Each instruction starts with a u32 whose top byte is the opcode. Those understood:

  | Opcode | Meaning | Extra data |
  |---|---|---|
  | `0x01` | push a number or string reference | u32 |
  | `0x02` | push an object id | two u32 |
  | `0x03` | global call; low byte + 1 stack values (the method name counts as an argument) | none |
  | `0x04` | object call; low byte + 2 stack values | none |
  | `0x10` | read a member of the object on the stack; low byte = member number | none |
  | `0x15`, `0x1A` | jumps | u32 |

- In a call, the method name reference is the second value from the top of the stack.
- Script functions about collectibles and the radar include `UnlockCollectible`,
  `IsCollectibleUnlocked` (hides items already found), `GetUnlockedCollectibleCount`,
  `UnlockRadio`, `AddNoteToInventory`, `SetDestinationEntity`, `AddMapEntity` and
  `RemoveMapEntity` (the last two are used by no shipped script; see [radar.md](radar.md)).
  `messages/message_defines.xml` declares the radar messages (receiver `HUD`). The executable
  also contains the string `ChestFound`.

## Collectibles

- **Categories and totals:** `global/cid_collectible.bin` (u32 count at offset 8). A record
  holds `0x35` (the GID class), id, name reference, another u32 and the total. Names resolve in
  `global/dp_global.bin`.

  | Category | Items | Total |
  |---|---|---|
  | `THERMOS` | coffee thermoses | 100 |
  | `CANPYRAMIDS` | can pyramids | 12 |
  | `CHESTS` | chests | 30 |
  | `RADIOSHOWS` | radio shows | 11 |
  | `TVSHOWS` | TV shows | 14 |
  | `SIGNS` | signs | 25 |
  | `COCONUTSONGS` | coconut songs (come with any playthrough) | 2 |
  | `DLC1_ALARMCLOCKS` | alarm clocks | 10 |
  | `DLC1_CARDBOARDCUTOUTS` | cardboard cutouts | 6 |
  | `DLC2_NIGHTSPRINGSVIDEOGAMES` | Night Springs video games | 10 |

- **Items:** pickup scripts call `game:UnlockCollectible(category, number)`. The category is a
  GID of class `0x35`; the number is a constant or a member value of a script object.
- Thermos numbers run 1 to 101 with no 90.
- **Manuscript pages:** `global/cid_notebook_page.bin` (u32 count at offset 8) has 29-byte
  records from offset 16: class `0x23`, id, name reference, text GID (two u32), episode, number,
  and a u8 Nightmare-only flag. There are 106 pages, numbered 0 to 105, 15 of them
  Nightmare-only. Scripts hand them out with `AddNoteToInventory(page)`.

## Episodes, levels and strings

- `globaldb/menu.xml` maps `level_NN` to episodes (`<submenu name="episodeN">`). The two
  special episodes are `level_100` and `level_200`.
- `locale/<language>/string_table.bin`: u32 count; per string: u32 key length, ASCII key, u32
  character count, UTF-16LE text.
- Episode and chapter names: `NAME_EPISODE_n`, `NAME_EPISODE_SPECIAL_n`, `NAME_LEVEL_n`.
- Page titles: `LEVEL_xx_PAGE_nn`, where `nn` is the number within the chapter.
- Cutout names: the first line of `cutout_<name>` (the examine text).
- No per-item name was found for the Night Springs video games in the English table.
- `locale_config.xml` maps languages to folders. Which language pack the game actually loads
  is not confirmed.
