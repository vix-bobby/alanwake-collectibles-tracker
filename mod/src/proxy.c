/*
 * d3d9.dll proxy for Alan Wake: forwards to the real Direct3D 9 and draws the
 * collectible totals on top of each frame. Adds files only; changes no game file.
 *
 * Another d3d9 mod can be kept by renaming it to d3d9_next.dll: it is loaded
 * instead of the system library.
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <d3d9.h>
#include <d3dx9core.h>
#include <stdarg.h>
#include <stdio.h>

#include "checkpoint.h"
#include "counts.h"
#include "menu.h"
#include "path.h"
#include "profile.h"
#include "radar.h"
#include "settings.h"
#include "steam.h"

#define MOD_VERSION "0.9.0"

static HMODULE self, next;
static int hooks_allowed;
static CRITICAL_SECTION lock;
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;

/* ---------------------------------------------------------------- log */

static char log_path[PATH_UTF8];

static void module_dir(char *out, size_t size)
{
    wchar_t wide[PATH_WIDE];
    char *slash;
    DWORD n = GetModuleFileNameW(self, wide, PATH_WIDE);

    wide[n < PATH_WIDE ? n : PATH_WIDE - 1] = L'\0';
    to_utf8(wide, out, (int)size);
    slash = strrchr(out, '\\');
    if (slash)
        slash[1] = '\0';
}

static FILE *open_log(const wchar_t *mode)
{
    wchar_t wide[PATH_WIDE];

    return to_wide(log_path, wide, PATH_WIDE) ? _wfopen(wide, mode) : NULL;
}

static void logf_(const char *format, ...)
{
    FILE *f;
    SYSTEMTIME t;
    va_list args;

    if (!log_path[0] || !(f = open_log(L"a")))
        return;
    GetLocalTime(&t);
    fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u  ", t.wYear, t.wMonth, t.wDay, t.wHour,
            t.wMinute, t.wSecond);
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

static BOOL CALLBACK initialise(PINIT_ONCE o, PVOID p, PVOID *c)
{
    char dir[PATH_UTF8], path[PATH_UTF8 + 32];
    wchar_t wide[PATH_WIDE];
    FILE *f;

    (void)o, (void)p, (void)c;
    InitializeCriticalSection(&lock);

    /* Never unload. The hooks live in tables that belong to the real library and to the game,
     * and the game calls FreeLibrary on us when it discards its Direct3D object. */
    HMODULE pinned;
    BOOL stays = GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN
                                        | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                    (LPCSTR)(void *)initialise, &pinned);

    /* log next to the DLL, or in %TEMP% if that folder is read-only */
    module_dir(dir, sizeof dir);
    snprintf(log_path, sizeof log_path, "%salanwake_collectibles.log", dir);
    if ((f = open_log(L"w")))
        fclose(f);
    else if (GetTempPathW(PATH_WIDE, wide) && to_utf8(wide, path, sizeof path))
        snprintf(log_path, sizeof log_path, "%salanwake_collectibles.log", path);
    logf_("alanwake collectibles mod " MOD_VERSION " loaded from %s", dir);
    if (!stays) /* hooking would be unsafe: forward only */
        logf_("cannot keep the mod loaded (error %lu): hooks disabled", GetLastError());
    hooks_allowed = stays;
    logf_("module at %p, kept loaded: %s", (void *)self, stays ? "yes" : "no");

    snprintf(path, sizeof path, "%sd3d9_next.dll", dir);
    if (path_attributes(path) == INVALID_FILE_ATTRIBUTES) {
        UINT n = GetSystemDirectoryW(wide, MAX_PATH);
        if (!n || n >= MAX_PATH || !to_utf8(wide, path, MAX_PATH * 3))
            return TRUE;
        strcat(path, "\\d3d9.dll");
    }
    next = to_wide(path, wide, PATH_WIDE) ? LoadLibraryW(wide) : NULL;
    logf_("forwarding to %s: %s", path, next ? "ok" : "FAILED");
    snprintf(path, sizeof path, "%salanwake_collectibles.ini", dir);
    settings_load(path, MOD_VERSION);
    logf_("overlay %s-%s, margins %d%% %d%%", POSITION.bottom ? "bottom" : "top",
          POSITION.right ? "right" : "left", POSITION.margin_x, POSITION.margin_y);
    for (int i = 0; i < OPT_COUNT; i++) {
        char key[8];
        logf_("option %s: %s, key %s", OPTIONS[i].name, OPTIONS[i].enabled ? "on" : "off",
              key_name(OPTIONS[i].key, key));
    }
    return TRUE;
}

