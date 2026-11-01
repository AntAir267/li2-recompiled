/* Character skinning, hand-written (0x86b3c0; builds/en_v2.py HOOKS 'skin'): about a quarter of the
   game thread's time in the town as translated (DESIGN.md, performance). Thiscall, ecx = a skinned
   mesh, one argument: its bones' matrices (64 bytes each, row vectors: x' = x*m0 + y*m4 + z*m8 + m12).
   It locks the mesh's vertex buffer (0x40f8a0), writes each vertex's position and normal moved by
   its bone (two passes: the first influence of each vertex, then the extra influences added in),
   lights them (0x8671c0), unlocks it (0x40f8d0) and draws the mesh's parts [verified: the
   instructions; the decompile in work/decomp/0086b3c0.c].

   The arithmetic is the x87's, in the same order: each product and sum in long double (80 bits, as
   the translation keeps the FPU's registers), rounded to float where the game stores one. So the
   vertices come out bit for bit as the translated function's (tests/hashcheck.sh: the draw hash
   covers the vertex bytes). The calls go to the translated functions as the game made them.
   Option native_skin = off runs the translation instead. */
#include <string.h>
#include "options.h"
#include "runtime.h"
#include "funcs.h"

int d3d_native_on(void);
void native_material(CPU *c, uint32_t mgr, uint32_t stage, uint32_t slot);
void native_preset(CPU *c, int32_t p);
void native_draw(CPU *c, uint32_t vb, uint32_t start, uint32_t n, uint32_t idx, uint32_t ni);

typedef long double ld;

#define SKIP_ALL 0x979ca8u              /* nonzero: the function does nothing */
#define RENDERER 0x5d51bd8u             /* -> the object 0x866bc0 and 0x8671c0 are methods of */
#define DEVICE 0x976088u                /* the Direct3D device */

/* p moved by m: the game's order, ((p0*m0 + p1*m4) + p2*m8) + m12 per coordinate */
static inline void move(const float *p, const float *m, ld o[3])
{
    for (int j = 0; j < 3; j++)
        o[j] = ((ld)p[0] * m[j] + (ld)p[1] * m[4 + j]) + (ld)p[2] * m[8 + j];
}

static inline float *fp(uint32_t a) { return (float *)gptr(a); }

