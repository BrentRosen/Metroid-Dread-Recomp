/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "gui/browser.h"
#include "common/utilities/common.h"
#include "common/utilities/mods.h"
#include "gui/i18n.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

static bool suffix(const char* name, const char* extension) {
    size_t n = strlen(name), e = strlen(extension);
    if (n < e) return false;
    for (size_t i = 0; i < e; ++i)
        if (tolower((unsigned char)name[n - e + i]) != extension[i]) return false;
    return true;
}
bool gui_is_cia(const char* name) { return suffix(name, ".cia"); }
bool gui_is_dump(const char* name) {
    return suffix(name, ".nro") || suffix(name, ".xci") || suffix(name, ".nsp") || suffix(name, ".dxci") ||
           suffix(name, ".dnsp");
}
bool gui_game_folder(const char* path) {
    char *folder = snx_dirname(path), *name, *data;
    const char *own = path + strlen(path), *leaf = folder + strlen(folder);
    bool found;
    while (own > path && own[-1] != '/' && own[-1] != '\\') --own;
    while (leaf > folder && leaf[-1] != '/' && leaf[-1] != '\\') --leaf;
    name = snx_strdup(own);
#ifdef _WIN32
    if (suffix(name, ".exe")) name[strlen(name) - 4] = 0;
#endif
    data = snx_format("%s/%s_data", folder, name);
    found = *name && (!strcmp(leaf, name) || snx_addon_name(leaf, name, NULL)) && snx_is_file(path) && snx_is_dir(data);
    free(data);
    free(name);
    free(folder);
    return found;
}
void gui_games_free(GuiGames* games) {
    snx_free_strings(games->names, games->count);
    snx_free_strings(games->shown, games->count);
    snx_free_strings(games->paths, games->count);
    free(games->addons);
    games->names = games->shown = games->paths = NULL;
    games->addons = NULL;
    games->count = games->room = 0;
}
uint64_t gui_tree_size(const char* path, const int* stop) {
    char** names = NULL;
    size_t count = 0, i;
    uint64_t total = 0;
    if (!*path || snx_list_dir(path, &names, &count)) return 0; /* "" is none: Windows would list the working folder */
    for (i = 0; i < count && !__atomic_load_n(stop, __ATOMIC_RELAXED); ++i) {
        char* entry = snx_path_join(path, names[i]);
        if (entry && !snx_is_link(entry))
            total += snx_is_dir(entry) ? gui_tree_size(entry, stop) : snx_file_size(entry);
        free(entry);
    }
    snx_free_strings(names, count);
    return total;
}
/* whether a folder's entry is a game of kinds: a CIA, or one ready to run. Libraries often carry executable bits but
   are not games, and hidden files are not shown. On Android the shared storage runs nothing: a game is <name> beside
   <name>_code.so (name is cut there), and a dump put there, to be prepared, shows with its own name */
static bool playable(const char* directory, char* name, int kinds) {
    char* path;
    bool listed;
    if (name[0] == '.') return false;
    if (suffix(name, ".cia")) {
        if (!(kinds & GUI_GAMES_CIA)) return false;
        path = snx_path_join(directory, name);
        listed = path && snx_is_file(path);
        free(path);
        return listed;
    }
    if ((kinds & GUI_GAMES_DUMP) && gui_is_dump(name)) {
        path = snx_path_join(directory, name);
        listed = path && snx_is_file(path);
        free(path);
        return listed;
    }
    if (!(kinds & GUI_GAMES_RUN)) return false;
#if defined(__ANDROID__) || defined(SNX_IOS)
    { /* a game's code or a dump, a file: a folder named so is neither. An app's games have no runtime copy */
        bool code = suffix(name, "_code.so");
        if (!code && !gui_is_dump(name)) return false;
        path = snx_path_join(directory, name);
        listed = path && snx_is_file(path);
        free(path);
        if (listed && code) name[strlen(name) - 8] = 0;
        return listed;
    }
#else
#ifdef _WIN32
    if (!suffix(name, ".exe")) return false;
#else
    if (suffix(name, ".so") || strstr(name, ".so.") || suffix(name, ".dll") || suffix(name, ".dylib")) return false;
#endif
    path = snx_path_join(directory, name);
#ifndef _WIN32
    if (!path || access(path, X_OK)) {
        free(path);
        return false;
    }
#endif
    {
        char* executable = path ? snx_find_tool(path) : NULL; /* regular file, with UTF-8 paths on Windows */
        free(path);
        listed = executable != NULL;
        free(executable);
    }
    return listed;
#endif
}
/* a game in a folder of its own, <name>/<name> (<name>.exe on Windows, <name>_code.so on Android): where it is, else
   NULL. Any other folder (an older version's <name>_data/, the player's) is no game */
