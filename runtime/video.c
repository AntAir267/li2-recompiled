/* The movies: a stand-in for the part of DirectShow the game uses (MSFMV.cpp, 0x81c693-0x81d281),
   the MPEG-1 files decoded with pl_mpeg (third_party/pl_mpeg, MIT).

   What the game does: CoCreateInstance(CLSID_FilterGraph, IID_IGraphBuilder), RenderFile(path),
   QueryInterface for IMediaControl, IMediaEventEx, IMediaSeeking, IVideoWindow and IBasicVideo;
   GetVideoSize and SetWindowPosition (centred on the screen), put_Owner, put_WindowStyle,
   put_AutoShow, put_Visible, SetNotifyWindow(hwnd, 0x40d); Run; then it pumps messages with
   Sleep(0) until its window procedure sees EC_COMPLETE from GetEvent (after the 0x40d message) or
   a key (the skippable movies), which Stops the graph.

   Here: RenderFile opens the file with pl_mpeg and decodes its whole soundtrack (the movies are
   at most a few minutes of MP2), Run starts the soundtrack as a runtime voice, and while the movie
   plays every PeekMessage of the game's loop shows the frame due (by the soundtrack's position)
   through a picture of the movie's height padded to the window's shape (bars at the sides), so the
   GPU scales it like the game's frames. At the end the notify window gets 0x40d and GetEvent
   answers EC_COMPLETE once. Headless and --test runs fail RenderFile: the game then skips the
   movie, as on a PC without the codec. */
#define _GNU_SOURCE
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define PL_MPEG_IMPLEMENTATION
#include "../third_party/pl_mpeg/pl_mpeg.h"
#include "runtime.h"
#include "gfx.h"
#include "sound.h"
#include "win.h"
#include "options.h"
#include <strings.h>

#define S_OK 0u
#define E_NOINTERFACE 0x80004002u
#define E_FAIL 0x80004005u
#define E_ABORT 0x80004004u
#define VFW_E_NOT_FOUND 0x80040216u
#define EC_COMPLETE 1u

Surface *ddraw_image_new(int w, int h);
uint32_t *ddraw_image_pixels(Surface *s);
void ddraw_image_show(Surface *s);
void ddraw_image_free(Surface *s);
void win_post(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);

