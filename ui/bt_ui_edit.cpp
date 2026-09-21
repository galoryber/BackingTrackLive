/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "bt_ui_edit.h"
#include "imgui.h"

#include <windows.h>
#include <commdlg.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>

namespace {

/* ------------------------------------------------------------- styling */

void push_theme() {
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowPadding     = ImVec2(18, 14);
    s.FramePadding      = ImVec2(10, 7);
    s.ItemSpacing        = ImVec2(10, 8);
    s.FrameRounding      = 4.0f;
    s.GrabRounding       = 4.0f;
    s.ScrollbarRounding  = 4.0f;

    ImVec4 *c = s.Colors;
    c[ImGuiCol_WindowBg]       = ImVec4(0.063f, 0.067f, 0.078f, 1.00f);
    c[ImGuiCol_ChildBg]        = ImVec4(0.086f, 0.094f, 0.110f, 1.00f);
    c[ImGuiCol_Text]           = ImVec4(0.910f, 0.918f, 0.933f, 1.00f);
    c[ImGuiCol_TextDisabled]   = ImVec4(0.541f, 0.565f, 0.612f, 1.00f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.133f, 0.145f, 0.169f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.180f, 0.196f, 0.227f, 1.00f);
    c[ImGuiCol_FrameBgActive]  = ImVec4(0.216f, 0.235f, 0.275f, 1.00f);
    c[ImGuiCol_Button]         = ImVec4(0.153f, 0.173f, 0.204f, 1.00f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.216f, 0.247f, 0.298f, 1.00f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.290f, 0.510f, 0.780f, 1.00f);
    c[ImGuiCol_Header]         = ImVec4(0.118f, 0.227f, 0.361f, 1.00f);
    c[ImGuiCol_HeaderHovered]  = ImVec4(0.157f, 0.298f, 0.463f, 1.00f);
    c[ImGuiCol_HeaderActive]   = ImVec4(0.196f, 0.369f, 0.573f, 1.00f);
    c[ImGuiCol_Separator]      = ImVec4(0.180f, 0.196f, 0.227f, 1.00f);
    c[ImGuiCol_CheckMark]      = ImVec4(0.369f, 0.659f, 1.000f, 1.00f);
    c[ImGuiCol_SliderGrab]     = ImVec4(0.369f, 0.659f, 1.000f, 1.00f);
    c[ImGuiCol_TableHeaderBg]  = ImVec4(0.110f, 0.120f, 0.141f, 1.00f);
    c[ImGuiCol_TableRowBg]     = ImVec4(0.078f, 0.086f, 0.102f, 1.00f);
    c[ImGuiCol_TableRowBgAlt]  = ImVec4(0.094f, 0.102f, 0.122f, 1.00f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.149f, 0.161f, 0.188f, 1.00f);
}

const ImVec4 COL_DIM   = ImVec4(0.541f, 0.565f, 0.612f, 1.0f);
const ImVec4 COL_WARN  = ImVec4(1.000f, 0.478f, 0.416f, 1.0f);
const ImVec4 COL_OK    = ImVec4(0.478f, 0.839f, 0.588f, 1.0f);
const ImVec4 COL_AMBER = ImVec4(1.000f, 0.839f, 0.361f, 1.0f);

/* ----------------------------------------------------------- utilities */

/* Stems must live inside the set list folder. That is not arbitrary: the
 * folder is meant to be copyable to the backup laptop as one unit, and the
 * loader rejects absolute paths and ".." for the same reason. So a chosen
 * file is either under the folder - in which case store it relative - or it
 * is a mistake worth saying out loud. */
bool relativise(const char *dir, const char *picked, char *out, size_t cap) {
    char full_dir[MAX_PATH], full_pick[MAX_PATH];
    if (!GetFullPathNameA(dir && *dir ? dir : ".", MAX_PATH, full_dir, nullptr)) return false;
    if (!GetFullPathNameA(picked, MAX_PATH, full_pick, nullptr)) return false;

    size_t n = std::strlen(full_dir);
    while (n && (full_dir[n - 1] == '\\' || full_dir[n - 1] == '/')) n--;

    if (_strnicmp(full_pick, full_dir, n) != 0) return false;
    const char *rest = full_pick + n;
    while (*rest == '\\' || *rest == '/') rest++;
    if (!*rest) return false;

    /* setlist.json uses forward slashes so the file reads the same on any
     * machine, even though only Windows will write it today. */
    size_t k = 0;
    for (const char *p = rest; *p && k + 1 < cap; p++)
        out[k++] = (*p == '\\') ? '/' : *p;
    out[k] = '\0';
    return true;
}

bool pick_audio_file(const char *start_dir, char *out, size_t cap) {
    char buf[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Audio\0*.wav;*.flac;*.mp3\0All files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrInitialDir = (start_dir && *start_dir) ? start_dir : nullptr;
    ofn.lpstrTitle  = "Add a stem";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&ofn)) return false;
    std::snprintf(out, cap, "%s", buf);
    return true;
}

