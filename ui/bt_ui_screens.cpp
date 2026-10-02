/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "bt_ui.h"
#include "imgui.h"

#include <cstdio>

namespace {

/* A dark bar is the design constraint. Near-black ground, one accent that
 * only ever means "this is the one", and text bright enough to read at a
 * glance without being a light source pointed at the audience. */
const ImU32 COL_BG       = IM_COL32( 16,  17,  20, 255);
const ImU32 COL_PANEL    = IM_COL32( 26,  28,  33, 255);
const ImU32 COL_TEXT     = IM_COL32(232, 234, 238, 255);
const ImU32 COL_DIM      = IM_COL32(138, 144, 156, 255);
const ImU32 COL_ACCENT   = IM_COL32( 94, 168, 255, 255);
const ImU32 COL_ACCENT_D = IM_COL32( 30,  58,  92, 255);
const ImU32 COL_BEAT_ON  = IM_COL32(255, 214,  92, 255);
const ImU32 COL_BEAT_OFF = IM_COL32( 62,  66,  76, 255);
const ImU32 COL_WARN     = IM_COL32(255, 122, 106, 255);
const ImU32 COL_GOOD     = IM_COL32(112, 206, 142, 255);

void text_at(ImDrawList *dl, ImFont *font, float size, ImVec2 p, ImU32 col,
             const char *s) {
    dl->AddText(font, size, p, col, s);
}

float text_w(ImFont *font, float size, const char *s) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, s).x;
}

void fmt_clock(char *dst, size_t cap, double sec) {
    bool neg = sec < 0;
    if (neg) sec = -sec;
    int m = (int)(sec / 60.0);
    std::snprintf(dst, cap, "%s%d:%04.1f", neg ? "-" : "", m, sec - m * 60.0);
}

/* The audio interface, as a dot and a name. Green when a stream is open, red
 * when it is not - and when it is not, why. */
void draw_device_badge(ImDrawList *dl, const bt_ui_state &st, ImVec2 sz,
                       float cy, bool with_name) {
    ImFont *f = ImGui::GetFont();
    const float pad = sz.x * 0.03f;
    const float ts  = sz.y * 0.030f;
    const float r   = ts * 0.42f;

    const char *label = st.device_live
                      ? (st.device_name && st.device_name[0] ? st.device_name : "audio ready")
                      : (st.device_note && st.device_note[0] ? st.device_note : "no audio device");
    if (!with_name && st.device_live) return;   /* silence is the good news */

    float tw = text_w(f, ts, label);
    float x  = sz.x - pad - tw;

    text_at(dl, f, ts, ImVec2(x, cy - ts * 0.5f),
            st.device_live ? COL_DIM : COL_WARN, label);
    dl->AddCircleFilled(ImVec2(x - r * 3.0f, cy), r,
                        st.device_live ? COL_GOOD : COL_WARN, 24);
    if (!st.device_live)
        dl->AddCircle(ImVec2(x - r * 3.0f, cy), r * 1.9f, COL_WARN, 24,
                      sz.y * 0.0025f);
}

/* ------------------------------------------------------------- stopped */

