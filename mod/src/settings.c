#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "path.h"
#include "settings.h"

struct option OPTIONS[OPT_COUNT] = {
    {"overlay", "Overlay", 1, VK_F8},
    {"menu_counts", "Menu counts", 1, VK_F9},
    {"menu_detail", "Menu counts per type", 1, 0},
    {"menu_small_text", "Small menu counts", 1, 0},
    {"menu_hooks", "Menu changes", 1, 0},
    {"overlay_expanded", "Missing list", 0, VK_F10},
    {"page_titles", "Page titles in the list", 0, VK_F1},
    {"item_details", "Item details in the list", 0, VK_F2},
};

/* clear of the radar and objective (top left) and above the subtitles */
struct position POSITION = {0, 1, 1, 9};

static wchar_t ini[PATH_WIDE];

#define POSITION_TEMPLATE                                                              \
    "\r\n"                                                                             \
    "[position]\r\n"                                                                   \
    "; Overlay corner: top-left, top-right, bottom-left or bottom-right\r\n"            \
    "corner=bottom-left\r\n"                                                           \
    "; Distance from the side, and from the top or bottom, in percent of the screen\r\n" \
    "margin_x=1\r\n"                                                                   \
    "margin_y=9\r\n"

static const char TEMPLATE[] =
    "; Alan Wake collectibles mod. Delete this file to restore the defaults.\r\n"
    "; Edits to this file apply the next time the game starts. The keys below act at once.\r\n"
    ";\r\n"
    "; Default keys in game:\r\n"
    ";   F8  show or hide the overlay\r\n"
    ";   F9  show or hide the counts in the episode and chapter menus\r\n"
    ";   F10 expand the overlay with the list of what is missing, or simplify it\r\n"
    ";   F1  name manuscript pages in that list by title instead of number (spoilers)\r\n"
    ";   F2  add details to other items in that list: cutout names, where some are (spoilers)\r\n"
    "\r\n"
    "[options]\r\n"
    "; 1 = on, 0 = off (the keys above change these and save them here)\r\n"
    "overlay=1\r\n"
    "menu_counts=1\r\n"
    "; 1 = total, then each type still to find; 0 = total only (shorter)\r\n"
    "menu_detail=1\r\n"
    "; 1 = counts in the menu lists drawn smaller than the names\r\n"
    "menu_small_text=1\r\n"
    "; 0 = never touch the game's menus (use this if menus misbehave)\r\n"
    "menu_hooks=1\r\n"
    "; 1 = the overlay also lists what is missing in the chapter; 0 = counts only\r\n"
    "overlay_expanded=0\r\n"
    "; 1 = that list shows manuscript page titles (mild spoilers); 0 = page numbers\r\n"
    "page_titles=0\r\n"
    "; 1 = that list adds cutout names, and where some chests and signs are (mild spoilers)\r\n"
    "item_details=0\r\n"
    "\r\n"
    "[keys]\r\n"
    "; F1 to F24, or none. F5 is the game's default manuscript key.\r\n"
    "overlay=F8\r\n"
    "menu_counts=F9\r\n"
    "menu_detail=none\r\n"
    "menu_small_text=none\r\n"
    "menu_hooks=none\r\n"
    "overlay_expanded=F10\r\n"
    "page_titles=F1\r\n"
    "item_details=F2\r\n" POSITION_TEMPLATE;

const char *key_name(int key, char out[8])
{
    if (key >= VK_F1 && key <= VK_F24)
        snprintf(out, 8, "F%d", key - VK_F1 + 1);
    else
        snprintf(out, 8, "-");
    return out;
}

static int parse_key(const char *text, int fallback)
{
    int n;

    if (_stricmp(text, "none") == 0)
        return 0;
    if ((text[0] == 'F' || text[0] == 'f') && (n = atoi(text + 1)) >= 1 && n <= 24)
        return VK_F1 + n - 1;
    return fallback;
}

