#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "counts.h"

struct item {
    unsigned char level, category, number, nightmare;
    unsigned char shown;  /* number within the chapter, as listed on screen */
    const char *text;     /* page title, cutout name, or a hint where it is; may be NULL */
};

struct level_name {
    int level;
    const char *episode, *chapter; /* chapter is empty for the special episodes */
};

#include "catalog.h"

void count_levels(const struct profile *p, int first, int last, struct remaining *out)
{
    size_t i;

    for (i = 0; i < sizeof CATALOG / sizeof *CATALOG; i++) {
        const struct item *item = &CATALOG[i];
        int missing;

        if (item->level < first || item->level > last)
            continue;
        missing = !p->found[item->category][item->number];
        if (item->nightmare) {
            out->nightmare_all++;
            out->nightmare += missing;
        } else {
            out->all[item->category]++;
            out->total++;
            out->left += missing;
            out->by_category[item->category] += missing;
        }
    }
}

/* String ids used by globaldb/menu.xml and the levels each one covers. */
static const struct entry {
    const char *id;
    int first, last;
} ENTRIES[] = {
    {"NAME_EPISODE_1", 1, 3},     {"NAME_EPISODE_2", 4, 5},     {"NAME_EPISODE_3", 9, 10},
    {"NAME_EPISODE_4", 11, 13},   {"NAME_EPISODE_5", 14, 15},   {"NAME_EPISODE_6", 16, 17},
    {"NAME_EPISODE_SPECIAL_1", 100, 100},                       {"NAME_EPISODE_SPECIAL_2", 200, 200},
    {"NAME_LEVEL_1", 1, 1},       {"NAME_LEVEL_2", 2, 2},       {"NAME_LEVEL_3", 3, 3},
    {"NAME_LEVEL_4", 4, 4},       {"NAME_LEVEL_5", 5, 5},       {"NAME_LEVEL_9", 9, 9},
    {"NAME_LEVEL_10", 10, 10},    {"NAME_LEVEL_11", 11, 11},    {"NAME_LEVEL_12", 12, 12},
    {"NAME_LEVEL_13", 13, 13},    {"NAME_LEVEL_14", 14, 14},    {"NAME_LEVEL_15", 15, 15},
    {"NAME_LEVEL_16", 16, 16},    {"NAME_LEVEL_17", 17, 17},
};

/* Short names, in the order of the categories: one item, several items. */
static const char *const NAMES[CAT_COUNT][2] = {
    {"page", "pages"},       {"thermos", "thermos"}, {"pyramid", "pyramids"},
    {"chest", "chests"},     {"radio", "radios"},    {"TV", "TVs"},
    {"sign", "signs"},       {"clock", "clocks"},
    {"cutout", "cutouts"},   {"video game", "video games"},
};

/* Append to a wide string; returns 0 if it did not fit. */
static int append(wchar_t *out, size_t size, const char *text)
{
    size_t n = wcslen(out);

    for (; *text; text++) {
        if (n + 1 >= size)
            return 0;
        out[n++] = (unsigned char)*text;
    }
    out[n] = 0;
    return 1;
}

static int describe(const struct remaining *r, int detail, wchar_t *out, size_t size)
{
    char part[64];
    int category, listed = 0, ok;

    out[0] = 0;
    if (r->left)
        snprintf(part, sizeof part, " (%d left", r->left);
    else
        snprintf(part, sizeof part, " (done");
    ok = append(out, size, part);
    for (category = 0; detail && category < CAT_COUNT; category++) {
        int n = r->by_category[category];

        if (!n)
            continue;
        snprintf(part, sizeof part, "%s%d %s", listed++ ? ", " : ": ", n, NAMES[category][n > 1]);
        ok = ok && append(out, size, part);
    }
    if (r->nightmare) {
        snprintf(part, sizeof part, "%s +%d Nightmare", detail && listed ? ";" : ",",
                 r->nightmare);
        ok = ok && append(out, size, part);
    }
    return ok && append(out, size, ")");
}

int menu_suffix(const struct profile *p, const char *id, int detail, wchar_t *out, size_t size)
{
    size_t i;

    for (i = 0; i < sizeof ENTRIES / sizeof *ENTRIES; i++) {
        struct remaining r = {0};

        if (strcmp(id, ENTRIES[i].id) != 0)
            continue;
        count_levels(p, ENTRIES[i].first, ENTRIES[i].last, &r);
        if (r.total + r.nightmare == 0)
            return 0; /* nothing to collect there */
        if (!describe(&r, detail, out, size))
            return 0;
        return 1;
    }
    return 0;
}

/* The levels making up the episode that contains `level`. */
static void episode_of(int level, int *first, int *last)
{
    size_t i;

    *first = *last = level;
    for (i = 0; i < sizeof ENTRIES / sizeof *ENTRIES; i++)
        if (strncmp(ENTRIES[i].id, "NAME_EPISODE_", 13) == 0 && level >= ENTRIES[i].first
            && level <= ENTRIES[i].last) {
            *first = ENTRIES[i].first;
            *last = ENTRIES[i].last;
        }
}

