/* DirectMusic stand-in (DirectX 6/7 interfaces, created through CoCreateInstance): the game's
   music. The objects are ours; the music itself is played by GothicKit's dmusic engine
   (third_party/dmusic, a patched copy: styles, bands, DLS instruments through
   TinySoundFont), rendered on the mixing thread as a streaming source (sound.h).

   What the game does (DirectMusic.cpp, 0x85a1e3-0x85b490; music per area, table 0x96ed78):
   - IDirectMusic2: GetDefaultPort, CreatePort, the port's GetCaps (wants an output-class DLS
     port, else EnumPort for one), SetDirectSound (with Miles' object: NULL here), Activate.
   - IDirectMusicPerformance2: Init, SetGlobalParam(AutoDownload / MasterVolume / MasterTempo),
     GetGlobalParam, AddPort, AssignPChannelBlock, AddNotificationType(segment), Invalidate,
     PlaySegment, Stop, CloseDown.
   - IDirectMusicLoader: SetSearchDirectory(all types, the area's folder), GetObject of a
     segment (file name) and of a DLS collection (file name, else the default GM set).
   - IDirectMusicSegment2::SetParam(GUID_ConnectToDLSCollection, collection).
   Every method we have no use for answers S_OK and logs once (LOG_SOUND).

   Files: segments, styles and DLS banks are found in the search folder first, then anywhere
   under _data\sounds\DirectMusic (the game's areas share styles), then gm.dls (the General MIDI
   set Windows has, from the user folder or the game folder) for the default collection. Every
   .dls in the search folder is loaded before a segment, as DirectMusic's scanning loader finds
   instruments referenced only by GUID. */
#define _GNU_SOURCE
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dmusic.h>
#include "runtime.h"
#include "sound.h"

#define LOGS(...) LOG(LOG_SOUND, __VA_ARGS__)
#define S_OK 0u
#define S_FALSE 1u
#define E_NOINTERFACE 0x80004002u
#define E_FAIL 0x80004005u
#define E_POINTER 0x80004003u
#define DMUS_E_LOADER_FAILEDOPEN 0x88781185u
#define DMUS_E_NOT_FOUND 0x88781193u

/* ------------------------------------------------------------------ GUIDs */
typedef struct { uint32_t d1; uint16_t d2, d3; uint8_t d4[8]; } G;
#define DMG(x) {x, 0xb39b, 0x11d1, {0x87, 0x04, 0x00, 0x60, 0x08, 0x93, 0xb1, 0xbd}}
static const G CLSID_DirectMusic = {0x636b9f10, 0x0c7d, 0x11d1, {0x95, 0xb2, 0x00, 0x20, 0xaf, 0xdc, 0x74, 0x21}};
static const G CLSID_DirectMusicPerformance = DMG(0xd2ac2881);
static const G CLSID_DirectMusicLoader = DMG(0xd2ac2892);
static const G CLSID_DirectMusicSegment = DMG(0xd2ac2882);
static const G CLSID_DirectMusicCollection = {0x480ff4b0, 0x28b2, 0x11d1, {0xbe, 0xf7, 0x00, 0xc0, 0x4f, 0xbf, 0x8f, 0xef}};
static const G GUID_PerfMasterVolume = DMG(0xd2ac28b1);
static const G GUID_ConnectToDLSCollection = {0x1db1ae6b, 0xe92e, 0x11d1, {0xa8, 0xc5, 0x00, 0xc0, 0x4f, 0xa3, 0x72, 0x6e}};

static int is(uint32_t a, const G *g) { return a && guid_eq(a, g->d1, g->d2, g->d3, g->d4); }

/* ------------------------------------------------------------------ the engine */
static pthread_mutex_t mlock = PTHREAD_MUTEX_INITIALIZER;
static DmLoader *dm_loader;
static DmPerformance *dm_perf;          /* made by the mixing thread at its rate */
static int perf_rate;
static float master_gain = 1.0f;
static uint64_t rendered;               /* frames rendered so far */
/* what the game asked for since the mixer last looked */
static DmSegment *pending_seg;
static int pending_play, pending_timing;
static char search_dir[1100];           /* host folder of SetSearchDirectory */
static char dm_root[1100];              /* host _data/sounds/DirectMusic */

static void *read_host(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *b = malloc(n > 0 ? (size_t)n : 1);
    *len = fread(b, 1, n > 0 ? (size_t)n : 0, f);
    fclose(f);
    return b;
}