typedef struct { uint32_t d1; uint16_t d2, d3; uint8_t d4[8]; } G;
#define DSG(x) {x, 0x0ad4, 0x11ce, {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}}
static const G CLSID_FilterGraph = {0xe436ebb3, 0x524f, 0x11ce, {0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
static const G IID_IGraphBuilder = DSG(0x56a868a9);
static const G IID_IMediaControl = DSG(0x56a868b1);
static const G IID_IMediaEvent = DSG(0x56a868b6);
static const G IID_IMediaEventEx = DSG(0x56a868c0);
static const G IID_IVideoWindow = DSG(0x56a868b4);
static const G IID_IBasicVideo = DSG(0x56a868b5);
static const G IID_IBasicAudio = DSG(0x56a868b3);
static const G IID_IMediaSeeking = {0x36b73880, 0xc2c8, 0x11cf, {0x8b, 0x46, 0x00, 0x80, 0x5f, 0x6c, 0xef, 0x60}};
static int is(uint32_t a, const G *g) { return a && guid_eq(a, g->d1, g->d2, g->d3, g->d4); }

typedef struct {
    int refs;
    uint32_t graph, control, event, seeking, vwin, bvideo, baudio;   /* the interfaces */
    plm_t *plm;
    int w, h;
    double fps, duration;
    int16_t *pcm;                       /* the whole soundtrack */
    uint32_t pcm_frames;
    int rate, voice;
    int running, ended, event_due;
    double start_ms;                    /* when Run started (no soundtrack: the clock) */
    int shown;                          /* frames shown */
    Surface *pic;
    int pic_w, pad;
    uint32_t notify_hwnd, notify_msg;
    float gain;                         /* IBasicAudio's volume */
} Movie;

static Movie *playing;                  /* the movie the game's loop waits on */
static ComClass graph_cls, control_cls, event_cls, seeking_cls, vwin_cls, bvideo_cls, baudio_cls;
#define MOV(o) ((Movie *)com_host(o))

/* ------------------------------------------------------------------ playing */
static void movie_stop(Movie *m)
{
    m->running = 0;
    if (m->voice >= 0)
        voice_pause(m->voice);
    if (playing == m)
        playing = NULL;
}

static void movie_free(Movie *m)
{
    movie_stop(m);
    if (m->voice >= 0)
        voice_free(m->voice);
    if (m->plm)
        plm_destroy(m->plm);
    free(m->pcm);
    if (m->pic)
        ddraw_image_free(m->pic);
    free(m);
}

/* the game's loop: show the frame that is due; at the end tell the notify window */
static void movie_tick(void)
{
    Movie *m = playing;
    if (!m || !m->running)
        return;
    double t;
    uint32_t pos;
    if (m->voice >= 0 && voice_playing(m->voice, &pos))
        t = (double)pos / m->rate;
    else
        t = (now_ms() - m->start_ms) / 1000.0;
    int due = (int)(t * m->fps);
    if (due < m->shown)
        return;
    plm_frame_t *f = NULL;
    while (m->shown <= due && (f = plm_decode_video(m->plm)) != NULL)
        m->shown++;
    if (!f) {
        if (m->voice >= 0 && voice_playing(m->voice, NULL))
            return;                     /* (the soundtrack's last part) */
        LOG(LOG_ALWAYS, "movie: ended after %d frames", m->shown);
        m->running = 0;
        m->ended = 1;
        m->event_due = 1;
        playing = NULL;
        if (m->notify_hwnd)
            win_post(m->notify_hwnd, m->notify_msg, 0, 0);
        return;
    }
    uint32_t *px = ddraw_image_pixels(m->pic);
    /* BGRA bytes are the X8R8G8B8 picture's little-endian layout */
    plm_frame_to_bgra(f, (uint8_t *)(px + m->pad), m->pic_w * 4);
    ddraw_image_show(m->pic);
}

/* called by user32's PeekMessage: the game's movie loop pumps messages while it waits */
void video_tick(void)
{
    if (playing)
        movie_tick();
}

/* a movie is on the screen (prompts.c draws nothing over it) */
int video_playing(void) { return playing != NULL; }

/* A controller button skips a movie as a key does: the game's movie window procedure (0x81ce72)
   stops a skippable movie on any WM_KEYDOWN, and in its loop the game reads no DirectInput, so
   the button is handed to it as an Escape key press. (The keyboard's own keys get there as they
   are.) */
void video_skip_press(void)
{
    if (!playing || !playing->running)
        return;
    post_message(0x0100, 0x1b, 0x00010001u);       /* WM_KEYDOWN, WM_KEYUP: VK_ESCAPE */
    post_message(0x0101, 0x1b, 0xc0010001u);
}

/* RenderFile: open the movie (wide path) */
static uint32_t open_movie(Movie *m, uint32_t wpath)
{
    char win[600], host[1200];
    gwstr(wpath, win, sizeof win);
    if (headless || deterministic) {
        LOG(LOG_ALWAYS, "movie %s: not played (no window, or a test run)", win);
        return VFW_E_NOT_FOUND;
    }
    /* intro_movies = off (options.txt): the three start-up movies are skipped, as the community
       "No intro videos" patch does */
    const char *base = strrchr(win, '\\');
    base = base ? base + 1 : win;
    int intro = !strcasecmp(base, "LegoSoftware.mpg") || !strcasecmp(base, "SDLogo.mpg") || !strcasecmp(base, "IntroFMV.mpg");
    if (intro && !option_bool("intro_movies", 1)) {
        LOG(LOG_ALWAYS, "movie %s: skipped (intro_movies = off)", base);
        return VFW_E_NOT_FOUND;
    }
    /* The start-up movies can all be skipped at once: the game plays the LEGO Software logo
       unskippable (0x402990 calls 0x81c693 with 0 as its third argument, the other two with 1;
       0x81c693 keeps it at 0x5d51700, which the movie window procedure 0x81ce72 checks on
       WM_KEYDOWN) [verified: the instructions]: made 1 for the three */
    if (intro)
        W32(0x5d51700u, 1);
    if (!path_to_host(win, host, sizeof host, 0)) {
        LOG(LOG_ALWAYS, "movie %s: no such file", win);
        return VFW_E_NOT_FOUND;
    }
    m->plm = plm_create_with_filename(host);
    if (!m->plm || !plm_has_headers(m->plm)) {
        LOG(LOG_ALWAYS, "movie %s: not an MPEG-1 file pl_mpeg can play", win);
        if (m->plm) plm_destroy(m->plm);
        m->plm = NULL;
        return E_FAIL;
    }
    m->w = plm_get_width(m->plm);
    m->h = plm_get_height(m->plm);
    m->fps = plm_get_framerate(m->plm);
    if (m->fps <= 0) m->fps = 25;
    m->duration = plm_get_duration(m->plm);
    m->rate = plm_get_samplerate(m->plm);
    /* the soundtrack, whole, as 16-bit stereo */
    plm_t *au = plm_get_num_audio_streams(m->plm) > 0 && m->rate > 0 ? plm_create_with_filename(host) : NULL;
    if (au) {                           /* (a reader of its own: audio only from the start) */
        plm_set_video_enabled(au, 0);
        size_t cap = (size_t)(m->duration > 0 ? m->duration + 2 : 60) * (size_t)m->rate, n = 0;
        m->pcm = malloc(cap * 2 * sizeof *m->pcm);
        plm_samples_t *s;
        /* (a NULL now and then before the end: the demuxer met only other streams' packets) */
        for (long misses = 0; !plm_has_ended(au) && misses < 1000000; ) {
            if (!(s = plm_decode_audio(au))) {
                misses++;
                continue;
            }
            if (n + s->count > cap) {
                cap = (n + s->count) * 2;
                m->pcm = realloc(m->pcm, cap * 2 * sizeof *m->pcm);
            }
            for (unsigned i = 0; i < 2 * s->count; i++) {
                float v = s->interleaved[i];
                m->pcm[2 * n + i] = (int16_t)lrintf((v > 1 ? 1 : v < -1 ? -1 : v) * 32767.0f);
            }
            n += s->count;
        }
        m->pcm_frames = (uint32_t)n;
        plm_destroy(au);
        plm_set_audio_enabled(m->plm, 0);
        m->voice = n ? voice_new(m->pcm, (uint32_t)n, 2, m->rate) : -1;
    }
    /* the picture: the movie's height, as wide as the window's shape (bars at the sides) */
    int ww = 0, wh = 0;
    host_desktop_size(&ww, &wh);
    m->pic_w = ww > 0 && wh > 0 && (double)ww / wh > (double)m->w / m->h ? (int)lround((double)m->h * ww / wh) : m->w;
    m->pic_w = (m->pic_w + 3) & ~3;
    m->pad = (m->pic_w - m->w) / 2;
    m->pic = ddraw_image_new(m->pic_w, m->h);
    LOG(LOG_ALWAYS, "movie %s: %dx%d, %.2f fps, %.1f s, sound %u frames at %d Hz", win, m->w, m->h, m->fps,
        m->duration, m->pcm_frames, m->rate);
    return S_OK;
}

/* ------------------------------------------------------------------ the interfaces */
static uint32_t qi(Movie *m, uint32_t riid, uint32_t ppv)
{
    uint32_t o = 0;
    if (is(riid, &IID_IGraphBuilder)) o = m->graph;
    else if (is(riid, &IID_IMediaControl)) o = m->control;
    else if (is(riid, &IID_IMediaEventEx) || is(riid, &IID_IMediaEvent)) o = m->event;
    else if (is(riid, &IID_IMediaSeeking)) o = m->seeking;
    else if (is(riid, &IID_IVideoWindow)) o = m->vwin;
    else if (is(riid, &IID_IBasicVideo)) o = m->bvideo;
    else if (is(riid, &IID_IBasicAudio)) o = m->baudio;
    if (ppv) W32(ppv, o);
    if (!o) {
        LOG(LOG_ALWAYS, "movie: QueryInterface({" GUID_FMT "}): not offered", GUID_ARGS(riid));
        return E_NOINTERFACE;
    }
    m->refs++;
    return S_OK;
}
METHOD(any_QueryInterface, 3) { return qi(MOV(ARG(0)), ARG(1), ARG(2)); }
METHOD(any_AddRef, 1) { return (uint32_t)++MOV(ARG(0))->refs; }
METHOD(any_Release, 1)
{
    Movie *m = MOV(ARG(0));
    int r = --m->refs;
    if (r == 0)
        movie_free(m);
    return (uint32_t)(r < 0 ? 0 : r);
}
/* IDispatch (the IMedia* interfaces are dispatch interfaces): not used */
METHOD(disp_GetTypeInfoCount, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }
METHOD(disp_GetTypeInfo, 4) { return E_FAIL; }
METHOD(disp_GetIDsOfNames, 6) { return E_FAIL; }
METHOD(disp_Invoke, 9) { return E_FAIL; }
/* whatever else: S_OK */
#define OK(name, nargs) METHOD(name, nargs) { return S_OK; }

/* IGraphBuilder */
OK(gb_AddFilter, 3)
OK(gb_RemoveFilter, 2)
METHOD(gb_EnumFilters, 2) { if (ARG(1)) W32(ARG(1), 0); return E_FAIL; }
METHOD(gb_FindFilterByName, 3) { if (ARG(2)) W32(ARG(2), 0); return VFW_E_NOT_FOUND; }
OK(gb_ConnectDirect, 4)
OK(gb_Reconnect, 2)
OK(gb_Disconnect, 2)
OK(gb_SetDefaultSyncSource, 1)
OK(gb_Connect, 3)
OK(gb_Render, 2)
METHOD(gb_RenderFile, 3) { return open_movie(MOV(ARG(0)), ARG(1)); }
METHOD(gb_AddSourceFilter, 4) { if (ARG(3)) W32(ARG(3), 0); return E_FAIL; }
OK(gb_SetLogFile, 2)
OK(gb_Abort, 1)
OK(gb_ShouldOperationContinue, 1)
static void (*const gb_m[])(CPU *) = {any_QueryInterface, any_AddRef, any_Release, gb_AddFilter, gb_RemoveFilter,
    gb_EnumFilters, gb_FindFilterByName, gb_ConnectDirect, gb_Reconnect, gb_Disconnect, gb_SetDefaultSyncSource,
    gb_Connect, gb_Render, gb_RenderFile, gb_AddSourceFilter, gb_SetLogFile, gb_Abort, gb_ShouldOperationContinue};
static const char *const gb_n[] = {"QueryInterface", "AddRef", "Release", "AddFilter", "RemoveFilter",
    "EnumFilters", "FindFilterByName", "ConnectDirect", "Reconnect", "Disconnect", "SetDefaultSyncSource",
    "Connect", "Render", "RenderFile", "AddSourceFilter", "SetLogFile", "Abort", "ShouldOperationContinue"};

/* IMediaControl */
METHOD(mc_Run, 1)
{
    Movie *m = MOV(ARG(0));
    if (!m->plm) {
        /* a movie that did not open: the game ignores RenderFile's failure and waits for the end
           (on Windows the CD's files were always there), so it ends at once */
        m->ended = 1;
        m->event_due = 1;
        if (m->notify_hwnd)
            win_post(m->notify_hwnd, m->notify_msg, 0, 0);
        return S_OK;
    }
    if (!m->running) {
        m->running = 1;
        m->ended = 0;
        playing = m;
        m->start_ms = now_ms() - (double)m->shown / m->fps * 1000.0;
        if (m->voice >= 0)
            voice_set_gain(m->voice, m->gain, m->gain);
            voice_play(m->voice, 1);
        LOG(LOG_ALWAYS, "movie: Run");
    }
    return S_OK;
}
METHOD(mc_Pause, 1) { Movie *m = MOV(ARG(0)); m->running = 0; if (m->voice >= 0) voice_pause(m->voice); return S_OK; }
METHOD(mc_Stop, 1)
{
    Movie *m = MOV(ARG(0));
    if (m->running)
        LOG(LOG_ALWAYS, "movie: stopped after %d frames", m->shown);
    movie_stop(m);
    return S_OK;
}
METHOD(mc_GetState, 3) { Movie *m = MOV(ARG(0)); if (ARG(2)) W32(ARG(2), m->running ? 2 : 0); return S_OK; }
METHOD(mc_RenderFile, 2) { return E_FAIL; }
METHOD(mc_AddSourceFilter, 3) { return E_FAIL; }
METHOD(mc_get_FilterCollection, 2) { return E_FAIL; }
METHOD(mc_get_RegFilterCollection, 2) { return E_FAIL; }
OK(mc_StopWhenReady, 1)
static void (*const mc_m[])(CPU *) = {any_QueryInterface, any_AddRef, any_Release, disp_GetTypeInfoCount,
    disp_GetTypeInfo, disp_GetIDsOfNames, disp_Invoke, mc_Run, mc_Pause, mc_Stop, mc_GetState, mc_RenderFile,
    mc_AddSourceFilter, mc_get_FilterCollection, mc_get_RegFilterCollection, mc_StopWhenReady};
static const char *const mc_n[] = {"QueryInterface", "AddRef", "Release", "GetTypeInfoCount", "GetTypeInfo",
    "GetIDsOfNames", "Invoke", "Run", "Pause", "Stop", "GetState", "RenderFile", "AddSourceFilter",
    "get_FilterCollection", "get_RegFilterCollection", "StopWhenReady"};

/* IMediaEventEx */
METHOD(me_GetEventHandle, 2) { if (ARG(1)) W32(ARG(1), 0); return E_FAIL; }
/* (&code, &param1, &param2, timeout): EC_COMPLETE once after the end, else E_ABORT (none) */
METHOD(me_GetEvent, 5)
{
    Movie *m = MOV(ARG(0));
    if (!m->event_due)
        return E_ABORT;
    m->event_due = 0;
    if (ARG(1)) W32(ARG(1), EC_COMPLETE);
    if (ARG(2)) W32(ARG(2), 0);
    if (ARG(3)) W32(ARG(3), 0);
    return S_OK;
}
METHOD(me_WaitForCompletion, 3)
{
    Movie *m = MOV(ARG(0));
    if (ARG(2)) W32(ARG(2), m->ended ? EC_COMPLETE : 0);
    return m->ended ? S_OK : E_ABORT;
}
OK(me_CancelDefaultHandling, 2)
OK(me_RestoreDefaultHandling, 2)
OK(me_FreeEventParams, 4)
/* (hwnd, message, lparam) */
METHOD(me_SetNotifyWindow, 4) { Movie *m = MOV(ARG(0)); m->notify_hwnd = ARG(1); m->notify_msg = ARG(2); return S_OK; }
OK(me_SetNotifyFlags, 2)
METHOD(me_GetNotifyFlags, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }
static void (*const me_m[])(CPU *) = {any_QueryInterface, any_AddRef, any_Release, disp_GetTypeInfoCount,
    disp_GetTypeInfo, disp_GetIDsOfNames, disp_Invoke, me_GetEventHandle, me_GetEvent, me_WaitForCompletion,
    me_CancelDefaultHandling, me_RestoreDefaultHandling, me_FreeEventParams, me_SetNotifyWindow,
    me_SetNotifyFlags, me_GetNotifyFlags};
static const char *const me_n[] = {"QueryInterface", "AddRef", "Release", "GetTypeInfoCount", "GetTypeInfo",
    "GetIDsOfNames", "Invoke", "GetEventHandle", "GetEvent", "WaitForCompletion", "CancelDefaultHandling",
    "RestoreDefaultHandling", "FreeEventParams", "SetNotifyWindow", "SetNotifyFlags", "GetNotifyFlags"};

/* IMediaSeeking (17 methods after IUnknown): answered E_FAIL, the game only releases it. The
   methods take different argument counts, so each pops its own (2 to 5 dwords). */
METHOD(ms_1, 2) { return E_FAIL; }
METHOD(ms_2, 3) { return E_FAIL; }
METHOD(ms_4, 5) { return E_FAIL; }
METHOD(ms_5, 6) { return E_FAIL; }
static void (*const ms_m[])(CPU *) = {any_QueryInterface, any_AddRef, any_Release,
    ms_1, ms_1, ms_1, ms_1, ms_1, ms_1, ms_1, ms_1, ms_1, ms_1, ms_5, ms_4, ms_2, ms_2, ms_2, ms_1, ms_1};
static const char *const ms_n[] = {"QueryInterface", "AddRef", "Release", "GetCapabilities", "CheckCapabilities",
    "IsFormatSupported", "QueryPreferredFormat", "GetTimeFormat", "IsUsingTimeFormat", "SetTimeFormat",
    "GetDuration", "GetStopPosition", "GetCurrentPosition", "ConvertTimeFormat", "SetPositions", "GetPositions",
    "GetAvailable", "SetRate", "GetRate", "GetPreroll"};

/* IVideoWindow (IDispatch + 39): positions and styles are taken, the movie fills the window */
METHOD(vw_put, 2) { return S_OK; }
METHOD(vw_get, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }
METHOD(vw_SetWindowPosition, 5) { return S_OK; }
METHOD(vw_rect, 5) { for (int i = 1; i <= 4; i++) if (ARG(i)) W32(ARG(i), 0); return S_OK; }
METHOD(vw_ideal, 3) { if (ARG(1)) W32(ARG(1), 0); if (ARG(2)) W32(ARG(2), 0); return S_OK; }
METHOD(vw_NotifyOwnerMessage, 5) { return S_OK; }
static void (*vw_m[46])(CPU *);
static const char *const vw_n[46] = {"QueryInterface", "AddRef", "Release", "GetTypeInfoCount", "GetTypeInfo",
    "GetIDsOfNames", "Invoke", "put_Caption", "get_Caption", "put_WindowStyle", "get_WindowStyle",
    "put_WindowStyleEx", "get_WindowStyleEx", "put_AutoShow", "get_AutoShow", "put_WindowState",
    "get_WindowState", "put_BackgroundPalette", "get_BackgroundPalette", "put_Visible", "get_Visible",
    "put_Left", "get_Left", "put_Width", "get_Width", "put_Top", "get_Top", "put_Height", "get_Height",
    "put_Owner", "get_Owner", "put_MessageDrain", "get_MessageDrain", "get_BorderColor", "put_BorderColor",
    "get_FullScreenMode", "put_FullScreenMode", "SetWindowForeground", "NotifyOwnerMessage",
    "SetWindowPosition", "GetWindowPosition", "GetMinIdealImageSize", "GetMaxIdealImageSize",
    "GetRestorePosition", "HideCursor", "IsCursorHidden"};

/* IBasicVideo (IDispatch + 32): the movie's size; the rest answered */
METHOD(bv_get_AvgTimePerFrame, 2)
{
    Movie *m = MOV(ARG(0));
    if (ARG(1)) { double v = 1.0 / m->fps; memcpy(gptr(ARG(1)), &v, 8); }
    return S_OK;
}
METHOD(bv_get_w, 2) { Movie *m = MOV(ARG(0)); if (ARG(1)) W32(ARG(1), (uint32_t)m->w); return S_OK; }
METHOD(bv_get_h, 2) { Movie *m = MOV(ARG(0)); if (ARG(1)) W32(ARG(1), (uint32_t)m->h); return S_OK; }
METHOD(bv_GetVideoSize, 3)
{
    Movie *m = MOV(ARG(0));
    if (ARG(1)) W32(ARG(1), (uint32_t)m->w);
    if (ARG(2)) W32(ARG(2), (uint32_t)m->h);
    return S_OK;
}
METHOD(bv_one, 1) { return S_OK; }
METHOD(bv_palette, 5) { if (ARG(4)) W32(ARG(4), 0); return E_FAIL; }
METHOD(bv_image, 3) { return E_FAIL; }
static void (*bv_m[39])(CPU *);
static const char *const bv_n[39] = {"QueryInterface", "AddRef", "Release", "GetTypeInfoCount", "GetTypeInfo",
    "GetIDsOfNames", "Invoke", "get_AvgTimePerFrame", "get_BitRate", "get_BitErrorRate", "get_VideoWidth",
    "get_VideoHeight", "put_SourceLeft", "get_SourceLeft", "put_SourceWidth", "get_SourceWidth",
    "put_SourceTop", "get_SourceTop", "put_SourceHeight", "get_SourceHeight", "put_DestinationLeft",
    "get_DestinationLeft", "put_DestinationWidth", "get_DestinationWidth", "put_DestinationTop",
    "get_DestinationTop", "put_DestinationHeight", "get_DestinationHeight", "SetSourcePosition",
    "GetSourcePosition", "SetDefaultSourcePosition", "SetDestinationPosition", "GetDestinationPosition",
    "SetDefaultDestinationPosition", "GetVideoSize", "GetVideoPaletteEntries", "GetCurrentImage",
    "IsUsingDefaultSource", "IsUsingDefaultDestination"};

/* IBasicAudio (IDispatch + 4) */
/* (volume in 1/100 dB: 0 full, -10000 silent) */
METHOD(ba_put_Volume, 2)
{
    Movie *m = MOV(ARG(0));
    int v = (int)ARG(1);
    m->gain = v >= 0 ? 1.0f : v <= -10000 ? 0.0f : powf(10.0f, v / 2000.0f);
    LOG(LOG_ALWAYS, "movie: volume %d (gain %.2f)", v, m->gain);
    if (m->voice >= 0) voice_set_gain(m->voice, m->gain, m->gain);
    return S_OK;
}
METHOD(ba_get_Volume, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }
OK(ba_put_Balance, 2)
METHOD(ba_get_Balance, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }
static void (*const ba_m[])(CPU *) = {any_QueryInterface, any_AddRef, any_Release, disp_GetTypeInfoCount,
    disp_GetTypeInfo, disp_GetIDsOfNames, disp_Invoke, ba_put_Volume, ba_get_Volume, ba_put_Balance, ba_get_Balance};
static const char *const ba_n[] = {"QueryInterface", "AddRef", "Release", "GetTypeInfoCount", "GetTypeInfo",
    "GetIDsOfNames", "Invoke", "put_Volume", "get_Volume", "put_Balance", "get_Balance"};

#define N(a) ((int)(sizeof a / sizeof *a))
static void classes(void)
{
    static int done;
    if (done++)
        return;
    void (*const disp[7])(CPU *) = {any_QueryInterface, any_AddRef, any_Release, disp_GetTypeInfoCount,
        disp_GetTypeInfo, disp_GetIDsOfNames, disp_Invoke};
    /* IVideoWindow: put_/get_ pairs from 7 to 36 (get_BorderColor/put_BorderColor and
       get_/put_FullScreenMode come get first), then the methods */
    for (int i = 0; i < 46; i++)
        vw_m[i] = i < 7 ? disp[i] : (i - 7) % 2 == 0 ? vw_put : vw_get;
    vw_m[33] = vw_get; vw_m[34] = vw_put; vw_m[35] = vw_get; vw_m[36] = vw_put;
    vw_m[37] = vw_put;                          /* SetWindowForeground(focus) */
    vw_m[38] = vw_NotifyOwnerMessage;
    vw_m[39] = vw_SetWindowPosition;
    vw_m[40] = vw_rect; vw_m[41] = vw_ideal; vw_m[42] = vw_ideal; vw_m[43] = vw_rect;
    vw_m[44] = vw_put;                          /* HideCursor(hide) */
    vw_m[45] = vw_get;                          /* IsCursorHidden(&hidden) */
    /* IBasicVideo: properties 7-27 (one argument each), then the methods */
    for (int i = 0; i < 39; i++)
        bv_m[i] = i < 7 ? disp[i] : i < 28 ? (i < 12 || i % 2 ? vw_get : vw_put) : vw_rect;
    bv_m[7] = bv_get_AvgTimePerFrame;
    bv_m[10] = bv_get_w;
    bv_m[11] = bv_get_h;
    bv_m[12] = vw_put; bv_m[14] = vw_put; bv_m[16] = vw_put; bv_m[18] = vw_put;
    bv_m[20] = vw_put; bv_m[22] = vw_put; bv_m[24] = vw_put; bv_m[26] = vw_put;
    bv_m[13] = vw_get; bv_m[15] = vw_get; bv_m[17] = vw_get; bv_m[19] = vw_get;
    bv_m[21] = vw_get; bv_m[23] = vw_get; bv_m[25] = vw_get; bv_m[27] = vw_get;
    bv_m[28] = vw_SetWindowPosition; bv_m[29] = vw_rect; bv_m[30] = bv_one;
    bv_m[31] = vw_SetWindowPosition; bv_m[32] = vw_rect; bv_m[33] = bv_one;
    bv_m[34] = bv_GetVideoSize;
    bv_m[35] = bv_palette;
    bv_m[36] = bv_image;
    bv_m[37] = bv_one; bv_m[38] = bv_one;
    com_class_init(&graph_cls, "IGraphBuilder", N(gb_m), gb_m, gb_n);
    com_class_init(&control_cls, "IMediaControl", N(mc_m), mc_m, mc_n);
    com_class_init(&event_cls, "IMediaEventEx", N(me_m), me_m, me_n);
    com_class_init(&seeking_cls, "IMediaSeeking", N(ms_m), ms_m, ms_n);
    com_class_init(&vwin_cls, "IVideoWindow", 46, (void (*const *)(CPU *))vw_m, vw_n);
    com_class_init(&bvideo_cls, "IBasicVideo", 39, (void (*const *)(CPU *))bv_m, bv_n);
    com_class_init(&baudio_cls, "IBasicAudio", N(ba_m), ba_m, ba_n);
}

/* ole32.c's CoCreateInstance: REGDB_E_CLASSNOTREG for anything but the filter graph */
uint32_t video_cocreate(uint32_t clsid, uint32_t iid, uint32_t ppv)
{
    if (!is(clsid, &CLSID_FilterGraph))
        return 0x80040154u;
    classes();
    Movie *m = calloc(1, sizeof *m);
    if (m) m->gain = 1.0f;
    m->voice = -1;
    m->graph = com_new(&graph_cls, m);
    m->control = com_new(&control_cls, m);
    m->event = com_new(&event_cls, m);
    m->seeking = com_new(&seeking_cls, m);
    m->vwin = com_new(&vwin_cls, m);
    m->bvideo = com_new(&bvideo_cls, m);
    m->baudio = com_new(&baudio_cls, m);
    uint32_t r = qi(m, iid, ppv);
    if (r) {
        free(m);
        return r;
    }
    return S_OK;
}
