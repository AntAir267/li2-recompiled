/* Per-frame steps kept to time (builds/en_v2.py FRAME_STEPS). Most of the game moves by its clock
   (0x8a0850: the frame's time in 1/30 s), but some code steps once a frame: a spin of 0.2 radians, a
   throttle ramp of 0.05, a timer counting frames, a damping of 0.8. The game was made for 30 frames
   a second (its clock's unit; its effect scripts turn seconds into frames at 30; it has no frame
   cap, so a fast PC on a 60 Hz screen ran these twice as fast). The translator passes each such
   step through here, scaled by the clock so that it keeps the pace it has at option frame_steps'
   rate (30 by default) whatever the frame rate:
     add   a step added each frame: times the frames' worth of time
     lerp  a fraction of the way each frame (x += (t - x) * f): 1 - (1 - f)^n
     pow   a factor each frame (v *= d): d^n
   frame_steps = off leaves them per frame, as the original. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "options.h"
#include "runtime.h"

#define GAME_CLOCK 0x8a0850u            /* the frame's time in 1/30 s (0x42aa40) */

static double per_clock = -1;           /* frames of the reference rate in one clock unit */

/* Some states do not run the game's clock (0x42aa40): it keeps the 1.0 its first frames set (a
   panel that opens a zone, z20DSP: its ramp ran a frame at a time [verified: LI2_WATCH of 0x8a0850
   and 0x5d4dd64]). Frames without it use the frame's real time instead (framerate_frame). */
static unsigned long clock_frame = ~0ul;
static double stale_clk = 1;

static f80 frames(void)
{
    if (per_clock < 0) {
        const char *o = option_str("frame_steps", "30");
        per_clock = !strcasecmp(o, "off") ? 0 : atof(o) > 0 ? atof(o) / 30.0 : 1.0;
    }
    if (!per_clock)
        return 1;
    if (clock_frame == ~0ul || clock_frame + 1 < frame_count)    /* (not this frame or the last) */
        return stale_clk * per_clock;
    float clk = RF32(GAME_CLOCK);
    return clk > 0 && clk < 100 ? clk * per_clock : 1;
}

f80 fstep_add(f80 v) { return v * frames(); }
/* the Graphics screen's FRAME RATE FIX (menu.c): on = frame_steps 30, off = as the original */
void framerate_steps_set(int on) { per_clock = on ? 1.0 : 0; }

/* A per-frame amount times the clock made an integer (_ftol truncates): at 30 fps a whole number,
   at higher rates its fraction was lost every frame (a fade of 10 a frame went 1 a frame at 165 fps,
   under 6 not at all). Rounded down or up instead, up as often as the fraction says (a golden-ratio
   sequence per site: evenly spread, the same every run); whole numbers are left as they are. */
int64_t fpu_ftol_dither(f80 v, uint32_t *n)
{
    double u = (double)(uint32_t)((*n)++ * 2654435769u) / 4294967296.0;
    double x = (double)v, f = floor(fabs(x) + u);
    return (int64_t)(x < 0 ? -f : f);
}

f80 fstep_lerp(f80 v)
{
    f80 n = frames();
    return n != 1 && v > 0 && v < 1 ? 1 - powl(1 - v, n) : v;
}

f80 fstep_pow(f80 v)
{
    f80 n = frames();
    return n == 1 ? v : v > 0 ? powl(v, n) : v < 0 ? -powl(-v, n) : v;
}

/* ------------------------------------------------------------------ particle effect tracks
   The engine's particle effects play scripted tracks (0x43a420 parses them: emission, size,
   colour... set or ramped over a window): a track's window is the frame number 0x9ccb0c plus its
   seconds times 30, and its ramps step once a frame (0x43c590 and its kin), evaluated by 0x439450
   for each effect each frame. So they run on a frame number at 30 a second: the tracks read
   tick_frame instead (builds/en_v2.py MEM_OVERRIDES), a frame number advanced by the game's clock, and an
   effect's tracks are evaluated once a tick (more after a slow frame, up to 3). */
#define FRAME_NUMBER 0x9ccb0cu          /* the frame number (0x41af55 counts it) */

static uint32_t slots;                  /* guest memory for the values the game reads from here */
#define TICK_FRAME (slots + 0)
static double tick_acc;

uint32_t framerate_tick_frame(void)
{
    if (!slots) {
        slots = vm_alloc(0, 0x10000, 0);
        W32(TICK_FRAME, R32(FRAME_NUMBER));
    }
    return TICK_FRAME;
}

/* after the game's clock is worked out for the frame (0x42aa40) */
static double real_clk = 1;             /* the last frame's real time in 1/30 s (framerate_frame) */
void framerate_clock(uint32_t frame)
{
    /* 0x42aa40 sets the clock to 1 while the frame number it is given is under 5 (a zone's first
       frames: no long first step after loading) [verified: decompile]. Some states call it without
       the frame number running (z20DSP's opening panel: it stayed under 5 for seconds, and so the
       clock at 1 [verified: LI2_WATCH]): past 5 such calls in a row, the clock is the frame's real
       time, as the game's would be (1 at 30 fps). */
    static unsigned pinned;
    pinned = frame < 5 ? pinned + 1 : 0;
    if (pinned > 5 && per_clock > 0)
        WF32(GAME_CLOCK, (float)real_clk);
    clock_frame = frame_count;
    framerate_tick_frame();
    frames();
    if (!per_clock) {                   /* frame_steps = off: the frame number itself */
        W32(TICK_FRAME, R32(FRAME_NUMBER));
        return;
    }
    float clk = RF32(GAME_CLOCK);
    tick_acc += clk > 0 && clk < 100 ? clk : 1;
    uint32_t n = (uint32_t)tick_acc;
    tick_acc -= n;
    W32(TICK_FRAME, R32(TICK_FRAME) + n);
}

