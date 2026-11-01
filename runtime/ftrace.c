#define _GNU_SOURCE
/* Frame-rate check (development): a build with -DFTRACE (tools/ftrace_build.sh) sends every x87
   store to memory here, and every integer store (frame counters). Between frames LI2_FTRACE_FROM
   and LI2_FTRACE_TO each memory location's value is compared from one frame to the next it is
   stored in (stores within a frame, as to a scratch buffer used for one object after another, do
   not count), and the change is put down to the store site that wrote it last: per site, the
   frames with a change and their total change. Stores to a thread's stack are left out, and integer
   changes over 1000 (pointers, float bits copied). At exit "site frames change" lines go to
   LI2_FTRACE=<file>.

   Run the same scene for the same game time at two frame rates (LI2_TEST_FPS): a value that follows
   the game's clock changes by about as much at both, one stepped once a frame changes by as many
   times more as there are more frames (tools/ftrace_cmp.py lists those). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"

#define FT_LO 0x401000u
#define FT_HI 0x890000u

typedef struct { uint32_t n; double change; } FtSite;
typedef struct { uint32_t addr, site; unsigned long frame; double base, cur; int isint; } FtLoc;
#define FT_LOCS (1u << 22)
static FtSite *sites;
static FtLoc *locs;
static unsigned long ft_from, ft_to;
static int ft_state;                    /* 0 unset, 1 on, -1 off */
static char ft_path[1024];

static void settle(FtLoc *l)
{
    double d = fabs(l->cur - l->base);
    if (d > 0 && d < (l->isint ? 1000 : 1e30)) {       /* (ints: counters, not pointers or float bits) */
        sites[l->site - FT_LO].n++;
        sites[l->site - FT_LO].change += d;
    }
    l->base = l->cur;
}

static void ft_write(void)
{
    for (uint32_t i = 0; i < FT_LOCS; i++)
        if (locs[i].addr)
            settle(&locs[i]);
    FILE *f = fopen(ft_path, "w");
    if (!f) return;
    for (uint32_t i = 0; i < FT_HI - FT_LO; i++)
        if (sites[i].n)
            fprintf(f, "%08x %u %.9g\n", FT_LO + i, sites[i].n, sites[i].change);
    fclose(f);
}

static int ft_on(void)
{
    if (!ft_state) {
        const char *p = getenv("LI2_FTRACE");
        ft_state = -1;
        if (p && *p) {
            snprintf(ft_path, sizeof ft_path, "%s", p);
            const char *a = getenv("LI2_FTRACE_FROM"), *b = getenv("LI2_FTRACE_TO");
            ft_from = a ? strtoul(a, NULL, 0) : 0;
            ft_to = b ? strtoul(b, NULL, 0) : ~0ul;
            sites = calloc(FT_HI - FT_LO, sizeof *sites);
            locs = calloc(FT_LOCS, sizeof *locs);
            if (sites && locs) {
                ft_state = 1;
                atexit(ft_write);
            }
        }
    }
    return ft_state > 0 && frame_count >= ft_from && frame_count < ft_to;
}

static void ft_note(CPU *c, uint32_t site, uint32_t a, double old, double v, int isint)
{
    if (a >= c->stack_lo && a < c->stack_hi) return;
    if (site < FT_LO || site >= FT_HI || !a) return;
    uint32_t h = (a * 2654435761u) >> 10 & (FT_LOCS - 1);
    while (locs[h].addr && locs[h].addr != a)
        h = (h + 1) & (FT_LOCS - 1);
    FtLoc *l = &locs[h];
    if (!l->addr) {                     /* first store: the value it had before is the base */
        l->addr = a;
        l->base = old;
        l->frame = frame_count;
    } else if (l->frame != frame_count) {
        settle(l);
        l->frame = frame_count;
    }
    l->cur = v;
    l->site = site;
    l->isint = isint;
}
void ftrace_f32(CPU *c, uint32_t site, uint32_t a, float v)
{
    if (ft_on()) {
        uint32_t u = R32(a);
        float old;
        memcpy(&old, &u, 4);
        ft_note(c, site, a, old, v, 0);
    }
    WF32(a, v);
}

void ftrace_f64(CPU *c, uint32_t site, uint32_t a, double v)
{
    if (ft_on()) {
        uint64_t u = R64(a);
        double old;
        memcpy(&old, &u, 8);
        ft_note(c, site, a, old, v, 0);
    }
    WF64(a, v);
}

void ftrace_int(CPU *c, uint32_t site, uint32_t a, int32_t v, int size)
{
    if (!ft_on()) return;
    int32_t old = size == 1 ? (int8_t)R8(a) : size == 2 ? (int16_t)R16(a) : (int32_t)R32(a);
    ft_note(c, site, a, old, v, 1);
}

/* Coverage (the same build: tools/ftrace_build.sh compiles the generated code with
   -finstrument-functions): LI2_COVER=<file> counts the calls of each translated function between
   frames LI2_COVER_FROM and LI2_COVER_TO; at exit "address calls" lines, read by tools/fpscheck.py
   to tell code run every frame from code the test never reached. */
#include <dlfcn.h>
typedef struct { void *fn; uint32_t addr, n; } CovSlot;
#define COV_SLOTS (1u << 15)
static CovSlot *cov;
static int cov_state;                   /* 0 unset, 1 on, -1 off */
static unsigned long cov_from, cov_to;
static char cov_path[1024];

static void cov_write(void)
{
    FILE *f = fopen(cov_path, "w");
    if (!f) return;
    for (uint32_t i = 0; i < COV_SLOTS; i++)
        if (cov[i].fn && cov[i].addr && cov[i].n)
            fprintf(f, "%08x %u\n", cov[i].addr, cov[i].n);
    fclose(f);
}

void __cyg_profile_func_enter(void *fn, void *site) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *fn, void *site) __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *fn, void *site)
{
    (void)site;
    if (cov_state == 0) {
        const char *p = getenv("LI2_COVER");
        cov_state = -1;
        if (p && *p && (cov = calloc(COV_SLOTS, sizeof *cov))) {
            snprintf(cov_path, sizeof cov_path, "%s", p);
            const char *a = getenv("LI2_COVER_FROM"), *b = getenv("LI2_COVER_TO");
            cov_from = a ? strtoul(a, NULL, 0) : 0;
            cov_to = b ? strtoul(b, NULL, 0) : ~0ul;
            cov_state = 1;
            atexit(cov_write);
        }
    }
    if (cov_state < 0 || frame_count < cov_from || frame_count >= cov_to)
        return;
    uint32_t h = (uint32_t)((uintptr_t)fn >> 4) * 2654435761u >> 17 & (COV_SLOTS - 1);
    while (cov[h].fn && cov[h].fn != fn)
        h = (h + 1) & (COV_SLOTS - 1);
    CovSlot *s = &cov[h];
    if (!s->fn) {                       /* first call: the translated function's address from its name */
        s->fn = fn;
        Dl_info di;
        if (dladdr(fn, &di) && di.dli_sname && di.dli_sname[0] == 'F' && di.dli_sname[1] == '_')
            s->addr = (uint32_t)strtoul(di.dli_sname + 2, NULL, 16);
    }
    s->n++;
}
void __cyg_profile_func_exit(void *fn, void *site) { (void)fn; (void)site; }