static int hook_skin_fn(CPU *c)
{
    uint32_t self = c->ecx, entry = c->esp, bones = R32(entry + 4);
    if (R32(SKIP_ALL)) {
        c->eax = R32(SKIP_ALL);
        goto done;
    }
    /* the function's frame: its matrix at ebp - 0x80; the calls' arguments below it */
    c->esp = entry - 0x90;
    uint32_t mat = entry - 0x84, parts = R32(self + 0x28), vdecl = R32(R32(self + 0xc) + 4);
    guest_call(c, 0x410ff0u, 1, mat);
    c->ecx = R32(R32(RENDERER));
    guest_call(c, 0x866bc0u, 1, mat);
    if (R32(self)) {
        c->ecx = R32(self + 0xc);
        uint32_t verts = guest_call(c, 0x40f8a0u, 1, 0);

        /* the first influence: a vertex (32 bytes: position, a dword, normal... copied as they are
           for no bone) to the buffer's (36 bytes), its normal (12) to the mesh's */
        uint32_t src = R32(self + 8), nsrc = R32(self + 0x1c), nout = R32(self + 0x18);
        uint32_t rec = R32(self + 0x30), out = verts;
        for (int32_t i = 0; i < (int32_t)R32(self + 0x34); i++) {
            int32_t bone = (int32_t)R32(rec);
            float *p = fp(src), *o = fp(out), *n = fp(nsrc), *no = fp(nout);
            if (bone == -1) {
                W32(out, R32(src)); W32(out + 4, R32(src + 4)); W32(out + 8, R32(src + 8));
                W32(out + 0x14, R32(src + 0x10)); W32(out + 0x18, R32(src + 0x14));
                W32(out + 0x1c, R32(src + 0x18)); W32(out + 0x20, R32(src + 0x1c));
                W32(nout, R32(nsrc)); W32(nout + 4, R32(nsrc + 4)); W32(nout + 8, R32(nsrc + 8));
            } else {
                const float *m = fp(bones + ((uint32_t)bone << 6));
                ld v[3];
                move(p, m, v);
                o[0] = (float)(v[0] + m[12]);
                o[1] = (float)(v[1] + m[13]);
                o[2] = (float)(v[2] + m[14]);
                move(n, m, v);
                no[0] = (float)v[0];
                no[1] = (float)v[1];
                no[2] = (float)v[2];
                if (R32(rec + 4) != 0x3f800000u) {          /* a weight other than 1.0 */
                    float w = *fp(rec + 4);
                    for (int j = 0; j < 3; j++) o[j] = (float)((ld)o[j] * w);
                    for (int j = 0; j < 3; j++) no[j] = (float)((ld)no[j] * w);
                }
            }
            src += 0x20; out += 0x24; nsrc += 0xc; nout += 0xc; rec += 8;
        }

        /* the extra influences (24 bytes: position, vertex, bone, weight), added to the first's */
        uint32_t inf = R32(self + 0x3c);
        for (int32_t i = 0; i < (int32_t)R32(self + 0x38); i++, inf += 0x18) {
            int32_t bone = (int32_t)R32(inf + 0x10);
            if (bone == -1) {
                guest_call(c, 0x842c90u, 1, 0x8880a0u);
                continue;
            }
            uint32_t vtx = R32(inf + 0xc);
            const float *m = fp(bones + ((uint32_t)bone << 6));
            float *o = fp(verts + vtx * 36), *no = fp(R32(self + 0x18) + vtx * 12);
            ld v[3];
            float t[3], tn[3];
            move(fp(inf), m, v);
            t[0] = (float)(v[0] + m[12]);
            t[1] = (float)(v[1] + m[13]);
            t[2] = (float)(v[2] + m[14]);
            move(fp(R32(self + 0x1c) + vtx * 12), m, v);
            tn[0] = (float)v[0];
            tn[1] = (float)v[1];
            tn[2] = (float)v[2];
            if (R32(inf + 0x14) != 0x3f800000u) {
                float w = *fp(inf + 0x14);
                for (int j = 0; j < 3; j++) t[j] = (float)((ld)t[j] * w);
                for (int j = 0; j < 3; j++) tn[j] = (float)((ld)tn[j] * w);
            }
            for (int j = 0; j < 3; j++) o[j] = (float)((ld)t[j] + o[j]);
            for (int j = 0; j < 3; j++) no[j] = (float)((ld)tn[j] + no[j]);
        }

        c->ecx = R32(R32(RENDERER));
        guest_call(c, 0x8671c0u, 3, R32(self + 4), verts, R32(self + 0x18));
        c->ecx = R32(self + 0xc);
        guest_call(c, 0x40f8d0u, 0);
    }

    /* the parts (20 bytes: first index, index count, a material?, vertex range...): one
       DrawIndexedPrimitive each */
    uint32_t part = parts + 0xc;
    int native = d3d_native_on();       /* (renderer = native: native.c's wrapper, the device directly) */
    for (int32_t i = 0; i < (int32_t)R32(self + 0x24); i++, part += 0x14) {
        if (native) {
            if (R32(part))
                native_material(c, 0x97a108u, 0, (uint32_t)i);
            native_preset(c, 5);
            native_draw(c, vdecl, R32(part - 4), R32(part), R32(part + 4), R32(part - 8));
            continue;
        }
        if (R32(part)) {
            c->ecx = 0x97a108u;
            guest_call(c, 0x40def0u, 2, 0, (uint32_t)i);
        }
        guest_call(c, 0x407ba0u, 1, 5);
        uint32_t dev = R32(DEVICE);
        uint32_t hr = guest_call(c, R32(R32(dev) + 0x80), 8, dev, 4, vdecl, R32(part - 4), R32(part),
                                 R32(part + 4), R32(part - 8), 0);
        guest_call(c, 0x40a8f0u, 1, hr);
    }
    c->eax = R32(self + 0x24);
done:
    c->esp = entry;
    c->ret = R32(c->esp);
    c->esp += 8;                        /* ret 4 */
    return 1;
}