void set_status(bt_ui_edit &ed, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ed.status, sizeof(ed.status), fmt, ap);
    va_end(ap);
}

void do_save(bt_ui_edit &ed) {
    if (!ed.can_save) {
        set_status(ed, "nothing to save to - this is the built-in demo set "
                       "(start btui with --setlist <file>)");
        return;
    }
    bt_err e = bt_setlist_save_file(ed.sl, ed.path);
    if (e == BT_OK) { ed.dirty = false; set_status(ed, "saved %s", ed.path); }
    else            set_status(ed, "save failed: %s", bt_strerror(e));
}

/* ------------------------------------------------------ set list screen */

void move_song(bt_ui_edit &ed, int32_t from, int32_t to) {
    if (from < 0 || to < 0 || from >= ed.sl->nsongs || to >= ed.sl->nsongs) return;
    bt_song tmp = ed.sl->song[from];
    if (from < to) for (int32_t i = from; i < to; i++) ed.sl->song[i] = ed.sl->song[i + 1];
    else           for (int32_t i = from; i > to; i--) ed.sl->song[i] = ed.sl->song[i - 1];
    ed.sl->song[to] = tmp;
    ed.song = to;
    ed.dirty = true;
}

void add_song(bt_ui_edit &ed) {
    /* Grow by hand: bt_setlist owns a plain array, and the editor is the only
     * thing that ever changes its length. */
    int32_t n = ed.sl->nsongs;
    bt_song *grown = (bt_song *)realloc(ed.sl->song, (size_t)(n + 1) * sizeof(bt_song));
    if (!grown) { set_status(ed, "out of memory"); return; }
    ed.sl->song = grown;
    bt_song &s = ed.sl->song[n];
    std::memset(&s, 0, sizeof(s));
    std::snprintf(s.title, sizeof(s.title), "New song");
    s.tempo.seg[0].bpm = 120.0;
    s.tempo.nseg    = 1;
    s.tempo.sig_num = 4;
    s.tempo.sig_den = 4;
    s.count_in_bars = 1;
    s.on_end        = BT_ON_END_STOP;
    /* Every song wants a click; making it the default saves a step and
     * removes the commonest omission btcheck warns about. */
    s.track[0].type = BT_TRACK_CLICK;
    std::snprintf(s.track[0].name, sizeof(s.track[0].name), "Click");
    std::snprintf(s.track[0].bus, sizeof(s.track[0].bus), "%s",
                  (ed.dev && ed.dev->nbuses > 1) ? ed.dev->bus[1].name : "inear");
    s.ntracks = 1;
    ed.sl->nsongs = n + 1;
    ed.sl->cap    = n + 1;
    ed.song = n;
    ed.dirty = true;
}

void remove_song(bt_ui_edit &ed, int32_t idx) {
    if (idx < 0 || idx >= ed.sl->nsongs) return;
    bt_song_free_audio(&ed.sl->song[idx]);
    for (int32_t i = idx; i + 1 < ed.sl->nsongs; i++)
        ed.sl->song[i] = ed.sl->song[i + 1];
    ed.sl->nsongs--;
    if (ed.song >= ed.sl->nsongs) ed.song = ed.sl->nsongs - 1;
    ed.dirty = true;
}

