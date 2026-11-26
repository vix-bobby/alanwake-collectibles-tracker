/* Windows test of the radar hooks against stand-ins for the game's objects and functions,
 * using the game's calling conventions and the memory layouts radar.c reads. Includes radar.c
 * directly, with the generated target table. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../src/profile.c"
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

static int logged_has(const char *text)
{
    return strstr(logged, text) != NULL;
}

static int state(struct profile *p, int *markers)
{
    *p = current;
    *markers = markers_on;
    return have_profile;
}

/* ---------------------------------------------------------------- the radar */

static struct {
    char *base;
    int count, capacity;
} radar_object, other_radar;
static char entry_memory[16 * ENTRY_SIZE];
static int hud_object, map_object;
static void *hud_global, *radar_global;
static int updates, adds, removes, saves, save_saw_count, lookups, add_sprite, add_fade[2];
static const char *add_text = "unset";

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

static void put_entry(const struct gid *g, const float *at)
{
    char *e = radar_object.base + radar_object.count++ * ENTRY_SIZE;

    memset(e, 0, ENTRY_SIZE);
    memcpy(e + ENTRY_GID, g, sizeof *g);
    if (at)
        memcpy(e + ENTRY_POSITION, at, 3 * sizeof *at);
    e[0xd4] = 1; /* active */
}

/* what the game does once an entry has faded out */
static void erase_entry(int i)
{
    if (i < 0 || i >= radar_object.count)
        return;
    memmove(radar_object.base + i * ENTRY_SIZE, radar_object.base + (i + 1) * ENTRY_SIZE,
            (radar_object.count - i - 1) * ENTRY_SIZE);
    radar_object.count--;
}

static int active_entries(void)
{
    int i, n = 0;

    for (i = 0; i < radar_object.count; i++)
        n += radar_object.base[i * ENTRY_SIZE + 0xd4] != 0;
    return n;
}

/* ---------------------------------------------------------------- scripts and objects */

/* tables: only their addresses matter, except for the unknown kinds, whose methods are called */
static void *script_mesh_table[8], *script_trigger_table[8], *script_waypoint_table[8];
static void *object_mesh_table[2], *object_trigger_table[2], *object_waypoint_table[2];

__attribute__((thiscall)) static void *unknown_script_object(void *self)
{
    return *(void **)((char *)self + 0x20);
}

__attribute__((thiscall)) static void *unknown_object_transform(void *self)
{
    return (char *)self + 0x80;
}

static void *unknown_script_table[8] = {0, 0, 0, 0, 0, 0, 0, (void *)unknown_script_object};
static void *unknown_object_table[2] = {0, (void *)unknown_object_transform};

enum kind { MESH, TRIGGER, WAYPOINT, UNKNOWN };

struct fake {
    struct gid gid;
    int loaded;
    char script[0x40];
    char object[0x100];
    char transform[0x40]; /* a waypoint's, pointed to by its object */
};
static struct fake fakes[24];
static int fake_count;

static struct fake *make(unsigned cls, unsigned id, enum kind kind, float x, float y, float z)
{
    struct fake *f = &fakes[fake_count++];
    float at[3] = {x, y, z};
    void *object = f->object;

    memset(f, 0, sizeof *f);
    f->gid.cls = cls;
    f->gid.id = id;
    f->loaded = 1;
    switch (kind) {
    case MESH:
        *(void **)f->script = script_mesh_table;
        memcpy(f->script + 0xc, &object, 4);
        *(void **)f->object = object_mesh_table;
        memcpy(f->object + 0x70 + TRANSFORM_POSITION, at, sizeof at);
        break;
    case TRIGGER:
        *(void **)f->script = script_trigger_table;
        memcpy(f->script + 0x10, &object, 4);
        f->script[TRIGGER_ENABLED] = 1;
        *(void **)f->object = object_trigger_table;
        memcpy(f->object + 0x40 + TRANSFORM_POSITION, at, sizeof at);
        break;
    case WAYPOINT: {
        char *transform = f->transform - 0x10;
        *(void **)f->script = script_waypoint_table;
        memcpy(f->script + 0xc, &object, 4);
        *(void **)f->object = object_waypoint_table;
        memcpy(f->object + 0x28, &transform, 4);
        memcpy(f->transform + TRANSFORM_POSITION, at, sizeof at);
        break;
    }
    case UNKNOWN:
        *(void **)f->script = unknown_script_table;
        memcpy(f->script + 0x20, &object, 4);
        *(void **)f->object = unknown_object_table;
        memcpy(f->object + 0x80 + TRANSFORM_POSITION, at, sizeof at);
        break;
    }
    return f;
}

