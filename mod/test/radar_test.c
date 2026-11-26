/* Windows test of the radar test hooks against stand-ins for the game's objects and functions,
 * using the game's calling conventions. Includes radar.c directly. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../src/radar.c"

#define EXPECT(x)                                        \
    do {                                                 \
        if (!(x)) {                                      \
            printf("FAIL line %d: %s\n", __LINE__, #x);  \
            return 1;                                    \
        }                                                \
    } while (0)

static char logged[65536];
static struct profile current;
static int have_profile = 1, markers_on = 1;

static void record(const char *format, ...)
{
    char line[1024];
    va_list args;

    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (strlen(logged) + strlen(line) + 2 < sizeof logged) {
        strcat(logged, line);
        strcat(logged, "\n");
    }
}

static int state(struct profile *p, int *markers)
{
    *p = current;
    *markers = markers_on;
    return have_profile;
}

/* stand-ins: a radar with its entry array, the HUD, the object map, objects, a camera */
static struct {
    char *base;
    int count, capacity;
} radar_object;
static char entry_memory[8 * ENTRY_SIZE];
static int hud_object, map_object;
static void *hud_global, *radar_global;
static int updates, adds, removes, saves, save_saw_count, add_sprite, add_fade[2];
static const char *add_text = "unset";

struct object {
    void **table;
    float transform[12]; /* rotation, then position */
};
struct registered {
    void **table;
    struct object *object;
    struct gid gid;
    int loaded;
};

static int object_calls;

__attribute__((thiscall)) static void *registered_object(void *self)
{
    object_calls++;
    return ((struct registered *)self)->object;
}

__attribute__((thiscall)) static void *object_transform(void *self)
{
    return ((struct object *)self)->transform;
}

static void *registered_table[8] = {0, 0, 0, 0, 0, 0, 0, (void *)registered_object};
static void *object_table[2] = {0, (void *)object_transform};
static struct object objects[CANDIDATE_COUNT];
static struct registered registry[CANDIDATE_COUNT];

static int camera_state;
static char camera_object[0x600];
static void *camera_global = camera_object;

static void place_camera(float x, float z)
{
    float at[3] = {x, 1.5f, z};
    memcpy(camera_object + CAMERA_POSITION, at, sizeof at);
}

/* make candidate `i` loaded at (x, z) */
static void load(int i, float x, float z)
{
    objects[i].table = object_table;
    objects[i].transform[9] = x;
    objects[i].transform[10] = 0;
    objects[i].transform[11] = z;
    registry[i].table = registered_table;
    registry[i].object = &objects[i];
    registry[i].gid = CANDIDATES[i].gid;
    registry[i].loaded = 1;
}

static int find_entry(const struct gid *gid)
{
    int i;

    for (i = 0; i < radar_object.count; i++) {
        struct gid g;
        memcpy(&g, radar_object.base + i * ENTRY_SIZE + ENTRY_GID, sizeof g);
        if (same(&g, gid))
            return i;
    }
    return -1;
}

static void put_entry(unsigned cls, unsigned id, const float *at)
{
    char *e = radar_object.base + radar_object.count++ * ENTRY_SIZE;
    struct gid g = {cls, id};

    memset(e, 0, ENTRY_SIZE);
    memcpy(e + ENTRY_GID, &g, sizeof g);
    if (at)
        memcpy(e + ENTRY_POSITION, at, 3 * sizeof *at);
    e[ENTRY_ACTIVE] = 1;
}

/* what the game does once an entry has faded out */
static void erase_entry(int i)
{
    if (i < 0)
        return;
    memmove(radar_object.base + i * ENTRY_SIZE, radar_object.base + (i + 1) * ENTRY_SIZE,
            (radar_object.count - i - 1) * ENTRY_SIZE);
    radar_object.count--;
}

static int active_entries(void)
{
    int i, n = 0;

    for (i = 0; i < radar_object.count; i++)
        n += radar_object.base[i * ENTRY_SIZE + ENTRY_ACTIVE] != 0;
    return n;
}

__attribute__((thiscall)) static void fake_update(void *hud)
{
    if (hud == &hud_object)
        updates++;
}

__attribute__((thiscall)) static void fake_add(void *hud, const struct gid *gid, int sprite,
                                               const char *text, int fade_sprite, int fade_text)
{
    int i;

    if (hud != &hud_object)
        return;
    adds++;
    add_sprite = sprite;
    add_text = text;
    add_fade[0] = fade_sprite;
    add_fade[1] = fade_text;
    for (i = 0; i < CANDIDATE_COUNT; i++)
        if (registry[i].loaded && same(&registry[i].gid, gid))
            put_entry(gid->cls, gid->id, objects[i].transform + 9);
}