/* once a frame (main.c), with the frame's time: a frame the game's clock did not run in gets
   its time from here, for the next frame's steps and the ticks */
void framerate_frame(double ms)
{
    double clk = ms / (1000.0 / 30.0);
    real_clk = clk > 0.01 ? (clk < 15 ? clk : 15) : 0.01;
    if (clock_frame == frame_count || !slots)
        return;
    stale_clk = real_clk;
    if (per_clock <= 0)
        return;
    tick_acc += stale_clk;
    uint32_t n = (uint32_t)tick_acc;
    tick_acc -= n;
    W32(TICK_FRAME, R32(TICK_FRAME) + n);
}

/* the tracks of one effect (thiscall, ecx = the effect): once per tick */
#define EFFECTS 4096
static struct { uint32_t obj, tick; } effects[EFFECTS];

int framerate_effect_tracks(CPU *c, void (*orig)(CPU *))
{
    frames();
    if (!per_clock)
        return 0;
    uint32_t now = R32(framerate_tick_frame());
    uint32_t h = (c->ecx * 2654435761u) >> 20 & (EFFECTS - 1);
    for (int k = 0; k < 8 && effects[h].obj && effects[h].obj != c->ecx; k++)
        h = (h + 1) & (EFFECTS - 1);
    uint32_t last = effects[h].obj == c->ecx ? effects[h].tick : now - 1;
    effects[h].obj = c->ecx;
    effects[h].tick = now;
    uint32_t runs = now - last;
    if (runs > 3) runs = 3;
    if (!runs) {
        c->ret = R32(c->esp);           /* not this frame */
        c->esp += 4;
        return 1;
    }
    uint32_t sp = c->esp, ra = R32(sp), self = c->ecx;
    for (uint32_t r = runs; r > 0; r--) {
        W32(TICK_FRAME, now - (r - 1));
        c->esp = sp;
        W32(sp, ra);
        c->ecx = self;
        orig(c);
    }
    W32(TICK_FRAME, now);
    return 1;
}

/* ------------------------------------------------------------------ frame counts
   A counter stepped once a frame and compared with a number of frames (builds/en_v2.py FRAME_COUNTS):
   the number is the frames that many 1/30 s take at the frame rate. */
uint32_t fcount(uint32_t n)
{
    f80 f = frames();
    if (!per_clock || f <= 0 || n == 0)
        return n;
    if ((int32_t)n < 0) {               /* (a count down to -n: the key prompt's blink, 0x4184fe) */
        int32_t r = (int32_t)(-(int32_t)n / f + 0.5);
        return (uint32_t)-(r ? r : 1);
    }
    uint32_t r = (uint32_t)(n / f + 0.5);
    return r ? r : 1;
}

/* ------------------------------------------------------------------ 30 Hz ticks
   An update made entirely of per-frame steps, with frame counts compared exactly (builds/en_v2.py
   TICKS: a landing effect, particles, a minigame's controller): run once per tick of the tick
   frame (1/30 s of game time) for each object (its first argument), with the game's clock at one
   tick while it runs, as the game at 30 frames a second. Between ticks the call returns what the
   last run returned. More than one tick since the last run (a slow frame): run again, up to twice. */
#define TICKED 8192
static struct { uint32_t fn, obj, tick, ret; } ticked[TICKED];

int framerate_tick_call(CPU *c, void (*orig)(CPU *), uint32_t fn)
{
    frames();
    if (!per_clock)
        return 0;
    uint32_t now = R32(framerate_tick_frame());
    uint32_t obj = R32(c->esp + 4);
    uint32_t h = ((obj ^ fn * 0x9e3779b1u) * 2654435761u) >> 19 & (TICKED - 1);
    for (int k = 0; k < 16 && ticked[h].fn && !(ticked[h].fn == fn && ticked[h].obj == obj); k++)
        h = (h + 1) & (TICKED - 1);
    int known = ticked[h].fn == fn && ticked[h].obj == obj;
    uint32_t runs = known ? now - ticked[h].tick : 1;
    if (runs > 2) runs = 2;
    if (!runs) {
        c->eax = ticked[h].ret;
        c->ret = R32(c->esp);
        c->esp += 4;
        return 1;
    }
    ticked[h].fn = fn;
    ticked[h].obj = obj;
    ticked[h].tick = now;
    uint32_t clk = R32(GAME_CLOCK), inv = R32(GAME_CLOCK + 4);
    const float one = 1.0f;
    uint32_t onebits;
    memcpy(&onebits, &one, 4);
    W32(GAME_CLOCK, onebits);
    W32(GAME_CLOCK + 4, onebits);
    CPU in = *c;
    uint32_t ra = R32(c->esp);
    for (uint32_t r = 0; r < runs; r++) {
        if (r) {
            c->eax = in.eax; c->ecx = in.ecx; c->edx = in.edx; c->ebx = in.ebx;
            c->esi = in.esi; c->edi = in.edi; c->ebp = in.ebp;
            c->esp = in.esp;
            W32(c->esp, ra);
        }
        orig(c);
    }
    W32(GAME_CLOCK, clk);
    W32(GAME_CLOCK + 4, inv);
    ticked[h].ret = c->eax;
    return 1;
}
