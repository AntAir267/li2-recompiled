/* Controllers as DirectInput joysticks (pad.h): the mapping SDL's gamepad events go through
   (win.c), and LI2_PAD, a scripted controller for headless tests that goes through the
   same calls.

   LEGO Island 2 reads one joystick (0x41f9c9; on when Options > Controls > Joystick is, the
   default): X and Y past half way are its four directions, as the arrow keys; buttons 1-10 are
   fixed to the keyboard's actions: 1 ACTION 1 (Left Ctrl: skateboard, horse, vehicle), 2 ACTION 2
   (Left Alt: jump), 3 ACTION 3 (Left Shift: talk, doors, hold for the Infomaniac), 4 ACTION 4
   (Space: throw pizza, gallop), 5-8 ACTION 5-8 (< > Z X C), 9 Return (the toy), 10 none. Escape
   (the pause menu, back in menus) is keyboard only, and the camera is the mouse's.
   The controller's buttons are numbered so the Xbox layout plays naturally: A jump, B talk,
   X throw, Y skateboard/vehicle, Back the toy, LB/RB ACTION 6/7, the stick clicks ACTION 5/8;
   Start presses Escape. Axes: the left stick (round dead zone, controller_deadzone in
   options.txt, percent, default 6) or the D-pad at full lock. */
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "input.h"
#include "options.h"
#include "pad.h"

typedef struct {
    int connected;
    uint8_t button[PAD_BUTTONS];
    int axis[PAD_AXES];
} Pad;

static pthread_mutex_t pad_lock = PTHREAD_MUTEX_INITIALIZER;
static Pad pads[INPUT_MAX_JOYS];
static double stick_dead = 0.06 * 32767;
/* TWO PLAYERS (split.c): while a two-player race runs, the second controller connected is player
   2's: it reaches neither the game's joystick (player 1's) nor the menus' keys, and is read as an
   Xbox pad (pad_p2_xbox) */
static int split_slot = -1;

/* SDL button -> joystick button (0-based: "Button 1" is 0), -1 none */
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
#define JOY_BRAKE 10
#define JOY_GAS 11
#define TRIGGER_DOWN 1000      /* a trigger counts as a button from 3 % of its travel */

/* keys the controller presses in menus (DirectInput key codes) */
#define DIK_ESCAPE 0x01
#define DIK_RETURN 0x1c
#define DIK_SPACE 0x39
#define DIK_UP 0xc8
#define DIK_LEFT 0xcb
#define DIK_RIGHT 0xcd
#define DIK_DOWN 0xd0
extern int game_driving(void);
static uint8_t menu_key_down[256];


static void menu_key(int dik, int down)
{
    if (down && game_driving())
        return;
    if (!down && !menu_key_down[dik])
        return;
    menu_key_down[dik] = (uint8_t)down;
    input_key(dik, down);
    input_used(1);                      /* still the controller (the button icons stay) */
}

static int clamp16(double v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : (int)lround(v); }

static int last_controller;             /* the device used last was a controller */
void input_used(int controller) { last_controller = controller != 0; }
int input_controller_last(void) { return last_controller; }

/* Plugging a controller in counts as using it (the prompts switch to it); unplugging the last
   one hands them back to the keyboard. */
int pad_attach(const char *id, const char *name)
{
    int slot = input_joy_attach(id, name, 6, 16, 1);
    if (slot < 0)
        return -1;
    input_joy_set_ff(slot, ifc_rumble_wanted());    /* the game plays force feedback as rumble */
    pthread_mutex_lock(&pad_lock);
    memset(&pads[slot], 0, sizeof pads[slot]);
    pads[slot].connected = 1;
    pthread_mutex_unlock(&pad_lock);
    input_used(1);
    return slot;
}

void pad_detach(int slot)
{
    if (slot < 0 || slot >= INPUT_MAX_JOYS)
        return;
    input_joy_disconnect(slot);
    pthread_mutex_lock(&pad_lock);
    memset(&pads[slot], 0, sizeof pads[slot]);
    pthread_mutex_unlock(&pad_lock);
    if (!pad_count())
        input_used(0);
}

/* the arrow keys, held while the D-pad or the stick holds them (0 left, 1 right, 2 up, 3 down) */
static uint8_t dpad_dir[4], stick_dir[4];
static void arrow(int i, int stick, int down)
{
    static const int dik[4] = {DIK_LEFT, DIK_RIGHT, DIK_UP, DIK_DOWN};
    (stick ? stick_dir : dpad_dir)[i] = (uint8_t)down;
    int want = dpad_dir[i] || stick_dir[i];
    if (want != menu_key_down[dik[i]])
        menu_key(dik[i], want);
}