/* a file named `base` (any case) in dir, or (deep) in its subfolders */
static int find_in(const char *dir, const char *base, int deep, char *out, size_t n)
{
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    int found = 0;
    char sub[64][256];
    int nsub = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        if (!strcasecmp(e->d_name, base)) {
            snprintf(out, n, "%s/%s", dir, e->d_name);
            found = 1;
            break;
        }
        if (deep && e->d_type == DT_DIR && nsub < 64)
            snprintf(sub[nsub++], sizeof sub[0], "%s", e->d_name);
    }
    closedir(d);
    for (int i = 0; !found && i < nsub; i++) {
        char p[1400];
        snprintf(p, sizeof p, "%s/%s", dir, sub[i]);
        found = find_in(p, base, 1, out, n);
    }
    return found;
}

/* dmusic's file lookups */
static void *resolve(void *ctx, char const *name, size_t *len)
{
    (void)ctx;
    const char *base = strrchr(name, '\\');
    base = base ? base + 1 : name;
    char path[1400];
    int ok = (search_dir[0] && find_in(search_dir, base, 0, path, sizeof path)) ||
             (dm_root[0] && find_in(dm_root, base, 1, path, sizeof path));
    if (!ok && !strcasecmp(base, "gm.dls")) {
        char p[1200];
        ok = path_to_host("gm.dls", p, sizeof p, 0);
        if (ok) snprintf(path, sizeof path, "%s", p);
    }
    if (!ok) {
        static int warned;
        if (!strcasecmp(base, "gm.dls") && !warned++)
            LOG(LOG_ALWAYS, "DirectMusic: no gm.dls (the General MIDI instruments some music uses are "
                "silent): copy it from a Windows installation (C:\\Windows\\System32\\drivers\\gm.dls) "
                "into the user folder");
        LOGS("DirectMusic: no file %s", name);
        return NULL;
    }
    return read_host(path, len);
}

static void engine_init(void)
{
    if (dm_loader)
        return;
    Dm_setLoggerDefault(LOGON(LOG_SOUND) ? DmLogLevel_INFO : DmLogLevel_ERROR);
    DmLoader_create(&dm_loader, DmLoader_DOWNLOAD);
    DmLoader_addResolver(dm_loader, resolve, NULL);
    char p[1200];
    if (path_to_host("_data\\sounds\\DirectMusic", p, sizeof p, 0))
        snprintf(dm_root, sizeof dm_root, "%s", p);
}

/* the mixing thread: render the music into the mix */
static void music_source(float *out, int frames, int rate, void *ctx)
{
    (void)ctx;
    static float *buf;
    static int cap;
    pthread_mutex_lock(&mlock);
    if (!dm_perf || perf_rate != rate) {
        if (dm_perf)
            DmPerformance_release(dm_perf);
        dm_perf = NULL;
        if (DmPerformance_create(&dm_perf, (uint32_t)rate) == DmResult_SUCCESS)
            perf_rate = rate;
    }
    if (dm_perf && pending_play) {
        DmPerformance_playSegment(dm_perf, pending_seg, (DmTiming)pending_timing);
        if (pending_seg)
            DmSegment_release(pending_seg);
        pending_seg = NULL;
        pending_play = 0;
    }
    if (dm_perf) {
        if (cap < frames) {
            cap = frames;
            buf = realloc(buf, sizeof(float) * 2 * (size_t)cap);
        }
        /* TinySoundFont plays the DLS instruments about 6 dB hotter than DirectMusic's software
           synthesizer (the town theme peaks at 1.4 of full scale on its own): halved */
        DmPerformance_setVolume(dm_perf, master_gain * 0.5f);
        DmPerformance_renderPcm(dm_perf, buf, (size_t)frames * 2, DmRender_FLOAT | DmRender_STEREO);
        for (int i = 0; i < 2 * frames; i++)
            out[i] += buf[i];
        rendered += (uint64_t)frames;
    }
    pthread_mutex_unlock(&mlock);
}

static void play(DmSegment *seg, int timing)
{
    pthread_mutex_lock(&mlock);
    if (pending_seg)
        DmSegment_release(pending_seg);
    pending_seg = seg ? DmSegment_retain(seg) : NULL;
    pending_timing = timing;
    pending_play = 1;
    pthread_mutex_unlock(&mlock);
}

/* ------------------------------------------------------------------ objects */
typedef struct {
    int kind, refs;
    uint32_t obj;
    DmSegment *seg;                     /* segments */
    char file[260];                     /* collections: the DLS file */
    uint32_t repeats;
    double start_ms;                    /* segment states: when it started (game clock) */
    uint32_t seg_obj;
} Host;
enum { K_DM = 1, K_PORT, K_PERF, K_LOADER, K_SEG, K_STATE, K_COLL };
static ComClass dm_cls, port_cls, perf_cls, loader_cls, seg_cls, state_cls, coll_cls;
static uint32_t the_dm, the_port;       /* one IDirectMusic, one port */
static uint32_t playing_seg;            /* the segment object last played (IsPlaying) */
static double playing_since, playing_len;

