/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Renders the stage screen, in a window or straight to a file.
 *
 *   btui                          a window you can drive from the keyboard
 *   btui --shot out.raw           one offscreen frame, raw RGBA
 *
 * The offscreen path exists so the UI can be built and reviewed over SSH on a
 * machine with no GPU and no display, and so layout changes are diffable the
 * way the golden render made mixer changes diffable.
 *
 * The transport here is SIMULATED - a clock and a BPM, no audio. This is a
 * prototype for judging feel and legibility, and saying so in the window
 * title matters more than it sounds: a UI that looks finished invites trust
 * it has not earned yet.
 */
#include <windows.h>
#include <d3d11.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "backtrack/bt_player.h"
#include "backtrack/bt_device.h"

#include "backtrack/bt_demo.h"
#include "backtrack/bt_wav.h"

#include "bt_ui.h"
#include "bt_ui_edit.h"
#include "bt_ui_start.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

/* ------------------------------------------------------------ demo data */

struct Demo { const char *title, *artist; double bpm; bool segue; const char *tuning; };
const Demo kDemo[] = {
    { "1985",                 "Bowling for Soup",  156.0, false, "E0" },
    { "Mr. Brightside",       "The Killers",       148.0, false, "E1" },
    { "Sweet Child O' Mine",  "Guns N' Roses",     125.0, false, "E1" },
    { "Basket Case",          "Green Day",         180.0, true , "E1" },
    { "When I Come Around",   "Green Day",         154.0, false, "E1" },
    { "Africa",               "Toto",               93.0, false, "E0" },
    { "Mr. Jones",            "Counting Crows",    139.0, false, "E0" },
    { "Semi-Charmed Life",    "Third Eye Blind",   103.0, false, "E0" },
    { "Come As You Are",      "Nirvana",           120.0, false, "D1" },
    { "Learn to Fly",         "Foo Fighters",      136.0, false, "E0" },
    { "The Middle",           "Jimmy Eat World",   162.0, false, "E0" },
    { "All the Small Things", "blink-182",         148.0, false, "E0" },
    { "Island in the Sun",    "Weezer",            115.0, false, "E0" },
    { "Everlong",             "Foo Fighters",      158.0, false, "E0" },
    { "Song 2",               "Blur",              130.0, false, "E0" },
};

bt_setlist *make_demo_setlist() {
    const int n = (int)(sizeof(kDemo) / sizeof(kDemo[0]));
    bt_setlist *sl = (bt_setlist *)calloc(1, sizeof(bt_setlist));
    std::snprintf(sl->name, sizeof(sl->name), "Set List #1");
    sl->song = (bt_song *)calloc((size_t)n, sizeof(bt_song));
    sl->nsongs = n;
    for (int i = 0; i < n; i++) {
        bt_song &s = sl->song[i];
        std::snprintf(s.title,  sizeof(s.title),  "%s", kDemo[i].title);
        std::snprintf(s.artist, sizeof(s.artist), "%s", kDemo[i].artist);
        std::snprintf(s.tuning, sizeof(s.tuning), "%s", kDemo[i].tuning);
        /* One song carries a cue, so the screen gets reviewed with one on it. */
        if (i == 2)
            std::snprintf(s.cue, sizeof(s.cue), "%s",
                          "drums in at 24  \xc2\xb7  stop on 114, segue");
        s.tempo.seg[0].bpm = kDemo[i].bpm;
        s.tempo.nseg = 1;
        s.tempo.sig_num = 4;
        s.tempo.sig_den = 4;
        s.on_end = kDemo[i].segue ? BT_ON_END_NEXT : BT_ON_END_STOP;
        s.count_in_bars = 1;
    }
    return sl;
}

/* --------------------------------------------------------------- app */

/* Real transport where a device can be opened, simulated where one cannot.
 *
 * The simulation exists for two honest reasons: --shot renders screens on
 * machines with no audio at all, and a laptop with the interface unplugged
 * should still let you build a set list. It is never a substitute for
 * playback when playback is possible - taking a device.json and then not
 * opening a device is exactly the behaviour that made this UI misleading. */
struct App {
    bt_setlist   *sl     = nullptr;
    bt_device_cfg dev{};
    bt_player    *player = nullptr;
    bt_device    *device = nullptr;
    bool          live   = false;      /* a real stream is open */
    int32_t       selected = 0;
    bool          show_clock = false;
    char          note[200] = {0};      /* startup / failure message        */
    char          dev_name[160] = {0};  /* short name for the on-screen badge */
    char          dev_why[120]  = {0};  /* why it is not live               */
    uint64_t      xruns_seen = 0;       /* kept across a device going away  */
    char          setlist_path[BT_MAX_PATH] = {0};
    /* Holding on the playing screen after a song ends, with the next one
     * queued. Cleared by going to the set list, or by starting something. */
    bool          armed = false;
    /* A start that is waiting for the loader rather than blocking on it. */
    bool          pending_start    = false;
    bool          pending_count_in = false;
    int32_t       pending_song     = -1;
    LARGE_INTEGER pending_since    = {};
    char          device_path[BT_MAX_PATH]  = {0};

    /* Simulated fallback, used only when live is false. */
    bool          sim_playing = false;
    LARGE_INTEGER freq{}, t0{};
    double        sim_start = 0.0;

