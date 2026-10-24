/* Tomodachi Life: Living the Dream - PC launcher.
 *
 * A small SDL3 + Dear ImGui program that lives beside the game in build\work (TomodachiLauncher.exe):
 *   - Play: checks the game files, starts the game the way scripts\play.ps1 does (a copy of the exe, the same
 *     environment, the log in build\work\play_<date>.log), hides itself while the game runs and reports how it ended;
 *   - Display / Audio: the options of build\config\graphics.ini that this port reads (gfx_options.c), edited in place
 *     (comments and unknown keys are kept);
 *   - Controls: the controllers SDL sees, and build\config\controllers.ini;
 *   - Saves: backups of build\userdata into build\saves\, restore (the current save is backed up first), open folders;
 *   - Tools: shader cache, logs, folders.
 * Built by scripts\build_launcher.sh with the toolchain of the port (clang, SDL3, imgui from ..\ssbu-recomp\deps). */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#ifndef LAUNCHER_VERSION
#define LAUNCHER_VERSION "dev"
#endif

/* ------------------------------------------------------------------ paths */
static char g_work[MAX_PATH], g_build[MAX_PATH], g_repo[MAX_PATH];
static char g_ini[MAX_PATH], g_pads_ini[MAX_PATH], g_userdata[MAX_PATH], g_saves[MAX_PATH], g_cache[MAX_PATH];
static char g_exe[MAX_PATH], g_play_exe[MAX_PATH], g_romfs[MAX_PATH], g_elf[MAX_PATH];