void draw_setlist(ImDrawList *dl, const bt_ui_state &st, ImVec2 sz) {
    ImFont *f = ImGui::GetFont();
    const float pad = sz.x * 0.03f;

    const float title_sz = sz.y * 0.055f;
    text_at(dl, f, title_sz, ImVec2(pad, pad * 0.6f), COL_TEXT,
            st.setlist ? st.setlist->name : "No set list");

    char sub[64];
    std::snprintf(sub, sizeof(sub), "%d songs",
                  st.setlist ? st.setlist->nsongs : 0);
    text_at(dl, f, title_sz * 0.55f,
            ImVec2(pad, pad * 0.6f + title_sz * 1.05f), COL_DIM, sub);

    const float list_top = pad * 0.6f + title_sz * 2.0f;
    const float foot_h   = sz.y * 0.10f;
    const float row_h    = sz.y * 0.082f;
    const float row_sz   = row_h * 0.46f;

    if (!st.setlist) return;

    /* Keep the selection in view without a scrollbar to hunt for: show a
     * window of rows centred on it. */
    /* Only count rows that fit *entirely* above the footer. Rounding up here
     * is what clipped the last row. */
    int visible = (int)((sz.y - list_top - foot_h) / row_h);
    if (visible < 1) visible = 1;
    if (list_top + visible * row_h > sz.y - foot_h) visible--;
    if (visible < 1) visible = 1;
    int first = st.selected - visible / 2;
    if (first > st.setlist->nsongs - visible) first = st.setlist->nsongs - visible;
    if (first < 0) first = 0;

    for (int i = first; i < st.setlist->nsongs && i < first + visible; i++) {
        const bt_song &s = st.setlist->song[i];
        float y = list_top + (i - first) * row_h;
        bool sel = (i == st.selected);

        if (sel)
            dl->AddRectFilled(ImVec2(pad * 0.5f, y),
                              ImVec2(sz.x - pad * 0.5f, y + row_h * 0.92f),
                              COL_ACCENT_D, row_h * 0.12f);

        /* The playing song keeps a marker even while you browse elsewhere,
         * so browsing can never be mistaken for having changed song. */
        if (i == st.current)
            dl->AddRectFilled(ImVec2(pad * 0.5f, y),
                              ImVec2(pad * 0.5f + row_h * 0.08f, y + row_h * 0.92f),
                              COL_ACCENT, row_h * 0.04f);

        char num[8];
        std::snprintf(num, sizeof(num), "%d", i + 1);
        text_at(dl, f, row_sz, ImVec2(pad * 1.4f, y + row_h * 0.22f),
                sel ? COL_TEXT : COL_DIM, num);

        text_at(dl, f, row_sz, ImVec2(pad * 3.2f, y + row_h * 0.22f),
                sel ? COL_TEXT : COL_TEXT, s.title);

        if (s.artist[0]) {
            float tw = text_w(f, row_sz, s.title);
            text_at(dl, f, row_sz * 0.8f,
                    ImVec2(pad * 3.4f + tw, y + row_h * 0.30f), COL_DIM, s.artist);
        }

        char bpm[16];
        std::snprintf(bpm, sizeof(bpm), "%.0f",
                      s.tempo.nseg ? s.tempo.seg[0].bpm : 0.0);
        text_at(dl, f, row_sz, ImVec2(sz.x - pad * 1.2f - text_w(f, row_sz, bpm),
                                      y + row_h * 0.22f),
                sel ? COL_TEXT : COL_DIM, bpm);
    }

    /* Footer: the keys, because this is driven from the keyboard, and the
     * state of the audio interface, because "am I plugged in and ready" is
     * the question you ask before counting a song in. */
    float fy = sz.y - foot_h;
    dl->AddRectFilled(ImVec2(0, fy), ImVec2(sz.x, sz.y), COL_PANEL);
    const float key_sz = foot_h * 0.34f;
    text_at(dl, f, key_sz, ImVec2(pad, fy + foot_h * 0.32f), COL_TEXT,
            "SPACE  play      \xe2\x86\x91 \xe2\x86\x93  choose      ENTER  play from top"
            "      N  next song      E  edit");

    draw_device_badge(dl, st, sz, fy + foot_h * 0.5f, true);
}

/* ------------------------------------------------------------- playing */