static FARPROC forward(const char *name)
{
    InitOnceExecuteOnce(&once, initialise, NULL, NULL);
    return next ? GetProcAddress(next, name) : NULL;
}

/* ---------------------------------------------------------------- profile */

static struct profile profile;
static int profile_ok;
static FILETIME profile_time, seen_time;
static DWORD last_check;
static char status[256] = "profile not read yet";

static int registry_string(const wchar_t *key, const wchar_t *value, char *out, int size)
{
    wchar_t wide[PATH_WIDE];
    DWORD bytes = sizeof wide;

    return RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_SZ, NULL, wide, &bytes)
               == ERROR_SUCCESS
           && to_utf8(wide, out, size);
}

/* <Steam>\userdata\<account>\108710\remote\config, see steam.c */
static int profile_path(char *out, size_t size)
{
    static char cached[PATH_UTF8 * 2], reported;
    char steam[PATH_UTF8] = "", game[PATH_UTF8], report[8192];
    wchar_t given[PATH_WIDE];
    struct steam_hints hints;
    DWORD user = 0, bytes = sizeof user;

    /* for testing and unusual setups */
    DWORD n = GetEnvironmentVariableW(L"ALANWAKE_COLLECTIBLES_PROFILE", given, PATH_WIDE);
    if (n && n < PATH_WIDE)
        return to_utf8(given, out, (int)size);
    if (cached[0] && path_attributes(cached) != INVALID_FILE_ATTRIBUTES) {
        snprintf(out, size, "%s", cached);
        return 1;
    }
    if (!registry_string(L"Software\\Valve\\Steam", L"SteamPath", steam, sizeof steam))
        steam[0] = '\0';
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser",
                     RRF_RT_REG_DWORD, NULL, &user, &bytes) != ERROR_SUCCESS)
        user = 0;
    module_dir(game, sizeof game);
    hints.registry_path = steam;
    hints.active_user = user;
    hints.game_dir = game;
    if (!steam_find_profile(&hints, out, size, report, sizeof report)) {
        if (!reported++)
            logf_("no profile found (account %lu); looked for userdata in:\n%s",
                  (unsigned long)user, report);
        return 0;
    }
    snprintf(cached, sizeof cached, "%s", out);
    return 1;
}

/* Log a drawing problem once, so a missing overlay can be explained from the log. */
#define COMPLAIN(...)                                                         \
    do {                                                                     \
        static int told_;                                                    \
        if (!told_++)                                                        \
            logf_(__VA_ARGS__);                                              \
    } while (0)

static void fail(const char *why)
{
    if (strcmp(status, why) != 0) {
        snprintf(status, sizeof status, "%s", why);
        logf_("%s", why);
    }
}

/* Re-read the profile when it changes; checked once a second. */
static void refresh_profile(void)
{
    static unsigned char data[65536];
    WIN32_FILE_ATTRIBUTE_DATA info;
    struct profile fresh;
    char path[PATH_UTF8 * 2];
    DWORD now = GetTickCount(), size = 0;
    HANDLE file;
    BOOL read;
    int i;

    if (last_check && now - last_check < 1000)
        return;
    last_check = now ? now : 1;
    if (!profile_path(path, sizeof path)) {
        fail("Steam profile folder not found");
        return;
    }
    if (!path_info(path, &info)) {
        fail("profile file not found");
        return;
    }
    if (profile_ok && CompareFileTime(&info.ftLastWriteTime, &profile_time) == 0)
        return;
    /* stay out of the way of a save in progress: wait for the timestamp to hold still */
    if (profile_ok && CompareFileTime(&info.ftLastWriteTime, &seen_time) != 0) {
        seen_time = info.ftLastWriteTime;
        return;
    }

    file = path_open_read(path);
    if (file == INVALID_HANDLE_VALUE) {
        fail("profile file cannot be opened");
        return;
    }
    read = ReadFile(file, data, sizeof data, &size, NULL);
    CloseHandle(file);
    /* a half-written file fails to parse and is retried on the next check */
    if (!read || profile_parse(data, size, &fresh) != 0) {
        fail("profile file not understood");
        return;
    }
    profile = fresh;
    profile_ok = 1;
    profile_time = info.ftLastWriteTime;
    status[0] = '\0';
    logf_("profile read (%lu bytes) from %s", (unsigned long)size, path);
    for (i = 0; i < CAT_COUNT; i++)
        logf_("  %-18s %3d / %3d", CATEGORIES[i].label, profile.count[i], CATEGORIES[i].total);
}

