/* The native renderer's game side (DESIGN.md "Native renderer", N2): the engine's own Direct3D
   wrapper, hand-written from its instructions. Each function does what the original does to the
   program's memory and returns what it returns (eax; the other registers as the callers, compiled
   C, can rely on), but calls the device straight (d3d.c d3dn_*) instead of through the program's
   COM calls. Only with renderer = native (d3d_native_on); otherwise the translation runs.
   [verified: the instructions in gen/ (the translator's listing); LI2_NATIVE_CHECK and the run
   hashes, tests/render_compare.sh] */
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "funcs.h"

int d3d_native_on(void);
uint32_t d3dn_set_transform(CPU *c, uint32_t dev, uint32_t t, uint32_t m);
uint32_t d3dn_get_transform(CPU *c, uint32_t dev, uint32_t t, uint32_t m);
uint32_t d3dn_set_rs(CPU *c, uint32_t dev, uint32_t s, uint32_t v);
uint32_t d3dn_set_tss(CPU *c, uint32_t dev, uint32_t s, uint32_t t, uint32_t v);
uint32_t d3dn_set_texture(CPU *c, uint32_t dev, uint32_t s, uint32_t surf);
uint32_t d3dn_apply_block(CPU *c, uint32_t dev, uint32_t h);
uint32_t d3dn_vb_lock(CPU *c, uint32_t vb, uint32_t flags, uint32_t pp, uint32_t psize);
uint32_t d3dn_vb_unlock(CPU *c, uint32_t vb);
uint32_t d3dn_draw_ivb(CPU *c, uint32_t dev, uint32_t prim, uint32_t vb, uint32_t start, uint32_t n, uint32_t idx,
                       uint32_t ni, uint32_t flags);

#define DEVICE R32(0x976088u)           /* the program's IDirect3DDevice7 */
#define WORLD_COPY 0x97a290u            /* the engine's copy of the world matrix (0x410e00) */
#define MAT_LAST 0x89528cu              /* the material last set (0x40def0) */
#define MAT_STAGE 0x895290u             /* the stage it went to */
#define PRESETS R32(0x976758u)          /* the render-mode presets (state blocks) object */
#define PRESET_SETS 0x976478u           /* how many times a preset was applied */

/* how many calls each hand-written function took (logged at exit: they did run) */
static unsigned long n_mesh, n_group, n_char, n_wrap;

/* return from a hooked function: eax, and its ret (4 + the bytes it pops) */
static int done(CPU *c, uint32_t eax, uint32_t pops)
{
    n_wrap++;
    c->eax = eax;
    c->ret = R32(c->esp);
    c->esp += 4 + pops;
    return 1;
}

/* 0x40a8f0, the HRESULT check: no effect; eax as it leaves it (the error table at 0x892a28,
   searched for hr) */
static uint32_t hr_check(uint32_t hr)
{
    uint32_t e = 0x892a28u;
    if (hr && hr != R32(e))
        while (R32(e + 4)) {
            uint32_t v = R32(e + 8);
            e += 8;
            if (v == hr)
                break;
        }
    return e;
}

/* 0x410e00 (cdecl (matrix)): the world matrix, its 4th column made 0 0 0 (w kept), into the
   engine's copy and to the device */
static uint32_t world_set(CPU *c, uint32_t m)
{
    for (uint32_t i = 0; i < 16; i++)
        W32(WORLD_COPY + 4 * i, i == 3 || i == 7 || i == 11 ? 0 : R32(m + 4 * i));
    uint32_t hr = d3dn_set_transform(c, DEVICE, 1, WORLD_COPY);
    c->ecx = hr;
    return hr_check(hr);
}
static int hook_world_set_fn(CPU *c)
{
    if (!d3d_native_on())
        return 0;
    return done(c, world_set(c, R32(c->esp + 4)), 0);
}

/* 0x410ff0 (cdecl (out)): the device's world matrix into out (its first element through the
   FPU: fld/fstp) */
