/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "bt_ui_start.h"
#include "imgui.h"

#include "backtrack/bt_json.h"
#include "backtrack/bt_error.h"

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

bool bt_ui_default_setlist_root(char *out, size_t cap) {
    char docs[MAX_PATH];
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs))) return false;
    std::snprintf(out, cap, "%s\\BackingTrackLive", docs);
    CreateDirectoryA(out, nullptr);
    return true;
}

bool bt_ui_pick_setlist(char *out, size_t cap) {
    char buf[MAX_PATH] = {0};
    char root[MAX_PATH] = {0};
    const bool have_root = bt_ui_default_setlist_root(root, sizeof(root));
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Set list (setlist.json)\0*.json\0All files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = "Open a set list";
    ofn.lpstrInitialDir = have_root ? root : nullptr;
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

static int CALLBACK browse_init(HWND hwnd, UINT msg, LPARAM, LPARAM data) {
    if (msg == BFFM_INITIALIZED && data)
        SendMessageA(hwnd, BFFM_SETSELECTIONA, TRUE, data);
    return 0;
}

bool bt_ui_pick_folder(const char *title, char *out, size_t cap) {
    static char root[MAX_PATH];
    const bool have_root = bt_ui_default_setlist_root(root, sizeof(root));
    BROWSEINFOA bi = {};
    if (have_root) {
        bi.lpfn   = browse_init;
        bi.lParam = (LPARAM)root;
    }
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

int32_t bt_ui_scan_setlists(bt_ui_found *out, int32_t cap) {
    if (!out || cap <= 0) return 0;
    char root[MAX_PATH];
    if (!bt_ui_default_setlist_root(root, sizeof(root))) return 0;

    char glob[MAX_PATH];
    std::snprintf(glob, sizeof(glob), "%s\\*", root);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    int32_t n = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;

        char sl[MAX_PATH];
        std::snprintf(sl, sizeof(sl), "%s\\%s\\setlist.json", root, fd.cFileName);
        if (GetFileAttributesA(sl) == INVALID_FILE_ATTRIBUTES) continue;

        std::snprintf(out[n].name, BT_MAX_NAME, "%s", fd.cFileName);
        std::snprintf(out[n].path, BT_MAX_PATH, "%s", sl);
        if (++n >= cap) break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

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

    /* The set lists you already have, first - this is the common case and it
     * used to be the one thing the screen did not offer. */
    if (st.nfound > 0) {
        ImGui::TextColored(COL_DIM, "YOUR SET LISTS");
        ImGui::Spacing();
        for (int32_t i = 0; i < st.nfound; i++) {
            char label[BT_MAX_NAME + 16];
            std::snprintf(label, sizeof(label), "%s##f%d", st.found[i].name, i);
            if (ImGui::Selectable(label, false, 0,
                                  ImVec2(io.DisplaySize.x * 0.42f, 0))) {
                st.found_index = i;
                st.action = bt_start_action::open_found;
            }
        }
        ImGui::Spacing();
        ImGui::Spacing();
    }

    /* Naming happens before anything is written, so a name you change your
     * mind about costs nothing. */
    if (st.naming) {
        ImGui::TextColored(COL_DIM, "NAME THE NEW SET LIST");
        ImGui::SetNextItemWidth(io.DisplaySize.x * 0.32f);
        /* Once, on the frame the prompt appears. Calling this every frame
         * forces ImGui's active item back to this box on every frame, and a
         * button needs to hold the active item from press to release - so
         * every button on the screen stopped working while the prompt was up,
         * including Cancel. */
        if (st.focus_name) {
            ImGui::SetKeyboardFocusHere();
            st.focus_name = false;
        }
        const bool entered = ImGui::InputText("##newname", st.new_name,
                                              sizeof(st.new_name),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        const bool named = st.new_name[0] != '\0';
        ImGui::BeginDisabled(!named);
        const bool pressed = ImGui::Button("Create");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { st.naming = false; st.new_name[0] = '\0'; }
        if (named && (entered || pressed)) {
            st.naming = false;
            st.action = bt_start_action::create_new;
        }
        ImGui::TextColored(COL_DIM,
            "It gets a folder of its own, so set lists cannot overwrite "
            "each other.");
        ImGui::Spacing();
        ImGui::Spacing();
    } else {
        if (ImGui::Button("New set list\xe2\x80\xa6", bsz)) {
            st.naming = true;
            st.focus_name = true;
            st.new_name[0] = '\0';
        }
        ImGui::SameLine();
        ImGui::TextColored(COL_DIM, "a new set in a folder of its own");
    }

    ImGui::Spacing();
    if (ImGui::Button("Open a set list\xe2\x80\xa6", bsz))
        st.action = bt_start_action::open;
    ImGui::SameLine();
    ImGui::TextColored(COL_DIM, "one kept somewhere else - a USB stick, say");

    ImGui::Spacing();
    if (ImGui::Button("Create a demo set\xe2\x80\xa6", bsz))
        st.action = bt_start_action::create_demo;
    ImGui::SameLine();
    ImGui::TextColored(COL_DIM, "two songs with generated stems, to try the thing out");

    if (st.settings && st.settings->nrecent > 0) {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextColored(COL_DIM, "RECENT");
        ImGui::Spacing();
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
    char root[BT_MAX_PATH];
    if (bt_ui_default_setlist_root(root, sizeof(root)))
        ImGui::TextColored(COL_DIM,
            "Set lists live in a folder with their stems, so the whole folder "
            "copies to a backup laptop as one piece.\nThey are kept in %s.", root);
    else
        ImGui::TextColored(COL_DIM,
            "Set lists live in a folder with their stems, so the whole folder "
            "copies to a backup laptop as one piece.");

    ImGui::PopFont();
    ImGui::EndGroup();
    ImGui::End();
}

bool bt_ui_machine_device_path(char *out, size_t cap) {
    char base[MAX_PATH];
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, base))) return false;
    char dir[MAX_PATH];
    std::snprintf(dir, sizeof(dir), "%s\\BackingTrackLive", base);
    CreateDirectoryA(dir, nullptr);
    std::snprintf(out, cap, "%s\\device.json", dir);
    return true;
}

bt_err bt_ui_new_setlist(const char *dir, const char *name) {
    if (!dir || !*dir) return BT_ERR_RANGE;

    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%s\\setlist.json", dir);

    /* Never over an existing set list. This used to open the file for writing
     * and truncate whatever was there, and the folder picker opened on the
     * set lists folder itself - so making a second set list without first
     * navigating somewhere destroyed the first one, silently and with no way
     * back. Set lists now get a folder of their own, and this refuses
     * regardless. */
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return BT_ERR_EXISTS;

    char tracks[MAX_PATH];
    std::snprintf(tracks, sizeof(tracks), "%s\\tracks", dir);
    CreateDirectoryA(dir, nullptr);
    CreateDirectoryA(tracks, nullptr);

    /* One song, with a click and nothing else. An empty set list is a screen
     * with nothing to press; one song is something you can immediately play
     * and then hang stems on. */
    char esc[256];
    json_escape(name && *name ? name : "New set", esc, sizeof(esc));

    FILE *f = fopen(path, "wb");
    if (!f) return BT_ERR_IO;
    std::fprintf(f,
"{\n"
"  \"version\": 1,\n"
"  \"name\": \"%s\",\n"
"  \"songs\": [\n"
"    {\n"
"      \"title\": \"New song\",\n"
"      \"artist\": \"\",\n"
"      \"tempo\": { \"bpm\": 120.0, \"sig\": [4, 4], \"downbeat_ms\": 0 },\n"
"      \"count_in_bars\": 2,\n"
"      \"on_end\": \"stop\",\n"
"      \"tracks\": [\n"
"        { \"name\": \"Click\", \"type\": \"click\", \"bus\": \"inear\" }\n"
"      ]\n"
"    }\n"
"  ]\n"
"}\n", esc);
    bool ok = (std::ferror(f) == 0);
    fclose(f);
    return ok ? BT_OK : BT_ERR_IO;
}
