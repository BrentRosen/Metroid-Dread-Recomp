/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* LLVM's settings (settings.txt's llvm table), which tools/settingsgen generates settings_llvm.c with from what the LLVM
   being built against has: the pipelines its opt printed, a toggle for each of their passes, and the options its C API
   sets. The recompile hands them to the LLVM backend through snx_llvm_tuning (nx/loader/module_llvm.h) */
#ifndef SNX_SETTINGS_LLVM_H
#define SNX_SETTINGS_LLVM_H
#include "snx_settings.h"
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <stddef.h>

/* opt's pipeline for -O<level> (0123sz), NULL without one (no opt of this LLVM ran where it was built) */
const char* settings_llvm_pipeline(char level);
/* the pass of that name (length bytes): 1 while s has it on, 0 off, -1 for no toggle */
int settings_llvm_pass(const Settings* s, const char* name, size_t length);
/* the options s sets, on the pass builder's options and on the target machine: LLVM's own for the others */
void settings_llvm_builder(const Settings* s, LLVMPassBuilderOptionsRef options);
void settings_llvm_machine(const Settings* s, LLVMTargetMachineRef machine);
#endif
