/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "3ds/gpu/vk/spirv.h"
#include <stdlib.h>
#include <string.h>

enum {
    OP_EXTENSION = 10,
    OP_EXT_INST_IMPORT = 11,
    OP_MEMORY_MODEL = 14,
    OP_ENTRY_POINT = 15,
    OP_EXECUTION_MODE = 16,
    OP_CAPABILITY = 17,
    OP_CONSTANT_TRUE = 41,
    OP_CONSTANT_FALSE = 42,
    OP_CONSTANT = 43,
    OP_CONSTANT_COMPOSITE = 44,
    OP_FUNCTION = 54,
    OP_FUNCTION_PARAMETER = 55,
    OP_FUNCTION_END = 56,
    OP_VARIABLE = 59,
    OP_DECORATE = 71,
    OP_MEMBER_DECORATE = 72
};

static void put(SnxCtrSpv* s, SnxCtrSpvWords* w, uint32_t word) {
    if (s->failed) return;
    if (w->count == w->capacity) {
        size_t n = w->capacity ? w->capacity * 2 : 256;
        uint32_t* words = (uint32_t*)realloc(w->words, n * sizeof *words);
        if (!words) {
            s->failed = true;
            return;
        }
        w->words = words;
        w->capacity = n;
    }
    w->words[w->count++] = word;
}
static void instruction(SnxCtrSpv* s, SnxCtrSpvWords* w, uint32_t op, size_t words) {
    put(s, w, (uint32_t)words << 16 | op);
}
static void append(SnxCtrSpv* s, SnxCtrSpvWords* to, const SnxCtrSpvWords* from) {
    for (size_t i = 0; i < from->count; ++i) put(s, to, from->words[i]);
}

void snx_ctr_spv_init(SnxCtrSpv* s) {
    memset(s, 0, sizeof *s);
    s->next_id = 1;
    s->glsl = snx_ctr_spv_id(s);
    snx_ctr_spv_capability(s, SPV_CAPABILITY_SHADER);
}
void snx_ctr_spv_free(SnxCtrSpv* s) {
    SnxCtrSpvWords* all[] = {&s->capabilities, &s->extensions, &s->modes, &s->decorations, &s->globals,
                             &s->code,         &s->locals, &s->body};
    for (size_t i = 0; i < sizeof all / sizeof *all; ++i) free(all[i]->words);
    free(s->interface);
    free(s->known);
    memset(s, 0, sizeof *s);
}
uint32_t snx_ctr_spv_id(SnxCtrSpv* s) { return s->next_id++; }
void snx_ctr_spv_capability(SnxCtrSpv* s, uint32_t capability) {
    for (size_t i = 0; i + 1 < s->capabilities.count; i += 2)
        if (s->capabilities.words[i + 1] == capability) return;
    instruction(s, &s->capabilities, OP_CAPABILITY, 2);
    put(s, &s->capabilities, capability);
}

/* a string's words, terminated and padded with zeros */
static void put_string(SnxCtrSpv* s, SnxCtrSpvWords* w, const char* text) {
    size_t length = strlen(text) + 1;
    for (size_t i = 0; i < length; i += 4) {
        uint32_t word = 0;
        for (size_t b = 0; b < 4 && i + b < length; ++b) word |= (uint32_t)(unsigned char)text[i + b] << (b * 8);
        put(s, w, word);
    }
}
void snx_ctr_spv_extension(SnxCtrSpv* s, const char* name) {
    instruction(s, &s->extensions, OP_EXTENSION, 1 + (strlen(name) + 4) / 4);
    put_string(s, &s->extensions, name);
}

/* a type (the id first, at = 1) or constant (its type first, the id second, at = 2) of these words, made once */
static uint32_t known(SnxCtrSpv* s, uint32_t op, size_t at, const uint32_t* operands, size_t count) {
    uint64_t h = 0xcbf29ce484222325ull ^ op;
    for (size_t i = 0; i < count; ++i) h = (h ^ operands[i]) * 0x100000001b3ull;
    for (size_t k = 0; k < s->known_count; ++k) {
        if (s->known[k].hash != h) continue;
        const uint32_t* w = s->globals.words + s->known[k].at;
        if ((w[0] & 0xffff) != op || w[0] >> 16 != count + 2) continue;
        bool same = true;
        for (size_t i = 0, j = 1; i < count && same; ++i, ++j) {
            if (j == at) ++j;
            same = w[j] == operands[i];
        }
        if (same) return s->known[k].id;
    }
    if (s->known_count == s->known_capacity) {
        size_t n = s->known_capacity ? s->known_capacity * 2 : 128;
        void* p = realloc(s->known, n * sizeof *s->known);
        if (!p) {
            s->failed = true;
            return 0;
        }
        s->known = p;
        s->known_capacity = n;
    }
    uint32_t id = snx_ctr_spv_id(s);
    s->known[s->known_count].hash = h;
    s->known[s->known_count].at = s->globals.count;
    s->known[s->known_count++].id = id;
    instruction(s, &s->globals, op, count + 2);
    for (size_t i = 0, j = 1; j < count + 2; ++j) put(s, &s->globals, j == at ? id : operands[i++]);
    return id;
}

