/*
 * TEST BUILD ONLY. A marker on the game's radar for the nearest missing collectible, through
 * the game's own AddMapEntity code. Off unless [test] radar_test=1 or 2 in the ini.
 *
 *   stage 1  hooks installed, nothing added: logs threads, objects, positions, distances, the
 *            item that would be marked, and every save
 *   stage 2  as stage 1, and while the radar_markers option is on (F4) marks the nearest loaded
 *            missing item, once a save has shown the guard works
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

/* a radar entry, 0xe0 bytes */
#define ENTRY_SIZE 0xe0
#define ENTRY_GID 0x04
#define ENTRY_POSITION 0x30 /* the object's world transform is copied to +0xc each frame */
#define ENTRY_FADE 0xd0
#define ENTRY_ACTIVE 0xd4
#define RADAR_ENTRIES 0x00
#define RADAR_COUNT 0x04
#define MAX_ENTRIES 64 /* more would mean the layout is not what we think */

/* the radar is centred on the active camera: camera + 0x4dc is its transform, + 0x24 in it the
 * position (0x43b300 returns camera + 0x500) */
#define CAMERA_STATE ((const int *)0x6c0894)
#define CAMERA_POSITION 0x500
/* an object's transform: position at + 0x24 */
#define TRANSFORM_POSITION 0x24

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
    update_fn update;
    add_fn add;
    remove_fn remove;
    save_fn save;
    map_instance_fn map_instance;
    map_pointer_fn map_pointer;
} game;

/* The missing level_100 items to try. An item can have two objects: the Player_Waypoint
 * (class 113, the kind the game's own objective markers use) is preferred to the mesh
 * (class 100). */
static const struct candidate {
    struct gid gid;
    int category, number;
    const char *name;
} CANDIDATES[] = {
    {{113, 0x7F351912}, CAT_CUTOUTS, 5, "cutout #5 (tor) waypoint"},
    {{100, 0x105E969B}, CAT_CUTOUTS, 5, "cutout #5 (tor) mesh"},
    {{100, 0xECFA79C2}, CAT_ALARMCLOCKS, 4, "alarm clock #4"},
    {{100, 0x7C50B87D}, CAT_ALARMCLOCKS, 5, "alarm clock #5"},
    {{100, 0x4D43D75A}, CAT_ALARMCLOCKS, 6, "alarm clock #6"},
    {{100, 0x92D6EDF3}, CAT_ALARMCLOCKS, 7, "alarm clock #7"},
    {{100, 0xD31605F5}, CAT_ALARMCLOCKS, 8, "alarm clock #8"},
    {{100, 0x5A9D3DDC}, CAT_ALARMCLOCKS, 9, "alarm clock #9"},
    {{100, 0x1AC35D76}, CAT_ALARMCLOCKS, 10, "alarm clock #10"},
    {{113, 0xF6251960}, CAT_CUTOUTS, 4, "cutout #4 (weaver) waypoint"},
    {{100, 0xAC8FC8F4}, CAT_CUTOUTS, 4, "cutout #4 (weaver) mesh"},
    {{113, 0x1A199A50}, CAT_CUTOUTS, 6, "cutout #6 (odin) waypoint"},
    {{100, 0x23351242}, CAT_CUTOUTS, 6, "cutout #6 (odin) mesh"},
    {{113, 0x4E8F5547}, CAT_CUTOUTS, 1, "cutout #1 (alice) waypoint"},
    {{100, 0x8C07C44F}, CAT_CUTOUTS, 1, "cutout #1 (alice) mesh"},
};
#define CANDIDATE_COUNT ((int)(sizeof CANDIDATES / sizeof *CANDIDATES))

