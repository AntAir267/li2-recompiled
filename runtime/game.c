#define _GNU_SOURCE
/* LEGO Island 2: what the runtime does to the game itself (hooks into the translated code, per-frame
   work). Nothing yet: the game runs as shipped. */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "funcs.h"
#include "options.h"
#include <strings.h>

#define GAME_DIR_VAR 0x975d48u      /* the game's folder from its command line, "C:\LEGO Island 2\" */
#define MOVIE_DIR_VAR 0x9764e4u     /* the folder the intro movies are played from */

/* The CD check (0x4083d0): the game looks for a CD drive holding LEGOMEDIA and loops on its
   "insert the CD" screen until there is one. Answered as the community no-CD patch makes it (its
   jne at 0x40842c turned into a jump, the je at 0x408478 removed): the CD counts as found on C:,
   and the movies are looked for in the game's folder: fmv\, then Lego Island 2\Movies\ (the
   patch's own two places), and also CD\Lego Island 2\Movies\ (a copy of the CD beside the game). */
static int hook_cd_check_fn(CPU *c)
{
    /* (the game's folder variable holds "C:\LEGO Island 2\_Data\" by now: the patch's fmv\ is
       _Data\fmv\; root is the game's folder itself) */
    /* (where: 0 the folder variable, 1 the drive's root as the patch's fallback, 2 the game's folder) */
    static const struct { int where; const char *sub; } dirs[] = {
        {0, "fmv\\"}, {1, "Lego Island 2\\Movies\\"}, {2, "CD\\Lego Island 2\\Movies\\"}};
    char base[260], root[260], dir[400], probe[500], host[1200];
    snprintf(base, sizeof base, "%s", gstr(GAME_DIR_VAR));
    snprintf(root, sizeof root, "%s", base);
    size_t rn = strlen(root);
    if (rn >= 6 && !strcasecmp(root + rn - 6, "_Data\\"))
        root[rn - 6] = 0;
    W8(MOVIE_DIR_VAR, 0);
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++) {
        snprintf(dir, sizeof dir, "%s%s", dirs[i].where == 0 ? base : dirs[i].where == 1 ? "C:\\" : root, dirs[i].sub);
        snprintf(probe, sizeof probe, "%sLegoSoftware.mpg", dir);
        if (path_to_host(probe, host, sizeof host, 0)) {
            gstrncpy(MOVIE_DIR_VAR, dir, 128);
            break;
        }
    }
    LOG(LOG_ALWAYS, "CD check: as the no-CD patch (found on C:; game folder \"%s\"); movies from \"%s\"", base,
        R8(MOVIE_DIR_VAR) ? gstr(MOVIE_DIR_VAR) : "(none)");
    c->eax = 2;                         /* drive C: */
    c->ret = R32(c->esp);               /* return at once (no arguments) */
    c->esp += 4;
    return 1;
}

/* ------------------------------------------------------------------ the playing size
   The game lists the display modes and plays at the first its filter (0x405270) accepts: of 640x480,
   800x600, 1024x768 and 1280x1024, so always 640x480 (its setup dialog that wrote GameSetup.txt is
   not reached in this build). Option resolution = native (default: the screen's own size, as the
   community widescreen patch puts in the filter's first slot) | original (the game's choice) |
   WxH. DirectDraw lists that size first and the filter takes it alone; the projection's aspect
   (the 0.75 at 0x40a494) becomes the size's height / width, so a wide screen sees more at the
   sides (Hor+). */
extern uint32_t ddraw_first_mode[2];
void host_desktop_size(int *w, int *h);
static int play_w, play_h;              /* 0: the game's own choice */

static void choose_size(void)
{
    const char *r = option_str("resolution", "native");
    int w = 0, h = 0;
    if (!strcasecmp(r, "native"))
        host_desktop_size(&w, &h);
    else if (sscanf(r, "%dx%d", &w, &h) != 2 || w < 640 || h < 480)
        w = h = 0;
    play_w = w;
    play_h = h;
    ddraw_first_mode[0] = (uint32_t)w;
    ddraw_first_mode[1] = (uint32_t)h;
    if (w && h) {
        float a = (float)h / (float)w;
        memcpy(&rc_aspect, &a, 4);
    }
    if (!w)
        LOG(LOG_ALWAYS, "playing size: the game's own (640x480)");
    else
        LOG(LOG_ALWAYS, "playing size: %dx%d (resolution = %s), aspect %.4f", w, h, r, (double)h / w);
}