static struct fake *fake_of(unsigned cls, unsigned id)
{
    int i;

    for (i = 0; i < fake_count; i++)
        if (fakes[i].gid.cls == cls && fakes[i].gid.id == id)
            return &fakes[i];
    return NULL;
}

static void hide(struct fake *f, int hidden)
{
    unsigned flags = hidden ? 1 : 0;
    memcpy(f->script + MESH_HIDDEN, &flags, 4);
}

static void move_to(struct fake *f, float x, float y, float z) /* a mesh */
{
    float at[3] = {x, y, z};
    memcpy(f->object + 0x70 + TRANSFORM_POSITION, at, sizeof at);
}

/* ---------------------------------------------------------------- the game's functions */

__attribute__((thiscall)) static void fake_update(void *hud)
{
    if (hud == &hud_object)
        updates++;
}

__attribute__((thiscall)) static void fake_add(void *hud, const struct gid *gid, int sprite,
                                               const char *text, int fade_sprite, int fade_text)
{
    struct fake *f = fake_of(gid->cls, gid->id);
    struct vec3 at = {0, 0, 0};

    if (hud != &hud_object || !f || !f->loaded)
        return;
    adds++;
    add_sprite = sprite;
    add_text = text;
    add_fade[0] = fade_sprite;
    add_fade[1] = fade_text;
    position_of(f->script, &at);
    put_entry(gid, &at.x);
}

__attribute__((thiscall)) static void fake_remove(void *radar, const struct gid *gid)
{
    int i = find_entry(gid);

    removes++;
    if (radar == &radar_object && i >= 0)
        radar_object.base[i * ENTRY_SIZE + 0xd4] = 0; /* fades, stays */
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
    struct fake *f = fake_of(gid->cls, gid->id);

    lookups++;
    return map == &map_object && f && f->loaded ? f->script : NULL;
}

static int camera_state;
static char camera_object[0x600];
static void *camera_global = camera_object;
static int difficulty_level;
static void *difficulty_object = &difficulty_level;

static void place_camera(float x, float y, float z)
{
    float at[3] = {x, y, z};
    memcpy(camera_object + CAMERA_POSITION, at, sizeof at);
}

static DWORD WINAPI save_elsewhere(void *unused)
{
    (void)unused;
    my_save(&radar_object, (void *)0x1234);
    return 0;
}

/* ---------------------------------------------------------------- the generated table */

static int target(int category, int number, unsigned cls)
{
    int t, i;

    for (t = 0; t < TARGET_COUNT; t++)
        for (i = 0; i < RADAR_TARGETS[t].count; i++)
            if (RADAR_TARGETS[t].items[i].category == category
                && RADAR_TARGETS[t].items[i].number == number && RADAR_TARGETS[t].cls == cls)
                return t;
    return -1;
}

static void set_found(int t, int found)
{
    int i;

    for (i = 0; i < RADAR_TARGETS[t].count; i++)
        current.found[RADAR_TARGETS[t].items[i].category][RADAR_TARGETS[t].items[i].number] =
            (unsigned char)found;
}

