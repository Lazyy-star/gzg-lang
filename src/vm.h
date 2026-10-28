/* 栈式虚拟机 */
#ifndef GZG_VM_H
#define GZG_VM_H

#include "common.h"
#include "object.h"
#include "table.h"
#include "value.h"

typedef struct {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;
} CallFrame;

/* try 块的异常处理器 */
typedef struct {
    int frameCount;      /* 进入 try 时的调用深度 */
    Value* stackTop;     /* 进入 try 时的栈顶 */
    uint8_t* catchIp;
} Handler;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;
    Value* stack;        /* 大小 STACK_MAX，固定不扩容（upvalue 指针指向栈内） */
    Value* stackTop;
    Handler handlers[HANDLERS_MAX];
    int handlerCount;
    ObjUpvalue* openUpvalues;

    Table strings;       /* 字符串驻留池（弱引用） */
    Table globalNames;   /* 全局名 -> 下标(int) */
    ValueArray globalValues;
    ValueArray globalNameList;   /* 下标 -> 名字，用于报错 */
    uint8_t* globalConst;        /* 下标 -> 是否 const */
    int globalConstCap;
    Table imports;       /* 已导入文件的绝对路径 */

    Table stringMethods, arrayMethods, mapMethods, rangeMethods;
    ObjString* initString;       /* "to_str" */
    ObjString* emptyString;

    Value pendingError;  /* 原生函数报错时设置的异常值 */
    bool hasError;
    bool uncaughtReported;
    ObjString* scriptDir;        /* 主脚本所在目录，import 相对于它 */
    ObjArray* scriptArgs;

    /* GC */
    Obj* objects;
    size_t bytesAllocated;
    size_t nextGC;
    int grayCount, grayCapacity;
    Obj** grayStack;
    Value* tempRoots[16];        /* 编译期/原生函数临时根 */
    int tempRootCount;
    bool replMode;
    /* 统计（gzg --stats） */
    int gcCount;
    double gcSeconds;
    size_t totalAllocated;
    size_t peakHeap;
} VM;

extern VM vm;

void initVM(void);
void freeVM(void);
InterpretResult interpret(const char* source, const char* filename);

void push(Value value);
Value pop(void);

/* 供原生函数使用 */
bool vmError(const char* fmt, ...);                 /* 设置运行时错误，总是返回 false */
bool vmThrowValue(Value v);
bool vmCall(Value callee, int argc, Value* args, Value* result);  /* 从原生代码回调 GZG 函数 */
bool vmStringify(Value v, bool quoted, ObjString** out);         /* 调用 to_str 等 */
int globalSlot(ObjString* name);                    /* 取得/创建全局变量下标 */
void defineNative(const char* name, NativeFn fn, int minArgs, int maxArgs);
void defineNativeMethod(Table* table, const char* name, NativeFn fn, int minArgs, int maxArgs);
void vmSetConstGlobal(int slot, bool isConst);
bool vmIsConstGlobal(int slot);

#endif
