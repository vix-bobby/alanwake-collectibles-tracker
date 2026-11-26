# The program

## Supported build

`AlanWake.exe` of Steam build 14596864, SHA-256
`ba208225a94eccfd7de4452dfb4df8ca9e60885840edc76a22931416cdbe282e`. It is 32-bit, has no
SteamStub and no ASLR (base 0x400000), so addresses are fixed. The mod compares the code it
relies on before changing anything.

## Loading a DLL

- Direct3D 9: `d3d_sf_Win32.dll` loads `D3D9.DLL` by bare name, so a `d3d9.dll` in the game
  folder is loaded instead of the system's. The game ships no file of that name.
- The game probes Direct3D, calls `FreeLibrary`, then uses it again. A proxy with hooks must
  pin itself, or the game crashes at launch.
- On Windows, `apphelp.dll` can hook Direct3D on top of a proxy on a first launch; the mod once
  recursed in `CreateDevice` because of it.
- Other possible proxy names (static imports), not tried: `xinput1_3.dll` (exe ordinals 2, 3;
  app_sf 2), `dbghelp.dll` (exe: `MiniDumpWriteDump`), `winmm.dll` (binkw32, fmodex),
  `d3dx9_43.dll` (exe and engine DLLs).
- Engine DLLs export drawing and string functions (such as `rend::drawText`).
- Things to watch with a `d3d9.dll` proxy: other tools using the same name (ReShade, DXVK),
  device reset, and exclusive fullscreen.

## Menus

- Labels are read once at load through `loc::StringTable::getStringById` of `loc_sf_Win32.dll`
  (IAT slot 0x62e888; return addresses 0x419d51 for items, 0x41996a for submenu titles) and
  copied into the menu items.
- Method table slots: 0x633634 `MenuMode::render` (called every frame before anything is
  measured or drawn), 0x632fb0 `MenuCommand::draw` (one list entry). How the mod uses them, and
  the object offsets, are in `mod/src/menu.c`.
- Menu conditions and commands are a fixed set with no way to print a number, so a mod made of
  data files only could only show static text. Adding entries to the settings menu would need
  more reverse engineering.
