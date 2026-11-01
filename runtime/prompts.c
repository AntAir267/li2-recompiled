/* On-screen button prompts (Options > Controls > PROMPTS; off by default): a strip at the bottom
   right of the frame naming what the buttons do now, drawn over the finished frame just before
   it is presented (ddraw.c ddraw_overlay) through the renderer's own 2D path in the frame's pixels
   (vk_draw, pretransformed 2: vk.c). The pictures are tools/prompts_atlas.py's (prompts_atlas.h).

   What is shown (game.c prompt_context): in the menus Select / Back; while a line of a cutscene's
   dialogue plays Skip; on foot in an island area (the zone table's type 1) Jump / Talk / Pizza /
   Skateboard; on the skateboard Ollie / Flip / Grab / Trick (in the air with a direction). The
   controller's buttons as studs (the game's menu-button look, or LEGO studs: PLASTIC STUDS), or
   with KEYBOARD KEYS the keys (the game's default keys) when the keyboard was used last.
   Options: prompts, prompt_style (game | lego), prompt_keys, prompt_menus, prompt_dialogue,
   prompt_legend. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "gfx.h"
#include "vk.h"
#include "options.h"
#include "prompts_atlas.h"

Surface *ddraw_texture_new(int w, int h);
uint32_t *ddraw_image_pixels(Surface *s);
extern void (*ddraw_overlay)(Surface *frame);
int input_controller_last(void);
enum { PC_NONE, PC_MENU, PC_DIALOGUE, PC_FOOT, PC_BOARD };
int prompt_context(void);

/* ------------------------------------------------------------------ the atlas (QOI) */
static Surface *atlas;
static int atlas_w, atlas_h;

static int qoi_decode(const uint8_t *q, size_t n, uint32_t **out, int *w, int *h)
{
    if (n < 22 || memcmp(q, "qoif", 4))
        return 0;
    *w = q[4] << 24 | q[5] << 16 | q[6] << 8 | q[7];
    *h = q[8] << 24 | q[9] << 16 | q[10] << 8 | q[11];
    size_t npx = (size_t)*w * (size_t)*h, p = 14, i = 0;
    uint32_t *px = malloc(npx * 4);
    if (!px)
        return 0;
    uint8_t index[64][4] = {{0}}, c[4] = {0, 0, 0, 255};
    int run = 0;
    while (i < npx) {
        if (run > 0)
            run--;
        else if (p < n - 8) {
            uint8_t b = q[p++];
            if (b == 0xfe) { c[0] = q[p]; c[1] = q[p + 1]; c[2] = q[p + 2]; p += 3; }
            else if (b == 0xff) { memcpy(c, q + p, 4); p += 4; }
            else if ((b & 0xc0) == 0x00) memcpy(c, index[b], 4);
            else if ((b & 0xc0) == 0x40) {
                c[0] += ((b >> 4) & 3) - 2; c[1] += ((b >> 2) & 3) - 2; c[2] += (b & 3) - 2;
            } else if ((b & 0xc0) == 0x80) {
                int dg = (b & 0x3f) - 32, b2 = q[p++];
                c[0] += dg - 8 + (b2 >> 4); c[1] += dg; c[2] += dg - 8 + (b2 & 15);
            } else
                run = b & 0x3f;
            memcpy(index[(c[0] * 3 + c[1] * 5 + c[2] * 7 + c[3] * 11) % 64], c, 4);
        }
        px[i++] = (uint32_t)c[3] << 24 | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];   /* A8R8G8B8 */
    }
    *out = px;
    return 1;
}

static int atlas_load(void)
{
    if (atlas)
        return 1;
    uint32_t *px;
    if (!qoi_decode(prompt_atlas_qoi, sizeof prompt_atlas_qoi, &px, &atlas_w, &atlas_h)) {
        LOG(LOG_ALWAYS, "prompts: the atlas could not be read");
        return 0;
    }
    atlas = ddraw_texture_new(atlas_w, atlas_h);
    memcpy(ddraw_image_pixels(atlas), px, (size_t)atlas_w * (size_t)atlas_h * 4);
    atlas->dirty++;
    free(px);
    return 1;
}