uint32_t snx_ctr_spv_void(SnxCtrSpv* s) { return known(s, SPV_OP_TYPE_VOID, 1, NULL, 0); }
uint32_t snx_ctr_spv_bool(SnxCtrSpv* s) { return known(s, SPV_OP_TYPE_BOOL, 1, NULL, 0); }
uint32_t snx_ctr_spv_int(SnxCtrSpv* s, bool is_signed) {
    uint32_t operands[2] = {32, is_signed};
    return known(s, SPV_OP_TYPE_INT, 1, operands, 2);
}
uint32_t snx_ctr_spv_float(SnxCtrSpv* s) {
    uint32_t width = 32;
    return known(s, SPV_OP_TYPE_FLOAT, 1, &width, 1);
}
uint32_t snx_ctr_spv_vector(SnxCtrSpv* s, uint32_t component, uint32_t count) {
    uint32_t operands[2] = {component, count};
    return known(s, SPV_OP_TYPE_VECTOR, 1, operands, 2);
}
uint32_t snx_ctr_spv_array(SnxCtrSpv* s, uint32_t element, uint32_t length) {
    uint32_t operands[2] = {element, snx_ctr_spv_uint(s, length)};
    return known(s, SPV_OP_TYPE_ARRAY, 1, operands, 2);
}
uint32_t snx_ctr_spv_pointer(SnxCtrSpv* s, uint32_t storage, uint32_t type) {
    uint32_t operands[2] = {storage, type};
    return known(s, SPV_OP_TYPE_POINTER, 1, operands, 2);
}
uint32_t snx_ctr_spv_function_type(SnxCtrSpv* s, uint32_t result, const uint32_t* params, size_t count) {
    uint32_t operands[17];
    if (count > 16) {
        s->failed = true;
        return 0;
    }
    operands[0] = result;
    for (size_t i = 0; i < count; ++i) operands[i + 1] = params[i];
    return known(s, SPV_OP_TYPE_FUNCTION, 1, operands, count + 1);
}
uint32_t snx_ctr_spv_image(SnxCtrSpv* s, uint32_t sampled_type, uint32_t dim, bool arrayed, uint32_t sampled,
                           uint32_t format) {
    uint32_t operands[7] = {sampled_type, dim, 0, arrayed, 0, sampled, format};
    return known(s, SPV_OP_TYPE_IMAGE, 1, operands, 7);
}
uint32_t snx_ctr_spv_sampled_image(SnxCtrSpv* s, uint32_t image) {
    return known(s, SPV_OP_TYPE_SAMPLED_IMAGE, 1, &image, 1);
}
uint32_t snx_ctr_spv_struct(SnxCtrSpv* s, const uint32_t* members, const uint32_t* offsets, size_t count) {
    uint32_t id = snx_ctr_spv_id(s);
    instruction(s, &s->globals, SPV_OP_TYPE_STRUCT, count + 2);
    put(s, &s->globals, id);
    for (size_t i = 0; i < count; ++i) put(s, &s->globals, members[i]);
    for (size_t i = 0; i < count; ++i) snx_ctr_spv_member_decorate(s, id, (uint32_t)i, SPV_DECORATION_OFFSET,
                                                                     &offsets[i], 1);
    return id;
}

uint32_t snx_ctr_spv_uint(SnxCtrSpv* s, uint32_t value) {
    uint32_t operands[2] = {snx_ctr_spv_int(s, false), value};
    return known(s, OP_CONSTANT, 2, operands, 2);
}
uint32_t snx_ctr_spv_sint(SnxCtrSpv* s, int32_t value) {
    uint32_t operands[2] = {snx_ctr_spv_int(s, true), (uint32_t)value};
    return known(s, OP_CONSTANT, 2, operands, 2);
}
uint32_t snx_ctr_spv_float_constant(SnxCtrSpv* s, float value) {
    uint32_t operands[2] = {snx_ctr_spv_float(s), 0};
    memcpy(&operands[1], &value, 4);
    return known(s, OP_CONSTANT, 2, operands, 2);
}
uint32_t snx_ctr_spv_bool_constant(SnxCtrSpv* s, bool value) {
    uint32_t type = snx_ctr_spv_bool(s);
    return known(s, value ? OP_CONSTANT_TRUE : OP_CONSTANT_FALSE, 2, &type, 1);
}
uint32_t snx_ctr_spv_composite(SnxCtrSpv* s, uint32_t type, const uint32_t* parts, size_t count) {
    uint32_t operands[17];
    if (count > 16) {
        s->failed = true;
        return 0;
    }
    operands[0] = type;
    for (size_t i = 0; i < count; ++i) operands[i + 1] = parts[i];
    return known(s, OP_CONSTANT_COMPOSITE, 2, operands, count + 1);
}

