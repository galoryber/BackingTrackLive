/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "bt_ui_start.h"
#include "imgui.h"

#include "backtrack/bt_json.h"

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {

const ImVec4 COL_DIM   = ImVec4(0.541f, 0.565f, 0.612f, 1.0f);
const ImVec4 COL_WARN  = ImVec4(1.000f, 0.478f, 0.416f, 1.0f);

bool settings_path(char *out, size_t cap) {
    char base[MAX_PATH];
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, base))) return false;
    char dir[MAX_PATH];
    std::snprintf(dir, sizeof(dir), "%s\\BackingTrackLive", base);
    CreateDirectoryA(dir, nullptr);          /* already existing is success */
    std::snprintf(out, cap, "%s\\settings.json", dir);
    return true;
}

void json_escape(const char *in, char *out, size_t cap) {
    size_t k = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && k + 2 < cap; p++) {
        if (*p == '\\' || *p == '"') { out[k++] = '\\'; out[k++] = (char)*p; }
        else if (*p >= 0x20)         { out[k++] = (char)*p; }
    }
    out[k] = '\0';
}

} /* namespace */

void bt_ui_settings_load(bt_ui_settings &s) {
    char path[MAX_PATH];
    if (!settings_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > (1 << 20)) { fclose(f); return; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';

    bt_json *j = nullptr;
    int line = 0;
    if (bt_json_parse(buf, got, &j, &line) == BT_OK) {
        std::snprintf(s.setlist, sizeof(s.setlist), "%s",
                      bt_json_string(bt_json_get(j, "last_setlist"), ""));
        std::snprintf(s.device, sizeof(s.device), "%s",
                      bt_json_string(bt_json_get(j, "last_device"), ""));
        const bt_json *r = bt_json_get(j, "recent");
        size_t n = bt_json_len(r);
        s.nrecent = 0;
        for (size_t i = 0; i < n && s.nrecent < BT_RECENT_MAX; i++) {
            const char *p = bt_json_string(bt_json_at(r, i), "");
            if (p && *p) std::snprintf(s.recent[s.nrecent++], BT_MAX_PATH, "%s", p);
        }
        bt_json_free(j);
    }
    free(buf);
}

void bt_ui_settings_save(const bt_ui_settings &s) {
    char path[MAX_PATH];
    if (!settings_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;

    char esc[BT_MAX_PATH * 2];
    std::fprintf(f, "{\n  \"version\": 1,\n");
    json_escape(s.setlist, esc, sizeof(esc));
    std::fprintf(f, "  \"last_setlist\": \"%s\",\n", esc);
    json_escape(s.device, esc, sizeof(esc));
    std::fprintf(f, "  \"last_device\": \"%s\",\n", esc);
    std::fprintf(f, "  \"recent\": [");
    for (int32_t i = 0; i < s.nrecent; i++) {
        json_escape(s.recent[i], esc, sizeof(esc));
        std::fprintf(f, "%s\n    \"%s\"", i ? "," : "", esc);
    }
    std::fprintf(f, "%s]\n}\n", s.nrecent ? "\n  " : "");
    fclose(f);
}

void bt_ui_settings_touch(bt_ui_settings &s, const char *setlist, const char *device) {
    if (!setlist || !*setlist) return;
    std::snprintf(s.setlist, sizeof(s.setlist), "%s", setlist);
    if (device && *device) std::snprintf(s.device, sizeof(s.device), "%s", device);

    /* Most recent first, no duplicates. */
    int32_t at = -1;
    for (int32_t i = 0; i < s.nrecent; i++)
        if (_stricmp(s.recent[i], setlist) == 0) { at = i; break; }
    if (at < 0) {
        if (s.nrecent < BT_RECENT_MAX) s.nrecent++;
        at = s.nrecent - 1;
    }
    for (int32_t i = at; i > 0; i--)
        std::snprintf(s.recent[i], BT_MAX_PATH, "%s", s.recent[i - 1]);
    std::snprintf(s.recent[0], BT_MAX_PATH, "%s", setlist);
    bt_ui_settings_save(s);
}

/* ------------------------------------------------------------- pickers */

bool bt_ui_pick_setlist(char *out, size_t cap) {
    char buf[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Set list (setlist.json)\0*.json\0All files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = "Open a set list";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&ofn)) return false;
    std::snprintf(out, cap, "%s", buf);
    return true;
}

bool bt_ui_pick_save_wav(char *out, size_t cap) {
    char buf[MAX_PATH] = {0};
    std::snprintf(buf, sizeof(buf), "render.wav");
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "WAV audio\0*.wav\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = "Render to";
    ofn.lpstrDefExt = "wav";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameA(&ofn)) return false;
    std::snprintf(out, cap, "%s", buf);
    return true;
}

bool bt_ui_pick_folder(const char *title, char *out, size_t cap) {
    BROWSEINFOA bi = {};
    char display[MAX_PATH] = {0};
    bi.pszDisplayName = display;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderA(&bi);
    if (!id) return false;
    char path[MAX_PATH] = {0};
    bool ok = SHGetPathFromIDListA(id, path) != FALSE;
    CoTaskMemFree(id);
    if (!ok) return false;
    std::snprintf(out, cap, "%s", path);
    return true;
}

/* -------------------------------------------------------- start screen */

void bt_ui_start_draw(bt_ui_start &st) {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("start", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    const float pad = io.DisplaySize.x * 0.06f;
    ImGui::SetCursorPos(ImVec2(pad, io.DisplaySize.y * 0.12f));
    ImGui::BeginGroup();

    ImGui::PushFont(ImGui::GetFont(), io.DisplaySize.y * 0.070f);
    ImGui::TextUnformatted("BackingTrackLive");
    ImGui::PopFont();

    ImGui::PushFont(ImGui::GetFont(), io.DisplaySize.y * 0.026f);
    ImGui::TextColored(COL_DIM, "Nothing open yet.");
    ImGui::Spacing();
    ImGui::Spacing();

    const ImVec2 bsz(io.DisplaySize.x * 0.30f, io.DisplaySize.y * 0.062f);

    if (ImGui::Button("Open a set list\xe2\x80\xa6", bsz))
        st.action = bt_start_action::open;
    ImGui::SameLine();
    ImGui::TextColored(COL_DIM, "a folder containing setlist.json and its stems");

    if (ImGui::Button("Create a demo set\xe2\x80\xa6", bsz))
        st.action = bt_start_action::create_demo;
    ImGui::SameLine();
    ImGui::TextColored(COL_DIM, "two songs with generated stems, to try the thing out");

    if (st.settings && st.settings->nrecent > 0) {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextColored(COL_DIM, "RECENT");
        for (int32_t i = 0; i < st.settings->nrecent; i++) {
            char label[BT_MAX_PATH + 16];
            std::snprintf(label, sizeof(label), "%s##r%d", st.settings->recent[i], i);
            if (ImGui::Selectable(label, false, 0, ImVec2(io.DisplaySize.x * 0.62f, 0))) {
                st.recent_index = i;
                st.action = bt_start_action::open_recent;
            }
        }
    }

    if (st.status[0]) {
        ImGui::Spacing();
        ImGui::TextColored(COL_WARN, "%s", st.status);
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextColored(COL_DIM,
        "Set lists live in a folder with their stems, so the whole folder "
        "copies to a backup laptop as one piece.");

    ImGui::PopFont();
    ImGui::EndGroup();
    ImGui::End();
}