void draw_playing(ImDrawList *dl, const bt_ui_state &st, ImVec2 sz) {
    ImFont *f = ImGui::GetFont();
    const float pad = sz.x * 0.03f;
    const bt_song *s = (st.setlist && st.current >= 0)
                     ? &st.setlist->song[st.current] : nullptr;
    const bool counting = st.playhead < 0;

    /* ---- header: what is playing, large ------------------------------
     *
     * This used to be a line of small grey text, on the reasoning that you
     * know what you are playing. True, and it is still the thing a glance
     * from across the stage should answer first - "where are we" is a
     * question other people ask too. */
    char pos[32];
    std::snprintf(pos, sizeof(pos), "%d / %d", st.current + 1,
                  st.setlist ? st.setlist->nsongs : 0);
    const float pos_sz = sz.y * 0.042f;
    text_at(dl, f, pos_sz, ImVec2(pad, sz.y * 0.035f), COL_DIM, pos);

    if (s) {
        const float title_sz = sz.y * 0.105f;
        text_at(dl, f, title_sz, ImVec2(pad, sz.y * 0.075f), COL_TEXT, s->title);
        if (s->artist[0])
            text_at(dl, f, sz.y * 0.055f, ImVec2(pad, sz.y * 0.195f),
                    COL_DIM, s->artist);
    }

    /* ---- bar number: kept, demoted ------------------------------------
     * Useful for finding your place, and not what the eye should land on. */
    if (!counting) {
        char bar[16];
        std::snprintf(bar, sizeof(bar), "%d", st.bar);
        const float bar_sz = sz.y * 0.095f;
        float bw = text_w(f, bar_sz, bar);
        text_at(dl, f, bar_sz, ImVec2(sz.x - pad - bw, sz.y * 0.060f),
                COL_TEXT, bar);
        const char *lbl = "BAR";
        float ls = sz.y * 0.030f;
        text_at(dl, f, ls, ImVec2(sz.x - pad - text_w(f, ls, lbl), sz.y * 0.030f),
                COL_DIM, lbl);
    }

    /* ---- the metronome: centre screen, the biggest thing on it --------
     *
     * During the count-in it moves down and shrinks to make room for the
     * number, which is what matters in that moment. Once the song starts it
     * takes the middle of the screen back. */
    int n = st.beats_per_bar > 0 ? st.beats_per_bar : 4;
    if (n > 16) n = 16;
    const float cy    = counting ? sz.y * 0.675f : sz.y * 0.540f;
    const float r_max = counting ? sz.y * 0.072f : sz.y * 0.105f;

    /* Size the dots to the bar rather than fixing them: 7/8 has to fit the
     * same width 4/4 does, without either looking apologetic. */
    float gap = (sz.x - pad * 2.0f) / (float)(n + 1);
    float r   = gap * 0.34f;
    if (r > r_max) r = r_max;
    gap = r * 3.1f;

    float total = (n - 1) * gap;
    float cx = (sz.x - total) * 0.5f;

    for (int i = 0; i < n; i++) {
        bool on   = (i == st.beat_in_bar);
        bool down = (i == 0);
        float rr  = on ? r * 1.22f : r;
        ImVec2 c(cx + i * gap, cy);

        if (on) {
            /* A soft ring around the live beat so it reads from a distance
             * as movement, not just a colour change. Kept faint - at full
             * strength it muddies into the dot and the edge is what the eye
             * actually tracks. */
            dl->AddCircleFilled(c, rr * 1.42f, IM_COL32(255, 214, 92, 26), 48);
            dl->AddCircleFilled(c, rr, COL_BEAT_ON, 48);
        } else {
            dl->AddCircleFilled(c, rr, COL_BEAT_OFF, 48);
            /* The one is outlined even when it is not lit, so you can see
             * where the bar starts without waiting for it. */
            if (down) dl->AddCircle(c, rr * 1.28f, COL_DIM, 48, sz.y * 0.004f);
        }
    }

    /* ---- count-in: the number, above the dots, dominant ---------------
     * Sized and placed to clear the metronome rather than land on top of it,
     * which is what the first version of this did. */
    if (counting) {
        char big[16];
        std::snprintf(big, sizeof(big), "%d", st.count_in_left);
        const float big_sz = sz.y * 0.235f;
        float bw = text_w(f, big_sz, big);
        text_at(dl, f, big_sz, ImVec2((sz.x - bw) * 0.5f, sz.y * 0.255f),
                COL_BEAT_ON, big);

        const char *lbl = "COUNT IN";
        float ls = sz.y * 0.038f;
        text_at(dl, f, ls, ImVec2((sz.x - text_w(f, ls, lbl)) * 0.5f,
                                  sz.y * 0.500f), COL_BEAT_ON, lbl);
    }

    /* ---- quiet row: tempo, clock, position ---------------------------- */
    const float small = sz.y * 0.040f;
    char tempo[32];
    std::snprintf(tempo, sizeof(tempo), "%.1f BPM", st.bpm);
    text_at(dl, f, small, ImVec2(pad, sz.y * 0.735f), COL_DIM, tempo);
    (void)0;

    if (st.show_clock) {
        char clock[32];
        fmt_clock(clock, sizeof(clock), st.elapsed_sec);
        text_at(dl, f, small,
                ImVec2(sz.x - pad - text_w(f, small, clock), sz.y * 0.735f),
                COL_DIM, clock);
    }

    if (st.total_sec > 0.0) {
        float t = (float)(st.elapsed_sec / st.total_sec);
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float py = sz.y * 0.795f;
        dl->AddRectFilled(ImVec2(pad, py), ImVec2(sz.x - pad, py + sz.y * 0.006f),
                          COL_BEAT_OFF);
        dl->AddRectFilled(ImVec2(pad, py),
                          ImVec2(pad + (sz.x - pad * 2) * t, py + sz.y * 0.006f),
                          COL_ACCENT);
    }

    /* ---- what is next: unchanged, it works ---------------------------- */
    const float foot_h = sz.y * 0.155f;
    float fy = sz.y - foot_h;
    dl->AddRectFilled(ImVec2(0, fy), ImVec2(sz.x, sz.y), COL_PANEL);

    const float nl = foot_h * 0.26f;
    text_at(dl, f, nl, ImVec2(pad, fy + foot_h * 0.16f), COL_DIM, "NEXT");

    const bool has_next = st.setlist && st.current + 1 < st.setlist->nsongs;
    if (has_next) {
        const bt_song &nx = st.setlist->song[st.current + 1];
        const float ns = foot_h * 0.42f;
        text_at(dl, f, ns, ImVec2(pad, fy + foot_h * 0.42f), COL_TEXT, nx.title);
        if (nx.artist[0])
            text_at(dl, f, ns * 0.72f,
                    ImVec2(pad + text_w(f, ns, nx.title) + pad * 0.6f,
                           fy + foot_h * 0.50f), COL_DIM, nx.artist);
        if (s && s->on_end == BT_ON_END_NEXT)
            text_at(dl, f, nl, ImVec2(sz.x - pad - text_w(f, nl, "SEGUE"),
                                      fy + foot_h * 0.16f), COL_BEAT_ON, "SEGUE");
    } else {
        text_at(dl, f, foot_h * 0.42f, ImVec2(pad, fy + foot_h * 0.42f), COL_DIM,
                "end of set");
    }

    if (st.xruns) {
        char x[48];
        std::snprintf(x, sizeof(x), "%llu XRUN", (unsigned long long)st.xruns);
        text_at(dl, f, small, ImVec2(sz.x - pad - text_w(f, small, x),
                                     sz.y * 0.675f), COL_WARN, x);
    }

    /* While playing, working audio needs no announcement - you can hear it.
     * Absent audio does. */
    draw_device_badge(dl, st, sz, sz.y * 0.675f, false);
}

} /* namespace */

void bt_ui_draw(const bt_ui_state &st) {
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 sz = io.DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(sz);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("stage", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), sz, COL_BG);

    if (st.playing) draw_playing(dl, st, sz);
    else            draw_setlist(dl, st, sz);

    ImGui::End();
    ImGui::PopStyleVar(2);
}