static char* folder_game(const char* directory, const char* name, int kinds) {
    char *folder, *own = NULL, *path = NULL;
    if (!(kinds & GUI_GAMES_RUN) || name[0] == '.') return NULL;
    folder = snx_path_join(directory, name);
    if (folder && snx_is_dir(folder) && !snx_is_link(folder)) {
#if defined(__ANDROID__) || defined(SNX_IOS)
        own = snx_format("%s_code.so", name);
#elif defined(_WIN32)
        own = snx_format("%s.exe", name);
#else
        own = snx_strdup(name);
#endif
        if (own && playable(folder, own, GUI_GAMES_RUN)) path = snx_path_join(folder, own);
    }
    free(own);
    free(folder);
    return path;
}
char* gui_folder_game(const char* directory, const char* name) { return folder_game(directory, name, GUI_GAMES_RUN); }
/* what the game at path, named name, is shown as: the name in its <name>_data/control.nacp, which the recompile
   copies there, else name */
static char* shown_name(const char* path, const char* name) {
    char *folder = snx_dirname(path), *stem = snx_strdup(name), *nacp = NULL, *shown = NULL, text[0x200];
    unsigned char* bytes;
    size_t size = 0;
    if (stem && suffix(stem, ".exe")) stem[strlen(stem) - 4] = 0;
    if (folder && stem) nacp = snx_format("%s/%s_data/control.nacp", folder, stem);
    bytes = nacp && snx_is_file(nacp) ? (unsigned char*)snx_read_file(nacp, &size) : NULL;
    if (bytes && snx_nacp_name(bytes, size, text, sizeof text)) shown = snx_strdup(text);
    free(bytes);
    free(nacp);
    free(stem);
    free(folder);
    return shown ? shown : snx_strdup(name);
}
char* gui_game_shown(const char* path) {
    char *folder = snx_dirname(path), *shown;
    const char* leaf = folder ? folder + strlen(folder) : NULL;
    if (!leaf) return NULL;
    while (leaf > folder && leaf[-1] != '/' && leaf[-1] != '\\') --leaf;
    shown = shown_name(path, leaf);
    free(folder);
    return shown;
}
char* gui_game_icon(const char* path) {
    char *folder, *icon;
    const char* leaf;
    if (!path || !gui_game_folder(path)) return NULL;
    folder = snx_dirname(path);
    leaf = folder + strlen(folder);
    while (leaf > folder && leaf[-1] != '/' && leaf[-1] != '\\') --leaf;
    icon = snx_format("%s/%s_data/icon.jpg", folder, leaf);
    free(folder);
    if (!snx_is_file(icon)) {
        free(icon);
        return NULL;
    }
    return icon;
}
/* whether the add-on named a (<game>_<entry>) is listed before b: updates first, the newest version first (one
   without a version last), then the DLCs by title ID */
static bool addon_before(SnxU64 game, const char* a, const char* b) {
    const char *ea = strchr(a, '_') + 1, *eb = strchr(b, '_') + 1;
    SnxU64 ia = 0, ib = 0, va, vb;
    int ua, ub;
    snx_addon_entry_id(game, ea, &ia);
    snx_addon_entry_id(game, eb, &ib);
    ua = snx_addon_kind(game, ia) == SNX_ADDON_UPDATE;
    ub = snx_addon_kind(game, ib) == SNX_ADDON_UPDATE;
    if (ua != ub) return ua;
    if (!ua) return strcmp(ea, eb) < 0;
    va = snx_addon_version(ea);
    vb = snx_addon_version(eb);
    if (va == SNX_NO_VERSION || vb == SNX_NO_VERSION) return vb == SNX_NO_VERSION && va != SNX_NO_VERSION;
    return va > vb;
}
/* the updates and DLCs made beside the game at path, in its folder (snx_addon_name): each a data folder, an update's
   with its program beside it. Their count, and with addons not NULL each added to it, in their names' order (an
   update's title ID before a DLC's) */
