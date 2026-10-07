/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "bt_ui_edit.h"
#include "imgui.h"

#include <windows.h>
#include <commdlg.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cmath>

#include "backtrack/bt_device.h"
#include "backtrack/bt_peaks.h"
#include "backtrack/bt_midi.h"
#include "bt_ui_start.h"

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
    /* Two bars, not one: one is barely enough to find the tempo, and a
     * spoken cue over the count-in will want at least this much room. */
    s.count_in_bars = 2;
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

    if (ImGui::Button("+ add song")) {
        add_song(ed);
        /* Straight into it. Adding a song and then having to find and press
         * "edit song" is two actions for one intention. */
        ed.screen = bt_edit_screen::song;
        ed.track  = -1;
    }
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
    ImGui::SameLine(0, 28);
    if (ImGui::Button("audio \xe2\x80\xa6")) ed.screen = bt_edit_screen::audio;
    ImGui::SameLine();
    if (ImGui::Button("check \xe2\x80\xa6")) ed.screen = bt_edit_screen::check;
    ImGui::SameLine();
    if (ImGui::Button("lighting \xe2\x80\xa6")) ed.screen = bt_edit_screen::lighting;
    ImGui::SameLine(0, 28);
    if (ImGui::Button("open another set list \xe2\x80\xa6")) ed.want_open_setlist = true;

    ImGui::Spacing();

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("songs", 7, flags, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, 44);
        ImGui::TableSetupColumn("title",  ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn("artist", ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn("bpm",    ImGuiTableColumnFlags_WidthFixed, 64);
        /* Tuning here as well as on stage: this is the screen where the set
         * gets ordered, and grouping songs that share a tuning is most of
         * what ordering a set is for. */
        ImGui::TableSetupColumn("tuning", ImGuiTableColumnFlags_WidthFixed, 72);
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
            if (s.tuning[0]) ImGui::TextColored(COL_AMBER, "%s", s.tuning);
            else             ImGui::TextDisabled("\xe2\x80\x94");
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


/* Play, stop and a position scrub.
 *
 * Without somewhere to drag, checking the end of a four-minute song means
 * listening to four minutes of it - and the question that matters most about
 * a stem, whether it is still in time at the last chorus, lives at the end. */
void draw_transport(bt_ui_edit &ed, const char *id) {
    ImGui::PushID(id);

    if (ed.playing) {
        if (ImGui::Button("stop", ImVec2(90, 0))) ed.want_stop = true;
    } else {
        if (ImGui::Button("play", ImVec2(90, 0))) ed.want_play = true;
    }

    ImGui::SameLine();
    char pos[64];
    const double total = ed.song_sec > 0.0 ? ed.song_sec : 0.0;
    std::snprintf(pos, sizeof(pos), "%d:%05.2f / %d:%05.2f",
                  (int)(ed.play_sec / 60.0), ed.play_sec - 60.0 * (int)(ed.play_sec / 60.0),
                  (int)(total / 60.0),       total       - 60.0 * (int)(total / 60.0));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-220);
    float at = (float)ed.play_sec;
    if (ImGui::SliderFloat("##scrub", &at, 0.0f, (float)(total > 0.0 ? total : 1.0),
                           pos, ImGuiSliderFlags_NoRoundToFormat)) {
        ed.want_seek = true;
        ed.seek_sec  = at;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("drag to skip");

    /* The ends of a song are where the answers are, so make them one press. */
    ImGui::SameLine();
    if (ImGui::SmallButton("start")) { ed.want_seek = true; ed.seek_sec = 0.0; }
    ImGui::SameLine();
    if (ImGui::SmallButton("last 20s")) {
        ed.want_seek = true;
        ed.seek_sec  = total > 20.0 ? total - 20.0 : 0.0;
    }

    ImGui::PopID();
}

void draw_tracks(bt_ui_edit &ed, bt_song &s) {
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("tracks", 8, flags)) return;

    /* A column to click. The table was all widgets and no row, so there was
     * no way to say which track you meant - and the align button, which acts
     * on the selected one, could never be reached. */
    ImGui::TableSetupColumn("",      ImGuiTableColumnFlags_WidthFixed, 26);
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

        /* Spans the row and sits under the widgets, so clicking anywhere that
         * is not itself a control selects the track. */
        ImGui::TableNextColumn();
        const bool is_sel = (ed.track == t);
        /* Sized to the row, not to a line of text: the row is as tall as the
         * widgets in it, and a highlight shorter than that reads as misaligned. */
        if (ImGui::Selectable("##row", is_sel,
                              ImGuiSelectableFlags_SpanAllColumns |
                              ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(0, ImGui::GetFrameHeight())))
            ed.track = t;
        ImGui::SameLine(0, 0);
        ImGui::TextColored(is_sel ? COL_AMBER : COL_DIM, is_sel ? "\xe2\x97\x8f" : " ");

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
        if (ed.track > remove_at) ed.track--;
        else if (ed.track == remove_at) ed.track = -1;
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

/* Copy a stem into the set list's tracks folder, creating it if needed.
 * Refuses to clobber: a second stem with the same basename gets a suffix. */
bool copy_into_set(const char *from, char *dest) {
    char dir[BT_MAX_PATH];
    std::snprintf(dir, sizeof(dir), "%s", dest);
    char *slash = std::strrchr(dir, '/');
    char *back  = std::strrchr(dir, '\\');
    if (back && (!slash || back > slash)) slash = back;
    if (slash) { *slash = '\0'; CreateDirectoryA(dir, nullptr); }

    char target[BT_MAX_PATH];
    std::snprintf(target, sizeof(target), "%s", dest);
    for (int n = 2; n < 100; n++) {
        if (GetFileAttributesA(target) == INVALID_FILE_ATTRIBUTES) break;
        char stem[BT_MAX_PATH], ext[64] = "";
        std::snprintf(stem, sizeof(stem), "%s", dest);
        char *dot = std::strrchr(stem, '.');
        if (dot) { std::snprintf(ext, sizeof(ext), "%s", dot); *dot = '\0'; }
        std::snprintf(target, sizeof(target), "%s-%d%s", stem, n, ext);
    }
    if (!CopyFileA(from, target, TRUE)) return false;
    std::snprintf(dest, BT_MAX_PATH, "%s", target);
    return true;
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
        /* Stems arrive in Downloads, and the folder has to stay self-contained
         * to be copyable to the backup laptop. Refusing put that chore on the
         * user; copying it in satisfies the rule without them having to know
         * the rule exists. */
        const char *base = std::strrchr(picked, '\\');
        const char *fwd  = std::strrchr(picked, '/');
        if (fwd && (!base || fwd > base)) base = fwd;
        base = base ? base + 1 : picked;

        char dest[BT_MAX_PATH];
        std::snprintf(dest, sizeof(dest), "%s/tracks/%s", ed.sl->dir, base);
        if (!copy_into_set(picked, dest)) {
            set_status(ed, "could not copy %s into the set list folder", base);
            return;
        }
        /* copy_into_set may have renamed to avoid clobbering, so the path
         * that goes in the set list is the one it actually wrote. */
        const char *wrote = std::strrchr(dest, '\\');
        const char *wf    = std::strrchr(dest, '/');
        if (wf && (!wrote || wf > wrote)) wrote = wf;
        wrote = wrote ? wrote + 1 : dest;
        std::snprintf(rel, sizeof(rel), "tracks/%s", wrote);
        set_status(ed, "copied %s into the set list folder", wrote);
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
    ed.track = s.ntracks;      /* the one you just added is the one you want */
    s.ntracks++;
    ed.dirty = true;
    set_status(ed, "added %s", rel);
}


/* A signature of everything the engine renders a song from.
 *
 * Twenty-odd widgets set ed.dirty, and asking each of them to also request a
 * republish is a list someone will fail to add to. Hashing what the engine
 * actually reads cannot miss a field, and costs nothing at UI rates.
 *
 * Deliberately excludes the file path: changing which file a track points at
 * has to go through the loader, not a republish. */
/* What only the loader can change: which files a song's tracks point at.
 *
 * render_signature deliberately ignores these, because the engine can be
 * re-pointed at audio it already has. Adding a stem is different - the audio
 * does not exist yet, and a song with nothing to load counts as resident, so
 * nothing would ever fetch it. */
uint64_t load_signature(const bt_song &s) {
    uint64_t h = 1469598103934665603ull;
    const unsigned char *b = (const unsigned char *)&s.ntracks;
    for (size_t i = 0; i < sizeof(s.ntracks); i++) { h ^= b[i]; h *= 1099511628211ull; }
    for (int32_t i = 0; i < s.ntracks; i++) {
        const char *f = s.track[i].file;
        for (size_t k = 0; k < strnlen(f, BT_MAX_PATH); k++) {
            h ^= (unsigned char)f[k]; h *= 1099511628211ull;
        }
        h ^= (uint64_t)s.track[i].type + 1u; h *= 1099511628211ull;
    }
    return h;
}

uint64_t render_signature(const bt_song &s) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void *p, size_t n) {
        const unsigned char *b = (const unsigned char *)p;
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    };
    mix(&s.tempo.nseg, sizeof(s.tempo.nseg));
    for (int32_t i = 0; i < s.tempo.nseg; i++) mix(&s.tempo.seg[i], sizeof(s.tempo.seg[i]));
    mix(&s.tempo.sig_num, sizeof(s.tempo.sig_num));
    mix(&s.tempo.sig_den, sizeof(s.tempo.sig_den));
    mix(&s.tempo.downbeat_ms, sizeof(s.tempo.downbeat_ms));
    mix(&s.count_in_bars, sizeof(s.count_in_bars));
    mix(&s.length_bars, sizeof(s.length_bars));
    mix(&s.ntracks, sizeof(s.ntracks));
    for (int32_t i = 0; i < s.ntracks; i++) {
        const bt_track &t = s.track[i];
        mix(&t.type, sizeof(t.type));
        mix(t.bus, strnlen(t.bus, BT_MAX_NAME));
        mix(&t.gain_db, sizeof(t.gain_db));
        mix(&t.offset_ms, sizeof(t.offset_ms));
        mix(&t.muted, sizeof(t.muted));
    }
    return h;
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

    /* Whatever shorthand the band uses - "E1", "D1", "capo 3". Shown on the
     * set list, beside the artist while playing, and in the NEXT panel, which
     * is when somebody reaches for a different guitar. */
    ImGui::SameLine(0, 26);
    ImGui::SetNextItemWidth(110);
    if (ImGui::InputText("tuning", s.tuning, sizeof(s.tuning))) ed.dirty = true;

    ImGui::SetNextItemWidth(620);
    if (ImGui::InputText("cue", s.cue, sizeof(s.cue))) ed.dirty = true;
    ImGui::SameLine();
    ImGui::TextDisabled("shown while the song plays - \"drums in at 24\", "
                        "\"vocals B34\"");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Shown on stage, exactly as typed.\n"
                          "Blank shows nothing.");

    /* Tempo is metadata, not analysis: it has to match what is already in the
     * stems. Saying so here is cheaper than anyone discovering it. */
    ImGui::Spacing();
    /* Typed as text, not through InputDouble's format string: a tempo of
     * 96.515 shown as 96.52 is a different song, and this is the screen where
     * the number is the truth rather than something rounded for a stage. The
     * other screens round on purpose; this one shows what the file says. */
    ImGui::SetNextItemWidth(190);
    {
        static char buf[32];
        static bool editing = false;
        if (!editing || !ImGui::IsItemActive())
            bt_format_number(s.tempo.nseg ? s.tempo.seg[0].bpm : 120.0,
                             buf, sizeof(buf));
        if (ImGui::InputText("BPM", buf, sizeof(buf),
                             ImGuiInputTextFlags_CharsDecimal)) {
            editing = true;
            const double v = strtod(buf, nullptr);
            if (v > 1.0 && v < 400.0) {
                if (s.tempo.nseg == 0) s.tempo.nseg = 1;
                s.tempo.seg[0].bpm = v;
                ed.dirty = true;
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) editing = false;
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
    /* Length matters only when the audio does not already say. A song with
     * stems ends when they do; a click-only song has nothing to end it. */
    bool click_only = true;
    for (int32_t i = 0; i < s.ntracks; i++)
        if (s.track[i].type == BT_TRACK_AUDIO) click_only = false;

    ImGui::SameLine(0, 30);
    ImGui::SetNextItemWidth(150);
    int lb = s.length_bars;
    if (ImGui::InputInt("ends at bar", &lb, 1, 8)) {
        if (lb >= 0 && lb <= 10000) { s.length_bars = lb; ed.dirty = true; }
    }
    ImGui::SameLine();
    {
        double bpm = s.tempo.nseg ? s.tempo.seg[0].bpm : 120.0;
        int    sig = s.tempo.sig_num > 0 ? s.tempo.sig_num : 4;
        if (s.length_bars > 0 && bpm > 0.0) {
            double sec = (double)s.length_bars * sig * 60.0 / bpm;
            ImGui::TextDisabled("%d:%04.1f", (int)(sec / 60.0),
                                sec - 60.0 * (int)(sec / 60.0));
            /* Say so when this is cutting audio off, and that it will not be
             * an abrupt one. */
            const int32_t sr = ed.dev && ed.dev->sample_rate > 0
                             ? ed.dev->sample_rate : 48000;
            if (bt_song_fade_frames(&s, sr) > 0) {
                ImGui::SameLine();
                ImGui::TextColored(COL_AMBER,
                                   "ends before the stems do - it fades out");
            }
        } else if (click_only) {
            ImGui::TextColored(COL_WARN,
                "no stems and no length - this song will count in and stop");
        } else {
            ImGui::TextDisabled("0 = play the stems out");
        }
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
    /* Audition. Blocked on nothing: this is the point of the screen. */
    ImGui::Separator();
    draw_transport(ed, "song");
    ImGui::TextDisabled("nudge while it plays - you will hear it from where you are, "
                        "not from the top");

    /* ---- lighting cues ---------------------------------------------- */
    if (ed.sl && ed.sl->light.channel > 0) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("LIGHTING");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(130);
        int pc = s.midi_program;
        if (ImGui::InputInt("program", &pc, 1, 1)) {
            if (pc >= -1 && pc <= 127) { s.midi_program = pc; ed.dirty = true; }
        }
        ImGui::SameLine();
        if (s.midi_program < 0)
            ImGui::TextDisabled("-1 sends nothing - the desk keeps whatever it had");
        else
            ImGui::TextDisabled("sent when this song starts, so the desk loads "
                                "its cue list before bar 1");

        ImGui::SameLine(0, 22);
        ImGui::BeginDisabled(s.nlight_cues >= BT_MAX_LIGHT_CUES);
        if (ImGui::Button("+ cue")) {
            bt_light_cue &c = s.light_cue[s.nlight_cues];
            std::memset(&c, 0, sizeof(c));
            /* A bar after the last one, since cues are written in order and
             * the next one is always later than the last. */
            c.bar = s.nlight_cues > 0 ? s.light_cue[s.nlight_cues - 1].bar + 8 : 1;
            s.nlight_cues++;
            ed.dirty = true;
        }
        ImGui::EndDisabled();

        if (s.nlight_cues == 0) {
            ImGui::TextDisabled("no cues - this song sends its program change "
                                "and nothing else");
        } else if (ImGui::BeginTable("cues", 4,
                   ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                   ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("bar",  ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupColumn("does", ImGuiTableColumnFlags_WidthFixed, 175);
            ImGui::TableSetupColumn("what it is for", ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn("",     ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableHeadersRow();

            int32_t remove_cue = -1;
            for (int32_t i = 0; i < s.nlight_cues; i++) {
                bt_light_cue &c = s.light_cue[i];
                ImGui::PushID(3000 + i);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                int bar = c.bar;
                if (ImGui::InputInt("##bar", &bar, 0, 0)) {
                    if (bar >= 1 && bar <= 10000) { c.bar = bar; ed.dirty = true; }
                }

                /* What the cue does, not which note it sends. The notes are
                 * set up once on the lighting screen; here the question is
                 * musical - does the song go on, finish, or stop dead. */
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                const bt_light_cfg &lc = ed.sl->light;
                const int32_t eff = bt_light_cue_note(&lc, &c);
                const char *what = eff == lc.next_note     ? "go"
                                 : eff == lc.end_note      ? "between songs"
                                 : eff == lc.blackout_note ? "blackout"
                                 : eff == lc.prev_note     ? "back one"
                                 : "note";
                char combo[48];
                std::snprintf(combo, sizeof(combo), "%s##what", what);
                if (ImGui::BeginCombo("##what", combo)) {
                    if (ImGui::Selectable("go", eff == lc.next_note))
                        { c.note = 0; ed.dirty = true; }
                    if (lc.end_note > 0 &&
                        ImGui::Selectable("between songs", eff == lc.end_note))
                        { c.note = lc.end_note; ed.dirty = true; }
                    if (lc.blackout_note > 0 &&
                        ImGui::Selectable("blackout", eff == lc.blackout_note))
                        { c.note = lc.blackout_note; ed.dirty = true; }
                    ImGui::Separator();
                    ImGui::SetNextItemWidth(90);
                    int raw = c.note;
                    if (ImGui::InputInt("note##raw", &raw, 0, 0)) {
                        if (raw >= 0 && raw <= 127) { c.note = raw; ed.dirty = true; }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("go      - the next look (note %d)\n"
                                      "between - the talking look at the end (note %d)\n"
                                      "blackout- straight to dark, for a hard ending (note %d)",
                                      lc.next_note, lc.end_note, lc.blackout_note);

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##desc", c.desc, sizeof(c.desc))) ed.dirty = true;

                ImGui::TableNextColumn();
                if (ImGui::SmallButton("x")) remove_cue = i;
                ImGui::PopID();
            }
            ImGui::EndTable();

            if (remove_cue >= 0) {
                for (int32_t i = remove_cue; i + 1 < s.nlight_cues; i++)
                    s.light_cue[i] = s.light_cue[i + 1];
                s.nlight_cues--;
                ed.dirty = true;
            }
        }

        /* Cues out of order would fire out of order, and a cue list only
         * knows "next" - so it is worth saying rather than discovering. */
        for (int32_t i = 1; i < s.nlight_cues; i++) {
            if (s.light_cue[i].bar <= s.light_cue[i - 1].bar) {
                ImGui::TextColored(COL_WARN,
                    "cue %d is not after cue %d - a cue list advances in order, "
                    "so these will not line up", i + 1, i);
                break;
            }
        }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("TRACKS");
    ImGui::SameLine();
    if (ImGui::Button("+ add stem")) add_track(ed, s);

    const bool have_sel = ed.track >= 0 && ed.track < s.ntracks;

    ImGui::SameLine(0, 18);
    ImGui::BeginDisabled(!have_sel || ed.track == 0);
    if (ImGui::Button("move up")) {
        if (bt_song_move_track(&s, ed.track, ed.track - 1) == BT_OK) {
            ed.track--;
            ed.dirty = true;
            /* Order changes nothing about the sound - the mixer sums the
             * tracks - so there is no reason to interrupt the audio to
             * republish it. */
            ed.sig_only = true;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!have_sel || ed.track + 1 >= s.ntracks);
    if (ImGui::Button("move down")) {
        if (bt_song_move_track(&s, ed.track, ed.track + 1) == BT_OK) {
            ed.track++;
            ed.dirty = true;
            ed.sig_only = true;
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine(0, 18);
    ImGui::BeginDisabled(!have_sel || s.track[ed.track].type == BT_TRACK_CLICK);
    if (ImGui::Button("align \xe2\x86\x92")) ed.screen = bt_edit_screen::align;
    ImGui::EndDisabled();
    if (!have_sel && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Click a track below to choose one.");
    ImGui::Spacing();

    draw_tracks(ed, s);
}

/* --------------------------------------------------------- align screen */

void draw_audio_screen(bt_ui_edit &ed);   /* defined below, needs bt_device.h */
void draw_check_screen(bt_ui_edit &ed);   /* defined below                    */
void draw_lighting_screen(bt_ui_edit &ed);


/* The first sample loud enough to be the music rather than the room.
 *
 * Reported, never acted on. There used to be a button that nudged this onto
 * beat 1, which assumed a stem's first sound is its downbeat - false for any
 * stem that enters partway through a song. On a synth part that comes in
 * after thirty seconds it moved the stem thirty seconds early, and there was
 * no undo. Where the audio starts is worth knowing; what it means is the
 * reader's call.
 *
 * Returns -1 for a stem that is silent throughout, which happens when the
 * wrong file gets downloaded. */
int32_t sig_num_of(const bt_song &s) {
    return s.tempo.sig_num > 0 ? s.tempo.sig_num : 4;
}

bt_frame find_first_sound(const bt_track &t) {
    if (!t.pcm || t.frames <= 0) return -1;
    float peak = 0.0f;
    for (int32_t c = 0; c < t.channels; c++)
        for (bt_frame i = 0; i < t.frames; i++) {
            float a = t.pcm[c][i]; a = a < 0 ? -a : a;
            if (a > peak) peak = a;
        }
    if (peak <= 0.0f) return -1;
    const float thresh = peak * 0.025f;
    for (bt_frame i = 0; i < t.frames; i++)
        for (int32_t c = 0; c < t.channels; c++) {
            float a = t.pcm[c][i]; a = a < 0 ? -a : a;
            if (a > thresh) return i;
        }
    return -1;
}

void draw_align_screen(bt_ui_edit &ed) {
    ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
    ImGui::TextUnformatted("EDIT  \xe2\x80\xa2  align");
    ImGui::PopStyleColor();
    if (ImGui::Button("\xe2\x86\x90 song")) ed.screen = bt_edit_screen::song;

    if (!ed.sl || ed.song < 0 || ed.song >= ed.sl->nsongs) return;
    bt_song &s = ed.sl->song[ed.song];
    if (ed.track < 0 || ed.track >= s.ntracks) { ImGui::TextUnformatted("No track."); return; }
    bt_track &t = s.track[ed.track];
    if (t.type == BT_TRACK_CLICK) {
        ImGui::TextColored(COL_DIM, "The click is generated, so there is nothing to align - "
                                    "it is the thing everything else aligns to.");
        return;
    }

    const int32_t sr = ed.dev && ed.dev->sample_rate > 0 ? ed.dev->sample_rate : 48000;

    ImGui::SameLine(0, 24);
    ImGui::Text("%s", t.name);
    ImGui::Spacing();
    draw_transport(ed, "align");

    /* The stem has to be in memory to be drawn, and only the loader puts it
     * there - so ask the host to make this song the live one. */
    if (!t.pcm || t.frames <= 0) {
        ImGui::Spacing();
        ImGui::TextColored(COL_AMBER, "This song's stems are not in memory yet.");
        if (ImGui::Button("load it")) {
            /* Ask for the audio first. Selecting a song whose stems have not
             * been fetched fails, which is what used to happen here. */
            ed.want_reload = true;
            ed.want_select = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("stems are held for the current song and the next one");
        return;
    }

    /* Rebuild the envelope when the source changes, and not otherwise. */
    if (ed.peaks_song != ed.song || ed.peaks_track != ed.track ||
        ed.peaks_src != (const void *)t.pcm) {
        bt_peaks_free(&ed.peaks);
        if (bt_peaks_build((const float *const *)t.pcm, t.channels, t.frames,
                           512, &ed.peaks) == BT_OK) {
            ed.peaks_song  = ed.song;
            ed.peaks_track = ed.track;
            ed.peaks_src   = (const void *)t.pcm;
            ed.first_sound = find_first_sound(t);
        }
    }

    ImGui::Separator();

    /* ---- controls ---- */
    /* Tempo, here as well as in the song editor. A guessed BPM is something
     * you fix by looking at the grid against the waveform, which is this
     * screen - and it takes effect while the song plays, so you can hear the
     * click move onto the beat. */
    ImGui::SetNextItemWidth(150);
    double bpm = s.tempo.nseg ? s.tempo.seg[0].bpm : 120.0;
    if (ImGui::InputDouble("BPM", &bpm, 0.1, 1.0, "%.2f")) {
        if (bpm >= 20.0 && bpm <= 400.0) {
            if (s.tempo.nseg < 1) s.tempo.nseg = 1;
            s.tempo.seg[0].bpm = bpm;
            s.tempo.seg[0].start_beat = 0;
            ed.dirty = true;
        }
    }
    if (s.tempo.nseg > 1) {
        ImGui::SameLine();
        ImGui::TextColored(COL_AMBER,
                           "this song has a tempo map - editing the first "
                           "segment only");
    }

    ImGui::SameLine(0, 24);
    ImGui::SetNextItemWidth(130);
    int off = t.offset_ms;
    if (ImGui::InputInt("nudge (ms)", &off, 1, 10)) {
        if (off > -600000 && off < 600000) { t.offset_ms = off; ed.dirty = true; }
    }
    /* A spoken cue - "starts on beat three, here we go" - is a stem that
     * plays over the count-in. The engine needs nothing for it: the playhead
     * is negative there and a track's offset is subtracted from it. What it
     * needed was somebody not having to work out that two bars of 4/4 at 148
     * is -3243 ms. */
    if (s.count_in_bars > 0) {
        ImGui::SameLine(0, 20);
        if (ImGui::Button("start on the count-in")) {
            const int64_t beats_in = (int64_t)s.count_in_bars * sig_num_of(s);
            const bt_frame ci = bt_tempo_beat_frame(&s.tempo, -beats_in, sr);
            const bt_frame first = ed.first_sound > 0 ? ed.first_sound : 0;
            t.offset_ms = (int32_t)llround((double)(ci - first) * 1000.0 / sr);
            ed.dirty = true;
            set_status(ed, "%s now starts with the count-in", t.name);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("For a spoken cue played to the in-ears over the\n"
                              "count-in. Puts this stem's first sound on the\n"
                              "first count-in beat.");
    }

    ImGui::SameLine(0, 20);
    ImGui::SetNextItemWidth(150);
    float zoom = (float)ed.view_len;
    if (ImGui::SliderFloat("seconds shown", &zoom, 0.5f, 60.0f, "%.1f s",
                           ImGuiSliderFlags_Logarithmic))
        ed.view_len = zoom;
    ImGui::SameLine();
    ImGui::TextDisabled("drag the waveform to nudge  \xc2\xb7  scroll to zoom");

    /* ---- the view ---- */
    /* Fill the window. Vertical resolution is what lets you see whether a
     * transient sits on the line or beside it, which is the entire job. */
    float avail_h = ImGui::GetContentRegionAvail().y - 90.0f;
    if (avail_h < 180.0f) avail_h = 180.0f;
    const ImVec2 size(ImGui::GetContentRegionAvail().x, avail_h);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("wave", size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(16, 19, 24, 255));

    /* Follow the playhead while it plays, and also when a seek has put it
     * outside the view - dragging the scrub while stopped is how you look at
     * the end of a song without listening to it. */
    if (ed.playing) {
        ed.view_start = ed.play_sec - ed.view_len * 0.35;
    } else if (ed.play_sec < ed.view_start ||
               ed.play_sec > ed.view_start + ed.view_len) {
        ed.view_start = ed.play_sec - ed.view_len * 0.35;
    }

    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const double at = ed.view_start +
                          ed.view_len * (double)((ImGui::GetIO().MousePos.x - p0.x) / size.x);
        ed.view_len *= (ImGui::GetIO().MouseWheel > 0 ? 0.85 : 1.0 / 0.85);
        if (ed.view_len < 0.2)  ed.view_len = 0.2;
        if (ed.view_len > 120.0) ed.view_len = 120.0;
        /* Keep the point under the cursor where it is: zooming somewhere
         * other than where you are looking is disorienting. */
        ed.view_start = at - ed.view_len * (double)((ImGui::GetIO().MousePos.x - p0.x) / size.x);
    }

    const double t0 = ed.view_start, t1 = ed.view_start + ed.view_len;
    auto x_of = [&](double sec) { return p0.x + (float)((sec - t0) / (t1 - t0)) * size.x; };

    /* Beat grid, from the beat index rather than accumulated - the same rule
     * the click obeys, so what is drawn is where the click actually lands. */
    const int64_t b_from = bt_tempo_frame_beat(&s.tempo, (bt_frame)(t0 * sr), sr) - 1;
    const int64_t b_to   = bt_tempo_frame_beat(&s.tempo, (bt_frame)(t1 * sr), sr) + 1;
    const int32_t sig    = s.tempo.sig_num > 0 ? s.tempo.sig_num : 4;
    if (b_to - b_from < 4000) {
        for (int64_t b = b_from; b <= b_to; b++) {
            const double sec = (double)bt_tempo_beat_frame(&s.tempo, b, sr) / sr;
            const float x = x_of(sec);
            if (x < p0.x - 2 || x > p0.x + size.x + 2) continue;
            const bool bar = (b % sig) == 0;
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + size.y),
                        bar ? IM_COL32(86, 112, 150, 255) : IM_COL32(44, 52, 66, 255),
                        bar ? 1.6f : 1.0f);
            if (bar && ed.view_len < 30.0) {
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%lld", (long long)(b / sig) + 1);
                dl->AddText(ImVec2(x + 3, p0.y + 3), IM_COL32(120, 140, 170, 255), lbl);
            }
        }
    }

    /* Waveform, drawn where it will actually sound: the stem's own time plus
     * its nudge. */
    const double off_sec = t.offset_ms / 1000.0;
    const int kMaxCols = 4096;
    int cols = (int)size.x;
    if (cols > kMaxCols) cols = kMaxCols;
    if (cols < 1) cols = 1;
    static float mn[kMaxCols], mx[kMaxCols];
    const bt_frame from = (bt_frame)llround((t0 - off_sec) * sr);
    const bt_frame to   = (bt_frame)llround((t1 - off_sec) * sr);
    const bt_frame per  = (to - from) / (cols > 0 ? cols : 1);

    bool ok;
    if (per < ed.peaks.frames_per_bucket)
        ok = bt_peaks_range((const float *const *)t.pcm, t.channels, t.frames,
                            from, to, mn, mx, cols) == BT_OK;
    else
        ok = bt_peaks_read(&ed.peaks, from, to, mn, mx, cols) == BT_OK;

    if (ok) {
        const float mid = p0.y + size.y * 0.5f;
        const float amp = size.y * 0.45f;
        for (int i = 0; i < cols; i++) {
            const float x = p0.x + (float)i;
            float a = mid - mx[(size_t)i] * amp, b = mid - mn[(size_t)i] * amp;
            if (b - a < 1.0f) { a -= 0.5f; b += 0.5f; }
            dl->AddLine(ImVec2(x, a), ImVec2(x, b), IM_COL32(118, 176, 228, 235));
        }
    }

    /* Where the stem begins and ends, so "it starts here" is visible even
     * when the audio there is quiet. */
    for (int e = 0; e < 2; e++) {
        const float x = x_of(off_sec + (e ? (double)t.frames / sr : 0.0));
        if (x > p0.x && x < p0.x + size.x)
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + size.y),
                        IM_COL32(236, 196, 96, 200), 1.5f);
    }

    /* Always drawn. Stopped, it is where playing would resume from, which is
     * exactly what you want to see while scrubbing. */
    {
        const float x = x_of(ed.play_sec);
        if (x >= p0.x && x <= p0.x + size.x)
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + size.y),
                        ed.playing ? IM_COL32(245, 245, 245, 230)
                                   : IM_COL32(245, 245, 245, 140), 1.8f);
    }

    /* Drag to nudge. A pixel is a known number of milliseconds, so this is
     * the same edit as the number field, done with the hand instead. */
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const double ms_per_px = (ed.view_len * 1000.0) / size.x;
        const double d = ImGui::GetIO().MouseDelta.x * ms_per_px;
        if (d != 0.0) {
            t.offset_ms = (int32_t)llround(t.offset_ms + d);
            ed.dirty = true;
        }
    }

    dl->AddRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(60, 70, 86, 255));

    ImGui::Spacing();
    ImGui::TextDisabled(
        "blue = the stem where it will sound   \xc2\xb7   amber = its first and last "
        "sample   \xc2\xb7   bright lines = bar starts");
    if (t.offset_ms)
        ImGui::Text("%s plays %d ms %s than the file says",
                    t.name, t.offset_ms < 0 ? -t.offset_ms : t.offset_ms,
                    t.offset_ms < 0 ? "earlier" : "later");
    else
        ImGui::TextDisabled("%s plays exactly where the file says", t.name);

    /* Where the audio lands against beat 1 as a number, because "slightly
     * late" and "40 ms late" are different problems and the eye cannot tell
     * them apart at this zoom. */
    if (ed.first_sound >= 0) {
        const double lands = off_sec + (double)ed.first_sound / sr;
        ImGui::TextDisabled("its first audible sound is at %d:%05.2f",
                            (int)(lands / 60.0), lands - 60.0 * (int)(lands / 60.0));
    }
}

} /* namespace */

/* -------------------------------------------------------------- public */

void bt_ui_edit_key(bt_ui_edit &ed, int vk) {
    /* Edit mode owns very few keys: text fields want the rest, and stealing
     * them from under a field being typed into is worse than having none. */
    if (ImGui::GetIO().WantTextInput) return;

    switch (vk) {
    case VK_ESCAPE:
        /* Backs out one level each press, and off the top it leaves edit mode
         * entirely - so ESC always means "back", never "quit". */
        if (ed.screen == bt_edit_screen::align)      ed.screen = bt_edit_screen::song;
        else if (ed.screen == bt_edit_screen::song)  ed.screen = bt_edit_screen::setlist;
        else if (ed.screen == bt_edit_screen::audio) ed.screen = bt_edit_screen::setlist;
        else if (ed.screen == bt_edit_screen::check) ed.screen = bt_edit_screen::setlist;
        else if (ed.screen == bt_edit_screen::lighting) ed.screen = bt_edit_screen::setlist;
        else                                         ed.leave = true;
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
    if (ed.leave) { ed.leave = false; stay = false; }
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
    case bt_edit_screen::audio:   draw_audio_screen(ed);   break;
    case bt_edit_screen::check:   draw_check_screen(ed);   break;
    case bt_edit_screen::lighting: draw_lighting_screen(ed); break;
    }

    /* One place, after everything has drawn, so no widget can forget. */
    if (ed.sl && ed.song >= 0 && ed.song < ed.sl->nsongs) {
        uint64_t sig = render_signature(ed.sl->song[ed.song]);
        if (ed.last_sig != 0 && sig != ed.last_sig && !ed.sig_only)
            ed.want_reapply = true;
        ed.last_sig = sig;
        ed.sig_only = false;

        /* A changed stem has to go back through the loader, not the engine. */
        uint64_t lsig = load_signature(ed.sl->song[ed.song]);
        if (ed.last_load_sig != 0 && lsig != ed.last_load_sig)
            ed.want_reload = true;
        ed.last_load_sig = lsig;
    } else {
        ed.last_sig = 0;
    }
    ImGui::EndChild();

    ImGui::Separator();
    /* Disabled rather than failing in a status line nobody reads. A save
     * button that looks like it worked, on a set list with nowhere to save
     * to, is how an edit survives into the running app and then vanishes on
     * restart. */
    ImGui::BeginDisabled(!ed.can_save);
    if (ImGui::Button(ed.dirty ? "save *" : "save")) {
        do_save(ed);
        /* Stop the audition too. Leaving a song playing while the screen goes
         * back to the set list leaves you hunting for where the sound is
         * coming from. */
        if (ed.playing) ed.want_stop = true;
        /* Saving a song means you are done with it, so go back to the list
         * rather than making that a second press - and a second press that
         * sits next to "leave edit mode", which is not what anyone meant. */
        if (ed.screen == bt_edit_screen::song || ed.screen == bt_edit_screen::align)
            ed.screen = bt_edit_screen::setlist;
    }
    ImGui::EndDisabled();
    if (!ed.can_save && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("This is the built-in demo set and has no file.\n"
                          "Start with:  btui --setlist <file> --device <file>");
    ImGui::SameLine();
    if (ImGui::Button("leave edit mode")) stay = false;
    ImGui::SameLine();

    if (!ed.can_save) {
        ImGui::TextColored(COL_WARN,
            "demo set - edits are in memory only and will be lost on exit");
        ImGui::SameLine();
        ImGui::TextColored(COL_DIM,
            "\xe2\x80\xa2 start with --setlist <file> to edit a real one");
    } else if (ed.dirty) {
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

/* ======================================================================
 * Audio routing
 *
 * Everything this screen needs already existed: bt_device_* enumerates with
 * API names and channel counts, and bt_device_cfg_save_file writes the file
 * losslessly. What was missing was somewhere to see it, so the only way to
 * configure audio was to read a CLI listing and hand-write JSON.
 * ==================================================================== */


namespace {

/* MME and DirectSound are listed only on request. They work, and on Windows
 * they are where a hundred milliseconds of latency comes from - offering them
 * beside WASAPI with equal weight invites the wrong choice. */
bool api_is_preferred(const char *api) {
    if (!api) return false;
    return strstr(api, "ASIO") || strstr(api, "WASAPI") || strstr(api, "WDM-KS")
        || strstr(api, "Core Audio") || strstr(api, "ALSA") || strstr(api, "JACK");
}

int32_t channels_of(int32_t idx) {
    bt_device_info di;
    if (idx < 0 || bt_device_get(idx, &di) != BT_OK) return 0;
    return di.max_out_channels;
}

/* A sensible starting map for a device we have just picked: front of house on
 * the first pair, click on the second if there is one, and the stereo
 * fallback - band one side, click the other - if there is not. */
void default_buses(bt_device_cfg &cfg, int32_t nch) {
    memset(cfg.bus, 0, sizeof(cfg.bus));
    snprintf(cfg.bus[0].name, BT_MAX_NAME, "foh");
    snprintf(cfg.bus[1].name, BT_MAX_NAME, "inear");
    if (nch >= 4) {
        cfg.bus[0].ch[0] = 0; cfg.bus[0].ch[1] = 1; cfg.bus[0].nch = 2;
        cfg.bus[1].ch[0] = 2; cfg.bus[1].ch[1] = 3; cfg.bus[1].nch = 2;
    } else {
        cfg.bus[0].ch[0] = 0; cfg.bus[0].nch = 1;
        cfg.bus[1].ch[0] = 1; cfg.bus[1].nch = 1;
    }
    cfg.nbuses = 2;
}

void draw_audio_screen(bt_ui_edit &ed) {
    bt_device_cfg &cfg = *ed.dev;

    ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
    ImGui::TextUnformatted("EDIT  \xe2\x80\xa2  audio");
    ImGui::PopStyleColor();
    if (ImGui::Button("\xe2\x86\x90 set list")) ed.screen = bt_edit_screen::setlist;
    ImGui::Separator();

    /* ---- device ---- */
    ImGui::TextUnformatted("DEVICE");
    /* Right-aligned, so it reads as an option on the list rather than as a
     * property of the word next to it. */
    {
        const char *lbl = "show every API";
        float w = ImGui::CalcTextSize(lbl).x + ImGui::GetFrameHeight()
                + ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - w);
        ImGui::Checkbox(lbl, &ed.show_all_apis);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("MME and DirectSound work and are slow - on this\n"
                              "hardware roughly 90-120 ms against WASAPI's 3 ms.");
    }

    const int32_t ndev = bt_device_count();
    if (ndev == 0) {
        ImGui::TextColored(COL_WARN, "No audio devices found.");
        ImGui::TextDisabled("Plug the interface in; this list refreshes on its own.");
    }

    ImGui::BeginChild("devlist", ImVec2(0, 230), true);
    for (int32_t i = 0; i < ndev; i++) {
        bt_device_info di;
        if (bt_device_get(i, &di) != BT_OK) continue;
        if (di.max_out_channels <= 0) continue;                 /* inputs */
        if (!ed.show_all_apis && !api_is_preferred(di.api)) continue;

        bool sel = (ed.picked_device == i);
        char row[320];
        std::snprintf(row, sizeof(row), "%-44.44s  %-14.14s  %d ch  %.0f Hz  %.1f ms##d%d",
                      di.name, di.api, di.max_out_channels,
                      di.default_sample_rate, di.default_low_latency * 1000.0, i);
        if (ImGui::Selectable(row, sel)) {
            ed.picked_device = i;
            std::snprintf(cfg.device, sizeof(cfg.device), "%s", di.name);
            std::snprintf(cfg.api, sizeof(cfg.api), "%s", di.api);
            if (di.default_sample_rate >= 8000.0)
                cfg.sample_rate = (int32_t)di.default_sample_rate;
            default_buses(cfg, di.max_out_channels);
            ed.device_dirty = true;
        }
    }
    ImGui::EndChild();

    if (cfg.device[0])
        ImGui::Text("selected:  %s   (%s)", cfg.device,
                    cfg.api[0] ? cfg.api : "best available");

    ImGui::Spacing();
    ImGui::Separator();

    /* ---- stream ---- */
    ImGui::TextUnformatted("STREAM");
    ImGui::SetNextItemWidth(170);
    int rate = cfg.sample_rate;
    if (ImGui::InputInt("sample rate", &rate, 0, 0)) {
        if (rate >= 8000 && rate <= 192000) { cfg.sample_rate = rate; ed.device_dirty = true; }
    }

    ImGui::SameLine(0, 24);
    ImGui::SetNextItemWidth(230);
    static const int kBufs[] = { 128, 256, 512, 1024, 2048 };
    int cur = 2;
    for (int i = 0; i < 5; i++) if (kBufs[i] == cfg.buffer_frames) cur = i;
    char blabel[64];
    std::snprintf(blabel, sizeof(blabel), "%d  (%.1f ms)", cfg.buffer_frames,
                  1000.0 * cfg.buffer_frames / (cfg.sample_rate > 0 ? cfg.sample_rate : 48000));
    if (ImGui::BeginCombo("buffer", blabel)) {
        for (int i = 0; i < 5; i++) {
            char item[64];
            std::snprintf(item, sizeof(item), "%d  (%.1f ms)%s", kBufs[i],
                          1000.0 * kBufs[i] / (cfg.sample_rate > 0 ? cfg.sample_rate : 48000),
                          kBufs[i] == 512 ? "   \xe2\x80\x94 recommended" : "");
            if (ImGui::Selectable(item, i == cur)) {
                cfg.buffer_frames = kBufs[i];
                ed.device_dirty = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("latency delays the click and the tracks together, so "
                        "a bigger buffer costs nothing here");

    ImGui::Spacing();
    ImGui::Separator();

    /* ---- buses ---- */
    ImGui::TextUnformatted("BUSES");
    ImGui::SameLine();
    ImGui::BeginDisabled(cfg.nbuses >= BT_MAX_BUSES);
    if (ImGui::Button("+ add bus")) {
        bt_bus &b = cfg.bus[cfg.nbuses];
        std::memset(&b, 0, sizeof(b));
        std::snprintf(b.name, BT_MAX_NAME, "bus%d", cfg.nbuses + 1);
        b.nch = 1;
        cfg.nbuses++;
        ed.device_dirty = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("setlist.json refers to these names, never to channel numbers");

    const int32_t nch = ed.picked_device >= 0 ? channels_of(ed.picked_device)
                                              : BT_MAX_BUS_CH * 2;
    int32_t remove_bus = -1;

    for (int32_t b = 0; b < cfg.nbuses; b++) {
        ImGui::PushID(1000 + b);
        ImGui::SetNextItemWidth(170);
        if (ImGui::InputText("##name", cfg.bus[b].name, BT_MAX_NAME))
            ed.device_dirty = true;

        /* Channels as toggles rather than typed numbers: the question is
         * "which outputs", and the device knows how many it has. */
        for (int32_t c = 0; c < nch && c < 16; c++) {
            ImGui::SameLine(0, c == 0 ? 16.0f : 4.0f);
            bool on = false;
            for (int32_t k = 0; k < cfg.bus[b].nch; k++)
                if (cfg.bus[b].ch[k] == c) on = true;

            char lbl[16];
            std::snprintf(lbl, sizeof(lbl), "%d##ch%d_%d", c + 1, b, c);
            if (on) ImGui::PushStyleColor(ImGuiCol_Button,
                                          ImVec4(0.290f, 0.510f, 0.780f, 1.0f));
            if (ImGui::Button(lbl, ImVec2(34, 0))) {
                if (on) {
                    int32_t w = 0;
                    for (int32_t k = 0; k < cfg.bus[b].nch; k++)
                        if (cfg.bus[b].ch[k] != c) cfg.bus[b].ch[w++] = cfg.bus[b].ch[k];
                    cfg.bus[b].nch = w;
                } else if (cfg.bus[b].nch < BT_MAX_BUS_CH) {
                    cfg.bus[b].ch[cfg.bus[b].nch++] = c;
                }
                ed.device_dirty = true;
            }
            if (on) ImGui::PopStyleColor();
        }

        ImGui::SameLine(0, 18);
        if (ImGui::SmallButton("remove")) remove_bus = b;
        ImGui::PopID();
    }

    if (remove_bus >= 0) {
        for (int32_t i = remove_bus; i + 1 < cfg.nbuses; i++) cfg.bus[i] = cfg.bus[i + 1];
        cfg.nbuses--;
        ed.device_dirty = true;
    }

    /* A bus a set list routes to but this machine does not define is the
     * commonest way a set fails to open, so say it here rather than later. */
    if (ed.sl) {
        for (int32_t i = 0; i < ed.sl->nsongs; i++)
            for (int32_t t = 0; t < ed.sl->song[i].ntracks; t++) {
                const char *want = ed.sl->song[i].track[t].bus;
                if (!bt_device_find_bus(&cfg, want)) {
                    ImGui::TextColored(COL_WARN,
                        "song %d routes to \"%s\", which is not defined above",
                        i + 1, want);
                    i = ed.sl->nsongs;      /* one is enough to make the point */
                    break;
                }
            }
    }

    ImGui::Spacing();
    ImGui::Separator();
    /* Three places this file can live, so never make anyone guess which. */
    {
        char machine[BT_MAX_PATH] = {0};
        bool is_machine = bt_ui_machine_device_path(machine, sizeof(machine)) &&
                          _stricmp(machine, ed.device_path) == 0;
        ImGui::TextColored(COL_DIM, "%s", is_machine
            ? "This is the machine's routing: it applies to every set list you "
              "open, and survives upgrading the program."
            : "This routing is stored with this set list, so it travels with "
              "the folder and overrides the machine's.");
        ImGui::TextDisabled("%s", ed.device_path);
    }

    /* Anyone upgrading already has a device.json beside their set list, and
     * it keeps winning - correctly, but it means the machine-wide setting
     * would never once be reached. This promotes it in one press. */
    {
        char machine[BT_MAX_PATH] = {0};
        bool have = bt_ui_machine_device_path(machine, sizeof(machine));
        bool is_machine = have && _stricmp(machine, ed.device_path) == 0;
        if (have && !is_machine) {
            if (ImGui::Button("make this the machine's default")) {
                bt_err e = bt_device_cfg_save_file(&cfg, machine);
                if (e == BT_OK)
                    set_status(ed, "saved as this machine's default: %s", machine);
                else
                    set_status(ed, "could not save: %s", bt_strerror(e));
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Every set list without routing of its own "
                                  "will use this.\nThis set list keeps its own "
                                  "until you delete its device.json.");
            ImGui::SameLine(0, 24);
        }
    }

    ImGui::BeginDisabled(!ed.can_save_device);
    if (ImGui::Button("save device.json and reopen audio")) {
        bt_err e = bt_device_cfg_save_file(&cfg, ed.device_path);
        if (e == BT_OK) {
            ed.device_dirty  = false;
            ed.reopen_device = true;
            set_status(ed, "saved %s", ed.device_path);
        } else {
            set_status(ed, "save failed: %s", bt_strerror(e));
        }
    }
    ImGui::EndDisabled();
    if (!ed.can_save_device &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("No device.json to write to.\n"
                          "Start with:  btui --setlist <file> --device <file>");
    ImGui::SameLine();
    if (ed.device_dirty) ImGui::TextColored(COL_AMBER, "unsaved audio changes");
}

} /* namespace */

/* ======================================================================
 * Validation and export: what btcheck and btrender do, without a terminal.
 * ==================================================================== */


namespace {

void draw_check_screen(bt_ui_edit &ed) {
    ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
    ImGui::TextUnformatted("EDIT  \xe2\x80\xa2  check");
    ImGui::PopStyleColor();
    if (ImGui::Button("\xe2\x86\x90 set list")) ed.screen = bt_edit_screen::setlist;
    ImGui::SameLine(0, 24);

    if (ImGui::Button("run the check")) {
        free(ed.issues);
        ed.issues = nullptr;
        ed.nissues = 0;
        bt_err e = bt_setlist_validate(ed.sl, ed.dev, ed.dev->sample_rate,
                                       &ed.issues, &ed.nissues, &ed.stats);
        ed.checked = (e == BT_OK);
        if (!ed.checked) set_status(ed, "check failed: %s", bt_strerror(e));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("decodes every stem once - a long set takes a moment");
    ImGui::Separator();

    if (!ed.checked) {
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Finds everything wrong with the set in one pass: missing stems, "
            "buses this machine cannot route, stems that are silent because "
            "the wrong file was downloaded, sample rates that will be "
            "resampled, nudges longer than the stem they move.");
        return;
    }

    ImGui::Text("%d song%s, %d track%s",
                ed.stats.songs, ed.stats.songs == 1 ? "" : "s",
                ed.stats.tracks, ed.stats.tracks == 1 ? "" : "s");
    ImGui::SameLine(0, 24);
    if (ed.stats.errors) ImGui::TextColored(COL_WARN, "%d error%s",
                                            ed.stats.errors,
                                            ed.stats.errors == 1 ? "" : "s");
    else ImGui::TextColored(COL_OK, "no errors");
    ImGui::SameLine(0, 16);
    if (ed.stats.warnings)
        ImGui::TextColored(COL_AMBER, "%d warning%s", ed.stats.warnings,
                           ed.stats.warnings == 1 ? "" : "s");
    else ImGui::TextDisabled("no warnings");
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("%d note%s - true, and nothing to act on",
                        ed.stats.notes, ed.stats.notes == 1 ? "" : "s");

    ImGui::Spacing();
    ImGui::BeginChild("issues", ImVec2(0, 330), true);
    /* Errors first: they are what stops the show. */
    for (int pass = 0; pass < 3; pass++) {
        bt_issue_level want = pass == 0 ? BT_ISSUE_ERROR
                            : pass == 1 ? BT_ISSUE_WARN : BT_ISSUE_NOTE;
        for (size_t i = 0; i < ed.nissues; i++) {
            const bt_issue &is = ed.issues[i];
            if (is.level != want) continue;

            char where[160] = "set list";
            if (is.song >= 0 && is.song < ed.sl->nsongs) {
                if (is.track >= 0 && is.track < ed.sl->song[is.song].ntracks)
                    std::snprintf(where, sizeof(where), "song %d \xe2\x80\xa2 %s",
                                  is.song + 1,
                                  ed.sl->song[is.song].track[is.track].name);
                else
                    std::snprintf(where, sizeof(where), "song %d \xe2\x80\xa2 %s",
                                  is.song + 1, ed.sl->song[is.song].title);
            }
            char row[420];
            std::snprintf(row, sizeof(row), "%-34.34s  %s##i%zu", where, is.msg, i);

            ImGui::PushStyleColor(ImGuiCol_Text,
                                  want == BT_ISSUE_ERROR ? COL_WARN
                                : want == BT_ISSUE_WARN  ? COL_AMBER : COL_DIM);
            /* Clicking goes to the song it is about - which is why
             * bt_setlist_validate carries indices rather than prose. */
            if (ImGui::Selectable(row) && is.song >= 0) {
                ed.song = is.song;
                ed.track = is.track;
                ed.screen = bt_edit_screen::song;
            }
            ImGui::PopStyleColor();
        }
    }
    if (ed.nissues == 0)
        ImGui::TextColored(COL_OK, "Nothing to report.");
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Text("set length      %.0f min %02.0f s   (longest song %.0f:%02.0f)",
                ed.stats.total_seconds / 60.0,
                ed.stats.total_seconds - 60.0 * (double)(int)(ed.stats.total_seconds / 60.0),
                ed.stats.longest_seconds / 60.0,
                ed.stats.longest_seconds - 60.0 * (double)(int)(ed.stats.longest_seconds / 60.0));
    ImGui::Text("preload peak    %.1f MB   (current + next song)",
                (double)ed.stats.peak_resident_bytes / (1024.0 * 1024.0));
    ImGui::TextDisabled("whole set       %.1f MB   if every song were held at once",
                        (double)ed.stats.all_resident_bytes / (1024.0 * 1024.0));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("EXPORT");
    ImGui::SameLine();
    ImGui::TextDisabled("optional - you do not need this to play a show");
    ImGui::TextWrapped(
        "Writes what you would hear to a .wav file: the stems mixed with the "
        "click, exactly as the set list is configured. Useful for sending the "
        "singer a rehearsal track, checking alignment in another editor, or "
        "proving a problem is in the set list rather than in the room. It "
        "changes nothing about the set.");
    if (ImGui::Button("render this song\xe2\x80\xa6")) {
        if (bt_ui_pick_save_wav(ed.export_path, sizeof(ed.export_path))) {
            ed.export_whole_set = false;
            ed.want_export = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("render the whole set\xe2\x80\xa6")) {
        if (bt_ui_pick_save_wav(ed.export_path, sizeof(ed.export_path))) {
            ed.export_whole_set = true;
            ed.want_export = true;
        }
    }
}


/* ======================================================================
 * Lighting
 *
 * Two kinds of setting on one screen, because this is where somebody sets up
 * lighting and they should not have to know which file each thing lands in.
 * The screen says which is which instead: the port belongs to this laptop,
 * the notes belong to the show and travel with the set list.
 * ==================================================================== */

void draw_lighting_screen(bt_ui_edit &ed) {
    ImGui::PushStyleColor(ImGuiCol_Text, COL_DIM);
    ImGui::TextUnformatted("EDIT  \xe2\x80\xa2  lighting");
    ImGui::PopStyleColor();
    if (ImGui::Button("\xe2\x86\x90 set list")) ed.screen = bt_edit_screen::setlist;
    ImGui::Separator();

    if (!ed.sl) { ImGui::TextDisabled("No set list open."); return; }
    bt_light_cfg &cfg = ed.sl->light;

    /* ---- the port: this machine ---- */
    ImGui::TextUnformatted("PORT");
    ImGui::SameLine();
    ImGui::TextDisabled("belongs to this laptop - stored with the audio "
                        "settings, not with the set list");

    const int32_t n = bt_midi_count();
    if (n == 0) {
        ImGui::TextColored(COL_AMBER, "No MIDI outputs on this machine.");
        ImGui::TextDisabled("Install loopMIDI, create a port, and leave it "
                            "running; QLC+ opens the same port as an input.");
    }

    ImGui::BeginChild("ports", ImVec2(0, 130), true);
    for (int32_t i = 0; i < n; i++) {
        bt_midi_info info;
        if (bt_midi_get(i, &info) != BT_OK) continue;
        const bool sel = ed.midi_port[0] &&
                         std::strstr(info.name, ed.midi_port) != nullptr;
        char row[200];
        std::snprintf(row, sizeof(row), "%s##mp%d", info.name, i);
        if (ImGui::Selectable(row, sel)) {
            std::snprintf(ed.midi_port, sizeof(ed.midi_port), "%s", info.name);
            ed.midi_dirty = true;
        }
    }
    ImGui::EndChild();

    if (ed.midi_open_name && *ed.midi_open_name)
        ImGui::TextColored(COL_OK, "open: %s", ed.midi_open_name);
    else if (ed.midi_why && *ed.midi_why)
        ImGui::TextColored(COL_WARN, "%s", ed.midi_why);
    else if (!ed.midi_port[0])
        ImGui::TextDisabled("no port chosen - nothing is sent");

    ImGui::SameLine();
    if (ImGui::Button("none")) { ed.midi_port[0] = '\0'; ed.midi_dirty = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stop sending lighting from this machine.");

    ImGui::Spacing();
    ImGui::Separator();

    /* ---- the notes: this show ---- */
    ImGui::TextUnformatted("WHAT TO SEND");
    ImGui::SameLine();
    ImGui::TextDisabled("belongs to the show - travels with the set list, so "
                        "the backup laptop lights the same");

    ImGui::SetNextItemWidth(150);
    int ch = cfg.channel;
    if (ImGui::InputInt("channel", &ch, 1, 1)) {
        if (ch >= 0 && ch <= 16) {
            /* Switching lighting on for the first time fills in a transport
             * layout that works, rather than leaving four note numbers at
             * zero for somebody to look up. They are only defaults. */
            if (cfg.channel == 0 && ch > 0 && cfg.next_note == 0) {
                cfg.next_note     = 38;
                cfg.end_note      = 37;
                cfg.prev_note     = 36;
                cfg.blackout_note = 39;
                cfg.velocity      = 127;
            }
            cfg.channel = ch;
            ed.dirty = true;
        }
    }
    ImGui::SameLine();
    if (cfg.channel == 0) ImGui::TextColored(COL_AMBER,
        "0 - this set list has no lighting");
    else ImGui::TextDisabled("1-16, as the desk counts them");

    ImGui::SetNextItemWidth(150);
    int nn = cfg.next_note;
    if (ImGui::InputInt("next cue note", &nn, 1, 1)) {
        if (nn >= 0 && nn <= 127) { cfg.next_note = nn; ed.dirty = true; }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("bind this to \"Next Cue\" in QLC+");

    ImGui::SetNextItemWidth(150);
    int en = cfg.end_note;
    if (ImGui::InputInt("song end note", &en, 1, 1)) {
        if (en >= 0 && en <= 127) { cfg.end_note = en; ed.dirty = true; }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("sent by a cue that names it, at the end of a song");

    ImGui::SetNextItemWidth(150);
    int pn = cfg.prev_note;
    if (ImGui::InputInt("previous cue note", &pn, 1, 1)) {
        if (pn >= 0 && pn <= 127) { cfg.prev_note = pn; ed.dirty = true; }
    }
    ImGui::SameLine();
    if (cfg.prev_note > 0)
        ImGui::TextDisabled("scrubbing back steps the cue list back, "
                            "instead of reloading it");
    else
        ImGui::TextColored(COL_AMBER,
            "0 - scrubbing back reloads the song and races forward to catch up");

    ImGui::SetNextItemWidth(150);
    int bn = cfg.blackout_note;
    if (ImGui::InputInt("blackout note", &bn, 1, 1)) {
        if (bn >= 0 && bn <= 127) { cfg.blackout_note = bn; ed.dirty = true; }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("for a song that ends on the chord, not on a fade");

    ImGui::SetNextItemWidth(150);
    int vel = cfg.velocity;
    if (ImGui::InputInt("velocity", &vel, 1, 10)) {
        if (vel >= 1 && vel <= 127) { cfg.velocity = vel; ed.dirty = true; }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("most desks ignore it");

    ImGui::Spacing();
    ImGui::Separator();

    /* ---- proving it, without a song and without a rig ---- */
    ImGui::TextUnformatted("TEST");
    ImGui::SameLine();
    ImGui::TextDisabled("sends one note now, so the desk can be checked "
                        "before a rehearsal rather than during one");

    ImGui::BeginDisabled(!(ed.midi_open_name && *ed.midi_open_name) || cfg.channel == 0);
    if (ImGui::Button("send the next-cue note")) ed.want_test_note = cfg.next_note;
    ImGui::SameLine();
    if (ImGui::Button("send the song-end note")) ed.want_test_note = cfg.end_note;
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextDisabled(
        "In QLC+: bind the next-cue note to a cue list's Next Cue, and a\n"
        "program change per song to the cue list for that song. Watch what is\n"
        "actually being sent with:   btmidi --listen \"%s\" 60",
        ed.midi_port[0] ? ed.midi_port : "your port");

    if (ed.cues_fired > 0)
        ImGui::TextDisabled("%d cue(s) sent since this song started",
                            ed.cues_fired);
}

} /* namespace */