void snx_ctr_spv_decorate(SnxCtrSpv* s, uint32_t id, uint32_t decoration, const uint32_t* operands, size_t count) {
    instruction(s, &s->decorations, OP_DECORATE, count + 3);
    put(s, &s->decorations, id);
    put(s, &s->decorations, decoration);
    for (size_t i = 0; i < count; ++i) put(s, &s->decorations, operands[i]);
}
void snx_ctr_spv_member_decorate(SnxCtrSpv* s, uint32_t type, uint32_t member, uint32_t decoration,
                                 const uint32_t* operands, size_t count) {
    instruction(s, &s->decorations, OP_MEMBER_DECORATE, count + 4);
    put(s, &s->decorations, type);
    put(s, &s->decorations, member);
    put(s, &s->decorations, decoration);
    for (size_t i = 0; i < count; ++i) put(s, &s->decorations, operands[i]);
}
uint32_t snx_ctr_spv_global(SnxCtrSpv* s, uint32_t storage, uint32_t type) {
    uint32_t pointer = snx_ctr_spv_pointer(s, storage, type), id = snx_ctr_spv_id(s);
    instruction(s, &s->globals, OP_VARIABLE, 4);
    put(s, &s->globals, pointer);
    put(s, &s->globals, id);
    put(s, &s->globals, storage);
    if (storage == SPV_STORAGE_INPUT || storage == SPV_STORAGE_OUTPUT) {
        if (s->interface_count == s->interface_capacity) {
            size_t n = s->interface_capacity ? s->interface_capacity * 2 : 16;
            uint32_t* p = (uint32_t*)realloc(s->interface, n * sizeof *p);
            if (!p) {
                s->failed = true;
                return id;
            }
            s->interface = p;
            s->interface_capacity = n;
        }
        s->interface[s->interface_count++] = id;
    }
    return id;
}
uint32_t snx_ctr_spv_resource(SnxCtrSpv* s, uint32_t storage, uint32_t type, uint32_t set, uint32_t binding) {
    uint32_t id = snx_ctr_spv_global(s, storage, type);
    snx_ctr_spv_decorate(s, id, SPV_DECORATION_DESCRIPTOR_SET, &set, 1);
    snx_ctr_spv_decorate(s, id, SPV_DECORATION_BINDING, &binding, 1);
    return id;
}
uint32_t snx_ctr_spv_location(SnxCtrSpv* s, uint32_t storage, uint32_t type, uint32_t location) {
    uint32_t id = snx_ctr_spv_global(s, storage, type);
    snx_ctr_spv_decorate(s, id, SPV_DECORATION_LOCATION, &location, 1);
    return id;
}
uint32_t snx_ctr_spv_builtin(SnxCtrSpv* s, uint32_t storage, uint32_t type, uint32_t builtin) {
    uint32_t id = snx_ctr_spv_global(s, storage, type);
    snx_ctr_spv_decorate(s, id, SPV_DECORATION_BUILT_IN, &builtin, 1);
    return id;
}

