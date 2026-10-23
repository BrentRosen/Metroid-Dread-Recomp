// SwitchRecomp - Copyright 2026 MZ Labs
// Licensed under the Apache License 2.0. See LICENSE.txt and NOTICE.txt;
// any copy or derived work must keep the credit to SwitchRecomp by MZ Labs.
// Play screen > Add to Steam: a non-Steam game shortcut in Steam's userdata/<account>/config/shortcuts.vdf (binary
// KeyValues: 0x00 map, 0x01 string, 0x02 int32, 0x07 uint64, 0x08 end of map). The file is parsed and written back
// whole, unknown entries kept as they are; shortcuts.vdf.bak holds the previous file. Steam rewrites the file when it
// exits, so a running Steam is shut down first and started again afterwards.
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstdint>

namespace {
struct Node {
    int type = 0;                          // 0 map, 1 string, 2 int32, 7 uint64, others kept raw
    std::string key, str;
    uint32_t i32 = 0; uint64_t u64 = 0;
    std::vector<std::unique_ptr<Node>> kids;
};
bool read_str(const std::string& d, size_t& p, std::string& out) {
    size_t e = d.find('\0', p); if (e == std::string::npos) return false;
    out = d.substr(p, e - p); p = e + 1; return true; }
bool parse_map(const std::string& d, size_t& p, Node& m) {   // children until 0x08
    while (p < d.size()) {
        int t = (unsigned char)d[p++];
        if (t == 8) return true;
        auto n = std::make_unique<Node>(); n->type = t;
        if (!read_str(d, p, n->key)) return false;
        if (t == 0) { if (!parse_map(d, p, *n)) return false; }
        else if (t == 1) { if (!read_str(d, p, n->str)) return false; }
        else if (t == 2) { if (p + 4 > d.size()) return false; memcpy(&n->i32, d.data() + p, 4); p += 4; }
        else if (t == 7) { if (p + 8 > d.size()) return false; memcpy(&n->u64, d.data() + p, 8); p += 8; }
        else return false;                 // a type we don't know: refuse rather than corrupt the file
        m.kids.push_back(std::move(n));
    }
    return false; }
void write_map(std::string& o, const Node& m) {
    for (auto& n : m.kids) {
        o += (char)n->type; o += n->key; o += '\0';
        if (n->type == 0) write_map(o, *n);
        else if (n->type == 1) { o += n->str; o += '\0'; }
        else if (n->type == 2) o.append((const char*)&n->i32, 4);
        else o.append((const char*)&n->u64, 8);
    }
    o += (char)8; }
Node* child(Node& m, const char* key) { for (auto& n : m.kids) if (_stricmp(n->key.c_str(), key) == 0) return n.get(); return nullptr; }
void add_str(Node& m, const char* k, const std::string& v) { auto n = std::make_unique<Node>(); n->type = 1; n->key = k; n->str = v; m.kids.push_back(std::move(n)); }
void add_int(Node& m, const char* k, uint32_t v) { auto n = std::make_unique<Node>(); n->type = 2; n->key = k; n->i32 = v; m.kids.push_back(std::move(n)); }
uint32_t crc32(const std::string& s) {
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : s) { c ^= b; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c; }
}   // namespace

