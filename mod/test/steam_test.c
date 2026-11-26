/* Windows test of the profile search: registry, Proton variables, game folder, accounts.
 * Runs in an empty scratch folder. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../src/path.h"
#include "../src/steam.h"

#define EXPECT(x)                                       \
    do {                                                \
        if (!(x)) {                                     \
            printf("FAIL line %d: %s\n", __LINE__, #x); \
            return 1;                                   \
        }                                               \
    } while (0)

static char base[PATH_UTF8];

static const char *at(const char *relative)
{
    static char paths[8][PATH_UTF8 * 2];
    static int next;
    char *p = paths[next++ % 8];

    snprintf(p, PATH_UTF8 * 2, "%s/%s", base, relative);
    return p;
}

/* Create a profile for `account` below `root`, dated `age` (larger is newer). */
static void profile(const char *root, const char *account, int age)
{
    char path[PATH_UTF8 * 2], *slash;
    wchar_t wide[PATH_WIDE];
    SYSTEMTIME t = {2020, 1, 0, 1, 0, 0, 0, 0};
    FILETIME time;
    HANDLE f;

    snprintf(path, sizeof path, "%s/%s/userdata/%s/108710/remote/config", base, root, account);
    for (slash = strchr(path + strlen(base) + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        to_wide(path, wide, PATH_WIDE);
        CreateDirectoryW(wide, NULL);
        *slash = '/';
    }
    to_wide(path, wide, PATH_WIDE);
    f = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    t.wDay = (WORD)age;
    SystemTimeToFileTime(&t, &time);
    SetFileTime(f, NULL, NULL, &time);
    CloseHandle(f);
}

/* As the system would hold it: the value is UTF-8 here. */
static void set_variable(const wchar_t *name, const char *value)
{
    wchar_t wide[PATH_WIDE];

    to_wide(value, wide, PATH_WIDE);
    SetEnvironmentVariableW(name, wide);
}

static void clear_variables(void)
{
    SetEnvironmentVariableA("STEAM_COMPAT_CLIENT_INSTALL_PATH", NULL);
    SetEnvironmentVariableA("STEAM_COMPAT_DATA_PATH", NULL);
    SetEnvironmentVariableA("WINEHOMEDIR", NULL);
}

int main(void)
{
    struct steam_hints h = {"", 0, ""};
    char out[PATH_UTF8 * 2], report[8192], buffer[256];
    wchar_t dir[PATH_WIDE], folder[PATH_WIDE];

    GetCurrentDirectoryW(PATH_WIDE, dir);
    /* everything below a folder name that fits no single code page */
    snwprintf(folder, PATH_WIDE, L"%ls\\%ls", dir, L"ゲーム Łódź");
    CreateDirectoryW(folder, NULL);
    EXPECT(to_utf8(folder, base, sizeof base));
    clear_variables();

    wine_path("/home/deck/.local/share/Steam", buffer, sizeof buffer);
    EXPECT(!strcmp(buffer, "Z:/home/deck/.local/share/Steam"));
    wine_path("\\??\\Z:\\home\\deck", buffer, sizeof buffer);
    EXPECT(!strcmp(buffer, "Z:\\home\\deck"));
    wine_path("C:\\Steam", buffer, sizeof buffer);
    EXPECT(!strcmp(buffer, "C:\\Steam"));

    EXPECT(!steam_find_profile(&h, out, sizeof out, report, sizeof report));

    /* Windows: registry path and signed-in account */
    profile("win", "111", 2);
    profile("win", "222", 3);
    h.registry_path = at("win");
    h.active_user = 111;
    EXPECT(steam_find_profile(&h, out, sizeof out, NULL, 0));
    EXPECT(strstr(out, "win/userdata/111/108710/remote/config"));
    h.active_user = 0; /* unknown account: newest */
    EXPECT(steam_find_profile(&h, out, sizeof out, NULL, 0) && strstr(out, "userdata/222/"));

    /* Proton: no registry; the Linux Steam folder comes from the environment */
    h.registry_path = "";
    EXPECT(!steam_find_profile(&h, out, sizeof out, report, sizeof report));
    profile("linux", "333", 4);
    set_variable(L"STEAM_COMPAT_CLIENT_INSTALL_PATH", at("linux/"));
    EXPECT(steam_find_profile(&h, out, sizeof out, NULL, 0));
    EXPECT(strstr(out, "linux/userdata/333/"));
    /* a registry path leading nowhere (Proton's stub folder) does not hide it */
    h.registry_path = at("stub");
    h.active_user = 333;
    EXPECT(steam_find_profile(&h, out, sizeof out, NULL, 0) && strstr(out, "linux/userdata/333/"));
    clear_variables();
    h.registry_path = "";
    h.active_user = 0;

    /* the library the game is installed in */
    profile("lib", "444", 5);
    h.game_dir = at("lib/steamapps/common/Alan Wake/");
    EXPECT(steam_find_profile(&h, out, sizeof out, NULL, 0) && strstr(out, "lib/userdata/444/"));
    h.game_dir = at("elsewhere/mysteamapps/common/");
    EXPECT(!steam_find_profile(&h, out, sizeof out, report, sizeof report));

    /* Proton's prefix variable, for a library path */
    set_variable(L"STEAM_COMPAT_DATA_PATH", at("lib/steamapps/compatdata/108710"));
    EXPECT(steam_find_profile(&h, out, sizeof out, NULL, 0) && strstr(out, "lib/userdata/444/"));
    clear_variables();

    /* last resort: the usual place under the Linux home folder (game on an SD card) */
    profile("home/.local/share/Steam", "555", 6);
    h.game_dir = at("sdcard/steamapps/common/Alan Wake/");
    set_variable(L"WINEHOMEDIR", at("home"));
    EXPECT(steam_find_profile(&h, out, sizeof out, NULL, 0) && strstr(out, "userdata/555/"));

    /* nothing anywhere: the report names the places tried */
    set_variable(L"WINEHOMEDIR", at("nobody"));
    set_variable(L"STEAM_COMPAT_CLIENT_INSTALL_PATH", at("missing"));
    EXPECT(!steam_find_profile(&h, out, sizeof out, report, sizeof report));
    EXPECT(strstr(report, "Proton Steam path: ") && strstr(report, "/missing"));
    EXPECT(strstr(report, "game library: ") && strstr(report, "nobody/.steam/steam"));
    printf("steam search: PASS\n");
    return 0;
}
