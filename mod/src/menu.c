/*
 * The game builds its menus once. Two entries in its method tables are redirected:
 *
 *   MenuMode::render     called every frame on the menu thread, before anything is measured
 *                        or drawn: labels are rewritten there when the profile changes.
 *   MenuCommand::draw    draws one list entry: with small text, the counts are drawn after
 *                        it at a smaller size, leaving the label itself alone.
 *
 * A chapter list's title is a separate string from the episode entry that opens it, so the
 * title can show the total only while the entry shows the counts per type.
 *
 * Addresses belong to the Steam build of AlanWake.exe dated 2020-05-15 (no ASLR) and are
 * checked against its code before anything is changed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "label.h"
#include "menu.h"

/* offsets in the game's objects */
#define MODE_STATE 0x10
#define MODE_SUBMENUS 0x24
#define MODE_SUBMENU_COUNT 0x28
#define SUBMENU_SIZE 0x1bc
#define SUBMENU_NAME 0x00
#define SUBMENU_TITLE 0x94
#define SUBMENU_ITEMS 0x1a8
#define SUBMENU_ITEM_COUNT 0x1ac
#define ITEM_TEXT 0xb4
#define ITEM_COMMAND 0x260
#define ITEM_ARGUMENT 0x2f4
#define SHAPE_TEXT_SIZE 0x3c
#define SHAPE_TEXT_SHADOW 0x40 /* the game turns it off while it draws an entry's name */

#define SMALL 0.7f
#define PIVOT_LEFT_MIDDLE 3
/* How far below the entry's anchor the small text must be centred for both to share a
 * baseline, in units of the name's text size. Measured in game for this font and SMALL:
 * without it the counts sat 19 px high on a 1440 px screen at text size 0.025. */
#define BASELINE_DROP 0.53f

/* the game's wide string, with room for LABEL_MAX characters inside it */
struct wide {
    wchar_t *data;
    unsigned inline_capacity, capacity, length;
    wchar_t *inline_data;
    wchar_t buffer[LABEL_MAX];
};

struct vec2 {
    float x, y;
};

typedef void(__attribute__((thiscall)) * render_fn)(void *mode);
typedef void(__attribute__((thiscall)) * draw_fn)(void *item, const struct vec2 *at, int selected);
typedef void(__attribute__((fastcall)) * measure_fn)(struct vec2 *size, const wchar_t *text);
typedef void(__attribute__((fastcall)) * draw_text_fn)(const struct vec2 *at, const wchar_t *text,
                                                       int pivot);
typedef char *(__attribute__((fastcall)) * shape_fn)(void);
typedef void *(__attribute__((fastcall)) * assign_fn)(struct wide *string, const wchar_t *text);

/* everything taken from the game; the test replaces it with stand-ins */
static struct {
    void **menu_object; /* global pointer to the MenuMode */
    const void *mode_vtable, *command_vtable;
    const float *selected_offset; /* the selected entry is shifted left by this */
    render_fn render;
    draw_fn draw;
    measure_fn measure;
    draw_text_fn draw_text;
    shape_fn *shape; /* import slot of ShapeEngine::getInstance */
    assign_fn assign;
} game;

static struct menu_host host;
static int small_text, broken;

#define ONCE(...)                  \
    do {                           \
        static int done_;          \
        if (!done_++)              \
            host.log(__VA_ARGS__); \
    } while (0)

/* Read the game's memory without risking a crash on a wrong offset. */
static int peek(const void *address, void *out, size_t size)
{
    SIZE_T got = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, out, size, &got)
           && got == size;
}

static void peek_string(const void *pointer_to_pointer, char *out, size_t size)
{
    const char *text = NULL;
    size_t i = 0;

    if (peek(pointer_to_pointer, &text, sizeof text))
        while (i + 1 < size && peek(text + i, out + i, 1) && out[i])
            i++;
    out[i] = '\0';
}

/* Copy a string of the menu if it looks exactly as expected. */
static int peek_wide(const struct wide *string, struct wide *out)
{
    if (!peek(string, out, sizeof *out) || out->data != string->buffer
        || out->inline_data != string->buffer || out->capacity != LABEL_MAX
        || out->length >= LABEL_MAX || out->buffer[out->length])
        return 0;
    return 1;
}

/* ---------------------------------------------------------------- labels */

/* The text each rewritten label had before any counts, found again by name: pointers are
 * looked up afresh every time. */
static struct saved {
    char key[96];
    wchar_t text[LABEL_MAX];
} saved[64];
static unsigned saved_count;

static const wchar_t *original(const char *submenu, const char *entry, const wchar_t *current)
{
    char key[96];
    unsigned i;

    snprintf(key, sizeof key, "%s/%s", submenu, entry);
    for (i = 0; i < saved_count; i++)
        if (strcmp(saved[i].key, key) == 0)
            return saved[i].text;
    if (saved_count == sizeof saved / sizeof *saved)
        return NULL;
    strcpy(saved[saved_count].key, key);
    wcscpy(saved[saved_count].text, current);
    return saved[saved_count++].text;
}

