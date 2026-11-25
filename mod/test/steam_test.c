/* Windows test of the profile search: registry, Proton variables, game folder, accounts.
 * Runs in an empty scratch folder. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../src/steam.h"

#define EXPECT(x)                                       \
    do {                                                \
        if (!(x)) {                                     \
            printf("FAIL line %d: %s\n", __LINE__, #x); \
            return 1;                                   \
        }                                               \
    } while (0)

static char base[MAX_PATH];

static const char *at(const char *relative)
{
    static char paths[8][MAX_PATH * 2];
    static int next;
    char *p = paths[next++ % 8];

    snprintf(p, MAX_PATH * 2, "%s/%s", base, relative);
    return p;
}

/* Create a profile for `account` below `root`, dated `age` (larger is newer). */
static void profile(const char *root, const char *account, int age)
{
    char path[MAX_PATH * 2], *slash;
    SYSTEMTIME t = {2020, 1, 0, 1, 0, 0, 0, 0};
    FILETIME time;
    HANDLE f;

    snprintf(path, sizeof path, "%s/%s/userdata/%s/108710/remote/config", base, root, account);
    for (slash = strchr(path + strlen(base) + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        CreateDirectoryA(path, NULL);
        *slash = '/';
    }
    f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    t.wDay = (WORD)age;
    SystemTimeToFileTime(&t, &time);
    SetFileTime(f, NULL, NULL, &time);
    CloseHandle(f);
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
    char out[1024], report[4096], buffer[256];

    GetCurrentDirectoryA(sizeof base, base);
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
    SetEnvironmentVariableA("STEAM_COMPAT_CLIENT_INSTALL_PATH", at("linux/"));
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
    SetEnvironmentVariableA("STEAM_COMPAT_DATA_PATH", at("lib/steamapps/compatdata/108710"));
    EXPECT(steam_find_profile(&h, out, sizeof out, NULL, 0) && strstr(out, "lib/userdata/444/"));
    clear_variables();

    /* last resort: the usual place under the Linux home folder (game on an SD card) */
    profile("home/.local/share/Steam", "555", 6);
    h.game_dir = at("sdcard/steamapps/common/Alan Wake/");
    SetEnvironmentVariableA("WINEHOMEDIR", at("home"));
    EXPECT(steam_find_profile(&h, out, sizeof out, NULL, 0) && strstr(out, "userdata/555/"));

    /* nothing anywhere: the report names the places tried */
    SetEnvironmentVariableA("WINEHOMEDIR", at("nobody"));
    SetEnvironmentVariableA("STEAM_COMPAT_CLIENT_INSTALL_PATH", at("missing"));
    EXPECT(!steam_find_profile(&h, out, sizeof out, report, sizeof report));
    EXPECT(strstr(report, "Proton Steam path: ") && strstr(report, "/missing"));
    EXPECT(strstr(report, "game library: ") && strstr(report, "nobody/.steam/steam"));
    printf("steam search: PASS\n");
    return 0;
}
