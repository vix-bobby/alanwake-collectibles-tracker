/* Windows test of the menu hooks against stand-ins for the game's objects and functions,
 * using the game's calling conventions. Includes menu.c directly. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/menu.c"

#define EXPECT(x)                                        \
    do {                                                 \
        if (!(x)) {                                      \
            printf("FAIL line %d: %s\n", __LINE__, #x);  \
            return 1;                                    \
        }                                                \
    } while (0)

static char last_log[256];
static struct profile current;
static struct menu_options options = {1, 1};

static void record(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(last_log, sizeof last_log, format, args);
    va_end(args);
}

static int state(struct profile *p, struct menu_options *o)
{
    *p = current;
    *o = options;
    return 1;
}

/* stand-ins */
static const int mode_table, command_table;
static const float shift = 0.5f;
static int renders, draws, assigns;
static char engine[0x80];
static float drawn_size;
static struct vec2 drawn_at;
static wchar_t drawn[LABEL_MAX];
static int drawn_pivot, drawn_selected, drawn_shadow;

static void __attribute__((thiscall)) fake_render(void *mode)
{
    (void)mode;
    renders++;
}

static void __attribute__((thiscall)) fake_draw(void *item, const struct vec2 *at, int selected)
{
    (void)item, (void)at;
    drawn_selected = selected;
    draws++;
}

static void __attribute__((fastcall)) fake_measure(struct vec2 *size, const wchar_t *text)
{
    size->x = (float)wcslen(text);
    size->y = 1;
}

static void __attribute__((fastcall)) fake_draw_text(const struct vec2 *at, const wchar_t *text,
                                                     int pivot)
{
    drawn_at = *at;
    drawn_size = *(float *)(engine + SHAPE_TEXT_SIZE);
    drawn_pivot = pivot;
    drawn_shadow = engine[SHAPE_TEXT_SHADOW];
    wcscpy(drawn, text);
}

static char *__attribute__((fastcall)) fake_shape(void)
{
    return engine;
}

static void *__attribute__((fastcall)) fake_assign(struct wide *s, const wchar_t *text)
{
    assigns++;
    wcscpy(s->buffer, text);
    s->length = wcslen(text);
    return s;
}

static void set_wide(void *at, const wchar_t *text)
{
    struct wide *s = at;
    s->data = s->inline_data = s->buffer;
    s->inline_capacity = s->capacity = LABEL_MAX;
    fake_assign(s, text);
}

static char *make_item(const char *command, const char *argument, const wchar_t *text)
{
    char *item = calloc(1, 0x400);
    *(const void **)item = &command_table;
    *(const char **)(item + ITEM_COMMAND) = command;
    *(const char **)(item + ITEM_ARGUMENT) = argument;
    set_wide(item + ITEM_TEXT, text);
    return item;
}

static const wchar_t *text_of(const char *at)
{
    return ((const struct wide *)at)->buffer;
}

static void make_submenu(char *submenu, const char *name, const wchar_t *title, char **items, int n)
{
    *(const char **)(submenu + SUBMENU_NAME) = name;
    set_wide(submenu + SUBMENU_TITLE, title);
    *(char ***)(submenu + SUBMENU_ITEMS) = items;
    *(unsigned *)(submenu + SUBMENU_ITEM_COUNT) = n;
}

