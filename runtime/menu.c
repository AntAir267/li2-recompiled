/* The port's settings in the game's own Options screens (the goals: no launcher, no file needed).

   The front end's menus are data, run by 0x4187d5 with the state at 0x974460 [verified: decompiles
   of 0x4187d5, 0x418125, 0x86c174, 0x86c815]: the state's +0x64 (0x9744c4) points at a table of
   screens, 0x14 bytes each (kind, rows counting the title, the rows, a handler, a flag), and each
   screen's rows, 0x4c bytes each: the label (0x3c bytes, "!KEY" for a line of the FE text files),
   0, the type (1 opens a screen, 2 back, 5 an ON / OFF toggle, 6 a slider of 0..100 in tens), the
   value, flags (0x20 greyed). Row 0 is the screen's title; rows from 10 on are the key-binding
   table's (0x9cc178), so a screen keeps under 10. One handler, 0x86c174 (cdecl (type,
   value, x)), applies a change, picking the setting by the current screen (0x974460) and row
   (0x974464, from 1); 0x86c815 copies the settings into the rows when the menus open.

   Here: Graphics (screen 10), Controls (9) and Cut Scenes (12) get rows of their own, inserted
   before BACK in a copy of the screen's rows (the screen table points at the copy, with the new
   count), and screens of ours (a copy of the screen table with four more), DISPLAY (29) opened
   from a row of Graphics, CAMERA (30), CONTROLLER (31) and PROMPTS (32) from rows of Controls (a
   panel holds the title and 7 rows); the two functions are hooked for
   those rows. Each change is applied at once and kept in
   options.txt. */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "options.h"
#include "funcs.h"

#define CUR_SCREEN   0x974460u
#define CUR_ROW      0x974464u
#define ROW_SIZE     0x4c
enum { T_SCREEN = 1, T_TOGGLE = 5, T_SLIDER = 6 };

typedef struct {
    int screen;
    const char *label;
    int type;
    int (*get)(void);                   /* the value shown: 0 / 1, or 0..100 */
    void (*set)(int v);
    int opens;                          /* T_SCREEN: the screen it opens */
} Row;

/* ------------------------------------------------------------------ the settings */
void d3d_fog_set(int on);
void framerate_steps_set(int on);
void frame_limit_options(void);
void pad_camera_speed_set(int speed);
void win_rumble_set(int on);
void pad_deadzone_set(double percent);
void camera_tilt_set(int percent);
void vk_options(void);
void d3d_gpu_transform_set(int on);
void d3d_native_set(int on);

static int fog_get(void) { return option_bool("fog", 0); }
static void fog_set(int v) { option_set("fog", v ? "on" : "off"); d3d_fog_set(v); }

static int vsync_get(void) { return option_bool("vsync", 1); }
static void vsync_set(int v) { option_set("vsync", v ? "on" : "off"); frame_limit_options(); vk_options(); }

static int aa_get(void) { const char *a = option_str("antialiasing", "off"); return strcasecmp(a, "off") && strcmp(a, "0"); }
static void aa_set(int v) { option_set("antialiasing", v ? "4x" : "off"); vk_options(); }

static int gpuxf_get(void) { return option_bool("gpu_transform", 0); }
static void gpuxf_set(int v) { option_set("gpu_transform", v ? "on" : "off"); d3d_gpu_transform_set(v); }

static int native_get(void) { return !strcasecmp(option_str("renderer", "d3d7"), "native"); }
static void native_set(int v) { option_set("renderer", v ? "native" : "d3d7"); d3d_native_set(v); }

static int steps_get(void) { return strcasecmp(option_str("frame_steps", "30"), "off") != 0; }
static void steps_set(int v) { option_set("frame_steps", v ? "30" : "off"); framerate_steps_set(v); }

/* the camera: 0..1200 (600 by default) as 0..100 */
static int cam_get(void) { int s = atoi(option_str("camera_speed", "600")); s = s < 0 ? 0 : s > 1200 ? 1200 : s; return (s + 60) / 120 * 10; }
static void cam_set(int v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", v * 12);
    option_set("camera_speed", b);
    pad_camera_speed_set(v * 12);
}