uint32_t snx_ctr_spv_function(SnxCtrSpv* s, uint32_t id, uint32_t result, const uint32_t* param_types, size_t count,
                              uint32_t* params) {
    uint32_t type = snx_ctr_spv_function_type(s, result, param_types, count);
    if (!id) id = snx_ctr_spv_id(s);
    instruction(s, &s->code, OP_FUNCTION, 5);
    put(s, &s->code, result);
    put(s, &s->code, id);
    put(s, &s->code, 0);
    put(s, &s->code, type);
    for (size_t i = 0; i < count; ++i) {
        params[i] = snx_ctr_spv_id(s);
        instruction(s, &s->code, OP_FUNCTION_PARAMETER, 3);
        put(s, &s->code, param_types[i]);
        put(s, &s->code, params[i]);
    }
    instruction(s, &s->code, SPV_OP_LABEL, 2);
    put(s, &s->code, snx_ctr_spv_id(s));
    s->locals.count = s->body.count = 0;
    s->in_function = true;
    return id;
}
void snx_ctr_spv_function_end(SnxCtrSpv* s) {
    append(s, &s->code, &s->locals);
    append(s, &s->code, &s->body);
    instruction(s, &s->code, OP_FUNCTION_END, 1);
    s->locals.count = s->body.count = 0;
    s->in_function = false;
}
uint32_t snx_ctr_spv_local(SnxCtrSpv* s, uint32_t type) {
    uint32_t pointer = snx_ctr_spv_pointer(s, SPV_STORAGE_FUNCTION, type), id = snx_ctr_spv_id(s);
    instruction(s, &s->locals, OP_VARIABLE, 4);
    put(s, &s->locals, pointer);
    put(s, &s->locals, id);
    put(s, &s->locals, SPV_STORAGE_FUNCTION);
    return id;
}
uint32_t snx_ctr_spv_op(SnxCtrSpv* s, uint32_t opcode, uint32_t type, const uint32_t* operands, size_t count) {
    if (!type) {
        snx_ctr_spv_void_op(s, opcode, operands, count);
        return 0;
    }
    uint32_t id = snx_ctr_spv_id(s);
    instruction(s, &s->body, opcode, count + 3);
    put(s, &s->body, type);
    put(s, &s->body, id);
    for (size_t i = 0; i < count; ++i) put(s, &s->body, operands[i]);
    return id;
}
void snx_ctr_spv_void_op(SnxCtrSpv* s, uint32_t opcode, const uint32_t* operands, size_t count) {
    instruction(s, &s->body, opcode, count + 1);
    for (size_t i = 0; i < count; ++i) put(s, &s->body, operands[i]);
}
uint32_t snx_ctr_spv_ext(SnxCtrSpv* s, uint32_t type, uint32_t instruction_number, const uint32_t* operands,
                         size_t count) {
    uint32_t id = snx_ctr_spv_id(s);
    instruction(s, &s->body, SPV_OP_EXT_INST, count + 5);
    put(s, &s->body, type);
    put(s, &s->body, id);
    put(s, &s->body, s->glsl);
    put(s, &s->body, instruction_number);
    for (size_t i = 0; i < count; ++i) put(s, &s->body, operands[i]);
    return id;
}
void snx_ctr_spv_label(SnxCtrSpv* s, uint32_t label) {
    instruction(s, &s->body, SPV_OP_LABEL, 2);
    put(s, &s->body, label);
}

void snx_ctr_spv_entry(SnxCtrSpv* s, uint32_t model, uint32_t function) {
    s->model = model;
    s->main = function;
}
void snx_ctr_spv_mode(SnxCtrSpv* s, uint32_t function, uint32_t mode) {
    instruction(s, &s->modes, OP_EXECUTION_MODE, 3);
    put(s, &s->modes, function);
    put(s, &s->modes, mode);
}
void snx_ctr_spv_mode_operand(SnxCtrSpv* s, uint32_t function, uint32_t mode, uint32_t operand) {
    instruction(s, &s->modes, OP_EXECUTION_MODE, 4);
    put(s, &s->modes, function);
    put(s, &s->modes, mode);
    put(s, &s->modes, operand);
}
uint32_t* snx_ctr_spv_finish(SnxCtrSpv* s, size_t* count) {
    SnxCtrSpvWords out = {0};
    put(s, &out, 0x07230203u);
    put(s, &out, 0x00010300u); /* SPIR-V 1.3 */
    put(s, &out, 0);
    put(s, &out, s->next_id);
    put(s, &out, 0);
    append(s, &out, &s->capabilities);
    append(s, &out, &s->extensions);
    instruction(s, &out, OP_EXT_INST_IMPORT, 2 + 4); /* "GLSL.std.450" and its terminator: 4 words */
    put(s, &out, s->glsl);
    put_string(s, &out, "GLSL.std.450");
    instruction(s, &out, OP_MEMORY_MODEL, 3);
    put(s, &out, 0); /* Logical */
    put(s, &out, 1); /* GLSL450 */
    instruction(s, &out, OP_ENTRY_POINT, 3 + 2 + s->interface_count);
    put(s, &out, s->model);
    put(s, &out, s->main);
    put_string(s, &out, "main");
    for (size_t i = 0; i < s->interface_count; ++i) put(s, &out, s->interface[i]);
    append(s, &out, &s->modes);
    append(s, &out, &s->decorations);
    append(s, &out, &s->globals);
    append(s, &out, &s->code);
    if (s->failed) {
        free(out.words);
        return NULL;
    }
    *count = out.count;
    return out.words;
}