/* the X axis: the left stick, or the D-pad at full lock while the stick rests */
static void steer(int slot)
{
    Pad *p = &pads[slot];
    int x = p->axis[PAD_LX], y = p->axis[PAD_LY];
    double r = hypot(x, y), k = r > stick_dead ? (r - stick_dead) / (32767 - stick_dead) * 32767 / r : 0;
    int sx = clamp16(x * k), sy = clamp16(y * k);
    if (sx == 0)
        sx = p->button[PAD_DLEFT] ? -32768 : p->button[PAD_DRIGHT] ? 32767 : 0;
    if (sy == 0)
        sy = p->button[PAD_DUP] ? -32768 : p->button[PAD_DDOWN] ? 32767 : 0;
    input_joy_axis(slot, INPUT_AXIS_X, sx);
    input_joy_axis(slot, INPUT_AXIS_Y, sy);
    /* the stick as arrow keys in menus, with a little hysteresis */
    for (int i = 0; i < 4; i++) {
        int want = i == 0 ? x < -20000 : i == 1 ? x > 20000 : i == 2 ? y < -20000 : y > 20000;
        if (want && !stick_dir[i]) arrow(i, 1, 1);
        else if (!want && stick_dir[i] && abs(i < 2 ? x : y) < 12000) arrow(i, 1, 0);
    }
}

/* the left stick's X for the car's steering (game.c): -1 .. 1 past the round dead zone, the
   connected controller pushed furthest */
double pad_steer(void)
{
    double best = 0;
    pthread_mutex_lock(&pad_lock);
    for (int s = 0; s < INPUT_MAX_JOYS; s++) {
        if (!pads[s].connected || s == split_slot)
            continue;
        double x = pads[s].axis[PAD_LX], y = pads[s].axis[PAD_LY], r = hypot(x, y);
        double v = r > stick_dead ? x * (r - stick_dead) / (32767 - stick_dead) / r : 0;
        if (fabs(v) > fabs(best))
            best = v;
    }
    pthread_mutex_unlock(&pad_lock);
    return best > 1 ? 1 : best < -1 ? -1 : best;
}

void pad_axis(int slot, int axis, int value)
{
    if (slot < 0 || slot >= INPUT_MAX_JOYS || axis < 0 || axis >= PAD_AXES)
        return;
    pthread_mutex_lock(&pad_lock);
    Pad *p = &pads[slot];
    p->axis[axis] = value;
    int lt = p->axis[PAD_LT], rt = p->axis[PAD_RT];
    pthread_mutex_unlock(&pad_lock);
    if (slot == split_slot)
        return;
    if (abs(value) > 12000)
        input_used(1);
    if (axis == PAD_LT || axis == PAD_RT) {
        input_joy_axis(slot, INPUT_AXIS_Z, clamp16((double)rt - lt));
        input_joy_button(slot, axis == PAD_RT ? JOY_GAS : JOY_BRAKE, value > TRIGGER_DOWN);
        /* the camera's zoom: the mouse's left button out (LT), right button in (RT) */
        static uint8_t held[2];
        int k = axis == PAD_RT, on = value > 8000 ? 1 : value < 4000 ? 0 : held[k];
        if (on != held[k]) {
            held[k] = (uint8_t)on;
            input_mouse_button(k, on);
        }
        return;
    }
    if (axis == PAD_LX || axis == PAD_LY) {
        steer(slot);
        return;
    }
    /* the right stick: round dead zone, the rest stretched over the range */
    int x = p->axis[PAD_RX], y = p->axis[PAD_RY];
    double r = hypot(x, y), k = r > stick_dead ? (r - stick_dead) / (32767 - stick_dead) * 32767 / r : 0;
    input_joy_axis(slot, INPUT_AXIS_RX, clamp16(x * k));
    input_joy_axis(slot, INPUT_AXIS_RY, clamp16(y * k));
}