static struct radar_host host;
static int stage;
static DWORD hud_thread;   /* where HUDMode::update runs */
static int guard_confirmed; /* a save went through the guard on hud_thread */
static int stopped;         /* something was not as expected: add nothing more */
static int marker = -1;     /* candidate we added, or -1 */
static signed char loaded[CANDIDATE_COUNT]; /* last lookup: -1 not tried, 0 no, 1 yes */
static void *last_hud, *last_radar;
static int last_count = -1, last_nearest = -2, last_camera = -1;
static unsigned ticks;

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

static int same_item(int a, int b)
{
    return CANDIDATES[a].category == CANDIDATES[b].category
           && CANDIDATES[a].number == CANDIDATES[b].number;
}

static void stop(const char *why)
{
    if (!stopped)
        host.log("radar: stopped, no more markers: %s", why);
    stopped = 1;
}

/* The radar's entries: returns the count, or -1 if they cannot be read. With `ours`, also
 * counts the entries holding one of our candidates; with `gid`, sets *has to whether it is
 * there and *entry to its address. */
static int entries(const char *radar, int *ours, const struct gid *gid, int *has, char **entry)
{
    char *base = NULL;
    int count = 0, i, j;

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
        for (j = 0; ours && j < CANDIDATE_COUNT; j++)
            if (same(&g, &CANDIDATES[j].gid)) {
                ++*ours;
                break;
            }
    }
    return count;
}

static int has_entry(const char *radar, const struct gid *gid)
{
    int has = 0;

    entries(radar, NULL, gid, &has, NULL);
    return has;
}

