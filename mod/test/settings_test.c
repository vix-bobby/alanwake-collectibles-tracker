/* Runs on Windows: settings file creation, toggling, comments kept, custom keys. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../src/settings.h"

#define EXPECT(x)                                            \
    do {                                                     \
        if (!(x)) {                                          \
            printf("FAIL line %d: %s\n", __LINE__, #x);      \
            return 1;                                        \
        }                                                    \
    } while (0)

static char text[4096];

static const char *contents(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;

    text[n] = '\0';
    if (f)
        fclose(f);
    return text;
}

int main(void)
{
    char dir[MAX_PATH], path[MAX_PATH + 32], name[8];

    /* the ini API needs a full path, or it looks in the Windows folder */
    GetCurrentDirectoryA(sizeof dir, dir);
    snprintf(path, sizeof path, "%s\\settings_test.ini", dir);
    DeleteFileA(path);

    settings_load(path);
    EXPECT(strstr(contents(path), "F9  show or hide the counts"));
    EXPECT(OPTIONS[OPT_OVERLAY].enabled && OPTIONS[OPT_MENU_COUNTS].enabled);
    EXPECT(OPTIONS[OPT_OVERLAY].key == VK_F8 && OPTIONS[OPT_MENU_COUNTS].key == VK_F9);

    EXPECT(settings_toggle(OPT_OVERLAY));
    EXPECT(!OPTIONS[OPT_OVERLAY].enabled);
    EXPECT(strstr(contents(path), "overlay=0"));
    EXPECT(strstr(text, "F9  show or hide the counts")); /* comments survive saving */

    OPTIONS[OPT_OVERLAY].enabled = 1;
    settings_load(path);
    EXPECT(!OPTIONS[OPT_OVERLAY].enabled); /* the choice persists */

    WritePrivateProfileStringA("keys", "overlay", "F10", path);
    WritePrivateProfileStringA("keys", "menu_counts", "none", path);
    settings_load(path);
    EXPECT(OPTIONS[OPT_OVERLAY].key == VK_F10 && OPTIONS[OPT_MENU_COUNTS].key == 0);
    EXPECT(strcmp(key_name(OPTIONS[OPT_OVERLAY].key, name), "F10") == 0);
    WritePrivateProfileStringA("keys", "overlay", "banana", path);
    settings_load(path);
    EXPECT(OPTIONS[OPT_OVERLAY].key == VK_F10); /* nonsense keeps the previous key */

    /* deleted while running: the commented file comes back with the current choices */
    WritePrivateProfileStringA("keys", "menu_counts", "F11", path);
    settings_load(path);
    OPTIONS[OPT_MENU_DETAIL].enabled = 0;
    DeleteFileA(path);
    EXPECT(settings_toggle(OPT_OVERLAY));
    {
        int overlay = OPTIONS[OPT_OVERLAY].enabled;

        EXPECT(strstr(contents(path), "F9  show or hide the counts"));
        OPTIONS[OPT_OVERLAY].enabled = !overlay;
        OPTIONS[OPT_MENU_DETAIL].enabled = 1;
        OPTIONS[OPT_OVERLAY].key = OPTIONS[OPT_MENU_COUNTS].key = VK_F1;
        settings_load(path);
        EXPECT(OPTIONS[OPT_OVERLAY].enabled == overlay);
        EXPECT(!OPTIONS[OPT_MENU_DETAIL].enabled);
        EXPECT(OPTIONS[OPT_OVERLAY].key == VK_F10 && OPTIONS[OPT_MENU_COUNTS].key == VK_F11);
        EXPECT(OPTIONS[OPT_MENU_DETAIL].key == 0);
    }

    /* position: defaults, custom values, nonsense */
    DeleteFileA(path);
    settings_load(path);
    EXPECT(!POSITION.right && POSITION.bottom);
    EXPECT(POSITION.margin_x == 1 && POSITION.margin_y == 9);
    WritePrivateProfileStringA("position", "corner", "Top-Right", path);
    WritePrivateProfileStringA("position", "margin_x", "7", path);
    WritePrivateProfileStringA("position", "margin_y", "500", path);
    settings_load(path);
    EXPECT(POSITION.right && !POSITION.bottom);
    EXPECT(POSITION.margin_x == 7 && POSITION.margin_y == 9);
    WritePrivateProfileStringA("position", "corner", "middle", path);
    settings_load(path);
    EXPECT(POSITION.right && !POSITION.bottom);
    /* a deleted file comes back with the position in use */
    DeleteFileA(path);
    EXPECT(settings_toggle(OPT_OVERLAY));
    EXPECT(strstr(contents(path), "corner=top-right") && strstr(text, "margin_x=7"));
    /* a file from an earlier version gains the section once, keeping its choices */
    {
        static const char old[] = "[options]\r\noverlay=0\r\n";
        FILE *f = fopen(path, "wb");

        fwrite(old, 1, sizeof old - 1, f);
        fclose(f);
        settings_load(path);
        settings_load(path);
        EXPECT(!OPTIONS[OPT_OVERLAY].enabled);
        EXPECT(strstr(contents(path), "overlay=0") && strstr(text, "corner=bottom-left"));
        EXPECT(strstr(text, "menu_small_text=1") && strstr(text, "menu_hooks=1"));
        EXPECT(strstr(strstr(text, "[position]") + 1, "[position]") == NULL);
    }

    DeleteFileA(path);
    printf("settings: PASS\n");
    return 0;
}