/* ---------------------------------------------------------------- checkpoint */

static int level; /* of the last checkpoint, 0 = unknown */

/* Re-read the level name when the save file changes; checked once a second. */
static void refresh_checkpoint(void)
{
    static FILETIME read_time, seen;
    static DWORD last;
    static int loaded;
    WIN32_FILE_ATTRIBUTE_DATA info;
    char path[PATH_UTF8 * 2], *name;
    unsigned char header[128];
    DWORD now = GetTickCount(), size = 0;
    HANDLE file;

    if (last && now - last < 1000)
        return;
    last = now ? now : 1;
    /* the save sits next to the profile */
    if (!profile_path(path, sizeof path - 16))
        return;
    name = path + strlen(path);
    while (name > path && name[-1] != '/' && name[-1] != '\\')
        name--;
    strcpy(name, "savegame.aws");
    if (!path_info(path, &info)) {
        level = loaded = 0;
        return;
    }
    if (loaded && CompareFileTime(&info.ftLastWriteTime, &read_time) == 0)
        return;
    /* as for the profile, do not open a file that is still being written */
    if (loaded && CompareFileTime(&info.ftLastWriteTime, &seen) != 0) {
        seen = info.ftLastWriteTime;
        return;
    }
    file = path_open_read(path);
    if (file == INVALID_HANDLE_VALUE)
        return;
    if (ReadFile(file, header, sizeof header, &size, NULL)) {
        int found = checkpoint_level(header, size);

        if (found) { /* otherwise half written: try again */
            if (found != level)
                logf_("checkpoint is in level %d", found);
            level = found;
            loaded = 1;
            read_time = info.ftLastWriteTime;
        }
    }
    CloseHandle(file);
}

/* ---------------------------------------------------------------- menus */

static int menu_installed;

/* Called from the game's menu drawing, possibly on another thread than the frames. */
static int menu_state(struct profile *out, struct menu_options *options)
{
    int ok;

    EnterCriticalSection(&lock);
    refresh_profile();
    ok = profile_ok;
    if (ok)
        *out = profile;
    options->enabled = OPTIONS[OPT_MENU_COUNTS].enabled;
    options->detail = OPTIONS[OPT_MENU_DETAIL].enabled;
    LeaveCriticalSection(&lock);
    return ok;
}

static int radar_installed;

/* Called from the game's HUD update, once a second. */
static int radar_state(struct profile *out, int *markers)
{
    int ok;

    EnterCriticalSection(&lock);
    refresh_profile();
    ok = profile_ok;
    if (ok)
        *out = profile;
    *markers = OPTIONS[OPT_RADAR_MARKERS].enabled;
    LeaveCriticalSection(&lock);
    return ok;
}

/* ---------------------------------------------------------------- hotkeys */

#define MESSAGE_MS 4000
#define INTRO_MS 12000

static char message[160];
static DWORD message_until;

static void show_message(DWORD milliseconds, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    message_until = GetTickCount() + milliseconds;
}

static int message_visible(void)
{
    return message[0] && (LONG)(message_until - GetTickCount()) > 0;
}