namespace fe {
// adds the shortcut to one shortcuts.vdf (created if missing). 1 added, 0 already there, -1 error (msg says why)
int steam_vdf_add(const std::string& path, const std::string& name, const std::string& exe, const std::string& dir, std::string& msg) {
    std::string d;
    if (FILE* f = fopen(path.c_str(), "rb")) { char buf[65536]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.append(buf, n); fclose(f); }
    Node root;
    if (!d.empty()) { size_t p = 0; if (!parse_map(d, p, root)) { msg = "Steam's shortcut list has a format this version doesn't know; nothing was changed."; return -1; } }
    Node* sc = child(root, "shortcuts");
    if (!sc) { auto n = std::make_unique<Node>(); n->type = 0; n->key = "shortcuts"; sc = n.get(); root.kids.push_back(std::move(n)); }
    std::string qexe = "\"" + exe + "\"";
    for (auto& e : sc->kids) if (e->type == 0) { Node* x = child(*e, "Exe"); if (x && _stricmp(x->str.c_str(), qexe.c_str()) == 0) { msg = "Metroid Dread is already in Steam."; return 0; } }
    int idx = 0; for (auto& e : sc->kids) { int v = atoi(e->key.c_str()); if (v + 1 > idx) idx = v + 1; }
    auto e = std::make_unique<Node>(); e->type = 0; e->key = std::to_string(idx);
    add_int(*e, "appid", crc32(qexe + name) | 0x80000000u);
    add_str(*e, "AppName", name); add_str(*e, "Exe", qexe); add_str(*e, "StartDir", "\"" + dir + "\"");
    add_str(*e, "icon", exe); add_str(*e, "ShortcutPath", ""); add_str(*e, "LaunchOptions", "");
    add_int(*e, "IsHidden", 0); add_int(*e, "AllowDesktopConfig", 1); add_int(*e, "AllowOverlay", 1);
    add_int(*e, "OpenVR", 0); add_int(*e, "Devkit", 0); add_str(*e, "DevkitGameID", ""); add_int(*e, "DevkitOverrideAppID", 0);
    add_int(*e, "LastPlayTime", 0); add_str(*e, "FlatpakAppID", "");
    { auto t = std::make_unique<Node>(); t->type = 0; t->key = "tags"; e->kids.push_back(std::move(t)); }
    sc->kids.push_back(std::move(e));
    std::string o; write_map(o, root);
    if (!d.empty()) { std::string bak = path + ".bak"; CopyFileA(path.c_str(), bak.c_str(), FALSE); }
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb"); if (!f) { msg = "Could not write Steam's shortcut list."; return -1; }
    bool ok = fwrite(o.data(), 1, o.size(), f) == o.size(); ok = fclose(f) == 0 && ok;
    if (!ok || !MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileA(tmp.c_str()); msg = "Could not write Steam's shortcut list."; return -1; }
    msg = "Added to Steam."; return 1; }

static bool steam_running(DWORD* pid) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (s == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32 pe; pe.dwSize = sizeof pe; bool found = false;
    for (BOOL ok = Process32First(s, &pe); ok; ok = Process32Next(s, &pe)) if (_stricmp(pe.szExeFile, "steam.exe") == 0) { found = true; if (pid) *pid = pe.th32ProcessID; break; }
    CloseHandle(s); return found; }
static std::string reg_str(HKEY root, const char* key, const char* val) {
    char buf[1024]; DWORD n = sizeof buf; if (RegGetValueA(root, key, val, RRF_RT_REG_SZ, nullptr, buf, &n) == ERROR_SUCCESS) return buf; return ""; }
static DWORD reg_dword(HKEY root, const char* key, const char* val) {
    DWORD v = 0, n = sizeof v; if (RegGetValueA(root, key, val, RRF_RT_REG_DWORD, nullptr, &v, &n) == ERROR_SUCCESS) return v; return 0; }

// the whole button: finds Steam and the account, closes and restarts Steam around the change
int steam_add_shortcut(const std::string& exe, const std::string& dir, std::string& msg) {
    std::string steam = reg_str(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath");
    if (steam.empty()) { msg = "Steam isn't installed for this Windows user."; return -1; }
    for (auto& ch : steam) if (ch == '/') ch = '\\';
    // the account: the one signed in now, else the userdata folder used most recently
    std::string acct; DWORD active = reg_dword(HKEY_CURRENT_USER, "Software\\Valve\\Steam\\ActiveProcess", "ActiveUser");
    if (active) acct = std::to_string(active);
    if (acct.empty()) {
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((steam + "\\userdata\\*").c_str(), &fd); FILETIME best = {};
        if (h != INVALID_HANDLE_VALUE) { do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] < '1' || fd.cFileName[0] > '9') continue;
                WIN32_FILE_ATTRIBUTE_DATA a; std::string lc = steam + "\\userdata\\" + fd.cFileName + "\\config\\localconfig.vdf";
                if (GetFileAttributesExA(lc.c_str(), GetFileExInfoStandard, &a) && CompareFileTime(&a.ftLastWriteTime, &best) > 0) { best = a.ftLastWriteTime; acct = fd.cFileName; }
            } while (FindNextFileA(h, &fd)); FindClose(h); }
    }
    if (acct.empty()) { msg = "Sign in to Steam once, then try again."; return -1; }
    std::string cfg = steam + "\\userdata\\" + acct + "\\config";
    CreateDirectoryA(cfg.c_str(), nullptr);
    DWORD pid = 0; bool was = steam_running(&pid);
    if (was) {   // Steam keeps its own copy and writes it back on exit: close it cleanly first
        ShellExecuteA(nullptr, "open", (steam + "\\steam.exe").c_str(), "-shutdown", nullptr, SW_HIDE);
        HANDLE ph = OpenProcess(SYNCHRONIZE, FALSE, pid);
        DWORD w = ph ? WaitForSingleObject(ph, 30000) : WAIT_FAILED; if (ph) CloseHandle(ph);
        if (w != WAIT_OBJECT_0 && steam_running(nullptr)) { msg = "Steam didn't close. Close Steam yourself, then press Add to Steam again."; return -1; }
        Sleep(1000);
    }
    int r = steam_vdf_add(cfg + "\\shortcuts.vdf", "Metroid Dread", exe, dir, msg);
    if (was) { ShellExecuteA(nullptr, "open", (steam + "\\steam.exe").c_str(), "-silent", nullptr, SW_SHOWMINNOACTIVE); if (r == 1) msg += " Steam was restarted to pick it up."; }
    return r; }
}   // namespace fe