/* (mode record): 1 if the game may use it */
static int hook_mode_filter_fn(CPU *c)
{
    if (!play_w)
        return 0;                       /* the game's own filter */
    uint32_t m = R32(c->esp + 4);
    uint32_t want_bpp = R32(0x976070u);
    c->eax = R32(m + 0x54) == want_bpp && R32(m + 0xc) == (uint32_t)play_w && R32(m + 8) == (uint32_t)play_h;
    c->ret = R32(c->esp);               /* return at once (cdecl) */
    c->esp += 4;
    return 1;
}

/* The front end (state 0, 0x7a87b3: zone z00FRE) shows its menus over a 3D set built for 4:3:
   wider than that, its edges show (white past the set, props waiting off-stage). While it runs,
   the projection is zoomed so the set fills the width as at 4:3 (a little is cut at the top and
   bottom); the game itself stays Hor+. */
extern float d3d_proj_zoom;
static int in_frontend;
static int hook_frontend_fn(CPU *c)
{
    /* the front end keeps its 4:3 picture: drawn with the game's own 4:3 projection and narrowed
       into the middle of the screen (vk_squeeze), black bars at the sides. (Widened, its menu
       pointer and the set's framing went wrong.) */
    extern float vk_squeeze;
    float k = play_w && play_h ? ((float)play_w / (float)play_h) / (4.0f / 3.0f) : 1.0f;
    uint32_t aspect = rc_aspect;
    rc_aspect = 0x3f400000u;            /* 0.75 */
    vk_squeeze = k > 1.0f ? 1.0f / k : 0.0f;
    in_frontend = 1;
    F_007a87b3_orig(c);
    in_frontend = 0;
    vk_squeeze = 0.0f;
    rc_aspect = aspect;
    return 1;
}

/* ------------------------------------------------------------------ the controller in menus
   The game's menus confirm with Return (0x10000) and go back with Escape (0x40000000); its
   joystick buttons are the actions (pad.c: A is ACTION 2, jump; B ACTION 3, talk). While a menu is
   up (the front end, or an area's pause menu: its state 3), the controller's A is Return and B is
   Escape, as on a console. The keyboard is as shipped. */
#define JOY_BUTTONS 0x9ccec8u           /* the joystick's DIJOYSTATE2 buttons (0x80 = down) */
static int area_paused;

static unsigned long paused_frame = ~0ul;  /* (the pause menu last seen up: prompt_context) */
static int hook_area_input_fn(CPU *c)
{
    uint32_t st = R32(c->esp + 4);
    area_paused = st && R32(st) == 3;
    if (area_paused)
        paused_frame = frame_count;
    F_00876480_orig(c);
    area_paused = 0;
    return 1;
}

/* On the skateboard (its steering, 0x7eaf01, ran this frame or the last) the controller follows
   Tony Hawk's layout: in the air the game takes a trick from the button held (0x7e8a40) and the
   stick's direction: ACTION 1 (Ctrl: Y) freestyle tricks, ACTION 3 (Shift) flips and saltos,
   ACTION 4 (Space) grabs. So while riding X is Shift (flip) and B is Space (grab); A jumps and Y is
   the board, as on foot. */
static unsigned long riding_frame = ~0ul;
static int hook_board_steer_fn(CPU *c)
{
    (void)c;
    riding_frame = frame_count;
    return 0;
}
static int riding(void) { return riding_frame != ~0ul && frame_count - riding_frame <= 1; }

/* Dialogue: the game ends a line on the talk action (ACTION 3: Left Shift, the controller's B)
   [verified: tests/dialogue_probe.sh: of Space, Return, Escape, Tab, Left Ctrl, Left Shift and
   the four buttons, only Left Shift and B end the first line of the town's opening early].
   While a line plays in a cutscene (a speech stream, mss32.c; the game's own test 0x441102:
   0x5d3fd5c set or 0x5d399d8 = 1), Space and Return are the talk action too. */
int mss_speech_playing(void);
static int in_dialogue(void)
{
    return (R32(0x5d3fd5cu) || R8(0x5d399d8u) == 1) && mss_speech_playing();
}