static void path_join(char *out, const char *a, const char *b) { snprintf(out, MAX_PATH, "%s\\%s", a, b); }
static int file_exists(const char *p) { DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static int dir_exists(const char *p) { DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static void parent(char *p) { char *s = strrchr(p, '\\'); if (s) *s = 0; }

static void paths_init(void) {
    GetModuleFileNameA(NULL, g_work, MAX_PATH);
    parent(g_work);                                          /* build\work */
    strcpy(g_build, g_work); parent(g_build);                /* build */
    strcpy(g_repo, g_build); parent(g_repo);
    path_join(g_ini, g_build, "config\\graphics.ini");
    path_join(g_pads_ini, g_build, "config\\controllers.ini");
    path_join(g_userdata, g_build, "userdata");
    path_join(g_saves, g_build, "saves");
    path_join(g_cache, g_build, "cache\\shaders");
    path_join(g_exe, g_work, "tomodachi_native.exe");
    path_join(g_play_exe, g_work, "tomodachi_play.exe");
    path_join(g_romfs, g_build, "dump\\romfs");
    path_join(g_elf, g_build, "dump\\exefs\\main.elf");
}

static void open_in_explorer(const char *p) { ShellExecuteA(NULL, "open", p, NULL, NULL, SW_SHOWNORMAL); }

/* ------------------------------------------------------------------ graphics.ini, edited in place */
#define INI_MAX 400
static char *g_lines[INI_MAX];
static int g_nlines, g_ini_dirty;

static void ini_load(void) {
    for (int i = 0; i < g_nlines; i++) free(g_lines[i]);
    g_nlines = 0;
    FILE *f = fopen(g_ini, "rb");
    if (!f) return;
    char buf[1024];
    while (g_nlines < INI_MAX && fgets(buf, sizeof buf, f)) {
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        g_lines[g_nlines++] = _strdup(buf);
    }
    fclose(f);
}
static int ini_find(const char *key) {                     /* the line "key = value" (not a comment) */
    size_t k = strlen(key);
    for (int i = 0; i < g_nlines; i++) {
        const char *s = g_lines[i];
        while (*s == ' ' || *s == '\t') s++;
        if (*s == ';' || *s == '#') continue;
        if (!strncmp(s, key, k) && (s[k] == ' ' || s[k] == '=' || s[k] == '\t')) return i;
    }
    return -1;
}
static const char *ini_get(const char *key, const char *def) {
    static char out[8][256]; static int slot;
    int i = ini_find(key);
    if (i < 0) return def;
    const char *eq = strchr(g_lines[i], '=');
    if (!eq) return def;
    eq++;
    while (*eq == ' ' || *eq == '\t') eq++;
    char *o = out[slot = (slot + 1) & 7];
    snprintf(o, 256, "%s", eq);
    char *c = strpbrk(o, ";#"); if (c) *c = 0;              /* trailing comment */
    size_t n = strlen(o); while (n && (o[n - 1] == ' ' || o[n - 1] == '\t')) o[--n] = 0;
    return o;
}
static void ini_set(const char *key, const char *val) {
    char line[512];
    snprintf(line, sizeof line, "%s = %s", key, val);
    int i = ini_find(key);
    if (i >= 0) { if (!strcmp(g_lines[i], line)) return; free(g_lines[i]); g_lines[i] = _strdup(line); }
    else if (g_nlines < INI_MAX) g_lines[g_nlines++] = _strdup(line);
    g_ini_dirty = 1;
}
static int ini_save(void) {
    FILE *f = fopen(g_ini, "wb");
    if (!f) return 0;
    for (int i = 0; i < g_nlines; i++) fprintf(f, "%s\r\n", g_lines[i]);
    fclose(f);
    g_ini_dirty = 0;
    return 1;
}

/* ------------------------------------------------------------------ status line (toast) */
static char g_toast[256];
static double g_toast_until;
static void toast(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(g_toast, sizeof g_toast, fmt, ap); va_end(ap);
    g_toast_until = ImGui::GetTime() + 4.0;
}

/* ------------------------------------------------------------------ the game process */
static PROCESS_INFORMATION g_game;
static int g_running;
static double g_started;
static char g_log[MAX_PATH], g_last_result[256];
static int g_last_ok = 1;

static void game_start(SDL_Window *win) {
    if (g_running) return;
    if (!file_exists(g_exe)) { toast("The game is not built yet: tomodachi_native.exe is missing."); return; }
    if (!file_exists(g_elf) || !dir_exists(g_romfs)) { toast("The game files are missing (build\\dump\\exefs and romfs)."); return; }
    if (g_ini_dirty) ini_save();
    if (!CopyFileA(g_exe, g_play_exe, FALSE) && !file_exists(g_play_exe)) { toast("Could not prepare the game (copy failed)."); return; }
    /* the environment of scripts\play.ps1 */
    SetEnvironmentVariableA("SSBU_ROMFS", g_romfs);
    SetEnvironmentVariableA("SSBU_ERROR_DIALOG", "0");
    if (!GetEnvironmentVariableA("SSBU_RENDERER", NULL, 0)) SetEnvironmentVariableA("SSBU_RENDERER", "gl");
    SetEnvironmentVariableA("SSBU_OS_CONSOLE_CORES", "1");
    SetEnvironmentVariableA("SSBU_KEYBOARD_PLAYER", "1");
    SetEnvironmentVariableA("SSBU_SHOW", "1");
    time_t now = time(NULL);
    struct tm lt; localtime_s(&lt, &now);
    char name[64], rec[MAX_PATH]; strftime(name, sizeof name, "play_%Y%m%d_%H%M%S.rec", &lt);
    path_join(rec, g_work, name);
    SetEnvironmentVariableA("SSBU_REC", rec);                 /* player 1's input, for replaying a bug as a test */
    strcpy(strrchr(name, '.'), ".log");
    path_join(g_log, g_work, name);
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE log = CreateFileA(g_log, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (log != INVALID_HANDLE_VALUE) { si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = log; si.hStdError = log; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE); }
    char cmd[MAX_PATH * 3];
    snprintf(cmd, sizeof cmd, "\"%s\" \"%s\"", g_play_exe, g_elf);
    memset(&g_game, 0, sizeof g_game);
    BOOL ok = CreateProcessA(g_play_exe, cmd, NULL, NULL, log != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW, NULL, g_work, &si, &g_game);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!ok) { toast("Could not start the game (error %lu).", GetLastError()); return; }
    g_running = 1; g_started = ImGui::GetTime();
    SDL_HideWindow(win);
}
static void game_poll(SDL_Window *win) {
    if (!g_running) return;
    if (WaitForSingleObject(g_game.hProcess, 0) != WAIT_OBJECT_0) return;
    DWORD code = 0;
    GetExitCodeProcess(g_game.hProcess, &code);
    CloseHandle(g_game.hProcess); CloseHandle(g_game.hThread);
    g_running = 0;
    double mins = (ImGui::GetTime() - g_started) / 60.0;
    g_last_ok = code == 0;
    if (code == 0) snprintf(g_last_result, sizeof g_last_result, "Last session: %.0f min, ended normally.", mins);
    else snprintf(g_last_result, sizeof g_last_result, "Last session: %.0f min, ended with code 0x%08lX. The log has the details.", mins, (unsigned long)code);
    SDL_ShowWindow(win); SDL_RaiseWindow(win);
}

/* ------------------------------------------------------------------ saves */
#define MAX_BACKUPS 64
static char g_backups[MAX_BACKUPS][MAX_PATH];
static int g_nbackups;
static char g_save_info[256];

static long long dir_size(const char *dir, FILETIME *newest) {
    char pat[MAX_PATH]; path_join(pat, dir, "*");
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    long long total = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char p[MAX_PATH]; path_join(p, dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) total += dir_size(p, newest);
        else {
            total += ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if (newest && CompareFileTime(&fd.ftLastWriteTime, newest) > 0) *newest = fd.ftLastWriteTime;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return total;
}
static void saves_scan(void) {
    FILETIME newest = {0, 0};
    long long sz = dir_exists(g_userdata) ? dir_size(g_userdata, &newest) : 0;
    if (!sz) snprintf(g_save_info, sizeof g_save_info, "No save yet: the game starts a new island.");
    else {
        SYSTEMTIME st, lt; FileTimeToSystemTime(&newest, &st); SystemTimeToTzSpecificLocalTime(NULL, &st, &lt);
        snprintf(g_save_info, sizeof g_save_info, "Save found (%.1f MB), last written %04d-%02d-%02d %02d:%02d.", sz / 1048576.0, lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute);
    }
    g_nbackups = 0;
    char pat[MAX_PATH]; path_join(pat, g_saves, "backup_*");
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && g_nbackups < MAX_BACKUPS) snprintf(g_backups[g_nbackups++], MAX_PATH, "%s", fd.cFileName);
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    for (int i = 0; i < g_nbackups; i++)                     /* newest first (the names sort by date) */
        for (int j = i + 1; j < g_nbackups; j++)
            if (strcmp(g_backups[j], g_backups[i]) > 0) { char t[MAX_PATH]; strcpy(t, g_backups[i]); strcpy(g_backups[i], g_backups[j]); strcpy(g_backups[j], t); }
}
static int shell_copy(const char *from_dir, const char *to_dir) {      /* the contents of from_dir into to_dir */
    char from[MAX_PATH + 4], to[MAX_PATH + 2];
    memset(from, 0, sizeof from); memset(to, 0, sizeof to);
    snprintf(from, MAX_PATH, "%s\\*", from_dir);
    snprintf(to, MAX_PATH, "%s", to_dir);
    CreateDirectoryA(to_dir, NULL);
    SHFILEOPSTRUCTA op; memset(&op, 0, sizeof op);
    op.wFunc = FO_COPY; op.pFrom = from; op.pTo = to;
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT;
    return SHFileOperationA(&op) == 0 && !op.fAnyOperationsAborted;
}
static int shell_recycle(const char *path) {
    char from[MAX_PATH + 2]; memset(from, 0, sizeof from); snprintf(from, MAX_PATH, "%s", path);
    SHFILEOPSTRUCTA op; memset(&op, 0, sizeof op);
    op.wFunc = FO_DELETE; op.pFrom = from;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    return SHFileOperationA(&op) == 0;
}
static int backup_now(const char *tag) {
    if (!dir_exists(g_userdata)) { toast("There is no save to back up yet."); return 0; }
    CreateDirectoryA(g_saves, NULL);
    time_t now = time(NULL); struct tm lt; localtime_s(&lt, &now);
    char name[64], dst[MAX_PATH];
    strftime(name, sizeof name, "backup_%Y%m%d_%H%M%S", &lt);
    if (tag && tag[0]) { size_t n = strlen(name); snprintf(name + n, sizeof name - n, "_%s", tag); }
    path_join(dst, g_saves, name);
    int ok = shell_copy(g_userdata, dst);
    saves_scan();
    return ok;
}

/* ------------------------------------------------------------------ look */
static ImFont *g_font_body, *g_font_head, *g_font_title, *g_font_small;
static const ImU32 C_INK = IM_COL32(52, 38, 20, 255), C_SOFT = IM_COL32(120, 92, 52, 255), C_CARD = IM_COL32(255, 248, 228, 250);
static const ImU32 C_ORANGE = IM_COL32(255, 122, 26, 255), C_ORANGE_HI = IM_COL32(255, 150, 60, 255), C_CREAM = IM_COL32(255, 243, 214, 255);

static void style_init(void) {
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 14; s.GrabRounding = 14; s.PopupRounding = 12; s.ChildRounding = 18; s.ScrollbarRounding = 12;
    s.FramePadding = ImVec2(14, 8); s.ItemSpacing = ImVec2(12, 12); s.WindowPadding = ImVec2(0, 0); s.WindowBorderSize = 0; s.ChildBorderSize = 0;
    s.FrameBorderSize = 0; s.PopupBorderSize = 0; s.GrabMinSize = 22; s.ScrollbarSize = 14;
    ImVec4 *c = s.Colors;
    c[ImGuiCol_Text] = ImColor(52, 38, 20); c[ImGuiCol_TextDisabled] = ImColor(150, 125, 90);
    c[ImGuiCol_WindowBg] = ImColor(0, 0, 0, 0); c[ImGuiCol_ChildBg] = ImColor(0, 0, 0, 0); c[ImGuiCol_PopupBg] = ImColor(255, 248, 228);
    c[ImGuiCol_FrameBg] = ImColor(255, 230, 170); c[ImGuiCol_FrameBgHovered] = ImColor(255, 218, 140); c[ImGuiCol_FrameBgActive] = ImColor(255, 205, 110);
    c[ImGuiCol_Button] = ImColor(255, 214, 120); c[ImGuiCol_ButtonHovered] = ImColor(255, 190, 80); c[ImGuiCol_ButtonActive] = ImColor(255, 165, 50);
    c[ImGuiCol_Header] = ImColor(255, 214, 120); c[ImGuiCol_HeaderHovered] = ImColor(255, 190, 80); c[ImGuiCol_HeaderActive] = ImColor(255, 165, 50);
    c[ImGuiCol_CheckMark] = ImColor(255, 122, 26); c[ImGuiCol_SliderGrab] = ImColor(255, 122, 26); c[ImGuiCol_SliderGrabActive] = ImColor(230, 100, 10);
    c[ImGuiCol_Separator] = ImColor(235, 205, 150); c[ImGuiCol_ScrollbarBg] = ImColor(0, 0, 0, 0); c[ImGuiCol_ScrollbarGrab] = ImColor(240, 200, 120);
    c[ImGuiCol_NavHighlight] = ImColor(255, 122, 26);
}
static void fonts_init(void) {
    ImGuiIO &io = ImGui::GetIO();
    char win[MAX_PATH], semi[MAX_PATH], bold[MAX_PATH];
    GetWindowsDirectoryA(win, MAX_PATH);
    snprintf(semi, MAX_PATH, "%s\\Fonts\\seguisb.ttf", win);
    snprintf(bold, MAX_PATH, "%s\\Fonts\\segoeuib.ttf", win);
    ImFontConfig cfg; cfg.OversampleH = 3; cfg.OversampleV = 2;
    if (file_exists(semi)) {
        g_font_body = io.Fonts->AddFontFromFileTTF(semi, 19.0f, &cfg);
        g_font_small = io.Fonts->AddFontFromFileTTF(semi, 15.0f, &cfg);
    }
    if (file_exists(bold)) {
        g_font_head = io.Fonts->AddFontFromFileTTF(bold, 28.0f, &cfg);
        g_font_title = io.Fonts->AddFontFromFileTTF(bold, 44.0f, &cfg);
    }
    if (!g_font_body) g_font_body = io.Fonts->AddFontDefault();
    if (!g_font_small) g_font_small = g_font_body;
    if (!g_font_head) g_font_head = g_font_body;
    if (!g_font_title) g_font_title = g_font_head;
    io.FontDefault = g_font_body;
}

/* the moving background: the game's own kind of doodles (rings, dots, squiggles, triangles) on warm yellow */
static void draw_background(ImDrawList *dl, ImVec2 size, float t) {
    dl->AddRectFilledMultiColor(ImVec2(0, 0), size, IM_COL32(255, 220, 80, 255), IM_COL32(255, 205, 64, 255), IM_COL32(255, 178, 46, 255), IM_COL32(255, 190, 52, 255));
    const ImU32 doodle = IM_COL32(255, 238, 150, 120);
    for (int i = 0; i < 26; i++) {
        float sx = fmodf(i * 0.6180339f * size.x + t * (8.0f + (i % 5) * 3.0f), size.x + 120) - 60;
        float sy = fmodf(i * 0.3819660f * size.y * 2.3f + t * (5.0f + (i % 3) * 2.0f), size.y + 120) - 60;
        float r = 10.0f + (i % 4) * 7.0f, a = t * 0.3f + i;
        switch (i % 4) {
        case 0: dl->AddCircle(ImVec2(sx, sy), r, doodle, 32, 5.0f); break;
        case 1: dl->AddCircleFilled(ImVec2(sx, sy), r * 0.45f, doodle); break;
        case 2: {
            ImVec2 p[12];
            for (int k = 0; k < 12; k++) p[k] = ImVec2(sx + (k - 6) * 6.0f * cosf(a) - sinf(k * 0.9f) * 7.0f * sinf(a), sy + (k - 6) * 6.0f * sinf(a) + sinf(k * 0.9f) * 7.0f * cosf(a));
            dl->AddPolyline(p, 12, doodle, 0, 5.0f);
            break;
        }
        default: {
            ImVec2 a1(sx + r * cosf(a), sy + r * sinf(a)), a2(sx + r * cosf(a + 2.094f), sy + r * sinf(a + 2.094f)), a3(sx + r * cosf(a + 4.189f), sy + r * sinf(a + 4.189f));
            dl->AddTriangle(a1, a2, a3, doodle, 5.0f);
        }
        }
    }
}

/* a big rounded button drawn by hand: returns true when clicked */
static bool fancy_button(const char *id, const char *label, ImVec2 size, ImU32 col, ImU32 col_hi, ImFont *font, bool enabled = true) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("b", size) && enabled;
    bool hov = ImGui::IsItemHovered() && enabled, held = ImGui::IsItemActive() && enabled;
    ImGui::PopID();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float r = size.y * 0.5f, lift = held ? 0.0f : hov ? -2.0f : 0.0f;
    ImU32 c = !enabled ? IM_COL32(220, 200, 170, 255) : hov ? col_hi : col;
    dl->AddRectFilled(ImVec2(pos.x, pos.y + 6), ImVec2(pos.x + size.x, pos.y + size.y + 6), IM_COL32(150, 70, 0, enabled ? 70 : 30), r);
    dl->AddRectFilled(ImVec2(pos.x, pos.y + lift), ImVec2(pos.x + size.x, pos.y + size.y + lift), c, r);
    dl->AddRectFilled(ImVec2(pos.x + r * 0.6f, pos.y + lift + 4), ImVec2(pos.x + size.x - r * 0.6f, pos.y + lift + size.y * 0.32f), IM_COL32(255, 255, 255, enabled ? 28 : 12), size.y * 0.16f);
    ImGui::PushFont(font);
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + lift + (size.y - ts.y) * 0.5f), IM_COL32(255, 255, 255, enabled ? 255 : 200), label);
    ImGui::PopFont();
    return clicked;
}

