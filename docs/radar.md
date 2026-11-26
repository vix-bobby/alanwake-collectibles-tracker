# The radar

How the game's radar works and how the mod puts a marker on it (`mod/src/radar.c`). From static
analysis of `AlanWake.exe` Steam build 14596864 (SHA-256 `ba208225...282e`, base 0x400000, no
ASLR), `rl_sf_Win32.dll` and `loc_sf_Win32.dll`.

Marks: (v) seen in the disassembly or data; (i) inferred; (?) not established.

## 1. Handlers and data flow

- (v) HUD singleton: `HUDMode*` at `[0x6bf9c8]` (getter 0x4318e0). Set by the constructor
  0x432a60.
  - Vtable 0x634ea0 (RTTI `.?AVHUDMode@@`): slot 6 (+0x18, at 0x634eb8) is `update` 0x4327c0
    (thiscall, no arguments, `ret`); slot 7 (+0x1c) is `render` 0x432160.
  - The message receiver is the sub-object at HUD+0xc. Its vtable is 0x634e94; slot 0 is the
    dispatcher 0x432e50 (thiscall, `Message*`, `ret 4`). The dispatcher compares the message name
    with `_stricmp` (0x43a470).
  - Argument readers on the message: 0x43a540 dword or string, 0x43a550 bool, 0x43a530 float,
    0x43a570 `GlobalID*` (two slots).
- (v) The AddMapEntity handler is **0x432990** (0x433211 is only the `push "AddMapEntity"` in
  that dispatcher):
  ```
  void __thiscall hud_add_map_entity(HUDMode *hud /*unused*/, const GlobalID *gid, int sprite,
                                     const char *text_id, bool fade_sprite, bool fade_text);
  // ret 0x14. The script argument order is (entity, text, sprite, fade, fade); the exe swaps
  // text and sprite on the stack.
  ```
  - It accepts GID classes 0x54, 0x55, 0x63, 0x64, 0x71, 0x90, 0x95, 0x9a and 0xa0 (decimal
    84, 85, 99, 100, 113, 144, 149, 154, 160). Other classes are ignored silently.
  - It then calls `GlobalIDMap::getInstance()->getPointer(gid)` and the virtual slot +0x1c of the
    result, which gives a `GameObject*`. **There is no NULL check on `getPointer`** (`mov edx,[eax]`
    at 0x432a16). `getPointer` returns NULL for an unregistered GID (rl 0x10024160, miss path at
    0x100241e5), so a GID that is not loaded crashes the game.
  - Finally it calls `Radar::addMapEntity` 0x586410 on `[0x6e8ce8]` (getter 0x583c40), with
    `(obj, gid, &identity_matrix, sprite, text, fade_sprite, fade_text)`, `ret 0x1c`.
- (v) RemoveMapEntity: `Radar::removeMapEntity` 0x584940 (thiscall, `const GlobalID*`, `ret 4`).
  It only clears the entry's active flag (+0xd4); the entry fades out and is erased later.
  - Also called by the game-object teardown 0x4c8030 with the object's own GID, before
    `unregisterObjectIDs`.
- (v) SetDestinationEntity is handled separately: 0x5852b0 (`ret 0x10`) writes the target handle
  to radar+0x120 and its other fields to +0x10c..+0x138, never to the entry array.
  - Call references confirm it: entries are pushed only from 0x58659b (add) and 0x58688a (load);
    the array is resized only from the constructor, destructor, reset and load; entries are
    erased only by update.
- (v) **Implemented on PC and live.**
  - The radar entry array: `[radar+0]` base, `[radar+4]` count, `[radar+8]` capacity, 0xE0 bytes
    per entry.
  - Entry fields:

    | Offset | Field |
    |---|---|
    | +0 | spawn-counted GameObject handle |
    | +4 | GlobalID (8 bytes) |
    | +0xc | 4x3 float world transform; position at +0x30 |
    | +0x3c | `r::String` text id |
    | +0xd0 | fade |
    | +0xd4 | active |
    | +0xd8 | sprite |
    | +0xdc | fade_sprite |
    | +0xdd | fade_text |

  - Adding a GID that is already present updates that entry in place; it does not reactivate an
    entry that is fading out.
  - The array is only written by add (0x586410, push_back 0x585e80), load (0x5866b0), and
    resize/clear (0x585d90 from the constructor, destructor and reset 0x586630).
  - `Radar::update` 0x5865f0 is called from `HUDMode::update` at 0x4328b8. It calls 0x585bb0
    **every frame**:
    - It checks each handle against `GameObject::sm_aInstanceSpawnCounts` and
      `sm_aInstancePointers` (rl exports).
    - If the handle is valid, it copies the object's current transform (virtual slot +4), so the
      marker **follows the entity**.
    - If the handle is invalid (object destroyed or unloaded), it clears the handle and active
      flag, fades the entry out and erases it (0x5858c0).
    - **No dangling pointers.**
  - `Radar::render` 0x5853e0 is called from `HUDMode::render` at 0x43234f. It always calls the
    map-entity drawer 0x585020: a text pass through `loc::StringTable::getStringById(text)`
    (0x5845b0), then a sprite pass (0x584360 → 0x583dd0).
  - Positions are relative to the active camera: 0x43eb60(0), then 0x43b310 = camera+0x4dc,
    position at +0x24. They are clamped to the radar edge when the fade flag is 0 (i).
