/*
 * A marker on the game's radar for the nearest missing collectible, through the game's own
 * AddMapEntity code. Markers are shown while the radar_markers option is on (F4); with
 * radar_hooks=0 nothing below is installed.
 *
 * Two places in the game are changed, both only after their code was checked byte for byte:
 *
 *   HUDMode::update (method table slot 0x634eb8). The game updates the radar there itself, on
 *       the game's main thread, so markers are added and removed where nothing else touches
 *       the radar at the same time.
 *   the call of Radar::save at 0x48355e, the only place the radar is written into the save.
 *       The game itself never adds radar entries (no script calls AddMapEntity), so every
 *       entry is ours: the guard sets the count to 0 for the length of the call, and the save
 *       gets what the game writes without the mod. It does so only on the thread markers are
 *       added on, and only if every entry is ours; otherwise it leaves the radar alone, logs
 *       "RADAR SAVE GUARD SKIPPED", removes our marker and adds no more.
 *
 * If either change cannot be made, neither is: no marker can exist without the guard.
 *
 * Where an object is, and whether it is hidden (a mesh) or switched off (a trigger), is read
 * from memory for the object types below, whose layout was taken from the game's code; for
 * other types the game's own methods are called, as the radar itself does each frame.
 * Addresses: Steam build of AlanWake.exe dated 2020-05-15 (no ASLR); see docs/radar.md.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "radar.h"

/* r::GlobalID */
struct gid {
    unsigned cls, id;
};

struct vec3 {
    float x, y, z;
};

struct radar_item {
    unsigned char category, number, nightmare;
};

/* an object to mark, and the items it stands for (radios and TVs can carry several shows) */
struct radar_target {
    unsigned cls, id;
    unsigned mesh;    /* class 100 object that must not be hidden, 0 = none */
    unsigned trigger; /* class 85 trigger that must be switched on, 0 = none */
    unsigned short level;
    unsigned char count;
    struct radar_item items[4];
};

#include "radar_targets.h"
#define TARGET_COUNT ((int)(sizeof RADAR_TARGETS / sizeof *RADAR_TARGETS))

#define CLASS_MESH 100
#define CLASS_TRIGGER 85

/* a radar entry, 0xe0 bytes */
#define ENTRY_SIZE 0xe0
#define ENTRY_GID 0x04
#define ENTRY_POSITION 0x30 /* the object's world transform is copied to +0xc each frame */
#define ENTRY_ACTIVE 0xd4   /* 0 once removed: the entry fades out */
#define RADAR_ENTRIES 0x00
#define RADAR_COUNT 0x04
#define MAX_ENTRIES 64 /* more would mean the layout is not what we think */

/* the radar is centred on the active camera: its transform at + 0x4dc, position + 0x24 */
#define CAMERA_POSITION 0x500
/* a transform: position at + 0x24 */
#define TRANSFORM_POSITION 0x24

/* What GlobalIDMap::getPointer gives for an object is its script; method 7 of the script
 * returns the object (0x573740: [+0xc], 0x578140: [+0x10]), whose method 1 returns its
 * transform. */
#define SCRIPT_MESH 0x64ba94     /* DynamicObjectScript: Hide sets bit 0 of +0x28 (0x570b73) */
#define SCRIPT_TRIGGER 0x64c070  /* TriggerScript: EnableTrigger stores +0x18 (0x573a67) */
#define SCRIPT_WAYPOINT 0x64c180 /* WaypointScript */
#define MESH_HIDDEN 0x28
#define TRIGGER_ENABLED 0x18
#define OBJECT_MESH 0x639e4c     /* DynamicObject: transform at + 0x70 (0x44e770) */
#define OBJECT_TRIGGER 0x6466a4  /* Trigger: transform at + 0x40 (0x5680d0) */
#define OBJECT_WAYPOINT 0x6470bc /* Waypoint: transform at [+ 0x28] + 0x10 (0x544c90) */

#define SPRITE 0 /* yellow dot; 0 also keeps the game from playing its marker sounds */
/* Move the marker only to an item clearly nearer: by a fifth and by at least 5 m. */
#define SWITCH_RATIO 0.8f
#define SWITCH_MIN 5.0f

typedef void(__attribute__((thiscall)) * update_fn)(void *hud);
typedef void(__attribute__((thiscall)) * add_fn)(void *hud, const struct gid *gid, int sprite,
                                                 const char *text, int fade_sprite,
                                                 int fade_text);