/* the frame limit: on = auto (the screen's refresh), off = none; a number in options.txt shows as on */
static int limit_get(void) { return strcasecmp(option_str("frame_limit", "auto"), "off") != 0; }
static void limit_set(int v) { option_set("frame_limit", v ? "auto" : "off"); frame_limit_options(); vk_options(); }

/* the left stick's dead zone: 0..20 % as 0..100 (6 % by default) */
static int dz_get(void)
{
    double p = atof(option_str("controller_deadzone", "6"));
    int v = (int)(p * 5 / 10 + 0.5) * 10;
    return v < 0 ? 0 : v > 100 ? 100 : v;
}
static void dz_set(int v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", v / 5);
    option_set("controller_deadzone", b);
    pad_deadzone_set(v / 5.0);
}

/* CAMERA TILT: how much of the game's own downward tilt the camera keeps, 0..100 % (game.c) */
static int tilt_get(void) { int v = atoi(option_str("camera_tilt", "100")); v = v < 0 ? 0 : v > 100 ? 100 : v; return (v + 5) / 10 * 10; }
static void tilt_set(int v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    option_set("camera_tilt", b);
    camera_tilt_set(v);
}

/* PROMPTS (prompts.c): the on-screen button prompts, their look and which kinds */
static int prompts_get(void) { return option_bool("prompts", 0); }
static void prompts_set(int v) { option_set("prompts", v ? "on" : "off"); }
static int pstyle_get(void) { return !strcasecmp(option_str("prompt_style", "game"), "lego"); }
static void pstyle_set(int v) { option_set("prompt_style", v ? "lego" : "game"); }
static int pkeys_get(void) { return option_bool("prompt_keys", 1); }
static void pkeys_set(int v) { option_set("prompt_keys", v ? "on" : "off"); }
static int pmenus_get(void) { return option_bool("prompt_menus", 1); }
static void pmenus_set(int v) { option_set("prompt_menus", v ? "on" : "off"); }
static int pdlg_get(void) { return option_bool("prompt_dialogue", 1); }
static void pdlg_set(int v) { option_set("prompt_dialogue", v ? "on" : "off"); }
static int plegend_get(void) { return option_bool("prompt_legend", 1); }
static void plegend_set(int v) { option_set("prompt_legend", v ? "on" : "off"); }

static int rumble_get(void) { return option_bool("rumble", 1); }
static void rumble_set(int v) { option_set("rumble", v ? "on" : "off"); win_rumble_set(v); }

static int intro_get(void) { return option_bool("intro_movies", 1); }
static void intro_set(int v) { option_set("intro_movies", v ? "on" : "off"); }

static const Row rows[] = {
    {10, "FOG", T_TOGGLE, fog_get, fog_set},
    {10, "DISPLAY", T_SCREEN, NULL, NULL, 29},     /* opens our screen 29 */
    {29, "VSYNC", T_TOGGLE, vsync_get, vsync_set},
    {29, "FRAME LIMIT", T_TOGGLE, limit_get, limit_set},
    {29, "ANTIALIASING", T_TOGGLE, aa_get, aa_set},
    {29, "FRAME RATE FIX", T_TOGGLE, steps_get, steps_set},
    {29, "GPU VERTICES", T_TOGGLE, gpuxf_get, gpuxf_set},
    {29, "NATIVE RENDERER", T_TOGGLE, native_get, native_set},
    {9, "CAMERA", T_SCREEN, NULL, NULL, 30},       /* opens our screen 30 */
    {9, "CONTROLLER", T_SCREEN, NULL, NULL, 31},
    {9, "PROMPTS", T_SCREEN, NULL, NULL, 32},
    {31, "DEAD ZONE", T_SLIDER, dz_get, dz_set},
    {31, "VIBRATION", T_TOGGLE, rumble_get, rumble_set},
    {32, "BUTTON PROMPTS", T_TOGGLE, prompts_get, prompts_set},
    {32, "PLASTIC STUDS", T_TOGGLE, pstyle_get, pstyle_set},
    {32, "KEYBOARD KEYS", T_TOGGLE, pkeys_get, pkeys_set},
    {32, "MENU PROMPTS", T_TOGGLE, pmenus_get, pmenus_set},
    {32, "DIALOGUE PROMPTS", T_TOGGLE, pdlg_get, pdlg_set},
    {32, "CONTROLS LEGEND", T_TOGGLE, plegend_get, plegend_set},
    {12, "INTRO", T_TOGGLE, intro_get, intro_set},          /* (the start-up movies; a narrow panel) */
    {30, "CAMERA SPEED", T_SLIDER, cam_get, cam_set},
    {30, "CAMERA TILT", T_SLIDER, tilt_get, tilt_set},
};
#define NROWS (int)(sizeof rows / sizeof *rows)

