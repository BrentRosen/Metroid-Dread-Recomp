/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_CTR_SPIRV_H
#define SNX_CTR_SPIRV_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* A SPIR-V module written as it is generated (spirv.c), for the vertex and fragment shaders vsgen.c and fsgen.c make
   while the game runs: SPIR-V 1.3 for Vulkan 1.3, the Shader capability, the GLSL.std.450 instructions and the
   Logical GLSL450 memory model. Types and constants are made once each (asking again gives the same id); everything
   else is the caller's instructions, in the order SPIR-V wants them within a function. A function's variables are
   gathered into its first block whenever they are declared. Running out of memory sets failed, and the module then
   finishes as NULL. */
typedef struct {
    uint32_t* words;
    size_t count, capacity;
} SnxCtrSpvWords;
typedef struct {
    SnxCtrSpvWords capabilities, extensions, modes, decorations, globals, code, locals, body;
    uint32_t next_id;
    uint32_t model, main; /* the entry point */
    uint32_t glsl; /* the GLSL.std.450 import */
    uint32_t* interface;
    size_t interface_count, interface_capacity;
    /* the types and constants made, by their instruction's words: index into globals, and their id */
    struct {
        uint64_t hash;
        size_t at;
        uint32_t id;
    }* known;
    size_t known_count, known_capacity;
    bool failed, in_function;
} SnxCtrSpv;