    double sim_now() const {
        LARGE_INTEGER n;
        QueryPerformanceCounter(&n);
        return sim_start + (double)(n.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    }
};

App g_app;
bt_ui_settings g_settings;
bt_ui_start    g_start;

/* Nothing open: the start screen is showing. */
bool nothing_open() { return g_app.sl == nullptr; }

bool file_exists(const char *path) {
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void audio_cb(float *const *out, int32_t nframes, void *user) {
    bt_player_render((bt_player *)user, out, nframes);
}

/* Start the selected song once it is in memory.
 *
 * bt_player_select waits for the loader, on this thread, for up to its
 * timeout. That wait is a frozen window: no redraw, no response, and from the
 * outside a key that did nothing - which is exactly what a newly added stem
 * produces, because nothing has decoded it yet. Waiting is the right
 * behaviour for the engine and the wrong behaviour for a UI, so the UI asks
 * whether the song is resident first and keeps asking until it is. */
void transport_start(App &a, bool count_in) {
    if (a.live && a.player && !bt_player_song_resident(a.player, a.selected)) {
        /* Ask for it. Without this the wait below never ends: the preload
         * window holds the current song and the next, so one played earlier
         * has been freed, and nothing loads it again until something asks.
         * Jumping backwards through a set is the normal way to hit this. */
        bt_player_request(a.player, a.selected);
        a.pending_start    = true;
        a.pending_count_in = count_in;
        a.pending_song     = a.selected;
        QueryPerformanceCounter(&a.pending_since);
        a.note[0] = '\0';
        return;
    }
    a.pending_start = false;

    if (a.live) {
        /* This used to `return` on failure and say nothing, so a stem that
         * would not load looked exactly like a dead keyboard: you pressed
         * play, and the program sat there. The player knows precisely which
         * song and why; there is no reason to keep it. */
        bt_err e = bt_player_select(a.player, a.selected);
        if (e != BT_OK) {
            int32_t bad = -1;
            bt_err le = bt_player_load_error(a.player, &bad);
            if (le != BT_OK && bad >= 0 && bad < a.sl->nsongs)
                std::snprintf(a.note, sizeof(a.note), "%s: %s",
                              a.sl->song[bad].title, bt_strerror(le));
            else if (e == BT_ERR_RANGE)
                std::snprintf(a.note, sizeof(a.note),
                              "a track is routed to a channel this device does "
                              "not have - check Edit \xe2\x86\x92 audio");
            else
                std::snprintf(a.note, sizeof(a.note), "could not start: %s",
                              bt_strerror(e));
            return;
        }
        a.note[0] = '\0';
        if (count_in) bt_player_start(a.player);
        else { bt_player_stop(a.player); bt_player_play(a.player); }
        return;
    }
    const bt_song &s = a.sl->song[a.selected];
    QueryPerformanceCounter(&a.t0);
    double bpm = s.tempo.nseg ? s.tempo.seg[0].bpm : 120.0;
    int beats = s.count_in_bars * (s.tempo.sig_num > 0 ? s.tempo.sig_num : 4);
    a.sim_start = count_in ? -(beats * 60.0 / bpm) : 0.0;
    a.sim_playing = true;
}

bool transport_playing(const App &a);   /* defined below */

/* Ask the loader for whatever is selected, so browsing the set warms it.
 *
 * Only while stopped. Moving the window frees songs outside it, and
 * bt_engine_render touches no stem audio unless it is playing - which is the
 * same assumption bt_player_select already makes when it moves the window.
 * While playing, the window belongs to the transport. */
void queue_selected(App &a) {
    if (a.live && a.player && !transport_playing(a))
        bt_player_request(a.player, a.selected);
}

void transport_stop(App &a) {
    a.pending_start = false;
    a.note[0] = '\0';
    if (a.live) bt_player_stop(a.player);
    else        a.sim_playing = false;
}

bool transport_playing(const App &a) {
    return a.live ? bt_player_playing(a.player) : a.sim_playing;
}

int32_t transport_current(const App &a) {
    return a.live ? bt_player_current(a.player) : a.selected;
}

void fill_state(bt_ui_state &st, App &a) {
    std::memset(&st, 0, sizeof(st));
    st.setlist     = a.sl;
    st.selected    = a.selected;
    st.show_clock  = a.show_clock;
    st.sample_rate = a.dev.sample_rate > 0 ? a.dev.sample_rate : 48000;
    st.playing     = transport_playing(a);
    st.armed       = a.armed && !st.playing;
    st.armed_song  = a.selected;
    st.loading     = a.pending_start;
    st.loading_song = a.pending_song;

    int32_t cur = transport_current(a);
    if (cur < 0) cur = 0;
    if (cur >= a.sl->nsongs) cur = a.sl->nsongs - 1;
    st.current = cur;
    if (a.sl->nsongs == 0) return;

    const bt_song &s = a.sl->song[cur];
    st.bpm           = s.tempo.nseg ? s.tempo.seg[0].bpm : 120.0;
    st.beats_per_bar = s.tempo.sig_num > 0 ? s.tempo.sig_num : 4;

    st.playhead = a.live ? bt_player_playhead(a.player)
                         : (bt_frame)((a.sim_playing ? a.sim_now() : 0.0) * st.sample_rate);

    /* Beat position comes from the same tempo map the click is generated
     * from, so the number on screen and the click in the ears cannot
     * disagree. */
    st.beat = bt_tempo_frame_beat(&s.tempo, st.playhead, st.sample_rate);
    if (st.beat < 0) {
        st.count_in_left = (int32_t)(-st.beat);
        int n = st.beats_per_bar;
        int m = (int)(st.beat % n);
        if (m < 0) m += n;
        st.beat_in_bar = m;
        st.bar = 0;
    } else {
        st.bar         = (int32_t)(st.beat / st.beats_per_bar) + 1;
        st.beat_in_bar = (int32_t)(st.beat % st.beats_per_bar);
    }

    st.elapsed_sec = (double)st.playhead / st.sample_rate;
    st.total_sec   = (double)bt_song_length(&s, st.sample_rate) / st.sample_rate;
    st.xruns       = a.live ? bt_device_xruns(a.device) : a.xruns_seen;
    st.device_live = a.live;
    st.device_name = a.dev_name[0] ? a.dev_name : nullptr;
    st.device_note = a.dev_why[0] ? a.dev_why
                   : (a.note[0]   ? a.note : nullptr);
}

/* ----------------------------------------------------------- d3d11 bits */

ID3D11Device        *g_dev = nullptr;
ID3D11DeviceContext *g_ctx = nullptr;
IDXGISwapChain      *g_swap = nullptr;
ID3D11RenderTargetView *g_rtv = nullptr;

bool make_device(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL fl;
    /* Hardware if there is any; WARP otherwise, which is what makes this run
     * on a VM with a Basic Display Adapter. */
    const D3D_DRIVER_TYPE types[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
    for (D3D_DRIVER_TYPE t : types) {
        if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(
                nullptr, t, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                &sd, &g_swap, &g_dev, &fl, &g_ctx)))
            return true;
    }
    return false;
}

void make_rtv() {
    ID3D11Texture2D *back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (!back) return;
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

void drop_rtv() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

bt_ui_edit g_edit;
bool g_editing = false;
bool g_quit = false;
bool g_fullscreen = false;

void toggle_fullscreen(HWND hwnd) {
    static RECT saved{};
    static DWORD saved_style = 0;
    if (!g_fullscreen) {
        GetWindowRect(hwnd, &saved);
        saved_style = (DWORD)GetWindowLongPtr(hwnd, GWL_STYLE);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongPtr(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
        g_fullscreen = true;
    } else {
        SetWindowLongPtr(hwnd, GWL_STYLE, saved_style);
        SetWindowPos(hwnd, HWND_TOP, saved.left, saved.top,
                     saved.right - saved.left, saved.bottom - saved.top,
                     SWP_FRAMECHANGED);
        g_fullscreen = false;
    }
}

void close_set() {
    App &a = g_app;
    g_start.rescan = true;      /* the folder may have changed since last time */
    if (a.device) { bt_device_stop(a.device); bt_device_close(a.device); a.device = nullptr; }
    if (a.player) { bt_player_destroy(a.player); a.player = nullptr; }
    if (a.sl)     { bt_setlist_free(a.sl); a.sl = nullptr; }
    /* The editor borrows the set list; it must not outlive it. */
    g_edit.sl = nullptr;
    g_edit.dev = nullptr;
    g_edit.song = 0;
    g_edit.track = -1;
    g_edit.peaks_song = g_edit.peaks_track = -1;
    g_edit.peaks_src = nullptr;
    g_edit.last_sig = 0;
    g_editing = false;
    a.live = false;
    a.selected = 0;
    a.dev_name[0] = a.dev_why[0] = '\0';
}

void reopen_audio(HWND hwnd);

/* Load a set list and bring up audio for it. device.json defaults to sitting
 * beside the set list, which is where the folder-is-one-unit rule puts it. */
bool open_set(HWND hwnd, const char *setlist_path, const char *device_path) {
    App &a = g_app;
    close_set();

    int line = 0;
    bt_setlist *sl = nullptr;
    bt_err e = bt_setlist_load_file(setlist_path, &sl, &line);
    if (e != BT_OK) {
        std::snprintf(g_start.status, sizeof(g_start.status),
                      "%s: %s (line %d)", setlist_path, bt_strerror(e), line);
        return false;
    }
    a.sl = sl;
    std::snprintf(a.setlist_path, sizeof(a.setlist_path), "%s", setlist_path);

    /* Where routing comes from, most specific first:
     *   1. --device, when someone asked for a particular file
     *   2. a device.json in the set list folder, which is what every version
     *      before this wrote, and is still how you pin one set to odd routing
     *   3. the machine's own, in %APPDATA% - the normal case, because the
     *      interface belongs to the laptop and not to any one set list
     * Nothing found means the built-in defaults, which are now good ones. */
    char dpath[BT_MAX_PATH];
    char beside[BT_MAX_PATH];
    std::snprintf(beside, sizeof(beside), "%s", setlist_path);
    {
        char *slash = std::strrchr(beside, '\\');
        char *fwd   = std::strrchr(beside, '/');
        if (fwd && (!slash || fwd > slash)) slash = fwd;
        if (slash) std::snprintf(slash + 1, sizeof(beside) - (size_t)(slash + 1 - beside),
                                 "device.json");
        else std::snprintf(beside, sizeof(beside), "device.json");
    }

    if (device_path && *device_path) {
        std::snprintf(dpath, sizeof(dpath), "%s", device_path);
    } else if (file_exists(beside)) {
        std::snprintf(dpath, sizeof(dpath), "%s", beside);
    } else if (!bt_ui_machine_device_path(dpath, sizeof(dpath))) {
        std::snprintf(dpath, sizeof(dpath), "%s", beside);
    }

    std::snprintf(a.device_path, sizeof(a.device_path), "%s", dpath);

    bt_device_cfg_defaults(&a.dev);
    a.dev.buffer_frames = 512;
    a.dev.sample_rate   = 48000;
    std::snprintf(a.dev.api, sizeof(a.dev.api), "WASAPI");
    int dline = 0;
    bt_device_cfg_load_file(dpath, &a.dev, &dline);   /* defaults stand if absent */

    int32_t nch = 0;
    for (int32_t i = 0; i < a.dev.nbuses; i++)
        for (int32_t k = 0; k < a.dev.bus[i].nch; k++)
            if (a.dev.bus[i].ch[k] + 1 > nch) nch = a.dev.bus[i].ch[k] + 1;
    if (nch <= 0) nch = 2;

    bt_player_cfg pc = { a.dev.sample_rate, nch, a.dev.buffer_frames, 1, 30000 };
    if (bt_player_create(&pc, a.sl, &a.dev, &a.player) != BT_OK ||
        bt_player_select(a.player, 0) != BT_OK) {
        std::snprintf(g_start.status, sizeof(g_start.status),
                      "could not prepare \"%s\" - check its stems with the "
                      "validation screen", setlist_path);
        close_set();
        return false;
    }

    g_edit.sl  = a.sl;
    g_edit.dev = &a.dev;
    g_edit.can_save = true;
    g_edit.can_save_device = true;
    g_edit.song = 0;
    g_edit.screen = bt_edit_screen::setlist;
    std::snprintf(g_edit.path, sizeof(g_edit.path), "%s", setlist_path);
    std::snprintf(g_edit.device_path, sizeof(g_edit.device_path), "%s", dpath);

    reopen_audio(hwnd);
    bt_ui_settings_touch(g_settings, setlist_path, dpath);
    g_start.status[0] = '\0';
    return true;
}

/* Render a song, or the whole set, to a WAV file.
 *
 * This is btrender's job done in-process. It renders through a second,
 * offline player rather than the live one, so exporting cannot disturb what
 * is bound for playback - and it is only reachable while stopped anyway. */
bool export_wav(const char *path, bool whole_set) {
    App &a = g_app;
    if (!a.sl || a.sl->nsongs == 0) return false;

    int32_t nch = 0;
    for (int32_t i = 0; i < a.dev.nbuses; i++)
        for (int32_t k = 0; k < a.dev.bus[i].nch; k++)
            if (a.dev.bus[i].ch[k] + 1 > nch) nch = a.dev.bus[i].ch[k] + 1;
    if (nch <= 0) return false;

    const int32_t block = 1024;
    bt_player_cfg pc = { a.dev.sample_rate, nch, block, 1, 30000 };
    bt_player *p = nullptr;
    if (bt_player_create(&pc, a.sl, &a.dev, &p) != BT_OK) return false;

    int32_t first = whole_set ? 0 : g_edit.song;
    if (first < 0 || first >= a.sl->nsongs) first = 0;
    if (bt_player_select(p, first) != BT_OK) { bt_player_destroy(p); return false; }

    bt_frame cap = (bt_frame)a.dev.sample_rate * 60 * 30;   /* a guard, not a limit */
    bt_frame len = 0;
    float **buf = (float **)calloc((size_t)nch, sizeof(float *));
    bt_frame have = 1 << 16;
    for (int32_t c = 0; c < nch; c++) buf[c] = (float *)calloc((size_t)have, sizeof(float));

    bt_player_start(p);
    float *win[BT_MAX_OUT_CH];
    bool done = false;
    while (!done && len < cap) {
        if (len + block > have) {
            bt_frame grown = have * 2;
            for (int32_t c = 0; c < nch; c++) {
                float *q = (float *)realloc(buf[c], (size_t)grown * sizeof(float));
                if (!q) { done = true; break; }
                memset(q + have, 0, (size_t)(grown - have) * sizeof(float));
                buf[c] = q;
            }
            if (done) break;
            have = grown;
        }
        for (int32_t c = 0; c < nch; c++) win[c] = buf[c] + len;
        bt_player_render(p, win, block);
        len += block;

        bt_tick_result t = BT_TICK_IDLE;
        if (bt_player_tick(p, &t) != BT_OK) break;
        if (t == BT_TICK_SONG_ENDED) {
            if (whole_set && bt_player_current(p) + 1 < bt_player_count(p)) {
                if (bt_player_next(p) != BT_OK) break;
                bt_player_play(p);
            } else {
                done = true;
            }
        }
    }

    bt_err e = bt_wav_write_file(path, (const float *const *)buf, nch,
                                 a.dev.sample_rate, len);
    for (int32_t c = 0; c < nch; c++) free(buf[c]);
    free(buf);
    bt_player_destroy(p);
    return e == BT_OK;
}

/* Open (or re-open) the stream from whatever g_app.dev currently says.
 * Startup and the routing editor both come through here, so there is one
 * description of what "open the audio" means. */
/* The product name first, because that is what a taskbar button shows when
 * there is no room for the rest of it. */
void set_title(HWND hwnd, const char *detail) {
    wchar_t w[320];
    if (detail && *detail) {
        char buf[320];
        std::snprintf(buf, sizeof(buf), "BackingTrackLive  -  %s", detail);
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, w, 320);
    } else {
        wcscpy_s(w, 320, L"BackingTrackLive");
    }
    SetWindowTextW(hwnd, w);
}

void reopen_audio(HWND hwnd) {
    App &a = g_app;

    if (a.device) {
        bt_device_stop(a.device);
        bt_device_close(a.device);
        a.device = nullptr;
    }
    a.live = false;
    a.dev_why[0] = '\0';

    int32_t nch = 0;
    for (int32_t i = 0; i < a.dev.nbuses; i++)
        for (int32_t k = 0; k < a.dev.bus[i].nch; k++)
            if (a.dev.bus[i].ch[k] + 1 > nch) nch = a.dev.bus[i].ch[k] + 1;

    if (!a.player || nch <= 0) {
        std::snprintf(a.dev_why, sizeof(a.dev_why), "no buses defined");
        return;
    }

    const char *want = (a.dev.device[0] && std::strcmp(a.dev.device, "default") != 0)
                     ? a.dev.device : nullptr;
    int32_t idx = bt_device_best(want, a.dev.api[0] ? a.dev.api : nullptr);
    if (idx == BT_DEVICE_DEFAULT && (want || a.dev.api[0])) {
        std::snprintf(a.dev_why, sizeof(a.dev_why), "no device matching that name and API");
        return;
    }

    bt_device_open_cfg oc = { idx, nch, a.dev.sample_rate, a.dev.buffer_frames };
    bt_err e = bt_device_open(&oc, audio_cb, a.player, &a.device);
    if (e != BT_OK || bt_device_start(a.device) != BT_OK) {
        std::snprintf(a.dev_why, sizeof(a.dev_why), "could not open: %s", bt_strerror(e));
        bt_device_close(a.device);
        a.device = nullptr;
        return;
    }

    a.live = true;
    bt_device_info di;
    if (bt_device_get(idx, &di) != BT_OK) std::memset(&di, 0, sizeof(di));
    std::snprintf(a.dev_name, sizeof(a.dev_name), "%s  \xc2\xb7  %s",
                  di.name[0] ? di.name : "default", di.api);

    set_title(hwnd, a.dev_name);
}

void on_key(HWND hwnd, WPARAM key) {
    App &a = g_app;

    /* With nothing open there is no transport to drive; the start screen is
     * mouse-driven and the keys would act on a set list that is not there. */
    if (nothing_open()) {
        if (key == VK_F11) toggle_fullscreen(hwnd);
        return;
    }

    if (g_editing) {
        if (key == VK_F11) { toggle_fullscreen(hwnd); return; }
        if (key == 'E' && !ImGui::GetIO().WantTextInput) {
            if (transport_playing(a)) transport_stop(a);   /* the audition */
            g_editing = false;
            return;
        }
        bt_ui_edit_key(g_edit, (int)key);
        return;
    }

    /* Armed: stopped, holding the playing screen with the next song queued. */
    if (a.armed) {
        switch (key) {
        case VK_UP:
            if (a.selected > 0) { a.selected--; queue_selected(a); }
            return;
        case VK_DOWN:
            if (a.selected + 1 < a.sl->nsongs) { a.selected++; queue_selected(a); }
            return;
        case 'L':
        case VK_ESCAPE:
            a.armed = false;              /* back to the set list */
            return;
        case VK_SPACE:
        case VK_RETURN:
            a.armed = false;
            transport_start(a, key == VK_SPACE);
            return;
        case 'E':
            a.armed = false;
            break;                        /* fall through to the normal keys */
        default:
            break;
        }
    }

    switch (key) {
    case VK_ESCAPE:
        /* ESC never quits. It used to, and a key that close to the rest of
         * the transport should not be able to end the show - there is no
         * confirmation that would make that safe at 11pm. Leaving fullscreen
         * is all it does here; the window's close button still works. */
        if (g_fullscreen) toggle_fullscreen(hwnd);
        break;
    case VK_SPACE:
        if (transport_playing(a)) transport_stop(a);
        else                      transport_start(a, true);
        break;
    case VK_RETURN:
        if (!transport_playing(a)) transport_start(a, false);  /* no count-in */
        break;
    case VK_UP:
        /* Browsing is a stopped-only activity. While playing, the set list is
         * not on screen, so moving a selection you cannot see and then having
         * space jump somewhere unexpected is the worst of both. */
        if (!transport_playing(a) && a.selected > 0) { a.selected--; queue_selected(a); }
        break;
    case VK_DOWN:
        if (!transport_playing(a) && a.selected + 1 < a.sl->nsongs) { a.selected++; queue_selected(a); }
        break;
    case 'N':
        /* The deliberate way to move on mid-song: explicit, one key, and it
         * does not depend on a selection you cannot see. */
        if (a.selected + 1 < a.sl->nsongs) {
            a.selected++;
            if (transport_playing(a)) transport_start(a, false);
        }
        break;
    case 'C':
        a.show_clock = !a.show_clock;
        break;
    case 'E':
        /* Blocked while playing, deliberately: the one thing worse than a
         * fiddly editor is one that can appear over a performance. */
        if (!transport_playing(a)) g_editing = !g_editing;
        break;
    case VK_F11:
        toggle_fullscreen(hwnd);
        break;
    default: break;
    }
}

LRESULT WINAPI wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_dev && wp != SIZE_MINIMIZED) {
            drop_rtv();
            g_swap->ResizeBuffers(0, (UINT)LOWORD(lp), (UINT)HIWORD(lp),
                                  DXGI_FORMAT_UNKNOWN, 0);
            make_rtv();
        }
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        on_key(hwnd, wp);
        return 0;
    case WM_DESTROY:
        g_quit = true;
        PostQuitMessage(0);
        return 0;
    default: break;
    }
    /* W, not the generic macro. The class is registered with
     * RegisterClassExW, so this window is Unicode; DefWindowProcA would read
     * the Unicode WM_SETTEXT as ANSI and stop at the first zero byte, which
     * is why the title bar used to read "B". */
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void load_font() {
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 48.0f))
        io.Fonts->AddFontDefault();
}

