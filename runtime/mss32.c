/* Miles Sound System 6 stand-in (mss32.dll): the 73 AIL functions LEGO Island 2 imports, played
   through the runtime's mixer (dsound.c's runtime voices, sound.h).

   What the game does (its sound object, 0x857f00-0x85b6c0; static initializer 0x429da0):
   - AIL_startup, AIL_set_redist_directory; AIL_waveOutOpen (one digital driver);
     AIL_enumerate_3D_providers, then AIL_open_3D_provider on the first of seven names it knows
     (EAX 2, EAX, DS3D hardware, DS3D 7+ software full/light HRTF or pan, DS3D emulation). Only
     with a 3D provider open does it go on: listener, "Maximum supported samples", its sample
     pools (2D and 3D handles), AIL_midiOutOpen and AIL_DLS_open (22050 Hz, 16 bit, stereo).
   - Sound effects: AIL_set_sample_file / AIL_set_3D_sample_file with WAV images in memory
     (22 kHz 16-bit PCM), volume 0-127, pan 0-127 (64 centre), playback rate, loop counts
     (0 = forever), status polling.
   - Speech and cutscene sound: AIL_open_stream on files (_data\sounds\speech\...: MP3 inside a
     WAV, 44.1 kHz mono), a callback when a stream ends.
   - Ambience: MIDI files (AIL_file_read, AIL_MIDI_to_XMI) played as sequences through DLS banks
     loaded with AIL_DLS_load_file (midi.c: TinySoundFont through dmusic's DLS reader). The game
     starts a sequence again whenever it is done (that is its loop).
   - AIL_get_DirectSound_info: the game hands its DirectSound object to DirectMusic; there is none
     (NULL), our DirectMusic needs none.

   Handles are small blocks in program memory ([magic][index]); the state is here. 3D samples are
   placed by the listener each time a position changes and once a frame: inverse-distance
   rolloff from the minimum distance, silent past the maximum, panned by the listener's right
   (left-handed, as Miles and DirectSound3D). */
#define _GNU_SOURCE
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "sound.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../third_party/dr_libs/dr_mp3.h"

#define LOGS(...) LOG(LOG_SOUND, __VA_ARGS__)

enum { SMP_FREE = 1, SMP_DONE = 2, SMP_PLAYING = 4, SMP_STOPPED = 8 };
enum { K_DRIVER = 1, K_SAMPLE, K_3DSAMPLE, K_STREAM, K_SEQUENCE, K_PROVIDER, K_LISTENER, K_MIDI, K_DLS };
#define MAGIC 0x53534d4cu                    /* "LMSS" */
#define MAXOBJ 1024

/* decoded sound, kept by the image it came from (the game sets the same images again and again) */
typedef struct Pcm {
    struct Pcm *next;
    uint32_t key_addr, key_size, key_sum;
    int16_t *data;
    uint32_t frames;
    int channels, rate;
} Pcm;

typedef struct Obj {
    int kind, used;
    uint32_t handle;                    /* the program's handle */
    /* samples and streams */
    int voice;                          /* runtime voice, -1 none */
    Pcm *pcm;                           /* (streams own theirs: not cached) */
    int status;
    int volume, pan;                    /* 0-127 */
    uint32_t rate;                      /* playback rate set (0: the file's) */
    int loops;                          /* 1 once, 0 forever */
    uint32_t callback;                  /* streams: called when it ends */
    int paused;
    /* 3D */
    float pos[3], front[3], up[3];
    float maxd, mind;
    char name[128];
    int midi;                           /* sequences: midi.c's sequence, -1 none */
} Obj;

static Obj objs[MAXOBJ];
static Pcm *pcm_cache;
static uint32_t redist_dir;
static int listener = -1;

/* ------------------------------------------------------------------ handles */
static uint32_t obj_new(int kind)
{
    for (int i = 1; i < MAXOBJ; i++)
        if (!objs[i].used) {
            Obj *o = &objs[i];
            uint32_t h = o->handle;
            memset(o, 0, sizeof *o);
            if (!h)
                h = galloc(64);
            memset(gptr(h), 0, 64);
            W32(h, MAGIC);
            W32(h + 4, (uint32_t)i);
            o->handle = h;
            o->used = 1;
            o->kind = kind;
            o->voice = -1;
            o->midi = -1;
            o->status = SMP_DONE;
            o->volume = 127;
            o->pan = 64;
            o->loops = 1;
            o->maxd = 100.0f;
            o->mind = 1.0f;
            o->front[2] = 1.0f;
            o->up[1] = 1.0f;
            return h;
        }
    fatal("Miles stand-in: out of handles");
}