/* p moved by m as 0x422772 (a point: the translation added) and 0x4227e6 (a direction: + a float 0) */
static inline void point(const float *p, const float *m, float o[3])
{
    ld v[3];
    move(p, m, v);
    o[0] = (float)(v[0] + m[12]);
    o[1] = (float)(v[1] + m[13]);
    o[2] = (float)(v[2] + m[14]);
}
static inline void dir(const float *p, const float *m, float o[3])
{
    ld v[3];
    move(p, m, v);
    for (int j = 0; j < 3; j++)
        o[j] = (float)(v[j] + (ld)0.0f);
}

/* 0x86b0a0 (thiscall (bones), ecx = a skinned mesh of the 32-byte layout, XYZ|DIFFUSE|TEX2: the
   menus' and the town's other characters): as hook_skin_fn's, with the engine's 0x422772 /
   0x4227e6 for the points and normals (so a normal's -0 is +0), a vertex without a bone copied
   whole (32 bytes), lit by 0x866db0, its parts drawn with preset 3 [verified: the instructions] */
static int hook_skin32_fn(CPU *c)
{
    uint32_t self = c->ecx, entry = c->esp, bones = R32(entry + 4);
    if (R32(SKIP_ALL)) {
        c->eax = R32(SKIP_ALL);
        goto done;
    }
    c->esp = entry - 0x88;              /* (its frame: the matrix at entry - 0x44) */
    uint32_t mat = entry - 0x44, parts = R32(self + 0x28), vdecl = R32(R32(self + 0xc) + 4);
    guest_call(c, 0x410ff0u, 1, mat);
    c->ecx = R32(R32(RENDERER));
    guest_call(c, 0x866bc0u, 1, mat);
    if (R32(self)) {
        c->ecx = R32(self + 0xc);
        uint32_t verts = guest_call(c, 0x40f8a0u, 1, 0);
        uint32_t src = R32(self + 8), nsrc = R32(self + 0x1c), nout = R32(self + 0x18);
        uint32_t rec = R32(self + 0x30), out = verts;
        for (int32_t i = 0; i < (int32_t)R32(self + 0x34); i++, src += 0x20, out += 0x20, nsrc += 0xc,
                     nout += 0xc, rec += 8) {
            int32_t bone = (int32_t)R32(rec);
            if (bone == -1) {
                for (uint32_t k = 0; k < 0x20; k += 4)
                    W32(out + k, R32(src + k));
                W32(nout, R32(nsrc)); W32(nout + 4, R32(nsrc + 4)); W32(nout + 8, R32(nsrc + 8));
                continue;
            }
            const float *m = fp(bones + ((uint32_t)bone << 6));
            float *o = fp(out), *no = fp(nout), t[3];
            point(fp(src), m, t);
            memcpy(o, t, 12);
            dir(fp(nsrc), m, t);
            memcpy(no, t, 12);
            if (R32(rec + 4) != 0x3f800000u) {
                float w = *fp(rec + 4);
                for (int j = 0; j < 3; j++) o[j] = (float)((ld)o[j] * w);
                for (int j = 0; j < 3; j++) no[j] = (float)((ld)no[j] * w);
            }
        }
        uint32_t inf = R32(self + 0x3c);
        for (int32_t i = 0; i < (int32_t)R32(self + 0x38); i++, inf += 0x18) {
            int32_t bone = (int32_t)R32(inf + 0x10);
            if (bone == -1) {
                guest_call(c, 0x842c90u, 1, 0x8880a0u);
                continue;
            }
            uint32_t vtx = R32(inf + 0xc);
            const float *m = fp(bones + ((uint32_t)bone << 6));
            float *o = fp(verts + (vtx << 5)), *no = fp(R32(self + 0x18) + vtx * 12), t[3], tn[3];
            point(fp(inf), m, t);
            dir(fp(R32(self + 0x1c) + vtx * 12), m, tn);
            if (R32(inf + 0x14) != 0x3f800000u) {
                float w = *fp(inf + 0x14);
                for (int j = 0; j < 3; j++) t[j] = (float)((ld)t[j] * w);
                for (int j = 0; j < 3; j++) tn[j] = (float)((ld)tn[j] * w);
            }
            for (int j = 0; j < 3; j++) o[j] = (float)((ld)t[j] + o[j]);
            for (int j = 0; j < 3; j++) no[j] = (float)((ld)tn[j] + no[j]);
        }
        c->ecx = R32(R32(RENDERER));
        guest_call(c, 0x866db0u, 3, R32(self + 4), verts, R32(self + 0x18));
        c->ecx = R32(self + 0xc);
        guest_call(c, 0x40f8d0u, 0);
    }
    uint32_t part = parts + 0xc;
    int native = d3d_native_on();
    for (int32_t i = 0; i < (int32_t)R32(self + 0x24); i++, part += 0x14) {
        if (native) {
            if (R32(part))
                native_material(c, 0x97a108u, 0, (uint32_t)i);
            native_preset(c, 3);
            native_draw(c, vdecl, R32(part - 4), R32(part), R32(part + 4), R32(part - 8));
            continue;
        }
        if (R32(part)) {
            c->ecx = 0x97a108u;
            guest_call(c, 0x40def0u, 2, 0, (uint32_t)i);
        }
        guest_call(c, 0x407ba0u, 1, 3);
        uint32_t dev = R32(DEVICE);
        uint32_t hr = guest_call(c, R32(R32(dev) + 0x80), 8, dev, 4, vdecl, R32(part - 4), R32(part),
                                 R32(part + 4), R32(part - 8), 0);
        guest_call(c, 0x40a8f0u, 1, hr);
    }
    c->eax = R32(self + 0x24);
done:
    c->esp = entry;
    c->ret = R32(c->esp);
    c->esp += 8;                        /* ret 4 */
    return 1;
}