typedef void(__attribute__((thiscall)) * remove_fn)(void *radar, const struct gid *gid);
typedef void(__attribute__((thiscall)) * save_fn)(void *radar, void *stream);
typedef void *(__attribute__((fastcall)) * map_instance_fn)(void);
typedef void *(__attribute__((thiscall)) * map_pointer_fn)(void *map, const struct gid *gid);
typedef void *(__attribute__((thiscall)) * getter_fn)(void *self); /* virtual, no arguments */

/* everything taken from the game; the test replaces it with stand-ins */
static struct {
    void **hud;   /* HUDMode* */
    void **radar; /* Radar* */
    const int *camera_state;
    void **cameras[8]; /* the camera global for each state, as in 0x43eb60 */
    void **difficulty; /* object whose first int is 0 normal, 1 hard, 2 nightmare */
    const void *script_mesh, *script_trigger, *script_waypoint;
    const void *object_mesh, *object_trigger, *object_waypoint;
    update_fn update;
    add_fn add;
    remove_fn remove;
    save_fn save;
    map_instance_fn map_instance;
    map_pointer_fn map_pointer;
} game;

static struct radar_host host;
static int installed;
static DWORD hud_thread;   /* where HUDMode::update runs */
static int guard_confirmed; /* a save went through the guard on hud_thread (logged once) */
static int stopped;         /* something was not as expected: add nothing more */
static int marker = -1;     /* target we added, or -1 */
static void *last_radar;
static int last_difficulty = -2;

static int peek(const void *address, void *out, size_t size)
{
    SIZE_T got = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, out, size, &got)
           && got == size;
}

static int same(const struct gid *a, const struct gid *b)
{
    return a->cls == b->cls && a->id == b->id;
}

static const struct gid *gid_of(int t)
{
    return (const struct gid *)&RADAR_TARGETS[t].cls;
}

static int is_target(const struct gid *g)
{
    int t;

    for (t = 0; t < TARGET_COUNT; t++)
        if (same(g, gid_of(t)))
            return 1;
    return 0;
}

/* "Coffee thermoses 12, level 4" (the first item it stands for) */
static const char *name_of(int t, char *out, size_t size)
{
    const struct radar_target *r = &RADAR_TARGETS[t];

    snprintf(out, size, "%s %d, level %d", CATEGORIES[r->items[0].category].label,
             r->items[0].number, r->level);
    return out;
}

static void stop(const char *why)
{
    if (!stopped)
        host.log("radar: markers stopped: %s", why);
    stopped = 1;
}

/* Log a problem once per kind. */
#define ONCE(...)                  \
    do {                           \
        static int done_;          \
        if (!done_++)              \
            host.log(__VA_ARGS__); \
    } while (0)

/* The radar's entries: returns the count, or -1 if they cannot be read. With `ours`, also
 * counts the entries holding one of our targets; with `gid`, sets *has to whether it is
 * there and *entry to its address. */
static int entries(const char *radar, int *ours, const struct gid *gid, int *has, char **entry)
{
    char *base = NULL;
    int count = 0, i;

    if (ours)
        *ours = 0;
    if (has)
        *has = 0;
    if (!peek(radar + RADAR_ENTRIES, &base, 4) || !peek(radar + RADAR_COUNT, &count, 4)
        || count < 0 || count > MAX_ENTRIES || (count && !base))
        return -1;
    for (i = 0; i < count; i++) {
        struct gid g;

        if (!peek(base + i * ENTRY_SIZE + ENTRY_GID, &g, sizeof g))
            return -1;
        if (gid && has && same(&g, gid)) {
            *has = 1;
            if (entry)
                *entry = base + i * ENTRY_SIZE;
        }
        if (ours && is_target(&g))
            ++*ours;
    }
    return count;
}

static int has_entry(const char *radar, const struct gid *gid)
{
    int has = 0;

    entries(radar, NULL, gid, &has, NULL);
    return has;
}

/* The object's script, if it is loaded. The game's AddMapEntity crashes on one that is not. */
static void *lookup(unsigned cls, unsigned id)
{
    struct gid gid = {cls, id};
    void *map = game.map_instance();

    return map ? game.map_pointer(map, &gid) : NULL;
}