/* Write the commented default file if there is none. Returns 1 if it was created. */
static int create_file(void)
{
    HANDLE file = CreateFileW(ini, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written;

    if (file == INVALID_HANDLE_VALUE)
        return 0;
    WriteFile(file, TEMPLATE, sizeof TEMPLATE - 1, &written, NULL);
    CloseHandle(file);
    return 1;
}

/* The ini functions, taking our narrow names and values (all ASCII). */
static DWORD ini_get(const char *section, const char *name, char *out, int size)
{
    wchar_t s[32], n[32], value[64];

    to_wide(section, s, 32);
    to_wide(name, n, 32);
    GetPrivateProfileStringW(s, n, L"", value, 64, ini);
    if (!to_utf8(value, out, size))
        out[0] = '\0'; /* too long to be a value we know */
    return (DWORD)strlen(out);
}

static int ini_set(const char *section, const char *name, const char *value)
{
    wchar_t s[32], n[32], v[32];

    to_wide(section, s, 32);
    to_wide(name, n, 32);
    to_wide(value, v, 32);
    return WritePrivateProfileStringW(s, n, v, ini) != 0;
}

static int ini_number(const char *section, const char *name, int fallback)
{
    wchar_t s[32], n[32];

    to_wide(section, s, 32);
    to_wide(name, n, 32);
    return (int)GetPrivateProfileIntW(s, n, fallback, ini);
}

static int save_option(const struct option *o)
{
    return ini_set("options", o->name, o->enabled ? "1" : "0");
}

/* Add the position section to a file written by an earlier version. */
static void add_position_section(void)
{
    static const char text[] = POSITION_TEMPLATE;
    wchar_t keys[64];
    DWORD written;
    HANDLE file;

    if (GetPrivateProfileSectionW(L"position", keys, 64, ini))
        return;
    file = CreateFileW(ini, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    WriteFile(file, text, sizeof text - 1, &written, NULL);
    CloseHandle(file);
}

static int read_margin(const char *name, int fallback)
{
    int value = ini_number("position", name, fallback);

    return value < 0 || value > 90 ? fallback : value;
}

static void load_position(void)
{
    char corner[24];

    ini_get("position", "corner", corner, sizeof corner);
    CharLowerA(corner);
    /* anything unrecognised keeps the current value */
    if (strstr(corner, "top"))
        POSITION.bottom = 0;
    if (strstr(corner, "bottom"))
        POSITION.bottom = 1;
    if (strstr(corner, "left"))
        POSITION.right = 0;
    if (strstr(corner, "right"))
        POSITION.right = 1;
    POSITION.margin_x = read_margin("margin_x", POSITION.margin_x);
    POSITION.margin_y = read_margin("margin_y", POSITION.margin_y);
}

static int save_position(void)
{
    char corner[24], x[8], y[8];

    snprintf(corner, sizeof corner, "%s-%s", POSITION.bottom ? "bottom" : "top",
             POSITION.right ? "right" : "left");
    snprintf(x, sizeof x, "%d", POSITION.margin_x);
    snprintf(y, sizeof y, "%d", POSITION.margin_y);
    return ini_set("position", "corner", corner) && ini_set("position", "margin_x", x)
           && ini_set("position", "margin_y", y);
}

/* A default key the file already gives to another option would switch both: drop it. */
static void drop_taken_defaults(const int *from_file)
{
    int i, j;

    for (i = 0; i < OPT_COUNT; i++)
        for (j = 0; !from_file[i] && j < OPT_COUNT; j++)
            if (from_file[j] && OPTIONS[j].key == OPTIONS[i].key)
                OPTIONS[i].key = 0;
}

void settings_load(const char *path)
{
    int from_file[OPT_COUNT] = {0};
    char value[16];
    int i;

    to_wide(path, ini, PATH_WIDE);
    if (!create_file())
        add_position_section();
    load_position();
    for (i = 0; i < OPT_COUNT; i++) {
        struct option *o = &OPTIONS[i];

        /* name options added since the file was written */
        if (!ini_get("options", o->name, value, sizeof value))
            save_option(o);
        o->enabled = ini_number("options", o->name, o->enabled) != 0;
        if (ini_get("keys", o->name, value, sizeof value)) {
            o->key = parse_key(value, o->key);
            from_file[i] = 1;
        }
    }
    drop_taken_defaults(from_file);
    /* and name the keys of options added since the file was written */
    for (i = 0; i < OPT_COUNT; i++)
        if (!from_file[i]) {
            char key[8];

            key_name(OPTIONS[i].key, key);
            ini_set("keys", OPTIONS[i].name, OPTIONS[i].key ? key : "none");
        }
}

int settings_toggle(int option)
{
    struct option *o = &OPTIONS[option];

    o->enabled = !o->enabled;
    if (create_file()) {
        /* the file was deleted while running: put every current choice back into it */
        int i, saved = 1;

        for (i = 0; i < OPT_COUNT; i++) {
            char key[8];

            key_name(OPTIONS[i].key, key);
            saved &= save_option(&OPTIONS[i]);
            saved &= ini_set("keys", OPTIONS[i].name, OPTIONS[i].key ? key : "none");
        }
        return saved & save_position();
    }
    return save_option(o);
}