static uint32_t world_get(CPU *c, uint32_t out)
{
    uint32_t e = c->esp, buf = e - 0x44;      /* (a scratch buffer below the stack pointer) */
    c->esp = buf - 12;
    d3dn_get_transform(c, DEVICE, 1, buf);
    c->esp = e;
    WF32(out, (float)(f80)RF32(buf));
    for (uint32_t i = 1; i < 16; i++)
        W32(out + 4 * i, R32(buf + 4 * i));
    c->ecx = R32(buf + 0x3c);
    return out;
}
static int hook_world_get_fn(CPU *c)
{
    if (!d3d_native_on())
        return 0;
    return done(c, world_get(c, R32(c->esp + 4)), 0);
}

/* 0x40f8a0 (thiscall (flags), ecx = the engine's vertex buffer: +4 the device's, +0xc the
   pointer it was locked at): Lock; returns the pointer */
static uint32_t vb_lock(CPU *c, uint32_t obj, uint32_t flags, uint32_t slot)
{
    d3dn_vb_lock(c, R32(obj + 4), flags, slot, slot - 8);  /* (the pointer into slot, the size below it) */
    uint32_t p = R32(slot);
    W32(obj + 0xc, p);
    return p;
}
static int hook_vb_lock_fn(CPU *c)
{
    if (!d3d_native_on())
        return 0;
    return done(c, vb_lock(c, c->ecx, R32(c->esp + 4), c->esp + 4), 4);   /* (the argument's slot, as the original) */
}

/* 0x40f8d0 (thiscall, the same object): Unlock */
static uint32_t vb_unlock(CPU *c, uint32_t obj) { return d3dn_vb_unlock(c, R32(obj + 4)); }
static int hook_vb_unlock_fn(CPU *c)
{
    if (!d3d_native_on())
        return 0;
    return done(c, vb_unlock(c, c->ecx), 0);
}

/* 0x40def0 (thiscall (stage, slot), ecx = the materials 0x97a108): the material of the current
   model's slot: its texture's address modes on stage 0 and the texture on stage `stage`, unless
   that material and stage are already set. (Its IsLost / Restore of the texture: the port's
   surfaces are never lost, surf_IsLost: left out.) */
static uint32_t material(CPU *c, uint32_t mgr, uint32_t stage, uint32_t slot)
{
    int32_t m = (int32_t)R32(R32(R32(R32(mgr + 0x1c) + R32(mgr) * 4) + 8) + slot * 4);
    uint32_t last = R32(MAT_LAST);
    if ((uint32_t)m == last && stage == R32(MAT_STAGE))
        return last;
    if (m < 0)
        m = 0;
    W32(MAT_STAGE, stage);
    if (m >= (int32_t)R32(mgr + 0x10))
        m = (int32_t)R32(mgr + 0x10) - 1;
    uint32_t rec = R32(mgr + 0x14) + (uint32_t)m * 0xd8, dev = DEVICE;
    hr_check(d3dn_set_tss(c, dev, 0, 13, R32(rec + 0x88)));    /* ADDRESSU */
    hr_check(d3dn_set_tss(c, dev, 0, 14, R32(rec + 0x8c)));    /* ADDRESSV */
    W32(MAT_LAST, (uint32_t)m);
    return hr_check(d3dn_set_texture(c, dev, stage, R32(rec + 0xb8)));
}
static int hook_material_fn(CPU *c)
{
    if (!d3d_native_on())
        return 0;
    return done(c, material(c, c->ecx, R32(c->esp + 4), R32(c->esp + 8)), 8);
}

/* 0x4079d0 (thiscall (preset), ecx = the presets): a render-mode preset: its state block applied
   and STIPPLEDALPHA set by the original's jump table (0x407ab4 / 0x407aac: the presets that
   reach 0x407a59 set it); 1 if done or nothing to do, 0 if the block could not be applied.
   (Its three GetRenderState calls only wrote its own stack: left out.) */