static int plausible(const struct vec3 *v)
{
    return isfinite(v->x) && isfinite(v->y) && isfinite(v->z) && fabsf(v->x) < 1e6f
           && fabsf(v->y) < 1e6f && fabsf(v->z) < 1e6f;
}

/* Call a virtual method without arguments, as the game does, after checking its table. */
static void *call_virtual(void *object, int slot)
{
    void **table = NULL;
    void *method = NULL;

    if (!peek(object, &table, 4) || !peek(table + slot, &method, 4) || !method)
        return NULL;
    return ((getter_fn)method)(object);
}

/* Where a loaded object is. */
static int position_of(void *script, struct vec3 *out)
{
    const void *table = NULL;
    char *object = NULL, *transform = NULL;

    if (!peek(script, &table, 4))
        return 0;
    if (table == game.script_mesh || table == game.script_waypoint)
        peek((char *)script + 0xc, &object, 4);
    else if (table == game.script_trigger)
        peek((char *)script + 0x10, &object, 4);
    else {
        ONCE("radar: object script of an unexpected type (%p): asking the game", table);
        object = call_virtual(script, 7);
    }
    if (!object || !peek(object, &table, 4))
        return 0;
    if (table == game.object_mesh)
        transform = object + 0x70;
    else if (table == game.object_trigger)
        transform = object + 0x40;
    else if (table == game.object_waypoint) {
        if (!peek(object + 0x28, &transform, 4) || !transform)
            return 0;
        transform += 0x10;
    } else {
        ONCE("radar: object of an unexpected type (%p): asking the game", table);
        transform = call_virtual(object, 1);
    }
    return transform && peek(transform + TRANSFORM_POSITION, out, sizeof *out) && plausible(out);
}

/* Why the item cannot be picked up now (its mesh hidden, its trigger switched off), or NULL.
 * What cannot be told counts as available. */
static const char *unavailable(const struct radar_target *r)
{
    const void *table = NULL;
    void *script;
    unsigned flags = 0;
    unsigned char enabled = 1;

    if (r->mesh) {
        if (!(script = lookup(CLASS_MESH, r->mesh)) || !peek(script, &table, 4))
            return "mesh not loaded";
        if (table == game.script_mesh && peek((char *)script + MESH_HIDDEN, &flags, 4)
            && (flags & 1))
            return "mesh hidden";
    }
    if (r->trigger) {
        if (!(script = lookup(CLASS_TRIGGER, r->trigger)) || !peek(script, &table, 4))
            return "trigger not loaded";
        if (table == game.script_trigger
            && peek((char *)script + TRIGGER_ENABLED, &enabled, 1) && !enabled)
            return "trigger switched off";
    }
    return NULL;
}

static int available(const struct radar_target *r)
{
    return unavailable(r) == NULL;
}

/* Entries of ours that are shown but are not the marker (one a save kept despite a skipped
 * guard, say): taken away. They fade, then the game erases them. */
static void remove_strays(char *radar)
{
    char *base = NULL;
    int count = 0, i;

    if (!peek(radar + RADAR_ENTRIES, &base, 4) || !peek(radar + RADAR_COUNT, &count, 4)
        || count < 0 || count > MAX_ENTRIES)
        return;
    for (i = 0; i < count; i++) {
        struct gid g;
        unsigned char active = 0;

        if (peek(base + i * ENTRY_SIZE + ENTRY_GID, &g, sizeof g)
            && peek(base + i * ENTRY_SIZE + ENTRY_ACTIVE, &active, 1) && active
            && is_target(&g) && (marker < 0 || !same(&g, gid_of(marker)))) {
            game.remove(radar, &g);
            host.log("radar: an unexpected marker of ours removed");
        }
    }
}

/* Is any item of the target still to find, on this difficulty? */
static int wanted(const struct radar_target *r, const struct profile *p, int nightmare)
{
    int i;

    for (i = 0; i < r->count && i < 4; i++)
        if (!p->found[r->items[i].category][r->items[i].number]
            && (!r->items[i].nightmare || nightmare))
            return 1;
    return 0;
}

/* The radar's centre: the active camera, chosen as 0x43eb60 does, read without calling it. */
static int camera_position(struct vec3 *out)
{
    int state = -1;
    char *camera = NULL;

    if (!peek(game.camera_state, &state, 4) || state < 0 || state > 7
        || !peek(game.cameras[state], &camera, 4) || !camera)
        return 0;
    return peek(camera + CAMERA_POSITION, out, sizeof *out) && plausible(out);
}

