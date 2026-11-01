/* MIDI sequences through DLS banks, for the Miles stand-in's ambience (mss32.c: the birds and
   seagulls of the town, the fishing, T-Rex, castle and asteroid ambiences: a standard MIDI file
   each, with the DLS collection beside it). A bank is turned into a TinySoundFont synthesizer by
   GothicKit dmusic's DLS reader (third_party/dmusic: the same as the DirectMusic music); each
   sequence plays on its own copy of its bank's synthesizer (they share the samples), so two
   sequences' channels never meet. Rendered as a mixer source (sound.h), 64 frames at a time. */
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "_Internal.h"
#include "runtime.h"
#include "sound.h"

#define LOGS(...) LOG(LOG_SOUND, __VA_ARGS__)

typedef struct {
    DmDls *dls;
    tsf *f;
} Bank;

typedef struct {
    double t;                           /* seconds from the start */
    uint8_t st, d1, d2;
} Ev;

typedef struct {
    int used, playing, ended;
    Ev *ev;
    int nev, next;
    double length, pos;                 /* seconds */
    int loops;                          /* passes left (0 = forever) */
    float gain;
    int bank;
    tsf *f;                             /* this sequence's copy of its bank's synthesizer */
    int rate;
    uint8_t msb[16], lsb[16];
} Seq;

#define MAXBANKS 16
#define MAXSEQ 16
static Bank banks[MAXBANKS];
static Seq seqs[MAXSEQ];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int source_on;

/* ------------------------------------------------------------------ banks */
int midi_bank_load(const void *data, size_t len)
{
    int b = -1;
    for (int i = 0; i < MAXBANKS; i++)
        if (!banks[i].f) { b = i; break; }
    if (b < 0) return -1;
    void *copy = Dm_alloc(len);         /* (the DLS reader keeps it, and frees it with the bank) */
    if (!copy) return -1;
    memcpy(copy, data, len);
    DmDls *dls = NULL;
    tsf *f = NULL;
    if (DmDls_create(&dls) != DmResult_SUCCESS) {
        Dm_free(copy);
        return -1;
    }
    if (DmDls_parse(dls, copy, len) != DmResult_SUCCESS || DmSynth_createTsfForDls(dls, &f) != DmResult_SUCCESS ||
        !f) {
        if (dls->backing_memory != copy) Dm_free(copy);
        DmDls_release(dls);
        return -1;
    }
    pthread_mutex_lock(&lock);
    banks[b].dls = dls;
    banks[b].f = f;
    pthread_mutex_unlock(&lock);
    LOGS("MIDI: DLS bank %d, %u instruments", b, (unsigned)dls->instrument_count);
    return b;
}

/* ------------------------------------------------------------------ standard MIDI files */
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static uint32_t vlq(const uint8_t *p, size_t n, size_t *o)
{
    uint32_t v = 0;
    for (int k = 0; k < 4 && *o < n; k++) {
        uint8_t c = p[(*o)++];
        v = v << 7 | (c & 0x7f);
        if (!(c & 0x80)) break;
    }
    return v;
}

typedef struct { uint64_t tick; uint32_t order; uint8_t st, d1, d2; uint32_t tempo; } Raw;