static int pic(const char *name)
{
    for (int i = 0; i < (int)(sizeof prompt_pics / sizeof *prompt_pics); i++)
        if (!strcmp(prompt_pics[i].name, name))
            return i;
    return -1;
}

/* ------------------------------------------------------------------ drawing */
#define MAXQ 64
static SwVertex vtx[MAXQ * 6];
static int nq;

/* the part a..b (of its width, 0..1) of picture p at x, y, w x h frame pixels, tinted (A8R8G8B8) */
static void quad_part(int p, float a, float b, float x, float y, float w, float h, uint32_t tint)
{
    if (p < 0 || nq >= MAXQ)
        return;
    float pw = (float)prompt_pics[p].w;
    float u0 = (prompt_pics[p].x + 0.5f + a * (pw - 1)) / atlas_w, v0 = (prompt_pics[p].y + 0.5f) / atlas_h;
    float u1 = (prompt_pics[p].x + 0.5f + b * (pw - 1)) / atlas_w, v1 = (prompt_pics[p].y + prompt_pics[p].h - 0.5f) / atlas_h;
    const float c[4][4] = {{x, y, u0, v0}, {x + w, y, u1, v0}, {x + w, y + h, u1, v1}, {x, y + h, u0, v1}};
    static const int tri[6] = {0, 1, 2, 0, 2, 3};
    uint32_t rgba = (tint & 0xff00ff00u) | ((tint >> 16) & 0xffu) | ((tint & 0xffu) << 16);
    for (int k = 0; k < 6; k++) {
        SwVertex *o = &vtx[nq * 6 + k];
        memset(o, 0, sizeof *o);
        o->x = c[tri[k]][0];
        o->y = c[tri[k]][1];
        o->w = 1;
        o->tc[0][0] = c[tri[k]][2];
        o->tc[0][1] = c[tri[k]][3];
        o->rgba[0] = rgba;
        o->rgba[1] = 0xff000000u;
        o->packed = 1;
    }
    nq++;
}

static void quad(int p, float x, float y, float w, float h, uint32_t tint)
{
    quad_part(p, 0, 1, x, y, w, h, tint);
}

/* an action's prompt: the controller's button or the keyboard's key, and the word */
typedef struct { const char *button, *key, *word; } Prompt;
static const Prompt menu_p[] = {{"A", "Enter", "Select"}, {"B", "Esc", "Back"}};
static const Prompt dialogue_p[] = {{"B", "Space", "Skip"}};
static const Prompt foot_p[] = {{"A", "Alt", "Jump"}, {"B", "Shift", "Talk"}, {"X", "Space", "Pizza"},
                                {"Y", "Ctrl", "Skateboard"}};
static const Prompt board_p[] = {{"A", "Alt", "Ollie"}, {"X", "Shift", "Flip"}, {"B", "Space", "Grab"},
                                 {"Y", "Ctrl", "Trick"}};

