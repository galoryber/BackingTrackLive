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

#include "bt_ui.h"
#include "bt_ui_edit.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

/* ------------------------------------------------------------ demo data */

struct Demo { const char *title, *artist; double bpm; bool segue; };
const Demo kDemo[] = {
    { "1985",                 "Bowling for Soup",  156.0, false },
    { "Mr. Brightside",       "The Killers",       148.0, false },
    { "Sweet Child O' Mine",  "Guns N' Roses",     125.0, false },
    { "Basket Case",          "Green Day",         180.0, true  },
    { "When I Come Around",   "Green Day",         154.0, false },
    { "Africa",               "Toto",               93.0, false },
    { "Mr. Jones",            "Counting Crows",    139.0, false },
    { "Semi-Charmed Life",    "Third Eye Blind",   103.0, false },
    { "Come As You Are",      "Nirvana",           120.0, false },
    { "Learn to Fly",         "Foo Fighters",      136.0, false },
    { "The Middle",           "Jimmy Eat World",   162.0, false },
    { "All the Small Things", "blink-182",         148.0, false },
    { "Island in the Sun",    "Weezer",            115.0, false },
    { "Everlong",             "Foo Fighters",      158.0, false },
    { "Song 2",               "Blur",              130.0, false },
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
        s.tempo.seg[0].bpm = kDemo[i].bpm;
        s.tempo.nseg = 1;
        s.tempo.sig_num = 4;
        s.tempo.sig_den = 4;
        s.on_end = kDemo[i].segue ? BT_ON_END_NEXT : BT_ON_END_STOP;
        s.count_in_bars = 1;
    }
    return sl;
}

/* ------------------------------------------------- simulated transport */

struct Sim {
    bt_setlist *sl = nullptr;
    int  current = 0, selected = 0;
    bool playing = false;
    bool show_clock = false;
    double song_len = 210.0;      /* stand-in; real songs come from stems */
    LARGE_INTEGER freq{}, t0{};
    double start_sec = 0.0;       /* negative when starting from a count-in */

    void begin(bool with_count_in) {
        QueryPerformanceCounter(&t0);
        const bt_song &s = sl->song[current];
        double bpm = s.tempo.seg[0].bpm;
        int beats = s.count_in_bars * s.tempo.sig_num;
        start_sec = with_count_in ? -(beats * 60.0 / bpm) : 0.0;
        playing = true;
    }