/* the opcodes, storage classes, decorations and built-ins the generators use (the SPIR-V specification's numbers) */
enum {
    SPV_OP_EXT_INST = 12,
    SPV_OP_TYPE_VOID = 19,
    SPV_OP_TYPE_BOOL = 20,
    SPV_OP_TYPE_INT = 21,
    SPV_OP_TYPE_FLOAT = 22,
    SPV_OP_TYPE_VECTOR = 23,
    SPV_OP_TYPE_IMAGE = 25,
    SPV_OP_TYPE_SAMPLED_IMAGE = 27,
    SPV_OP_TYPE_ARRAY = 28,
    SPV_OP_TYPE_STRUCT = 30,
    SPV_OP_TYPE_POINTER = 32,
    SPV_OP_TYPE_FUNCTION = 33,
    SPV_OP_FUNCTION_CALL = 57,
    SPV_OP_LOAD = 61,
    SPV_OP_STORE = 62,
    SPV_OP_ACCESS_CHAIN = 65,
    SPV_OP_VECTOR_SHUFFLE = 79,
    SPV_OP_COMPOSITE_CONSTRUCT = 80,
    SPV_OP_COMPOSITE_EXTRACT = 81,
    SPV_OP_COMPOSITE_INSERT = 82,
    SPV_OP_SAMPLED_IMAGE = 86,
    SPV_OP_IMAGE_SAMPLE_IMPLICIT_LOD = 87,
    SPV_OP_IMAGE_SAMPLE_EXPLICIT_LOD = 88,
    SPV_OP_IMAGE_SAMPLE_PROJ_IMPLICIT_LOD = 91,
    SPV_OP_IMAGE_FETCH = 95,
    SPV_OP_IMAGE_READ = 98,
    SPV_OP_IMAGE_WRITE = 99,
    SPV_OP_IMAGE = 100,
    SPV_OP_IMAGE_QUERY_SIZE_LOD = 103,
    SPV_OP_IMAGE_QUERY_SIZE = 104,
    SPV_OP_CONVERT_F_TO_U = 109,
    SPV_OP_CONVERT_F_TO_S = 110,
    SPV_OP_CONVERT_S_TO_F = 111,
    SPV_OP_CONVERT_U_TO_F = 112,
    SPV_OP_BITCAST = 124,
    SPV_OP_S_NEGATE = 126,
    SPV_OP_F_NEGATE = 127,
    SPV_OP_I_ADD = 128,
    SPV_OP_F_ADD = 129,
    SPV_OP_I_SUB = 130,
    SPV_OP_F_SUB = 131,
    SPV_OP_I_MUL = 132,
    SPV_OP_F_MUL = 133,
    SPV_OP_U_DIV = 134,
    SPV_OP_S_DIV = 135,
    SPV_OP_F_DIV = 136,
    SPV_OP_U_MOD = 137,
    SPV_OP_S_MOD = 139,
    SPV_OP_F_MOD = 141,
    SPV_OP_VECTOR_TIMES_SCALAR = 142,
    SPV_OP_DOT = 148,
    SPV_OP_ANY = 154,
    SPV_OP_ALL = 155,
    SPV_OP_IS_NAN = 156,
    SPV_OP_IS_INF = 157,
    SPV_OP_LOGICAL_EQUAL = 164,
    SPV_OP_LOGICAL_NOT_EQUAL = 165,
    SPV_OP_LOGICAL_OR = 166,
    SPV_OP_LOGICAL_AND = 167,
    SPV_OP_LOGICAL_NOT = 168,
    SPV_OP_SELECT = 169,
    SPV_OP_I_EQUAL = 170,
    SPV_OP_I_NOT_EQUAL = 171,
    SPV_OP_U_GREATER_THAN = 172,
    SPV_OP_S_GREATER_THAN = 173,
    SPV_OP_U_GREATER_THAN_EQUAL = 174,
    SPV_OP_S_GREATER_THAN_EQUAL = 175,
    SPV_OP_U_LESS_THAN = 176,
    SPV_OP_S_LESS_THAN = 177,
    SPV_OP_U_LESS_THAN_EQUAL = 178,
    SPV_OP_S_LESS_THAN_EQUAL = 179,
    SPV_OP_F_ORD_EQUAL = 180,
    SPV_OP_F_UNORD_NOT_EQUAL = 183,
    SPV_OP_F_ORD_LESS_THAN = 184,
    SPV_OP_F_ORD_GREATER_THAN = 186,
    SPV_OP_F_ORD_LESS_THAN_EQUAL = 188,
    SPV_OP_F_ORD_GREATER_THAN_EQUAL = 190,
    SPV_OP_SHIFT_RIGHT_LOGICAL = 194,
    SPV_OP_SHIFT_RIGHT_ARITHMETIC = 195,
    SPV_OP_SHIFT_LEFT_LOGICAL = 196,
    SPV_OP_BITWISE_OR = 197,
    SPV_OP_BITWISE_XOR = 198,
    SPV_OP_BITWISE_AND = 199,
    SPV_OP_NOT = 200,
    SPV_OP_BIT_FIELD_INSERT = 201,
    SPV_OP_BIT_FIELD_S_EXTRACT = 202,
    SPV_OP_BIT_FIELD_U_EXTRACT = 203,
    SPV_OP_DPDX = 207,
    SPV_OP_DPDY = 208,
    SPV_OP_FWIDTH = 209,
    SPV_OP_ATOMIC_COMPARE_EXCHANGE = 230,
    SPV_OP_PHI = 245,
    SPV_OP_LOOP_MERGE = 246,
    SPV_OP_SELECTION_MERGE = 247,
    SPV_OP_LABEL = 248,
    SPV_OP_BRANCH = 249,
    SPV_OP_BRANCH_CONDITIONAL = 250,
    SPV_OP_SWITCH = 251,
    SPV_OP_KILL = 252,
    SPV_OP_RETURN = 253,
    SPV_OP_RETURN_VALUE = 254,
    SPV_OP_UNREACHABLE = 255
};
enum {
    SPV_STORAGE_UNIFORM_CONSTANT = 0,
    SPV_STORAGE_INPUT = 1,
    SPV_STORAGE_UNIFORM = 2,
    SPV_STORAGE_OUTPUT = 3,
    SPV_STORAGE_PRIVATE = 6,
    SPV_STORAGE_FUNCTION = 7,
    SPV_STORAGE_PUSH_CONSTANT = 9,
    SPV_STORAGE_IMAGE = 11
};
enum {
    SPV_DECORATION_BLOCK = 2,
    SPV_DECORATION_ARRAY_STRIDE = 6,
    SPV_DECORATION_BUILT_IN = 11,
    SPV_DECORATION_FLAT = 14,
    SPV_DECORATION_LOCATION = 30,
    SPV_DECORATION_BINDING = 33,
    SPV_DECORATION_DESCRIPTOR_SET = 34,
    SPV_DECORATION_OFFSET = 35
};
enum {
    SPV_BUILT_IN_POSITION = 0,
    SPV_BUILT_IN_CLIP_DISTANCE = 3,
    SPV_BUILT_IN_FRAG_COORD = 15,
    SPV_BUILT_IN_FRONT_FACING = 17,
    SPV_BUILT_IN_FRAG_DEPTH = 22
};
enum { SPV_MODEL_VERTEX = 0, SPV_MODEL_FRAGMENT = 4 };
enum {
    SPV_MODE_ORIGIN_UPPER_LEFT = 7,
    SPV_MODE_DEPTH_REPLACING = 12,
    SPV_MODE_SIGNED_ZERO_INF_NAN_PRESERVE = 4461 /* SPV_KHR_float_controls, its operand the width */
};
enum {
    SPV_CAPABILITY_SHADER = 1,
    SPV_CAPABILITY_SAMPLED_BUFFER = 46,
    SPV_CAPABILITY_IMAGE_QUERY = 50,
    SPV_CAPABILITY_CLIP_DISTANCE = 32,
    SPV_CAPABILITY_SIGNED_ZERO_INF_NAN_PRESERVE = 4466
};
/* image dimensions: OpTypeImage's Dim */
enum { SPV_DIM_2D = 1, SPV_DIM_CUBE = 3, SPV_DIM_BUFFER = 5 };
/* the GLSL.std.450 instructions */
enum {
    SPV_GLSL_ROUND = 1,
    SPV_GLSL_ROUND_EVEN = 2,
    SPV_GLSL_TRUNC = 3,
    SPV_GLSL_F_ABS = 4,
    SPV_GLSL_S_ABS = 5,
    SPV_GLSL_F_SIGN = 6,
    SPV_GLSL_FLOOR = 8,
    SPV_GLSL_CEIL = 9,
    SPV_GLSL_FRACT = 10,
    SPV_GLSL_SIN = 13,
    SPV_GLSL_COS = 14,
    SPV_GLSL_POW = 26,
    SPV_GLSL_EXP = 27,
    SPV_GLSL_LOG = 28,
    SPV_GLSL_EXP2 = 29,
    SPV_GLSL_LOG2 = 30,
    SPV_GLSL_SQRT = 31,
    SPV_GLSL_INVERSE_SQRT = 32,
    SPV_GLSL_F_MIN = 37,
    SPV_GLSL_U_MIN = 38,
    SPV_GLSL_S_MIN = 39,
    SPV_GLSL_F_MAX = 40,
    SPV_GLSL_U_MAX = 41,
    SPV_GLSL_S_MAX = 42,
    SPV_GLSL_F_CLAMP = 43,
    SPV_GLSL_U_CLAMP = 44,
    SPV_GLSL_S_CLAMP = 45,
    SPV_GLSL_F_MIX = 46,
    SPV_GLSL_STEP = 48,
    SPV_GLSL_SMOOTH_STEP = 49,
    SPV_GLSL_FMA = 50,
    SPV_GLSL_LENGTH = 66,
    SPV_GLSL_DISTANCE = 67,
    SPV_GLSL_CROSS = 68,
    SPV_GLSL_NORMALIZE = 69,
    SPV_GLSL_REFLECT = 71,
    SPV_GLSL_N_MIN = 79,
    SPV_GLSL_N_MAX = 80,
    SPV_GLSL_N_CLAMP = 81
};

