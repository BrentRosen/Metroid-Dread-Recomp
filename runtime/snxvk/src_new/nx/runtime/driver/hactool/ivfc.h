#ifndef HACTOOL_IVFC_H
#define HACTOOL_IVFC_H

#include <string.h>
#include "types.h"
#include "utils.h"
#include "settings.h"


#define IVFC_HEADER_SIZE 0xE0
#define IVFC_MAX_LEVEL 6
#define IVFC_MAX_BUFFERSIZE 0x4000

#define MAGIC_IVFC 0x43465649

typedef struct {
    uint64_t logical_offset;
    uint64_t hash_data_size;
    uint32_t block_size;
    uint32_t reserved;
} ivfc_level_hdr_t;

typedef struct {
    uint64_t data_offset;
    uint64_t data_size;
    uint64_t hash_offset;
    uint32_t hash_block_size;
    validity_t hash_validity;
} ivfc_level_ctx_t;

typedef struct {
    uint32_t magic;
    uint32_t id;
    uint32_t master_hash_size;
    uint32_t num_levels;
    ivfc_level_hdr_t level_headers[IVFC_MAX_LEVEL];
    uint8_t _0xA0[0x20];
    uint8_t master_hash[0x20];
} ivfc_hdr_t;

typedef struct {
    uint32_t magic;
    uint32_t id;
    uint32_t master_hash_size;
    uint32_t num_levels;
    ivfc_level_hdr_t level_headers[IVFC_MAX_LEVEL];
    uint8_t salt_source[0x20];
} ivfc_save_hdr_t;

/* ScarletNX: an IVFC header's words between its file's little-endian order and the host's (the same both ways) */
static inline void ivfc_levels_swap(ivfc_level_hdr_t *levels, unsigned int count) {
    for (unsigned int i = 0; SNX_HOST_BE && i < count; i++) {
        snx_le64_array(&levels[i].logical_offset, 2);
        snx_le32_array(&levels[i].block_size, 2);
    }
}

static inline void ivfc_hdr_swap(ivfc_hdr_t *hdr) {
    snx_le32_array(&hdr->magic, 4);
    ivfc_levels_swap(hdr->level_headers, IVFC_MAX_LEVEL);
}

/* RomFS structs. */
#define ROMFS_HEADER_SIZE 0x00000050

typedef struct {
    uint64_t header_size;
    uint64_t dir_hash_table_offset;
    uint64_t dir_hash_table_size;
    uint64_t dir_meta_table_offset;
    uint64_t dir_meta_table_size;
    uint64_t file_hash_table_offset;
    uint64_t file_hash_table_size;
    uint64_t file_meta_table_offset;
    uint64_t file_meta_table_size;
    uint64_t data_offset;
} romfs_hdr_t;

typedef struct {
    uint32_t parent;
    uint32_t sibling;
    uint32_t child;
    uint32_t file;
    uint32_t hash;
    uint32_t name_size;
    char name[];
} romfs_direntry_t;

typedef struct {
    uint32_t parent;
    uint32_t sibling;
    uint64_t offset;
    uint64_t size;
    uint32_t hash;
    uint32_t name_size;
    char name[];
} romfs_fentry_t;

typedef struct {
    ivfc_hdr_t ivfc_header;
    uint8_t _0xE0[0x58];
} romfs_superblock_t;

typedef struct {
    romfs_superblock_t *superblock;
    FILE *file;
    hactool_ctx_t *tool_ctx;
    validity_t superblock_hash_validity;
    ivfc_level_ctx_t ivfc_levels[IVFC_MAX_LEVEL];
    uint64_t romfs_offset;
    romfs_hdr_t header;
    romfs_direntry_t *directories;
    romfs_fentry_t *files;
} romfs_ctx_t;

#define ROMFS_ENTRY_EMPTY 0xFFFFFFFF

/* the directory entry at offset: its fields copied into *entry in the host's order; its name, name_size bytes long and
   unterminated, at the address returned */
static inline const char *romfs_get_direntry(const romfs_direntry_t *directories, uint32_t offset, romfs_direntry_t *entry) {
    const char *at = (const char *)directories + offset;
    memcpy(entry, at, offsetof(romfs_direntry_t, name));
    snx_le32_array(entry, offsetof(romfs_direntry_t, name) / 4);
    return at + offsetof(romfs_direntry_t, name);
}

/* the file entry at offset: its fields copied into *entry in the host's order, as the table keeps entries 4 bytes apart
   and two of them are 64-bit; its name, name_size bytes long and unterminated, at the address returned */
static inline const char *romfs_get_fentry(const romfs_fentry_t *files, uint32_t offset, romfs_fentry_t *entry) {
    const char *at = (const char *)files + offset;
    entry->parent = snx_ld_le32(at);
    entry->sibling = snx_ld_le32(at + 4);
    entry->offset = snx_ld_le64(at + 8);
    entry->size = snx_ld_le64(at + 0x10);
    entry->hash = snx_ld_le32(at + 0x18);
    entry->name_size = snx_ld_le32(at + 0x1C);
    return at + offsetof(romfs_fentry_t, name);
}

void romfs_process(romfs_ctx_t *ctx);
void romfs_save(romfs_ctx_t *ctx);
void romfs_print(romfs_ctx_t *ctx);


#endif