static uint32_t preset_apply(CPU *c, uint32_t obj, int32_t p)
{
    if (!R32(0x97634cu + (uint32_t)p * 4))
        p = p != 7 ? 3 : 9;                 /* (0x4079b0: a preset not made yet) */
    if ((int32_t)R32(obj) == p)
        return 1;
    W32(PRESET_SETS, R32(PRESET_SETS) + 1);
    if (p < 0 || p >= 0x1e)
        return 1;
    W32(obj, (uint32_t)p);
    uint32_t k = (uint32_t)p - 4, stipple = 0;
    if (k <= 0x10)
        stipple = R32(0x407aacu + 4 * R8(0x407ab4u + k)) == 0x407a59u;
    uint32_t dev = DEVICE;
    hr_check(d3dn_set_rs(c, dev, 0x1d, stipple));
    return d3dn_apply_block(c, dev, R32(obj + (uint32_t)p * 4 + 0x164)) == 0;
}

/* 0x407ba0 (cdecl (preset)): preset_apply; when it fails the presets are made again (the
   original's own calls: free 0x407640 / 0x41c4b0, new 0x41c478 + 0x407680) and it is tried once
   more */
static uint32_t preset(CPU *c, int32_t p)
{
    uint32_t r = preset_apply(c, PRESETS, p);
    if (!r) {
        uint32_t old = PRESETS;
        if (old) {
            c->ecx = old;
            guest_call(c, 0x407640u, 0);
            guest_call(c, 0x41c4b0u, 1, old);
        }
        uint32_t o = guest_call(c, 0x41c478u, 3, 0x254u, 0x892744u, 0x3e8u);
        if (o) {
            c->ecx = o;
            o = guest_call(c, 0x407680u, 0);
        }
        W32(0x976758u, o);
        r = o ? preset_apply(c, o, p) : 0;
    }
    return r;
}
static int hook_preset_fn(CPU *c)
{
    if (!d3d_native_on() || !PRESETS)
        return 0;
    return done(c, preset(c, (int32_t)R32(c->esp + 4)), 0);
}

/* 0x86a8c0 (thiscall (model, mode), ecx = a mesh: the static meshes, 70% of the town's draws):
   culled by its box (+0x6c: 8 corners, 0x410910); +0x74 the other kind (0x86ac00); +0x8c a vertex
   callback, else the mode's render preset (and the vertices lit once, 0x86a820: +0x60 says done);
   then its parts (+0x24 of them at +0x28, 0x14 each: kind (low 7 bits), -, material slot, -; the
   index count, first vertex, vertex count, indices; +0x2c their matrices' numbers, 0x10 each from
   +0xc, 0 = the world matrix it came with, n = that times +0x80's n-th) in three passes: kind 0,
   kind 1, then the rest (+0x68 marks the drawn; the third pass compares the kind with the vertex
   buffer handle, as the original's frame has it there). The engine's own work (the box test, the
   lighting, the matrix product 0x4225e5, the mode presets' objects) stays the translation's. */
static const uint32_t mesh_cases[8] = {0x86a993, 0x86a9a1, 0x86a9b7, 0x86a9c3, 0x86a9cf, 0x86a9f0, 0x86a9fc, 0x86aa08};
static int mesh_ok = -1;

static void mesh_light(CPU *c, uint32_t mesh)
{
    c->ecx = mesh;
    guest_call(c, 0x86a820u, 0);
}