/* 0 normal, 1 hard, 2 nightmare, -1 unknown */
static int difficulty(void)
{
    char *system = NULL;
    int level = -1;

    if (!peek(game.difficulty, &system, 4) || !system || !peek(system, &level, 4) || level < 0
        || level > 2)
        return -1;
    return level;
}

static float distance(const struct vec3 *a, const struct vec3 *b)
{
    float x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;
    return sqrtf(x * x + y * y + z * z);
}

static void remove_marker(char *radar, const char *why)
{
    char name[80];

    if (has_entry(radar, gid_of(marker)))
        game.remove(radar, gid_of(marker)); /* it fades, then the game erases it */
    host.log("radar: marker on %s removed: %s", name_of(marker, name, sizeof name), why);
    marker = -1;
}

static int add_marker(void *hud, char *radar, int t)
{
    if (!lookup(RADAR_TARGETS[t].cls, RADAR_TARGETS[t].id)) /* right before the call */
        return 0;
    game.add(hud, gid_of(t), SPRITE, NULL, 0, 0);
    if (!has_entry(radar, gid_of(t))) {
        stop("the marker did not appear in the radar");
        return 0;
    }
    marker = t;
    return 1;
}

/* Once a second, on the game's main thread, inside HUDMode::update. */
static void tick(void)
{
    struct profile profile;
    struct vec3 camera, where;
    void *hud = *game.hud, *radar = *game.radar;
    int markers = 0, have_profile, nightmare, nearest = -1, t;
    float best = 0;
    char name[80], other[80];

    if (radar != last_radar) {
        marker = -1; /* a new radar (or none, in menus) holds nothing of ours */
        last_radar = radar;
    }
    if (!hud || !radar)
        return;
    if (entries(radar, NULL, NULL, NULL, NULL) < 0) {
        stop("radar entries not as expected");
        return;
    }
    have_profile = host.state(&profile, &markers);

    if (marker >= 0) {
        const struct radar_target *r = &RADAR_TARGETS[marker];

        if (stopped)
            remove_marker(radar, "stopped");
        else if (!has_entry(radar, gid_of(marker)))
            marker = -1; /* the object unloaded, or the level was reloaded: chosen again */
        else if (!markers)
            remove_marker(radar, "markers switched off");
        else if (have_profile && !wanted(r, &profile, difficulty() == 2))
            remove_marker(radar, "found");
        else if (!available(r))
            remove_marker(radar, "picked up or not available");
    }
    remove_strays(radar);
    if (stopped || !markers || !have_profile || !camera_position(&camera))
        return;

    nightmare = difficulty();
    if (nightmare != last_difficulty) {
        host.log("radar: difficulty %s", nightmare == 2   ? "Nightmare"
                                         : nightmare >= 0 ? "normal or hard"
                                                          : "unknown: Nightmare-only pages left out");
        last_difficulty = nightmare;
    }
    nightmare = nightmare == 2;
    for (t = 0; t < TARGET_COUNT; t++) {
        static unsigned char told[TARGET_COUNT];
        const struct radar_target *r = &RADAR_TARGETS[t];
        const char *why;
        void *script;
        float d;

        if (!wanted(r, &profile, nightmare) || !(script = lookup(r->cls, r->id))
            || !position_of(script, &where))
            continue;
        if ((why = unavailable(r))) {
            /* once per item and game start: says why a loaded item gets no dot */
            if (!told[t]++)
                host.log("radar: %s not marked: %s", name_of(t, name, sizeof name), why);
            continue;
        }
        d = distance(&camera, &where);
        if (nearest < 0 || d < best) {
            nearest = t;
            best = d;
        }
    }
    if (nearest < 0 || nearest == marker)
        return;
    if (has_entry(radar, gid_of(nearest)))
        return; /* still fading out after an earlier removal: wait */
    if (marker < 0) {
        if (add_marker(hud, radar, nearest))
            host.log("radar: marker on %s, %.0f m away", name_of(nearest, name, sizeof name),
                     best);
        return;
    }
    {
        char *entry = NULL;
        int has = 0;
        float now;

        entries(radar, NULL, gid_of(marker), &has, &entry);
        if (!has || !peek(entry + ENTRY_POSITION, &where, sizeof where) || !plausible(&where))
            return;
        now = distance(&camera, &where);
        if (best < now * SWITCH_RATIO && now - best >= SWITCH_MIN) {
            name_of(marker, other, sizeof other);
            remove_marker(radar, "a nearer item is there");
            if (add_marker(hud, radar, nearest))
                host.log("radar: marker moved from %s (%.0f m) to %s (%.0f m)", other, now,
                         name_of(nearest, name, sizeof name), best);
        }
    }
}