void draw_setlist_screen(bt_ui_edit &ed) {
    ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
    ImGui::TextUnformatted("EDIT  \xe2\x80\xa2  set list");
    ImGui::PopStyleColor();

    ImGui::SetNextItemWidth(360);
    if (ImGui::InputText("set list name", ed.sl->name, sizeof(ed.sl->name)))
        ed.dirty = true;

    ImGui::SameLine();
    ImGui::TextDisabled("%d song%s", ed.sl->nsongs, ed.sl->nsongs == 1 ? "" : "s");

    ImGui::Separator();

    if (ImGui::Button("+ add song")) add_song(ed);
    ImGui::SameLine();
    ImGui::BeginDisabled(ed.sl->nsongs == 0);
    if (ImGui::Button("remove")) remove_song(ed, ed.song);
    ImGui::SameLine();
    if (ImGui::Button("move up"))   move_song(ed, ed.song, ed.song - 1);
    ImGui::SameLine();
    if (ImGui::Button("move down")) move_song(ed, ed.song, ed.song + 1);
    ImGui::SameLine();
    if (ImGui::Button("edit song \xe2\x86\x92")) ed.screen = bt_edit_screen::song;
    ImGui::EndDisabled();

    ImGui::Spacing();

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("songs", 6, flags, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, 44);
        ImGui::TableSetupColumn("title",  ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn("artist", ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn("bpm",    ImGuiTableColumnFlags_WidthFixed, 64);
        ImGui::TableSetupColumn("tracks", ImGuiTableColumnFlags_WidthFixed, 92);
        ImGui::TableSetupColumn("on end", ImGuiTableColumnFlags_WidthFixed, 74);
        ImGui::TableHeadersRow();

        for (int32_t i = 0; i < ed.sl->nsongs; i++) {
            const bt_song &s = ed.sl->song[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            char label[32];
            std::snprintf(label, sizeof(label), "%d##row%d", i + 1, i);
            if (ImGui::Selectable(label, ed.song == i,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                ed.song = i;
                if (ImGui::IsMouseDoubleClicked(0)) ed.screen = bt_edit_screen::song;
            }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(s.title);
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", s.artist);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", s.tempo.nseg ? s.tempo.seg[0].bpm : 0.0);
            ImGui::TableNextColumn();
            /* A song with no audio is not an error, but it is worth noticing
               before soundcheck rather than during it. */
            int32_t audio = 0;
            for (int32_t t = 0; t < s.ntracks; t++)
                if (s.track[t].type == BT_TRACK_AUDIO) audio++;
            /* Dim, not red. Red should mean "this will fail"; a song with no
             * stems yet is simply a song you have not finished. */
            if (audio == 0) ImGui::TextDisabled("click only");
            else            ImGui::Text("%d stem%s", audio, audio == 1 ? "" : "s");
            ImGui::TableNextColumn();
            if (s.on_end == BT_ON_END_NEXT) ImGui::TextColored(COL_AMBER, "segue");
            else                            ImGui::TextDisabled("stop");
        }
        ImGui::EndTable();
    }
}

/* ---------------------------------------------------------- song screen */

void draw_tracks(bt_ui_edit &ed, bt_song &s) {
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("tracks", 7, flags)) return;

    ImGui::TableSetupColumn("name",  ImGuiTableColumnFlags_WidthStretch, 2);
    ImGui::TableSetupColumn("file",  ImGuiTableColumnFlags_WidthStretch, 3);
    ImGui::TableSetupColumn("bus",   ImGuiTableColumnFlags_WidthFixed, 130);
    ImGui::TableSetupColumn("gain",  ImGuiTableColumnFlags_WidthFixed, 150);
    ImGui::TableSetupColumn("nudge", ImGuiTableColumnFlags_WidthFixed, 215);
    ImGui::TableSetupColumn("mute",  ImGuiTableColumnFlags_WidthFixed, 56);
    ImGui::TableSetupColumn("",      ImGuiTableColumnFlags_WidthFixed, 40);
    ImGui::TableHeadersRow();

    int32_t remove_at = -1;
    for (int32_t t = 0; t < s.ntracks; t++) {
        bt_track &tr = s.track[t];
        ImGui::PushID(t);
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##name", tr.name, sizeof(tr.name))) ed.dirty = true;

        ImGui::TableNextColumn();
        if (tr.type == BT_TRACK_CLICK) ImGui::TextDisabled("(generated click)");
        else                           ImGui::TextUnformatted(tr.file);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##bus", tr.bus)) {
            if (ed.dev) {
                for (int32_t b = 0; b < ed.dev->nbuses; b++) {
                    bool sel = std::strcmp(tr.bus, ed.dev->bus[b].name) == 0;
                    if (ImGui::Selectable(ed.dev->bus[b].name, sel)) {
                        std::snprintf(tr.bus, sizeof(tr.bus), "%s", ed.dev->bus[b].name);
                        ed.dirty = true;
                    }
                }
            } else {
                ImGui::TextDisabled("no device.json loaded");
            }
            ImGui::EndCombo();
        }

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        float g = (float)tr.gain_db;
        if (ImGui::SliderFloat("##gain", &g, -24.0f, 6.0f, "%+.1f dB")) {
            tr.gain_db = g;
            ed.dirty = true;
        }

        ImGui::TableNextColumn();
        if (tr.type == BT_TRACK_CLICK) {
            ImGui::TextDisabled("\xe2\x80\x94");
        } else {
            /* Steppers rather than a slider: alignment is a millisecond job,
             * and a slider cannot be nudged by one. */
            if (ImGui::SmallButton("<<")) { tr.offset_ms -= 10; ed.dirty = true; }
            ImGui::SameLine(0, 4);
            if (ImGui::SmallButton("<"))  { tr.offset_ms -= 1;  ed.dirty = true; }
            ImGui::SameLine(0, 6);
            ImGui::Text("%+d ms", tr.offset_ms);
            ImGui::SameLine(0, 6);
            if (ImGui::SmallButton(">"))  { tr.offset_ms += 1;  ed.dirty = true; }
            ImGui::SameLine(0, 4);
            if (ImGui::SmallButton(">>")) { tr.offset_ms += 10; ed.dirty = true; }
        }

        ImGui::TableNextColumn();
        bool m = tr.muted;
        if (ImGui::Checkbox("##mute", &m)) { tr.muted = m; ed.dirty = true; }

        ImGui::TableNextColumn();
        if (ImGui::SmallButton("x")) remove_at = t;

        ImGui::PopID();
    }
    ImGui::EndTable();

    if (remove_at >= 0) {
        bt_track &tr = s.track[remove_at];
        if (tr.pcm) {
            for (int32_t c = 0; c < tr.channels; c++) free(tr.pcm[c]);
            free(tr.pcm);
        }
        for (int32_t i = remove_at; i + 1 < s.ntracks; i++) s.track[i] = s.track[i + 1];
        s.ntracks--;
        ed.dirty = true;
    }
}