void pad_button(int slot, int button, int down)
{
    if (slot < 0 || slot >= INPUT_MAX_JOYS || button < 0 || button >= PAD_BUTTONS)
        return;
    if (down) {
        input_used(1);
        video_skip_press();             /* (any button skips a movie, as any key: video.c) */
    }
    pthread_mutex_lock(&pad_lock);
    Pad *p = &pads[slot];
    p->button[button] = down != 0;
    int up = p->button[PAD_DUP], dn = p->button[PAD_DDOWN], l = p->button[PAD_DLEFT], r = p->button[PAD_DRIGHT];
    pthread_mutex_unlock(&pad_lock);
    if (slot == split_slot)
        return;
    switch (button) {
    case PAD_START: menu_key(DIK_ESCAPE, down); break;         /* the pause menu */
    case PAD_DLEFT: arrow(0, 0, down); break;
    case PAD_DRIGHT: arrow(1, 0, down); break;
    case PAD_DUP: arrow(2, 0, down); break;
    case PAD_DDOWN: arrow(3, 0, down); break;
    }
    if (button >= PAD_DUP && button <= PAD_DRIGHT) {
        int v = INPUT_POV_CENTERED;
        if (up && r) v = 4500; else if (r && dn) v = 13500;
        else if (dn && l) v = 22500; else if (l && up) v = 31500;
        else if (up) v = 0; else if (r) v = 9000; else if (dn) v = 18000; else if (l) v = 27000;
        input_joy_pov(slot, 0, v);
        steer(slot);
        return;
    }
    int jb = joy_button(button);
    if (jb >= 0)
        input_joy_button(slot, jb, down);
}

int pad_held(int button)
{
    int r = 0;
    pthread_mutex_lock(&pad_lock);
    for (int s = 0; s < INPUT_MAX_JOYS; s++)
        r |= pads[s].connected && s != split_slot && pads[s].button[button];
    pthread_mutex_unlock(&pad_lock);
    return r;
}

int pad_axis_value(int axis)
{
    int r = 0;
    pthread_mutex_lock(&pad_lock);
    for (int s = 0; s < INPUT_MAX_JOYS; s++)
        if (pads[s].connected && s != split_slot && abs(pads[s].axis[axis]) > abs(r))
            r = pads[s].axis[axis];
    pthread_mutex_unlock(&pad_lock);
    return r;
}

/* two players on (split.c, each frame of a two-player race) or off: the second controller connected
   is player 2's; the game's joystick for it is left at rest */
void pad_split(int on)
{
    int want = -1;
    pthread_mutex_lock(&pad_lock);
    for (int s = 0, seen = 0; on && s < INPUT_MAX_JOYS && want < 0; s++)
        if (pads[s].connected && seen++) want = s;
    int was = split_slot;
    split_slot = want;
    pthread_mutex_unlock(&pad_lock);
    if (want == was)
        return;
    if (want >= 0) {
        for (int a = 0; a < INPUT_JOY_AXES; a++) input_joy_axis(want, a, 0);
        for (int b = 0; b < INPUT_JOY_BUTTONS; b++) input_joy_button(want, b, 0);
        input_joy_pov(want, 0, INPUT_POV_CENTERED);
        LOG(LOG_ALWAYS, "two players: controller %d is player 2's", want);
    }
}

/* player 2's controller as the Xbox pad the game reads: buttons (1 up, 2 down, 4 left, 8 right,
   0x10 Start, 0x20 Back, 0x40/0x80 the sticks), the analog buttons 0-255 (A B X Y Black White LT
   RT; RB is Black, LB White), the sticks (+ up, round dead zone). 0 if there is none. */
int pad_p2_xbox(uint16_t *buttons, uint8_t analog[8], int16_t stick[4])
{
    pthread_mutex_lock(&pad_lock);
    int s = split_slot;
    if (s < 0 || !pads[s].connected) {
        pthread_mutex_unlock(&pad_lock);
        return 0;
    }
    const Pad *p = &pads[s];
    static const int bits[8][2] = {{PAD_DUP, 1}, {PAD_DDOWN, 2}, {PAD_DLEFT, 4}, {PAD_DRIGHT, 8},
                                   {PAD_START, 0x10}, {PAD_BACK, 0x20}, {PAD_LSTICK, 0x40}, {PAD_RSTICK, 0x80}};
    uint16_t b = 0;
    for (int i = 0; i < 8; i++)
        if (p->button[bits[i][0]]) b |= (uint16_t)bits[i][1];
    *buttons = b;
    static const int abtn[6] = {PAD_A, PAD_B, PAD_X, PAD_Y, PAD_RB, PAD_LB};
    for (int i = 0; i < 6; i++)
        analog[i] = p->button[abtn[i]] ? 255 : 0;
    analog[6] = (uint8_t)(p->axis[PAD_LT] > 0 ? p->axis[PAD_LT] * 255 / 32767 : 0);
    analog[7] = (uint8_t)(p->axis[PAD_RT] > 0 ? p->axis[PAD_RT] * 255 / 32767 : 0);
    for (int k = 0; k < 2; k++) {
        double x = p->axis[k ? PAD_RX : PAD_LX], y = p->axis[k ? PAD_RY : PAD_LY], r = hypot(x, y);
        double f = r > stick_dead ? (r - stick_dead) / (32767 - stick_dead) * 32767 / r : 0;
        stick[2 * k] = (int16_t)clamp16(x * f);
        stick[2 * k + 1] = (int16_t)clamp16(-y * f);
    }
    pthread_mutex_unlock(&pad_lock);
    return 1;
}