void snx_ctr_spv_init(SnxCtrSpv* s);
void snx_ctr_spv_free(SnxCtrSpv* s);
uint32_t snx_ctr_spv_id(SnxCtrSpv* s);
void snx_ctr_spv_capability(SnxCtrSpv* s, uint32_t capability);
void snx_ctr_spv_extension(SnxCtrSpv* s, const char* name);

/* types, made once each */
uint32_t snx_ctr_spv_void(SnxCtrSpv* s);
uint32_t snx_ctr_spv_bool(SnxCtrSpv* s);
uint32_t snx_ctr_spv_int(SnxCtrSpv* s, bool is_signed); /* 32 bits */
uint32_t snx_ctr_spv_float(SnxCtrSpv* s);               /* 32 bits */
uint32_t snx_ctr_spv_vector(SnxCtrSpv* s, uint32_t component, uint32_t count);
uint32_t snx_ctr_spv_array(SnxCtrSpv* s, uint32_t element, uint32_t length); /* length a number, not an id */
uint32_t snx_ctr_spv_pointer(SnxCtrSpv* s, uint32_t storage, uint32_t type);
uint32_t snx_ctr_spv_function_type(SnxCtrSpv* s, uint32_t result, const uint32_t* params, size_t count);
/* OpTypeImage: sampled 1 for an image sampled through a sampler, 2 for a storage image (with its format) */
uint32_t snx_ctr_spv_image(SnxCtrSpv* s, uint32_t sampled_type, uint32_t dim, bool arrayed, uint32_t sampled,
                           uint32_t format);
uint32_t snx_ctr_spv_sampled_image(SnxCtrSpv* s, uint32_t image);
/* a struct of its own each time (structs are decorated, as blocks): members with their byte offsets */
uint32_t snx_ctr_spv_struct(SnxCtrSpv* s, const uint32_t* members, const uint32_t* offsets, size_t count);