static void card_begin(const char *id, ImVec2 size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x + 4, p.y + 8), ImVec2(p.x + size.x + 4, p.y + size.y + 8), IM_COL32(160, 90, 0, 50), 26);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), C_CARD, 26);
    ImGui::BeginChild(id, size, 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPos(ImVec2(30, 26));
    ImGui::BeginGroup();
}
static void card_end(void) { ImGui::EndGroup(); ImGui::EndChild(); }
static void heading(const char *text, const char *sub) {
    ImGui::PushFont(g_font_head); ImGui::TextUnformatted(text); ImGui::PopFont();
    if (sub) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(C_SOFT)); ImGui::PushFont(g_font_small); ImGui::TextWrapped("%s", sub); ImGui::PopFont(); ImGui::PopStyleColor(); }
    ImGui::Dummy(ImVec2(0, 4));
}
static void label_col(const char *text, float w = 230) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(text); ImGui::SameLine(w); }
static void hint(const char *text) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(C_SOFT)); ImGui::PushFont(g_font_small); ImGui::TextWrapped("%s", text); ImGui::PopFont(); ImGui::PopStyleColor(); }
static void status_chip(bool ok, const char *text) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight() + 10;
    ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddRectFilled(p, ImVec2(p.x + ts.x + 46, p.y + h), ok ? IM_COL32(210, 245, 190, 255) : IM_COL32(255, 210, 190, 255), h * 0.5f);
    dl->AddCircleFilled(ImVec2(p.x + 18, p.y + h * 0.5f), 7, ok ? IM_COL32(70, 170, 60, 255) : IM_COL32(220, 80, 40, 255));
    dl->AddText(ImVec2(p.x + 34, p.y + 5), C_INK, text);
    ImGui::Dummy(ImVec2(ts.x + 46, h));
}