/* Set a label to its original text, followed by the counts if `p` is given. */
static int relabel(struct wide *string, const char *submenu, const char *entry, const char *id,
                   int title, const struct profile *p, int detail)
{
    struct wide now;
    wchar_t text[LABEL_MAX], counts[LABEL_MAX];
    const wchar_t *base;

    if (!peek_wide(string, &now)) {
        ONCE("menu label %s in '%s' is not as expected: left alone", id, submenu);
        return 0;
    }
    if (!(base = original(submenu, entry, now.buffer)))
        return 0;
    wcscpy(text, base);
    if (p && menu_counts(p, detail, title, id, base, counts))
        wcscat(text, counts);
    if (wcscmp(text, now.buffer) == 0)
        return 0;
    game.assign(string, text); /* short enough to stay in place */
    return 1;
}

/* Walk every menu and bring the labels up to date. Returns the number changed. */
static int update_labels(const char *mode, const struct profile *p, int detail)
{
    const char *submenus = NULL;
    unsigned count = 0, i, j;
    int changed = 0;

    if (!peek(mode + MODE_SUBMENUS, &submenus, 4) || !peek(mode + MODE_SUBMENU_COUNT, &count, 4)
        || !count || count > 200) {
        ONCE("menu layout not as expected: counts disabled");
        broken = 1;
        return 0;
    }
    for (i = 0; i < count; i++) {
        const char *submenu = submenus + i * SUBMENU_SIZE;
        char *const *items = NULL;
        unsigned n = 0;
        char name[48], id[32];

        peek_string(submenu + SUBMENU_NAME, name, sizeof name);
        /* these two are rebuilt every time they open */
        if (strcmp(name, "load") == 0 || strcmp(name, "manuscript_dynamic") == 0)
            continue;
        if (menu_title_id(name, id, sizeof id))
            changed += relabel((void *)(submenu + SUBMENU_TITLE), name, "#title", id, 1, p, detail);
        if (small_text)
            continue; /* entries keep their name; counts are drawn after it */
        if (!peek(submenu + SUBMENU_ITEMS, &items, 4) || !peek(submenu + SUBMENU_ITEM_COUNT, &n, 4))
            continue;
        for (j = 0; j < n && j < 64; j++) {
            char *item = NULL, command[32], argument[48];
            const void *vtable = NULL;

            if (!peek(items + j, &item, 4) || !peek(item, &vtable, 4)
                || vtable != game.command_vtable)
                continue;
            peek_string(item + ITEM_COMMAND, command, sizeof command);
            peek_string(item + ITEM_ARGUMENT, argument, sizeof argument);
            if (menu_entry_id(command, argument, id, sizeof id))
                changed += relabel((void *)(item + ITEM_TEXT), name, argument, id, 0, p, detail);
        }
    }
    return changed;
}

__attribute__((thiscall, force_align_arg_pointer)) static void my_render(void *mode)
{
    static struct profile applied;
    static struct menu_options applied_options;
    static int have_applied;
    const void *vtable = NULL;

    if (!broken && mode == *game.menu_object && peek(mode, &vtable, 4)
        && vtable == game.mode_vtable) {
        struct profile profile;
        struct menu_options options = {0, 0};
        int state = -1;

        peek((char *)mode + MODE_STATE, &state, 4);
        ONCE("menu drawing hooked, first seen in state %d", state);
        if (!host.state(&profile, &options))
            options.enabled = 0;
        if (!options.enabled)
            memset(&profile, 0, sizeof profile);
        if (!have_applied || memcmp(&options, &applied_options, sizeof options) != 0
            || memcmp(&profile, &applied, sizeof profile) != 0) {
            int changed = update_labels(mode, options.enabled ? &profile : NULL, options.detail);

            host.log("menu labels updated: %d changed (counts %s)", changed,
                     options.enabled ? "on" : "off");
            applied = profile;
            applied_options = options;
            have_applied = 1;
        }
    }
    game.render(mode);
}

/* ---------------------------------------------------------------- small counts */