/* Called once per frame. Keys only count while the game has the keyboard. */
static void poll_keys(void)
{
    static int held[OPT_COUNT];
    DWORD owner = 0;
    HWND window = GetForegroundWindow();
    int alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    int i;

    if (window)
        GetWindowThreadProcessId(window, &owner);
    for (i = 0; i < OPT_COUNT; i++) {
        struct option *o = &OPTIONS[i];
        int down = o->key && owner == GetCurrentProcessId() && (GetAsyncKeyState(o->key) & 0x8000);

        if (o->key == VK_F4 && alt)
            down = 0; /* Alt+F4 closes the game */

        if (down && !held[i] && i == OPT_RADAR_MARKERS && !radar_installed) {
            char key[8];

            /* nothing would show: say why instead of switching */
            show_message(MESSAGE_MS, "%s: not available  (%s)\n%s", o->label,
                         key_name(o->key, key),
                         OPTIONS[OPT_RADAR_HOOKS].enabled
                             ? "This version of the game is not supported"
                             : "radar_hooks=0 in alanwake_collectibles.ini");
        } else if (down && !held[i]) {
            char key[8];
            int saved = settings_toggle(i);

            show_message(MESSAGE_MS, "%s: %s  (%s to change)%s%s", o->label,
                         o->enabled ? "on" : "off", key_name(o->key, key),
                         i == OPT_MENU_SMALL || i == OPT_MENU_HOOKS || i == OPT_RADAR_HOOKS
                                 || ((i == OPT_MENU_COUNTS || i == OPT_MENU_DETAIL)
                                     && !menu_installed)
                             ? "\nApplies the next time the game starts"
                         : (i == OPT_EXPANDED || i == OPT_PAGE_TITLES || i == OPT_ITEM_DETAILS)
                                 && !OPTIONS[OPT_OVERLAY].enabled
                             ? "\nThe overlay is hidden"
                         : (i == OPT_PAGE_TITLES || i == OPT_ITEM_DETAILS)
                                 && !OPTIONS[OPT_EXPANDED].enabled
                             ? "\nShown in the missing list"
                         : i == OPT_RADAR_MARKERS && o->enabled ? radar_note() : "",
                         saved ? "" : "\nCould not save to alanwake_collectibles.ini");
            logf_("%s switched %s%s", o->name, o->enabled ? "on" : "off",
                  saved ? "" : " (not saved)");
        }
        held[i] = down != 0;
    }
}

/* With the overlay hidden, say once how to bring it back. */
static void introduce(void)
{
    static int done;
    char key[8];

    if (done++ || OPTIONS[OPT_OVERLAY].enabled || !OPTIONS[OPT_OVERLAY].key)
        return;
    show_message(INTRO_MS, "Collectibles mod: press %s to show the overlay",
                 key_name(OPTIONS[OPT_OVERLAY].key, key));
}

/* The overlay is a stack of text blocks in two sizes; the keys are in two columns. */
enum {
    BLOCK_HEADER, BLOCK_COUNTS, BLOCK_MISSING, BLOCK_FOOTER, BLOCK_KEYS, BLOCK_LIST_KEYS,
    BLOCK_MESSAGE, BLOCK_COUNT
};

struct block {
    char text[2000];
    int small;
    int gap;    /* empty line above, if anything precedes */
    int beside; /* to the right of the previous block instead of under it */
};

static void add_line(struct block *b, const char *format, ...)
{
    size_t n = strlen(b->text);
    va_list args;

    if (n && n + 1 < sizeof b->text)
        b->text[n++] = '\n';
    va_start(args, format);
    vsnprintf(b->text + n, sizeof b->text - n, format, args);
    va_end(args);
}