/* combo over a list of (value, label) pairs stored in the ini */
static void ini_combo(const char *key, const char *def, const char *const *values, const char *const *labels, int n, float width = 300) {
    const char *cur = ini_get(key, def);
    int sel = 0;
    for (int i = 0; i < n; i++) if (!_stricmp(cur, values[i])) sel = i;
    ImGui::SetNextItemWidth(width);
    ImGui::PushID(key);
    if (ImGui::BeginCombo("##c", labels[sel])) {
        for (int i = 0; i < n; i++) if (ImGui::Selectable(labels[i], i == sel)) ini_set(key, values[i]);
        ImGui::EndCombo();
    }
    ImGui::PopID();
}
static void ini_toggle(const char *key, const char *def) {
    bool on = !_stricmp(ini_get(key, def), "on");
    ImGui::PushID(key);
    if (ImGui::Checkbox("##t", &on)) ini_set(key, on ? "on" : "off");
    ImGui::PopID();
}

/* ------------------------------------------------------------------ pages */
enum { P_PLAY, P_DISPLAY, P_AUDIO, P_CONTROLS, P_SAVES, P_TOOLS, P_ABOUT, P_COUNT };
static const char *const k_pages[P_COUNT] = {"Play", "Display", "Audio", "Controls", "Saves", "Tools", "About"};

