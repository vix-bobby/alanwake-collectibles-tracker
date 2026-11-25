Alan Wake collectibles tracker 0.7.1
====================================

Shows which collectibles you still have to find, per chapter and episode.

FOR THE ORIGINAL ALAN WAKE (2010), NOT ALAN WAKE REMASTERED.
Tested only with the Steam version, on Windows and on Linux (Steam Deck).

REQUIREMENTS
- The original Alan Wake (2010) on Steam, on Windows or Linux (Steam Deck).
  Not Alan Wake Remastered.
- GOG and Epic versions are not supported: the mod finds your profile through Steam.
- The counts in menus need the current Steam version of AlanWake.exe (dated 2020-05-15).
  On any other version they switch themselves off.

INSTALL
1. Open the game folder. In Steam: right-click Alan Wake, Manage, Browse local files.
2. If there is already a d3d9.dll there (from another mod, such as ReShade), rename it to
   d3d9_next.dll. This mod will load it.
3. Copy this mod's d3d9.dll into the folder, next to AlanWake.exe.
4. Start the game as usual.
No game file is replaced.

UNINSTALL
1. Delete these from the game folder: d3d9.dll, alanwake_collectibles.ini and
   alanwake_collectibles.log.
2. If you renamed another mod's file to d3d9_next.dll, rename it back to d3d9.dll.
Steam's "verify integrity of game files" does not remove the mod's files.

IN GAME
- Overlay, bottom left: found / total for the chapter of your last checkpoint, and in
  brackets for its whole episode. F8 shows or hides it.
- Episode and chapter menus: what is left to find, updated as you play. F9 shows or hides it.
- Nightmare-only manuscript pages are counted separately.
- The mod's text is in English, whatever the game's language.

SETTINGS (alanwake_collectibles.ini)
The file is created in the game folder, next to AlanWake.exe, the first time you start the
game with the mod. Open it with Notepad. Edits apply the next time you start the game.
Delete the file to restore the defaults.

[options]          1 = on, 0 = off
  overlay          1   Show the overlay.
  menu_counts      1   Show counts in the episode and chapter menus.
  menu_detail      1   Menu lists show each type left to find; 0 shows totals only.
  menu_small_text  1   Counts in menu lists are smaller than the names; 0 = normal size.
  menu_hooks       1   0 leaves the game's menus completely untouched (no menu counts).

[keys]             F1 to F24, or none
  overlay          F8
  menu_counts      F9
  menu_detail      none
  menu_small_text  none
  menu_hooks       none
Each key switches the option of the same name and saves your choice in the file. It acts at
once, except for menu_small_text and menu_hooks, which apply at the next start. F5 is the
game's default manuscript key, so avoid it.

[position]         where the overlay sits
  corner           bottom-left   Or top-left, top-right, bottom-right.
  margin_x         1             Distance from the side, in % of the screen width (0-90).
  margin_y         9             Distance from top or bottom, in % of the screen height (0-90).

IF SOMETHING GOES WRONG
- Menus misbehave: set menu_small_text=0, or menu_hooks=0, in the ini.
- The game does not start: delete d3d9.dll.
- To report a problem, attach alanwake_collectibles.log. It contains your Steam account
  number as part of a folder path; remove it first if you prefer.

PRIVACY AND SAFETY
The mod reads your profile and checkpoint files, and writes only its settings and log. It
does not use the network. As with any DLL of this kind, an antivirus may flag it.

TESTED
Steam version only, on Windows and on Linux (Steam Deck).

LICENCE
MIT, copyright vix-bobby: you may share and adapt this mod. See
AlanWakeCollectibles-LICENSE.txt.