static int hook_input_read_fn(CPU *c)
{
    uint32_t out = R32(c->esp + 8);
    F_0041f9c9_orig(c);
    uint32_t w = c->eax;
    if ((w & (0x80u | 0x10000u)) && !in_frontend && !area_paused && in_dialogue()) {
        w |= 0x40u;                     /* (Space is ACTION 4, Return the toy: talk as well) */
        c->eax = w;
        if (out)
            W32(out, w);
    }
    if (!R8(0x9cd0e4u) || !R32(0x89e18cu))
        return 1;                       /* (no joystick read) */
    if (in_frontend || area_paused) {
        if (R8(JOY_BUTTONS + 1) & 0x80)     /* A */
            w = (w & ~0x20u) | 0x10000u;
        if (R8(JOY_BUTTONS + 2) & 0x80)     /* B */
            w = (w & ~0x40u) | 0x40000000u;
    } else if (riding()) {
        int b = R8(JOY_BUTTONS + 2) & 0x80, x = R8(JOY_BUTTONS + 3) & 0x80;
        if (b || x)
            w = (w & ~0xc0u) | (b ? 0x80u : 0) | (x ? 0x40u : 0);
    } else
        return 1;
    c->eax = w;
    if (out)
        W32(out, w);
    return 1;
}

/* Loading (0x42a870): the game draws and presents its loading screen after every item it loads;
   presenting waits for the screen's refresh, so loading took a refresh per item. With fast_loading
   (default on) the screen is drawn again only once 33 ms have passed (as the community load-time
   patch's 40 ms); test runs keep every frame (they count frames, not time). */
double host_ms(void);
static unsigned long loading_frame = ~0ul; /* (a loading screen last drawn: prompt_context) */
static int hook_loading_frame_fn(CPU *c)
{
    static double last;
    loading_frame = frame_count;
    static int on = -1;
    if (on < 0)
        on = option_bool("fast_loading", 1);
    if (!on || deterministic)
        return 0;
    double t = host_ms();
    if (t - last < 33.0) {
        c->ret = R32(c->esp);           /* not this time */
        c->esp += 4;
        return 1;
    }
    last = t;
    return 0;
}

/* ------------------------------------------------------------------ button prompts
   The minigames' control panels (0x444c21: "CONTROL<n>" texts, "BUTTON_4 Fire Bananas") name the
   key bound to each input bit (0x4205c1). When the controller was used last (and the game's
   joystick is on) the action buttons are named by the controller's instead: joystick button n
   sets bit 0x10, 0x20, 0x40, 0x80, 0x1000 ... (0x41f9c9), and pad.c numbers the Xbox buttons
   (Y A B X, RS LB RB LS, BACK). The directions keep their names (the stick or the D-pad). */
static int hook_key_name_fn(CPU *c)
{
    static const struct { uint32_t bit; const char *name; } pads[] = {
        {0x10, "Y"}, {0x20, "A"}, {0x40, "B"}, {0x80, "X"}, {0x1000, "RS"}, {0x2000, "LB"},
        {0x4000, "RB"}, {0x8000, "LS"}, {0x10000, "BACK"}};
    static uint32_t names;              /* the names in guest memory, 8 bytes each */
    extern int input_controller_last(void);
    if (!input_controller_last() || R32(0x975a40 + 0x13 * 4) != 1)
        return 0;
    uint32_t bit = R32(c->esp + 4);
    for (size_t i = 0; i < sizeof pads / sizeof *pads; i++)
        if (pads[i].bit == bit) {
            if (!names) {
                names = vm_alloc(0, 0x10000, 0);
                for (size_t k = 0; k < sizeof pads / sizeof *pads; k++)
                    for (size_t j = 0; j <= strlen(pads[k].name); j++)
                        W8(names + 8 * k + j, (uint8_t)pads[k].name[j]);
            }
            c->eax = names + 8 * i;
            c->ret = R32(c->esp);
            c->esp += 4;
            return 1;
        }
    return 0;
}

/* ------------------------------------------------------------------ animation log (development)
   LI2_ANIM_LOG=1: each animation started (0x4232a3: (model, animation, flags)) is logged with the
   frame and the translated functions it was called from */
#include <dlfcn.h>
#include <execinfo.h>
static int hook_anim_play_fn(CPU *c)
{
    char chain[200] = "";
    void *bt[24];
    int nb = backtrace(bt, 24), k = 0;
    for (int i = 0; i < nb && k < 180; i++) {
        Dl_info di;
        if (dladdr(bt[i], &di) && di.dli_sname && di.dli_sname[0] == 'F' && di.dli_sname[1] == '_')
            k += snprintf(chain + k, sizeof chain - (size_t)k, " %s", di.dli_sname + 4);
    }
    LOG(LOG_ALWAYS, "anim: frame %lu model %08x anim %d flags %x |%s", frame_count, R32(c->esp + 4),
        (int)R32(c->esp + 8), R32(c->esp + 12), chain);
    return 0;
}

