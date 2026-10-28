/* 字节码块：指令流 + 行号表 + 常量池。操作数为 1 字节(u8) 或 2 字节大端(u16)。 */
#ifndef GZG_CHUNK_H
#define GZG_CHUNK_H

#include "common.h"
#include "value.h"

typedef enum {
    OP_CONSTANT,       /* u16 常量下标 */
    OP_NIL, OP_TRUE, OP_FALSE, OP_UNDEF,
    OP_POP, OP_DUP, OP_DUP2, OP_SWAP,
    OP_GET_LOCAL, OP_SET_LOCAL,          /* u8 槽位 */
    OP_GET_GLOBAL, OP_SET_GLOBAL,        /* u16 全局下标 */
    OP_DEFINE_GLOBAL,                    /* u16 */
    OP_GET_UPVALUE, OP_SET_UPVALUE,      /* u8 */
    OP_CLOSE_UPVALUE,
    OP_GET_FIELD, OP_SET_FIELD,          /* u16 名字常量 */
    OP_GET_INDEX, OP_SET_INDEX, OP_SLICE,
    OP_EQUAL, OP_NOT_EQUAL, OP_LESS, OP_LESS_EQUAL, OP_GREATER, OP_GREATER_EQUAL,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_IDIV, OP_MOD, OP_POW,
    OP_NEGATE, OP_NOT,
    OP_BAND, OP_BOR, OP_BXOR, OP_SHL, OP_SHR, OP_BNOT,
    OP_IN,
    OP_JUMP, OP_JUMP_IF_FALSE, OP_POP_JUMP_IF_FALSE, OP_JUMP_IF_TRUE, OP_LOOP,  /* u16 偏移 */
    OP_JUMP_IF_NOT_UNDEF,                /* u8 槽位, u16 偏移：默认参数 */
    OP_CALL,                             /* u8 实参个数 */
    OP_CALL_KW,                          /* u8 实参总数, u16 常量(命名参数名数组) */
    OP_INVOKE,                           /* u16 方法名, u8 实参个数 */
    OP_INVOKE_KW,                        /* u16 方法名, u8 实参总数, u16 命名参数名数组 */
    OP_CLOSURE,                          /* u16 函数常量, 然后每个 upvalue 两字节(isLocal, index) */
    OP_RETURN,
    OP_ARRAY,                            /* u16 元素个数 */
    OP_MAP,                              /* u16 键值对个数 */
    OP_BUILD_STRING,                     /* u16 片段个数 */
    OP_RANGE,                            /* u8 1=闭区间 */
    OP_STRUCT,                           /* u16 名字常量, u16 字段描述常量 */
    OP_METHOD,                           /* u16 方法名 */
    OP_FOR_PREP,                         /* u8 基址槽位, u8 1=闭区间：校验 [i end step] 并把闭区间换成开区间 */
    OP_FOR_RANGE,                        /* u8 基址槽位, u16 跳出偏移：检查 i<end 并把 i 压栈 */
    OP_FOR_STEP,                         /* u8 基址槽位, u16 回跳偏移：i += step 后跳回循环头 */
    OP_FOR_ITER,                         /* u8 基址槽位, u8 模式(1=单变量 2=双变量), u16 跳出偏移 */
    OP_IN_RANGE,                         /* u8 1=闭区间；match 区间模式：[subject lo hi] -> [subject bool] */
    OP_TRY,                              /* u16 catch 偏移 */
    OP_END_TRY,
    OP_THROW,
    OP_IMPORT,                           /* u16 路径常量 */
    OP_REPL_PRINT,
} OpCode;

typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;
    ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

/* 反汇编（gzg --dis） */
void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstruction(Chunk* chunk, int offset);

#endif