- (v) Sprites: `data\textures\hardcoded\radar.tex` is raw 512x512 BGRA with a 32-byte header,
  16x16 cells of 32 px; cell = index % 16, row = index / 16.
  - Row 0: 0 yellow dot, 1 green dot, 2 red dot, 3 blue dot, 4 grey dot, 5 "?" diamond,
    6 target, 7 skull, 8 yellow ⌘, 9 green diamond, 10 red diamond, 11 blue cross, 12 grey
    square, 13 gear.
  - 14 and 15 are the objective markers the game uses itself; 16 and 17 are the height chevrons.
- (v) Text: an empty or NULL id gives `L""` and no label (checks at 0x58653c and 0x5845c3; loc
  0x10004a9f). A real string-table id prints that string.
- (v) No shipped level script calls AddMapEntity or RemoveMapEntity. Every script call in every
  level bundle was decoded with the `tools/collectibles.py` walker. SetDestinationEntity is used
  465 times (classes 113, 85, 84, 99, 125, 144).
- Radar draw conditions:
  - (v) The HUD map flag HUD+0x18 must be set (HideMap clears it; reset sets it).
  - (v) The HUD fade `[HUD+0x4c]` must be non-zero (FadeIn/FadeOutHUD).
  - (v) `*(u8*)([0x76c5d8] + 0x144)` must be set. (i) This is probably the HUD option in the video
    settings: the string `MENU_VIDEO_PC_HUD` exists. (?) Not tied down.
  - (?) Not established: when the radar is visible during normal play.
- (v) Reset: the world reset 0x578760 calls `Radar::reset` 0x586630, which resizes the array to 0.
  `Radar::load` also starts with a reset. After any load or restart the markers are gone and must
  be re-added.

## 2. From a collectible to the handle

- (v) `r::GlobalID` = `{u32 class; u32 id;}`: constructor (II) at rl 0x1001c970; the destructor is
  a bare `ret`.
  - The registry (`.../GIDRegistry.txt`, `class(decimal),id(hex),path,guid`) gives these values
    directly. They are the same pairs that script bytecode pushes (`collectibles.py` relies on
    this).
- (v) A collectible's **mesh** child (class 100) is accepted. Examples:
  - `...dlc1_collectible_alarm_clock_000::prefab::AlarmClock`
  - `...cutout_weaver_000::prefab::Sign::cutouts_weaver_000`
  - Or the class 113 `Player_Waypoint` that cutouts have.
  - The prefab root (class 90) is **not** accepted.
- Runtime lookup: `GlobalIDMap::getInstance` (rl export `?getInstance@GlobalIDMap@r@@SIPAV12@XZ`,
  fastcall, IAT 0x62f784) and `getPointer` (`?getPointer@GlobalIDMap@r@@QBEPAXABVGlobalID@2@@Z`,
  thiscall, `ret 4`, IAT 0x62f7e8). `getPointer` takes a critical section, so it is thread-safe.
  NULL means the object is not loaded.

## 3. Loading, unloading, pickup

- (v) The episode is split into group bundles (`level_100/G01_bright_falls.bin`, `G02_forest.bin`,
  `G03_sawmill.bin`, `G04_endfight.bin`, ...). The alarm clock mesh record is in
  `G02_forest.bin/cid_dynamicobject.bin`.
- (i) Objects exist only while their group is loaded, so "loaded" roughly means "in this part of
  the chapter". That is good enough for "nearby".
- (?) When the groups load and unload, and whether PermanentContent is always loaded.
- (v) Unload or destroy: the handle becomes invalid; the entry fades out and is erased. Safe.
- (v) Pickup: the alarm clock script only runs `FadeOut`, `Hide(1)` and `UnlockCollectible`. The
  object is not destroyed, so **the marker would stay after pickup**. The mod must remove it when
  the profile shows the item as found. Profile updates may lag until the game writes `config`.

## 4. Threads

- (v) WinMain 0x407f30 calls the game main 0x407500 directly. It names the main thread
  "RMD - Game Thread" and registers it as thread id 1.
- (v) Frame 0x405950 calls mode update 0x428530 (slot 6 of each mode), then mode render 0x4292c0
  (slot 7).
- (v) Other threads: loader, streamer, game job, physics; the renderer DLL only adds a
  tessellator thread.
- Present: (i) from the code; in game, HUD update, presented frames and saves were on one thread.

## 5. Saves (the main risk)