/* Returns 0 if there is nothing to show. */
static int overlay_blocks(struct block *blocks)
{
    struct block *counts = &blocks[BLOCK_COUNTS], *footer = &blocks[BLOCK_FOOTER];
    int i, shown = 0;

    memset(blocks, 0, BLOCK_COUNT * sizeof *blocks);
    blocks[BLOCK_HEADER].small = footer->small = 1;
    blocks[BLOCK_KEYS].small = blocks[BLOCK_LIST_KEYS].small = 1;
    blocks[BLOCK_LIST_KEYS].beside = 1;
    blocks[BLOCK_MISSING].gap = footer->gap = blocks[BLOCK_MESSAGE].gap = 1;
    if (OPTIONS[OPT_OVERLAY].enabled) {
        add_line(&blocks[BLOCK_HEADER], "Collectibles mod " MOD_VERSION);
        if (!profile_ok)
            add_line(counts, "%s", status);
        else if (chapter_text(&profile, level, counts->text, sizeof counts->text)) {
            size_t n = strlen(counts->text);

            while (n && counts->text[n - 1] == '\n')
                counts->text[--n] = '\0';
            add_line(footer, "(chapter of your last checkpoint)");
            if (OPTIONS[OPT_EXPANDED].enabled)
                missing_text(&profile, level, OPTIONS[OPT_PAGE_TITLES].enabled,
                             OPTIONS[OPT_ITEM_DETAILS].enabled,
                             blocks[BLOCK_MISSING].text, sizeof blocks[BLOCK_MISSING].text);
        } else
            add_line(counts, "Current chapter unknown: no checkpoint found");
        /* left column: general keys; right column: the missing list and what it shows */
        for (i = 0; i < OPT_COUNT; i++) {
            int list = i == OPT_EXPANDED || i == OPT_PAGE_TITLES || i == OPT_ITEM_DETAILS;
            char key[8];
            /* the spoiler keys matter only while the list is shown */
            if (OPTIONS[i].key && (i != OPT_RADAR_MARKERS || radar_installed)
                && (OPTIONS[OPT_EXPANDED].enabled
                    || (i != OPT_PAGE_TITLES && i != OPT_ITEM_DETAILS)))
                add_line(&blocks[list ? BLOCK_LIST_KEYS : BLOCK_KEYS], "[%s]  %s: %s",
                         key_name(OPTIONS[i].key, key), OPTIONS[i].label,
                         OPTIONS[i].enabled ? "on" : "off");
        }
    }
    if (message_visible())
        add_line(&blocks[BLOCK_MESSAGE], "%s", message);
    for (i = 0; i < BLOCK_COUNT; i++)
        shown |= blocks[i].text[0] != '\0';
    return shown;
}

/* ---------------------------------------------------------------- overlay */

typedef HRESULT(WINAPI *create_font_fn)(IDirect3DDevice9 *, INT, UINT, UINT, UINT, BOOL, DWORD,
                                        DWORD, DWORD, DWORD, const char *, ID3DXFont **);

static ID3DXFont *fonts[2]; /* normal, small */
static IDirect3DDevice9 *font_device;

static void release_font(void)
{
    int i;

    for (i = 0; i < 2; i++) {
        if (fonts[i])
            ID3DXFont_Release(fonts[i]);
        fonts[i] = NULL;
    }
    font_device = NULL;
}

static int ensure_fonts(IDirect3DDevice9 *device, int height)
{
    static create_font_fn create;
    static int failed;
    int i, small = height * 3 / 4 < 12 ? 12 : height * 3 / 4;

    if (fonts[0] && font_device == device)
        return 1;
    release_font();
    if (!create) {
        HMODULE d3dx = LoadLibraryA("d3dx9_43.dll"); /* the game depends on it too */
        if (d3dx)
            create = (create_font_fn)(void *)GetProcAddress(d3dx, "D3DXCreateFontA");
    }
    for (i = 0; i < 2; i++)
        if (!create
            || FAILED(create(device, i ? small : height, 0, i ? FW_NORMAL : FW_BOLD, 1, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, "Segoe UI", &fonts[i]))) {
            release_font();
            if (!failed++)
                logf_("cannot create the overlay font (d3dx9_43.dll)");
            return 0;
        }
    font_device = device;
    return 1;
}