/* each screen with rows of ours: its first one (row numbers count from 1, as CUR_ROW) and table */
static struct { int screen, first, n; uint32_t table; } ext[8];
static int next;

#define SCREEN_PTR  0x9744c4u           /* the screen table [verified: 0x86c826.., 0x4187d5's +0x64] */
#define NSCREENS    29                  /* 0..28 [verified: the next 0x14 bytes are the state, 0x974460] */
#define DISPLAY     29                  /* ours: Graphics' DISPLAY row opens it */
#define CAMERA      30                  /* ours: Controls' CAMERA row opens it */
#define CONTROLLER  31                  /* ours: Controls' CONTROLLER row */
#define PROMPTS     32                  /* ours: Controls' PROMPTS row */
static uint32_t screen_desc(int s) { return R32(SCREEN_PTR) + (uint32_t)s * 0x14; }

static void put_row(uint32_t r, const Row *row)
{
    memset(gptr(r), 0, ROW_SIZE);
    snprintf((char *)gptr(r), 0x3c, "%s", row->label);
    W32(r + 0x40, (uint32_t)row->type);
    W32(r + 0x44, (uint32_t)(row->type == T_SCREEN ? row->opens : row->get()));
}

/* screen s: its rows copied with ours inserted before BACK (the first row of type 2) */
static void extend(int s)
{
    uint32_t d = screen_desc(s), old = R32(d + 8);
    int n = (int)R32(d + 4), add = 0, back = -1;   /* (n: the rows, the title as row 0) */
    for (int i = 0; i < NROWS; i++)
        add += rows[i].screen == s;
    for (int k = 1; old && k < n && back < 0; k++)
        if (R32(old + (uint32_t)k * ROW_SIZE + 0x40) == 2)
            back = k;
    if (!old || n < 2 || n > 20 || !add || back < 0 || next >= 8)
        return;
    uint32_t t = galloc((uint32_t)(n + add) * ROW_SIZE);
    memcpy(gptr(t), gptr(old), (size_t)back * ROW_SIZE);
    memcpy(gptr(t + (uint32_t)(back + add) * ROW_SIZE), gptr(old + (uint32_t)back * ROW_SIZE),
           (size_t)(n - back) * ROW_SIZE);
    int k = 0;
    for (int i = 0; i < NROWS; i++)
        if (rows[i].screen == s)
            put_row(t + (uint32_t)(back + k++) * ROW_SIZE, &rows[i]);
    W32(d + 8, t);
    W32(d + 4, (uint32_t)(n + add));
    ext[next].screen = s;
    ext[next].first = back;
    ext[next].n = add;
    ext[next].table = t;
    next++;
}

/* screen s of ours (in the copied table): its title, our rows, the BACK of screen `parent` (the
   one whose row opens it) */
