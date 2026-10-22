/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A CIA read where it lies: the file itself mapped read-only and shared for as long as it is open, checked once as the
   extraction checks it, its ExeFS files and RomFS level 3 handed out as views into the mapping. Nothing is written */
#include "3ds/loader/internal.h"
#include "common/filesystem/host.h"
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

struct SnxCtrCia {
    int fd;
    const unsigned char* map;
    uint64_t size, mtime;
    CtrCia cia;
    CtrNcch ncch;
};
/* the host's modification time of path, UTC Unix milliseconds, as fs_tree_mtime reads it; 0 when it can't say */
static uint64_t modified(const char* path) {
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attributes)) return 0;
    ULARGE_INTEGER time = {.LowPart = attributes.ftLastWriteTime.dwLowDateTime,
                           .HighPart = attributes.ftLastWriteTime.dwHighDateTime};
    return time.QuadPart < 116444736000000000ull ? 0 : (time.QuadPart - 116444736000000000ull) / 10000;
#else
    struct stat st;
    if (stat(path, &st) || st.st_mtime < 0) return 0;
#if defined(__APPLE__)
    return (uint64_t)st.st_mtime * 1000 + (uint64_t)st.st_mtimespec.tv_nsec / 1000000;
#else
    return (uint64_t)st.st_mtime * 1000 + (uint64_t)st.st_mtim.tv_nsec / 1000000;
#endif
#endif
}
void snx_ctr_cia_close(SnxCtrCia* c) {
    if (!c) return;
    ctr_ncch_free(&c->ncch);
    ctr_cia_free(&c->cia);
    plat_file_unshare(c->fd, c->map, c->size);
    free(c);
}
SnxCtrExtractResult snx_ctr_cia_open(const char* input, SnxCtrCia** out, char* error, size_t error_size) {
    /* read where it lies every launch: no whole-data hashes (CtrReader.quick) */
    CtrReader r = {.error = error, .error_size = error_size, .quick = true};
    SnxCtrCia* c = calloc(1, sizeof *c);
    *out = NULL;
    if (error_size) error[0] = 0;
    if (!c) {
        ctr_fail(&r, SNX_CTR_EXTRACT_LIMIT, "out of memory");
        return r.result;
    }
    c->fd = -1;
    if (!input || !snx_is_file(input)) {
        ctr_fail(&r, SNX_CTR_EXTRACT_IO, "cannot open CIA input");
        goto done;
    }
    c->size = snx_file_size(input);
    /* the largest CIA the extraction takes: one larger is refused before it is mapped */
    if (c->size > CTR_OUTPUT_LIMIT) {
        ctr_fail(&r, SNX_CTR_EXTRACT_LIMIT, "CIA exceeds 1 GiB input limit");
        goto done;
    }
    const uint8_t* view = NULL;
    if (plat_file_share(input, c->size, &c->fd, &view) || !view) {
        ctr_fail(&r, SNX_CTR_EXTRACT_IO, "cannot map CIA input");
        goto done;
    }
    c->map = view;
    c->mtime = modified(input);
    r.map = c->map;
    r.size = c->size;
    if (ctr_cia_load(&r, &c->cia) && ctr_cia_contents(&r, &c->cia, NULL)) ctr_ncch_parse(&r, &c->cia, &c->ncch);
done:
    if (r.result == SNX_CTR_EXTRACT_OK)
        *out = c;
    else
        snx_ctr_cia_close(c);
    return r.result;
}
uint64_t snx_ctr_cia_title(const SnxCtrCia* c) { return c->cia.title_id; }
uint64_t snx_ctr_cia_mtime(const SnxCtrCia* c) { return c->mtime; }
const unsigned char* snx_ctr_cia_exheader(const SnxCtrCia* c) { return c->ncch.exheader; }
const unsigned char* snx_ctr_cia_code(const SnxCtrCia* c, size_t* size) {
    *size = c->ncch.code_size;
    return c->ncch.code;
}
const unsigned char* snx_ctr_cia_exefs(const SnxCtrCia* c, const char* name, uint64_t* size) {
    for (size_t i = 0; i < 10; ++i)
        if (*c->ncch.names[i] && !strcmp(c->ncch.names[i], name)) {
            *size = c->ncch.files[i].size;
            return c->map + c->ncch.files[i].offset;
        }
    *size = 0;
    return NULL;
}
const unsigned char* snx_ctr_cia_romfs(const SnxCtrCia* c, uint64_t* size) {
    *size = c->ncch.level3.size;
    return *size ? c->map + c->ncch.level3.offset : NULL;
}