static int hook_mesh_fn(CPU *c)
{
    if (!d3d_native_on() || !PRESETS)
        return 0;
    if (mesh_ok < 0) {
        mesh_ok = 1;
        for (int i = 0; i < 8; i++)
            mesh_ok &= R32(0x86abe0u + 4 * (uint32_t)i) == mesh_cases[i];
        if (!mesh_ok)
            LOG(LOG_ALWAYS, "native: 0x86a8c0's jump table is not as expected: left to the translation");
    }
    if (!mesh_ok)
        return 0;
    n_mesh++;
    uint32_t e = c->esp, mesh = c->ecx, model = R32(e + 4), mode = R32(e + 8), eax;
    uint32_t ebp = e - 4, l50 = ebp - 0x50, l90 = ebp - 0x90;   /* (its frame's matrices) */
    if ((eax = R32(0x979ca8u)) != 0)
        return done(c, eax, 8);
    if (!R32(mesh + 0xc))
        return done(c, 0, 8);
    c->esp = e - 0xa0;                  /* (below its frame, as its own calls are) */
    if (R32(mesh + 0x6c)) {
        uint32_t all = eax = guest_call(c, 0x410910u, 2, R32(mesh + 0x6c), 0);
        for (uint32_t o = 0xc; o < 0x60; o += 0xc)
            all &= eax = guest_call(c, 0x410910u, 2, R32(mesh + 0x6c) + o, 0);
        if (all) {
            c->esp = e;
            return done(c, eax, 8);
        }
    }
    uint32_t mats = R32(mesh + 0x2c);
    if (mats && (int32_t)R32(mats + 0xc) > 0)
        world_get(c, l50);
    if (R32(mesh + 0x74)) {
        c->ecx = mesh;
        eax = guest_call(c, 0x86ac00u, 2, model, mode);
        c->esp = e;
        return done(c, eax, 8);
    }
    uint32_t vbh = R32(R32(mesh + 0xc) + 4);
    if (R32(mesh + 0x8c)) {
        uint32_t vo = R32(mesh + 0xc);
        uint32_t p = vb_lock(c, vo, 0, c->esp - 4);
        uint32_t r = guest_call(c, R32(mesh + 0x8c), 3, p, R32(mesh + 4), 0);
        c->ecx = mesh;
        guest_call(c, 0x865b70u, 1, r);
        vb_unlock(c, R32(mesh + 0xc));
    } else {
        switch (mode) {
        case 0:
            if (!R32(mesh + 0x60)) {
                W32(mesh + 0x60, 1);
                mesh_light(c, mesh);
            }
            preset(c, 3);
            break;
        case 1: mesh_light(c, mesh); preset(c, 3); break;
        case 2: preset(c, 1); break;
        case 3: preset(c, 0x10); break;
        case 4:
            if (!R32(mesh + 0x60)) {
                W32(mesh + 0x60, 1);
                mesh_light(c, mesh);
            }
            preset(c, 0x12);
            break;
        case 5: preset(c, 0xd); break;
        case 6: preset(c, 0); break;
        case 7: preset(c, 0x1a); break;
        }
    }
    uint32_t n = R32(mesh + 0x24) & 0x3fffffffu, used = R32(mesh + 0x68);   /* (rep stosd) */
    for (uint32_t i = 0; i < n; i++)
        W32(used + 4 * i, 0);
    if (R32(mesh + 0x64)) {
        c->ecx = mesh;
        guest_call(c, 0x865bb0u, 0);
        guest_call(c, 0x86a7e0u, 1, 1);
        vbh = R32(R32(mesh + 0xc) + 4);
    }
    const uint32_t kinds[3] = {0, 1, vbh};
    W32(0x97a108u, model);              /* (0x40dd70: the materials' current model) */
    uint32_t cur = 0, dev = DEVICE;
    for (int pass = 0; pass < 3; pass++) {
        uint32_t p = R32(mesh + 0x28), mp = R32(mesh + 0x2c) + 0xc;
        for (int32_t i = 0; i < (int32_t)R32(mesh + 0x24); i++, p += 0x14, mp += mp != 0xc ? 0x10 : 0) {
            uint32_t cnt = R32(p + 0xc);
            if ((int32_t)cnt <= 0)
                continue;
            if (cnt == 1)
                fatal("int3 at 0086aabf (a mesh part of one vertex)");
            if (!((pass == 2 && !R32(R32(mesh + 0x68) + 4 * (uint32_t)i)) || (R8(p) & 0x7f) == kinds[pass]))
                continue;
            W32(R32(mesh + 0x68) + 4 * (uint32_t)i, 1);
            if ((R8(p) & 0x7f) <= 1)
                cur = 0;
            material(c, 0x97a108u, 0, (uint32_t)(int32_t)(int8_t)R8(p + 2));
            if (mp != 0xc) {
                uint32_t m = R32(mp);
                if (m != cur) {
                    if (!m)
                        world_set(c, l50);
                    else {
                        guest_call(c, 0x4225e5u, 3, l90, l50, R32(mesh + 0x80) + (m << 6) - 0x40);
                        world_set(c, l90);
                    }
                }
                cur = R32(mp);
            }
            hr_check(d3dn_draw_ivb(c, dev, 4, vbh, R32(p + 8), R32(p + 0xc), R32(p + 0x10), R32(p + 4), 0));
        }
    }
    eax = 0;
    if (R32(mesh + 0x64)) {
        c->ecx = mesh;
        guest_call(c, 0x865cb0u, 0);
        eax = guest_call(c, 0x86a7e0u, 1, 0);
    }
    c->esp = e;
    return done(c, eax, 8);
}