static void page_play(SDL_Window *win, ImVec2 avail) {
    card_begin("play", avail);
    ImGui::PushFont(g_font_title); ImGui::TextUnformatted("Welcome back!"); ImGui::PopFont();
    hint("Your island is waiting. The game opens in its own window; this launcher comes back when you close it.");
    ImGui::Dummy(ImVec2(0, 10));
    bool exe_ok = file_exists(g_exe), files_ok = file_exists(g_elf) && dir_exists(g_romfs);
    status_chip(exe_ok, exe_ok ? "Game built and ready" : "Game not built (run scripts\\dev_build.sh)");
    status_chip(files_ok, files_ok ? "Game files found (build\\dump)" : "Game files missing: build\\dump\\exefs and romfs");
    status_chip(true, g_save_info);
    ImGui::Dummy(ImVec2(0, 16));
    float t = (float)ImGui::GetTime(), pulse = 0.5f + 0.5f * sinf(t * 2.5f);
    ImVec2 bp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(bp.x - 6 * pulse, bp.y - 6 * pulse), ImVec2(bp.x + 320 + 6 * pulse, bp.y + 84 + 6 * pulse), IM_COL32(255, 160, 40, (int)(60 * pulse)), 48);
    if (fancy_button("play", g_running ? "Playing..." : "Play", ImVec2(320, 84), C_ORANGE, C_ORANGE_HI, g_font_title, exe_ok && files_ok && !g_running)) game_start(win);
    ImGui::Dummy(ImVec2(0, 14));
    if (g_last_result[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, g_last_ok ? ImVec4(0.25f, 0.45f, 0.15f, 1) : ImVec4(0.7f, 0.2f, 0.1f, 1));
        ImGui::TextWrapped("%s", g_last_result);
        ImGui::PopStyleColor();
        if (g_log[0] && ImGui::Button("Open the log")) ShellExecuteA(NULL, "open", g_log, NULL, NULL, SW_SHOWNORMAL);
    }
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushFont(g_font_small);
    label_col("Window", 150);
    static const char *const wv[] = {"windowed", "borderless", "fullscreen"}, *const wl[] = {"Window", "Borderless", "Fullscreen"};
    ini_combo("window", "windowed", wv, wl, 3, 220);
    hint("F11 switches between a window and fullscreen while you play. F1 opens the in-game settings.");
    ImGui::PopFont();
    card_end();
}