static void draw_blocks(IDirect3DDevice9 *device, const struct block *blocks)
{
    D3DVIEWPORT9 view;
    D3DRECT box;
    RECT at[BLOCK_COUNT]; /* where each block goes, from the box's top left */
    int height, pad, width = 0, total = 0, x, y, i;
    int top = 0, right = 0; /* of the last block placed, for one beside it */
    HRESULT hr = IDirect3DDevice9_GetViewport(device, &view);

    if (FAILED(hr)) {
        COMPLAIN("GetViewport failed: 0x%08lx", (unsigned long)hr);
        return;
    }
    height = (int)view.Height / 60 < 14 ? 14 : (int)view.Height / 60;
    pad = height / 2;
    if (!ensure_fonts(device, height))
        return;

    for (i = 0; i < BLOCK_COUNT; i++) {
        RECT size;
        int beside = blocks[i].beside && blocks[i - 1].text[0];

        if (!blocks[i].text[0])
            continue;
        SetRect(&size, 0, 0, 0, 0);
        ID3DXFont_DrawTextA(fonts[blocks[i].small], NULL, blocks[i].text, -1, &size,
                            DT_CALCRECT | DT_LEFT, 0);
        if (!beside) {
            if (blocks[i].gap && total)
                total += height;
            top = total;
        }
        OffsetRect(&size, beside ? right + 2 * height : 0, top);
        at[i] = size;
        right = size.right;
        if (size.bottom > total)
            total = size.bottom;
        if (size.right > width)
            width = size.right;
    }

    /* place the box in its corner, keeping it on the screen */
    x = (int)view.Width * POSITION.margin_x / 100 + pad;
    if (POSITION.right)
        x = (int)view.Width - x - width;
    y = (int)view.Height * POSITION.margin_y / 100 + pad;
    if (POSITION.bottom)
        y = (int)view.Height - y - total;
    if (x > (int)view.Width - width - pad)
        x = (int)view.Width - width - pad;
    if (y > (int)view.Height - total - pad)
        y = (int)view.Height - total - pad;
    if (x < pad)
        x = pad;
    if (y < pad)
        y = pad;

    box.x1 = x - pad;
    box.y1 = y - pad;
    box.x2 = x + width + pad;
    box.y2 = y + total + pad;
    IDirect3DDevice9_Clear(device, 1, &box, D3DCLEAR_TARGET, D3DCOLOR_XRGB(12, 12, 16), 0, 0);

    for (i = 0; i < BLOCK_COUNT; i++) {
        RECT area;

        if (!blocks[i].text[0])
            continue;
        area = at[i];
        OffsetRect(&area, x, y);
        ID3DXFont_DrawTextA(fonts[blocks[i].small], NULL, blocks[i].text, -1, &area,
                            DT_LEFT | DT_NOCLIP,
                            blocks[i].small ? D3DCOLOR_XRGB(176, 176, 170)
                                            : D3DCOLOR_XRGB(240, 240, 232));
    }
}

/* Draw onto the back buffer, leaving the game's device state as it was. */
static void overlay(IDirect3DDevice9 *device)
{
    IDirect3DSurface9 *back = NULL, *target = NULL, *depth = NULL;
    IDirect3DStateBlock9 *state = NULL;
    D3DVIEWPORT9 view;
    static int announced;
    HRESULT hr;
    static struct block blocks[BLOCK_COUNT];

    if (IDirect3DDevice9_TestCooperativeLevel(device) != D3D_OK) {
        release_font(); /* lost device: hold nothing that would block Reset */
        return;
    }
    refresh_profile();
    refresh_checkpoint();
    introduce();
    poll_keys();
    if (!overlay_blocks(blocks))
        return;
    hr = IDirect3DDevice9_CreateStateBlock(device, D3DSBT_ALL, &state);
    if (FAILED(hr)) {
        COMPLAIN("CreateStateBlock failed: 0x%08lx", (unsigned long)hr);
        return;
    }
    IDirect3DDevice9_GetViewport(device, &view);
    IDirect3DDevice9_GetRenderTarget(device, 0, &target);
    IDirect3DDevice9_GetDepthStencilSurface(device, &depth);

    hr = IDirect3DDevice9_GetBackBuffer(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    if (FAILED(hr))
        COMPLAIN("GetBackBuffer failed: 0x%08lx", (unsigned long)hr);
    else {
        IDirect3DDevice9_SetRenderTarget(device, 0, back); /* also resets the viewport */
        IDirect3DDevice9_SetDepthStencilSurface(device, NULL);
        hr = IDirect3DDevice9_BeginScene(device);
        if (FAILED(hr))
            COMPLAIN("BeginScene failed: 0x%08lx", (unsigned long)hr);
        else {
            IDirect3DDevice9_SetRenderState(device, D3DRS_SRGBWRITEENABLE, FALSE);
            draw_blocks(device, blocks);
            IDirect3DDevice9_EndScene(device);
            if (!announced++)
                logf_("first frame drawn");
        }
        IDirect3DSurface9_Release(back);
    }

    if (target) {
        IDirect3DDevice9_SetRenderTarget(device, 0, target);
        IDirect3DSurface9_Release(target);
    }
    IDirect3DDevice9_SetDepthStencilSurface(device, depth);
    if (depth)
        IDirect3DSurface9_Release(depth);
    IDirect3DStateBlock9_Apply(state);
    IDirect3DStateBlock9_Release(state);
    IDirect3DDevice9_SetViewport(device, &view);
}

/* ---------------------------------------------------------------- hooks */

typedef HRESULT(WINAPI *create_device_fn)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD,
                                          D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef HRESULT(WINAPI *reset_fn)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
typedef HRESULT(WINAPI *present_fn)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND,
                                    const RGNDATA *);
