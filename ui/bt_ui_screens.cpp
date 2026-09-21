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

    /* Footer: the keys, because this is driven from the keyboard. */
    float fy = sz.y - foot_h;
    dl->AddRectFilled(ImVec2(0, fy), ImVec2(sz.x, sz.y), COL_PANEL);
    const float key_sz = foot_h * 0.34f;
    text_at(dl, f, key_sz, ImVec2(pad, fy + foot_h * 0.32f), COL_TEXT,
            "SPACE  play      \xe2\x86\x91 \xe2\x86\x93  choose      ENTER  play from top      N  next");
}

/* ------------------------------------------------------------- playing */

void draw_playing(ImDrawList *dl, const bt_ui_state &st, ImVec2 sz) {
    ImFont *f = ImGui::GetFont();
    const float pad = sz.x * 0.03f;
    const bt_song *s = (st.setlist && st.current >= 0)
                     ? &st.setlist->song[st.current] : nullptr;

    /* Small, top-left: orientation only. You know what you are playing. */
    char pos[32];
    std::snprintf(pos, sizeof(pos), "%d / %d", st.current + 1,
                  st.setlist ? st.setlist->nsongs : 0);
    const float small = sz.y * 0.040f;
    text_at(dl, f, small, ImVec2(pad, pad * 0.5f), COL_DIM, pos);
    if (s) {
        text_at(dl, f, small, ImVec2(pad + text_w(f, small, pos) + pad * 0.8f,
                                     pad * 0.5f), COL_TEXT, s->title);
    }

    /* The count-in reads differently from the song: negative bars would be
     * meaningless, so it counts down in beats instead. */
    const bool counting = st.playhead < 0;

    /* Dominant, and the same size either way so the eye does not have to
     * re-find it when the song starts: the count-in counts DOWN, the song
     * counts bars UP. A count-in that does not show the number is just a
     * label, and the number is the entire point - it is how you know when to
     * come in. */
    char big[24];
    if (counting) std::snprintf(big, sizeof(big), "%d", st.count_in_left);
    else          std::snprintf(big, sizeof(big), "%d", st.bar);

    const float bar_sz = sz.y * 0.42f;
    float bw = text_w(f, bar_sz, big);
    float by = sz.y * 0.12f;
    text_at(dl, f, bar_sz, ImVec2((sz.x - bw) * 0.5f, by),
            counting ? COL_BEAT_ON : COL_TEXT, big);

    const char *lbl = counting ? "COUNT IN" : "BAR";
    float ls = sz.y * 0.05f;
    text_at(dl, f, ls, ImVec2((sz.x - text_w(f, ls, lbl)) * 0.5f,
                              by + bar_sz * 0.95f),
            counting ? COL_BEAT_ON : COL_DIM, lbl);

    /* Beat dots: the accent on one, so a glance tells you where in the bar
     * you are, not merely that something is pulsing. */
    int n = st.beats_per_bar > 0 ? st.beats_per_bar : 4;
    float r  = sz.y * 0.030f;
    float gap = r * 3.0f;
    float total = (n - 1) * gap;
    float cx = (sz.x - total) * 0.5f;
    float cy = sz.y * 0.70f;
    for (int i = 0; i < n; i++) {
        bool on = (i == st.beat_in_bar);
        float rr = on ? r * 1.35f : r;
        dl->AddCircleFilled(ImVec2(cx + i * gap, cy), rr,
                            on ? COL_BEAT_ON : COL_BEAT_OFF, 32);
        if (i == 0 && !on)
            dl->AddCircle(ImVec2(cx + i * gap, cy), r * 1.3f, COL_DIM, 32, 2.0f);
    }

    /* Tempo, quietly, beside the dots. */
    char tempo[32];
    std::snprintf(tempo, sizeof(tempo), "%.1f BPM", st.bpm);
    text_at(dl, f, sz.y * 0.042f, ImVec2(pad, cy - sz.y * 0.021f), COL_DIM, tempo);

    if (st.show_clock) {
        char clock[32];
        fmt_clock(clock, sizeof(clock), st.elapsed_sec);
        float cw = text_w(f, sz.y * 0.042f, clock);
        text_at(dl, f, sz.y * 0.042f,
                ImVec2(sz.x - pad - cw, cy - sz.y * 0.021f), COL_DIM, clock);
    }

    /* Thin progress line: position in the song without occupying attention. */
    if (st.total_sec > 0.0) {
        float t = (float)(st.elapsed_sec / st.total_sec);
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float py = sz.y * 0.785f;
        dl->AddRectFilled(ImVec2(pad, py), ImVec2(sz.x - pad, py + sz.y * 0.006f),
                          COL_BEAT_OFF);
        dl->AddRectFilled(ImVec2(pad, py),
                          ImVec2(pad + (sz.x - pad * 2) * t, py + sz.y * 0.006f),
                          COL_ACCENT);
    }

    /* Footer: what is next, which is the other thing worth knowing mid-song. */
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

    /* Anything but zero here means the audience heard a click. */
    if (st.xruns) {
        char x[48];
        std::snprintf(x, sizeof(x), "%llu XRUN", (unsigned long long)st.xruns);
        text_at(dl, f, small, ImVec2(sz.x - pad - text_w(f, small, x), pad * 0.5f),
                COL_WARN, x);
    }
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