/* ------------------------------------------------------------------ the draw distance
   Each area sets its near, farthest and fog distances (0x429d30 -> 0x407f30); 0x407e00 puts the far
   plane between near and farthest by Settings.set's detail slider (setting 21, 0x975a94: 50 by
   default, halfway; the set-up dialog that changed it is unreachable). The far plane (0x8929b0) is
   also the frustum's for culling and sizes the sky dome and the sea. Option draw_distance: far
   (default: the area's farthest, the slider at its top), game (the slider), or a number: that many
   times the area's farthest. The fog (when on) moves out with it. */
static double draw_distance(void)
{
    static double k = -1;
    if (k < 0) {
        const char *v = option_str("draw_distance", "game");
        k = !strcmp(v, "game") ? 0 : !strcmp(v, "far") ? 1 : atof(v);
        if (k < 0 || k > 8)
            k = 1;
    }
    return k;
}

static int hook_far_plane_fn(CPU *c)
{
    double k = draw_distance();
    if (k == 0)
        return 0;
    uint32_t slider = R32(0x975a94u), far = R32(0x976448u), fog = R32(0x97644cu);
    W32(0x975a94u, 0);
    WF32(0x976448u, (float)(RF32(0x976448u) * k));
    WF32(0x97644cu, (float)(RF32(0x97644cu) * k));
    F_00407e00_orig(c);
    W32(0x975a94u, slider);
    W32(0x976448u, far);
    W32(0x97644cu, fog);
    LOG(LOG_ALWAYS, "draw distance %.0f (the area's %.0f to %.0f)", RF32(0x8929b0u), RF32(0x976444u),
        RF32(0x976448u));
    return 1;
}

/* ------------------------------------------------------------------ the camera's tilt
   CAMERA TILT (Controls > CAMERA; options.txt camera_tilt, percent of the game's own downward
   tilt kept: 100 as shipped, the default). The view is a look-at (0x4109e0, cdecl (out, eye,
   target, up): the view matrix, and the culling volume from it, 0x4100c0) of the frame's camera
   (0x40a3a0: the globals 0x979468 eye / 0x9793c8 target) and of the player's camera object
   (0x424c9c with 0x9ccb28: eye, target at +0xc; the view it builds is the one the frame's draws
   use, and its rows go back into the object for moving relative to the camera) [verified: a probe
   of 0x4109e0's callers in the town and two zones: those two, the camera 10-15 degrees above its
   target]. Below 100 the eye is lowered round the target, its distance kept, to that share of
   its angle above the target: a flatter view with Pepper where he was on the screen. Not in the
   front end nor in cutscenes (the game's own test 0x441102: 0x5d3fd5c, 0x5d399d8), nor for a
   camera looking up or straight down. */
static int tilt_pct = -1;
void camera_tilt_set(int percent) { tilt_pct = percent < 0 ? 0 : percent > 100 ? 100 : percent; }

static int hook_lookat_fn(CPU *c)
{
    if (tilt_pct < 0)
        camera_tilt_set(atoi(option_str("camera_tilt", "100")));
    uint32_t e = c->esp, eye = R32(e + 8), at = R32(e + 12);
    if (tilt_pct >= 100 || in_frontend || R32(0x5d3fd5cu) || R8(0x5d399d8u) == 1 ||
        (eye != 0x979468u && eye != 0x9ccb28u))
        return 0;
    double dx = RF32(eye) - RF32(at), dy = RF32(eye + 4) - RF32(at + 4), dz = RF32(eye + 8) - RF32(at + 8);
    double h = sqrt(dx * dx + dz * dz), r = sqrt(h * h + dy * dy);
    if (dy <= 0 || h < 1e-3)
        return 0;
    static uint32_t moved;              /* (the lowered eye, for the original to build from) */
    if (!moved)
        moved = galloc(16);
    double a = atan2(dy, h) * tilt_pct / 100.0, k = r * cos(a) / h;
    WF32(moved, (float)(RF32(at) + dx * k));
    WF32(moved + 4, (float)(RF32(at + 4) + r * sin(a)));
    WF32(moved + 8, (float)(RF32(at + 8) + dz * k));
    W32(e + 8, moved);
    F_004109e0_orig(c);
    W32(e + 8, eye);                    /* (the caller's argument as it was) */
    return 1;
}