/* -------------------------------------------------------- offscreen path */

int run_shot(const char *out, int w, int h, const char *state, int song, int bar) {
    const bool armed_shot = !std::strcmp(state, "armed");
    D3D_FEATURE_LEVEL fl;
    ID3D11Device *dev = nullptr; ID3D11DeviceContext *ctx = nullptr;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                 nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx))) {
        std::fprintf(stderr, "WARP device failed\n"); return 1;
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *rt = nullptr;
    ID3D11RenderTargetView *rtv = nullptr;
    dev->CreateTexture2D(&td, nullptr, &rt);
    dev->CreateRenderTargetView(rt, nullptr, &rtv);

    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *stg = nullptr;
    dev->CreateTexture2D(&td, nullptr, &stg);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2((float)w, (float)h);
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    load_font();
    ImGui_ImplDX11_Init(dev, ctx);

    bt_setlist *sl = make_demo_setlist();
    bt_ui_state st = {};
    st.setlist = sl; st.current = song; st.selected = song;
    st.sample_rate = 48000; st.beats_per_bar = 4;
    st.bpm = sl->song[song].tempo.seg[0].bpm;
    st.bar = bar; st.beat_in_bar = 2;
    st.elapsed_sec = 134.0; st.total_sec = 303.0;
    st.device_live = true;
    st.device_name = "OUT 1-4 (BEHRINGER UMC 404HD)  \xc2\xb7  Windows WASAPI";
    st.xruns       = 0;
    /* The state worth having a picture of: the interface has gone. */
    if (!std::strcmp(state, "disconnected")) {
        st.device_live = false;
        st.device_name = nullptr;
        st.device_note = "interface disconnected";
    }

    if (armed_shot) {
        /* Stopped, holding the playing screen with the next song queued. */
        st.playing    = false;
        st.armed      = true;
        st.armed_song = (song + 1 < st.setlist->nsongs) ? song + 1 : song;
        st.playhead   = 0;
    } else if (!std::strcmp(state, "playing")) {
        st.playing = true;
        st.playhead = (bt_frame)(st.elapsed_sec * st.sample_rate);
        st.beat = (int64_t)(st.elapsed_sec * st.bpm / 60.0);
    } else if (!std::strcmp(state, "countin")) {
        st.playing = true;
        st.playhead = -(bt_frame)(1.5 * st.sample_rate);
        st.beat = -3;
        st.count_in_left = 3;
        st.beat_in_bar = 1;
        /* Nothing has started, so nothing has elapsed. The fixture used to
         * leave the playing value here and the still showed a progress bar
         * 45% along during a count-in - a screenshot that lied about the
         * thing it existed to show. */
        st.elapsed_sec = 0.0;
    }

    bt_ui_edit ed;
    static bt_device_cfg shot_dev;
    bt_device_cfg_defaults(&shot_dev);
    /* The empty state is a screen like any other, so it gets reviewed like
     * one. Fixture recents, because a first run has none and the interesting
     * layout question is what it looks like once it does. */
    static bt_ui_settings shot_settings;
    static bt_ui_start    shot_start;
    const bool start_shot = !std::strcmp(state, "start") ||
                            !std::strcmp(state, "firstrun");
    if (start_shot) {
        shot_start.settings = &shot_settings;
        /* The real scan, so a screenshot shows what a screen with set lists
         * on it actually looks like rather than what I assume it does. */
        shot_start.nfound = bt_ui_scan_setlists(shot_start.found, BT_FOUND_MAX);
        if (!std::strcmp(state, "start")) {
            std::snprintf(shot_settings.recent[0], BT_MAX_PATH,
                          "D:\\band\\covers-2026\\setlist.json");
            std::snprintf(shot_settings.recent[1], BT_MAX_PATH,
                          "D:\\band\\acoustic-duo\\setlist.json");
            std::snprintf(shot_settings.recent[2], BT_MAX_PATH,
                          "C:\\Users\\gary\\Documents\\demo\\setlist.json");
            shot_settings.nrecent = 3;
        }
    }

    const bool edit_shot = !std::strcmp(state, "edit") ||
                           !std::strcmp(state, "editsong") ||
                           !std::strcmp(state, "editaudio") ||
                           !std::strcmp(state, "editalign") ||
                           !std::strcmp(state, "check");
    if (edit_shot) {
        ed.sl  = sl;
        ed.dev = &shot_dev;
        ed.song = song;
        ed.track = 1;
        ed.screen = !std::strcmp(state, "editalign") ? bt_edit_screen::align
                  : !std::strcmp(state, "editsong")  ? bt_edit_screen::song
                  : !std::strcmp(state, "editaudio") ? bt_edit_screen::audio
                                                     : bt_edit_screen::setlist;
        if (ed.screen == bt_edit_screen::align) {
            /* A stem that begins 380 ms after the downbeat: what a purchased
             * backing track with a lead-in actually looks like. */
            if (ed.song < 0 || ed.song >= sl->nsongs) ed.song = 0;
            bt_song &sg = sl->song[ed.song];
            ed.track = 1;
            for (int32_t i = 0; i < sg.ntracks; i++)
                if (sg.track[i].type == BT_TRACK_AUDIO) { ed.track = i; break; }
            bt_track &tr = sg.track[ed.track];
            const int32_t sr = 48000;
            tr.channels = 1;
            tr.frames   = sr * 6;
            tr.pcm = (float **)calloc(1, sizeof(float *));
            tr.pcm[0] = (float *)calloc((size_t)tr.frames, sizeof(float));
            const bt_frame lead = (bt_frame)(sr * 0.38);
            for (bt_frame i = lead; i < tr.frames; i++) {
                /* A note every half second, so the grid relationship is
                 * visible rather than a wall of noise. */
                bt_frame k = (i - lead) % (sr / 2);
                float env = k < sr / 8 ? 1.0f - (float)k / (float)(sr / 8) : 0.0f;
                float ph  = (float)((i * 220) % sr) / (float)sr;
                tr.pcm[0][i] = env * 0.7f * (ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph);
            }
            ed.view_start = -0.2;
            ed.view_len   = 4.0;
        }
        if (ed.screen == bt_edit_screen::audio) {
            /* The screen's job is listing real devices, so enumeration has to
             * be running even for a screenshot. */
            bt_device_init();
            ed.can_save_device = true;
            std::snprintf(ed.device_path, sizeof(ed.device_path), "demo/device.json");
            shot_dev.buffer_frames = 512;
            shot_dev.sample_rate   = 48000;
            std::snprintf(shot_dev.api, sizeof(shot_dev.api), "WASAPI");
            ed.picked_device = bt_device_best(nullptr, "WASAPI");
            if (ed.picked_device >= 0) {
                bt_device_info di;
                if (bt_device_get(ed.picked_device, &di) == BT_OK) {
                    std::snprintf(shot_dev.device, sizeof(shot_dev.device), "%s", di.name);
                    std::snprintf(shot_dev.api, sizeof(shot_dev.api), "%s", di.api);
                }
            }
        }
        /* Give the shot something to show: the demo songs carry no stems. */
        if (ed.song >= 0 && ed.song < sl->nsongs) {
            bt_song &ss = sl->song[ed.song];
            ss.track[0].type = BT_TRACK_CLICK;
            std::snprintf(ss.track[0].name, sizeof(ss.track[0].name), "Click");
            std::snprintf(ss.track[0].bus, sizeof(ss.track[0].bus), "inear");
            ss.track[1].type = BT_TRACK_AUDIO;
            std::snprintf(ss.track[1].name, sizeof(ss.track[1].name), "Synth");
            std::snprintf(ss.track[1].file, sizeof(ss.track[1].file), "stems/synth.wav");
            std::snprintf(ss.track[1].bus, sizeof(ss.track[1].bus), "foh");
            ss.track[1].gain_db = -2.0;
            ss.track[2].type = BT_TRACK_AUDIO;
            std::snprintf(ss.track[2].name, sizeof(ss.track[2].name), "Bass");
            std::snprintf(ss.track[2].file, sizeof(ss.track[2].file), "stems/bass.wav");
            std::snprintf(ss.track[2].bus, sizeof(ss.track[2].bus), "foh");
            ss.track[2].offset_ms = -12;
            ss.ntracks = 3;
        }
    }

    /* Two frames: the first builds the font atlas, the second draws with it. */
    for (int i = 0; i < 2; i++) {
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        if (start_shot)     bt_ui_start_draw(shot_start);
        else if (edit_shot) bt_ui_edit_draw(ed);
        else                (void)bt_ui_draw(st);
        ImGui::Render();
        const float clear[4] = { 0.06f, 0.07f, 0.08f, 1.0f };
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        ctx->Flush();
    }

    ctx->CopyResource(stg, rt);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m))) return 1;
    FILE *f = fopen(out, "wb");
    if (!f) return 1;
    for (int y = 0; y < h; y++)
        fwrite((unsigned char *)m.pData + (size_t)y * m.RowPitch, 1, (size_t)w * 4, f);
    fclose(f);
    ctx->Unmap(stg, 0);
    std::printf("wrote %s (%dx%d RGBA, state=%s)\n", out, w, h, state);
    return 0;
}