__attribute__((thiscall, force_align_arg_pointer)) static void my_update(void *hud)
{
    static DWORD last;
    DWORD now = GetTickCount();

    if (!hud_thread) {
        hud_thread = GetCurrentThreadId();
        host.log("radar: game thread %lu", (unsigned long)hud_thread);
    }
    if (GetCurrentThreadId() == hud_thread && (!last || now - last >= 1000)) {
        last = now ? now : 1;
        tick();
    }
    game.update(hud);
}

__attribute__((thiscall, force_align_arg_pointer)) static void my_save(void *radar,
                                                                     void *stream)
{
    DWORD thread = GetCurrentThreadId();
    int ours = 0, count = radar ? entries(radar, &ours, NULL, NULL, NULL) : -1;

    if (count == 0) {
        game.save(radar, stream); /* nothing to hide */
        if (!hud_thread)
            ; /* before the first HUD update (loading): proves nothing either way */
        else if (thread == hud_thread && !guard_confirmed) {
            guard_confirmed = 1;
            host.log("radar: save guard confirmed: saves run on the game thread");
        } else if (thread != hud_thread)
            stop("a save ran on another thread than the HUD");
        return;
    }
    if (count < 0 || thread != hud_thread || ours != count) {
        host.log("RADAR SAVE GUARD SKIPPED: %s (thread %lu, game thread %lu, %d entries, %d "
                 "ours); the save may contain %d marker(s)",
                 count < 0 ? "radar not readable" : thread != hud_thread ? "other thread"
                                                                         : "entries not ours",
                 (unsigned long)thread, (unsigned long)hud_thread, count, ours, ours);
        stop("save guard skipped");
        game.save(radar, stream);
        return;
    }
    /* every entry is ours, fading ones included: the save sees none */
    *(int *)((char *)radar + RADAR_COUNT) = 0;
    game.save(radar, stream);
    *(int *)((char *)radar + RADAR_COUNT) = count;
    host.log("radar: saved without the %d marker(s)", count);
}

const char *radar_note(void)
{
    if (!installed)
        return "\nNot available: see the log";
    if (stopped)
        return "\nStopped: see the log";
    return "\nShown on the radar, for items in the area";
}

/* ---------------------------------------------------------------- installation */