    double now_sec() const {
        LARGE_INTEGER n;
        QueryPerformanceCounter(&n);
        return start_sec + (double)(n.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    }
};

void fill_state(bt_ui_state &st, const Sim &sim) {
    std::memset(&st, 0, sizeof(st));
    st.setlist       = sim.sl;
    st.current       = sim.current;
    st.selected      = sim.selected;
    st.playing       = sim.playing;
    st.sample_rate   = 48000;
    st.show_clock    = sim.show_clock;
    st.total_sec     = sim.song_len;

    const bt_song &s = sim.sl->song[st.current];
    st.bpm           = s.tempo.nseg ? s.tempo.seg[0].bpm : 120.0;
    st.beats_per_bar = s.tempo.sig_num > 0 ? s.tempo.sig_num : 4;

    /* Editing can remove songs from under us. */
    if (st.current >= sim.sl->nsongs) st.current = sim.sl->nsongs - 1;
    if (st.selected >= sim.sl->nsongs) st.selected = sim.sl->nsongs - 1;
    if (st.current < 0) st.current = 0;
    if (st.selected < 0) st.selected = 0;

    double el = sim.playing ? sim.now_sec() : 0.0;
    st.elapsed_sec = el;
    st.playhead    = (bt_frame)(el * st.sample_rate);

    double beats = el * st.bpm / 60.0;
    st.beat = (int64_t)std::floor(beats);

    if (st.beat < 0) {
        /* -3 -> "3": the number is how many beats until you come in. */
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

Sim g_sim;
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

void on_key(HWND hwnd, WPARAM key) {
    Sim &s = g_sim;

    if (g_editing) {
        /* Edit mode gets the keyboard, except the two that must always work. */
        if (key == VK_F11) { toggle_fullscreen(hwnd); return; }
        if (key == 'E' && !ImGui::GetIO().WantTextInput) { g_editing = false; return; }
        bt_ui_edit_key(g_edit, (int)key);
        return;
    }

    switch (key) {
    case VK_ESCAPE:
        if (g_fullscreen) toggle_fullscreen(hwnd); else g_quit = true;
        break;
    case VK_SPACE:
        if (s.playing) s.playing = false;
        else { s.current = s.selected; s.begin(true); }
        break;
    case VK_RETURN:
        s.current = s.selected;
        s.begin(false);           /* straight in, no count-in */
        break;
    case VK_UP:
        if (s.selected > 0) s.selected--;
        break;
    case VK_DOWN:
        if (s.selected + 1 < s.sl->nsongs) s.selected++;
        break;
    case 'N':
        if (s.current + 1 < s.sl->nsongs) {
            s.current++;
            s.selected = s.current;
            if (s.playing) s.begin(false);
        }
        break;
    case 'C':
        s.show_clock = !s.show_clock;
        break;
    case 'E':
        /* Blocked while playing, deliberately: the one thing worse than a
         * fiddly editor is one that can appear over a performance. */
        if (!s.playing) g_editing = !g_editing;
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
    return DefWindowProc(hwnd, msg, wp, lp);
}

void load_font() {
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 48.0f))
        io.Fonts->AddFontDefault();
}

/* -------------------------------------------------------- offscreen path */

int run_shot(const char *out, int w, int h, const char *state, int song, int bar) {
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

    if (!std::strcmp(state, "playing")) {
        st.playing = true;
        st.playhead = (bt_frame)(st.elapsed_sec * st.sample_rate);
        st.beat = (int64_t)(st.elapsed_sec * st.bpm / 60.0);
    } else if (!std::strcmp(state, "countin")) {
        st.playing = true;
        st.playhead = -(bt_frame)(1.5 * st.sample_rate);
        st.beat = -3;
        st.count_in_left = 3;
        st.beat_in_bar = 1;
    }

    bt_ui_edit ed;
    static bt_device_cfg shot_dev;
    bt_device_cfg_defaults(&shot_dev);
    const bool edit_shot = !std::strcmp(state, "edit") || !std::strcmp(state, "editsong");
    if (edit_shot) {
        ed.sl  = sl;
        ed.dev = &shot_dev;
        ed.song = song;
        ed.track = 1;
        ed.screen = !std::strcmp(state, "editsong") ? bt_edit_screen::song
                                                    : bt_edit_screen::setlist;
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
        if (edit_shot) bt_ui_edit_draw(ed);
        else           bt_ui_draw(st);
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
        "            [--state stopped|playing|countin|edit|editsong] [--song N] [--bar N]\n");
    return 2;
}

} /* namespace */

int main(int argc, char **argv) {
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

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, wndproc, 0, 0,
                       GetModuleHandle(nullptr), nullptr, nullptr, nullptr,
                       nullptr, L"BackingTrackLive", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName,
                              L"BackingTrackLive - stage UI prototype (simulated transport, no audio)",
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

    /* A real set list if one was named, otherwise the demo - which is
     * editable so the screens can be exercised, but has nowhere to save to
     * and says so rather than pretending. */
    static bt_device_cfg dev_cfg;
    bt_device_cfg_defaults(&dev_cfg);
    bool have_dev = false;
    if (device_path) {
        int line = 0;
        bt_err e = bt_device_cfg_load_file(device_path, &dev_cfg, &line);
        if (e != BT_OK)
            std::fprintf(stderr, "%s: %s (line %d) - using defaults\n",
                         device_path, bt_strerror(e), line);
        else have_dev = true;
    }

    bt_setlist *sl = nullptr;
    if (setlist_path) {
        int line = 0;
        bt_err e = bt_setlist_load_file(setlist_path, &sl, &line);
        if (e != BT_OK) {
            std::fprintf(stderr, "%s: %s (line %d)\n", setlist_path,
                         bt_strerror(e), line);
            return 1;
        }
    }
    if (!sl) sl = make_demo_setlist();

    g_sim.sl = sl;
    g_sim.selected = 0;
    g_sim.current  = 0;
    QueryPerformanceFrequency(&g_sim.freq);

    g_edit.sl  = sl;
    g_edit.dev = &dev_cfg;
    g_edit.can_save = (setlist_path != nullptr);
    if (setlist_path) std::snprintf(g_edit.path, sizeof(g_edit.path), "%s", setlist_path);
    if (!have_dev)
        std::snprintf(g_edit.status, sizeof(g_edit.status),
                      "no device.json given - bus names are the built-in defaults");

    while (!g_quit) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) g_quit = true;
        }
        if (g_quit) break;
        if (!g_rtv) { Sleep(10); continue; }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_editing) {
            g_edit.song = g_edit.song < 0 ? 0 : g_edit.song;
            if (!bt_ui_edit_draw(g_edit)) g_editing = false;
            /* Keep the play view's cursor on something that still exists. */
            if (g_sim.selected >= g_sim.sl->nsongs)
                g_sim.selected = g_sim.sl->nsongs - 1;
            if (g_sim.current >= g_sim.sl->nsongs)
                g_sim.current = g_sim.sl->nsongs - 1;
        } else {
            bt_ui_state st;
            fill_state(st, g_sim);
            bt_ui_draw(st);
        }

        ImGui::Render();
        const float clear[4] = { 0.06f, 0.07f, 0.08f, 1.0f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