static size_t addons_of(const char* path, GuiGames* addons) {
    char *folder = snx_dirname(path), **names = NULL;
    const char* leaf = folder + strlen(folder);
    const char* own = path + strlen(path);
    size_t count = 0, found = 0, i, j;
    SnxU64 game = 0, aoc_base = 0;
    while (leaf > folder && leaf[-1] != '/' && leaf[-1] != '\\') --leaf;
    if (snx_title_parse(leaf, &game) && addons) { /* what its DLCs are numbered from: its control.nacp's base ID */
        char* file = snx_format("%s/%s_data/control.nacp", folder, leaf);
        size_t size = 0;
        unsigned char* nacp = (unsigned char*)snx_read_file(file, &size);
        aoc_base = snx_nacp_aoc_base(nacp, size, game);
        free(nacp);
        free(file);
    }
    while (own > path && own[-1] != '/' && own[-1] != '\\') --own;
    /* the game's own program, <name>/<name>: an add-on has none of its own */
    if (!gui_game_folder(path) || strncmp(own, leaf, strlen(leaf)) || (own[strlen(leaf)] && own[strlen(leaf)] != '.') ||
        snx_list_dir(folder, &names, &count))
        count = 0;
    for (i = 0; i < count; ++i) {
        size_t n = strlen(names[i]);
        char *stem = snx_strdup(names[i]), *data = snx_path_join(folder, names[i]), *program = NULL, *own;
        char text[0x40], version[0x20];
        SnxU64 id;
        /* <game>_<its ID>_data: a DLC's record, or an update's beside its program */
        bool listed = n > 5 && !strcmp(names[i] + n - 5, "_data") && snx_is_dir(data);
        if (listed) stem[n - 5] = 0;
        listed = listed && snx_addon_name(leaf, stem, &id);
        if (listed && snx_addon_kind(game, id) == SNX_ADDON_UPDATE) {
#if defined(__ANDROID__) || defined(SNX_IOS)
            char* file = snx_format("%s_code.so", stem);
#elif defined(_WIN32)
            char* file = snx_format("%s.exe", stem);
#else
            char* file = snx_strdup(stem);
#endif
            listed = playable(folder, file, GUI_GAMES_RUN);
            free(file);
#if defined(_WIN32)
            program = snx_format("%s/%s.exe", folder, stem);
#else
            program = snx_path_join(folder, stem);
#endif
        }
        if (listed && addons) {
            unsigned char* nacp;
            size_t size = 0;
            own = snx_format("%s/control.nacp", data);
            nacp = snx_addon_kind(game, id) == SNX_ADDON_UPDATE ? (unsigned char*)snx_read_file(own, &size) : NULL;
            if (snx_addon_kind(game, id) == SNX_ADDON_DLC && id > aoc_base && id - aoc_base < 0x800)
                snprintf(text, sizeof text, snx_tr("DLC %u"), (unsigned)(id - aoc_base));
            else if (snx_addon_kind(game, id) == SNX_ADDON_DLC) { /* numbered from no base the game names */
                char title[17];
                snx_title_text(id, title);
                snprintf(text, sizeof text, snx_tr("DLC %s"), title);
            } else if (snx_nacp_version(nacp, size, version, sizeof version))
                snprintf(text, sizeof text, snx_tr("Update %s"), version);
            else
                snprintf(text, sizeof text, "%s", snx_tr("Update"));
            free(nacp);
            free(own);
            for (j = addons->count; j > 0 && addon_before(game, stem, addons->names[j - 1]); --j) {
                addons->names[j] = addons->names[j - 1];
                addons->shown[j] = addons->shown[j - 1];
                addons->paths[j] = addons->paths[j - 1];
                addons->addons[j] = 0;
            }
            addons->names[j] = snx_strdup(stem);
            addons->shown[j] = snx_strdup(text);
            addons->paths[j] = program ? snx_strdup(program) : snx_strdup(data);
            addons->addons[j] = 0;
            addons->count++;
        }
        found += listed;
        free(program);
        free(data);
        free(stem);
    }
    snx_free_strings(names, count);
    free(folder);
    return found;
}
bool gui_game_addons(const char* path, GuiGames* addons) {
    size_t room = addons_of(path, NULL) + 1;
    gui_games_free(addons);
    addons->names = (char**)calloc(room, sizeof *addons->names);
    addons->shown = (char**)calloc(room, sizeof *addons->shown);
    addons->paths = (char**)calloc(room, sizeof *addons->paths);
    addons->addons = (size_t*)calloc(room, sizeof *addons->addons);
    if (!addons->names || !addons->shown || !addons->paths || !addons->addons) {
        gui_games_free(addons);
        return false;
    }
    addons->room = room;
    addons_of(path, addons);
    return addons->count > 0;
}
size_t gui_game_mods(const char* path, char** folder, char*** names) {
    char *dir, *data;
    const char* leaf;
    size_t count;
    char** listed = NULL;
    *folder = NULL;
    if (names) *names = NULL;
    if (!gui_game_folder(path)) return 0;
    dir = snx_dirname(path);
    for (leaf = dir + strlen(dir); leaf > dir && leaf[-1] != '/' && leaf[-1] != '\\'; --leaf) continue;
    data = snx_format("%s/%s_data", dir, leaf);
    *folder = snx_mods_folder(data);
    count = snx_mods_list(*folder, &listed);
    if (names)
        *names = listed;
    else
        snx_free_strings(listed, count);
    free(data);
    free(dir);
    return count;
}
static int before(const char* a, const char* b) {
    for (; *a && tolower((unsigned char)*a) == tolower((unsigned char)*b); ++a, ++b) {}
    return tolower((unsigned char)*a) < tolower((unsigned char)*b);
}
/* directory's games into games, sorted from first on by what they are shown as; with depth, its folders' games too, down
   that many levels, a folder that is a game or hidden left alone */