int pad_count(void)
{
    int n = 0;
    pthread_mutex_lock(&pad_lock);
    for (int s = 0; s < INPUT_MAX_JOYS; s++)
        n += pads[s].connected;
    pthread_mutex_unlock(&pad_lock);
    return n;
}

/* ------------------------------------------------------------------ LI2_PAD */
/* axis: 0 a button, 1 an axis, 2 plugging (index 0 unplug, 1 the controller, 2 another one);
   p2: player 2's controller ("P2." before the name; connected at its first event) */
typedef struct { unsigned long at, release; int axis, index, value, hold, state, p2; } PadPress;
static PadPress *script;
static int nscript, script_slot = -1, script_slot2 = -1;
static const char *const plug_names[3] = {"UNPLUG", "PLUG", "PLUG2"};

static const struct { const char *name; int button; } button_names[] = {
    {"A", PAD_A}, {"B", PAD_B}, {"X", PAD_X}, {"Y", PAD_Y}, {"BACK", PAD_BACK}, {"VIEW", PAD_BACK},
    {"SELECT", PAD_BACK}, {"GUIDE", PAD_GUIDE}, {"START", PAD_START}, {"MENU", PAD_START},
    {"LS", PAD_LSTICK}, {"L3", PAD_LSTICK}, {"RS", PAD_RSTICK}, {"R3", PAD_RSTICK},
    {"LB", PAD_LB}, {"RB", PAD_RB}, {"UP", PAD_DUP}, {"DOWN", PAD_DDOWN}, {"LEFT", PAD_DLEFT},
    {"RIGHT", PAD_DRIGHT}, {"MISC", PAD_MISC1},
};
static const char *const axis_names[PAD_AXES] = {"LX", "LY", "RX", "RY", "LT", "RT"};

/* the Controls screen's DEAD ZONE (menu.c): percent */
void pad_deadzone_set(double percent) { stick_dead = (percent < 0 ? 0 : percent > 90 ? 90 : percent) / 100 * 32767; }

void pad_init(void)
{
    const char *dz = option_str("controller_deadzone", NULL);
    if (dz) {
        double v = atof(dz);
        stick_dead = (v < 0 ? 0 : v > 90 ? 90 : v) / 100 * 32767;
    }
    const char *env = getenv("LI2_PAD");
    if (!env || !*env)
        return;
    char *copy = strdup(env), *save = NULL;
    for (char *tok = strtok_r(copy, ", \t\n", &save); tok; tok = strtok_r(NULL, ", \t\n", &save)) {
        char *colon = strchr(tok, ':'), *end;
        unsigned long at = colon ? strtoul(tok, &end, 10) : 0;
        if (!colon || end != colon) {
            LOG(LOG_ALWAYS, "LI2_PAD: \"%s\" is not frame:BUTTON or frame:AXIS=value", tok);
            continue;
        }
        char *name = colon + 1, *star = strchr(name, '*'), *eq = strchr(name, '=');
        int hold = 3, p2 = 0;
        if (!strncasecmp(name, "P2.", 3)) {
            p2 = 1;
            name += 3;
        }
        if (star) {
            *star = 0;
            hold = atoi(star + 1) > 0 ? atoi(star + 1) : 1;
        }
        PadPress p = {at, 0, 0, -1, 1, hold, 0, p2};
        for (int k = 0; k < 3 && !eq; k++)
            if (!strcasecmp(name, plug_names[k])) {
                p.axis = 2;
                p.index = k;
            }
        if (p.axis == 2) {
        } else if (eq) {
            *eq = 0;
            p.axis = 1;
            p.value = atoi(eq + 1);
            for (int a = 0; a < PAD_AXES; a++)
                if (!strcasecmp(name, axis_names[a]))
                    p.index = a;
        } else {
            for (size_t k = 0; k < sizeof button_names / sizeof *button_names; k++)
                if (!strcasecmp(name, button_names[k].name))
                    p.index = button_names[k].button;
        }
        if (p.index < 0) {
            LOG(LOG_ALWAYS, "LI2_PAD: unknown %s \"%s\"", eq ? "axis" : "button", name);
            continue;
        }
        script = realloc(script, (nscript + 1) * sizeof *script);
        script[nscript++] = p;
    }
    free(copy);
    int first = -1;                 /* the first plugging event: if it plugs in, none at start-up */
    for (int k = 0; k < nscript; k++)
        if (script[k].axis == 2 && (first < 0 || script[k].at < script[first].at))
            first = k;
    if (first >= 0 && script[first].index) {
        LOG(LOG_ALWAYS, "LI2_PAD: %d scripted inputs, the controller not connected yet", nscript);
        return;
    }
    script_slot = pad_attach("insane-pad-script", "Scripted controller");
    LOG(LOG_ALWAYS, "LI2_PAD: controller connected as joystick %d, %d scripted inputs", script_slot, nscript);
}