/* 0x86ac00 (thiscall (model, mode), ecx = a group of meshes: +0x70 of them at +0x74, sharing
   the parts table's layout: +0x24 parts): each mesh culled by its box (a flag each, kept in the
   original's frame: a mesh without a box leaves its flag as the stack had it: logged), then the
   mode's preset (mode 0 on a mesh not yet lit: lit, no preset: as the original), or the group's
   vertex callback; then part by part over the meshes not culled, each part with a count drawn
   in its material. */
static const uint32_t group_cases[6] = {0x86ac7d, 0x86ac94, 0x86aca7, 0x86acb3, 0x86acbf, 0x86accb};
static int group_ok = -1, group_nobox;

static int hook_mesh_group_fn(CPU *c)
{
    if (!d3d_native_on() || !PRESETS)
        return 0;
    if (group_ok < 0) {
        group_ok = 1;
        for (int i = 0; i < 6; i++)
            group_ok &= R32(0x86addcu + 4 * (uint32_t)i) == group_cases[i];
        if (!group_ok)
            LOG(LOG_ALWAYS, "native: 0x86ac00's jump table is not as expected: left to the translation");
    }
    if (!group_ok)
        return 0;
    n_group++;
    uint32_t e = c->esp, grp = c->ecx, model = R32(e + 4), mode = R32(e + 8);
    uint32_t flags = e - 0x400;         /* (its frame's culled flags) */
    c->esp = e - 0x418;
    for (int32_t k = 0; k < (int32_t)R32(grp + 0x70); k++) {
        uint32_t m = R32(R32(grp + 0x74) + 4 * (uint32_t)k), box = R32(m + 0x6c);
        if (box) {
            uint32_t all = guest_call(c, 0x410910u, 2, box, 0);
            W32(flags + 4 * (uint32_t)k, all);
            for (uint32_t o = 0xc; o < 0x60; o += 0xc) {
                all &= guest_call(c, 0x410910u, 2, R32(m + 0x6c) + o, 0);
                W32(flags + 4 * (uint32_t)k, all);
            }
        } else if (!group_nobox++)
            LOG(LOG_ALWAYS, "native: 0x86ac00: a mesh without a box (its culled flag is the stack's)");
        if (R32(grp + 0x8c)) {
            uint32_t p = vb_lock(c, R32(m + 0xc), 0, c->esp - 4);
            uint32_t r = guest_call(c, R32(grp + 0x8c), 3, p, R32(grp + 4), 0);
            c->ecx = grp;
            guest_call(c, 0x865b70u, 1, r);
            vb_unlock(c, R32(m + 0xc));
            continue;
        }
        switch (mode) {
        case 0:
            if (!R32(m + 0x60)) {
                W32(m + 0x60, 1);
                mesh_light(c, m);
            } else
                preset(c, 3);
            break;
        case 1: mesh_light(c, m); preset(c, 3); break;
        case 2: preset(c, 1); break;
        case 3: preset(c, 0x10); break;
        case 4: preset(c, 0x12); break;
        case 5: preset(c, 0xd); break;
        }
    }
    W32(0x97a108u, model);
    uint32_t dev = DEVICE, j = 0;
    for (; (int32_t)j < (int32_t)R32(grp + 0x24); j++)
        for (int32_t k = 0; k < (int32_t)R32(grp + 0x70); k++) {
            uint32_t m = R32(R32(grp + 0x74) + 4 * (uint32_t)k), vo = R32(m + 0xc);
            if (!vo || R32(flags + 4 * (uint32_t)k))
                continue;
            uint32_t vbh = R32(vo + 4), p = R32(m + 0x28) + j * 0x14;
            if (!R32(p + 0xc))
                continue;
            material(c, 0x97a108u, 0, (uint32_t)(int32_t)(int8_t)R8(p + 2));
            hr_check(d3dn_draw_ivb(c, dev, 4, vbh, R32(p + 8), R32(p + 0xc), R32(p + 0x10), R32(p + 4), 0));
        }
    c->esp = e;
    return done(c, (int32_t)R32(grp + 0x24) > 0 ? j : R32(grp + 0x24), 8);
}

