/* LEGO Island 2's side of the AntiStatic runtime's hooks (port.h): start-up, frames, COM, and the
   controller's layout.

   THE CONTROLLER. LEGO Island 2 reads one joystick (0x41f9c9; on when Options > Controls >
   Joystick is, the default): X and Y past half way are its four directions, as the arrow keys;
   buttons 1-10 are fixed to the keyboard's actions: 1 ACTION 1 (Left Ctrl: skateboard, horse,
   vehicle), 2 ACTION 2 (Left Alt: jump), 3 ACTION 3 (Left Shift: talk, doors, hold for the
   Infomaniac), 4 ACTION 4 (Space: throw pizza, gallop), 5-8 ACTION 5-8 (< > Z X C), 9 Return (the
   toy), 10 none. Escape (the pause menu, back in menus) is keyboard only, and the camera is the
   mouse's. So the toolkit's controllers (pad.c: every controller merged into the game's one
   joystick, the sticks' round dead zone) go to the game this way (port_pad_send), the Xbox layout
   playing naturally:
     A jump, B talk, X throw, Y skateboard / vehicle, Back the toy, LB / RB ACTION 6 / 7, the stick
     clicks ACTION 5 / 8; Start presses Escape; the left stick, or the D-pad at full lock while it
     rests, is X and Y, and either is the arrow keys too (menus); the triggers are Z (RT - LT) and
     buttons 11 / 12, and the mouse's buttons (the camera's zoom: LT out, RT in); the right stick
     moves the mouse (the camera: port_pad_frame); any button skips a movie (video.c). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "input.h"
#include "options.h"
#include "pad.h"

/* ------------------------------------------------------------------ start-up and frames */
void video_tick(void);
void ddraw_init(void);
uint32_t dmusic_cocreate(uint32_t clsid, uint32_t iid, uint32_t ppv);
void framerate_frame(double ms);

void port_peek_hook(void) { video_tick(); }             /* the movie player's next frame */
void port_graphics_init(void) { ddraw_init(); }
uint32_t port_cocreate(uint32_t clsid, uint32_t iid, uint32_t ppv) { return dmusic_cocreate(clsid, iid, ppv); }
void port_frame_time(double ms) { framerate_frame(ms); }

/* Miles' 3D provider: the game opens one of seven it knows by name (0x858861: EAX 2, EAX, DS3D
   hardware, DS3D 7+ software full / light HRTF or pan, DS3D emulation) and only with one open sets
   its sound up, DirectMusic too (without which it stops at the front end, 0x85b0e7) */
static const char *const miles_providers[] = {"DirectSound3D 7+ Software - Full HRTF", NULL};
const char *const *port_miles_providers(void) { return miles_providers; }

/* options.txt's comments for this game's keys (written above a key the first time it is saved) */
const char *port_option_help(const char *key)
{
    static const char *const help[][2] = {
        {"resolution", "resolution: native (the screen's size, the default), original (640x480) or WxH"},
        {"vsync", "vsync: on (frames wait for the screen) or off"},
        {"frame_steps", "frame_steps: 30 (what the game steps once a frame keeps its pace at 30 frames a "
                        "second, the rate it was made for: the default), 60, or off (once a frame, as the original)"},
        {"fast_loading", "fast_loading: on (the loading screen redrawn at most 30 times a second, the default) or off"},
        {"intro_movies", "intro_movies: on or off (the three start-up movies)"},
        {"camera_speed", "camera_speed: how fast the right stick turns the camera (600 by default)"},
        {"controller_deadzone", "controller_deadzone: the sticks' dead zone in percent (6 by default)"},
        {"prompts", "prompts: on-screen button prompts, off (the default) or on; prompt_style game (the menu "
                    "buttons' look) or lego, prompt_keys (the keyboard's keys when it was used last), "
                    "prompt_menus, prompt_dialogue, prompt_legend (on foot and on the skateboard): on or off"},
        {"collision_fix", "collision_fix: on (the default: two values the collision code reads without setting "
                          "them are 0) or off (as shipped: whatever is left on the stack; for the next start)"},
        {"camera_tilt", "camera_tilt: how much of the game's downward camera tilt is kept, 0-100 % (100, the "
                        "default: as shipped)"},
        {"fog", "fog: off (no distance fog, the default) or on (as shipped)"},
        {"renderer", "renderer: d3d7 (the game's Direct3D 7 calls through the port's Direct3D 7, the default) "
                     "or native (DESIGN.md: Native renderer; in progress)"},
        {"gpu_transform", "gpu_transform: off (the CPU transforms the vertices, as the original path; the "
                          "default) or on (the GPU: the same picture to a pixel or two)"},
        {"draw_distance", "draw_distance: game (the Graphics screen's Fog Distance slider, the default), far "
                          "(each area's farthest whatever the slider) or a number (that many times the farthest)"},
        {"antialiasing", "antialiasing: off (the default), 2x, 4x or 8x (multisampling)"},
    };
    for (size_t i = 0; i < sizeof help / sizeof *help; i++)
        if (!strcasecmp(help[i][0], key))
            return help[i][1];
    return NULL;
}

