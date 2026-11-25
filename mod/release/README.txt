Alan Wake Collectibles 0.7.1
============================

Shows which collectibles you still have to find, per chapter and episode.

FOR THE ORIGINAL ALAN WAKE (2010), NOT ALAN WAKE REMASTERED.
Tested only with the Steam version, on Windows and on Linux (Steam Deck).

REQUIREMENTS
- The original Alan Wake (2010) on Steam, on Windows or Linux (Steam Deck).
  Not Alan Wake Remastered, and not American Nightmare.
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

SETTINGS
alanwake_collectibles.ini is created next to the game at the first start, with explanations:
overlay position, keys, smaller or normal menu text, counts per type or totals only.
Edits to this file apply the next time you start the game. F8 and F9 act at once, and save
your choice in the file.

IF SOMETHING GOES WRONG
- Menus misbehave: set menu_small_text=0, or menu_hooks=0, in the ini.
- The game does not start: delete d3d9.dll.
- To report a problem, attach alanwake_collectibles.log. It contains your Steam account
  number as part of a folder path; remove it first if you prefer.

PRIVACY AND SAFETY
The mod reads your profile and checkpoint files, and writes only its settings and log. It
does not use the network. As with any DLL of this kind, an antivirus may flag it.

TESTED
Steam version only, on Windows and on Linux (Steam Deck). The small menu text was aligned
at 2560x1440; if it looks off line on your screen, menu_small_text=0 avoids that.