/* 0x422772 / 0x4227e6 (cdecl (out, matrix, v) -> out): v moved by the matrix, with its translation
   (a point) or without (a direction: + 0.0, as the original adds a float 0, which makes -0 +0);
   the engine's own, called for the other skinned meshes (0x86b0a0) and elsewhere: the same
   arithmetic as move() [verified: the instructions] */
static int hook_vec_point_fn(CPU *c)
{
    uint32_t out = R32(c->esp + 4), m = R32(c->esp + 8);
    const float *mm = fp(m);
    ld v[3];
    move(fp(R32(c->esp + 12)), mm, v);
    float r[3] = {(float)(v[0] + mm[12]), (float)(v[1] + mm[13]), (float)(v[2] + mm[14])};
    memcpy(fp(out), r, 12);
    c->eax = out;
    c->edx = m;
    c->ret = R32(c->esp);
    c->esp += 4;
    return 1;
}

static int hook_vec_dir_fn(CPU *c)
{
    uint32_t out = R32(c->esp + 4), m = R32(c->esp + 8);
    ld v[3];
    move(fp(R32(c->esp + 12)), fp(m), v);
    float r[3] = {(float)(v[0] + (ld)0.0f), (float)(v[1] + (ld)0.0f), (float)(v[2] + (ld)0.0f)};
    memcpy(fp(out), r, 12);
    c->eax = out;
    c->edx = m;
    c->ret = R32(c->esp);
    c->esp += 4;
    return 1;
}

void skin_init(void)
{
    if (option_bool("native_skin", 1)) {
        hook_skin = hook_skin_fn;
        hook_skin32 = hook_skin32_fn;
        hook_vec_point = hook_vec_point_fn;
        hook_vec_dir = hook_vec_dir_fn;
    }
}
