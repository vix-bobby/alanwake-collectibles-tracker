/* Parser for the Alan Wake profile file `config`. No platform dependencies. */
#ifndef PROFILE_H
#define PROFILE_H

#include <stddef.h>

enum {
    CAT_MANUSCRIPTS, CAT_THERMOS, CAT_CANPYRAMIDS, CAT_CHESTS, CAT_RADIOSHOWS, CAT_TVSHOWS,
    CAT_SIGNS, CAT_COCONUTSONGS, CAT_ALARMCLOCKS, CAT_CUTOUTS, CAT_VIDEOGAMES, CAT_COUNT
};

struct category {
    const char *key;    /* name in `config` (manuscripts have none) */
    const char *label;  /* shown on screen */
    int total;          /* from cid_collectible.bin / cid_notebook_page.bin */
};

extern const struct category CATEGORIES[CAT_COUNT];

struct profile {
    unsigned char found[CAT_COUNT][256]; /* found[category][item number] */
    int count[CAT_COUNT];
};

/* Returns 0 on success, -1 if the data does not look like a profile. */
int profile_parse(const unsigned char *data, size_t size, struct profile *out);

#endif
