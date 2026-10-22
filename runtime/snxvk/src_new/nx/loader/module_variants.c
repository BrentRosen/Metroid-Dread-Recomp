/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "nx/loader/module_variants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LLVMValueRef global(SnxIr* ir, LLVMTypeRef type, const char* name, LLVMValueRef value, int constant) {
    LLVMValueRef g = LLVMAddGlobal(ir->mod, type, name);
    LLVMSetLinkage(g, LLVMInternalLinkage);
    LLVMSetInitializer(g, value ? value : LLVMConstNull(type));
    LLVMSetGlobalConstant(g, constant);
    return g;
}

static LLVMValueRef const_array(SnxIr* ir, LLVMTypeRef element, LLVMValueRef* values, size_t count) {
    return global(ir, LLVMArrayType(element, (unsigned)count), "", LLVMConstArray(element, values, (unsigned)count), 1);
}

int snx_module_variants_emit(SnxIr* ir, const SnxJitCache* cache, size_t* count, char* error, size_t error_size) {
    LLVMTypeRef run_type, function_type, variants_type;
    LLVMValueRef *functions = NULL, variants_array, variants_global, fn, table, slot;
    LLVMValueRef fields[8], run_fields[2], variants_fields[3], indices[2], args[2];
    size_t total = 0, at = 0, i, j;
    char name[64];
    if (!ir || !cache || !count) {
        snprintf(error, error_size, "no IR or cache");
        return -1;
    }
    *count = 0;
    for (i = 0; i < snx_jit_cache_count(cache); ++i) {
        const SnxJitCacheRecord* record = snx_jit_cache_record(cache, i);
        if (record) total += record->function_count;
    }
    if (!total) return 0;
    if (total > 0xFFFFFFFFu) {
        snprintf(error, error_size, "the variant table is too large");
        return -1;
    }
    run_type = LLVMStructTypeInContext(ir->ctx, (LLVMTypeRef[]){ir->t_i64, ir->t_i32}, 2, 0);
    function_type = LLVMStructTypeInContext(
        ir->ctx, (LLVMTypeRef[]){ir->t_i64, ir->t_i32, ir->t_ptr, ir->t_i32, ir->t_ptr, ir->t_i64, ir->t_i64, ir->t_ptr},
        8, 0);
    variants_type = LLVMStructTypeInContext(ir->ctx, (LLVMTypeRef[]){ir->t_i32, ir->t_i32, ir->t_ptr}, 3, 0);
    functions = (LLVMValueRef*)malloc(total * sizeof(*functions));
    if (!functions) {
        snprintf(error, error_size, "out of memory");
        return -1;
    }
    for (i = 0; i < snx_jit_cache_count(cache); ++i) {
        const SnxJitCacheRecord* record = snx_jit_cache_record(cache, i);
        LLVMTypeRef table_type;
        LLVMValueRef *values;
        if (!record || !record->function_count) continue;
        table_type = LLVMArrayType(ir->t_ptr, (unsigned)record->function_count);
        table = LLVMAddGlobal(ir->mod, table_type, record->name);
        LLVMSetGlobalConstant(table, 1);
        LLVMSetVisibility(table, LLVMHiddenVisibility);
        for (j = 0; j < record->function_count; ++j, ++at) {
            const SnxJitCacheFunction* function = &record->functions[j];
            LLVMValueRef leaders = LLVMConstNull(ir->t_ptr), runs = LLVMConstNull(ir->t_ptr), wrapper;
            if (function->leader_count) {
                values = (LLVMValueRef*)malloc(function->leader_count * sizeof(*values));
                if (!values) {
                    free(functions);
                    snprintf(error, error_size, "out of memory");
                    return -1;
                }
                for (size_t k = 0; k < function->leader_count; ++k) values[k] = ir_i64(ir, function->leaders[k]);
                leaders = const_array(ir, ir->t_i64, values, function->leader_count);
                free(values);
            }
            if (function->run_count) {
                values = (LLVMValueRef*)malloc(function->run_count * sizeof(*values));
                if (!values) {
                    free(functions);
                    snprintf(error, error_size, "out of memory");
                    return -1;
                }
                for (size_t k = 0; k < function->run_count; ++k) {
                    run_fields[0] = ir_i64(ir, function->runs[k].address);
                    run_fields[1] = ir_i32(ir, function->runs[k].words);
                    values[k] = LLVMConstNamedStruct(run_type, run_fields, 2);
                }
                runs = const_array(ir, run_type, values, function->run_count);
                free(values);
            }
            snprintf(name, sizeof(name), "snx_variant_%lu_%lu", (unsigned long)i, (unsigned long)j);
            wrapper = LLVMAddFunction(ir->mod, name, ir->t_guest);
            LLVMSetLinkage(wrapper, LLVMInternalLinkage);
            LLVMPositionBuilderAtEnd(ir->b, LLVMAppendBasicBlockInContext(ir->ctx, wrapper, "entry"));
            indices[0] = ir_i64(ir, 0);
            indices[1] = ir_i64(ir, j);
            slot = LLVMConstInBoundsGEP2(table_type, table, indices, 2);
            fn = LLVMBuildLoad2(ir->b, ir->t_ptr, slot, "");
            args[0] = LLVMGetParam(wrapper, 0);
            args[1] = LLVMGetParam(wrapper, 1);
            fn = LLVMBuildCall2(ir->b, ir->t_guest, fn, args, 2, "");
            LLVMSetTailCallKind(fn, LLVMTailCallKindTail);
            LLVMBuildRetVoid(ir->b);
            fields[0] = ir_i64(ir, function->start);
            fields[1] = ir_i32(ir, (SnxU32)function->leader_count);
            fields[2] = leaders;
            fields[3] = ir_i32(ir, (SnxU32)function->run_count);
            fields[4] = runs;
            fields[5] = ir_i64(ir, function->h1);
            fields[6] = ir_i64(ir, function->h2);
            fields[7] = LLVMConstPointerCast(wrapper, ir->t_ptr);
            functions[at] = LLVMConstNamedStruct(function_type, fields, 8);
        }
    }
    variants_array = const_array(ir, function_type, functions, total);
    variants_fields[0] = ir_i32(ir, SNX_VARIANTS_VERSION);
    variants_fields[1] = ir_i32(ir, (SnxU32)total);
    variants_fields[2] = variants_array;
    variants_global = global(ir, variants_type, "", LLVMConstNamedStruct(variants_type, variants_fields, 3), 1);
    free(functions);
    fn = LLVMAddFunction(ir->mod, "snx_variants", LLVMFunctionType(ir->t_ptr, NULL, 0, 0));
    if (ir->windows) LLVMSetDLLStorageClass(fn, LLVMDLLExportStorageClass);
    ir->fn = fn;
    LLVMPositionBuilderAtEnd(ir->b, LLVMAppendBasicBlockInContext(ir->ctx, fn, "entry"));
    LLVMBuildRet(ir->b, variants_global);
    *count = total;
    return 0;
}