typedef HRESULT(WINAPI *chain_present_fn)(IDirect3DSwapChain9 *, const RECT *, const RECT *, HWND,
                                          const RGNDATA *, DWORD);

static create_device_fn real_create_device;
static reset_fn real_reset;
static present_fn real_present;
static chain_present_fn real_chain_present;
static __thread int presenting; /* one overlay per frame if Present nests */

static int in_forwarded_library(void *address)
{
    HMODULE owner = NULL;

    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              address, &owner)
           && owner == next;
}

/* Put a hook in a method table, or put it back: the Direct3D runtime rewrites entries of the
 * device's table (creating a state block is enough). Once hooked, an entry is replaced only
 * when it points into the library we forward to. Anything else there has hooked on top of us
 * (Windows' compatibility layer does at a program's first start, overlays can too) and calls
 * us itself: taking it as the function to call would make the two call each other forever. */
static void patch(void *slot, void *hook, void **original)
{
    void **entry = slot;
    DWORD protection;

    if (*entry == hook || (*original && !in_forwarded_library(*entry))
        || !VirtualProtect(entry, sizeof *entry, PAGE_EXECUTE_READWRITE, &protection))
        return;
    *original = *entry;
    *entry = hook;
    VirtualProtect(entry, sizeof *entry, protection, &protection);
}

static HRESULT WINAPI my_reset(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *parameters);
static HRESULT WINAPI my_present(IDirect3DDevice9 *device, const RECT *source, const RECT *dest,
                                 HWND window, const RGNDATA *dirty);

static void restore_hooks(IDirect3DDevice9 *device)
{
    patch(&device->lpVtbl->Reset, my_reset, (void **)&real_reset);
    patch(&device->lpVtbl->Present, my_present, (void **)&real_present);
}

static void hooked_frame(IDirect3DDevice9 *device)
{
    EnterCriticalSection(&lock);
    overlay(device);
    restore_hooks(device);
    LeaveCriticalSection(&lock);
}

static HRESULT WINAPI my_present(IDirect3DDevice9 *device, const RECT *source, const RECT *dest,
                                 HWND window, const RGNDATA *dirty)
{
    HRESULT result;

    if (!presenting++) {
        COMPLAIN("frames arrive through IDirect3DDevice9::Present");
        hooked_frame(device);
    }
    result = real_present(device, source, dest, window, dirty);
    restore_hooks(device);
    presenting--;
    return result;
}

static HRESULT WINAPI my_chain_present(IDirect3DSwapChain9 *chain, const RECT *source,
                                       const RECT *dest, HWND window, const RGNDATA *dirty,
                                       DWORD flags)
{
    IDirect3DDevice9 *device = NULL;
    HRESULT result;

    if (!presenting++ && SUCCEEDED(IDirect3DSwapChain9_GetDevice(chain, &device))) {
        COMPLAIN("frames arrive through IDirect3DSwapChain9::Present");
        hooked_frame(device);
        IDirect3DDevice9_Release(device);
    }
    result = real_chain_present(chain, source, dest, window, dirty, flags);
    presenting--;
    return result;
}

static void hook_device(IDirect3DDevice9 *device)
{
    IDirect3DSwapChain9 *chain = NULL;

    patch(&device->lpVtbl->Reset, my_reset, (void **)&real_reset);
    patch(&device->lpVtbl->Present, my_present, (void **)&real_present);
    if (SUCCEEDED(IDirect3DDevice9_GetSwapChain(device, 0, &chain))) {
        patch(&chain->lpVtbl->Present, my_chain_present, (void **)&real_chain_present);
        IDirect3DSwapChain9_Release(chain);
    }
}

static HRESULT WINAPI my_reset(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *parameters)
{
    HRESULT result;

    /* the font owns default-pool resources, which would make Reset fail */
    EnterCriticalSection(&lock);
    release_font();
    LeaveCriticalSection(&lock);
    result = real_reset(device, parameters);
    logf_("device reset: 0x%08lx", (unsigned long)result);
    hook_device(device);
    return result;
}

