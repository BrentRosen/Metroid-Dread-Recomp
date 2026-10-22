/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* GDB's JIT interface for the JIT loader (code_loader.c): each object a load placed in the arena is handed to an
   attached GDB, which then names the functions in it (and has the object's .eh_frame to unwind with). Off unless
   asked (snx_gdb_jit_enable: the NX runtime's gdb_jit with the GDB stub): GDB stops the process at every
   registration to read the object, and each object's copy stays until its loader goes */
#ifndef SNX_RECOMPILER_LLVM_GDB_JIT_H
#define SNX_RECOMPILER_LLVM_GDB_JIT_H
#include <stddef.h>

/* Registration on, for the rest of the process: once, before the first load and the threads that load. Nothing on
   Windows, where the GDB stub is not (nx/runtime/gdbstub.c) and the loader takes COFF */
void snx_gdb_jit_enable(void);
/* A copy of the size bytes at object, a load's object file, for owner (a loader): the caller writes into it where the
   load put the object's parts (an ELF object's sh_addr), then hands it to snx_gdb_jit_add. NULL when registration is
   off or out of memory */
unsigned char* snx_gdb_jit_copy(const void* owner, const void* object, size_t size);
/* Registers copy (snx_gdb_jit_copy's) with GDB, until its owner's snx_gdb_jit_remove */
void snx_gdb_jit_add(unsigned char* copy);
/* Removes every object owner added: before its code goes */
void snx_gdb_jit_remove(const void* owner);
#endif