static void page_display(ImVec2 avail) {
    card_begin("display", avail);
    heading("Display", "How the game's picture reaches your screen. Changes apply the next time the game starts.");
    static const char *const wv[] = {"windowed", "borderless", "fullscreen"}, *const wl[] = {"Window", "Borderless window", "Fullscreen (borderless, on top)"};
    label_col("Window mode"); ini_combo("window", "windowed", wv, wl, 3);
    label_col("Window size");
    {
        static const int sizes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
        char cur[64]; snprintf(cur, sizeof cur, "%s x %s", ini_get("width", "1280"), ini_get("height", "720"));
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo("##size", cur)) {
            for (int i = 0; i < 5; i++) {
                char l[32]; snprintf(l, sizeof l, "%d x %d", sizes[i][0], sizes[i][1]);
                if (ImGui::Selectable(l, !strcmp(l, cur))) { char w[16], h[16]; snprintf(w, sizeof w, "%d", sizes[i][0]); snprintf(h, sizeof h, "%d", sizes[i][1]); ini_set("width", w); ini_set("height", h); }
            }
            ImGui::EndCombo();
        }
    }
    hint("In a window the picture keeps its 16:9 shape; other shapes get bars.");
    static const char *const rv[] = {"1.0", "1.5", "2.0", "3.0"}, *const rl[] = {"1080p (the console's)", "1620p", "2160p (4K)", "3240p"};
    label_col("Internal resolution"); ini_combo("resolution_scale", "1.0", rv, rl, 4);
    hint("The game's 3D picture is drawn at this size and fitted to the window. 4K needs a recent graphics card.");
    static const char *const vv[] = {"auto", "display", "clock", "off"}, *const vl[] = {"Automatic", "Follow the display (smoothest)", "Own 60 Hz clock", "Off (lowest latency, may tear)"};
    label_col("Vertical sync"); ini_combo("vsync", "auto", vv, vl, 4);
    static const char *const lv[] = {"normal", "low", "lowest"}, *const ll[] = {"Normal", "Low", "Lowest"};
    label_col("Input latency"); ini_combo("latency", "normal", lv, ll, 3);
    label_col("Frame-time overlay"); ini_toggle("frame_time_overlay", "off");
    label_col("Statistics overlay"); ini_toggle("stats_overlay", "off");
    label_col("In-game settings menu (F1)"); ini_toggle("menu", "on");
    ImGui::Dummy(ImVec2(0, 6));
    hint("Renderer: OpenGL (this port's renderer for Tomodachi Life).");
    card_end();
}

static void page_audio(ImVec2 avail) {
    card_begin("audio", avail);
    heading("Audio", "Sound comes from the game's own mix, with its reverbs and delays.");
    const char *v = ini_get("volume", "auto");
    bool automatic = !_stricmp(v, "auto");
    int vol = automatic ? 100 : atoi(v);
    label_col("Use the system volume");
    if (ImGui::Checkbox("##auto", &automatic)) ini_set("volume", automatic ? "auto" : "100");
    if (!automatic) {
        label_col("Volume");
        ImGui::SetNextItemWidth(300);
        if (ImGui::SliderInt("##vol", &vol, 0, 100, "%d %%")) { char b[16]; snprintf(b, sizeof b, "%d", vol); ini_set("volume", b); }
    }
    static const char *const lv[] = {"auto", "en", "fr", "de", "es", "it", "nl", "ja"}, *const ll[] = {"Automatic", "English", "Francais", "Deutsch", "Espanol", "Italiano", "Nederlands", "Japanese"};
    label_col("Language"); ini_combo("language", "auto", lv, ll, 8);
    hint("Automatic follows the Windows display language.");
    card_end();
}

static void page_controls(ImVec2 avail) {
    card_begin("controls", avail);
    heading("Controls", "Plug in a controller at any time. The keyboard always works for player 1.");
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (n == 0) status_chip(false, "No controller connected: the keyboard is used");
    for (int i = 0; i < n; i++) { const char *name = SDL_GetGamepadNameForID(ids[i]); status_chip(true, name ? name : "Controller"); }
    if (ids) SDL_free(ids);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PushFont(g_font_head); ImGui::TextUnformatted("Keyboard (defaults)"); ImGui::PopFont();
    static const char *const keys[][2] = {                   /* nnpad_sdl.c g_kb_default */
        {"A / confirm", "Z"}, {"B / back", "X"}, {"X / menu", "C"}, {"Y / residents", "V"}, {"Left stick", "Arrow keys"}, {"Right stick (camera)", "T F G H"},
        {"D-pad", "I J K L"}, {"L / R", "Q / E"}, {"ZL / ZR", "A / S"}, {"+ / -", "Enter / Backspace"}, {"Fullscreen", "F11"}, {"Settings", "F1"}};
    if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_SizingFixedFit)) {
        for (int i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(keys[i][0]); ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.85f, 0.4f, 0.05f, 1), "%s", keys[i][1]); }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 6));
    if (ImGui::Button("Edit controllers.ini")) ShellExecuteA(NULL, "open", "notepad.exe", g_pads_ini, NULL, SW_SHOWNORMAL);
    card_end();
}