/* ------------------------------------------------------------------ what the prompts show
   prompts.c: the menus (the front end, an area's pause menu), a line of a cutscene's dialogue,
   Pepper on the skateboard, Pepper on foot in an island area (the zone table 0x88f8b0's type, +4,
   is 1 for the front end and the z09 areas, Pepper's house, the pizzeria...; 2-4 the minigames
   and races [verified: the table read from the executable]; the zone is flag 0x59, 0x896c90), or
   nothing (a movie, a loading screen, a cutscene's other moments, a minigame). */
int video_playing(void);
enum { PC_NONE, PC_MENU, PC_DIALOGUE, PC_FOOT, PC_BOARD };
static int recent(unsigned long f) { return f != ~0ul && frame_count - f <= 2; }
int prompt_context(void)
{
    if (video_playing() || recent(loading_frame))
        return PC_NONE;
    if (in_frontend || recent(paused_frame))
        return PC_MENU;
    if (in_dialogue())
        return PC_DIALOGUE;
    if (R32(0x5d3fd5cu) || R8(0x5d399d8u) == 1)
        return PC_NONE;
    if (riding())
        return PC_BOARD;
    uint32_t zone = R32(0x896c90u + 0x59 * 4);
    if (zone && zone < 35 && R32(0x88f8b4u + zone * 0x14) == 1)
        return PC_FOOT;
    return PC_NONE;
}

/* ------------------------------------------------------------------ an uninitialized local
   The collision sweep 0x428ab0 (cdecl (object, ray, flag); its last hit triangle cached at
   0x9d0c68) adds a local float it never wrote to the ray's length before its first test
   (fadd [esp + 0xa8] after the length's fsqrt: the slot's first store is far later) [verified: the
   instructions]: whatever an earlier call left on the stack. In the town about half the calls
   find 0 there, a third a small number, a tenth something huge (then the first test always
   passes) [verified: a probe of the slot, 208321 calls]. And when its sub-tests decide
   (0x427ca0 / 0x427da0 / 0x427ed0 / 0x428080) it stores another unwritten local (entry - 0x64)
   as the cached triangle, which its next call tests against [verified: decompile; zone 2's run
   with the native renderer had a stale heap pointer there at frame 9660, the Direct3D 7 path's
   stack 0]. Both were the stack's on Windows too (Direct3D's own leftovers); here they are 0
   (the first one's most common value; no cached triangle), so that the collisions do not depend
   on what other code (the native renderer's, in particular) leaves on the stack. Option
   collision_fix = off (options.txt only) leaves them as they were. */
static int hook_coll_fn(CPU *c)
{
    W32(c->esp - 0x5c, 0);              /* (the slots, in the frame the function is about to make) */
    W32(c->esp - 0x64, 0);
    return 0;
}

/* ------------------------------------------------------------------ the frame rate (framerate.c) */
uint32_t framerate_tick_frame(void);
void framerate_clock(uint32_t frame);
int framerate_effect_tracks(CPU *c, void (*orig)(CPU *));

static uint32_t consts;                 /* constants the translated code is pointed at (game_init) */

static int hook_game_clock_fn(CPU *c)
{
    uint32_t frame = R32(c->esp + 4);   /* (cdecl (frame number 0x9ccb0c): the callers push it) */
    F_0042aa40_orig(c);
    framerate_clock(frame);
    if (consts) {                       /* 1 / clock, for the counts made in frames (MEM_OVERRIDES inv_clock) */
        float clk = RF32(0x8a0850u);
        WF32(consts + 8, clk > 1.0f / 16 ? 1.0f / clk : 16.0f);
        WF32(consts + 12, 15.0f * RF32(consts + 8));   /* the credits' 15 frames (MEM_OVERRIDES credits_15) */
    }
    return 1;
}

static int hook_effect_tracks_fn(CPU *c) { return framerate_effect_tracks(c, F_00439450_orig); }

static void framerate_init(void)
{
    uint32_t t = framerate_tick_frame();
    uint32_t *reads[] = {&rm_tick_frame_43947b, &rm_tick_frame_43c092, &rm_tick_frame_43c125,
                         &rm_tick_frame_43c1c5, &rm_tick_frame_43c4e5, &rm_tick_frame_43c590,
                         &rm_tick_frame_43c5d5, &rm_tick_frame_43c6c0, &rm_tick_frame_43c705,
                         &rm_tick_frame_43c810, &rm_tick_frame_43c833, &rm_tick_frame_43c8a5,
                         &rm_tick_frame_43ca00, &rm_tick_frame_43ca95, &rm_tick_frame_43cbd5};
    for (size_t i = 0; i < sizeof reads / sizeof *reads; i++)
        *reads[i] = t;
    hook_game_clock = hook_game_clock_fn;
    hook_effect_tracks = hook_effect_tracks_fn;
    hook_key_name = hook_key_name_fn;
    hook_board_steer = hook_board_steer_fn;
    hook_far_plane = hook_far_plane_fn;
    if (option_bool("collision_fix", 1))
        hook_coll = hook_coll_fn;
    hook_lookat = hook_lookat_fn;
    if (getenv("LI2_ANIM_LOG"))
        hook_anim_play = hook_anim_play_fn;
}