static Obj *obj_get(uint32_t h, int kind)
{
    if (!h || R32(h) != MAGIC)
        return NULL;
    uint32_t i = R32(h + 4);
    if (i == 0 || i >= MAXOBJ || !objs[i].used || (kind && objs[i].kind != kind))
        return NULL;
    return &objs[i];
}

static float fl(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t fu(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* ------------------------------------------------------------------ decoding */
static uint32_t rd16(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static const int ima_index[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static const int ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
    80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
    544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
    2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static int16_t ima_nibble(int nib, int *pred, int *idx)
{
    int step = ima_step[*idx], diff = step >> 3;
    if (nib & 4) diff += step;
    if (nib & 2) diff += step >> 1;
    if (nib & 1) diff += step >> 2;
    *pred += nib & 8 ? -diff : diff;
    if (*pred > 32767) *pred = 32767;
    if (*pred < -32768) *pred = -32768;
    *idx += ima_index[nib];
    if (*idx < 0) *idx = 0;
    if (*idx > 88) *idx = 88;
    return (int16_t)*pred;
}

/* A WAV image (or raw MP3) to 16-bit PCM. Returns 0 if it is not something we know. */
static int decode(const uint8_t *p, uint32_t n, Pcm *out)
{
    memset(out, 0, sizeof *out);
    if (n >= 12 && !memcmp(p, "RIFF", 4) && !memcmp(p + 8, "WAVE", 4)) {
        const uint8_t *fmt = NULL, *data = NULL;
        uint32_t fmtn = 0, datan = 0;
        for (uint32_t o = 12; o + 8 <= n; ) {
            uint32_t sz = rd32(p + o + 4);
            if (!memcmp(p + o, "fmt ", 4)) { fmt = p + o + 8; fmtn = sz; }
            else if (!memcmp(p + o, "data", 4)) { data = p + o + 8; datan = sz; }
            o += 8 + sz + (sz & 1);
        }
        if (!fmt || !data || fmtn < 16)
            return 0;
        if (data + datan > p + n)
            datan = (uint32_t)(p + n - data);
        uint32_t tag = rd16(fmt), ch = rd16(fmt + 2), rate = rd32(fmt + 4), align = rd16(fmt + 12),
                 bits = rd16(fmt + 14);
        if (!ch || !rate)
            return 0;
        out->channels = (int)ch;
        out->rate = (int)rate;
        if (tag == 1 && (bits == 16 || bits == 8)) {
            uint32_t fr = datan / (ch * bits / 8);
            out->frames = fr;
            out->data = malloc((size_t)fr * ch * 2 + 2);
            for (uint32_t i = 0; i < fr * ch; i++)
                out->data[i] = bits == 16 ? (int16_t)rd16(data + 2 * i) : (int16_t)((data[i] - 128) << 8);
            return 1;
        }
        if (tag == 0x11 && align) {                     /* IMA ADPCM */
            uint32_t per = (align - 4 * ch) * 2 / ch + 1, blocks = datan / align;
            out->frames = per * blocks;
            out->data = malloc((size_t)out->frames * ch * 2 + 2);
            int16_t *d = out->data;
            for (uint32_t b = 0; b < blocks; b++) {
                const uint8_t *q = data + b * align;
                int pred[2], idx[2];
                for (uint32_t c = 0; c < ch && c < 2; c++) {
                    pred[c] = (int16_t)rd16(q + 4 * c);
                    idx[c] = q[4 * c + 2] > 88 ? 88 : q[4 * c + 2];
                    d[c] = (int16_t)pred[c];
                }
                d += ch;
                q += 4 * ch;
                for (uint32_t k = 0; k < (per - 1) / 8; k++)
                    for (uint32_t c = 0; c < ch && c < 2; c++) {
                        for (int j = 0; j < 4; j++) {
                            uint8_t v = q[j];
                            d[(2 * j) * ch + c] = ima_nibble(v & 15, &pred[c], &idx[c]);
                            d[(2 * j + 1) * ch + c] = ima_nibble(v >> 4, &pred[c], &idx[c]);
                        }
                        q += 4;
                        if (c + 1 == ch)
                            d += 8 * ch;
                    }
            }
            return 1;
        }
        if (tag == 0x55)                                /* MPEG layer 3 inside the WAV */
            return decode(data, datan, out);
        LOG(LOG_ALWAYS, "Miles stand-in: WAV format %u (%u bits) not supported", tag, bits);
        return 0;
    }
    /* raw MP3 */
    drmp3_config cfg;
    drmp3_uint64 frames;
    int16_t *pcm = drmp3_open_memory_and_read_pcm_frames_s16(p, n, &cfg, &frames, NULL);
    if (!pcm)
        return 0;
    out->data = pcm;
    out->frames = (uint32_t)frames;
    out->channels = (int)cfg.channels;
    out->rate = (int)cfg.sampleRate;
    return 1;
}

static uint32_t sum_of(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i += n > 4096 ? 61 : 1)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static uint32_t image_size(uint32_t img, int32_t block)
{
    if (block > 0)
        return (uint32_t)block;
    if (!memcmp(gptr(img), "RIFF", 4))
        return R32(img + 4) + 8;
    return 0;
}

static Pcm *pcm_for_image(uint32_t img, int32_t block)
{
    if (!img)                           /* (a sound the game failed to load) */
        return NULL;
    uint32_t n = image_size(img, block);
    if (!n)
        return NULL;
    uint32_t sum = sum_of((const uint8_t *)gptr(img), n);
    for (Pcm *c = pcm_cache; c; c = c->next)
        if (c->key_addr == img && c->key_size == n && c->key_sum == sum)
            return c;
    Pcm *c = calloc(1, sizeof *c);
    if (!decode((const uint8_t *)gptr(img), n, c)) {
        free(c);
        return NULL;
    }
    c->key_addr = img;
    c->key_size = n;
    c->key_sum = sum;
    c->next = pcm_cache;
    pcm_cache = c;
    return c;
}

/* ------------------------------------------------------------------ playing */
static void apply_gain(Obj *o);

static void set_pcm(Obj *o, Pcm *p)
{
    if (o->voice >= 0)
        voice_free(o->voice);
    o->voice = -1;
    o->pcm = p;
    if (p && p->frames) {
        o->voice = voice_new(p->data, p->frames, p->channels, p->rate);
        if (o->rate)
            voice_set_rate(o->voice, o->rate);
        apply_gain(o);
    }
    o->status = SMP_DONE;
}

/* the 3D listener's view of a sample: gain and the left/right balance */
static void place_3d(const Obj *o, float *g, float *bal)
{
    *g = 1.0f;
    *bal = 0.0f;
    const Obj *l = listener > 0 ? &objs[listener] : NULL;
    if (!l)
        return;
    float d[3] = {o->pos[0] - l->pos[0], o->pos[1] - l->pos[1], o->pos[2] - l->pos[2]};
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (o->maxd > 0 && dist > o->maxd) {
        *g = 0.0f;
        return;
    }
    float mind = o->mind > 0.001f ? o->mind : 0.001f;
    *g = dist > mind ? mind / dist : 1.0f;
    if (dist > 1e-4f) {
        const float *f = l->front, *u = l->up;
        float r[3] = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};
        float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        if (rl > 1e-6f)
            *bal = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / (rl * dist);
    }
}

static void apply_gain(Obj *o)
{
    if (o->voice < 0)
        return;
    float v = (float)(o->volume < 0 ? 0 : o->volume > 127 ? 127 : o->volume) / 127.0f;
    float p = (float)(o->pan < 0 ? 0 : o->pan > 127 ? 127 : o->pan) / 127.0f * 2.0f - 1.0f;   /* -1..1 */
    if (o->kind == K_3DSAMPLE) {
        float g, bal;
        place_3d(o, &g, &bal);
        v *= g;
        p = bal * 0.8f;                 /* (a 3D sound is never fully in one ear) */
    }
    float l = p > 0 ? 1.0f - p : 1.0f, r = p < 0 ? 1.0f + p : 1.0f;
    voice_set_gain(o->voice, v * l, v * r);
}

static void start(Obj *o)
{
    if (o->voice < 0) {
        o->status = SMP_DONE;
        return;
    }
    voice_rewind(o->voice);
    apply_gain(o);
    voice_play(o->voice, o->loops);
    o->status = SMP_PLAYING;
    o->paused = 0;
}

static int status_of(Obj *o)
{
    if (o->status == SMP_PLAYING && (o->voice < 0 || !voice_playing(o->voice, NULL)))
        o->status = SMP_DONE;
    return o->status;
}

static void stop(Obj *o)
{
    if (o->voice >= 0)
        voice_pause(o->voice);
    if (status_of(o) == SMP_PLAYING)
        o->status = SMP_STOPPED;
}

static void resume(Obj *o)
{
    if (o->status == SMP_STOPPED && o->voice >= 0) {
        voice_play(o->voice, o->loops);
        o->status = SMP_PLAYING;
    }
}

static void end(Obj *o)
{
    if (o->voice >= 0) {
        voice_pause(o->voice);
        voice_rewind(o->voice);
    }
    o->status = SMP_DONE;
}

static void release(Obj *o)
{
    if (o->voice >= 0)
        voice_free(o->voice);
    if (o->kind == K_STREAM && o->pcm) {
        free(o->pcm->data);
        free(o->pcm);
    }
    o->voice = -1;
    o->pcm = NULL;
    o->used = 0;
    W32(o->handle, 0);
}

/* the game thread, once a frame: streams that ended call back; 3D gains follow the listener */
void mss_frame(void)
{
    CPU *c = current_cpu();
    for (int i = 1; i < MAXOBJ; i++) {
        Obj *o = &objs[i];
        if (!o->used)
            continue;
        if (o->kind == K_3DSAMPLE && o->status == SMP_PLAYING)
            apply_gain(o);
        if (o->kind == K_STREAM && o->status == SMP_PLAYING && status_of(o) == SMP_DONE && o->callback && c) {
            LOGS("AIL stream %08x ended: callback %08x", o->handle, o->callback);
            guest_call(c, o->callback, 1, o->handle);
        }
    }
}

/* ------------------------------------------------------------------ start-up, drivers */
#define AIL(name, n) HOSTFN(w_MSS32__AIL_##name##_##n, (n) / 4)

AIL(startup, 0) { LOGS("AIL_startup"); return 1; }
AIL(shutdown, 0)
{
    LOGS("AIL_shutdown");
    for (int i = 1; i < MAXOBJ; i++)
        if (objs[i].used)
            release(&objs[i]);
    listener = -1;
    return 0;
}
AIL(set_redist_directory, 4)
{
    uint32_t old = redist_dir;
    if (ARG(0)) {
        if (redist_dir) gfree(redist_dir);
        redist_dir = gstrdup(gstr(ARG(0)));
    }
    LOGS("AIL_set_redist_directory(%s)", ARG(0) ? gstr(ARG(0)) : "NULL");
    return old ? old : redist_dir;
}
AIL(lock, 0) { return 0; }
AIL(unlock, 0) { return 0; }

/* (driver, &waveout, device id, format): 0 = it opened */
AIL(waveOutOpen, 16)
{
    uint32_t h = obj_new(K_DRIVER);
    if (ARG(0)) W32(ARG(0), h);
    if (ARG(1)) W32(ARG(1), 0);
    LOGS("AIL_waveOutOpen -> %08x", h);
    return 0;
}
AIL(waveOutClose, 4) { Obj *o = obj_get(ARG(0), K_DRIVER); if (o) release(o); return 0; }
AIL(midiOutOpen, 12)
{
    uint32_t h = obj_new(K_MIDI);
    if (ARG(0)) W32(ARG(0), h);
    if (ARG(1)) W32(ARG(1), 0);
    LOGS("AIL_midiOutOpen -> %08x", h);
    return 0;
}
AIL(midiOutClose, 4) { Obj *o = obj_get(ARG(0), K_MIDI); if (o) release(o); return 0; }
/* (sample, &IDirectSound, &IDirectSoundBuffer) */
AIL(get_DirectSound_info, 12)
{
    if (ARG(1)) W32(ARG(1), 0);
    if (ARG(2)) W32(ARG(2), 0);
    return 0;
}

/* ------------------------------------------------------------------ 2D samples */
AIL(allocate_sample_handle, 4) { uint32_t h = obj_new(K_SAMPLE); objs[R32(h + 4)].status = SMP_DONE; return h; }
AIL(release_sample_handle, 4) { Obj *o = obj_get(ARG(0), K_SAMPLE); if (o) release(o); return 0; }
AIL(init_sample, 4)
{
    Obj *o = obj_get(ARG(0), K_SAMPLE);
    if (o) {
        set_pcm(o, NULL);
        o->volume = 127; o->pan = 64; o->rate = 0; o->loops = 1;
    }
    return 0;
}
/* (sample, file image, block): 1 if it can play it */
AIL(set_sample_file, 12)
{
    Obj *o = obj_get(ARG(0), K_SAMPLE);
    if (!o)
        return 0;
    Pcm *p = pcm_for_image(ARG(1), (int32_t)ARG(2));
    set_pcm(o, p);
    LOGS("AIL_set_sample_file(%08x, %08x): %s", ARG(0), ARG(1), p ? "ok" : "not decoded");
    return p != NULL;
}
AIL(start_sample, 4) { Obj *o = obj_get(ARG(0), K_SAMPLE); if (o) start(o); return 0; }
AIL(stop_sample, 4) { Obj *o = obj_get(ARG(0), K_SAMPLE); if (o) stop(o); return 0; }
AIL(resume_sample, 4) { Obj *o = obj_get(ARG(0), K_SAMPLE); if (o) resume(o); return 0; }
AIL(end_sample, 4) { Obj *o = obj_get(ARG(0), K_SAMPLE); if (o) end(o); return 0; }
AIL(sample_status, 4) { Obj *o = obj_get(ARG(0), K_SAMPLE); return o ? (uint32_t)status_of(o) : SMP_FREE; }
AIL(set_sample_volume, 8) { Obj *o = obj_get(ARG(0), K_SAMPLE); if (o) { o->volume = (int)ARG(1); apply_gain(o); } return 0; }
AIL(set_sample_pan, 8) { Obj *o = obj_get(ARG(0), K_SAMPLE); if (o) { o->pan = (int)ARG(1); apply_gain(o); } return 0; }
AIL(set_sample_playback_rate, 8)
{
    Obj *o = obj_get(ARG(0), K_SAMPLE);
    if (o) { o->rate = ARG(1); if (o->voice >= 0 && o->rate) voice_set_rate(o->voice, o->rate); }
    return 0;
}
AIL(set_sample_loop_count, 8) { Obj *o = obj_get(ARG(0), K_SAMPLE); if (o) o->loops = (int)ARG(1); return 0; }
AIL(sample_loop_count, 4) { Obj *o = obj_get(ARG(0), K_SAMPLE); return o ? (uint32_t)o->loops : 0; }

/* ------------------------------------------------------------------ 3D */
static const char *const provider_names[] = {"DirectSound3D 7+ Software - Full HRTF"};
static uint32_t provider_handles[1], provider_strs[1];

/* (&next, &provider, &name): 1 while there are more */
AIL(enumerate_3D_providers, 12)
{
    uint32_t k = ARG(0) ? R32(ARG(0)) : 0;
    if (k >= sizeof provider_names / sizeof *provider_names)
        return 0;
    if (!provider_handles[k]) {
        provider_handles[k] = obj_new(K_PROVIDER);
        provider_strs[k] = gstrdup(provider_names[k]);
    }
    if (ARG(1)) W32(ARG(1), provider_handles[k]);
    if (ARG(2)) W32(ARG(2), provider_strs[k]);
    if (ARG(0)) W32(ARG(0), k + 1);
    return 1;
}
AIL(open_3D_provider, 4) { LOGS("AIL_open_3D_provider(%08x)", ARG(0)); return obj_get(ARG(0), K_PROVIDER) ? 0 : 1; }
AIL(close_3D_provider, 4) { return 0; }
/* (provider, name, &value) */
AIL(3D_provider_attribute, 12)
{
    const char *n = gstr(ARG(1));
    if (ARG(2)) W32(ARG(2), n && !strcasecmp(n, "Maximum supported samples") ? 64 : 0);
    return 0;
}
AIL(open_3D_listener, 4)
{
    uint32_t h = obj_new(K_LISTENER);
    listener = (int)R32(h + 4);
    return h;
}
AIL(allocate_3D_sample_handle, 4) { return obj_new(K_3DSAMPLE); }
AIL(release_3D_sample_handle, 4) { Obj *o = obj_get(ARG(0), K_3DSAMPLE); if (o) release(o); return 0; }
/* (sample, file image): 1 if it can play it */
AIL(set_3D_sample_file, 8)
{
    Obj *o = obj_get(ARG(0), K_3DSAMPLE);
    if (!o)
        return 0;
    Pcm *p = pcm_for_image(ARG(1), -1);
    uint32_t keep = o->rate;
    o->rate = 0;
    set_pcm(o, p);
    o->rate = keep;
    return p != NULL;
}
AIL(start_3D_sample, 4) { Obj *o = obj_get(ARG(0), K_3DSAMPLE); if (o) start(o); return 0; }
AIL(stop_3D_sample, 4) { Obj *o = obj_get(ARG(0), K_3DSAMPLE); if (o) stop(o); return 0; }
AIL(resume_3D_sample, 4) { Obj *o = obj_get(ARG(0), K_3DSAMPLE); if (o) resume(o); return 0; }
AIL(end_3D_sample, 4) { Obj *o = obj_get(ARG(0), K_3DSAMPLE); if (o) end(o); return 0; }
AIL(3D_sample_status, 4) { Obj *o = obj_get(ARG(0), K_3DSAMPLE); return o ? (uint32_t)status_of(o) : SMP_FREE; }
AIL(set_3D_sample_volume, 8) { Obj *o = obj_get(ARG(0), K_3DSAMPLE); if (o) { o->volume = (int)ARG(1); apply_gain(o); } return 0; }
AIL(set_3D_sample_playback_rate, 8)
{
    Obj *o = obj_get(ARG(0), K_3DSAMPLE);
    if (o) { o->rate = ARG(1); if (o->voice >= 0 && o->rate) voice_set_rate(o->voice, o->rate); }
    return 0;
}
AIL(set_3D_sample_loop_count, 8) { Obj *o = obj_get(ARG(0), K_3DSAMPLE); if (o) o->loops = (int)ARG(1); return 0; }
AIL(3D_sample_loop_count, 4) { Obj *o = obj_get(ARG(0), K_3DSAMPLE); return o ? (uint32_t)o->loops : 0; }
/* (sample, max, min) */
AIL(set_3D_sample_distances, 12)
{
    Obj *o = obj_get(ARG(0), K_3DSAMPLE);
    if (o) { o->maxd = fl(ARG(1)); o->mind = fl(ARG(2)); apply_gain(o); }
    return 0;
}
AIL(3D_sample_distances, 12)
{
    Obj *o = obj_get(ARG(0), K_3DSAMPLE);
    if (ARG(1)) W32(ARG(1), fu(o ? o->maxd : 0));
    if (ARG(2)) W32(ARG(2), fu(o ? o->mind : 0));
    return 0;
}
/* (object, x, y, z): a 3D sample or the listener */
AIL(set_3D_position, 16)
{
    Obj *o = obj_get(ARG(0), 0);
    if (o) {
        o->pos[0] = fl(ARG(1)); o->pos[1] = fl(ARG(2)); o->pos[2] = fl(ARG(3));
        if (o->kind == K_3DSAMPLE) apply_gain(o);
    }
    return 0;
}
AIL(3D_position, 16)
{
    Obj *o = obj_get(ARG(0), 0);
    for (int k = 0; k < 3; k++)
        if (ARG(1 + k)) W32(ARG(1 + k), fu(o ? o->pos[k] : 0));
    return 0;
}
/* (object, front x y z, up x y z) */
AIL(set_3D_orientation, 28)
{
    Obj *o = obj_get(ARG(0), 0);
    if (o)
        for (int k = 0; k < 3; k++) { o->front[k] = fl(ARG(1 + k)); o->up[k] = fl(ARG(4 + k)); }
    return 0;
}

/* ------------------------------------------------------------------ streams (whole files, decoded) */
static int load_file(const char *win, uint8_t **data, uint32_t *size)
{
    char host[1200];
    if (!win || !path_to_host(win, host, sizeof host, 0))
        return 0;
    FILE *f = fopen(host, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = malloc(n > 0 ? (size_t)n : 1);
    *size = fread(*data, 1, n > 0 ? (size_t)n : 0, f) == (size_t)n ? (uint32_t)n : 0;
    fclose(f);
    if (!*size) { free(*data); return 0; }
    return 1;
}

/* (driver, file name, stream memory): a stream, NULL if the file cannot be played */
AIL(open_stream, 12)
{
    const char *name = gstr(ARG(1));
    uint8_t *d;
    uint32_t n;
    if (!load_file(name, &d, &n)) {
        LOGS("AIL_open_stream(%s): no such file", name ? name : "NULL");
        return 0;
    }
    Pcm *p = calloc(1, sizeof *p);
    int ok = decode(d, n, p);
    free(d);
    if (!ok) {
        free(p);
        LOGS("AIL_open_stream(%s): cannot decode", name);
        return 0;
    }
    uint32_t h = obj_new(K_STREAM);
    Obj *o = obj_get(h, K_STREAM);
    set_pcm(o, p);
    snprintf(o->name, sizeof o->name, "%s", name);
    LOGS("AIL_open_stream(%s) -> %08x: %u frames, %d Hz", name, h, p->frames, p->rate);
    return h;
}
/* a line of speech is playing (a stream from sounds/speech: game.c, the keyboard's dialogue skip) */
int mss_speech_playing(void)
{
    for (int i = 0; i < MAXOBJ; i++)
        if (objs[i].used && objs[i].kind == K_STREAM && strcasestr(objs[i].name, "speech") &&
            status_of(&objs[i]) == SMP_PLAYING)
            return 1;
    return 0;
}
AIL(close_stream, 4) { Obj *o = obj_get(ARG(0), K_STREAM); if (o) release(o); return 0; }
AIL(start_stream, 4) { Obj *o = obj_get(ARG(0), K_STREAM); if (o) start(o); return 0; }
/* (stream, on/off) */
AIL(pause_stream, 8)
{
    Obj *o = obj_get(ARG(0), K_STREAM);
    if (o) {
        if (ARG(1)) stop(o);
        else resume(o);
    }
    return 0;
}
AIL(stream_status, 4) { Obj *o = obj_get(ARG(0), K_STREAM); return o ? (uint32_t)status_of(o) : SMP_FREE; }
AIL(set_stream_volume, 8) { Obj *o = obj_get(ARG(0), K_STREAM); if (o) { o->volume = (int)ARG(1); apply_gain(o); } return 0; }
AIL(set_stream_pan, 8) { Obj *o = obj_get(ARG(0), K_STREAM); if (o) { o->pan = (int)ARG(1); apply_gain(o); } return 0; }
AIL(set_stream_loop_count, 8) { Obj *o = obj_get(ARG(0), K_STREAM); if (o) o->loops = (int)ARG(1); return 0; }
/* (stream, callback): the old callback */
AIL(register_stream_callback, 8)
{
    Obj *o = obj_get(ARG(0), K_STREAM);
    uint32_t old = o ? o->callback : 0;
    if (o) o->callback = ARG(1);
    return old;
}
AIL(auto_service_stream, 8) { return 0; }

/* ------------------------------------------------------------------ files */
AIL(file_size, 4)
{
    uint8_t *d;
    uint32_t n;
    if (!load_file(gstr(ARG(0)), &d, &n)) {
        LOGS("AIL_file_size(%s): not found", gstr(ARG(0)) ? gstr(ARG(0)) : "NULL");
        return 0xffffffffu;
    }
    free(d);
    return n;
}
/* (file name, destination or NULL to allocate): the data, NULL on failure */
AIL(file_read, 8)
{
    uint8_t *d;
    uint32_t n;
    if (!load_file(gstr(ARG(0)), &d, &n)) {
        LOGS("AIL_file_read(%s): not found", gstr(ARG(0)) ? gstr(ARG(0)) : "NULL");
        return 0;
    }
    uint32_t dst = ARG(1);
    if (!dst || dst == 0xffffffffu)
        dst = galloc(n);
    memcpy(gptr(dst), d, n);
    free(d);
    return dst;
}
AIL(mem_free_lock, 4) { if (ARG(0)) gfree(ARG(0)); return 0; }

/* ------------------------------------------------------------------ MIDI and DLS (midi.c) */
int midi_bank_load(const void *data, size_t len);
int midi_seq_new(const void *smf, size_t len, int bank);
void midi_seq_free(int id);
void midi_seq_play(int id, int loops);
void midi_seq_pause(int id, int pause);
void midi_seq_end(int id);
int midi_seq_state(int id);
void midi_seq_gain(int id, float g);

/* a sequence's volume (0-127) scales its channels' volume controllers: their DLS curve, squared */
static float seq_gain(int v) { float g = (v < 0 ? 0 : v > 127 ? 127 : v) / 127.0f; return g * g; }

/* (MIDI, size, &XMIDI, &XMIDI size, flags): the data kept as it is (we read standard MIDI) */
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* a standard MIDI file's length from its own chunks (MThd, MTrk...); 0 if it is not one */
static uint32_t smf_length(uint32_t m)
{
    const uint8_t *p = (const uint8_t *)gptr(m);
    if (memcmp(p, "MThd", 4))
        return 0;
    uint32_t o = 0, tracks = (uint32_t)(p[10] << 8 | p[11]);
    for (uint32_t k = 0; k <= tracks && k < 1024; k++) {
        uint32_t len = be32(p + o + 4);
        if (len > (16u << 20))
            return 0;
        o += 8 + len;
    }
    return o;
}

AIL(MIDI_to_XMI, 20)
{
    /* the size can be wrong: the game passes AIL_file_size of a name it never formatted
       ("midiList[i].midiName", 0x859a28), -1; Miles reads the file's own chunk lengths */
    if (!ARG(0))                        /* (AIL_file_read failed: no such file) */
        return 0;
    uint32_t n = ARG(1), real = smf_length(ARG(0));
    if (real && (n > real || n == 0))
        n = real;
    if (n > (16u << 20))
        return 0;
    uint32_t d = galloc(n ? n : 1);
    memcpy(gptr(d), gptr(ARG(0)), n);
    if (ARG(2)) W32(ARG(2), d);
    if (ARG(3)) W32(ARG(3), n);
    return 1;
}
/* (midi driver, digital driver, library, flags, rate, bits, channels) */
AIL(DLS_open, 28) { LOGS("AIL_DLS_open(%u Hz)", ARG(4)); return obj_new(K_DLS); }
AIL(DLS_close, 8) { Obj *o = obj_get(ARG(0), K_DLS); if (o) release(o); return 0; }
/* (dls, file name, flags): a file id, 0 on failure. Sequences prepared after it play through it
   (the game loads each ambience's collection just before its MIDI file); the same file loaded
   again is the same bank. */
static int dls_last = -1;
AIL(DLS_load_file, 12)
{
    static struct { char name[260]; int bank; } loaded[16];
    static int nloaded;
    const char *name = gstr(ARG(1));
    LOGS("AIL_DLS_load_file(%s)", name ? name : "NULL");
    if (!name) return 0;
    for (int i = 0; i < nloaded; i++)
        if (!strcasecmp(loaded[i].name, name)) {
            dls_last = loaded[i].bank;
            return (uint32_t)i + 1;
        }
    uint8_t *d;
    uint32_t n;
    if (!load_file(name, &d, &n)) {
        LOGS("AIL_DLS_load_file(%s): not found", name);
        return 0;
    }
    int b = midi_bank_load(d, n);
    free(d);
    if (b < 0 || nloaded >= 16) {
        LOGS("AIL_DLS_load_file(%s): not a DLS collection", name);
        return 0;
    }
    snprintf(loaded[nloaded].name, sizeof loaded[nloaded].name, "%s", name);
    loaded[nloaded].bank = b;
    dls_last = b;
    return (uint32_t)++nloaded;
}
AIL(allocate_sequence_handle, 4) { return obj_new(K_SEQUENCE); }
AIL(release_sequence_handle, 4)
{
    Obj *o = obj_get(ARG(0), K_SEQUENCE);
    if (o) {
        if (o->midi >= 0) midi_seq_free(o->midi);
        o->midi = -1;
        release(o);
    }
    return 0;
}
/* (sequence, XMIDI, sequence number): 1 = ready */
AIL(init_sequence, 12)
{
    Obj *o = obj_get(ARG(0), K_SEQUENCE);
    if (!o) return 0;
    if (o->midi >= 0) midi_seq_free(o->midi);
    uint32_t n = ARG(1) ? smf_length(ARG(1)) : 0;
    o->midi = n ? midi_seq_new(gptr(ARG(1)), n, dls_last) : -1;
    o->status = SMP_DONE;
    LOGS("AIL_init_sequence: %u bytes, bank %d -> %d", n, dls_last, o->midi);
    midi_seq_gain(o->midi, seq_gain(o->volume));
    return 1;
}
AIL(start_sequence, 4)
{
    Obj *o = obj_get(ARG(0), K_SEQUENCE);
    if (o) {
        o->status = SMP_PLAYING;
        LOGS("AIL_start_sequence(%d) volume %d", o->midi, o->volume);
        if (o->midi >= 0) midi_seq_play(o->midi, o->loops);
    }
    return 0;
}
AIL(stop_sequence, 4)
{
    Obj *o = obj_get(ARG(0), K_SEQUENCE);
    if (o && o->status == SMP_PLAYING) {
        o->status = SMP_STOPPED;
        if (o->midi >= 0) midi_seq_pause(o->midi, 1);
    }
    return 0;
}
AIL(resume_sequence, 4)
{
    Obj *o = obj_get(ARG(0), K_SEQUENCE);
    if (o && o->status == SMP_STOPPED) {
        o->status = SMP_PLAYING;
        if (o->midi >= 0) midi_seq_pause(o->midi, 0);
    }
    return 0;
}
AIL(end_sequence, 4)
{
    Obj *o = obj_get(ARG(0), K_SEQUENCE);
    if (o) {
        o->status = SMP_DONE;
        if (o->midi >= 0) midi_seq_end(o->midi);
    }
    return 0;
}
/* a playing sequence is done once midi.c has played it through (with no sound output: never) */
AIL(sequence_status, 4)
{
    Obj *o = obj_get(ARG(0), K_SEQUENCE);
    if (!o) return SMP_FREE;
    if (o->status == SMP_PLAYING && (o->midi < 0 || midi_seq_state(o->midi) < 0))
        o->status = SMP_DONE;
    return (uint32_t)o->status;
}
/* (sequence, volume 0-127, milliseconds) */
AIL(set_sequence_volume, 12)
{
    Obj *o = obj_get(ARG(0), K_SEQUENCE);
    if (o) {
        o->volume = (int)ARG(1);
        if (o->midi >= 0) midi_seq_gain(o->midi, seq_gain(o->volume));
    }
    return 0;
}
AIL(sequence_volume, 4) { Obj *o = obj_get(ARG(0), K_SEQUENCE); return o ? (uint32_t)o->volume : 0; }