void add_track(bt_ui_edit &ed, bt_song &s) {
    if (s.ntracks >= BT_MAX_TRACKS) {
        set_status(ed, "a song holds at most %d tracks", BT_MAX_TRACKS);
        return;
    }
    char picked[MAX_PATH];
    if (!pick_audio_file(ed.sl->dir, picked, sizeof(picked))) return;

    char rel[BT_MAX_PATH];
    if (!relativise(ed.sl->dir, picked, rel, sizeof(rel))) {
        set_status(ed, "that file is outside the set list folder - copy stems "
                       "into it so the folder stays portable");
        return;
    }

    bt_track &tr = s.track[s.ntracks];
    std::memset(&tr, 0, sizeof(tr));
    tr.type = BT_TRACK_AUDIO;
    std::snprintf(tr.file, sizeof(tr.file), "%s", rel);

    /* Name it after the file, which is nearly always what it should be
     * called and is one less field to fill in. */
    const char *base = std::strrchr(rel, '/');
    base = base ? base + 1 : rel;
    std::snprintf(tr.name, sizeof(tr.name), "%s", base);
    char *dot = std::strrchr(tr.name, '.');
    if (dot) *dot = '\0';

    std::snprintf(tr.bus, sizeof(tr.bus), "%s",
                  (ed.dev && ed.dev->nbuses > 0) ? ed.dev->bus[0].name : "foh");
    s.ntracks++;
    ed.dirty = true;
    set_status(ed, "added %s", rel);
}

