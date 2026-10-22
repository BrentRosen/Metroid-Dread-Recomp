/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors
   The C function emitter kept for the reference tests. The production path is the LLVM backend; this file is built
   into snx_a64_reference only. */
#include "common/cpu/recompiler/codegen.h"
#include "common/cpu/aarch64/a64_emit.h"
#include <stdio.h>

static void function_name(FILE* out, SnxU64 address) {
    fputs("F_", out);
    snx_a64_hex(out, address);
}
static void label(FILE* out, SnxU64 address) {
    fputs("L_", out);
    snx_a64_hex(out, address);
}

static void call(FILE* out, const SnxCodegen* codegen, SnxU64 target, int tail) {
    const SnxFunction* function;
    function = snx_codegen_function(codegen, target);
    if (tail) fputs("{ ", out);
    /* --hook-direct-calls: through CPU.disp even to a known function, where runtime wrappers can see the call */
    if (function && !codegen->hook_direct_calls) {
        function_name(out, target);
        fputs("(c,", out);
    } else
        fputs("CALLDYN(", out);
    snx_a64_literal(out, target);
    fputs(");", out);
    if (tail) fputs(" return; }", out);
}

/* a backward branch is how a loop goes round: there a thread that makes no SVC can still be taken off its core */
static void poll(FILE* out, SnxU64 resume) {
    fputs("if(UNLIKELY(__atomic_load_n(&c->poll,__ATOMIC_RELAXED))){ c->pc=", out);
    snx_a64_literal(out, resume);
    fputs("; c->rt->poll(c); } ", out);
}

static void branch(FILE* out, const SnxCodegen* codegen, const SnxFunction* function, SnxU64 address, SnxU64 target) {
    if (snx_function_contains(function, target)) {
        if (target <= address) {
            fputs("{ ", out);
            poll(out, target);
        }
        fputs("goto ", out);
        label(out, target);
        fputc(';', out);
        if (target <= address) fputs(" }", out);
    } else
        call(out, codegen, target, 1);
}

void snx_codegen_emit(FILE* out, SnxCodegen* codegen, const SnxFunction* function) {
    size_t i;
    SnxU64 address, next, target;
    SnxU32 word;
    const SnxA64Case* c;
    int dispatch, terminal;
    fputs("void ", out);
    function_name(out, function->start);
    fputs("(CPU* RESTRICT c,u64 e){\n u8* const M=c->mem;\n", out);
    fputs(" if(UNLIKELY(e!=", out);
    snx_a64_literal(out, function->start);
    dispatch = function->leader_count > 1;
    fputs(dispatch ? ")){ c->pc=e; goto D_; }\n" : ")){ CALLDYN(e); return; }\n", out);
    if (function->count && snx_function_address(function, 0) != function->start) {
        fputs(" goto ", out);
        label(out, function->start);
        fputs(";\n", out);
    }
    for (i = 0; i < function->count; ++i) {
        address = snx_function_address(function, i);
        next = address + 4;
        if (function->leaders[i]) {
            label(out, address);
            fputs(":;\n", out);
        }
        word = 0;
        if (!snx_codegen_word(codegen, address, &word))
            c = snx_a64_decode(0);
        else
            c = snx_a64_decode(word);
        if (codegen->check_fetch) {
            fputs(" {u32 live; snx_fetch(c,", out);
            snx_a64_literal(out, address);
            fprintf(out, ",&live); if(live!=0x%08lXu){snx_interp(c,", (unsigned long)word);
            snx_a64_literal(out, address);
            fputs(");return;}}\n", out);
        }
        if (c->kind == SNX_FLOW_UNDEF) {
            fputs(" c->pc=", out);
            snx_a64_literal(out, address);
            fputs("; c->rt->undef(c,", out);
            snx_a64_literal(out, address);
            fprintf(out, ",0x%08lXu); return;\n", (unsigned long)word);
            continue;
        }
        terminal = 0;
        snx_a64_scope(out, word, address, c);
        switch (c->kind) {
            case SNX_FLOW_PLAIN:
                if (!snx_a64_fast_simd(out, word)) snx_a64_body(out, c->first, "c->rt->undef(c,pc,w)");
                break;
            case SNX_FLOW_B:
                branch(out, codegen, function, address, snx_a64_target(word, address));
                terminal = 1;
                break;
            case SNX_FLOW_BC:
                fprintf(out, "if(%s) ", c->first);
                branch(out, codegen, function, address, snx_a64_target(word, address));
                break;
            case SNX_FLOW_BL:
                target = snx_a64_target(word, address);
                fputs("c->x[30]=pc+4; ", out);
                call(out, codegen, target, 0);
                fputs(" if(UNLIKELY(c->pc!=pc+4)) return;", out);
                break;
            case SNX_FLOW_BLR:
                fprintf(out, "{ u64 t_=%s; c->x[30]=pc+4; CALLDYN(t_); if(UNLIKELY(c->pc!=pc+4)) return; }", c->first);
                break;
            case SNX_FLOW_BR:
                if (dispatch)
                    fprintf(out, "c->pc=%s; goto D_;", c->first);
                else
                    fprintf(out, "{ u64 t_=%s; CALLDYN(t_); return; }", c->first);
                terminal = 1;
                break;
            case SNX_FLOW_RET:
                fprintf(out, "c->pc=%s; return;", c->first);
                terminal = 1;
                break;
            case SNX_FLOW_SVC: fprintf(out, "c->pc=pc+4; c->rt->svc(c,%s);", c->first); break;
            case SNX_FLOW_BRK:
                fprintf(out, "c->pc=pc; c->rt->brk(c,pc,%s); return;", c->first);
                terminal = 1;
                break;
            case SNX_FLOW_UNDEF: break;
        }
        fputs(" }\n", out);
        if (!terminal && (!snx_function_contains(function, next) ||
                          (i + 1 < function->count && snx_function_address(function, i + 1) != next))) {
            fputc(' ', out);
            call(out, codegen, next, 1);
            fputc('\n', out);
        }
    }
    if (dispatch) {
        fputs(" D_: if(UNLIKELY(__atomic_load_n(&c->poll,__ATOMIC_RELAXED))) c->rt->poll(c);\n switch(c->pc){\n", out);
        for (i = 0; i < function->count; ++i)
            if (function->leaders[i]) {
                address = snx_function_address(function, i);
                fputs(" case ", out);
                snx_a64_literal(out, address);
                fputs(": goto ", out);
                label(out, address);
                fputs(";\n", out);
            }
        fputs(" default: CALLDYN(c->pc); return; }\n", out);
    }
    fputs("}\n", out);
}