static const struct {
    unsigned address;
    unsigned char size, code[16];
} EXPECTED[] = {
    /* HUDMode::update */
    {0x4327c0, 16, {0x83, 0xec, 0x10, 0x56, 0x8b, 0xf1, 0xe8, 0x85, 0x96, 0xfd, 0xff, 0x8b, 0xc8, 0xe8, 0x6e, 0x55}},
    /* HUDMode AddMapEntity: GID, sprite, text, fade sprite, fade text */
    {0x432990, 16, {0x83, 0xec, 0x30, 0x56, 0x57, 0xe8, 0x76, 0x85, 0xfd, 0xff, 0x50, 0x8d, 0x4c, 0x24, 0x0c, 0xe8}},
    /* Radar::save */
    {0x5841e0, 16, {0x55, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x7c, 0x24, 0x10, 0x8d, 0x86, 0x3c, 0x01, 0x00, 0x00, 0x50}},
    /* Radar::removeMapEntity */
    {0x584940, 16, {0x56, 0x57, 0x8b, 0xf9, 0x8b, 0x47, 0x04, 0x8b, 0x37, 0x69, 0xc0, 0xe0, 0x00, 0x00, 0x00, 0x03}},
    /* Radar::addMapEntity */
    {0x586410, 16, {0x64, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x6a, 0xff, 0x68, 0x87, 0x9d, 0x61, 0x00, 0x50, 0x8b, 0x44}},
    /* the getters of the radar and the HUD */
    {0x583c40, 6, {0xa1, 0xe8, 0x8c, 0x6e, 0x00, 0xc3}},
    {0x4318e0, 6, {0xa1, 0xc8, 0xf9, 0x6b, 0x00, 0xc3}},
    /* in the world save: push esi; call Radar getter; mov ecx, eax; call Radar::save; push esi */
    {0x483556, 14, {0x56, 0xe8, 0xe4, 0x06, 0x10, 0x00, 0x8b, 0xc8, 0xe8, 0x7d, 0x0c, 0x10, 0x00, 0x56}},
    /* the active camera: state at 0x6c0894, at most 7, then a table of cases */
    {0x43eb60, 16, {0xa1, 0x94, 0x08, 0x6c, 0x00, 0x83, 0xf8, 0x07, 0x77, 0x7f, 0xff, 0x24, 0x85, 0xec, 0xeb, 0x43}},
    {0x43eb71, 4, {0x84, 0xc9, 0x74, 0x56}}, /* state 0: with argument 0, jump to 0x43ebcb */
    {0x43ebcb, 6, {0xa1, 0x98, 0x08, 0x6c, 0x00, 0xc3}}, /* state 0, without the zoom copy */
    {0x43ebd1, 6, {0xa1, 0x9c, 0x08, 0x6c, 0x00, 0xc3}},
    {0x43ebd7, 6, {0xa1, 0xa0, 0x08, 0x6c, 0x00, 0xc3}},
    {0x43ebdd, 6, {0xa1, 0xa4, 0x08, 0x6c, 0x00, 0xc3}},
    {0x43ebe3, 6, {0xa1, 0xac, 0x08, 0x6c, 0x00, 0xc3}},
    /* its case table: 0x43eb71 (state 0), d7, d7, dd, d1, d1, d7, e3 */
    {0x43ebec, 16, {0x71, 0xeb, 0x43, 0x00, 0xd7, 0xeb, 0x43, 0x00, 0xd7, 0xeb, 0x43, 0x00, 0xdd, 0xeb, 0x43, 0x00}},
    {0x43ebfc, 16, {0xd1, 0xeb, 0x43, 0x00, 0xd1, 0xeb, 0x43, 0x00, 0xd7, 0xeb, 0x43, 0x00, 0xe3, 0xeb, 0x43, 0x00}},
    /* the camera position getter the AI debug view uses: camera + 0x500 */
    {0x43b300, 7, {0x8d, 0x81, 0x00, 0x05, 0x00, 0x00, 0xc3}},
    /* difficulty: the getter, and its save writing the first int of the object */
    {0x5af350, 6, {0xa1, 0x60, 0xb5, 0x76, 0x00, 0xc3}},
    {0x5af4f0, 8, {0x51, 0x56, 0x8b, 0xf1, 0x8b, 0x06, 0x57, 0x8b}},
    /* scripts' method 7 (the object) and how Hide and EnableTrigger store their state */
    {0x573740, 4, {0x8b, 0x41, 0x0c, 0xc3}},
    {0x578140, 4, {0x8b, 0x41, 0x10, 0xc3}},
    {0x570b73, 4, {0x83, 0x4e, 0x28, 0x01}},
    {0x570b79, 4, {0x83, 0x66, 0x28, 0xfe}},
    {0x573a67, 3, {0x88, 0x46, 0x18}},
    /* objects' method 1 (the transform) */
    {0x44e770, 4, {0x8d, 0x41, 0x70, 0xc3}},
    {0x5680d0, 4, {0x8d, 0x41, 0x40, 0xc3}},
    {0x544c90, 7, {0x8b, 0x41, 0x28, 0x83, 0xc0, 0x10, 0xc3}},
};

/* method table entries relied on: address, expected value */
static const unsigned SLOTS[][2] = {
    {0x634eb8, 0x4327c0}, /* HUDMode::update, the one replaced */
    {0x64ba94 + 0x1c, 0x573740}, {0x64ba94, 0x570b00}, /* DynamicObjectScript */
    {0x64c070 + 0x1c, 0x578140}, {0x64c070, 0x573a20}, /* TriggerScript */
    {0x64c180 + 0x1c, 0x573740},                       /* WaypointScript */
    {0x639e4c + 4, 0x44e770},                          /* DynamicObject */
    {0x6466a4 + 4, 0x5680d0},                          /* Trigger */
    {0x6470bc + 4, 0x544c90},                          /* Waypoint */
};
#define UPDATE_SLOT ((void **)0x634eb8) /* HUDMode method table, slot 6 */
#define SAVE_CALL 0x48355e              /* e8 <rel32> */
#define GIDMAP_INSTANCE_IMPORT ((void **)0x62f784)
#define GIDMAP_POINTER_IMPORT ((void **)0x62f7e8)