void draw_song_screen(bt_ui_edit &ed) {
    if (ed.song < 0 || ed.song >= ed.sl->nsongs) {
        ed.screen = bt_edit_screen::setlist;
        return;
    }
    bt_song &s = ed.sl->song[ed.song];

    ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
    ImGui::Text("EDIT  \xe2\x80\xa2  song %d of %d", ed.song + 1, ed.sl->nsongs);
    ImGui::PopStyleColor();

    if (ImGui::Button("\xe2\x86\x90 set list")) ed.screen = bt_edit_screen::setlist;
    ImGui::SameLine();
    ImGui::BeginDisabled(ed.song == 0);
    if (ImGui::Button("< prev")) ed.song--;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(ed.song + 1 >= ed.sl->nsongs);
    if (ImGui::Button("next >")) ed.song++;
    ImGui::EndDisabled();

    ImGui::Separator();

    ImGui::SetNextItemWidth(360);
    if (ImGui::InputText("title", s.title, sizeof(s.title))) ed.dirty = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300);
    if (ImGui::InputText("artist", s.artist, sizeof(s.artist))) ed.dirty = true;

    /* Tempo is metadata, not analysis: it has to match what is already in the
     * stems. Saying so here is cheaper than anyone discovering it. */
    ImGui::Spacing();
    ImGui::SetNextItemWidth(190);
    double bpm = s.tempo.nseg ? s.tempo.seg[0].bpm : 120.0;
    if (ImGui::InputDouble("BPM", &bpm, 0.1, 1.0, "%.2f")) {
        if (bpm > 1.0 && bpm < 400.0) {
            s.tempo.seg[0].bpm = bpm;
            if (s.tempo.nseg == 0) s.tempo.nseg = 1;
            ed.dirty = true;
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    int num = s.tempo.sig_num;
    if (ImGui::InputInt("##sig_num", &num, 1, 1)) {
        if (num >= 1 && num <= 32) { s.tempo.sig_num = num; ed.dirty = true; }
    }
    ImGui::SameLine(); ImGui::TextUnformatted("/");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    int den = s.tempo.sig_den;
    if (ImGui::InputInt("time sig", &den, 1, 1)) {
        if (den >= 1 && den <= 64) { s.tempo.sig_den = den; ed.dirty = true; }
    }

    ImGui::SetNextItemWidth(190);
    double db_ms = s.tempo.downbeat_ms;
    if (ImGui::InputDouble("first downbeat (ms)", &db_ms, 1.0, 10.0, "%.1f")) {
        s.tempo.downbeat_ms = db_ms;
        ed.dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextColored(COL_DIM, "where beat 1 lands in the stems");

    ImGui::SetNextItemWidth(150);
    int ci = s.count_in_bars;
    if (ImGui::InputInt("count-in bars", &ci, 1, 1)) {
        if (ci >= 0 && ci <= 8) { s.count_in_bars = ci; ed.dirty = true; }
    }
    ImGui::SameLine(0, 30);
    int oe = (s.on_end == BT_ON_END_NEXT) ? 1 : 0;
    ImGui::TextUnformatted("on end:");
    ImGui::SameLine();
    if (ImGui::RadioButton("stop", &oe, 0)) { s.on_end = BT_ON_END_STOP; ed.dirty = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("segue into next", &oe, 1)) {
        s.on_end = BT_ON_END_NEXT;
        ed.dirty = true;
    }
    if (s.on_end == BT_ON_END_NEXT && ed.song + 1 >= ed.sl->nsongs) {
        ImGui::SameLine();
        ImGui::TextColored(COL_WARN, "last song - it will stop anyway");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("TRACKS");
    ImGui::SameLine();
    if (ImGui::Button("+ add stem")) add_track(ed, s);
    ImGui::SameLine();
    ImGui::BeginDisabled(ed.track < 0 || ed.track >= s.ntracks ||
                         s.track[ed.track].type == BT_TRACK_CLICK);
    if (ImGui::Button("align \xe2\x86\x92")) ed.screen = bt_edit_screen::align;
    ImGui::EndDisabled();
    ImGui::Spacing();

    draw_tracks(ed, s);
}

/* --------------------------------------------------------- align screen */

void draw_align_screen(bt_ui_edit &ed) {
    ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
    ImGui::TextUnformatted("EDIT  \xe2\x80\xa2  align");
    ImGui::PopStyleColor();
    if (ImGui::Button("\xe2\x86\x90 song")) ed.screen = bt_edit_screen::song;
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(COL_AMBER, "Not built yet.");
    ImGui::TextWrapped(
        "This is where the stem's waveform is drawn against the click grid so "
        "it can be dragged into time, with a few bars auditioned against the "
        "click to confirm by ear what was done by eye. bt_peaks - the envelope "
        "this needs - is in place and tested; the view itself is the next "
        "piece of work.");
}

} /* namespace */

/* -------------------------------------------------------------- public */

void bt_ui_edit_key(bt_ui_edit &ed, int vk) {
    /* Edit mode owns very few keys: text fields want the rest, and stealing
     * them from under a field being typed into is worse than having none. */
    if (ImGui::GetIO().WantTextInput) return;

    switch (vk) {
    case VK_ESCAPE:
        if (ed.screen == bt_edit_screen::align)        ed.screen = bt_edit_screen::song;
        else if (ed.screen == bt_edit_screen::song)    ed.screen = bt_edit_screen::setlist;
        break;
    case 'S':
        if (GetKeyState(VK_CONTROL) & 0x8000) do_save(ed);
        break;
    default: break;
    }
}

bool bt_ui_edit_draw(bt_ui_edit &ed) {
    static bool themed = false;
    if (!themed) { push_theme(); themed = true; }

    bool stay = true;
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("edit", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    /* The stage font is loaded large, because the play screen draws text at
     * explicit sizes and wants headroom. Widgets inherit the font's own size
     * instead, so at 48px every field collapsed to its buttons and the BPM
     * had no room to render a number. An editor is read from a foot away,
     * not across a stage. */
    const float ui_px = 20.0f;
    ImGui::PushFont(ImGui::GetFont(), ui_px);

    /* Content in a child that stops short of the footer, so the footer is
     * always visible rather than pushed off the bottom by a long table. */
    const float footer_h = ui_px * 2.6f;
    ImGui::BeginChild("content", ImVec2(0, -footer_h), false);
    switch (ed.screen) {
    case bt_edit_screen::setlist: draw_setlist_screen(ed); break;
    case bt_edit_screen::song:    draw_song_screen(ed);    break;
    case bt_edit_screen::align:   draw_align_screen(ed);   break;
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button(ed.dirty ? "save *" : "save")) do_save(ed);
    ImGui::SameLine();
    if (ImGui::Button("leave edit mode")) stay = false;
    ImGui::SameLine();

    if (ed.dirty) {
        ImGui::TextColored(COL_AMBER, "unsaved changes");
        if (ed.status[0]) {
            ImGui::SameLine();
            ImGui::TextColored(COL_DIM, "\xe2\x80\xa2 %s", ed.status);
        }
    } else if (ed.status[0]) {
        ImGui::TextColored(COL_OK, "%s", ed.status);
    } else {
        ImGui::TextColored(COL_DIM, "Ctrl+S saves \xe2\x80\xa2 ESC goes back "
                                    "\xe2\x80\xa2 E leaves edit mode");
    }

    ImGui::PopFont();
    ImGui::End();
    return stay;
}