- (v) `Radar::save` 0x5841e0 (thiscall, `FileStream*`, `ret 4`) is called once, at
  **0x48355e** (`e8 7d 0c 10 00`). That call is in the world save 0x483420, between
  "CharacterManager" and "ItemFactory"; the chain is 0x427de0 → 0x40ca70 → 0x483420.
  - It writes `[radar+4]` and then every entry: handle, GID, transform, text, fade, active,
    sprite, flags.
  - The matching load is 0x5866b0 (called at 0x483da5).
  - **A marker present at a checkpoint goes into `savegame.aws`.** Because only the mod adds
    entries, the fix is to wrap that call: count = 0, call, restore.
  - With the wrapper, the save bytes are the same as the game writes today.
- (?) Not checked: how `Radar::load` behaves when a saved GID is not loaded. (i) The handle is 0,
  so the entry fades out and is erased.

### Callers of the save

- (v) The only caller of `Radar::save` 0x5841e0 is 0x48355e. The world save 0x483420 is called
  only from 0x40ca70. 0x40ca70 is called only from 0x427de0, and 0x427de0 only from 0x474e72.
- (v) 0x474c50, the savegame writer, builds a `FileStream` and writes the header, then runs the
  chain above. Its callers:
  - 0x408930, called from mode update 0x428530 (at 0x4291a9);
  - 0x4751c0 (to a file), called from `GameMode::update` 0x40d580 (GameMode vtable slot 6);
  - 0x4750a0 (to a `BufferedMemoryFile`, under a mutex), called from 0x475820 and 0x475170.
    - 0x475820 is called from `GameMode::update`.
    - 0x475170 is called from 0x475750, and 0x475750 is called from 0x40f2b0, which is slot 0 of
      GameMode's second vtable 0x63279c (its message handler).
  - So every save that writes the radar goes through 0x48355e.
- (v) Mode update and `GameMode::update` run in the main-thread frame (see section 4).
  (i) The message handler path probably does too. **(?)** Not proven: the mod checks the
  thread at run time.
- (v) Nothing else writes radar entries: there are no other external references to the
  entry-array functions. The array destructor 0x584890 is only reached through exception
  unwinding.
- (v) Radar::save copies `[radar+4]` and writes it as an int, then writes that many entries.
  With the count at 0 it writes the int 0 and no entries.
  - Without the mod the count is always 0, because nothing else adds entries. So the guarded
    save is byte for byte what the game writes without the mod.
  - The other radar fields written (destination, +0x13c..+0x29c, +0x120) are not touched by
    AddMapEntity (0x586410 writes only the entry and the array).
- (v) Side effect of a marker: when an entry finishes fading in or out, update 0x585bb0 calls
  0x53d230 or 0x53d250 on the singleton at 0x6e02cc (i: UI sounds), but only if the sprite is not
  0. The mod uses sprite 0 (yellow dot), so this is skipped. (i) Neither is saved anyway.
- (v) Text NULL: the entry's `r::String` is resized to 0 and freed when the entry is erased. The
  handle is a plain number with no reference count.

## 6. Positions

- **Radar centre** (v). `Radar` 0x584360 and 0x5845b0 call 0x43eb60(0), then 0x43b310 (camera
  + 0x4dc, a 4x3 float transform), + 0x24. That is camera + 0x500 (0x43b300 returns exactly that
  pointer). It is the position the dots are placed relative to.
  - 0x43eb60 reads the state `[0x6c0894]`; states above 7 return NULL. Its case table maps
    0→0x6c0898 (with argument 0), 1/2/6→0x6c08a0, 3→0x6c08a4, 4/5→0x6c089c, 7→0x6c08ac.
  - The mod repeats that choice with plain memory reads and calls nothing. The code bytes, the
    case table and the state-0 branch at 0x43eb71 (`84 c9 74 56`) are part of the build check.
  - Once stopped, the mod also stops calling into game objects.
  - (i) The camera follows Alan a few metres away. Since the radar is centred on the camera,
    "nearest" uses the same centre as the dots.
- **Distance**: on the ground plane (x, z), since the radar is flat.
- **An object's position without a marker** (v). It uses exactly the calls the game's own radar
  code makes:
  1. `p = GlobalIDMap::getPointer(gid)`, then `go = p->vtbl[7](p)`: as in AddMapEntity
     0x432a16..0x432a1d. No arguments; NULL is checked.
  2. `m = go->vtbl[1](go)`: as in the radar update 0x585d38..0x585d3d. No arguments.
  3. Position at `m + 0x24`: 0x40acf0 is a plain 48-byte copy into entry+0xc, and entry+0x30 is
     the position seen in game.
  - The method pointers are read with `ReadProcessMemory` and checked for NULL. The position
    must be finite and within ±1e6.
  - (i) Class 113 is proven in game, through the radar. Class 100 follows the same path
    (alarm clock meshes).

## 7. Script route (ep999 archive): rejected

It needs a rebuilt level bundle that replaces a shipped one (the mod only adds files), has
the same save problem, cannot know what has been found unless the script calls
`IsCollectibleUnlocked`, and the bytecode writer is only partly understood.