int main(void)
{
    static char mode[0x100], submenus[3 * SUBMENU_SIZE], other[0x400];
    static char *episodes[3], *chapters[1], *rebuilt[1];
    static void *menu;
    struct menu_host h = {record, state};
    render_fn render = my_render;
    draw_fn draw = my_draw;
    struct vec2 at = {2, 3};
    volatile int canary = 0x5a5a;
    int i;

    EXPECT(sizeof(struct wide) == 0x114);
    EXPECT(!menu_install(&h, 1)); /* this program is not the game */
    EXPECT(strstr(last_log, "not the supported build"));

    host = h;
    game.menu_object = &menu;
    game.mode_vtable = &mode_table;
    game.command_vtable = &command_table;
    game.selected_offset = &shift;
    game.render = fake_render;
    game.draw = fake_draw;
    game.measure = fake_measure;
    game.draw_text = fake_draw_text;
    {
        static shape_fn slot = fake_shape;
        game.shape = &slot;
    }
    game.assign = fake_assign;

    episodes[0] = make_item("submenu", "episode1", L"Episode 1");
    episodes[1] = make_item("load_episode", "dlc1_world:level_100.lvese", L"Signal");
    episodes[2] = other; /* not a command entry: must be ignored */
    chapters[0] = make_item("load_episode", "gameworld:level_01", L"Dream");
    rebuilt[0] = make_item("load_episode", "gameworld:level_02", L"Continue");
    make_submenu(submenus, "selectEpisode", L"Episodes", episodes, 3);
    make_submenu(submenus + SUBMENU_SIZE, "episode1", L"Episode 1", chapters, 1);
    make_submenu(submenus + 2 * SUBMENU_SIZE, "load", L"Continue", rebuilt, 1);
    *(const void **)mode = &mode_table;
    *(char **)(mode + MODE_SUBMENUS) = submenus;
    *(unsigned *)(mode + MODE_SUBMENU_COUNT) = 3;
    menu = mode;

    /* normal size: entries carry the split, the chapter list title the total */
    small_text = 0;
    render(mode);
    EXPECT(renders == 1);
    EXPECT(wcsncmp(text_of(episodes[0] + ITEM_TEXT), L"Episode 1 (", 11) == 0);
    EXPECT(wcschr(text_of(episodes[0] + ITEM_TEXT), L':'));
    EXPECT(wcschr(text_of(episodes[1] + ITEM_TEXT), L':'));
    EXPECT(wcschr(text_of(chapters[0] + ITEM_TEXT), L':'));
    EXPECT(wcsncmp(text_of(submenus + SUBMENU_SIZE + SUBMENU_TITLE), L"Episode 1 (", 11) == 0);
    EXPECT(!wcschr(text_of(submenus + SUBMENU_SIZE + SUBMENU_TITLE), L':'));
    EXPECT(wcscmp(text_of(submenus + SUBMENU_TITLE), L"Episodes") == 0);
    EXPECT(wcscmp(text_of(rebuilt[0] + ITEM_TEXT), L"Continue") == 0);

    /* nothing changed: nothing rewritten */
    assigns = 0;
    for (i = 0; i < 1000; i++)
        render(mode);
    EXPECT(assigns == 0 && renders == 1001);

    /* the profile changes during play: counts follow, never stacking */
    {
        wchar_t before[LABEL_MAX];
        wcscpy(before, text_of(chapters[0] + ITEM_TEXT));
        current.found[CAT_THERMOS][1] = 1;
        render(mode);
        EXPECT(wcscmp(before, text_of(chapters[0] + ITEM_TEXT)) != 0);
        EXPECT(wcsncmp(text_of(chapters[0] + ITEM_TEXT), L"Dream (", 7) == 0);
        EXPECT(!wcschr(wcschr(text_of(chapters[0] + ITEM_TEXT), L'(') + 1, L'('));
    }

    /* switched off: originals return; on again: counts return */
    options.enabled = 0;
    render(mode);
    EXPECT(wcscmp(text_of(chapters[0] + ITEM_TEXT), L"Dream") == 0);
    EXPECT(wcscmp(text_of(episodes[0] + ITEM_TEXT), L"Episode 1") == 0);
    EXPECT(wcscmp(text_of(submenus + SUBMENU_SIZE + SUBMENU_TITLE), L"Episode 1") == 0);
    options.enabled = 1;
    render(mode);
    EXPECT(wcsncmp(text_of(chapters[0] + ITEM_TEXT), L"Dream (", 7) == 0);

    /* small text: names stay, titles still get the total, counts are drawn after the name */
    options.enabled = 0;
    render(mode);
    small_text = 1;
    options.enabled = 1;
    render(mode);
    EXPECT(wcscmp(text_of(chapters[0] + ITEM_TEXT), L"Dream") == 0);
    EXPECT(wcschr(text_of(submenus + SUBMENU_SIZE + SUBMENU_TITLE), L'('));
    *(float *)(engine + SHAPE_TEXT_SIZE) = 2.0f;
    engine[SHAPE_TEXT_SHADOW] = 1;
    for (i = 0; i < 100000; i++) /* an unbalanced stack would not survive this */
        draw(chapters[0], &at, 0);
    EXPECT(draws == 100000 && canary == 0x5a5a);
    EXPECT(wcsncmp(drawn, L" (", 2) == 0 && wcschr(drawn, L':'));
    EXPECT(drawn_size > 1.39f && drawn_size < 1.41f && drawn_pivot == PIVOT_LEFT_MIDDLE);
    EXPECT(*(float *)(engine + SHAPE_TEXT_SIZE) == 2.0f);
    EXPECT(drawn_shadow == 0 && engine[SHAPE_TEXT_SHADOW] == 1);
    EXPECT(drawn_at.x == 2 + 5);
    EXPECT(drawn_at.y > 4.05f && drawn_at.y < 4.07f); /* 3 + 0.53 * text size 2 */
    draw(chapters[0], &at, 1);
    EXPECT(drawn_selected == 1 && drawn_at.x == 2 + 5 - shift);
    drawn[0] = 0;
    draw(other, &at, 0);
    options.enabled = 0;
    draw(chapters[0], &at, 0);
    EXPECT(!drawn[0]);

    /* damaged data is left alone */
    options.enabled = 1;
    small_text = 0;
    ((struct wide *)(chapters[0] + ITEM_TEXT))->capacity = 7;
    current.found[CAT_THERMOS][2] = 1;
    render(mode);
    EXPECT(wcscmp(text_of(chapters[0] + ITEM_TEXT), L"Dream") == 0);
    *(unsigned *)(mode + MODE_SUBMENU_COUNT) = 100000;
    current.found[CAT_THERMOS][3] = 1;
    render(mode);
    EXPECT(broken && strstr(last_log, "menu labels updated: 0"));
    printf("menu hooks: PASS\n");
    return 0;
}
