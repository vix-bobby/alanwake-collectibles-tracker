/* Same logic as read_profile() in tools/collectibles.py. */
#include <string.h>

#include "profile.h"

const struct category CATEGORIES[CAT_COUNT] = {
    {NULL, "Manuscript pages", 106},
    {"THERMOS", "Coffee thermoses", 100},
    {"CANPYRAMIDS", "Can pyramids", 12},
    {"CHESTS", "Chests", 30},
    {"RADIOSHOWS", "Radio shows", 11},
    {"TVSHOWS", "TV shows", 14},
    {"SIGNS", "Signs", 25},
    {"COCONUTSONGS", "Coconut songs", 2},
    {"DLC1_ALARMCLOCKS", "Alarm clocks", 10},
    {"DLC1_CARDBOARDCUTOUTS", "Cardboard cutouts", 6},
    {"DLC2_NIGHTSPRINGSVIDEOGAMES", "Video games", 10},
};

static void add(struct profile *p, int category, unsigned char number)
{
    if (!p->found[category][number]) {
        p->found[category][number] = 1;
        p->count[category]++;
    }
}

/* A list is a count byte followed by that many strictly increasing page numbers; it is
 * located backwards from where it ends. Returns the offset of the count byte, or -1. */
static long list_before(const unsigned char *d, long end)
{
    long n, i;

    for (n = end - 1 < 255 ? end - 1 : 255; n >= 0; n--) {
        long start = end - n;
        if (d[start - 1] != n)
            continue;
        for (i = start + 1; i < end && d[i - 1] < d[i]; i++)
            ;
        if (i >= end)
            return start - 1;
    }
    return -1;
}

int profile_parse(const unsigned char *d, size_t size, struct profile *out)
{
    static const unsigned char anchor[] = "\x0b\0\0\0CANPYRAMIDS";
    size_t block, pos, i;
    long end;
    int entry, entries, list;

    memset(out, 0, sizeof *out);
    for (block = 1; block + sizeof anchor - 1 <= size; block++)
        if (memcmp(d + block, anchor, sizeof anchor - 1) == 0)
            break;
    if (block + sizeof anchor - 1 > size)
        return -1;

    /* two manuscript lists end where the collectible block begins */
    end = (long)block - 1;
    for (list = 0; list < 2; list++) {
        long start = list_before(d, end);
        if (start < 0)
            return -1;
        for (i = (size_t)start + 1; i < (size_t)end; i++)
            add(out, CAT_MANUSCRIPTS, d[i]);
        end = start;
    }

    entries = d[block - 1];
    pos = block;
    for (entry = 0; entry < entries; entry++) {
        size_t length, n;
        const unsigned char *name;
        int category;

        if (size - pos < 4)
            return -1;
        length = d[pos] | d[pos + 1] << 8 | d[pos + 2] << 16 | (size_t)d[pos + 3] << 24;
        pos += 4;
        if (length > 64 || size - pos < length + 1)
            return -1;
        name = d + pos;
        pos += length;
        n = d[pos++];
        if (size - pos < n)
            return -1;
        for (category = 1; category < CAT_COUNT; category++)
            if (strlen(CATEGORIES[category].key) == length
                && memcmp(CATEGORIES[category].key, name, length) == 0)
                for (i = 0; i < n; i++)
                    add(out, category, d[pos + i]);
        pos += n;
    }
    return 0;
}