static void page_saves(ImVec2 avail) {
    card_begin("saves", avail);
    heading("Saves", "Your island lives in build\\userdata. Backups go to build\\saves and can be restored at any time.");
    status_chip(true, g_save_info);
    ImGui::Dummy(ImVec2(0, 4));
    if (fancy_button("backup", "Back up now", ImVec2(220, 50), C_ORANGE, C_ORANGE_HI, g_font_body, !g_running)) toast(backup_now(NULL) ? "Backup made." : "The backup failed.");
    ImGui::SameLine(0, 16);
    if (ImGui::Button("Open the save folder")) { CreateDirectoryA(g_userdata, NULL); open_in_explorer(g_userdata); }
    ImGui::SameLine();
    if (ImGui::Button("Open the backups")) { CreateDirectoryA(g_saves, NULL); open_in_explorer(g_saves); }
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushFont(g_font_head); ImGui::TextUnformatted("Backups"); ImGui::PopFont();
    static int pending = -1;
    if (g_nbackups == 0) hint("No backups yet.");
    ImGui::BeginChild("blist", ImVec2(avail.x - 60, ImGui::GetContentRegionAvail().y - 26), 0);
    for (int i = 0; i < g_nbackups; i++) {
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(g_backups[i] + 7);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 250);
        if (ImGui::Button("Restore") && !g_running) pending = i;
        ImGui::SameLine();
        if (ImGui::Button("Delete")) { char p[MAX_PATH]; path_join(p, g_saves, g_backups[i]); toast(shell_recycle(p) ? "Backup moved to the Recycle Bin." : "Could not delete it."); saves_scan(); }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (pending >= 0) ImGui::OpenPopup("Restore?");
    if (ImGui::BeginPopupModal("Restore?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::Text("Restore %s?", pending >= 0 ? g_backups[pending] + 7 : "");
        hint("Your current save is backed up first, so nothing is lost.");
        if (ImGui::Button("Restore", ImVec2(140, 0))) {
            char src[MAX_PATH]; path_join(src, g_saves, g_backups[pending]);
            int ok = !dir_exists(g_userdata) || backup_now("before_restore");
            if (ok) {
                char tmp[MAX_PATH]; snprintf(tmp, MAX_PATH, "%s", g_userdata);
                shell_recycle(tmp);
                ok = shell_copy(src, g_userdata);
            }
            toast(ok ? "Save restored." : "The restore failed: your previous save is in the backups.");
            saves_scan(); pending = -1; ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140, 0))) { pending = -1; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    card_end();
}

static void page_tools(ImVec2 avail) {
    card_begin("tools", avail);
    heading("Tools", "Housekeeping for the port.");
    FILETIME ft = {0, 0};
    long long cache = dir_exists(g_cache) ? dir_size(g_cache, &ft) : 0;
    char l[128]; snprintf(l, sizeof l, "Shader cache: %.1f MB", cache / 1048576.0);
    status_chip(true, l);
    hint("Compiled shaders are kept so the game does not stutter the next time it meets them. Clearing it is only useful after a driver problem.");
    if (ImGui::Button("Clear the shader cache") && !g_running) toast(shell_recycle(g_cache) ? "Shader cache moved to the Recycle Bin." : "Nothing to clear.");
    ImGui::Dummy(ImVec2(0, 8));
    if (ImGui::Button("Open the logs folder")) open_in_explorer(g_work);
    ImGui::SameLine();
    if (ImGui::Button("Open the config folder")) { char p[MAX_PATH]; path_join(p, g_build, "config"); open_in_explorer(p); }
    ImGui::SameLine();
    if (ImGui::Button("Open the port's folder")) open_in_explorer(g_repo);
    ImGui::Dummy(ImVec2(0, 8));
    if (ImGui::Button("Edit graphics.ini")) { if (g_ini_dirty) ini_save(); ShellExecuteA(NULL, "open", "notepad.exe", g_ini, NULL, SW_SHOWNORMAL); }
    ImGui::SameLine();
    if (ImGui::Button("Reload settings")) { ini_load(); toast("Settings reloaded from graphics.ini."); }
    card_end();
}

static void page_about(ImVec2 avail) {
    card_begin("about", avail);
    heading("About", NULL);
    ImGui::TextWrapped("Tomodachi Life: Living the Dream, running natively on Windows through a static recompilation of the Nintendo Switch game.");
    ImGui::Dummy(ImVec2(0, 6));
    hint("You need your own copy of the game: its files go in build\\dump (exefs and romfs). Nothing of the game is included.");
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Text("Launcher version %s", LAUNCHER_VERSION);
    ImGui::Text("Folder: %s", g_repo);
    ImGui::Dummy(ImVec2(0, 6));
    hint("Built with SDL3 and Dear ImGui. Tomodachi Life is a trademark of Nintendo; this project is not affiliated with Nintendo.");
    card_end();
}

/* ------------------------------------------------------------------ the window icon: a smiling yellow face, drawn here */
static void set_icon(SDL_Window *win) {
    const int S = 64;
    SDL_Surface *s = SDL_CreateSurface(S, S, SDL_PIXELFORMAT_RGBA32);
    if (!s) return;
    Uint32 *px = (Uint32 *)s->pixels;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float dx = x - S / 2 + 0.5f, dy = y - S / 2 + 0.5f, d = sqrtf(dx * dx + dy * dy);
            Uint8 r = 0, g = 0, b = 0, a = 0;
            if (d < S / 2 - 1) { r = 255; g = 200; b = 40; a = 255; }
            if (d >= S / 2 - 4 && d < S / 2 - 1) { r = 240; g = 120; b = 20; }
            float ex = fabsf(dx) - 9, ey = dy + 7;
            if (ex * ex + ey * ey < 14) { r = 60; g = 40; b = 20; }
            float m = sqrtf(dx * dx + (dy - 2) * (dy - 2));
            if (dy > 4 && m > 13 && m < 17) { r = 60; g = 40; b = 20; }
            px[y * S + x] = (Uint32)r | (Uint32)g << 8 | (Uint32)b << 16 | (Uint32)a << 24;
        }
    SDL_SetWindowIcon(win, s);
    SDL_DestroySurface(s);
}