__attribute__((thiscall)) static void fake_remove(void *radar, const struct gid *gid)
{
    int i = find_entry(gid);

    removes++;
    if (radar == &radar_object && i >= 0)
        radar_object.base[i * ENTRY_SIZE + ENTRY_ACTIVE] = 0; /* fades, stays */
}

__attribute__((thiscall)) static void fake_save(void *radar, void *stream)
{
    if (radar == &radar_object && stream == (void *)0x1234) {
        saves++;
        save_saw_count = radar_object.count;
    }
}

__attribute__((fastcall)) static void *fake_instance(void)
{
    return &map_object;
}

__attribute__((thiscall)) static void *fake_pointer(void *map, const struct gid *gid)
{
    int i;

    for (i = 0; map == &map_object && i < CANDIDATE_COUNT; i++)
        if (registry[i].loaded && same(&registry[i].gid, gid))
            return &registry[i];
    return NULL;
}

static DWORD WINAPI save_elsewhere(void *unused)
{
    (void)unused;
    my_save(&radar_object, (void *)0x1234);
    return 0;
}

static int logged_has(const char *text)
{
    return strstr(logged, text) != NULL;
}

enum { TOR_WAYPOINT = 0, TOR_MESH = 1, CLOCK_4 = 2, CLOCK_5 = 3 };