/* 0x867af0 (cdecl (model)): the engine's batch buffer (0x5d51c30, filled during the frame, still
   locked): unlocked and, with vertices in it (0x5de1c40 of them, 0x5d51c24 indices at
   0x5de1c4c), drawn in material slot 0, preset 5, at the world matrix 0x5d51be0 */
static int hook_batch_fn(CPU *c)
{
    if (!d3d_native_on() || !PRESETS)
        return 0;
    uint32_t model = R32(c->esp + 4), vo = R32(0x5d51c30u);
    if ((int32_t)R32(0x5de1c40u) <= 0)
        return done(c, vb_unlock(c, vo), 0);
    W32(0x97a108u, model);
    uint32_t e = c->esp;
    c->esp = e - 4;                     /* (below its saved register) */
    material(c, 0x97a108u, 0, 0);
    preset(c, 5);
    vb_unlock(c, R32(0x5d51c30u));
    uint32_t vbh = R32(R32(0x5d51c30u) + 4);
    world_set(c, 0x5d51be0u);
    uint32_t r = hr_check(d3dn_draw_ivb(c, DEVICE, 4, vbh, 0, R32(0x5de1c40u), R32(0x5de1c4cu), R32(0x5d51c24u), 0));
    c->esp = e;
    return done(c, r, 0);
}

/* for runtime/skin.c's draws (the skinned meshes): the same wrapper, and an indexed triangle
   list draw of a vertex buffer */
void native_material(CPU *c, uint32_t mgr, uint32_t stage, uint32_t slot) { material(c, mgr, stage, slot); }
void native_preset(CPU *c, int32_t p)
{
    if (PRESETS)
        preset(c, p);
    else
        guest_call(c, 0x407ba0u, 1, (uint32_t)p);     /* (no presets object yet: the original's way) */
}
void native_draw(CPU *c, uint32_t vb, uint32_t start, uint32_t n, uint32_t idx, uint32_t ni)
{
    hr_check(d3dn_draw_ivb(c, DEVICE, 4, vb, start, n, idx, ni, 0));
}

static void native_stats(void)
{
    if (n_wrap)
        LOG(LOG_ALWAYS, "native: hand-written calls %lu (static meshes %lu, mesh groups %lu, rigid meshes %lu)", n_wrap,
            n_mesh, n_group, n_char);
}