/* constants, made once each */
uint32_t snx_ctr_spv_uint(SnxCtrSpv* s, uint32_t value);
uint32_t snx_ctr_spv_sint(SnxCtrSpv* s, int32_t value);
uint32_t snx_ctr_spv_float_constant(SnxCtrSpv* s, float value);
uint32_t snx_ctr_spv_bool_constant(SnxCtrSpv* s, bool value);
uint32_t snx_ctr_spv_composite(SnxCtrSpv* s, uint32_t type, const uint32_t* parts, size_t count);

void snx_ctr_spv_decorate(SnxCtrSpv* s, uint32_t id, uint32_t decoration, const uint32_t* operands, size_t count);
void snx_ctr_spv_member_decorate(SnxCtrSpv* s, uint32_t type, uint32_t member, uint32_t decoration,
                                 const uint32_t* operands, size_t count);
/* a module-scope variable (an Input or Output one becomes part of the entry point's interface) */
uint32_t snx_ctr_spv_global(SnxCtrSpv* s, uint32_t storage, uint32_t type);
/* a decorated resource: a Uniform block or a UniformConstant image or sampler at set and binding */
uint32_t snx_ctr_spv_resource(SnxCtrSpv* s, uint32_t storage, uint32_t type, uint32_t set, uint32_t binding);
/* an Input or Output at a location, or a built-in */
uint32_t snx_ctr_spv_location(SnxCtrSpv* s, uint32_t storage, uint32_t type, uint32_t location);
uint32_t snx_ctr_spv_builtin(SnxCtrSpv* s, uint32_t storage, uint32_t type, uint32_t builtin);

/* a function: its id (made beforehand to call it before it is written, or 0 for a new one), its result and parameter
   types. Parameters come back in params; the first block's label is placed */
uint32_t snx_ctr_spv_function(SnxCtrSpv* s, uint32_t id, uint32_t result, const uint32_t* param_types, size_t count,
                              uint32_t* params);
void snx_ctr_spv_function_end(SnxCtrSpv* s);
/* a Function variable of the function being written, of type (the pointer is made here) */
uint32_t snx_ctr_spv_local(SnxCtrSpv* s, uint32_t type);
/* an instruction of the function's: with a result of type (its id returned), or without one (type 0, nothing
   returned) */
uint32_t snx_ctr_spv_op(SnxCtrSpv* s, uint32_t opcode, uint32_t type, const uint32_t* operands, size_t count);
void snx_ctr_spv_void_op(SnxCtrSpv* s, uint32_t opcode, const uint32_t* operands, size_t count);
uint32_t snx_ctr_spv_ext(SnxCtrSpv* s, uint32_t type, uint32_t instruction, const uint32_t* operands, size_t count);
void snx_ctr_spv_label(SnxCtrSpv* s, uint32_t label);

/* the shorthands: SPV(s, opcode, type, operands...) and SPV_VOID(s, opcode, operands...), SPV_EXT(s, type,
   instruction, operands...), each with one operand at least (C99 has no empty variadic arguments): an instruction
   without any is snx_ctr_spv_void_op(s, opcode, NULL, 0) */
#define SNX_CTR_SPV_ARGS(...) ((const uint32_t[]){__VA_ARGS__}), (sizeof((const uint32_t[]){__VA_ARGS__}) / 4)
#define SPV(s, opcode, type, ...) snx_ctr_spv_op((s), (opcode), (type), SNX_CTR_SPV_ARGS(__VA_ARGS__))
#define SPV_VOID(s, opcode, ...) snx_ctr_spv_void_op((s), (opcode), SNX_CTR_SPV_ARGS(__VA_ARGS__))
#define SPV_EXT(s, type, instruction, ...) snx_ctr_spv_ext((s), (type), (instruction), SNX_CTR_SPV_ARGS(__VA_ARGS__))

/* the entry point "main" of the stage's model, with its execution modes */
void snx_ctr_spv_entry(SnxCtrSpv* s, uint32_t model, uint32_t function);
void snx_ctr_spv_mode(SnxCtrSpv* s, uint32_t function, uint32_t mode);
void snx_ctr_spv_mode_operand(SnxCtrSpv* s, uint32_t function, uint32_t mode, uint32_t operand);
/* the module's words, the caller's to free; NULL when it ran out of memory */
uint32_t* snx_ctr_spv_finish(SnxCtrSpv* s, size_t* count);
#endif