static uint32_t obj_new(ComClass *cls, int kind)
{
    Host *h = calloc(1, sizeof *h);
    h->kind = kind;
    h->refs = 1;
    h->obj = com_new(cls, h);
    return h->obj;
}
#define HOST(o) ((Host *)com_host(o))

/* the common three: QueryInterface (any interface of the same object: the game asks only for
   ones the object has), AddRef, Release */
METHOD(m_QueryInterface, 3)
{
    uint32_t riid = ARG(1), ppv = ARG(2);
    Host *h = HOST(ARG(0));
    if (!ppv)
        return E_POINTER;
    /* IKsControl on a port (the game's reverb property): not offered */
    if (h && h->kind == K_PORT && R32(riid) == 0x28f54685u) {
        W32(ppv, 0);
        return E_NOINTERFACE;
    }
    if (h) h->refs++;
    W32(ppv, ARG(0));
    LOGS("DirectMusic QueryInterface({" GUID_FMT "})", GUID_ARGS(riid));
    return S_OK;
}
METHOD(m_AddRef, 1) { Host *h = HOST(ARG(0)); return h ? (uint32_t)++h->refs : 1; }
METHOD(m_Release, 1)
{
    Host *h = HOST(ARG(0));
    if (!h)
        return 0;
    int r = --h->refs;
    if (r <= 0 && h->kind == K_SEG && h->seg) {
        DmSegment_release(h->seg);
        h->seg = NULL;
    }
    return (uint32_t)(r < 0 ? 0 : r);
}