/* 0x86ae00 (thiscall (model, mode), ecx = a mesh: the characters' rigid meshes): as 0x86a8c0
   with one pass over its parts and no kinds: culled by its box; mode 2 nothing, 1 lit; otherwise
   its vertex buffer locked for the vertex callback (+0x8c) or preset 3, and unlocked */
static int hook_mesh_char_fn(CPU *c)
{
    if (!d3d_native_on() || !PRESETS)
        return 0;
    n_char++;
    uint32_t e = c->esp, mesh = c->ecx, model = R32(e + 4), mode = R32(e + 8), eax;
    uint32_t l20 = e - 0x80, l60 = e - 0x40;  /* (its frame's matrices) */
    uint32_t parts = R32(mesh + 0x28), mats = R32(mesh + 0x2c);
    if ((eax = R32(0x979ca8u)) != 0)
        return done(c, eax, 8);
    c->esp = e - 0xa0;
    if (R32(mesh + 0x6c)) {
        uint32_t all = eax = guest_call(c, 0x410910u, 2, R32(mesh + 0x6c), 0);
        for (uint32_t o = 0xc; o < 0x60; o += 0xc)
            all &= eax = guest_call(c, 0x410910u, 2, R32(mesh + 0x6c) + o, 0);
        if (all) {
            c->esp = e;
            return done(c, eax, 8);
        }
    }
    if (mats && (int32_t)R32(mats + 0xc) > 0)
        world_get(c, l20);
    uint32_t vbh = R32(R32(mesh + 0xc) + 4);
    if (mode == 1)
        mesh_light(c, mesh);
    else if (mode != 2) {
        uint32_t p = vb_lock(c, R32(mesh + 0xc), 0, c->esp - 4);
        if (R32(mesh + 0x8c)) {
            uint32_t r = guest_call(c, R32(mesh + 0x8c), 3, p, R32(mesh + 4), 0);
            c->ecx = mesh;
            guest_call(c, 0x865b70u, 1, r);
        } else
            preset(c, 3);
        vb_unlock(c, R32(mesh + 0xc));
    }
    W32(0x97a108u, model);
    uint32_t cur = 0, dev = DEVICE, mp = mats + 0xc, j = 0;
    for (; (int32_t)j < (int32_t)R32(mesh + 0x24); j++, mp += mp != 0xc ? 0x10 : 0) {
        uint32_t p = parts + j * 0x14;
        if ((int32_t)R32(p + 0xc) <= 0)
            continue;
        material(c, 0x97a108u, 0, (uint32_t)(int32_t)(int8_t)R8(p + 2));
        if (mp != 0xc) {
            uint32_t m = R32(mp);
            if (m != cur) {
                if (!m)
                    world_set(c, l20);
                else {
                    guest_call(c, 0x4225e5u, 3, l60, l20, R32(mesh + 0x80) + (m << 6) - 0x40);
                    world_set(c, l60);
                }
            }
            cur = R32(mp);
        }
        hr_check(d3dn_draw_ivb(c, dev, 4, vbh, R32(p + 8), R32(p + 0xc), R32(p + 0x10), R32(p + 4), 0));
    }
    c->esp = e;
    return done(c, (int32_t)R32(mesh + 0x24) > 0 ? j : R32(mesh + 0x24), 8);
}

void native_init(void)
{
    hook_mesh_char = hook_mesh_char_fn;
    hook_batch = hook_batch_fn;
    atexit(native_stats);
    hook_mesh_group = hook_mesh_group_fn;
    hook_mesh = hook_mesh_fn;
    hook_world_set = hook_world_set_fn;
    hook_world_get = hook_world_get_fn;
    hook_vb_lock = hook_vb_lock_fn;
    hook_vb_unlock = hook_vb_unlock_fn;
    hook_material = hook_material_fn;
    hook_preset = hook_preset_fn;
}