/* ------------------------------------------------------------------ the controller */
#define JOY_BRAKE 10
#define JOY_GAS 11
#define TRIGGER_DOWN 1000      /* a trigger counts as a button from 3 % of its travel */
#define DIK_ESCAPE 0x01
#define DIK_UP 0xc8
#define DIK_LEFT 0xcb
#define DIK_RIGHT 0xcd
#define DIK_DOWN 0xd0

/* controller button -> joystick button (0-based: "Button 1" is 0), -1 none */
static int joy_button(int b)
{
    switch (b) {
    case PAD_Y: return 0;       /* ACTION 1: skateboard, horse, vehicle */
    case PAD_A: return 1;       /* ACTION 2: jump */
    case PAD_B: return 2;       /* ACTION 3: talk, doors (hold: the Infomaniac) */
    case PAD_X: return 3;       /* ACTION 4: throw pizza, gallop */
    case PAD_RSTICK: return 4;  /* ACTION 5 */
    case PAD_LB: return 5;      /* ACTION 6 */
    case PAD_RB: return 6;      /* ACTION 7 */
    case PAD_LSTICK: return 7;  /* ACTION 8 */
    case PAD_BACK: return 8;    /* Return: the toy */
    case PAD_GUIDE: return 9;
    }
    return -1;
}

static int clamp16(double v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : (int)lround(v); }

/* the sticks' dead zone (controller_deadzone, percent; pad.c reads the same) in stick units */
static double stick_dead(void)
{
    double v = atof(option_str("controller_deadzone", "6"));
    return (v < 0 ? 0 : v > 90 ? 90 : v) / 100 * 32767;
}

/* keys the controller presses (menus, the pause menu), each let go only if the controller pressed it */
static uint8_t key_down[256];
static void key(int dik, int down)
{
    if (!down && !key_down[dik])
        return;
    key_down[dik] = (uint8_t)down;
    input_key(dik, down);
    input_used(1);                      /* still the controller (the button prompts stay) */
}

/* the arrow keys, held while the D-pad or the stick holds them (0 left, 1 right, 2 up, 3 down) */
static uint8_t dpad_dir[4], stick_dir[4];
static void arrows(const PadState *st)
{
    static const int dik[4] = {DIK_LEFT, DIK_RIGHT, DIK_UP, DIK_DOWN};
    static const int dpad[4] = {PAD_DLEFT, PAD_DRIGHT, PAD_DUP, PAD_DDOWN};
    int x = pad_axis_value(PAD_LX), y = pad_axis_value(PAD_LY);  /* (the stick before its dead zone) */
    for (int i = 0; i < 4; i++) {
        dpad_dir[i] = st->button[dpad[i]];
        int want = i == 0 ? x < -20000 : i == 1 ? x > 20000 : i == 2 ? y < -20000 : y > 20000;
        if (want)                       /* (a little hysteresis) */
            stick_dir[i] = 1;
        else if (abs(i < 2 ? x : y) < 12000)
            stick_dir[i] = 0;
        int on = dpad_dir[i] || stick_dir[i];
        if (on != key_down[dik[i]])
            key(dik[i], on);
    }
}

