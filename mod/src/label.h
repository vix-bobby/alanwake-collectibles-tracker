/* Which menu entries get counts, and what is written there. No platform dependencies. */
#ifndef LABEL_H
#define LABEL_H

#include <stddef.h>
#include <wchar.h>

#include "profile.h"

/* Longest label the game keeps in a menu entry without reallocating. */
#define LABEL_MAX 128

/* The string id naming what a menu entry opens, from its command and argument, e.g.
 * ("load_episode", "gameworld:level_05") gives NAME_LEVEL_5. Returns 0 for other entries. */
int menu_entry_id(const char *command, const char *argument, char *id, size_t size);

/* The string id of a chapter list's title ("episode3" gives NAME_EPISODE_3), or 0. */
int menu_title_id(const char *submenu, char *id, size_t size);

/* Write the counts to show after `base` into `out` (LABEL_MAX wide), such that both together
 * fit in LABEL_MAX. Chapter list titles (`title`) get the total only, so that the list has
 * room; entries are split by type if `detail` and if that fits. Returns 0 for no counts. */
int menu_counts(const struct profile *p, int detail, int title, const char *id,
                const wchar_t *base, wchar_t *out);

#endif
