/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A game's runtime JIT cache as the launcher sees it: where it is, the optimization and hook its key records, and
   whether the game's AOT optimization may build a pack from it. */
#ifndef SNX_GUI_AOT_H
#define SNX_GUI_AOT_H
#include <stdint.h>

typedef struct {
    uint64_t size; /* the cache file's bytes */
    char opt;      /* the -O its key was made with */
    int hook;      /* 1: hooked direct calls */
} GuiAotInfo;

/* where the runtime keeps <name>.jitcache: the .folders cache= folder when it is there, else its user path, else the
   game's folder. Owned; NULL when out of memory */
char* gui_aot_cache_path(const char* folder, const char* name);
/* 1 with the cache's header read, 0 for a missing file, another file, or a key without opt= and hook= */
int gui_aot_cache_info(const char* path, GuiAotInfo* info);
/* 1 when game_opt (the launcher's opt setting, "2" for none) is the level the cache was built at */
int gui_aot_levels_match(const char* game_opt, char cache_opt);
#endif