static HRESULT WINAPI my_create_device(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND window,
                                       DWORD flags, D3DPRESENT_PARAMETERS *parameters,
                                       IDirect3DDevice9 **device)
{
    HRESULT result;

    logf_("CreateDevice: calling");
    result = real_create_device(d3d, adapter, type, window, flags, parameters, device);
    logf_("CreateDevice: 0x%08lx, behaviour flags 0x%08lx", (unsigned long)result,
          (unsigned long)flags);
    if (SUCCEEDED(result) && device && *device) {
        if (parameters)
            logf_("  %ux%u, %s", parameters->BackBufferWidth, parameters->BackBufferHeight,
                  parameters->Windowed ? "windowed" : "fullscreen");
        hook_device(*device);
    }
    return result;
}

/* ---------------------------------------------------------------- exports */

IDirect3D9 *WINAPI Direct3DCreate9(UINT version)
{
    typedef IDirect3D9 *(WINAPI *fn)(UINT);
    fn real = (fn)(void *)forward("Direct3DCreate9");
    IDirect3D9 *d3d = real ? real(version) : NULL;
    static LONG calls;

    logf_("Direct3DCreate9 (call %ld): %s", InterlockedIncrement(&calls), d3d ? "ok" : "FAILED");
    if (d3d && hooks_allowed)
        patch(&d3d->lpVtbl->CreateDevice, my_create_device, (void **)&real_create_device);
    if (!hooks_allowed) {
        /* nothing to install */
    } else if (next && !OPTIONS[OPT_MENU_HOOKS].enabled) {
        COMPLAIN("menu_hooks=0: menus left alone");
    } else if (next) {
        static const struct menu_host host = {logf_, menu_state};
        menu_installed = menu_install(&host, OPTIONS[OPT_MENU_SMALL].enabled);
    }
    if (!hooks_allowed) {
        /* nothing to install */
    } else if (next && !OPTIONS[OPT_RADAR_HOOKS].enabled) {
        COMPLAIN("radar_hooks=0: radar left alone");
    } else if (next) {
        static const struct radar_host radar_host = {logf_, radar_state};
        radar_installed = radar_install(&radar_host);
    }
    return d3d;
}

HRESULT WINAPI Direct3DCreate9Ex(UINT version, IDirect3D9Ex **out)
{
    typedef HRESULT(WINAPI *fn)(UINT, IDirect3D9Ex **);
    fn real = (fn)(void *)forward("Direct3DCreate9Ex");

    logf_("Direct3DCreate9Ex called (no overlay on this path)");
    return real ? real(version, out) : D3DERR_NOTAVAILABLE;
}

#define FORWARD(result, name, parameters, arguments, fallback)                 \
    result WINAPI name parameters                                              \
    {                                                                          \
        typedef result(WINAPI *fn) parameters;                                 \
        fn real = (fn)(void *)forward(#name);                                  \
        if (!real)                                                             \
            return fallback;                                                   \
        return real arguments;                                                 \
    }

FORWARD(int, D3DPERF_BeginEvent, (D3DCOLOR c, LPCWSTR n), (c, n), -1)
FORWARD(int, D3DPERF_EndEvent, (void), (), -1)
FORWARD(DWORD, D3DPERF_GetStatus, (void), (), 0)
FORWARD(BOOL, D3DPERF_QueryRepeatFrame, (void), (), FALSE)

#define FORWARD_VOID(name, parameters, arguments)                              \
    void WINAPI name parameters                                                \
    {                                                                          \
        typedef void(WINAPI *fn) parameters;                                   \
        fn real = (fn)(void *)forward(#name);                                  \
        if (real)                                                              \
            real arguments;                                                    \
    }

FORWARD_VOID(D3DPERF_SetMarker, (D3DCOLOR c, LPCWSTR n), (c, n))
FORWARD_VOID(D3DPERF_SetOptions, (DWORD o), (o))
FORWARD_VOID(D3DPERF_SetRegion, (D3DCOLOR c, LPCWSTR n), (c, n))

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        self = instance; /* everything else waits for the first export call */
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
