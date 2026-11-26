/* Runs on Windows: settings file creation, toggling, comments kept, custom keys. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../src/path.h"
#include "../src/settings.h"

#define EXPECT(x)                                            \
    do {                                                     \
        if (!(x)) {                                          \
            printf("FAIL line %d: %s\n", __LINE__, #x);      \
            return 1;                                        \
        }                                                    \
    } while (0)

static char text[4096];

static FILE *open_path(const char *path, const wchar_t *mode)
{
    wchar_t wide[PATH_WIDE];

    to_wide(path, wide, PATH_WIDE);
    return _wfopen(wide, mode);
}

static int ini_write(const char *section, const char *name, const char *value, const char *path)
{
    wchar_t s[32], n[32], v[32], wide[PATH_WIDE];

    to_wide(section, s, 32);
    to_wide(name, n, 32);
    to_wide(value, v, 32);
    to_wide(path, wide, PATH_WIDE);
    return WritePrivateProfileStringW(s, n, v, wide) != 0;
}

static int remove_file(const char *path)
{
    wchar_t wide[PATH_WIDE];

    to_wide(path, wide, PATH_WIDE);
    return DeleteFileW(wide) != 0;
}

static const char *contents(const char *path)
{
    FILE *f = open_path(path, L"rb");
    size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;

    text[n] = '\0';
    if (f)
        fclose(f);
    return text;
}

int main(void)
{
    char path[PATH_UTF8], name[8];
    wchar_t dir[PATH_WIDE], folder[PATH_WIDE];

    /* the ini API needs a full path, or it looks in the Windows folder */
    GetCurrentDirectoryW(PATH_WIDE, dir);
    /* a folder name that fits no single code page */
    snwprintf(folder, PATH_WIDE, L"%ls\\%ls", dir, L"ゲーム Łódź");
    CreateDirectoryW(folder, NULL);
    wcscat(folder, L"\\settings_test.ini");
    EXPECT(to_utf8(folder, path, sizeof path));
    remove_file(path);

    settings_load(path);
    EXPECT(strstr(contents(path), "F9  show or hide the counts"));
    EXPECT(OPTIONS[OPT_OVERLAY].enabled && OPTIONS[OPT_MENU_COUNTS].enabled);
    EXPECT(OPTIONS[OPT_OVERLAY].key == VK_F8 && OPTIONS[OPT_MENU_COUNTS].key == VK_F9);
    EXPECT(OPTIONS[OPT_EXPANDED].key == VK_F10 && OPTIONS[OPT_PAGE_TITLES].key == VK_F1);
    EXPECT(OPTIONS[OPT_ITEM_DETAILS].key == VK_F2 && !OPTIONS[OPT_ITEM_DETAILS].enabled);
    EXPECT(!OPTIONS[OPT_EXPANDED].enabled && !OPTIONS[OPT_PAGE_TITLES].enabled);

    EXPECT(settings_toggle(OPT_OVERLAY));
    EXPECT(!OPTIONS[OPT_OVERLAY].enabled);
    EXPECT(strstr(contents(path), "overlay=0"));
    EXPECT(strstr(text, "F9  show or hide the counts")); /* comments survive saving */

    OPTIONS[OPT_OVERLAY].enabled = 1;
    settings_load(path);
    EXPECT(!OPTIONS[OPT_OVERLAY].enabled); /* the choice persists */

    ini_write("keys", "overlay", "F6", path);
    ini_write("keys", "menu_counts", "none", path);
    settings_load(path);
    EXPECT(OPTIONS[OPT_OVERLAY].key == VK_F6 && OPTIONS[OPT_MENU_COUNTS].key == 0);
    EXPECT(strcmp(key_name(OPTIONS[OPT_OVERLAY].key, name), "F6") == 0);
    ini_write("keys", "overlay", "banana", path);
    settings_load(path);
    EXPECT(OPTIONS[OPT_OVERLAY].key == VK_F6); /* nonsense keeps the previous key */

    /* deleted while running: the commented file comes back with the current choices */
    ini_write("keys", "menu_counts", "F7", path);
    settings_load(path);
    OPTIONS[OPT_MENU_DETAIL].enabled = 0;
    remove_file(path);
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
        EXPECT(OPTIONS[OPT_OVERLAY].key == VK_F6 && OPTIONS[OPT_MENU_COUNTS].key == VK_F7);
        EXPECT(OPTIONS[OPT_MENU_DETAIL].key == 0);
    }

    /* position: defaults, custom values, nonsense */
    remove_file(path);
    settings_load(path);
    EXPECT(!POSITION.right && POSITION.bottom);
    EXPECT(POSITION.margin_x == 1 && POSITION.margin_y == 9);
    ini_write("position", "corner", "Top-Right", path);
    ini_write("position", "margin_x", "7", path);
    ini_write("position", "margin_y", "500", path);
    settings_load(path);
    EXPECT(POSITION.right && !POSITION.bottom);
    EXPECT(POSITION.margin_x == 7 && POSITION.margin_y == 9);
    ini_write("position", "corner", "middle", path);
    settings_load(path);
    EXPECT(POSITION.right && !POSITION.bottom);
    /* a deleted file comes back with the position in use */
    remove_file(path);
    EXPECT(settings_toggle(OPT_OVERLAY));
    EXPECT(strstr(contents(path), "corner=top-right") && strstr(text, "margin_x=7"));
    /* a file from an earlier version gains the section once, keeping its choices */
    {
        static const char old[] = "[options]\r\noverlay=0\r\n";
        FILE *f = open_path(path, L"wb");

        fwrite(old, 1, sizeof old - 1, f);
        fclose(f);
        settings_load(path);
        settings_load(path);
        EXPECT(!OPTIONS[OPT_OVERLAY].enabled);
        EXPECT(strstr(contents(path), "overlay=0") && strstr(text, "corner=bottom-left"));
        EXPECT(strstr(text, "menu_small_text=1") && strstr(text, "menu_hooks=1"));
        EXPECT(strstr(strstr(text, "[position]") + 1, "[position]") == NULL);
        EXPECT(strstr(text, "overlay_expanded=0") && strstr(text, "page_titles=F1\r"));
        EXPECT(strstr(text, "item_details=F2"));
    }
    /* an older file that already uses F10 keeps it for its own option only */
    {
        static const char old[] = "[keys]\r\noverlay=F10\r\n";
        FILE *f = open_path(path, L"wb");

        fwrite(old, 1, sizeof old - 1, f);
        fclose(f);
        OPTIONS[OPT_EXPANDED].key = VK_F10;
        OPTIONS[OPT_PAGE_TITLES].key = VK_F1;
        settings_load(path);
        EXPECT(OPTIONS[OPT_OVERLAY].key == VK_F10 && OPTIONS[OPT_EXPANDED].key == 0);
        EXPECT(OPTIONS[OPT_PAGE_TITLES].key == VK_F1);
        EXPECT(strstr(contents(path), "overlay_expanded=none"));
    }

    remove_file(path);
    printf("settings: PASS\n");
    return 0;
}