int main(int argc, char **argv) {
    /* --shot <file.bmp> [page]: draw one page off screen into a picture and exit (a check of the look without a window) */
    const char *shot = NULL; int shot_page = 0, frame = 0;
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--shot")) { shot = argv[i + 1]; if (i + 2 < argc) shot_page = atoi(argv[i + 2]); }
    paths_init();
    ini_load();
    saves_scan();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return 1;
    SDL_Window *win = SDL_CreateWindow("Tomodachi Life - Launcher", 1180, 740, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) return 1;
    SDL_SetWindowMinimumSize(win, 980, 640);
    set_icon(win);
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) return 1;
    SDL_SetRenderVSync(ren, 1);
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_Texture *shot_tex = NULL;
    if (shot) { int w, h; SDL_GetWindowSizeInPixels(win, &w, &h); shot_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h); }
    else SDL_ShowWindow(win);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    style_init();
    fonts_init();
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);
    int page = shot ? shot_page % P_COUNT : P_PLAY;
    bool done = false;
    while (!done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(win))) done = true;
        }
        game_poll(win);
        if (!shot && g_running || !shot && (SDL_GetWindowFlags(win) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN))) { SDL_Delay(50); continue; }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImVec2 size = io.DisplaySize;
        draw_background(ImGui::GetBackgroundDrawList(), size, (float)ImGui::GetTime());
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("root", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        /* sidebar */
        const float side = 250, pad = 28;
        ImGui::SetCursorPos(ImVec2(pad, pad));
        ImGui::BeginGroup();
        ImGui::PushFont(g_font_title); ImGui::TextColored(ImVec4(0.42f, 0.22f, 0.02f, 1), "Tomodachi"); ImGui::PopFont();
        ImGui::PushFont(g_font_head); ImGui::TextColored(ImVec4(0.55f, 0.30f, 0.05f, 1), "Life"); ImGui::PopFont();
        ImGui::PushFont(g_font_small); ImGui::TextColored(ImVec4(0.55f, 0.36f, 0.10f, 1), "Living the Dream  -  PC"); ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 18));
        for (int i = 0; i < P_COUNT; i++) {
            bool sel = i == page;
            if (fancy_button(k_pages[i], k_pages[i], ImVec2(side - pad - 10, 48), sel ? C_ORANGE : IM_COL32(255, 236, 180, 255), sel ? C_ORANGE_HI : IM_COL32(255, 245, 210, 255), g_font_body)) page = i;
            if (!sel) {                                      /* dark label on the light buttons */
                ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                ImVec2 ts = ImGui::CalcTextSize(k_pages[i]);
                ImGui::GetWindowDrawList()->AddRectFilled(mn, mx, IM_COL32(255, 240, 195, 255), (mx.y - mn.y) * 0.5f);
                ImGui::GetWindowDrawList()->AddText(ImVec2(mn.x + (mx.x - mn.x - ts.x) * 0.5f, mn.y + (mx.y - mn.y - ts.y) * 0.5f), C_INK, k_pages[i]);
            }
        }
        ImGui::EndGroup();
        if (g_ini_dirty) {                                   /* unsaved settings: save button at the bottom of the sidebar */
            ImGui::SetCursorPos(ImVec2(pad, size.y - pad - 56));
            if (fancy_button("save", "Save settings", ImVec2(side - pad - 10, 52), IM_COL32(80, 170, 70, 255), IM_COL32(100, 190, 90, 255), g_font_body))
                toast(ini_save() ? "Settings saved." : "Could not write graphics.ini.");
        }
        /* page */
        ImGui::SetCursorPos(ImVec2(side + 10, pad));
        ImVec2 avail(size.x - side - 10 - pad, size.y - 2 * pad);
        switch (page) {
        case P_PLAY: page_play(win, avail); break;
        case P_DISPLAY: page_display(avail); break;
        case P_AUDIO: page_audio(avail); break;
        case P_CONTROLS: page_controls(avail); break;
        case P_SAVES: page_saves(avail); break;
        case P_TOOLS: page_tools(avail); break;
        default: page_about(avail); break;
        }
        if (g_toast[0] && ImGui::GetTime() < g_toast_until) {   /* the toast */
            ImDrawList *dl = ImGui::GetForegroundDrawList();
            ImVec2 ts = ImGui::CalcTextSize(g_toast);
            ImVec2 p((size.x - ts.x) * 0.5f, size.y - 70);
            dl->AddRectFilled(ImVec2(p.x - 22, p.y - 12), ImVec2(p.x + ts.x + 22, p.y + ts.y + 12), IM_COL32(60, 40, 20, 230), 20);
            dl->AddText(p, IM_COL32(255, 248, 228, 255), g_toast);
        }
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 255, 200, 60, 255);
        if (shot_tex) SDL_SetRenderTarget(ren, shot_tex);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
        if (shot_tex && ++frame == 4) {                     /* a few frames: fonts and layout settle */
            SDL_Surface *px = SDL_RenderReadPixels(ren, NULL);
            int ok = px && SDL_SaveBMP(px, shot);
            if (px) SDL_DestroySurface(px);
            printf("%s %s\n", ok ? "saved" : "could not save", shot);
            done = true;
        }
        SDL_RenderPresent(ren);
    }
    if (g_ini_dirty) ini_save();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
