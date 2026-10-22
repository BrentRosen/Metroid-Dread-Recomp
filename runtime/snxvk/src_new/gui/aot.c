/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "gui/aot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/cpu/recompiler/llvm/jit_cache.h"
#include "common/utilities/common.h"
#include "common/utilities/game_ini.h"

#define AOT_KEY_MAX 8192 /* the key names the host, LLVM and its features: far less */

static uint32_t get32(const unsigned char* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

char* gui_aot_cache_path(const char* folder, const char* name) {
    char* record;
    char* recorded;
    char* dir = NULL;
    char* moved;
    char* path;
    if (!folder || !name) return NULL;
    /* the runtime's .folders cache=, when it names a folder that is there; else its user path, as kind_dir */
    record = snx_format("%s/%s.folders", folder, name);
    recorded = record ? snx_ini_value(record, "cache") : NULL;
    if (recorded && snx_is_dir(recorded)) {
        dir = snx_strdup(recorded);
    } else {
        /* the runtime creates its user path; when that is not there, the cache is still beside the game */
        moved = snx_user_path(folder, SNX_USER_CACHE);
        dir = snx_strdup(moved && strcmp(moved, folder) && snx_is_dir(moved) ? moved : folder);
        free(moved);
    }
    free(recorded);
    free(record);
    if (!dir) return NULL;
    moved = snx_absolute(dir);
    if (moved) {
        free(dir);
        dir = moved;
    }
    path = snx_format("%s/%s.jitcache", dir, name);
    free(dir);
    return path;
}

int gui_aot_cache_info(const char* path, GuiAotInfo* info) {
    unsigned char head[12];
    uint32_t length;
    char* key;
    const char* field;
    size_t got;
    FILE* f;
    if (!path || !info) return 0;
    f = snx_fopen(path, "rb");
    if (!f) return 0;
    if (fread(head, 1, sizeof head, f) != sizeof head || memcmp(head, SNX_JIT_CACHE_MAGIC, 8)) {
        fclose(f);
        return 0;
    }
    length = get32(head + 8);
    if (!length || length > AOT_KEY_MAX) {
        fclose(f);
        return 0;
    }
    key = (char*)malloc((size_t)length + 1);
    if (!key) {
        fclose(f);
        return 0;
    }
    got = fread(key, 1, length, f);
    fclose(f);
    if (got != length) {
        free(key);
        return 0;
    }
    key[length] = 0;
    field = strstr(key, " opt=");
    if (!field || !strchr("0123sz", field[5])) {
        free(key);
        return 0;
    }
    info->opt = field[5];
    field = strstr(key, " hook=");
    if (!field || (field[6] != '0' && field[6] != '1')) {
        free(key);
        return 0;
    }
    info->hook = field[6] - '0';
    info->size = (uint64_t)snx_file_size(path);
    free(key);
    return 1;
}

int gui_aot_levels_match(const char* game_opt, char cache_opt) {
    char game = game_opt && game_opt[0] ? game_opt[0] : '2';
    return game == cache_opt;
}