int chapter_text(const struct profile *p, int level, char *out, size_t size)
{
    const struct level_name *name = NULL;
    struct remaining here = {0}, episode = {0};
    int first, last, category, whole;
    size_t i, n = 0;

#define ADD(...)                                      \
    do {                                              \
        if (n < size)                                 \
            n += snprintf(out + n, size - n, __VA_ARGS__); \
    } while (0)

    for (i = 0; i < sizeof LEVEL_NAMES / sizeof *LEVEL_NAMES; i++)
        if (LEVEL_NAMES[i].level == level)
            name = &LEVEL_NAMES[i];
    if (!name || !size)
        return 0;
    episode_of(level, &first, &last);
    count_levels(p, level, level, &here);
    count_levels(p, first, last, &episode);
    whole = !name->chapter[0]; /* the chapter is the whole episode */

    ADD("%s\n", name->episode);
    if (!whole)
        ADD("%s\n", name->chapter);
    ADD("\n");
    if (!whole)
        ADD("found in this chapter  (in the episode)\n");
    for (category = 0; category < CAT_COUNT; category++) {
        if (!episode.all[category])
            continue;
        ADD("%s:  ", CATEGORIES[category].label);
        if (here.all[category])
            ADD("%d / %d", here.all[category] - here.by_category[category], here.all[category]);
        else
            ADD("none here");
        if (!whole)
            ADD("  (%d / %d)", episode.all[category] - episode.by_category[category],
                episode.all[category]);
        ADD("\n");
    }
    if (episode.nightmare_all) {
        ADD("Nightmare pages:  ");
        if (here.nightmare_all)
            ADD("%d / %d", here.nightmare_all - here.nightmare, here.nightmare_all);
        else
            ADD("none here");
        if (!whole)
            ADD("  (%d / %d)", episode.nightmare_all - episode.nightmare, episode.nightmare_all);
        ADD("\n");
    }
#undef ADD
    return 1;
}

/* The missing-items list is built line by line; long lists wrap. */
#define WRAP 60
#define TITLE_LINES 16

struct text {
    char *out;
    size_t size, length, line; /* `line` is where the current line starts */
};

static void put(struct text *t, const char *format, ...)
{
    va_list args;
    int n;

    if (t->length >= t->size)
        return;
    va_start(args, format);
    n = vsnprintf(t->out + t->length, t->size - t->length, format, args);
    va_end(args);
    if (n > 0)
        t->length += n;
}

static void new_line(struct text *t)
{
    put(t, "\n");
    t->line = t->length;
}

/* Add an entry to a comma-separated list, wrapping before it if the line is full. */
static void entry(struct text *t, int first, const char *word)
{
    if (!first)
        put(t, ",");
    if (t->length - t->line + strlen(word) + 1 > WRAP) {
        new_line(t);
        put(t, "   ");
    }
    put(t, " %s", word);
}

int missing_text(const struct profile *p, int level, int titles, int details, char *out,
                 size_t size)
{
    struct text t = {out, size, 0, 0};
    struct remaining here = {0};
    int category, title_lines = 0, untitled = 0;
    size_t i;

    if (!size)
        return 0;
    out[0] = '\0';
    count_levels(p, level, level, &here);
    if (here.total + here.nightmare_all == 0)
        return 0;
    put(&t, "Missing in this chapter");
    if (here.left + here.nightmare == 0) {
        new_line(&t);
        put(&t, "nothing: all found");
        return 1;
    }
    for (category = 0; category < CAT_COUNT; category++) {
        int listed = 0;
        int by_title = titles && category == CAT_MANUSCRIPTS;

        for (i = 0; i < sizeof CATALOG / sizeof *CATALOG; i++) {
            const struct item *item = &CATALOG[i];
            const char *hard = item->nightmare ? " (Nightmare)" : "";
            char word[96];

            if (item->level != level || item->category != category
                || p->found[category][item->number])
                continue;
            if (!listed++) {
                new_line(&t);
                if (category == CAT_MANUSCRIPTS)
                    put(&t, "%s:", CATEGORIES[category].label);
                else
                    put(&t, "%s (of %d):", CATEGORIES[category].label,
                        here.all[category]);
            }
            if (by_title) {
                if (title_lines >= TITLE_LINES) {
                    untitled++;
                    continue;
                }
                title_lines++;
                new_line(&t);
                put(&t, "    %d  %s%s", item->shown, item->text ? item->text : "", hard);
                continue;
            }
            if (category == CAT_MANUSCRIPTS)
                snprintf(word, sizeof word, "%d%s", item->shown, hard);
            else if (item->text && details)
                snprintf(word, sizeof word, "#%d (%s)", item->shown, item->text);
            else
                snprintf(word, sizeof word, "#%d", item->shown);
            entry(&t, listed == 1, word);
        }
        if (by_title && untitled) {
            new_line(&t);
            put(&t, "    +%d more pages", untitled);
        }
    }
    return 1;
}
