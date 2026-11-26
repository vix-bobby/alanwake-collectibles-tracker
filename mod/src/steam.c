#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "path.h"
#include "steam.h"

#define PATH_SIZE PATH_UTF8
#define PROFILE "108710/remote/config"

void wine_path(const char *path, char *out, size_t size)
{
    /* Wine's own variables use the NT form \??\Z:\... */
    if (strncmp(path, "\\??\\", 4) == 0)
        path += 4;
    snprintf(out, size, "%s%s", path[0] == '/' ? "Z:" : "", path);
}

struct roots {
    char path[8][PATH_SIZE];
    const char *source[8];
    int count;
};

static void add_root(struct roots *r, const char *source, const char *path, const char *suffix)
{
    size_t n;

    if (!path[0] || r->count == 8)
        return;
    n = strlen(path);
    while (n && (path[n - 1] == '/' || path[n - 1] == '\\'))
        n--;
    snprintf(r->path[r->count], PATH_SIZE, "%.*s%s", (int)n, path, suffix);
    r->source[r->count++] = source;
}

/* The Steam library holding `path`: everything before its "steamapps" folder. */
static void library_of(const char *path, char *out, size_t size)
{
    size_t i, n = strlen(path);

    out[0] = '\0';
    for (i = 1; i + 9 <= n; i++)
        if ((path[i - 1] == '/' || path[i - 1] == '\\') && _strnicmp(path + i, "steamapps", 9) == 0
            && (!path[i + 9] || path[i + 9] == '/' || path[i + 9] == '\\')) {
            snprintf(out, size, "%.*s", (int)(i - 1), path);
            return;
        }
}

static int variable(const char *name, char *out, size_t size)
{
    wchar_t wide_name[64], wide[PATH_WIDE];
    char raw[PATH_SIZE];
    DWORD n = to_wide(name, wide_name, 64) ? GetEnvironmentVariableW(wide_name, wide, PATH_WIDE)
                                           : 0;

    out[0] = '\0';
    if (!n || n >= PATH_WIDE || !to_utf8(wide, raw, sizeof raw))
        return 0;
    wine_path(raw, out, size);
    return 1;
}

static void collect(const struct steam_hints *hints, struct roots *r)
{
    char value[PATH_SIZE], library[PATH_SIZE];

    r->count = 0;
    add_root(r, "registry", hints->registry_path, "");
    /* Proton: the Linux Steam installation, which holds userdata */
    variable("STEAM_COMPAT_CLIENT_INSTALL_PATH", value, sizeof value);
    add_root(r, "Proton Steam path", value, "");
    library_of(hints->game_dir, library, sizeof library);
    add_root(r, "game library", library, "");
    variable("STEAM_COMPAT_DATA_PATH", value, sizeof value);
    library_of(value, library, sizeof library);
    add_root(r, "Proton library", library, "");
    if (variable("WINEHOMEDIR", value, sizeof value)) {
        add_root(r, "Linux home", value, "/.local/share/Steam");
        add_root(r, "Linux home", value, "/.steam/steam");
        add_root(r, "Flatpak Steam", value, "/.var/app/com.valvesoftware.Steam/.local/share/Steam");
    }
}

/* Keep the most recently written profile below `root` seen so far. */
static void newest_below(const char *root, char *best, size_t size, FILETIME *newest)
{
    char pattern[PATH_SIZE + 16], path[PATH_SIZE * 2], account[MAX_PATH * 3];
    wchar_t wide[PATH_WIDE];
    WIN32_FIND_DATAW entry;
    WIN32_FILE_ATTRIBUTE_DATA info;
    HANDLE search;

    snprintf(pattern, sizeof pattern, "%s/userdata/*", root);
    if (!to_wide(pattern, wide, PATH_WIDE))
        return;
    search = FindFirstFileW(wide, &entry);
    if (search == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!to_utf8(entry.cFileName, account, sizeof account))
            continue;
        snprintf(path, sizeof path, "%s/userdata/%s/" PROFILE, root, account);
        if (path_info(path, &info)
            && (!best[0] || CompareFileTime(&info.ftLastWriteTime, newest) > 0)) {
            *newest = info.ftLastWriteTime;
            snprintf(best, size, "%s", path);
        }
    } while (FindNextFileW(search, &entry));
    FindClose(search);
}

int steam_find_profile(const struct steam_hints *hints, char *out, size_t size, char *report,
                       size_t report_size)
{
    struct roots r;
    FILETIME newest = {0, 0};
    char path[PATH_SIZE * 2];
    size_t n = 0;
    int i;

    collect(hints, &r);
    if (report && report_size)
        report[0] = '\0';
    out[0] = '\0';

    /* the signed-in account, if Steam says which, even before it has saved anything */
    if (hints->active_user)
        for (i = 0; i < r.count; i++) {
            snprintf(path, sizeof path, "%s/userdata/%lu", r.path[i], hints->active_user);
            if (path_attributes(path) != INVALID_FILE_ATTRIBUTES) {
                snprintf(out, size, "%s/" PROFILE, path);
                return 1;
            }
        }
    /* otherwise the profile written last, in any of the places */
    for (i = 0; i < r.count; i++)
        newest_below(r.path[i], out, size, &newest);
    if (out[0])
        return 1;

    for (i = 0; report && i < r.count && n < report_size; i++)
        n += snprintf(report + n, report_size - n, "  %s: %s\n", r.source[i], r.path[i]);
    return 0;
}