__attribute__((thiscall, force_align_arg_pointer)) static void my_draw(void *item,
                                                                      const struct vec2 *at,
                                                                      int selected)
{
    struct profile profile;
    struct menu_options options = {0, 0};
    struct wide text;
    wchar_t counts[LABEL_MAX];
    char command[32], argument[48], id[32];
    struct vec2 size = {0, 0}, place;
    const void *vtable = NULL;
    char *shape;
    float normal;
    char shadow;

    game.draw(item, at, selected);

    if (broken || !peek(item, &vtable, 4) || vtable != game.command_vtable)
        return;
    peek_string((char *)item + ITEM_COMMAND, command, sizeof command);
    peek_string((char *)item + ITEM_ARGUMENT, argument, sizeof argument);
    if (!menu_entry_id(command, argument, id, sizeof id) || !host.state(&profile, &options)
        || !options.enabled || !peek_wide((void *)((char *)item + ITEM_TEXT), &text)
        || !text.length || !menu_counts(&profile, options.detail, 0, id, text.buffer, counts))
        return;

    shape = (*game.shape)();
    if (!peek(shape + SHAPE_TEXT_SIZE, &normal, sizeof normal) || !(normal > 0)
        || !peek(shape + SHAPE_TEXT_SHADOW, &shadow, sizeof shadow)) {
        ONCE("text size not found: no small counts");
        return;
    }
    game.measure(&size, text.buffer); /* at the size the name was drawn */
    place.x = at->x - ((selected & 0xff) ? *game.selected_offset : 0) + size.x;
    place.y = at->y + normal * BASELINE_DROP;
    *(float *)(shape + SHAPE_TEXT_SIZE) = normal * SMALL;
    shape[SHAPE_TEXT_SHADOW] = 0; /* as for the name */
    game.draw_text(&place, counts, PIVOT_LEFT_MIDDLE);
    shape[SHAPE_TEXT_SHADOW] = shadow;
    *(float *)(shape + SHAPE_TEXT_SIZE) = normal;
    ONCE("first small counts drawn: text size %.3f, at %.3f %.3f, name width %.3f", normal,
         at->x, at->y, size.x);
}

/* ---------------------------------------------------------------- installation */

static int patch_slot(void **slot, void *hook)
{
    DWORD protection;

    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &protection))
        return 0;
    *slot = hook;
    VirtualProtect(slot, sizeof *slot, protection, &protection);
    return 1;
}

/* the first bytes of each function relied on */
static const struct {
    unsigned address;
    unsigned char code[16];
} EXPECTED[] = {
    {0x427550, {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x6a, 0xff, 0x68, 0xec, 0x5b, 0x60, 0x00, 0x64, 0xa1, 0x00}},
    {0x412370, {0x83, 0xec, 0x20, 0x57, 0x8d, 0xb9, 0xb4, 0x00, 0x00, 0x00, 0x8b, 0xcf, 0xff, 0x15, 0x04, 0xf8}},
    {0x4464b0, {0x6a, 0xff, 0x68, 0x17, 0x6a, 0x60, 0x00, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x64, 0x89}},
    {0x446000, {0x6a, 0xff, 0x68, 0x17, 0x6a, 0x60, 0x00, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x64, 0x89}},
};
#define RENDER_SLOT ((void **)0x633634) /* MenuMode vtable, slot 7 */
#define DRAW_SLOT ((void **)0x632fb0)   /* MenuCommand vtable, slot 1 */

static int expected_build(void)
{
    unsigned char code[16];
    void *render = NULL, *draw = NULL;
    size_t i;

    if ((ULONG_PTR)GetModuleHandleA(NULL) != 0x400000)
        return 0;
    for (i = 0; i < sizeof EXPECTED / sizeof *EXPECTED; i++)
        if (!peek((void *)(ULONG_PTR)EXPECTED[i].address, code, sizeof code)
            || memcmp(code, EXPECTED[i].code, sizeof code) != 0)
            return 0;
    return peek(RENDER_SLOT, &render, 4) && render == (void *)0x427550
           && peek(DRAW_SLOT, &draw, 4) && draw == (void *)0x412370;
}

int menu_install(const struct menu_host *h, int small)
{
    static int installed = -1;
    HMODULE strings;

    if (installed >= 0)
        return installed;
    installed = 0;
    host = *h;
    if (!expected_build()) {
        host.log("menu counts unavailable: this is not the supported build of AlanWake.exe");
        return 0;
    }
    strings = GetModuleHandleA("rl_sf_Win32.dll");
    game.assign = strings ? (assign_fn)(void *)GetProcAddress(strings, "??4WString@r@@QAIABV01@PB_W@Z")
                          : NULL;
    if (!game.assign) {
        host.log("menu counts unavailable: string function not found");
        return 0;
    }
    game.menu_object = (void **)0x6be940;
    game.mode_vtable = (void *)0x633618;
    game.command_vtable = (void *)0x632fac;
    game.selected_offset = (const float *)0x6be930;
    game.render = (render_fn)0x427550;
    game.draw = (draw_fn)0x412370;
    game.measure = (measure_fn)0x446000;
    game.draw_text = (draw_text_fn)0x4464b0;
    game.shape = (shape_fn *)0x62f4d0;
    small_text = small && patch_slot(DRAW_SLOT, (void *)my_draw);
    installed = patch_slot(RENDER_SLOT, (void *)my_render);
    host.log("menu hooks: %s, small text %s", installed ? "installed" : "FAILED",
             small_text ? "on" : "off");
    return installed;
}