/* methods with nothing to do: S_OK, logged the first time */
#define NOP(name, nargs) METHOD(name, nargs) \
    { static int once; if (!once++) LOGS("DirectMusic " #name ": nothing to do"); return S_OK; }

/* ------------------------------------------------------------------ IDirectMusic */
static void fill_portcaps(uint32_t p)
{
    uint32_t size = R32(p);
    memset(gptr(p), 0, size >= 0x134 ? 0x134 : size);
    W32(p, size);
    W32(p + 4, 1u | 2u);                /* DMUS_PC_DLS | DMUS_PC_EXTERNAL... (DLS is what the game checks) */
    W32(p + 8, 0x58c2b4d0);             /* guidPort: the Microsoft software synthesizer's */
    W16(p + 12, 0x46e7);
    W16(p + 14, 0x11d1);
    static const uint8_t d4[8] = {0x89, 0xac, 0x00, 0xa0, 0xc9, 0x05, 0x41, 0x29};
    for (int i = 0; i < 8; i++) W8(p + 16 + i, d4[i]);
    W32(p + 0x18, 1);                   /* DMUS_PC_OUTPUTCLASS */
    W32(p + 0x1c, 1);                   /* DMUS_PORT_USER_MODE_SYNTH */
    W32(p + 0x20, 0xffffffffu);         /* memory: system */
    W32(p + 0x24, 1000);                /* channel groups */
    W32(p + 0x28, 1000);                /* voices */
    W32(p + 0x2c, 2);
    gwput(p + 0x34, "Microsoft Synthesizer", 128);
}
/* (index, caps): S_FALSE past the last port */
METHOD(dm_EnumPort, 3)
{
    if (ARG(1) > 0)
        return S_FALSE;
    if (ARG(2)) fill_portcaps(ARG(2));
    return S_OK;
}
NOP(dm_CreateMusicBuffer, 4)
/* (port guid, params, &port, outer) */
METHOD(dm_CreatePort, 5)
{
    if (!the_port)
        the_port = obj_new(&port_cls, K_PORT);
    else
        HOST(the_port)->refs++;
    if (ARG(3)) W32(ARG(3), the_port);
    LOGS("IDirectMusic::CreatePort -> %08x", the_port);
    return S_OK;
}
METHOD(dm_EnumMasterClock, 3) { return S_FALSE; }
NOP(dm_GetMasterClock, 3)
NOP(dm_SetMasterClock, 2)
NOP(dm_Activate, 2)
/* (&guid): the software synthesizer's */
METHOD(dm_GetDefaultPort, 2)
{
    uint32_t tmp = galloc(0x134);
    W32(tmp, 0x134);
    fill_portcaps(tmp);
    if (ARG(1)) memcpy(gptr(ARG(1)), gptr(tmp + 8), 16);
    gfree(tmp);
    return S_OK;
}
NOP(dm_SetDirectSound, 3)

static void (*const dm_methods[])(CPU *) = {m_QueryInterface, m_AddRef, m_Release, dm_EnumPort,
    dm_CreateMusicBuffer, dm_CreatePort, dm_EnumMasterClock, dm_GetMasterClock, dm_SetMasterClock,
    dm_Activate, dm_GetDefaultPort, dm_SetDirectSound};
static const char *const dm_names[] = {"QueryInterface", "AddRef", "Release", "EnumPort",
    "CreateMusicBuffer", "CreatePort", "EnumMasterClock", "GetMasterClock", "SetMasterClock",
    "Activate", "GetDefaultPort", "SetDirectSound"};

/* ------------------------------------------------------------------ IDirectMusicPort */
NOP(port_PlayBuffer, 2)
NOP(port_SetReadNotificationHandle, 2)
NOP(port_Read, 2)
NOP(port_DownloadInstrument, 5)
NOP(port_UnloadInstrument, 2)
NOP(port_GetLatencyClock, 2)
NOP(port_GetRunningStats, 2)
NOP(port_Compact, 1)
METHOD(port_GetCaps, 2) { if (ARG(1)) fill_portcaps(ARG(1)); return S_OK; }
NOP(port_DeviceIoControl, 8)
NOP(port_SetNumChannelGroups, 2)
METHOD(port_GetNumChannelGroups, 2) { if (ARG(1)) W32(ARG(1), 2); return S_OK; }
NOP(port_Activate, 2)
NOP(port_SetChannelPriority, 4)
NOP(port_GetChannelPriority, 4)
NOP(port_SetDirectSound, 3)
NOP(port_GetFormat, 4)

static void (*const port_methods[])(CPU *) = {m_QueryInterface, m_AddRef, m_Release, port_PlayBuffer,
    port_SetReadNotificationHandle, port_Read, port_DownloadInstrument, port_UnloadInstrument,
    port_GetLatencyClock, port_GetRunningStats, port_Compact, port_GetCaps, port_DeviceIoControl,
    port_SetNumChannelGroups, port_GetNumChannelGroups, port_Activate, port_SetChannelPriority,
    port_GetChannelPriority, port_SetDirectSound, port_GetFormat};
static const char *const port_names[] = {"QueryInterface", "AddRef", "Release", "PlayBuffer",
    "SetReadNotificationHandle", "Read", "DownloadInstrument", "UnloadInstrument", "GetLatencyClock",
    "GetRunningStats", "Compact", "GetCaps", "DeviceIoControl", "SetNumChannelGroups",
    "GetNumChannelGroups", "Activate", "SetChannelPriority", "GetChannelPriority", "SetDirectSound",
    "GetFormat"};

/* ------------------------------------------------------------------ IDirectMusicPerformance */
/* (&IDirectMusic, IDirectSound, hwnd) */
METHOD(perf_Init, 4)
{
    if (!the_dm)
        the_dm = obj_new(&dm_cls, K_DM);
    if (ARG(1) && !R32(ARG(1))) {
        HOST(the_dm)->refs++;
        W32(ARG(1), the_dm);
    }
    engine_init();
    static int added;
    if (!added++)
        sound_add_source(music_source, NULL);
    LOGS("IDirectMusicPerformance::Init");
    return S_OK;
}
/* (segment, flags, start time, &segment state) */
METHOD(perf_PlaySegment, 5)
{
    Host *s = HOST(ARG(1));
    uint32_t fl = ARG(2);
    if (!s || s->kind != K_SEG)
        return E_POINTER;
    /* DMUS_SEGF_BEAT 0x1000, GRID 0x2000, MEASURE 0x200 (and the default boundary): dmusic's
       timings; anything else starts at once */
    int timing = fl & 0x1000 ? 3 : fl & 0x2000 ? DmTiming_GRID : fl & 0x200 ? 4 : DmTiming_INSTANT;
    if (timing > 4) timing = DmTiming_INSTANT;
    play(s->seg, timing);
    playing_seg = ARG(1);
    playing_since = now_ms();
    uint32_t rep = s->repeats != 0xffffffffu ? s->repeats : 0;
    playing_len = s->seg ? DmSegment_getLength(s->seg) * 1000.0 * (s->repeats == 0xffffffffu ? 1e9 : rep + 1) : 0;
    if (ARG(4)) {
        uint32_t st = obj_new(&state_cls, K_STATE);
        HOST(st)->seg_obj = ARG(1);
        HOST(st)->start_ms = playing_since;
        W32(ARG(4), st);
    }
    LOGS("IDirectMusicPerformance::PlaySegment(%08x \"%s\", flags %x)", ARG(1),
         s->seg && DmSegment_getName(s->seg) ? DmSegment_getName(s->seg) : "?", fl);
    return S_OK;
}
/* (segment, segment state, time, flags): stop that segment or (both NULL) everything */
METHOD(perf_Stop, 5)
{
    if (!ARG(1) || ARG(1) == playing_seg || (ARG(2) && HOST(ARG(2)) && HOST(ARG(2))->seg_obj == playing_seg)) {
        play(NULL, DmTiming_INSTANT);
        playing_seg = 0;
    }
    LOGS("IDirectMusicPerformance::Stop(%08x)", ARG(1));
    return S_OK;
}
METHOD(perf_GetSegmentState, 3) { if (ARG(1)) W32(ARG(1), 0); return DMUS_E_NOT_FOUND; }
NOP(perf_SetPrepareTime, 2)
METHOD(perf_GetPrepareTime, 2) { if (ARG(1)) W32(ARG(1), 1000); return S_OK; }
NOP(perf_SetBumperLength, 2)
METHOD(perf_GetBumperLength, 2) { if (ARG(1)) W32(ARG(1), 50); return S_OK; }
NOP(perf_SendPMsg, 2)
NOP(perf_MusicToReferenceTime, 3)
NOP(perf_ReferenceToMusicTime, 4)       /* (REFERENCE_TIME is 8 bytes) */
/* (segment, segment state): S_OK while it plays */
METHOD(perf_IsPlaying, 3)
{
    uint32_t seg = ARG(1);
    if (!seg && ARG(2) && HOST(ARG(2)))
        seg = HOST(ARG(2))->seg_obj;
    int on = playing_seg && (!seg || seg == playing_seg) && now_ms() - playing_since < playing_len;
    return on ? S_OK : S_FALSE;
}
/* (&reference time, &music time) */
METHOD(perf_GetTime, 3)
{
    double t = now_ms();
    if (ARG(1)) { uint64_t r = (uint64_t)(t * 10000.0); W32(ARG(1), (uint32_t)r); W32(ARG(1) + 4, (uint32_t)(r >> 32)); }
    if (ARG(2)) W32(ARG(2), (uint32_t)(t * 768.0 * 120.0 / 60000.0));
    return S_OK;
}
METHOD(perf_AllocPMsg, 3) { if (ARG(2)) W32(ARG(2), 0); return E_FAIL; }
NOP(perf_FreePMsg, 2)
METHOD(perf_GetGraph, 2) { if (ARG(1)) W32(ARG(1), 0); return DMUS_E_NOT_FOUND; }
NOP(perf_SetGraph, 2)
NOP(perf_SetNotificationHandle, 4)      /* (handle, REFERENCE_TIME) */
METHOD(perf_GetNotificationPMsg, 2) { if (ARG(1)) W32(ARG(1), 0); return S_FALSE; }
NOP(perf_AddNotificationType, 2)
NOP(perf_RemoveNotificationType, 2)
NOP(perf_AddPort, 2)
NOP(perf_RemovePort, 2)
NOP(perf_AssignPChannelBlock, 4)
NOP(perf_AssignPChannel, 5)
NOP(perf_PChannelInfo, 5)
NOP(perf_DownloadInstrument, 7)
NOP(perf_Invalidate, 3)
METHOD(perf_GetParam, 7) { return DMUS_E_NOT_FOUND; }
NOP(perf_SetParam, 6)
/* (guid, data, size) */
METHOD(perf_GetGlobalParam, 4)
{
    if (is(ARG(1), &GUID_PerfMasterVolume) && ARG(2) && ARG(3) >= 4) {
        int32_t mb = master_gain <= 1e-5f ? -10000 : (int32_t)lrint(2000.0 * log10(master_gain));
        W32(ARG(2), (uint32_t)mb);
        return S_OK;
    }
    return E_FAIL;
}
METHOD(perf_SetGlobalParam, 4)
{
    if (is(ARG(1), &GUID_PerfMasterVolume) && ARG(2) && ARG(3) >= 4) {
        int32_t mb = (int32_t)R32(ARG(2));          /* hundredths of a decibel */
        master_gain = mb <= -9600 ? 0.0f : (float)pow(10.0, mb / 2000.0);
        LOGS("DirectMusic master volume %d (%.3f)", mb, master_gain);
    }
    return S_OK;
}
NOP(perf_GetLatencyTime, 2)
NOP(perf_GetQueueTime, 2)
NOP(perf_AdjustTime, 3)
METHOD(perf_CloseDown, 1)
{
    play(NULL, DmTiming_INSTANT);
    playing_seg = 0;
    return S_OK;
}
NOP(perf_GetResolvedTime, 5)
NOP(perf_MIDIToMusic, 5)
NOP(perf_MusicToMIDI, 6)
NOP(perf_TimeToRhythm, 6)
NOP(perf_RhythmToTime, 6)

static void (*const perf_methods[])(CPU *) = {m_QueryInterface, m_AddRef, m_Release, perf_Init,
    perf_PlaySegment, perf_Stop, perf_GetSegmentState, perf_SetPrepareTime, perf_GetPrepareTime,
    perf_SetBumperLength, perf_GetBumperLength, perf_SendPMsg, perf_MusicToReferenceTime,
    perf_ReferenceToMusicTime, perf_IsPlaying, perf_GetTime, perf_AllocPMsg, perf_FreePMsg, perf_GetGraph,
    perf_SetGraph, perf_SetNotificationHandle, perf_GetNotificationPMsg, perf_AddNotificationType,
    perf_RemoveNotificationType, perf_AddPort, perf_RemovePort, perf_AssignPChannelBlock,
    perf_AssignPChannel, perf_PChannelInfo, perf_DownloadInstrument, perf_Invalidate, perf_GetParam,
    perf_SetParam, perf_GetGlobalParam, perf_SetGlobalParam, perf_GetLatencyTime, perf_GetQueueTime,
    perf_AdjustTime, perf_CloseDown, perf_GetResolvedTime, perf_MIDIToMusic, perf_MusicToMIDI,
    perf_TimeToRhythm, perf_RhythmToTime};
static const char *const perf_names[] = {"QueryInterface", "AddRef", "Release", "Init", "PlaySegment",
    "Stop", "GetSegmentState", "SetPrepareTime", "GetPrepareTime", "SetBumperLength", "GetBumperLength",
    "SendPMsg", "MusicToReferenceTime", "ReferenceToMusicTime", "IsPlaying", "GetTime", "AllocPMsg",
    "FreePMsg", "GetGraph", "SetGraph", "SetNotificationHandle", "GetNotificationPMsg",
    "AddNotificationType", "RemoveNotificationType", "AddPort", "RemovePort", "AssignPChannelBlock",
    "AssignPChannel", "PChannelInfo", "DownloadInstrument", "Invalidate", "GetParam", "SetParam",
    "GetGlobalParam", "SetGlobalParam", "GetLatencyTime", "GetQueueTime", "AdjustTime", "CloseDown",
    "GetResolvedTime", "MIDIToMusic", "MusicToMIDI", "TimeToRhythm", "RhythmToTime"};

/* ------------------------------------------------------------------ IDirectMusicLoader */
static void preload_dls_in(const char *dir)
{
    DIR *d = opendir(dir);
    for (struct dirent *e; d && (e = readdir(d));) {
        size_t n = strlen(e->d_name);
        if (n > 4 && !strcasecmp(e->d_name + n - 4, ".dls"))
            DmLoader_preloadDls(dm_loader, e->d_name);
    }
    if (d)
        closedir(d);
}

/* (desc, iid, &object) */
METHOD(ld_GetObject, 4)
{
    uint32_t desc = ARG(1), ppv = ARG(3);
    if (!desc || !ppv)
        return E_POINTER;
    W32(ppv, 0);
    uint32_t valid = R32(desc + 4);
    char file[260] = "";
    if (valid & 0x10)                   /* DMUS_OBJ_FILENAME */
        gwstr(desc + 0x138, file, sizeof file);
    engine_init();
    if (is(desc + 0x18, &CLSID_DirectMusicSegment)) {
        if (search_dir[0])
            preload_dls_in(search_dir);
        DmSegment *seg = NULL;
        if (!file[0] || DmLoader_getSegment(dm_loader, file, &seg) != DmResult_SUCCESS) {
            LOG(LOG_ALWAYS, "DirectMusic: cannot load segment %s", file);
            return DMUS_E_LOADER_FAILEDOPEN;
        }
        uint32_t o = obj_new(&seg_cls, K_SEG);
        HOST(o)->seg = seg;
        HOST(o)->repeats = DmSegment_getRepeats(seg);
        W32(ppv, o);
        LOGS("IDirectMusicLoader::GetObject(segment %s) -> %08x, %.1f s, repeats %u", file, o,
             DmSegment_getLength(seg), HOST(o)->repeats);
        return S_OK;
    }
    if (is(desc + 0x18, &CLSID_DirectMusicCollection)) {
        if (!file[0]) {
            if (!(valid & 1))           /* DMUS_OBJ_OBJECT: the default GM set */
                return DMUS_E_LOADER_FAILEDOPEN;
            snprintf(file, sizeof file, "gm.dls");
        }
        size_t n;
        void *b = resolve(NULL, file, &n);
        if (!b)
            return DMUS_E_LOADER_FAILEDOPEN;
        free(b);
        uint32_t o = obj_new(&coll_cls, K_COLL);
        snprintf(HOST(o)->file, sizeof HOST(o)->file, "%s", file);
        W32(ppv, o);
        LOGS("IDirectMusicLoader::GetObject(collection %s) -> %08x", file, o);
        return S_OK;
    }
    LOG(LOG_ALWAYS, "IDirectMusicLoader::GetObject(class {" GUID_FMT "}, %s): not supported",
        GUID_ARGS(desc + 0x18), file);
    return DMUS_E_LOADER_FAILEDOPEN;
}
NOP(ld_SetObject, 2)
/* (class, wide path, clear) */
METHOD(ld_SetSearchDirectory, 4)
{
    char p[260], h[1200];
    gwstr(ARG(2), p, sizeof p);
    if (path_to_host(p, h, sizeof h, 0))
        snprintf(search_dir, sizeof search_dir, "%s", h);
    else
        search_dir[0] = 0;
    LOGS("IDirectMusicLoader::SetSearchDirectory(%s) -> %s", p, search_dir[0] ? search_dir : "(none)");
    return S_OK;
}
NOP(ld_ScanDirectory, 4)
NOP(ld_CacheObject, 2)
NOP(ld_ReleaseObject, 2)
NOP(ld_ClearCache, 2)
NOP(ld_EnableCache, 3)
METHOD(ld_EnumObject, 4) { return S_FALSE; }

static void (*const ld_methods[])(CPU *) = {m_QueryInterface, m_AddRef, m_Release, ld_GetObject,
    ld_SetObject, ld_SetSearchDirectory, ld_ScanDirectory, ld_CacheObject, ld_ReleaseObject,
    ld_ClearCache, ld_EnableCache, ld_EnumObject};
static const char *const ld_names[] = {"QueryInterface", "AddRef", "Release", "GetObject", "SetObject",
    "SetSearchDirectory", "ScanDirectory", "CacheObject", "ReleaseObject", "ClearCache", "EnableCache",
    "EnumObject"};

/* ------------------------------------------------------------------ IDirectMusicSegment2 */
/* (&music time) */
METHOD(seg_GetLength, 2)
{
    Host *s = HOST(ARG(0));
    if (ARG(1)) W32(ARG(1), s && s->seg ? (uint32_t)(DmSegment_getLength(s->seg) * 768.0 * 2) : 0);
    return S_OK;
}
NOP(seg_SetLength, 2)
METHOD(seg_GetRepeats, 2) { Host *s = HOST(ARG(0)); if (ARG(1)) W32(ARG(1), s ? s->repeats : 0); return S_OK; }
METHOD(seg_SetRepeats, 2) { Host *s = HOST(ARG(0)); if (s) s->repeats = ARG(1); return S_OK; }
METHOD(seg_GetDefaultResolution, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }
NOP(seg_SetDefaultResolution, 2)
METHOD(seg_GetTrack, 5) { if (ARG(4)) W32(ARG(4), 0); return DMUS_E_NOT_FOUND; }
METHOD(seg_GetTrackGroup, 3) { if (ARG(2)) W32(ARG(2), 0); return DMUS_E_NOT_FOUND; }
NOP(seg_InsertTrack, 3)
NOP(seg_RemoveTrack, 2)
METHOD(seg_InitPlay, 5) { if (ARG(1)) W32(ARG(1), 0); return E_FAIL; }
METHOD(seg_GetGraph, 2) { if (ARG(1)) W32(ARG(1), 0); return DMUS_E_NOT_FOUND; }
NOP(seg_SetGraph, 2)
NOP(seg_AddNotificationType, 2)
NOP(seg_RemoveNotificationType, 2)
METHOD(seg_GetParam, 7) { return DMUS_E_NOT_FOUND; }
/* (guid, group bits, index, time, data) */
METHOD(seg_SetParam, 6)
{
    if (is(ARG(1), &GUID_ConnectToDLSCollection)) {
        Host *col = HOST(ARG(5));
        if (col && col->kind == K_COLL && col->file[0]) {
            DmLoader_preloadDls(dm_loader, col->file);
            LOGS("IDirectMusicSegment::SetParam(ConnectToDLSCollection, %s)", col->file);
        }
    }
    return S_OK;
}
METHOD(seg_Clone, 4) { if (ARG(3)) W32(ARG(3), 0); return E_FAIL; }
NOP(seg_SetStartPoint, 2)
METHOD(seg_GetStartPoint, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }
NOP(seg_SetLoopPoints, 3)
METHOD(seg_GetLoopPoints, 3) { if (ARG(1)) W32(ARG(1), 0); if (ARG(2)) W32(ARG(2), 0); return S_OK; }
NOP(seg_SetPChannelsUsed, 3)
METHOD(seg_Compose, 5) { if (ARG(4)) W32(ARG(4), 0); return E_FAIL; }
METHOD(seg_GetAudioPathConfig, 2) { if (ARG(1)) W32(ARG(1), 0); return E_FAIL; }

static void (*const seg_methods[])(CPU *) = {m_QueryInterface, m_AddRef, m_Release, seg_GetLength,
    seg_SetLength, seg_GetRepeats, seg_SetRepeats, seg_GetDefaultResolution, seg_SetDefaultResolution,
    seg_GetTrack, seg_GetTrackGroup, seg_InsertTrack, seg_RemoveTrack, seg_InitPlay, seg_GetGraph,
    seg_SetGraph, seg_AddNotificationType, seg_RemoveNotificationType, seg_GetParam, seg_SetParam,
    seg_Clone, seg_SetStartPoint, seg_GetStartPoint, seg_SetLoopPoints, seg_GetLoopPoints,
    seg_SetPChannelsUsed, seg_Compose, seg_GetAudioPathConfig};
static const char *const seg_names[] = {"QueryInterface", "AddRef", "Release", "GetLength", "SetLength",
    "GetRepeats", "SetRepeats", "GetDefaultResolution", "SetDefaultResolution", "GetTrack",
    "GetTrackGroup", "InsertTrack", "RemoveTrack", "InitPlay", "GetGraph", "SetGraph",
    "AddNotificationType", "RemoveNotificationType", "GetParam", "SetParam", "Clone", "SetStartPoint",
    "GetStartPoint", "SetLoopPoints", "GetLoopPoints", "SetPChannelsUsed", "Compose",
    "GetAudioPathConfig"};

/* ------------------------------------------------------------------ IDirectMusicSegmentState */
METHOD(st_GetRepeats, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }
METHOD(st_GetSegment, 2)
{
    Host *h = HOST(ARG(0));
    if (ARG(1)) {
        W32(ARG(1), h ? h->seg_obj : 0);
        if (h && h->seg_obj && HOST(h->seg_obj)) HOST(h->seg_obj)->refs++;
    }
    return h && h->seg_obj ? S_OK : E_FAIL;
}
METHOD(st_GetStartTime, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }
METHOD(st_GetSeek, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }
METHOD(st_GetStartPoint, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }

static void (*const st_methods[])(CPU *) = {m_QueryInterface, m_AddRef, m_Release, st_GetRepeats,
    st_GetSegment, st_GetStartTime, st_GetSeek, st_GetStartPoint};
static const char *const st_names[] = {"QueryInterface", "AddRef", "Release", "GetRepeats", "GetSegment",
    "GetStartTime", "GetSeek", "GetStartPoint"};

/* ------------------------------------------------------------------ IDirectMusicCollection */
METHOD(coll_GetInstrument, 3) { if (ARG(2)) W32(ARG(2), 0); return DMUS_E_NOT_FOUND; }
METHOD(coll_EnumInstrument, 5) { return S_FALSE; }

static void (*const coll_methods[])(CPU *) = {m_QueryInterface, m_AddRef, m_Release, coll_GetInstrument,
    coll_EnumInstrument};
static const char *const coll_names[] = {"QueryInterface", "AddRef", "Release", "GetInstrument",
    "EnumInstrument"};

/* ------------------------------------------------------------------ creation */
#define N(a) ((int)(sizeof a / sizeof *a))
static void classes(void)
{
    static int done;
    if (done++)
        return;
    com_class_init(&dm_cls, "IDirectMusic2", N(dm_methods), dm_methods, dm_names);
    com_class_init(&port_cls, "IDirectMusicPort", N(port_methods), port_methods, port_names);
    com_class_init(&perf_cls, "IDirectMusicPerformance2", N(perf_methods), perf_methods, perf_names);
    com_class_init(&loader_cls, "IDirectMusicLoader", N(ld_methods), ld_methods, ld_names);
    com_class_init(&seg_cls, "IDirectMusicSegment2", N(seg_methods), seg_methods, seg_names);
    com_class_init(&state_cls, "IDirectMusicSegmentState", N(st_methods), st_methods, st_names);
    com_class_init(&coll_cls, "IDirectMusicCollection", N(coll_methods), coll_methods, coll_names);
}

/* ole32.c's CoCreateInstance: REGDB_E_CLASSNOTREG for classes that are not DirectMusic's */
uint32_t dmusic_cocreate(uint32_t clsid, uint32_t iid, uint32_t ppv)
{
    (void)iid;
    classes();
    uint32_t o = 0;
    if (is(clsid, &CLSID_DirectMusic)) {
        if (!the_dm)
            the_dm = obj_new(&dm_cls, K_DM);
        else
            HOST(the_dm)->refs++;
        o = the_dm;
    } else if (is(clsid, &CLSID_DirectMusicPerformance))
        o = obj_new(&perf_cls, K_PERF);
    else if (is(clsid, &CLSID_DirectMusicLoader))
        o = obj_new(&loader_cls, K_LOADER);
    else
        return 0x80040154u;
    if (ppv)
        W32(ppv, o);
    LOGS("CoCreateInstance(DirectMusic {" GUID_FMT "}) -> %08x", GUID_ARGS(clsid), o);
    return S_OK;
}
