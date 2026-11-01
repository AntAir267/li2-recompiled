/* Enhanced options (design point 1): <user>/options.txt, "key = value" lines, # comments.
   Missing keys keep the faithful defaults. --option key=value on the command line overrides
   the file for one run. The in-game Enhanced page (enhanced.c) edits the same file. */
#include <ctype.h>
#include <stdlib.h>
#include <strings.h>
#include "runtime.h"
#include "options.h"

typedef struct { char key[48], value[208]; int dirty; } Opt;
static Opt opts[64];
static int nopts;

static void set(const char *k, const char *v)
{
    for (int i = 0; i < nopts; i++)
        if (!strcasecmp(opts[i].key, k)) {
            snprintf(opts[i].value, sizeof opts[i].value, "%s", v);
            return;
        }
    if (nopts < 64) {
        snprintf(opts[nopts].key, sizeof opts[nopts].key, "%s", k);
        snprintf(opts[nopts].value, sizeof opts[nopts].value, "%s", v);
        nopts++;
    }
}

/* "key = value" -> set; returns 0 if the line has no '=' */
int option_parse(const char *line)
{
    const char *eq = strchr(line, '=');
    if (!eq) return 0;
    char k[64], v[256];
    const char *a = line, *b = eq;
    while (a < b && isspace((unsigned char)*a)) a++;
    while (b > a && isspace((unsigned char)b[-1])) b--;
    snprintf(k, sizeof k, "%.*s", (int)(b - a), a);
    a = eq + 1;
    while (*a && isspace((unsigned char)*a)) a++;
    b = a + strlen(a);
    while (b > a && isspace((unsigned char)b[-1])) b--;
    snprintf(v, sizeof v, "%.*s", (int)(b - a), a);
    set(k, v);
    return 1;
}

void options_load(void)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/options.txt", user_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n#")] = 0;
        char *s = line;
        while (isspace((unsigned char)*s)) s++;
        if (*s && !option_parse(s))
            LOG(LOG_ALWAYS, "options.txt: \"%s\" is not key = value", s);
    }
    fclose(f);
}

/* Where a key was found, by the address of the caller's key string (some are asked many times a
   frame): a hint, checked; each thread its own. A key not there stays not there until one is added. */
static __thread struct { const char *key; int idx, n; } hint[32];

const char *option_str(const char *key, const char *def)
{
    unsigned h = (unsigned)((uintptr_t)key >> 3) % 32;
    if (hint[h].key == key) {
        int i = hint[h].idx;
        if (i < 0 ? hint[h].n == nopts : !strcasecmp(opts[i].key, key))
            return i < 0 ? def : opts[i].value;
    }
    int found = -1;
    for (int i = 0; i < nopts && found < 0; i++)
        if (!strcasecmp(opts[i].key, key))
            found = i;
    hint[h].key = key;
    hint[h].idx = found;
    hint[h].n = nopts;
    return found < 0 ? def : opts[found].value;
}

int option_bool(const char *key, int def)
{
    const char *v = option_str(key, NULL);
    if (!v) return def;
    return !strcasecmp(v, "on") || !strcasecmp(v, "yes") || !strcasecmp(v, "true") || !strcmp(v, "1");
}

void option_set(const char *key, const char *value)
{
    set(key, value);
    for (int i = 0; i < nopts; i++)
        if (!strcasecmp(opts[i].key, key))
            opts[i].dirty = 1;
}

