/* Collectibles left per level, and the menu text that reports them. No platform dependencies. */
#ifndef COUNTS_H
#define COUNTS_H

#include <wchar.h>

#include "profile.h"

struct remaining {
    int total;     /* collectibles there, Nightmare-only pages excluded */
    int left;      /* of those, not yet found */
    int nightmare; /* Nightmare-only pages not yet found */
    int by_category[CAT_COUNT]; /* `left`, split by category */
    int all[CAT_COUNT];         /* `total`, split by category */
    int nightmare_all;          /* Nightmare-only pages, found or not */
};

/* Describe the chapter `level` and its episode: found / total per category. Returns 0 if
 * the level is not one with a name. */
int chapter_text(const struct profile *p, int level, char *out, size_t size);

/* List what is still missing in the chapter `level`: page numbers (or, with `titles`, one
 * titled line per page) and the other items numbered within the chapter, with `details` also
 * naming the cutouts and saying where some items are. Returns 0 if there is nothing to
 * collect in the level. */
int missing_text(const struct profile *p, int level, int titles, int details, char *out,
                 size_t size);

/* Add the levels first..last (level numbers, e.g. 1..3, or 100) to *out. */
void count_levels(const struct profile *p, int first, int last, struct remaining *out);

/* If `id` names an episode or chapter menu entry, write the text to append and return 1.
 * With `detail`, the total is followed by the count of each category that has items left. */
int menu_suffix(const struct profile *p, const char *id, int detail, wchar_t *out, size_t size);

#endif
