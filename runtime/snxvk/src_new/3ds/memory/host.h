/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_CTR_HOST_MEMORY_H
#define SNX_CTR_HOST_MEMORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* The host's side of the 3DS's flat address space (memory.c): the board's RAM as one host memory object that can be
   mapped at several host addresses at once, a 4 GiB reservation the guest's addresses are offsets into, and its pages
   mapped there as views of that object, so every guest address of a physical page (a heap page and its MAP mirror,
   shared memory and its service, the linear heap) is the same memory. Linux and Android use a memfd, the other POSIX
   systems an unlinked shm_open object, Windows a pagefile section mapped into placeholders (Windows 10 1803). */

/* whether the host can: 4 KiB pages (the 3DS's, the granularity of every mapping), and the calls above */
bool ctr_host_flat(void);
/* the guest address space's size, every 32-bit address, and the reservation's, with an inaccessible guard after it */
#define CTR_HOST_SPACE (1ull << 32)
#define CTR_HOST_GUARD (1ull << 16)
typedef struct {
    intptr_t handle;     /* the object: a file descriptor, or a Windows section HANDLE */
    unsigned char* view; /* all of it mapped once, readable and writable: the physical memory HLE and the GPU use */
    uint64_t size;
} CtrHostMemory;
bool ctr_host_memory_new(CtrHostMemory* memory, uint64_t size);
void ctr_host_memory_free(CtrHostMemory* memory);
/* CTR_HOST_SPACE + CTR_HOST_GUARD bytes, inaccessible; NULL on failure */
unsigned char* ctr_host_reserve(void);
void ctr_host_release(unsigned char* base);
/* permissions as the guest's (SNX_CTR_READ | SNX_CTR_WRITE; execute is read: blocks are compiled, never run there) */
bool ctr_host_map(const CtrHostMemory* memory, unsigned char* base, uint32_t va, uint64_t offset, uint32_t size,
                  unsigned permissions);
bool ctr_host_protect(unsigned char* base, uint32_t va, uint32_t size, unsigned permissions);
/* back to reserved and inaccessible */
bool ctr_host_unmap(unsigned char* base, uint32_t va, uint32_t size);
/* A host fault in [base, base + CTR_HOST_SPACE + CTR_HOST_GUARD) goes to resolve first (write tracking's,
   snx_ctr_track_fault): when it says the page was a tracked one, now open enough, the access is made again. Otherwise
   it is the guest's data abort: the run cannot go on from the middle of a compiled block, so the handler reports the
   guest address, and *pc (the block the CPU entered) when pc is not NULL, and ends the process. Called on the thread
   that runs the guest, which faults on its own stack: on POSIX hosts (Linux, FreeBSD, the others) the handler runs on
   an alternate stack this gives the thread, large enough for a resolution that waits for the GPU. One space at a
   time; base NULL takes the handler away */
void ctr_host_faults(unsigned char* base, const uint32_t* pc, bool (*resolve)(void* context, uint32_t va),
                     void* context);
/* a line to the standard error, from a fault handler (no stdio) */
void ctr_host_say(const char* text, size_t size);
#endif