void menu_init(void);
void skin_init(void);
void native_init(void);
void prompts_init(void);
void game_init(void)
{
    framerate_init();
    menu_init();
    skin_init();
    native_init();
    prompts_init();
    hook_input_read = hook_input_read_fn;
    hook_area_input = hook_area_input_fn;
    hook_loading_frame = hook_loading_frame_fn;
    hook_frontend = hook_frontend_fn;
    hook_cd_check = hook_cd_check_fn;
    hook_mode_filter = hook_mode_filter_fn;
    choose_size();
    consts = vm_alloc(0, 0x10000, 0);
    WF32(consts, 1.0f);                 /* the clock as at 30 fps: the Station Master's arrival radius, */
    rm_arrive_clock_4d51f2 = consts;    /* Pepper's turn test */
    rm_unit_clock_8724a5 = rm_unit_clock_8724c4 = consts;
    rm_unit_clock_53655a = rm_unit_clock_53673e = consts;   /* the countdown, the throw's sweep, */
    rm_unit_clock_87497b = rm_unit_clock_87498d = rm_unit_clock_87499f = consts;
    rm_unit_clock_882bc1 = consts;      /* the development camera (builds/en_v2.py) */
    WF32(consts + 8, 1.0f);             /* 1 / clock (hook_game_clock_fn) */
    rm_inv_clock_526c4d = rm_inv_clock_7c75c1 = consts + 8;
    WF32(consts + 12, 15.0f);
    rm_credits_15_7a9cce = rm_credits_15_7a9cf3 = consts + 12;
    if (play_w && play_h) {             /* the culling volume's sides as wide as the view */
        double k = (double)play_w / play_h / (4.0 / 3.0);
        WF32(consts + 4, (float)(0.7 * (k > 1 ? k : 1)));
        rm_cull_side_41016c = rm_cull_side_410176 = rm_cull_side_410184 = consts + 4;
    }
    if (play_w && play_h) {             /* the menus' mouse pointer as at 640 x 480 */
        rc_cursor_w = (uint32_t)(40 * play_w / 640);
        rc_cursor_h = (uint32_t)(40 * play_h / 480);
        rc_cursor_sw = (uint32_t)(44 * play_w / 640);
        rc_cursor_sh = (uint32_t)(44 * play_h / 480);
        rc_cursor_dx = (uint32_t)(4 * play_w / 640);
        rc_cursor_dy = (uint32_t)(4 * play_h / 480);
    }
}

/* A controller plugged in after start-up: the game looks for a joystick once (0x41f0b2, from its
   input set-up 0x41f24d), so it is asked to look again when one appears and it has none. One
   unplugged stays the game's device and comes back when plugged in again (dinput.c keeps its slot). */
static void joystick_hotplug(void)
{
    extern int pad_count(void);
    static int last;
    int n = pad_count();
    if (n == last)
        return;
    last = n;
    CPU *c = current_cpu();
    if (n > 0 && c && R32(0x9ccfbcu) && !R32(0x9cd0dcu)) {
        guest_call(c, 0x41f0b2u, 1, R32(0x975ba0u));
        LOG(LOG_ALWAYS, "controller: the game took it as its joystick (%s)", R32(0x9cd0dcu) ? "yes" : "no");
    }
}

void game_frame(unsigned long frame)
{
    (void)frame;
    joystick_hotplug();
    extern void mss_frame(void);
    mss_frame();                        /* Miles: stream callbacks, 3D gains */
}

/* pad.c asks whether the player is driving (for its own button handling): not known yet */
int game_driving(void) { return 0; }
/* win.c: the screen's refresh changed (a frame rate that follows it) */
void frame_rate_reapply(void) {}
/* pad.c: the game's own use of the controller, once a frame */
void padgame_frame(unsigned long frame) { (void)frame; }
/* force feedback: the game has none, so no rumble is asked for */
int ifc_rumble_wanted(void) { return 0; }
void ifc_frame(unsigned long frame) { (void)frame; }