static void add_screen(int s, int parent, const char *name)
{
    uint32_t g = screen_desc(parent), grows = R32(g + 8);
    int add = 0, gn = (int)R32(g + 4), gback = -1;
    for (int k = 1; k < gn && gback < 0; k++)
        if (R32(grows + (uint32_t)k * ROW_SIZE + 0x40) == 2)
            gback = k;
    for (int i = 0; i < NROWS; i++)
        add += rows[i].screen == s;
    if (gback < 0 || !add)
        return;
    uint32_t r = galloc((uint32_t)(add + 2) * ROW_SIZE);
    memset(gptr(r), 0, ROW_SIZE);
    snprintf((char *)gptr(r), 0x3c, "%s", name);
    int k = 1;
    for (int i = 0; i < NROWS; i++)
        if (rows[i].screen == s)
            put_row(r + (uint32_t)k++ * ROW_SIZE, &rows[i]);
    memcpy(gptr(r + (uint32_t)k * ROW_SIZE), gptr(grows + (uint32_t)gback * ROW_SIZE), ROW_SIZE);   /* BACK */
    uint32_t d = screen_desc(s);
    W32(d, R32(g));                     /* (the parent's kind) */
    W32(d + 4, (uint32_t)(add + 2));
    W32(d + 8, r);
    W32(d + 0xc, 0);
    W32(d + 0x10, R32(g + 0x10));
    ext[next].screen = s;
    ext[next].first = 1;
    ext[next].n = add;
    ext[next].table = r;
    next++;
}

static const Row *row_at(int s, int r)
{
    for (int e = 0; e < next; e++)
        if (ext[e].screen == s && r >= ext[e].first && r < ext[e].first + ext[e].n) {
            int k = r - ext[e].first;
            for (int i = 0; i < NROWS; i++)
                if (rows[i].screen == s && k-- == 0)
                    return &rows[i];
        }
    return NULL;
}

/* 0x86c174 (cdecl (type, value, x) -> 0 to stay): a change; ours when on one of our rows */
static int hook_menu_change_fn(CPU *c)
{
    const Row *row = row_at((int)R32(CUR_SCREEN), (int)R32(CUR_ROW));
    if (!row || row->type == T_SCREEN)
        return 0;                       /* (opening a screen: the engine's own) */
    /* the engine (0x4187d5) calls this every frame with what happened: 5 a toggle just flipped,
       6 a slider just moved [verified: its switch on 0x418514's result]; otherwise the value is
       stale, and the game's own handler does nothing for our rows */
    if ((int)R32(c->esp + 4) != row->type) {
        c->eax = 0;
        c->ret = R32(c->esp);
        c->esp += 4;
        return 1;
    }
    int v = (int)R32(c->esp + 8);
    if (row->type == T_TOGGLE)
        v = v != 0;
    v = v < 0 ? 0 : v > 100 ? 100 : v;
    row->set(v);
    options_save();
    LOG(LOG_ALWAYS, "menu: %s = %d", row->label, v);
    c->eax = 0;
    c->ret = R32(c->esp);
    c->esp += 4;
    return 1;
}

/* 0x86c815 (no arguments): the rows' values from the settings; then ours from the options */
static int hook_menu_load_fn(CPU *c)
{
    F_0086c815_orig(c);
    for (int e = 0; e < next; e++)
        for (int k = 0; k < ext[e].n; k++) {
            const Row *row = row_at(ext[e].screen, ext[e].first + k);
            if (row && row->type != T_SCREEN)
                W32(ext[e].table + (uint32_t)(ext[e].first + k) * ROW_SIZE + 0x44, (uint32_t)row->get());
        }
    return 1;
}

/* the screen table copied with four more screens of ours: DISPLAY (from Graphics), CAMERA,
   CONTROLLER and PROMPTS (from Controls) */
static void add_screens(void)
{
    uint32_t base = R32(SCREEN_PTR);
    if (!base)
        return;
    uint32_t t = galloc((NSCREENS + 4) * 0x14);
    memcpy(gptr(t), gptr(base), NSCREENS * 0x14);
    W32(SCREEN_PTR, t);
    add_screen(DISPLAY, 10, "DISPLAY");
    add_screen(CAMERA, 9, "CAMERA");
    add_screen(CONTROLLER, 9, "CONTROLLER");
    add_screen(PROMPTS, 9, "PROMPTS");
}

void menu_init(void)
{
    add_screens();
    extend(10);
    extend(9);
    extend(12);
    hook_menu_change = hook_menu_change_fn;
    hook_menu_load = hook_menu_load_fn;
    LOG(LOG_ALWAYS, "menu: %d rows added to the Options screens", NROWS);
}