int port_pad_send(int joy, const PadState *st)
{
    static uint8_t was[PAD_BUTTONS];
    int pressed = 0;
    for (int b = 0; b < PAD_BUTTONS; b++)
        pressed |= st->button[b] && !was[b];
    if (pressed)
        video_skip_press();             /* (any button skips a movie, as any key: video.c) */
    if (st->button[PAD_START] != was[PAD_START])
        key(DIK_ESCAPE, st->button[PAD_START]);                 /* the pause menu */
    memcpy(was, st->button, sizeof was);

    /* X and Y: the left stick, or the D-pad at full lock while the stick rests */
    int sx = st->axis[PAD_LX], sy = st->axis[PAD_LY];
    if (sx == 0)
        sx = st->button[PAD_DLEFT] ? -32768 : st->button[PAD_DRIGHT] ? 32767 : 0;
    if (sy == 0)
        sy = st->button[PAD_DUP] ? -32768 : st->button[PAD_DDOWN] ? 32767 : 0;
    input_joy_axis(joy, INPUT_AXIS_X, sx);
    input_joy_axis(joy, INPUT_AXIS_Y, sy);
    input_joy_axis(joy, INPUT_AXIS_RX, st->axis[PAD_RX]);
    input_joy_axis(joy, INPUT_AXIS_RY, st->axis[PAD_RY]);
    arrows(st);

    /* the triggers: Z, two buttons, and the camera's zoom as the mouse's buttons (left out, right in) */
    int lt = st->axis[PAD_LT], rt = st->axis[PAD_RT];
    input_joy_axis(joy, INPUT_AXIS_Z, clamp16((double)rt - lt));
    input_joy_button(joy, JOY_BRAKE, lt > TRIGGER_DOWN);
    input_joy_button(joy, JOY_GAS, rt > TRIGGER_DOWN);
    static uint8_t zoom[2];
    for (int k = 0; k < 2; k++) {
        int v = k ? rt : lt, on = v > 8000 ? 1 : v < 4000 ? 0 : zoom[k];
        if (on != zoom[k]) {
            zoom[k] = (uint8_t)on;
            input_mouse_button(k, on);
        }
    }

    for (int b = 0; b < PAD_BUTTONS; b++) {
        int jb = joy_button(b);
        if (jb >= 0)
            input_joy_button(joy, jb, st->button[b]);
    }
    int up = st->button[PAD_DUP], dn = st->button[PAD_DDOWN], l = st->button[PAD_DLEFT], r = st->button[PAD_DRIGHT];
    int v = INPUT_POV_CENTERED;
    if (up && r) v = 4500; else if (r && dn) v = 13500;
    else if (dn && l) v = 22500; else if (l && up) v = 31500;
    else if (up) v = 0; else if (r) v = 9000; else if (dn) v = 18000; else if (l) v = 27000;
    input_joy_pov(joy, 0, v);
    return 1;
}

/* The camera is the mouse's (forward/back tilts, left/right turns): the right stick moves the
   mouse, at most camera_speed (options.txt, default 600; the Controls screen: menu.c) pixels a
   second at full tilt, by the frames' clock so the frame rate does not change it. */
static int speed = -1;
void camera_speed_set(int s) { speed = s; }
double frame_clock_ms(void);

void port_pad_frame(unsigned long frame)
{
    (void)frame;
    static double last, fx, fy;
    if (!pad_count())
        return;
    if (speed < 0)
        speed = atoi(option_str("camera_speed", "600"));
    double now = frame_clock_ms();                  /* (--test: the virtual clock) */
    double dt = last > 0 ? (now - last) / 1000.0 : 0;
    last = now;
    if (dt > 0.1)
        dt = 0.1;
    int x = pad_axis_value(PAD_RX), y = pad_axis_value(PAD_RY);
    double r = hypot(x, y), dead = stick_dead();
    if (r <= dead)
        return;
    double t = (r - dead) / (32767 - dead);
    t = t > 1 ? 1 : t;
    double k = t * t / r;               /* (a curve: fine aim near the centre), over the direction */
    fx += x * k * speed * dt;
    fy += y * k * speed * dt * 0.6;
    int dx = (int)fx, dy = (int)fy;
    fx -= dx;
    fy -= dy;
    if (dx || dy) {
        input_mouse_move(dx, dy);
        input_used(1);
    }
}
