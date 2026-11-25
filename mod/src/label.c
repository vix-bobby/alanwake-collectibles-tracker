#include <stdio.h>
#include <string.h>

#include "counts.h"
#include "label.h"

static int episode_name(const char *name, char *id, size_t size)
{
    if (strncmp(name, "episode", 7) != 0 || name[7] < '1' || name[7] > '6' || name[8])
        return 0;
    snprintf(id, size, "NAME_EPISODE_%c", name[7]);
    return 1;
}

int menu_title_id(const char *submenu, char *id, size_t size)
{
    return episode_name(submenu, id, size);
}

int menu_entry_id(const char *command, const char *argument, char *id, size_t size)
{
    const char *level;
    int n = 0;

    if (strcmp(command, "submenu") == 0)
        return episode_name(argument, id, size);
    if (strcmp(command, "load_episode") != 0 || !(level = strstr(argument, ":level_")))
        return 0;
    for (level += 7; *level >= '0' && *level <= '9' && n < 1000; level++)
        n = n * 10 + *level - '0';
    if (strncmp(argument, "gameworld:", 10) == 0 && n >= 1 && n <= 17)
        snprintf(id, size, "NAME_LEVEL_%d", n);
    else if (strncmp(argument, "dlc1_world:", 11) == 0 && n == 100)
        snprintf(id, size, "NAME_EPISODE_SPECIAL_1");
    else if (strncmp(argument, "dlc2_world:", 11) == 0 && n == 200)
        snprintf(id, size, "NAME_EPISODE_SPECIAL_2");
    else
        return 0;
    return 1;
}

int menu_counts(const struct profile *p, int detail, int title, const char *id,
                const wchar_t *base, wchar_t *out)
{
    size_t used = wcslen(base), room;

    if (used >= LABEL_MAX)
        return 0;
    room = LABEL_MAX - used;
    /* fall back to the total alone if the split does not fit */
    return (detail && !title && menu_suffix(p, id, 1, out, room))
           || menu_suffix(p, id, 0, out, room);
}