/* Is the object loaded? The game's AddMapEntity crashes on an object that is not. */
static void *lookup(const struct gid *gid)
{
    void *map = game.map_instance();

    return map ? game.map_pointer(map, gid) : NULL;
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

/* Where a loaded object is: what the radar itself does with it each frame (0x432990 takes
 * slot 7 of the registered object, 0x585bb0 slot 1 of the result for its transform). */
static int object_position(const struct gid *gid, struct vec3 *out)
{
    void *registered = lookup(gid), *object, *transform;

    if (!registered || !(object = call_virtual(registered, 7))
        || !(transform = call_virtual(object, 1)))
        return 0;
    return peek((char *)transform + TRANSFORM_POSITION, out, sizeof *out) && plausible(out);
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

/* the radar is flat: distance on the ground */
static float distance(const struct vec3 *a, const struct vec3 *b)
{
    float x = a->x - b->x, z = a->z - b->z;
    return sqrtf(x * x + z * z);
}

static void log_marker(const char *radar, const struct vec3 *camera)
{
    struct vec3 at = {0, 0, 0};
    unsigned char active = 0;
    float fade = 0;
    char *entry = NULL;
    int has = 0, count = entries(radar, NULL, &CANDIDATES[marker].gid, &has, &entry);

    if (has) {
        peek(entry + ENTRY_POSITION, &at, sizeof at);
        peek(entry + ENTRY_ACTIVE, &active, 1);
        peek(entry + ENTRY_FADE, &fade, 4);
    }
    host.log("radar: %d entries; marker on %s: %s, active %d, fade %.2f, at %.1f %.1f %.1f, "
             "%.1f m away",
             count, CANDIDATES[marker].name, has ? "present" : "absent", active, fade, at.x, at.y,
             at.z, camera ? distance(camera, &at) : -1.0f);
}

static void remove_marker(char *radar, const char *why)
{
    if (has_entry(radar, &CANDIDATES[marker].gid))
        game.remove(radar, &CANDIDATES[marker].gid); /* it fades, then the game erases it */
    host.log("radar: marker on %s removed: %s", CANDIDATES[marker].name, why);
    marker = -1;
}

static int add_marker(void *hud, char *radar, int i)
{
    if (!lookup(&CANDIDATES[i].gid)) /* checked again right before the call */
        return 0;
    game.add(hud, &CANDIDATES[i].gid, SPRITE, NULL, 0, 0);
    if (!has_entry(radar, &CANDIDATES[i].gid)) {
        stop("the marker did not appear in the radar");
        return 0;
    }
    marker = i;
    return 1;
}

/* Once a second, on the game's main thread, inside HUDMode::update. */
static void tick(void)
{
    struct profile profile;
    struct vec3 camera, where[CANDIDATE_COUNT];
    float away[CANDIDATE_COUNT];
    void *hud = *game.hud, *radar = *game.radar;
    int have_profile, markers = 0, count, have_camera, nearest = -1, i, j;

    if (!ticks++)
        memset(loaded, -1, sizeof loaded);
    if (hud != last_hud || radar != last_radar) {
        host.log("radar: HUD %p, radar %p", hud, radar);
        last_hud = hud;
        last_radar = radar;
    }
    if (!hud || !radar)
        return;
    count = entries(radar, NULL, NULL, NULL, NULL);
    if (count != last_count) {
        host.log("radar: %d entries", count);
        last_count = count;
    }
    if (count < 0) {
        stop("radar entries not as expected");
        return;
    }
    have_profile = host.state(&profile, &markers);

    /* the marker we put there */
    if (marker >= 0) {
        const struct candidate *c = &CANDIDATES[marker];

        if (stopped)
            remove_marker(radar, "stopped");
        else if (!has_entry(radar, &c->gid)) {
            host.log("radar: marker on %s gone (object unloaded, or level reloaded)", c->name);
            marker = -1;
        } else if (have_profile && profile.found[c->category][c->number])
            remove_marker(radar, "found");
        else if (!markers || stage < 2)
            remove_marker(radar, "markers switched off");
    }
    if (stopped)
        return; /* something was not as expected: no more calls into game objects either */

    have_camera = camera_position(&camera);
    if (have_camera != last_camera) {
        if (have_camera)
            host.log("radar: camera at %.1f %.1f %.1f", camera.x, camera.y, camera.z);
        else
            host.log("radar: no camera: nearest item not chosen");
        last_camera = have_camera;
    }

    /* where each missing item is, if loaded; an item's first loaded object stands for it */
    for (i = 0; i < CANDIDATE_COUNT; i++) {
        const struct candidate *c = &CANDIDATES[i];
        int is_loaded;

        away[i] = -1;
        if (!have_profile || profile.found[c->category][c->number])
            continue;
        is_loaded = object_position(&c->gid, &where[i]);
        if (is_loaded != loaded[i]) {
            if (is_loaded)
                host.log("radar: %s (%u:%08X): loaded, at %.1f %.1f %.1f", c->name, c->gid.cls,
                         c->gid.id, where[i].x, where[i].y, where[i].z);
            else
                host.log("radar: %s (%u:%08X): not loaded", c->name, c->gid.cls, c->gid.id);
        }
        loaded[i] = (signed char)is_loaded;
        for (j = 0; j < i && is_loaded; j++)
            if (same_item(i, j) && away[j] >= 0)
                is_loaded = 0; /* the item already has a loaded object */
        if (!is_loaded || !have_camera)
            continue;
        away[i] = distance(&camera, &where[i]);
        if (nearest < 0 || away[i] < away[nearest])
            nearest = i;
    }
    if (!have_profile) {
        static int told;
        if (!told++)
            host.log("radar: no profile read yet: nothing chosen");
        return;
    }
    if (nearest != last_nearest && have_camera) {
        if (nearest >= 0)
            host.log("radar: nearest missing item: %s, %.1f m", CANDIDATES[nearest].name,
                     away[nearest]);
        else
            host.log("radar: no missing item loaded");
        last_nearest = nearest;
    }
    if (ticks % 10 == 0 && have_camera) {
        char line[512];
        int n = snprintf(line, sizeof line, "radar: camera %.1f %.1f %.1f;", camera.x, camera.y,
                         camera.z);

        for (i = 0; i < CANDIDATE_COUNT && n > 0 && n < (int)sizeof line; i++)
            if (away[i] >= 0)
                n += snprintf(line + n, sizeof line - n, " %s %.1f m;", CANDIDATES[i].name,
                              away[i]);
        host.log("%s", line);
        if (marker >= 0)
            log_marker(radar, &camera);
    }

    if (stage < 2 || !markers || stopped || nearest < 0)
        return;
    if (!guard_confirmed) {
        static int told;
        if (!told++)
            host.log("radar: waiting for a save through the guard before adding a marker"
                     " (reach a checkpoint)");
        return;
    }
    if (has_entry(radar, &CANDIDATES[nearest].gid) && marker != nearest)
        return; /* still fading out after an earlier removal: wait */
    if (marker < 0) {
        if (add_marker(hud, radar, nearest)) {
            host.log("radar: marker added on %s, %.1f m away", CANDIDATES[nearest].name,
                     away[nearest]);
            log_marker(radar, &camera);
        }
        return;
    }
    if (!same_item(nearest, marker)) {
        struct vec3 at;
        char *entry = NULL;
        int has = 0;
        float now;

        entries(radar, NULL, &CANDIDATES[marker].gid, &has, &entry);
        if (!has || !peek(entry + ENTRY_POSITION, &at, sizeof at) || !plausible(&at))
            return;
        now = distance(&camera, &at);
        if (away[nearest] < now * SWITCH_RATIO && now - away[nearest] >= SWITCH_MIN) {
            int old = marker;

            remove_marker(radar, "a nearer item is loaded");
            if (add_marker(hud, radar, nearest))
                host.log("radar: marker moved from %s (%.1f m) to %s (%.1f m)",
                         CANDIDATES[old].name, now, CANDIDATES[nearest].name, away[nearest]);
        }
    }
}

__attribute__((thiscall, force_align_arg_pointer)) static void my_update(void *hud)
{
    static DWORD last;
    DWORD now = GetTickCount();

    if (!hud_thread) {
        hud_thread = GetCurrentThreadId();
        host.log("radar: HUD update runs on thread %lu", (unsigned long)hud_thread);
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
    static unsigned calls;
    DWORD thread = GetCurrentThreadId();
    int ours = 0, count = radar ? entries(radar, &ours, NULL, NULL, NULL) : -1;

    calls++;
    host.log("radar: save %u on thread %lu (HUD thread %lu): %d entries, %d ours", calls,
             (unsigned long)thread, (unsigned long)hud_thread, count, ours);
    if (count == 0) {
        game.save(radar, stream); /* nothing to hide */
        if (!hud_thread)
            ; /* before the first HUD update (loading): proves nothing either way */
        else if (thread == hud_thread && !guard_confirmed) {
            guard_confirmed = 1;
            host.log("radar: save guard confirmed: saves run on the HUD thread");
        } else if (thread != hud_thread)
            stop("a save ran on another thread than the HUD");
        return;
    }
    if (count < 0 || thread != hud_thread || ours != count) {
        host.log("RADAR SAVE GUARD SKIPPED: %s; the save may contain %d marker(s)",
                 count < 0 ? "radar not readable" : thread != hud_thread ? "other thread"
                                                                         : "entries not ours",
                 ours);
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

void radar_frame(void)
{
    static int done;

    if (stage && hud_thread && !done++)
        host.log("radar: frames presented on thread %lu (HUD thread %lu)",
                 (unsigned long)GetCurrentThreadId(), (unsigned long)hud_thread);
}

const char *radar_note(void)
{
    if (!stage)
        return "\nRadar test not active";
    if (stopped)
        return "\nRadar markers stopped: see the log";
    if (stage < 2)
        return "\nRadar test stage 1: nothing is marked";
    if (!guard_confirmed)
        return "\nShown after the next checkpoint";
    return "";
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
};
#define UPDATE_SLOT ((void **)0x634eb8) /* HUDMode method table, slot 6 */
#define SAVE_CALL 0x48355e              /* e8 <rel32> */
#define GIDMAP_INSTANCE_IMPORT ((void **)0x62f784)
#define GIDMAP_POINTER_IMPORT ((void **)0x62f7e8)

static int expected_build(void)
{
    unsigned char code[16];
    void *slot = NULL;
    size_t i;

    if ((ULONG_PTR)GetModuleHandleA(NULL) != 0x400000)
        return 0;
    for (i = 0; i < sizeof EXPECTED / sizeof *EXPECTED; i++)
        if (!peek((void *)(ULONG_PTR)EXPECTED[i].address, code, EXPECTED[i].size)
            || memcmp(code, EXPECTED[i].code, EXPECTED[i].size) != 0)
            return 0;
    return peek(UPDATE_SLOT, &slot, 4) && slot == (void *)0x4327c0;
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

int radar_install(const struct radar_host *h, int test_stage)
{
    /* the camera global for each state of 0x43eb60 (state 0 read as when called with 0) */
    static const unsigned CAMERAS[8] = {0x6c0898, 0x6c08a0, 0x6c08a0, 0x6c08a4,
                                        0x6c089c, 0x6c089c, 0x6c08a0, 0x6c08ac};
    static int installed = -1;
    HMODULE rl;
    void *instance, *pointer, *imported[2] = {NULL, NULL};
    LONG rel;
    int i;

    if (installed >= 0)
        return installed;
    installed = 0;
    host = *h;
    if (test_stage < 1)
        return 0;
    host.log("radar test stage %d: %s", test_stage,
             test_stage >= 2 ? "marks the nearest missing item while radar_markers is on,"
                               " after a guarded save"
                             : "log only, no marker");
    if (!expected_build()) {
        host.log("radar test unavailable: this is not the supported build of AlanWake.exe");
        return 0;
    }
    rl = GetModuleHandleA("rl_sf_Win32.dll");
    instance = rl ? (void *)GetProcAddress(rl, "?getInstance@GlobalIDMap@r@@SIPAV12@XZ") : NULL;
    pointer = rl ? (void *)GetProcAddress(rl, "?getPointer@GlobalIDMap@r@@QBEPAXABVGlobalID@2@@Z")
                 : NULL;
    peek(GIDMAP_INSTANCE_IMPORT, &imported[0], 4);
    peek(GIDMAP_POINTER_IMPORT, &imported[1], 4);
    if (!instance || !pointer || instance != imported[0] || pointer != imported[1]) {
        host.log("radar test unavailable: object lookup functions not found");
        return 0;
    }
    game.hud = (void **)0x6bf9c8;
    game.radar = (void **)0x6e8ce8;
    game.camera_state = CAMERA_STATE;
    for (i = 0; i < 8; i++)
        game.cameras[i] = (void **)(ULONG_PTR)CAMERAS[i];
    game.update = (update_fn)0x4327c0;
    game.add = (add_fn)0x432990;
    game.remove = (remove_fn)0x584940;
    game.save = (save_fn)0x5841e0;
    game.map_instance = (map_instance_fn)instance;
    game.map_pointer = (map_pointer_fn)pointer;
    stage = test_stage;

    /* the save guard first: without it, nothing else is installed */
    rel = (LONG)((ULONG_PTR)(void *)my_save - (SAVE_CALL + 5));
    if (!write_code((void *)(SAVE_CALL + 1), &rel, 4)) {
        host.log("radar test unavailable: the save guard could not be installed");
        stage = 0;
        return 0;
    }
    {
        void *hook = (void *)my_update;
        if (!write_code(UPDATE_SLOT, &hook, 4)) {
            static const LONG original = 0x5841e0 - (SAVE_CALL + 5);
            write_code((void *)(SAVE_CALL + 1), &original, 4); /* put the save back */
            host.log("radar test unavailable: the HUD hook could not be installed");
            stage = 0;
            return 0;
        }
    }
    installed = 1;
    host.log("radar test: save guard and HUD hook installed");
    return 1;
}