static void script_plug(unsigned long frame, int which)
{
    if (script_slot >= 0) {
        LOG(LOG_ALWAYS, "LI2_PAD: frame %lu: joystick %d unplugged", frame, script_slot);
        pad_detach(script_slot);
        script_slot = -1;
    }
    if (which) {
        script_slot = which == 1 ? pad_attach("insane-pad-script", "Scripted controller")
                                 : pad_attach("insane-pad-script-2", "Another scripted controller");
        LOG(LOG_ALWAYS, "LI2_PAD: frame %lu: %s connected as joystick %d", frame,
            which == 1 ? "controller" : "another controller", script_slot);
    }
}

/* The camera is the mouse's (forward/back tilts, left/right turns; left button zooms out, right
   zooms in): the right stick moves the mouse, at most camera_speed (options.txt, default 600)
   pixels a second at full tilt, by real time so the frame rate does not change it; the triggers
   are the buttons (pad_axis). */
static int speed = -1;
void pad_camera_speed_set(int s) { speed = s; }      /* (the Controls screen: menu.c) */
static void camera_stick(void)
{
    static double last, fx, fy;
    if (speed < 0)
        speed = atoi(option_str("camera_speed", "600"));
    extern double frame_clock_ms(void);
    double now = frame_clock_ms();                  /* (--test: the virtual clock, LI2_TEST_FPS) */
    double dt = last > 0 ? (now - last) / 1000.0 : 0;
    last = now;
    if (dt > 0.1)
        dt = 0.1;
    int x = pad_axis_value(PAD_RX), y = pad_axis_value(PAD_RY);
    double r = hypot(x, y);
    if (r <= stick_dead)
        return;
    double t = (r - stick_dead) / (32767 - stick_dead);
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

void pad_frame(unsigned long frame)
{
    if (pad_count())
        camera_stick();
    for (int k = 0; k < nscript; k++) {
        PadPress *s = &script[k];
        int down = -1;
        if (s->state == 0 && frame >= s->at) {
            s->state = 1;
            s->release = frame + (unsigned long)s->hold;
            down = 1;
        } else if (s->state == 1 && frame >= s->release) {
            s->state = 2;
            down = 0;
        }
        if (down < 0)
            continue;
        if (s->p2 && script_slot2 < 0) {
            script_slot2 = pad_attach("insane-pad-script-p2", "Player 2's scripted controller");
            LOG(LOG_ALWAYS, "LI2_PAD: frame %lu: player 2's controller connected as joystick %d", frame, script_slot2);
        }
        int slot = s->p2 ? script_slot2 : script_slot;
        if (s->axis == 2) {
            if (down)
                script_plug(frame, s->index);
        } else if (s->axis) {
            LOG(LOG_ALWAYS, "LI2_PAD: frame %lu: %s%s = %d", frame, s->p2 ? "P2." : "", axis_names[s->index], down ? s->value : 0);
            pad_axis(slot, s->index, down ? s->value : 0);
        } else {
            const char *n = "?";
            for (size_t b = 0; b < sizeof button_names / sizeof *button_names; b++)
                if (button_names[b].button == s->index) {
                    n = button_names[b].name;
                    break;
                }
            LOG(LOG_ALWAYS, "LI2_PAD: frame %lu: %s%s %s", frame, s->p2 ? "P2." : "", n, down ? "down" : "up");
            pad_button(slot, s->index, down);
        }
    }
    padgame_frame(frame);
}