int main(void)
{
    static const struct radar_host h = {record, state};
    int clock4, clock5, tor, sign, radio = -1, page = -1, t, i, n;
    HANDLE thread;
    struct fake *f4, *f5, *ftor_mesh, *ftor_trigger, *fsign, *fpage;

    /* the generated table: each item at most once per object, sensible classes */
    for (t = 0; t < TARGET_COUNT; t++) {
        const struct radar_target *r = &RADAR_TARGETS[t];
        EXPECT(r->cls == 85 || r->cls == 100 || r->cls == 113);
        EXPECT(r->count >= 1 && r->count <= 4);
        for (i = 0; i < r->count; i++) {
            EXPECT(r->items[i].category < CAT_COUNT);
            EXPECT(r->items[i].nightmare == 0 || r->items[i].category == CAT_MANUSCRIPTS);
        }
        if (r->count > 1 && r->items[0].category == CAT_RADIOSHOWS)
            radio = t;
        if (r->items[0].nightmare && r->cls == 100 && page < 0)
            page = t;
    }
    EXPECT(TARGET_COUNT > 300);
    clock4 = target(CAT_ALARMCLOCKS, 4, 100);
    clock5 = target(CAT_ALARMCLOCKS, 5, 100);
    tor = target(CAT_CUTOUTS, 5, 113);
    sign = target(CAT_SIGNS, 2, 85) >= 0 ? target(CAT_SIGNS, 2, 85) : target(CAT_SIGNS, 2, 113);
    EXPECT(clock4 >= 0 && clock5 >= 0 && tor >= 0 && sign >= 0 && radio >= 0 && page >= 0);
    EXPECT(RADAR_TARGETS[clock4].id == 0xECFA79C2 && RADAR_TARGETS[clock4].mesh == 0xECFA79C2);
    EXPECT(RADAR_TARGETS[tor].id == 0x7F351912 && RADAR_TARGETS[tor].mesh == 0x105E969B);
    EXPECT(RADAR_TARGETS[tor].trigger != 0);

    /* outside the game: nothing installed */
    EXPECT(radar_install(&h) == 0 && logged_has("radar markers unavailable"));
    EXPECT(strcmp(radar_note(), "\nNot available: see the log") == 0);

    radar_object.base = entry_memory;
    hud_global = &hud_object;
    radar_global = &radar_object;
    game.hud = &hud_global;
    game.radar = &radar_global;
    game.camera_state = &camera_state;
    for (i = 0; i < 8; i++)
        game.cameras[i] = &camera_global;
    game.difficulty = &difficulty_object;
    game.script_mesh = script_mesh_table;
    game.script_trigger = script_trigger_table;
    game.script_waypoint = script_waypoint_table;
    game.object_mesh = object_mesh_table;
    game.object_trigger = object_trigger_table;
    game.object_waypoint = object_waypoint_table;
    game.update = fake_update;
    game.add = fake_add;
    game.remove = fake_remove;
    game.save = fake_save;
    game.map_instance = fake_instance;
    game.map_pointer = fake_pointer;
    host = h;
    installed = 1;

    /* everything found but what the test loads */
    for (t = 0; t < TARGET_COUNT; t++)
        set_found(t, 1);
    set_found(clock4, 0);
    set_found(tor, 0);
    place_camera(0, 0, 0);
    f4 = make(100, RADAR_TARGETS[clock4].id, MESH, 10, 0, 0);
    make(113, RADAR_TARGETS[tor].id, WAYPOINT, 100, 0, 0);
    ftor_mesh = make(100, RADAR_TARGETS[tor].mesh, MESH, 100, 0, 0);
    ftor_trigger = make(85, RADAR_TARGETS[tor].trigger, TRIGGER, 100, 0, 0);

    /* off: no lookups at all, the game's update still runs */
    markers_on = 0;
    my_update(&hud_object);
    EXPECT(updates == 1 && hud_thread == GetCurrentThreadId() && lookups == 0 && adds == 0);

    /* on: the nearest at once, no save needed first, with the agreed arguments */
    markers_on = 1;
    tick();
    EXPECT(adds == 1 && marker == clock4 && radar_object.count == 1);
    EXPECT(add_sprite == 0 && add_text == NULL && add_fade[0] == 0 && add_fade[1] == 0);
    EXPECT(logged_has("radar: marker on Alarm clocks 4, level 100, 10 m away"));
    tick();
    EXPECT(adds == 1);

    /* an empty save first confirms the guard; ours are hidden from the next */
    my_save(&radar_object, (void *)0x1234);
    EXPECT(saves == 1 && save_saw_count == 0 && radar_object.count == 1);
    EXPECT(logged_has("saved without the 1 marker(s)"));

    /* moves only to an item clearly nearer */
    place_camera(59, 0, 0); /* clock 49 m, tor 41 m */
    tick();
    EXPECT(adds == 1 && marker == clock4);
    place_camera(80, 0, 0); /* clock 70 m, tor 20 m */
    tick();
    EXPECT(removes == 1 && adds == 2 && marker == tor && active_entries() == 1);
    EXPECT(logged_has("marker moved from Alarm clocks 4, level 100 (70 m) to Cardboard cutouts 5"));
    /* a save while one fades: both hidden */
    my_save(&radar_object, (void *)0x1234);
    EXPECT(save_saw_count == 0 && radar_object.count == 2);

    /* the tor's trigger switched off (being read, or not yet there): removed, clock marked
     * once its old entry has faded */
    ftor_trigger->script[TRIGGER_ENABLED] = 0;
    tick();
    EXPECT(marker == -1 && logged_has("removed: picked up or not available"));
    EXPECT(logged_has("radar: Cardboard cutouts 5, level 100 not marked: trigger switched off"));
    erase_entry(find_entry(gid_of(clock4)));
    tick();
    EXPECT(marker == clock4 && adds == 3);
    erase_entry(find_entry(gid_of(tor)));
    ftor_trigger->script[TRIGGER_ENABLED] = 1;

    /* the clock hidden (picked up, before the profile says so): removed; tor marked */
    hide(f4, 1);
    tick();
    EXPECT(removes == 3 && marker == tor && adds == 4);
    hide(f4, 0);
    /* the tor's mesh hidden, or not loaded: not available */
    hide(ftor_mesh, 1);
    tick();
    EXPECT(marker == -1);
    hide(ftor_mesh, 0);
    erase_entry(find_entry(gid_of(tor)));
    erase_entry(find_entry(gid_of(clock4)));
    ftor_mesh->loaded = 0;
    tick();
    EXPECT(marker == clock4 && adds == 5);
    ftor_mesh->loaded = 1;

    /* found in the profile: removed */
    current.found[CAT_ALARMCLOCKS][4] = 1;
    tick();
    EXPECT(logged_has("removed: found") && marker == tor && adds == 6);
    erase_entry(find_entry(gid_of(clock4)));

    /* switched off: removed, nothing looked up */
    markers_on = 0;
    n = lookups;
    tick();
    EXPECT(marker == -1 && logged_has("removed: markers switched off") && lookups == n);
    erase_entry(find_entry(gid_of(tor)));
    markers_on = 1;

    /* height counts: an item right above is farther than one 20 m away on the ground */
    set_found(tor, 1);
    set_found(clock5, 0);
    current.found[CAT_ALARMCLOCKS][4] = 0;
    place_camera(0, 0, 0);
    move_to(f4, 5, 50, 0);
    f5 = make(100, RADAR_TARGETS[clock5].id, MESH, 20, 0, 0);
    tick();
    EXPECT(marker == clock5 && adds == 7);

    /* level reload: the radar emptied, chosen again; a new radar object: forgotten */
    radar_object.count = 0;
    tick();
    EXPECT(marker == clock5 && adds == 8 && radar_object.count == 1);
    radar_global = &other_radar; /* as in a menu: another radar, no HUD */
    hud_global = NULL;
    tick();
    EXPECT(marker == -1);
    hud_global = &hud_object;
    radar_global = &radar_object;
    radar_object.count = 0;
    tick();
    EXPECT(marker == clock5 && adds == 9);
    radar_object.count = 0;
    marker = -1;
    set_found(clock5, 1);
    current.found[CAT_ALARMCLOCKS][4] = 1;
    f5->loaded = 0;

    /* Nightmare-only pages: only on Nightmare */
    set_found(page, 0);
    fpage = make(100, RADAR_TARGETS[page].id, MESH, 3, 0, 0);
    if (RADAR_TARGETS[page].mesh != RADAR_TARGETS[page].id)
        make(100, RADAR_TARGETS[page].mesh, MESH, 3, 0, 0);
    difficulty_level = 0;
    tick();
    EXPECT(marker == -1 && logged_has("radar: difficulty normal or hard"));
    difficulty_object = NULL;
    tick();
    EXPECT(marker == -1 && logged_has("difficulty unknown"));
    difficulty_object = &difficulty_level;
    difficulty_level = 2;
    tick();
    EXPECT(marker == page);
    difficulty_level = 0;
    tick(); /* back on normal: the page is not wanted */
    EXPECT(marker == -1);
    erase_entry(find_entry(gid_of(page)));
    set_found(page, 1);
    fpage->loaded = 0;

    /* an object of an unknown kind: the game's own methods give its position */
    set_found(sign, 0);
    fsign = make(RADAR_TARGETS[sign].cls, RADAR_TARGETS[sign].id, UNKNOWN, 7, 0, 0);
    if (RADAR_TARGETS[sign].trigger && RADAR_TARGETS[sign].trigger != RADAR_TARGETS[sign].id)
        make(85, RADAR_TARGETS[sign].trigger, TRIGGER, 7, 0, 0);
    tick();
    EXPECT(marker == sign && logged_has("unexpected type"));
    erase_entry(find_entry(gid_of(sign)));
    marker = -1;
    set_found(sign, 1);
    fsign->loaded = 0;

    /* a show of a radio that plays two: wanted while either is missing */
    set_found(radio, 1);
    current.found[RADAR_TARGETS[radio].items[1].category][RADAR_TARGETS[radio].items[1].number] = 0;
    make(RADAR_TARGETS[radio].cls, RADAR_TARGETS[radio].id, TRIGGER, 4, 0, 0);
    if (RADAR_TARGETS[radio].trigger && RADAR_TARGETS[radio].trigger != RADAR_TARGETS[radio].id)
        make(85, RADAR_TARGETS[radio].trigger, TRIGGER, 4, 0, 0);
    tick();
    EXPECT(marker == radio);

    /* a save on another thread: the radar left alone, the marker removed, none added */
    thread = CreateThread(NULL, 0, save_elsewhere, NULL, 0, NULL);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    EXPECT(save_saw_count == 1 && radar_object.count == 1 && stopped);
    EXPECT(logged_has("RADAR SAVE GUARD SKIPPED: other thread"));
    EXPECT(strcmp(radar_note(), "\nStopped: see the log") == 0);
    tick();
    EXPECT(marker == -1 && logged_has("removed: stopped"));
    erase_entry(0);
    n = adds;
    tick();
    EXPECT(adds == n);

    /* an active entry of ours that is not the marker (kept by a save): taken away */
    stopped = 0;
    radar_object.count = 0;
    marker = -1;
    markers_on = 0;
    put_entry(gid_of(clock4), NULL);
    n = removes;
    tick();
    EXPECT(removes == n + 1 && active_entries() == 0 && logged_has("unexpected marker of ours"));
    tick(); /* fading: left to the game */
    EXPECT(removes == n + 1);
    radar_object.count = 0;
    markers_on = 1;

    /* an entry that is not ours: left in, logged */
    stopped = 0;
    {
        struct gid foreign = {1, 2};
        put_entry(&foreign, NULL);
    }
    my_save(&radar_object, (void *)0x1234);
    EXPECT(save_saw_count == 1 && radar_object.count == 1 && stopped);
    EXPECT(logged_has("RADAR SAVE GUARD SKIPPED: entries not ours"));
    radar_object.count = 0;

    /* an unreadable radar: left alone */
    stopped = 0;
    radar_object.count = MAX_ENTRIES + 1;
    tick();
    EXPECT(stopped && logged_has("radar entries not as expected"));

    printf("radar test passed (%d targets)\n", TARGET_COUNT);
    return 0;
}