/* room in the lists for n entries, never fewer than they have: a folder deeper in adds its games while its
   parent's are still to come, as many as it holds */
static bool games_room(GuiGames* games, size_t n) {
    char** more;
    size_t* addons;
    if (n <= games->room) return true;
    if (n < 2 * games->room) n = 2 * games->room;
    if (!(more = (char**)realloc(games->names, n * sizeof *more))) return false;
    games->names = more;
    if (!(more = (char**)realloc(games->shown, n * sizeof *more))) return false;
    games->shown = more;
    if (!(more = (char**)realloc(games->paths, n * sizeof *more))) return false;
    games->paths = more;
    if (!(addons = (size_t*)realloc(games->addons, n * sizeof *addons))) return false;
    games->addons = addons;
    games->room = n;
    return true;
}
static bool add_games(GuiGames* games, const char* directory, int kinds, size_t first, int depth, char* error,
                      size_t size) {
    char** names = NULL;
    size_t count = 0, i;
    if (snx_list_dir(directory, &names, &count)) {
        char why[256];
        snprintf(error, size, snx_tr("Cannot open %s: %s"), directory, snx_os_error(why, sizeof why));
        return false;
    }
    size_t j;
    for (i = 0; i < count; ++i) {
        char* path = folder_game(directory, names[i], kinds);
        char* shown;
        if (!path && playable(directory, names[i], kinds)) path = snx_path_join(directory, names[i]);
        if (!path && depth > 0 && names[i][0] != '.') { /* a folder of games, not one */
            char* sub = snx_path_join(directory, names[i]);
            if (sub && snx_is_dir(sub)) add_games(games, sub, kinds, first, depth - 1, error, 0);
            free(sub);
            continue;
        }
        if (!path) continue;
        if (!games_room(games, games->count + 2)) { /* it and the end */
            free(path);
            snx_free_strings(names, count);
            snprintf(error, size, snx_tr("Not enough memory to list %s."), directory);
            return false;
        }
        shown = shown_name(path, names[i]);
        if (!shown) {
            free(path);
            continue;
        }
        /* in among the folder's ones by what they are shown as, a game's folder being its title ID */
        for (j = games->count; j > first && before(shown, games->shown[j - 1]); --j) {
            games->names[j] = games->names[j - 1];
            games->shown[j] = games->shown[j - 1];
            games->paths[j] = games->paths[j - 1];
            games->addons[j] = games->addons[j - 1];
        }
        games->names[j] = names[i];
        games->shown[j] = shown;
        games->paths[j] = path;
        {
            char* mods;
            games->addons[j] = addons_of(path, NULL) + gui_game_mods(path, &mods, NULL);
            free(mods);
        }
        games->count++;
        names[i] = NULL;
    }
    snx_free_strings(names, count);
    return true;
}
bool gui_games_add(GuiGames* games, const char* directory, int kinds, char* error, size_t size) {
    return gui_games_add_deep(games, directory, kinds, 0, error, size);
}
bool gui_games_add_deep(GuiGames* games, const char* directory, int kinds, int depth, char* error, size_t size) {
    if (size) error[0] = 0;
    return add_games(games, directory, kinds, games->count, depth, error, size);
}
bool gui_games_heading(const GuiGames* games, size_t i) { return i < games->count && !games->paths[i]; }
static bool dump_entry(const GuiGames* games, size_t i) {
    return gui_is_dump(games->names[i]) || gui_is_cia(games->names[i]);
}
bool gui_games_group(GuiGames* games, const char* ready, const char* dumps,
                     bool (*made)(const char* path, const void* user), const void* user) {
    size_t room = games->count + 2, n = 0, i, in[2] = {0, 0};
    char* heading[2][2] = {{NULL, NULL}, {NULL, NULL}}; /* each group's name and text */
    GuiGames out;
    int group;
    bool ok;
    for (i = 0; i < games->count; ++i)
        if (games->paths[i] && !(dump_entry(games, i) && made && made(games->paths[i], user)))
            in[dump_entry(games, i)]++;
    memset(&out, 0, sizeof out);
    out.names = (char**)calloc(room, sizeof *out.names);
    out.shown = (char**)calloc(room, sizeof *out.shown);
    out.paths = (char**)calloc(room, sizeof *out.paths);
    out.addons = (size_t*)calloc(room, sizeof *out.addons);
    out.room = room;
    ok = out.names && out.shown && out.paths && out.addons;
    for (group = 0; group < 2 && ok; ++group)
        if (in[0] && in[1]) { /* one group alone has nothing to be told apart from */
            heading[group][0] = snx_strdup("");
            heading[group][1] = snx_strdup(group ? dumps : ready);
            ok = heading[group][0] && heading[group][1];
        }
    if (!ok) { /* nothing moved yet: games stays as it was */
        for (group = 0; group < 2; ++group) {
            free(heading[group][0]);
            free(heading[group][1]);
        }
        gui_games_free(&out);
        return false;
    }
    for (group = 0; group < 2; ++group) {
        if (!in[group]) continue;
        if (heading[group][1]) {
            out.names[n] = heading[group][0];
            out.shown[n++] = heading[group][1];
        }
        for (i = 0; i < games->count; ++i) {
            if (!games->paths[i] || (int)dump_entry(games, i) != group) continue; /* an earlier heading goes */
            if (group && made && made(games->paths[i], user)) continue;
            out.names[n] = games->names[i];
            out.shown[n] = games->shown[i];
            out.paths[n] = games->paths[i];
            out.addons[n++] = games->addons[i];
            games->names[i] = games->shown[i] = games->paths[i] = NULL; /* moved to out */
        }
    }
    out.count = n;
    gui_games_free(games); /* what was left out: dumps made already, earlier headings */
    *games = out;
    return true;
}
bool gui_has_folders(const char* directory, int kinds) {
    char** names = NULL;
    size_t count = 0, i;
    bool found = false;
    if (snx_list_dir(directory, &names, &count)) return false;
    for (i = 0; i < count && !found; ++i) {
        char *sub, *game;
        if (names[i][0] == '.') continue;
        sub = snx_path_join(directory, names[i]);
        game = folder_game(directory, names[i], kinds);
        found = sub && snx_is_dir(sub) && !game;
        free(game);
        free(sub);
    }
    snx_free_strings(names, count);
    return found;
}
bool gui_games_scan(GuiGames* games, const char* directory, bool cia, char* error, size_t size) {
    gui_games_free(games);
    return gui_games_add(games, directory, cia ? GUI_GAMES_CIA : GUI_GAMES_RUN, error, size);
}
