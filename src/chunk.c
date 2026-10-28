#include "chunk.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    initValueArray(&chunk->constants);
}

void freeChunk(Chunk* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
        chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int addConstant(Chunk* chunk, Value value) {
    /* 相同的字符串/整数常量复用同一槽位 */
    for (int i = 0; i < chunk->constants.count; i++) {
        Value c = chunk->constants.values[i];
        if (c.type != value.type) continue;
        if (IS_INT(c) && AS_INT(c) == AS_INT(value)) return i;
        if (IS_OBJ(c) && IS_STRING(c) && AS_OBJ(c) == AS_OBJ(value)) return i;
    }
    vm.tempRoots[vm.tempRootCount++] = &value;   /* 扩容可能触发 GC */
    writeValueArray(&chunk->constants, value);
    vm.tempRootCount--;
    return chunk->constants.count - 1;
}

/* ---------------- 反汇编 ---------------- */

static const char* opNames[] = {
    "CONSTANT", "NIL", "TRUE", "FALSE", "UNDEF",
    "POP", "DUP", "DUP2", "SWAP",
    "GET_LOCAL", "SET_LOCAL", "GET_GLOBAL", "SET_GLOBAL", "DEFINE_GLOBAL",
    "GET_UPVALUE", "SET_UPVALUE", "CLOSE_UPVALUE",
    "GET_FIELD", "SET_FIELD", "GET_INDEX", "SET_INDEX", "SLICE",
    "EQUAL", "NOT_EQUAL", "LESS", "LESS_EQUAL", "GREATER", "GREATER_EQUAL",
    "ADD", "SUB", "MUL", "DIV", "IDIV", "MOD", "POW",
    "NEGATE", "NOT",
    "BAND", "BOR", "BXOR", "SHL", "SHR", "BNOT",
    "IN",
    "JUMP", "JUMP_IF_FALSE", "POP_JUMP_IF_FALSE", "JUMP_IF_TRUE", "LOOP",
    "JUMP_IF_NOT_UNDEF",
    "CALL", "CALL_KW", "INVOKE", "INVOKE_KW", "CLOSURE", "RETURN",
    "ARRAY", "MAP", "BUILD_STRING", "RANGE", "STRUCT", "METHOD",
    "FOR_PREP", "FOR_RANGE", "FOR_STEP", "FOR_ITER", "IN_RANGE",
    "TRY", "END_TRY", "THROW", "IMPORT", "REPL_PRINT",
};

static void printConstant(Chunk* chunk, int idx) {
    Value v = chunk->constants.values[idx];
    ObjString* s;
    if (vmStringify(v, true, &s)) printf("%.*s", s->length > 40 ? 40 : s->length, s->chars);
}

#define U16(o) ((uint16_t)((chunk->code[(o)] << 8) | chunk->code[(o) + 1]))

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) printf("   | ");
    else printf("%4d ", chunk->lines[offset]);

    uint8_t op = chunk->code[offset];
    printf("%-18s", opNames[op]);
    switch (op) {
        case OP_CONSTANT: case OP_GET_FIELD: case OP_SET_FIELD: case OP_METHOD: case OP_IMPORT: {
            int idx = U16(offset + 1);
            printf("%4d '", idx);
            printConstant(chunk, idx);
            printf("'\n");
            return offset + 3;
        }
        case OP_GET_GLOBAL: case OP_SET_GLOBAL: case OP_DEFINE_GLOBAL: {
            int idx = U16(offset + 1);
            printf("%4d", idx);
            if (idx < vm.globalNameList.count)
                printf(" (%s)", AS_CSTRING(vm.globalNameList.values[idx]));
            printf("\n");
            return offset + 3;
        }
        case OP_ARRAY: case OP_MAP: case OP_BUILD_STRING:
            printf("%4d\n", U16(offset + 1));
            return offset + 3;
        case OP_GET_LOCAL: case OP_SET_LOCAL: case OP_GET_UPVALUE: case OP_SET_UPVALUE:
        case OP_CALL: case OP_RANGE: case OP_IN_RANGE:
            printf("%4d\n", chunk->code[offset + 1]);
            return offset + 2;
        case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_POP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE:
        case OP_TRY: {
            int j = U16(offset + 1);
            printf("%4d -> %d\n", offset, offset + 3 + j);
            return offset + 3;
        }
        case OP_LOOP: {
            int j = U16(offset + 1);
            printf("%4d -> %d\n", offset, offset + 3 - j);
            return offset + 3;
        }
        case OP_JUMP_IF_NOT_UNDEF: {
            int j = U16(offset + 2);
            printf("slot %d -> %d\n", chunk->code[offset + 1], offset + 4 + j);
            return offset + 4;
        }
        case OP_CALL_KW:
            printf("%4d kw=%d\n", chunk->code[offset + 1], U16(offset + 2));
            return offset + 4;
        case OP_INVOKE: {
            int idx = U16(offset + 1);
            printf("(%d args) '", chunk->code[offset + 3]);
            printConstant(chunk, idx);
            printf("'\n");
            return offset + 4;
        }
        case OP_STRUCT: {
            int idx = U16(offset + 1);
            printf("%4d '", idx);
            printConstant(chunk, idx);
            printf("'\n");
            return offset + 5;
        }
        case OP_FOR_RANGE: {
            int j = U16(offset + 2);
            printf("base %d -> %d\n", chunk->code[offset + 1], offset + 4 + j);
            return offset + 4;
        }
        case OP_FOR_STEP: {
            int j = U16(offset + 2);
            printf("base %d -> %d\n", chunk->code[offset + 1], offset + 4 - j);
            return offset + 4;
        }
        case OP_FOR_PREP:
            printf("base %d incl %d\n", chunk->code[offset + 1], chunk->code[offset + 2]);
            return offset + 3;
        case OP_INVOKE_KW: {
            int idx = U16(offset + 1);
            printf("(%d args) '", chunk->code[offset + 3]);
            printConstant(chunk, idx);
            printf("' kw=%d\n", U16(offset + 4));
            return offset + 6;
        }
        case OP_FOR_ITER: {
            int j = U16(offset + 3);
            printf("base %d mode %d -> %d\n", chunk->code[offset + 1], chunk->code[offset + 2], offset + 5 + j);
            return offset + 5;
        }
        case OP_CLOSURE: {
            int idx = U16(offset + 1);
            offset += 3;
            printf("%4d ", idx);
            printConstant(chunk, idx);
            printf("\n");
            ObjFunction* fn = AS_FUNCTION(chunk->constants.values[idx]);
            for (int j = 0; j < fn->upvalueCount; j++) {
                int isLocal = chunk->code[offset++];
                int index = chunk->code[offset++];
                printf("%04d    |                     %s %d\n", offset - 2, isLocal ? "local" : "upvalue", index);
            }
            return offset;
        }
        default:
            printf("\n");
            return offset + 1;
    }
}

void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);
    for (int offset = 0; offset < chunk->count;) offset = disassembleInstruction(chunk, offset);
    /* 递归反汇编嵌套函数 */
    for (int i = 0; i < chunk->constants.count; i++) {
        Value c = chunk->constants.values[i];
        if (IS_FUNCTION(c)) {
            ObjFunction* f = AS_FUNCTION(c);
            printf("\n");
            disassembleChunk(&f->chunk, f->name ? f->name->chars : "<匿名函数>");
        }
    }
}
