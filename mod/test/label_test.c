/* Native test of which menu entries get counts, and of what kind. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "../src/label.h"

static const char *entry(const char *command, const char *argument)
{
    static char id[32];
    return menu_entry_id(command, argument, id, sizeof id) ? id : "";
}

int main(void)
{
    static struct profile nothing_found;
    wchar_t out[LABEL_MAX], long_name[LABEL_MAX];
    char id[32];
    int i;

    assert(!strcmp(entry("submenu", "episode3"), "NAME_EPISODE_3"));
    assert(!strcmp(entry("load_episode", "gameworld:level_05"), "NAME_LEVEL_5"));
    assert(!strcmp(entry("load_episode", "gameworld:level_17"), "NAME_LEVEL_17"));
    assert(!strcmp(entry("load_episode", "dlc1_world:level_100.lvese"), "NAME_EPISODE_SPECIAL_1"));
    assert(!strcmp(entry("load_episode", "dlc2_world:level_200.lvese"), "NAME_EPISODE_SPECIAL_2"));
    assert(!*entry("submenu", "cinematics1") && !*entry("submenu", "radios1"));
    assert(!*entry("submenu", "episode7") && !*entry("manuscript", "1"));
    assert(!*entry("load_episode", "gameworld:level_99999999999"));
    assert(!*entry("load_episode", "other:level_1") && !*entry("load_episode", ""));
    assert(menu_title_id("episode6", id, sizeof id) && !strcmp(id, "NAME_EPISODE_6"));
    assert(!menu_title_id("selectEpisode", id, sizeof id));

    /* entries list types, titles do not, unless asked for totals only */
    assert(menu_counts(&nothing_found, 1, 0, "NAME_EPISODE_1", L"E", out) && wcschr(out, L':'));
    assert(menu_counts(&nothing_found, 1, 0, "NAME_LEVEL_1", L"E", out) && wcschr(out, L':'));
    assert(menu_counts(&nothing_found, 1, 1, "NAME_EPISODE_1", L"E", out) && !wcschr(out, L':'));
    assert(menu_counts(&nothing_found, 0, 0, "NAME_LEVEL_1", L"E", out) && !wcschr(out, L':'));
    /* name and counts together always fit */
    for (i = 0; i < LABEL_MAX - 1; i++)
        long_name[i] = L'a';
    long_name[LABEL_MAX - 1] = 0;
    assert(!menu_counts(&nothing_found, 1, 0, "NAME_LEVEL_5", long_name, out));
    long_name[LABEL_MAX - 40] = 0;
    assert(menu_counts(&nothing_found, 1, 0, "NAME_LEVEL_5", long_name, out));
    assert(wcslen(long_name) + wcslen(out) < LABEL_MAX && !wcschr(out, L':'));
    puts("labels: PASS");
    return 0;
}