int usage() {
    std::fprintf(stderr,
        "usage: btui [--setlist <file>] [--device <file>]\n"
        "         windowed and keyboard-driven. Without --setlist it opens a\n"
        "         built-in demo set, which can be edited but not saved.\n"
        "\n"
        "       btui --shot <out.raw> [--w N] [--h N]\n"
        "            [--state start|firstrun|stopped|playing|countin|disconnected|\n"
"                     edit|editsong|editaudio|check]\n"
        "            [--song N] [--bar N]\n");
    return 2;
}

} /* namespace */

/* A windowed app has no console, so --shot and --help would print into
 * nothing when run from a terminal. If there is a parent console - which
 * there is when someone typed the name, and is not when they double-clicked -
 * borrow it. */
void attach_console_if_launched_from_one(void) {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE *f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
}

int main(int argc, char **argv) {
    if (argc > 1) attach_console_if_launched_from_one();
    const char *shot = nullptr, *state = "stopped";
    const char *setlist_path = nullptr, *device_path = nullptr;
    int w = 1280, h = 720, song = 2, bar = 17;

    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--setlist") && i + 1 < argc) setlist_path = argv[++i];
        else if (!std::strcmp(argv[i], "--device") && i + 1 < argc) device_path = argv[++i];
        else if (!std::strcmp(argv[i], "--w") && i + 1 < argc) w = atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--h") && i + 1 < argc) h = atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--state") && i + 1 < argc) state = argv[++i];
        else if (!std::strcmp(argv[i], "--song") && i + 1 < argc) song = atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--bar") && i + 1 < argc) bar = atoi(argv[++i]);
        else return usage();
    }
    if (shot) {
        if (w < 64 || h < 64 || w > 4096 || h > 4096) return usage();
        return run_shot(shot, w, h, state, song, bar);
    }

    /* ---------------------------------------------------------- windowed */

    HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, wndproc, 0, 0,
                       GetModuleHandle(nullptr), icon, nullptr, nullptr,
                       nullptr, L"BackingTrackLive", icon };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName,
                              L"BackingTrackLive",
                              WS_OVERLAPPEDWINDOW, 100, 100, 1280, 760,
                              nullptr, nullptr, wc.hInstance, nullptr);
    if (!make_device(hwnd)) {
        std::fprintf(stderr, "no D3D11 device (tried hardware and WARP)\n");
        return 1;
    }
    make_rtv();
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    load_font();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    bt_ui_settings_load(g_settings);
    g_start.settings = &g_settings;

    const bool audio_available = (bt_device_init() == BT_OK);

    /* Command-line arguments still work and still win. With none, the last
     * set list opens; with no last set list, the start screen does. Opening
     * an unsaveable demo because nobody passed a flag was a developer's
     * answer to an empty state. */
    const char *want_set = setlist_path ? setlist_path
                         : (g_settings.setlist[0] ? g_settings.setlist : nullptr);
    const char *want_dev = device_path ? device_path
                         : (g_settings.device[0] ? g_settings.device : nullptr);
    if (want_set) open_set(hwnd, want_set, want_dev);

    while (!g_quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) g_quit = true;
        }
        if (g_quit) break;
        if (!g_rtv) { Sleep(10); continue; }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (nothing_open()) {
            g_start.action = bt_start_action::none;
            /* Not every frame. This enumerates a directory, and on a
             * OneDrive-backed Documents folder those entries can be cloud
             * placeholders whose enumeration goes to the network - sixty
             * times a second is a way to make a window stop responding. */
            if (g_start.rescan) {
                g_start.nfound = bt_ui_scan_setlists(g_start.found, BT_FOUND_MAX);
                g_start.rescan = false;
            }
            bt_ui_start_draw(g_start);

            char picked[BT_MAX_PATH];
            switch (g_start.action) {
            case bt_start_action::open:
                if (bt_ui_pick_setlist(picked, sizeof(picked)))
                    open_set(hwnd, picked, nullptr);
                break;
            case bt_start_action::open_recent:
                if (g_start.recent_index >= 0 &&
                    g_start.recent_index < g_settings.nrecent)
                    open_set(hwnd, g_settings.recent[g_start.recent_index], nullptr);
                break;
            case bt_start_action::open_found:
                if (g_start.found_index >= 0 && g_start.found_index < g_start.nfound)
                    open_set(hwnd, g_start.found[g_start.found_index].path, nullptr);
                break;
            case bt_start_action::create_new: {
                /* A folder named after the set list, inside the set lists
                 * folder. No directory picker: offering one is how the
                 * previous version let someone land on the folder that
                 * already held a set list and truncate it. */
                char root[BT_MAX_PATH], dir[BT_MAX_PATH], safe[BT_MAX_NAME];
                if (!bt_ui_default_setlist_root(root, sizeof(root))) {
                    std::snprintf(g_start.status, sizeof(g_start.status),
                                  "could not find your Documents folder");
                    break;
                }

                std::snprintf(safe, sizeof(safe), "%s", g_start.new_name);
                for (char *c = safe; *c; c++)
                    if (std::strchr("\\/:*?\"<>|", *c)) *c = '-';
                std::snprintf(dir, sizeof(dir), "%s\\%s", root, safe);

                /* A clone when one was asked for, otherwise an empty set. */
                bt_err e;
                if (g_start.clone_from >= 0 &&
                    g_start.clone_from < g_start.nfound) {
                    /* The scan records the setlist.json; the folder is its
                     * parent. */
                    char src[BT_MAX_PATH];
                    std::snprintf(src, sizeof(src), "%s",
                                  g_start.found[g_start.clone_from].path);
                    char *cut = std::strrchr(src, '\\');
                    if (cut) *cut = '\0';
                    e = bt_ui_clone_setlist(src, dir);
                } else {
                    e = bt_ui_new_setlist(dir, safe);
                }
                const bool was_clone = g_start.clone_from >= 0;
                g_start.clone_from = -1;

                if (e == BT_ERR_EXISTS) {
                    std::snprintf(g_start.status, sizeof(g_start.status),
                                  "\"%s\" already exists - open it above, or "
                                  "choose another name", safe);
                    break;
                }
                if (e != BT_OK) {
                    std::snprintf(g_start.status, sizeof(g_start.status),
                                  "could not create \"%s\": %s", safe,
                                  bt_strerror(e));
                    break;
                }

                char sp[BT_MAX_PATH];
                std::snprintf(sp, sizeof(sp), "%s\\setlist.json", dir);
                if (open_set(hwnd, sp, nullptr)) {
                    g_editing = true;
                    /* A new set list has one empty song and nothing to listen
                     * to, so it opens on that song. A clone has a whole set
                     * already, and what you came to do is change which songs
                     * are in it - so it opens on the list. */
                    g_edit.screen = was_clone ? bt_edit_screen::setlist
                                              : bt_edit_screen::song;
                    g_edit.song = 0;
                }
                break;
            }
            case bt_start_action::create_demo: {
                char dir[BT_MAX_PATH];
                if (bt_ui_pick_folder("Where should the demo set go?",
                                      dir, sizeof(dir))) {
                    bt_err e = bt_demo_write(dir, 120.0, nullptr, nullptr);
                    if (e != BT_OK) {
                        std::snprintf(g_start.status, sizeof(g_start.status),
                                      "could not write the demo: %s", bt_strerror(e));
                    } else {
                        char sp[BT_MAX_PATH];
                        std::snprintf(sp, sizeof(sp), "%s\\setlist.json", dir);
                        open_set(hwnd, sp, nullptr);
                    }
                }
                break;
            }
            default: break;
            }

            ImGui::Render();
            const float clear[4] = { 0.06f, 0.07f, 0.08f, 1.0f };
            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
            g_ctx->ClearRenderTargetView(g_rtv, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_swap->Present(1, 0);
            continue;
        }

        /* A start that was waiting on the loader. Checked every frame, so the
         * window stays alive and says what it is doing. */
        if (g_app.pending_start && g_app.player) {
            if (g_app.pending_song != g_app.selected) {
                g_app.pending_start = false;      /* they chose something else */
                g_app.note[0] = '\0';
            } else if (bt_player_song_resident(g_app.player, g_app.pending_song)) {
                const bool ci = g_app.pending_count_in;
                g_app.pending_start = false;
                transport_start(g_app, ci);
            } else {
                int32_t bad = -1;
                bt_err le = bt_player_load_error(g_app.player, &bad);
                LARGE_INTEGER now, freq;
                QueryPerformanceCounter(&now);
                QueryPerformanceFrequency(&freq);
                const double waited = (double)(now.QuadPart - g_app.pending_since.QuadPart)
                                    / (double)freq.QuadPart;
                if (le != BT_OK && bad == g_app.pending_song) {
                    g_app.pending_start = false;
                    std::snprintf(g_app.note, sizeof(g_app.note), "%s: %s",
                                  g_app.sl->song[bad].title, bt_strerror(le));
                } else if (waited > 60.0) {
                    g_app.pending_start = false;
                    std::snprintf(g_app.note, sizeof(g_app.note),
                                  "%s is taking longer than a minute to load",
                                  g_app.sl->song[g_app.pending_song].title);
                }
            }
        }

        if (g_app.live) {
            bt_tick_result t = BT_TICK_IDLE;
            bt_player_tick(g_app.player, &t);
            if (t == BT_TICK_ADVANCED) g_app.selected = bt_player_current(g_app.player);

            /* A song that stops holds the screen instead of dropping to the
             * set list: the band has just finished, somebody is talking to
             * the room, and what matters is that the next song is queued and
             * one key starts it. */
            if (t == BT_TICK_SONG_ENDED) {
                g_app.armed = true;
                if (g_app.selected + 1 < g_app.sl->nsongs) g_app.selected++;
            }

            /* Three buffer periods with no callback means the interface is
             * gone. WASAPI reports no error for an unplug - the callbacks
             * simply stop - so their absence is the only signal there is. */
            int32_t quiet = (int32_t)(3000.0 * g_app.dev.buffer_frames /
                                      (g_app.dev.sample_rate > 0 ? g_app.dev.sample_rate : 48000));
            if (quiet < 150) quiet = 150;
            if (quiet > 2000) quiet = 2000;
            if (bt_device_lost(g_app.device) || bt_device_stalled(g_app.device, quiet)) {
                g_app.xruns_seen = bt_device_xruns(g_app.device);
                bt_player_stop(g_app.player);
                g_app.live = false;
                /* On screen, not in the title bar. The title is invisible in
                 * fullscreen, which is where this would actually happen. */
                std::snprintf(g_app.dev_why, sizeof(g_app.dev_why),
                              "interface disconnected");
                std::snprintf(g_app.note, sizeof(g_app.note), "%s", g_app.dev_why);
                set_title(hwnd, g_app.note);
            }
        }

        if (g_editing) {
            g_edit.song = g_edit.song < 0 ? 0 : g_edit.song;
            g_edit.playing  = transport_playing(g_app);
            {
                const double sr = g_app.dev.sample_rate > 0 ? g_app.dev.sample_rate : 48000;
                g_edit.play_sec = g_app.live && g_app.player
                                ? (double)bt_player_playhead(g_app.player) / sr : 0.0;
                /* The scrub needs the song's length, and the engine only knows
                 * it for the song actually bound to it. */
                g_edit.song_sec = (g_app.live && g_app.player &&
                                   bt_player_current(g_app.player) == g_edit.song)
                                ? (double)bt_player_song_frames(g_app.player) / sr
                                : (g_app.sl && g_edit.song >= 0 &&
                                   g_edit.song < g_app.sl->nsongs
                                   ? (double)bt_song_length(&g_app.sl->song[g_edit.song],
                                                            (int32_t)sr) / sr
                                   : 0.0);
            }
            if (!bt_ui_edit_draw(g_edit)) g_editing = false;

            /* Routing changed: tear the stream down and build it again from
             * the new config. Only reachable while stopped, so there is no
             * audio to interrupt. */
            if (g_edit.reopen_device) {
                g_edit.reopen_device = false;
                reopen_audio(hwnd);
            }
            if (g_edit.want_reload) {
                g_edit.want_reload = false;
                /* A stem was added, removed or re-pointed: the loader holds
                 * something that is no longer what the set list says. */
                if (g_app.player) bt_player_reload(g_app.player, g_edit.song);
            }
            if (g_edit.want_select) {
                g_edit.want_select = false;
                /* The align view needs the stem in memory, and only the
                 * loader puts it there. */
                if (g_app.live && g_app.player) {
                    g_app.selected = g_edit.song;
                    bt_err se = bt_player_select(g_app.player, g_edit.song);
                    if (se != BT_OK) {
                        /* Report what actually failed. This used to print the
                         * loader's last error, which is BT_OK when the loader
                         * never had a problem - so a song whose stems had not
                         * been fetched failed with "could not load: ok". */
                        int32_t bad = -1;
                        bt_err le = bt_player_load_error(g_app.player, &bad);
                        if (le != BT_OK && bad == g_edit.song)
                            std::snprintf(g_edit.status, sizeof(g_edit.status),
                                          "could not load: %s", bt_strerror(le));
                        else if (se == BT_ERR_STATE)
                            std::snprintf(g_edit.status, sizeof(g_edit.status),
                                          "its stems are still loading - try again "
                                          "in a moment");
                        else
                            std::snprintf(g_edit.status, sizeof(g_edit.status),
                                          "could not open the song: %s",
                                          bt_strerror(se));
                    }
                }
            }
            else if (g_edit.want_open_setlist) {
                g_edit.want_open_setlist = false;
                g_editing = false;
                close_set();
            }
            else if (g_edit.want_play) {
                g_edit.want_play = false;
                g_app.selected = g_edit.song;
                transport_start(g_app, false);   /* no count-in while editing */
            }
            else if (g_edit.want_stop) {
                g_edit.want_stop = false;
                transport_stop(g_app);
            }
            else if (g_edit.want_seek) {
                g_edit.want_seek = false;
                if (g_app.live && g_app.player) {
                    /* Seeking a song that is not the live one has to select it
                     * first, or the playhead would move in a song nobody is
                     * listening to. */
                    if (bt_player_current(g_app.player) != g_edit.song) {
                        g_app.selected = g_edit.song;
                        bt_player_select(g_app.player, g_edit.song);
                    }
                    const double sr = g_app.dev.sample_rate > 0 ? g_app.dev.sample_rate : 48000;
                    bt_player_seek(g_app.player, (bt_frame)(g_edit.seek_sec * sr));
                }
            }
            else if (g_edit.want_reapply) {
                g_edit.want_reapply = false;
                /* Only meaningful for the song actually bound to the engine;
                 * editing a different one changes nothing to hear. */
                if (g_app.live && g_app.player &&
                    bt_player_current(g_app.player) == g_edit.song)
                    bt_player_reapply(g_app.player);
            }

            if (g_edit.want_export) {
                g_edit.want_export = false;
                bool ok = export_wav(g_edit.export_path, g_edit.export_whole_set);
                std::snprintf(g_edit.status, sizeof(g_edit.status),
                              ok ? "rendered %s" : "could not render %s",
                              g_edit.export_path);
            }
            /* close_set() may have just run, so there may be no set list any
             * more. The frame still has to finish - ImGui has an open frame
             * that must be rendered - so this guards rather than bailing. */
            if (g_app.sl) {
                if (g_app.selected >= g_app.sl->nsongs)
                    g_app.selected = g_app.sl->nsongs - 1;
                if (g_app.selected < 0) g_app.selected = 0;
            }
        } else {
            bt_ui_state st;
            fill_state(st, g_app);
            bt_ui_result r = bt_ui_draw(st);
            switch (r.click) {
            case bt_ui_click::select:
                if (r.song >= 0 && r.song < g_app.sl->nsongs) {
                    g_app.selected = r.song;
                    queue_selected(g_app);
                }
                break;
            case bt_ui_click::play:
                if (r.song >= 0 && r.song < g_app.sl->nsongs) {
                    g_app.selected = r.song;
                    if (!transport_playing(g_app)) transport_start(g_app, true);
                }
                break;
            default: break;
            }
        }

        ImGui::Render();
        const float clear[4] = { 0.06f, 0.07f, 0.08f, 1.0f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    bt_ui_settings_save(g_settings);
    close_set();
    if (audio_available) bt_device_term();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