static int raw_cmp(const void *a, const void *b)
{
    const Raw *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

/* every track's events merged in time, ticks turned into seconds through the tempo map */
static int smf_parse(const uint8_t *p, size_t n, Ev **out, double *length)
{
    if (n < 14 || memcmp(p, "MThd", 4)) return -1;
    uint32_t hl = be32(p + 4);
    int ntrk = p[10] << 8 | p[11], div = p[12] << 8 | p[13];
    if (div <= 0 || div & 0x8000) return -1;            /* (SMPTE time: not used here) */
    size_t o = 8 + hl;
    Raw *r = NULL;
    size_t nr = 0, cap = 0;
    uint32_t order = 0;
    uint64_t end_tick = 0;
    for (int t = 0; t < ntrk && o + 8 <= n; t++) {
        if (memcmp(p + o, "MTrk", 4)) break;
        size_t len = be32(p + o + 4), e = o + 8 + len;
        if (e > n) e = n;
        size_t q = o + 8;
        uint64_t tick = 0;
        uint8_t run = 0;
        while (q < e) {
            tick += vlq(p, e, &q);
            if (q >= e) break;
            uint8_t st = p[q];
            if (st & 0x80) q++;
            else st = run;
            Raw ev = {tick, order++, st, 0, 0, 0};
            if (st == 0xff) {
                if (q >= e) break;
                uint8_t type = p[q++];
                uint32_t ml = vlq(p, e, &q);
                if (type == 0x51 && ml >= 3 && q + 3 <= e) {
                    ev.tempo = (uint32_t)p[q] << 16 | p[q + 1] << 8 | p[q + 2];
                } else {
                    q += ml;
                    if (type == 0x2f) break;
                    continue;
                }
                q += ml;
            } else if (st == 0xf0 || st == 0xf7) {
                q += vlq(p, e, &q);
                continue;
            } else if (st >= 0x80) {
                run = st;
                int two = (st & 0xf0) != 0xc0 && (st & 0xf0) != 0xd0;
                if (q >= e) break;
                ev.d1 = p[q++];
                if (two) { if (q >= e) break; ev.d2 = p[q++]; }
            } else {
                break;                  /* running status with no status yet */
            }
            if (nr == cap) {
                cap = cap ? cap * 2 : 256;
                Raw *g = realloc(r, cap * sizeof *r);
                if (!g) { free(r); return -1; }
                r = g;
            }
            r[nr++] = ev;
        }
        if (tick > end_tick) end_tick = tick;
        o = o + 8 + len;
    }
    qsort(r, nr, sizeof *r, raw_cmp);
    Ev *ev = malloc((nr ? nr : 1) * sizeof *ev);
    if (!ev) { free(r); return -1; }
    int ne = 0;
    double sec = 0, us_per_q = 500000;
    uint64_t last = 0;
    for (size_t i = 0; i < nr; i++) {
        sec += (double)(r[i].tick - last) * us_per_q / 1e6 / div;
        last = r[i].tick;
        if (r[i].st == 0xff) {
            us_per_q = r[i].tempo ? r[i].tempo : us_per_q;
            continue;
        }
        ev[ne++] = (Ev){sec, r[i].st, r[i].d1, r[i].d2};
    }
    sec += (double)(end_tick - last) * us_per_q / 1e6 / div;
    free(r);
    *out = ev;
    *length = sec;
    return ne;
}

/* ------------------------------------------------------------------ sequences */
static void seq_preset(Seq *s, int ch, int prog)
{
    int drums = ch == 9;
    int bank = (s->msb[ch] << 8 | s->lsb[ch]) | (drums ? 0x4000 : 0);
    if (tsf_channel_set_bank_preset(s->f, ch, bank, prog)) return;
    if (tsf_channel_set_bank_preset(s->f, ch, drums ? 0x4000 : 0, prog)) return;
    tsf_channel_set_bank_preset(s->f, ch, bank, 0);
}

static void seq_reset(Seq *s)
{
    tsf_reset(s->f);
    for (int ch = 0; ch < 16; ch++) {
        s->msb[ch] = s->lsb[ch] = 0;
        seq_preset(s, ch, 0);
    }
    s->next = 0;
    s->pos = 0;
}

int midi_seq_new(const void *smf, size_t len, int bank)
{
    if (bank < 0 || bank >= MAXBANKS || !banks[bank].f) return -1;
    Ev *ev;
    double length;
    int ne = smf_parse(smf, len, &ev, &length);
    if (ne < 0) return -1;
    pthread_mutex_lock(&lock);
    int id = -1;
    for (int i = 0; i < MAXSEQ; i++)
        if (!seqs[i].used) { id = i; break; }
    if (id >= 0) {
        Seq *s = &seqs[id];
        memset(s, 0, sizeof *s);
        s->used = 1;
        s->ev = ev;
        s->nev = ne;
        s->length = length > 0 ? length : 0.001;
        s->bank = bank;
        s->gain = 1;
        s->f = tsf_copy(banks[bank].f);
        if (!s->f) { s->used = 0; id = -1; }
    }
    if (id < 0) free(ev);
    if (!source_on) {
        extern void midi_source(float *out, int frames, int rate, void *ctx);
        sound_add_source(midi_source, NULL);
        source_on = 1;
    }
    pthread_mutex_unlock(&lock);
    LOGS("MIDI: sequence %d: %d events, %.1f s, bank %d", id, ne, length, bank);
    return id;
}

static Seq *seq(int id) { return id >= 0 && id < MAXSEQ && seqs[id].used ? &seqs[id] : NULL; }

void midi_seq_free(int id)
{
    pthread_mutex_lock(&lock);
    Seq *s = seq(id);
    if (s) {
        tsf_close(s->f);
        free(s->ev);
        memset(s, 0, sizeof *s);
    }
    pthread_mutex_unlock(&lock);
}

/* from the start: loops = passes (0 = forever) */
void midi_seq_play(int id, int loops)
{
    pthread_mutex_lock(&lock);
    Seq *s = seq(id);
    if (s) {
        if (s->rate) seq_reset(s);
        else s->next = 0, s->pos = 0;
        s->loops = loops;
        s->playing = 1;
        s->ended = 0;
    }
    pthread_mutex_unlock(&lock);
}

void midi_seq_pause(int id, int pause)
{
    pthread_mutex_lock(&lock);
    Seq *s = seq(id);
    if (s && !s->ended) {
        s->playing = !pause;
        if (pause && s->rate) tsf_note_off_all(s->f);
    }
    pthread_mutex_unlock(&lock);
}

void midi_seq_end(int id)
{
    pthread_mutex_lock(&lock);
    Seq *s = seq(id);
    if (s) {
        s->playing = 0;
        s->ended = 1;
        if (s->rate) tsf_note_off_all(s->f);
    }
    pthread_mutex_unlock(&lock);
}

/* 1 playing, 0 stopped (paused), -1 ended (or never started) */
int midi_seq_state(int id)
{
    pthread_mutex_lock(&lock);
    Seq *s = seq(id);
    int r = !s ? -1 : s->playing ? 1 : s->ended || s->next == 0 ? -1 : 0;
    pthread_mutex_unlock(&lock);
    return r;
}

void midi_seq_gain(int id, float g)
{
    pthread_mutex_lock(&lock);
    Seq *s = seq(id);
    if (s) s->gain = g;
    pthread_mutex_unlock(&lock);
}

static void seq_event(Seq *s, const Ev *e)
{
    int ch = e->st & 0x0f;
    switch (e->st & 0xf0) {
    case 0x80: tsf_channel_note_off(s->f, ch, e->d1); break;
    case 0x90:
        if (e->d2) tsf_channel_note_on(s->f, ch, e->d1, e->d2 / 127.0f);
        else tsf_channel_note_off(s->f, ch, e->d1);
        break;
    case 0xb0:
        if (e->d1 == 0) s->msb[ch] = e->d2;
        else if (e->d1 == 32) s->lsb[ch] = e->d2;
        else tsf_channel_midi_control(s->f, ch, e->d1, e->d2);
        break;
    case 0xc0: seq_preset(s, ch, e->d1); break;
    case 0xe0: tsf_channel_set_pitchwheel(s->f, ch, e->d1 | e->d2 << 7); break;
    }
}

/* the mixer's source: every playing sequence added into out */
void midi_source(float *out, int frames, int rate, void *ctx)
{
    (void)ctx;
    static float buf[64 * 2];
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAXSEQ; i++) {
        Seq *s = &seqs[i];
        if (!s->used || !s->playing) continue;
        if (s->rate != rate) {
            tsf_set_output(s->f, TSF_STEREO_INTERLEAVED, rate, 0);
            if (!s->rate) seq_reset(s);
            s->rate = rate;
        }
        for (int done = 0; done < frames && s->playing;) {
            int n = frames - done < 64 ? frames - done : 64;
            double end = s->pos + (double)n / rate;
            while (s->next < s->nev && s->ev[s->next].t < end)
                seq_event(s, &s->ev[s->next++]);
            tsf_render_float(s->f, buf, n, 0, 1);
            float g = s->gain * 0.5f;           /* (headroom, as the music's) */
            for (int k = 0; k < n * 2; k++)
                out[done * 2 + k] += buf[k] * g;
            s->pos = end;
            done += n;
            if (s->pos >= s->length && s->next >= s->nev) {
                if (s->loops == 1) {
                    s->playing = 0;
                    s->ended = 1;
                    tsf_note_off_all(s->f);
                } else {
                    if (s->loops > 1) s->loops--;
                    s->next = 0;
                    s->pos = 0;
                }
            }
        }
    }
    pthread_mutex_unlock(&lock);
}