static void strip(Surface *frame, const Prompt *ps, int n)
{
    float H = (float)frame->height, W = (float)frame->width, u = H / 1080.0f;
    float bh = 58 * u, gap = 10 * u, sep = 30 * u, pad = 22 * u, y = H - 40 * u - bh;
    int keys = option_bool("prompt_keys", 1) && !input_controller_last();
    const char *style = !strcasecmp(option_str("prompt_style", "game"), "lego") ? "lego_" : "stud_";
    int btn[8], wd[8];
    float bw[8], ww[8], total = 0;
    for (int i = 0; i < n; i++) {
        char name[32];
        if (keys)
            snprintf(name, sizeof name, "key_%s", ps[i].key);
        else
            snprintf(name, sizeof name, "%s%s", style, ps[i].button);
        btn[i] = pic(name);
        snprintf(name, sizeof name, "word_%s", ps[i].word);
        wd[i] = pic(name);
        if (btn[i] < 0 || wd[i] < 0)
            return;
        bw[i] = bh * (float)prompt_pics[btn[i]].w / (float)prompt_pics[btn[i]].h;
        ww[i] = bh * 0.80f * (float)prompt_pics[wd[i]].w / (float)prompt_pics[wd[i]].h;
        total += bw[i] + gap + ww[i] + (i + 1 < n ? sep : 0);
    }
    float x1 = W - 40 * u, x0 = x1 - total - 2 * pad, ph = bh + 16 * u, py = y - 8 * u;
    uint32_t shade = 0x70000000u;                                           /* the strip: dark, see-through */
    quad_part(pic("disc"), 0, 0.5f, x0 - ph / 2, py, ph / 2, ph, shade);    /* (half discs: no overlap to darken) */
    quad_part(pic("disc"), 0.5f, 1, x1, py, ph / 2, ph, shade);
    quad(pic("white"), x0, py, x1 - x0, ph, shade);
    float x = x0 + pad;
    for (int i = 0; i < n; i++) {
        quad(btn[i], x, y, bw[i], bh, 0xffffffffu);
        x += bw[i] + gap;
        quad(wd[i], x, y + bh * 0.12f, ww[i], bh * 0.80f, 0xffffffffu);
        x += ww[i] + sep;
    }
}

static void overlay(Surface *frame)
{
    if (!option_bool("prompts", 0) || frame->width < 64 || frame->height < 64)
        return;
    int ctx = prompt_context();
    nq = 0;
    switch (ctx) {
    case PC_MENU:
        if (option_bool("prompt_menus", 1))
            strip(frame, menu_p, 2);
        break;
    case PC_DIALOGUE:
        if (option_bool("prompt_dialogue", 1))
            strip(frame, dialogue_p, 1);
        break;
    case PC_FOOT:
        if (option_bool("prompt_legend", 1))
            strip(frame, foot_p, 4);
        break;
    case PC_BOARD:
        if (option_bool("prompt_legend", 1))
            strip(frame, board_p, 4);
        break;
    }
    if (!nq || !atlas_load())
        return;
    D3DState st;
    memset(&st, 0, sizeof st);
    st.rt = frame;
    st.vp.w = frame->width;
    st.vp.h = frame->height;
    st.vp.maxz = 1;
    st.rs[RS_ALPHABLENDENABLE] = 1;
    st.rs[RS_SRCBLEND] = 5;                 /* SRCALPHA */
    st.rs[RS_DESTBLEND] = 6;                /* INVSRCALPHA */
    st.rs[RS_CULLMODE] = 1;                 /* none */
    st.tss[0][TSS_COLOROP] = 4;             /* MODULATE: the picture times the tint */
    st.tss[0][TSS_COLORARG1] = 2;           /* TEXTURE */
    st.tss[0][TSS_COLORARG2] = 0;           /* DIFFUSE */
    st.tss[0][TSS_ALPHAOP] = 4;
    st.tss[0][TSS_ALPHAARG1] = 2;
    st.tss[0][TSS_ALPHAARG2] = 0;
    st.tss[0][TSS_ADDRESSU] = st.tss[0][TSS_ADDRESSV] = 3;      /* CLAMP */
    st.tss[0][TSS_MAGFILTER] = st.tss[0][TSS_MINFILTER] = 2;    /* LINEAR */
    st.tss[0][TSS_MIPFILTER] = 1;
    st.tss[1][TSS_COLOROP] = st.tss[1][TSS_ALPHAOP] = 1;        /* DISABLE */
    st.tex[0] = atlas;
    vk_draw(&st, PT_TRIANGLELIST, vtx, nq * 6, NULL, 0, 2);
}

void prompts_init(void)
{
    ddraw_overlay = overlay;
}