int main(void)
{
    static const struct radar_host h = {record, state};
    HANDLE thread;
    int i;

    /* off: nothing at all */
    EXPECT(radar_install(&h, 0) == 0 && stage == 0);

    radar_object.base = entry_memory;
    hud_global = &hud_object;
    radar_global = &radar_object;
    game.hud = &hud_global;
    game.radar = &radar_global;
    game.camera_state = &camera_state;
    for (i = 0; i < 8; i++)
        game.cameras[i] = &camera_global;
    game.update = fake_update;
    game.add = fake_add;
    game.remove = fake_remove;
    game.save = fake_save;
    game.map_instance = fake_instance;
    game.map_pointer = fake_pointer;
    host = h;
    stage = 2;
    for (i = 1; i <= 3; i++) /* alarm clocks #1-#3 found */
        current.found[CAT_ALARMCLOCKS][i] = 1;
    place_camera(0, 0);
    load(TOR_WAYPOINT, 100, 0);
    load(TOR_MESH, 100, 0);
    load(CLOCK_4, 10, 0);

    /* a save while loading, before the HUD ever updated: proves nothing */
    my_save(&radar_object, (void *)0x1234);
    EXPECT(saves == 1 && save_saw_count == 0 && !guard_confirmed && !stopped);

    /* the first update: thread recorded, the game's update runs; the nearest item is found
     * but nothing is added before a save went through the guard */
    my_update(&hud_object);
    EXPECT(updates == 1 && hud_thread == GetCurrentThreadId());
    EXPECT(adds == 0 && marker == -1);
    EXPECT(logged_has("alarm clock #4 (100:ECFA79C2): loaded, at 10.0 0.0 0.0"));
    EXPECT(logged_has("nearest missing item: alarm clock #4, 10.0 m"));
    EXPECT(logged_has("waiting for a save through the guard"));

    my_save(&radar_object, (void *)0x1234);
    EXPECT(saves == 2 && guard_confirmed && !stopped);

    /* no profile: nothing added */
    have_profile = 0;
    tick();
    EXPECT(adds == 0);
    have_profile = 1;

    /* the nearest, not the first in the list, with the agreed arguments */
    tick();
    EXPECT(adds == 1 && marker == CLOCK_4 && radar_object.count == 1);
    EXPECT(add_sprite == 0 && add_text == NULL && add_fade[0] == 0 && add_fade[1] == 0);
    EXPECT(logged_has("marker added on alarm clock #4, 10.0 m away"));
    tick();
    EXPECT(adds == 1);

    /* half way: not clearly nearer, the marker stays */
    place_camera(55, 0); /* clock 45 m, tor 45 m */
    tick();
    EXPECT(adds == 1 && marker == CLOCK_4);
    place_camera(59, 0); /* clock 49 m, tor 41 m: not a fifth nearer */
    tick();
    EXPECT(adds == 1 && marker == CLOCK_4);

    /* clearly nearer: moved; the old one fades, one marker active */
    place_camera(80, 0); /* clock 70 m, tor 20 m */
    tick();
    EXPECT(removes == 1 && adds == 2 && marker == TOR_WAYPOINT);
    EXPECT(radar_object.count == 2 && active_entries() == 1);
    EXPECT(logged_has("marker moved from alarm clock #4 (70.0 m) to cutout #5 (tor) waypoint (20.0 m)"));

    /* a save while one fades: both are hidden */
    my_save(&radar_object, (void *)0x1234);
    EXPECT(save_saw_count == 0 && radar_object.count == 2);
    EXPECT(logged_has("saved without the 2 marker(s)"));

    /* back near the clock: it is still fading, so the move waits */
    place_camera(12, 0); /* clock 2 m, tor 88 m */
    tick();
    EXPECT(adds == 2 && marker == TOR_WAYPOINT);
    erase_entry(find_entry(&CANDIDATES[CLOCK_4].gid));
    tick();
    EXPECT(adds == 3 && marker == CLOCK_4 && active_entries() == 1);
    erase_entry(find_entry(&CANDIDATES[TOR_WAYPOINT].gid));

    /* switched off: removed, nothing added; on again: back */
    markers_on = 0;
    tick();
    EXPECT(removes == 3 && marker == -1 && adds == 3);
    EXPECT(logged_has("marker on alarm clock #4 removed: markers switched off"));
    tick();
    EXPECT(adds == 3);
    erase_entry(find_entry(&CANDIDATES[CLOCK_4].gid));
    markers_on = 1;
    tick();
    EXPECT(adds == 4 && marker == CLOCK_4);

    /* found: removed, and the next nearest item marked at once; one active marker */
    current.found[CAT_ALARMCLOCKS][4] = 1;
    load(CLOCK_5, 30, 0);
    tick();
    EXPECT(removes == 4 && marker == CLOCK_5 && adds == 5 && active_entries() == 1);
    EXPECT(logged_has("marker on alarm clock #4 removed: found"));
    erase_entry(find_entry(&CANDIDATES[CLOCK_4].gid));

    /* level reload: the game empties the radar; the marker comes back */
    radar_object.count = 0;
    tick();
    EXPECT(logged_has("marker on alarm clock #5 gone"));
    EXPECT(marker == CLOCK_5 && adds == 6 && radar_object.count == 1);

    /* an object that is registered but has no game object counts as not loaded */
    registry[CLOCK_5].object = NULL;
    tick();
    EXPECT(logged_has("alarm clock #5 (100:7C50B87D): not loaded"));
    registry[CLOCK_5].object = &objects[CLOCK_5];

    /* no camera: nothing is chosen or moved */
    camera_state = 9;
    place_camera(100, 0);
    tick();
    EXPECT(logged_has("no camera: nearest item not chosen") && marker == CLOCK_5 && adds == 6);
    camera_state = 0;

    /* a save on another thread: the radar is left alone, the marker removed, none added */
    thread = CreateThread(NULL, 0, save_elsewhere, NULL, 0, NULL);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    EXPECT(save_saw_count == 1 && radar_object.count == 1 && stopped);
    EXPECT(logged_has("RADAR SAVE GUARD SKIPPED: other thread"));
    tick();
    EXPECT(marker == -1 && logged_has("marker on alarm clock #5 removed: stopped"));
    erase_entry(0);
    i = object_calls;
    tick();
    EXPECT(adds == 6 && object_calls == i); /* no calls into objects once stopped */

    /* an entry that is not ours: left in, logged */
    stopped = 0;
    put_entry(1, 2, NULL);
    my_save(&radar_object, (void *)0x1234);
    EXPECT(save_saw_count == 1 && radar_object.count == 1 && stopped);
    EXPECT(logged_has("RADAR SAVE GUARD SKIPPED: entries not ours"));
    radar_object.count = 0;

    /* an unreadable radar: left alone */
    stopped = 0;
    radar_object.count = MAX_ENTRIES + 1;
    tick();
    EXPECT(stopped);
    radar_object.count = 0;

    /* stage 1 never adds, but still says what it would mark */
    stopped = 0;
    stage = 1;
    last_nearest = -2;
    place_camera(30, 0);
    tick();
    EXPECT(adds == 6 && logged_has("nearest missing item: alarm clock #5"));
    EXPECT(strcmp(radar_note(), "\nRadar test stage 1: nothing is marked") == 0);

    printf("radar test passed\n");
    return 0;
}
