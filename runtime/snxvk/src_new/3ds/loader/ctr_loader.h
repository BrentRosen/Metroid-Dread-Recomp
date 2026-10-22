/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_CTR_LOADER_H
#define SNX_CTR_LOADER_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    SNX_CTR_EXTRACT_OK,
    SNX_CTR_EXTRACT_IO,
    SNX_CTR_EXTRACT_FORMAT,
    SNX_CTR_EXTRACT_UNSUPPORTED,
    SNX_CTR_EXTRACT_LIMIT,
    SNX_CTR_EXTRACT_EXISTS
} SnxCtrExtractResult;
/* Decrypted application CIAs only. RSA/ECDSA trust and encrypted layers are not
   implemented. Hash checks are integrity checks, NOT signature authentication.
   Output is a NEW directory, published only after complete extraction. NULL
   destination selects <work>/<title-id>-<full-input-sha256>, work/3ds when work
   is NULL. Existing outputs are refused, never treated as a cache hit. Parent
   directory and input must be trusted and not concurrently modified. published
   is owned by caller on OK. */
SnxCtrExtractResult snx_ctr_extract(const char* input, const char* destination, const char* work, char** published,
                                    char* error, size_t error_size);
/* A decrypted application CIA read where it lies, with nothing extracted: the file itself mapped read-only and
   shared for as long as it is open, after every check snx_ctr_extract makes (hashes, bounds, the RomFS metadata).
   Encrypted content is refused as the extraction refuses it. *cia is the caller's on OK. The input must not be
   modified while it is open: the mapping shows what the file holds */
typedef struct SnxCtrCia SnxCtrCia;
SnxCtrExtractResult snx_ctr_cia_open(const char* input, SnxCtrCia** cia, char* error, size_t error_size);
void snx_ctr_cia_close(SnxCtrCia* cia);
uint64_t snx_ctr_cia_title(const SnxCtrCia* cia);
/* the CIA file's modification time, UTC Unix milliseconds, 0 when the host can't say */
uint64_t snx_ctr_cia_mtime(const SnxCtrCia* cia);
/* the boot NCCH's ExHeader (0x800 bytes, as exheader.bin holds it) and its .code expanded to its segments' pages (as
   exefs/.code holds it) */
const unsigned char* snx_ctr_cia_exheader(const SnxCtrCia* cia);
const unsigned char* snx_ctr_cia_code(const SnxCtrCia* cia, size_t* size);
/* an ExeFS file as the CIA stores it (.code compressed), in the mapping; NULL with *size 0 for none by that name */
const unsigned char* snx_ctr_cia_exefs(const SnxCtrCia* cia, const char* name, uint64_t* size);
/* the RomFS's level 3, the image the guest reads, in the mapping; NULL with *size 0 without a RomFS */
const unsigned char* snx_ctr_cia_romfs(const SnxCtrCia* cia, uint64_t* size);
/* An encrypted application CIA decrypted into output, published only complete, which then reads as a decrypted CIA:
   its contents' title-key layer and their NCCHs' AES-CTR layer taken off, the NCCHs marked NoCrypto, the TMD's hashes
   those of the decrypted contents. keys is an aes_keys.txt in Azahar's format (the generator constant, the KeyX of
   slots 0x2C, 0x3D and the secondary key's, the common KeyYs). A CIA with nothing encrypted is left alone,
   *encrypted false and nothing written. Seed crypto (seeddb) is refused */
SnxCtrExtractResult snx_ctr_cia_decrypt(const char* input, const char* keys, const char* output, bool* encrypted,
                                        char* error, size_t error_size);
/* ExeFS backward LZSS/BLZ, exact bounded expansion; output_size is authoritative.
   The input and output buffers must not overlap. */
bool snx_ctr_blz(const unsigned char* input, size_t input_size, unsigned char* output, size_t output_size);
#endif
