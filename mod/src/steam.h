/* Finding the profile file `config` in Steam's userdata folder, on Windows and under Proton
 * (Linux, Steam Deck), where Windows' registry entries for Steam may be absent. */
#ifndef STEAM_H
#define STEAM_H

#include <stddef.h>

/* All paths are UTF-8. */
struct steam_hints {
    const char *registry_path; /* HKCU\Software\Valve\Steam SteamPath, or "" */
    unsigned long active_user; /* ...\ActiveProcess ActiveUser, or 0 */
    const char *game_dir;      /* folder the mod was loaded from */
};

/* A Linux path as Wine sees it ("/home/deck" gives "Z:/home/deck"); other paths unchanged. */
void wine_path(const char *path, char *out, size_t size);

/* Write the profile path into `out`. Returns 0 if none was found; `report`, if given, is
 * then filled with the places looked at, one per line. */
int steam_find_profile(const struct steam_hints *hints, char *out, size_t size, char *report,
                       size_t report_size);

#endif