static int expected_build(void)
{
    unsigned char code[16];
    size_t i;

    if ((ULONG_PTR)GetModuleHandleA(NULL) != 0x400000)
        return 0;
    for (i = 0; i < sizeof EXPECTED / sizeof *EXPECTED; i++)
        if (!peek((void *)(ULONG_PTR)EXPECTED[i].address, code, EXPECTED[i].size)
            || memcmp(code, EXPECTED[i].code, EXPECTED[i].size) != 0)
            return 0;
    for (i = 0; i < sizeof SLOTS / sizeof *SLOTS; i++) {
        unsigned value = 0;
        if (!peek((void *)(ULONG_PTR)SLOTS[i][0], &value, 4) || value != SLOTS[i][1])
            return 0;
    }
    return 1;
}

static int write_code(void *address, const void *data, size_t size)
{
    DWORD protection;

    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &protection))
        return 0;
    memcpy(address, data, size);
    VirtualProtect(address, size, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    return memcmp(address, data, size) == 0;
}

int radar_install(const struct radar_host *h)
{
    /* the camera global for each state of 0x43eb60 (state 0 read as when called with 0) */
    static const unsigned CAMERAS[8] = {0x6c0898, 0x6c08a0, 0x6c08a0, 0x6c08a4,
                                        0x6c089c, 0x6c089c, 0x6c08a0, 0x6c08ac};
    static int tried;
    HMODULE rl;
    void *instance, *pointer, *imported[2] = {NULL, NULL};
    LONG rel;
    int i;

    if (tried++)
        return installed;
    host = *h;
    if (!expected_build()) {
        host.log("radar markers unavailable: this is not the supported build of AlanWake.exe");
        return 0;
    }
    rl = GetModuleHandleA("rl_sf_Win32.dll");
    instance = rl ? (void *)GetProcAddress(rl, "?getInstance@GlobalIDMap@r@@SIPAV12@XZ") : NULL;
    pointer = rl ? (void *)GetProcAddress(rl, "?getPointer@GlobalIDMap@r@@QBEPAXABVGlobalID@2@@Z")
                 : NULL;
    peek(GIDMAP_INSTANCE_IMPORT, &imported[0], 4);
    peek(GIDMAP_POINTER_IMPORT, &imported[1], 4);
    if (!instance || !pointer || instance != imported[0] || pointer != imported[1]) {
        host.log("radar markers unavailable: object lookup functions not found");
        return 0;
    }
    game.hud = (void **)0x6bf9c8;
    game.radar = (void **)0x6e8ce8;
    game.camera_state = (const int *)0x6c0894;
    for (i = 0; i < 8; i++)
        game.cameras[i] = (void **)(ULONG_PTR)CAMERAS[i];
    game.difficulty = (void **)0x76b560;
    game.script_mesh = (void *)SCRIPT_MESH;
    game.script_trigger = (void *)SCRIPT_TRIGGER;
    game.script_waypoint = (void *)SCRIPT_WAYPOINT;
    game.object_mesh = (void *)OBJECT_MESH;
    game.object_trigger = (void *)OBJECT_TRIGGER;
    game.object_waypoint = (void *)OBJECT_WAYPOINT;
    game.update = (update_fn)0x4327c0;
    game.add = (add_fn)0x432990;
    game.remove = (remove_fn)0x584940;
    game.save = (save_fn)0x5841e0;
    game.map_instance = (map_instance_fn)instance;
    game.map_pointer = (map_pointer_fn)pointer;

    /* the save guard first: without it, nothing else is installed */
    rel = (LONG)((ULONG_PTR)(void *)my_save - (SAVE_CALL + 5));
    if (!write_code((void *)(SAVE_CALL + 1), &rel, 4)) {
        host.log("radar markers unavailable: the save guard could not be installed");
        return 0;
    }
    {
        void *hook = (void *)my_update;
        if (!write_code(UPDATE_SLOT, &hook, 4)) {
            static const LONG original = 0x5841e0 - (SAVE_CALL + 5);
            write_code((void *)(SAVE_CALL + 1), &original, 4); /* put the save back */
            host.log("radar markers unavailable: the HUD hook could not be installed");
            return 0;
        }
    }
    installed = 1;
    host.log("radar hooks installed (%d objects known)", TARGET_COUNT);
    return 1;
}
