/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_GUI_BROWSER_H
#define SNX_GUI_BROWSER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    char** names; /* basenames: a game's folder, or an older version's file */
    char** shown; /* what each is shown as, sorted in each folder: the name in its control.nacp, else names' */
    char** paths; /* where each is */
    size_t* addons; /* how many updates and DLCs each has beside it (gui_game_addons), and mods (gui_game_mods) */
    size_t count;
    size_t room; /* the entries the lists have room for, one past the last included */
} GuiGames;
/* what a folder's games are: ready to run (executables; on Android a game's code or a dump), CIA files, and dumps
   (NSP, XCI and NRO files), prepared when they run */
enum { GUI_GAMES_RUN = 1, GUI_GAMES_CIA = 2, GUI_GAMES_DUMP = 4 };
/* A folder's games of kinds added after those listed: its own files, and each folder of it that holds its game
   (<name>/<name>), named by the folder; nothing deeper. False, error saying why, when it cannot be read */
bool gui_games_add(GuiGames* games, const char* directory, int kinds, char* error, size_t size);
/* and with depth, the games of its other folders too, down that many levels; a hidden folder left alone. The folder's
   own error alone is said */
bool gui_games_add_deep(GuiGames* games, const char* directory, int kinds, int depth, char* error, size_t size);
/* games in two groups: those ready to run, then the dumps and CIAs, each in the order it had; when both have games,
   each under a heading of its own (an entry whose path is NULL, its text shown). A dump made already (made says so of
   its path) is left out. False when out of memory, games as they were */
bool gui_games_group(GuiGames* games, const char* ready, const char* dumps,
                     bool (*made)(const char* path, const void* user), const void* user);
/* whether entry i of games is a group's heading, which nothing runs */
bool gui_games_heading(const GuiGames* games, size_t i);
/* whether directory holds a folder that is no game and not hidden: one whose games a deep add would list */
bool gui_has_folders(const char* directory, int kinds);
/* games/'s alone: its executable files and game folders, or with cia CIA files (case-insensitive extension, no execute
   bit needed). Never recurse into data/save directories. */
bool gui_games_scan(GuiGames* games, const char* directory, bool cia, char* error, size_t size);
void gui_games_free(GuiGames* games);
/* The bytes of the files under path, in its folders too, links not followed: 0 for no such folder, or "". It stops,
   with what it counted, once *stop is set by another thread */
uint64_t gui_tree_size(const char* path, const int* stop);
/* An NRO, XCI or NSP (or a decrypted one) by its name: a dump the driver prepares, not a game ready to run */
bool gui_is_dump(const char* name);
/* A game's program in a folder of its own beside its data (<name>/<name>, .exe on Windows, and <name>/<name>_data/),
   or an update's or a DLC's beside it (<name>/<name>_<its title ID>), as the recompile lays games out: the driver
   plays it, and recompiles it first when another build made it. A game an older version laid out beside the others
   is not one */
bool gui_game_folder(const char* path);
/* The program of the game in directory/name, a folder of its own as gui_game_folder has it (<name>/<name>, .exe on
   Windows, <name>_code.so on Android), as Games lists it: owned, else NULL */
char* gui_folder_game(const char* directory, const char* name);
/* What Games shows the game whose program is at path as, a game of a folder of its own: the name in its
   <name>_data/control.nacp, else its folder's name. Owned */
char* gui_game_shown(const char* path);
/* The icon the extraction kept for the game whose program is at path, a game of a folder of its own: its
   <name>_data/icon.jpg, owned, else NULL (none there, or no such game) */
char* gui_game_icon(const char* path);
/* The updates and DLCs of the game at path, made beside it in its folder (<name>/<name>_<add-on's title ID>_data/, an
   update's program beside it), into addons (emptied first): names <name>_<its title ID>, shown what the player reads
   ("Update 1.2.0" from its control.nacp, "DLC 1"), paths an update's program or a DLC's data folder, an update
   first. True when it has any; false, addons empty, when it has none or memory ran out */
bool gui_game_addons(const char* path, GuiGames* addons);
/* The mods folder of the game at path, a program in a folder of its own (<name>/<name>_data/mods/, snx_mods_folder),
   into *folder (owned, NULL for any other game), and its mods (snx_mods_list) into *names when names is not NULL:
   their count */
size_t gui_game_mods(const char* path, char** folder, char*** names);
/* A 3DS CIA by its name */
bool gui_is_cia(const char* name);
#endif