/* comments for keys written to the file the first time */
static const char *const help[][2] = {
    {"display", "display: fullscreen (the default when started from the applications menu) or window "
                "(Alt+Enter switches too)"},
    {"resolution", "resolution: native (the screen's size, the default), original (640x480) or WxH"},
    {"vsync", "vsync: on (frames wait for the screen) or off"},
    {"frame_limit", "frame_limit: auto (the screen's refresh, the default), off or a rate"},
    {"frame_steps", "frame_steps: 30 (what the game steps once a frame keeps its pace at 30 frames a "
                    "second, the rate it was made for: the default), 60, or off (once a frame, as the original)"},
    {"fast_loading", "fast_loading: on (the loading screen redrawn at most 30 times a second, the default) or off"},
    {"intro_movies", "intro_movies: on or off (the three start-up movies)"},
    {"camera_speed", "camera_speed: how fast the right stick turns the camera (600 by default)"},
    {"controller_deadzone", "controller_deadzone: the left stick's dead zone in percent (6 by default)"},
    {"rumble", "rumble: on or off (controller vibration)"},
    {"prompts", "prompts: on-screen button prompts, off (the default) or on; prompt_style game (the menu buttons' look) or lego, prompt_keys (the keyboard's keys when it was used last), prompt_menus, prompt_dialogue, prompt_legend (on foot and on the skateboard): on or off"},
    {"collision_fix", "collision_fix: on (the default: two values the collision code reads without setting them are 0) or off (as shipped: whatever is left on the stack; for the next start)"},
    {"camera_tilt", "camera_tilt: how much of the game's downward camera tilt is kept, 0-100 % (100, the default: as shipped)"},
    {"fog", "fog: off (no distance fog, the default) or on (as shipped)"},
    {"renderer", "renderer: d3d7 (the game's Direct3D 7 calls through the port's Direct3D 7, the default) or native (DESIGN.md: Native renderer; in progress)"},
    {"gpu_transform", "gpu_transform: off (the CPU transforms the vertices, as the original path; the default) or on (the GPU: the same picture to a pixel or two)"},
    {"draw_distance", "draw_distance: game (the Graphics screen's Fog Distance slider, the default), far "
                      "(each area's farthest whatever the slider) or a number (that many times the farthest)"},
    {"antialiasing", "antialiasing: off (the default), 2x, 4x or 8x (multisampling)"},
    {NULL, NULL}};

/* Rewrite options.txt keeping its comments and order: a changed key's line is replaced, keys
   the file does not have yet are appended. */
int options_save(void)
{
    char path[1200], tmp[1300];
    snprintf(path, sizeof path, "%s/options.txt", user_dir);
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *in = fopen(path, "r"), *out = fopen(tmp, "w");
    if (!out) {
        if (in) fclose(in);
        LOG(LOG_ALWAYS, "options: cannot write %s", tmp);
        return 0;
    }
    int written[64] = {0};
    char line[512];
    if (!in)
        fputs("# LEGO Island 2 native: options (key = value).\n"
              "# Delete a line to get the original behaviour.\n", out);
    while (in && fgets(line, sizeof line, in)) {
        char k[64] = "";
        const char *eq = strchr(line, '=');
        const char *hash = strchr(line, '#');
        if (eq && (!hash || hash > eq)) {
            const char *a = line, *b = eq;
            while (a < b && isspace((unsigned char)*a)) a++;
            while (b > a && isspace((unsigned char)b[-1])) b--;
            snprintf(k, sizeof k, "%.*s", (int)(b - a), a);
        }
        int done = 0;
        for (int i = 0; i < nopts && k[0]; i++)
            if (!strcasecmp(opts[i].key, k) && opts[i].dirty) {
                if (!written[i]) fprintf(out, "%s = %s\n", opts[i].key, opts[i].value);
                written[i] = done = 1;
            }
        if (!done)
            fputs(line, out);
    }
    for (int i = 0; i < nopts; i++)
        if (opts[i].dirty && !written[i]) {
            for (int k = 0; help[k][0]; k++)
                if (!strcasecmp(help[k][0], opts[i].key))
                    fprintf(out, "\n# %s\n", help[k][1]);
            fprintf(out, "%s = %s\n", opts[i].key, opts[i].value);
        }
    if (in) fclose(in);
    int ok = fclose(out) == 0 && rename(tmp, path) == 0;
    if (!ok)
        LOG(LOG_ALWAYS, "options: cannot save %s", path);
    return ok;
}
