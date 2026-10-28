/* 堆对象。所有对象以 Obj 头开始，串成链表供 GC 清扫。 */
#ifndef GZG_OBJECT_H
#define GZG_OBJECT_H

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_ARRAY,
    OBJ_MAP,
    OBJ_STRUCT,
    OBJ_INSTANCE,
    OBJ_BOUND,
    OBJ_RANGE,
} ObjType;

struct Obj {
    ObjType type;
    bool marked;
    struct Obj* next;
};

/* 字符串：UTF-8 字节 + 码点数。不可变、全部驻留(intern)，相等比较只需比指针。 */
struct ObjString {
    Obj obj;
    int length;      /* 字节数 */
    int charCount;   /* Unicode 码点数；== length 时说明是纯 ASCII，可 O(1) 下标 */
    uint32_t hash;
    char chars[];
};

typedef struct {
    Obj obj;
    int arity;           /* 形参个数 */
    int minArity;        /* 必填形参个数（其余有默认值） */
    int upvalueCount;
    bool isMethod;
    Chunk chunk;
    ObjString* name;
    ObjString* file;     /* 源文件名，用于报错 */
    ValueArray params;   /* 形参名（ObjString），用于命名参数调用 */
} ObjFunction;

/* 原生函数：args[0..argc-1]，失败时调用 vmError 并返回 false */
typedef bool (*NativeFn)(int argc, Value* args, Value* result);

typedef struct {
    Obj obj;
    NativeFn fn;
    const char* name;
    int minArgs;
    int maxArgs;         /* -1 表示不限 */
    bool isMethod;       /* 内建方法：args[0] 为接收者，min/maxArgs 不含接收者 */
} ObjNative;

typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount;
} ObjClosure;

typedef struct {
    Obj obj;
    ValueArray items;
} ObjArray;

/* 有序字典：entries 按插入顺序排列，index 是开放寻址的哈希索引（存 entries 下标+1，0 表示空，-1 表示墓碑） */
typedef struct {
    Value key;
    Value value;
    uint32_t hash;
    bool live;
} MapEntry;

typedef struct {
    Obj obj;
    int count;           /* 存活键数 */
    int entryCount;      /* entries 已用槽数（含已删除） */
    int entryCap;
    MapEntry* entries;
    int indexCap;        /* 2 的幂 */
    int32_t* index;
} ObjMap;

typedef struct {
    Obj obj;
    ObjString* name;
    int fieldCount;
    ObjString** fieldNames;
    Value* defaults;     /* UNDEF 表示必填字段 */
    Table fieldIndex;    /* 字段名 -> 下标(int) */
    Table methods;       /* 方法名 -> 闭包 */
} ObjStruct;

typedef struct {
    Obj obj;
    ObjStruct* type;
    int fieldCount;      /* 冗余保存，清扫时结构体可能已先被释放 */
    Value fields[];
} ObjInstance;

typedef struct {
    Obj obj;
    Value receiver;
    Value method;        /* 闭包或原生函数 */
} ObjBound;

/* 区间统一存成左闭右开 [start, end)，step 可为负 */
typedef struct {
    Obj obj;
    int64_t start, end, step;
} ObjRange;

#define OBJ_TYPE(v)     (AS_OBJ(v)->type)
static inline bool isObjType(Value v, ObjType t) { return IS_OBJ(v) && AS_OBJ(v)->type == t; }

#define IS_STRING(v)    isObjType(v, OBJ_STRING)
#define IS_FUNCTION(v)  isObjType(v, OBJ_FUNCTION)
#define IS_NATIVE(v)    isObjType(v, OBJ_NATIVE)
#define IS_CLOSURE(v)   isObjType(v, OBJ_CLOSURE)
#define IS_ARRAY(v)     isObjType(v, OBJ_ARRAY)
#define IS_MAP(v)       isObjType(v, OBJ_MAP)
#define IS_STRUCT(v)    isObjType(v, OBJ_STRUCT)
#define IS_INSTANCE(v)  isObjType(v, OBJ_INSTANCE)
#define IS_BOUND(v)     isObjType(v, OBJ_BOUND)
#define IS_RANGE(v)     isObjType(v, OBJ_RANGE)

#define AS_STRING(v)    ((ObjString*)AS_OBJ(v))
#define AS_CSTRING(v)   (((ObjString*)AS_OBJ(v))->chars)
#define AS_FUNCTION(v)  ((ObjFunction*)AS_OBJ(v))
#define AS_NATIVE(v)    ((ObjNative*)AS_OBJ(v))
#define AS_CLOSURE(v)   ((ObjClosure*)AS_OBJ(v))
#define AS_ARRAY(v)     ((ObjArray*)AS_OBJ(v))
#define AS_MAP(v)       ((ObjMap*)AS_OBJ(v))
#define AS_STRUCT(v)    ((ObjStruct*)AS_OBJ(v))
#define AS_INSTANCE(v)  ((ObjInstance*)AS_OBJ(v))
#define AS_BOUND(v)     ((ObjBound*)AS_OBJ(v))
#define AS_RANGE(v)     ((ObjRange*)AS_OBJ(v))

ObjString* copyString(const char* chars, int length);   /* 复制并驻留 */
ObjString* takeString(char* chars, int length);         /* 接管 malloc 出的缓冲区 */
ObjString* concatStrings(ObjString* a, ObjString* b);
ObjString* cstr(const char* s);
ObjFunction* newFunction(void);
ObjNative* newNative(NativeFn fn, const char* name, int minArgs, int maxArgs);
ObjClosure* newClosure(ObjFunction* function);
ObjUpvalue* newUpvalue(Value* slot);
ObjArray* newArray(void);
ObjMap* newMap(void);
ObjStruct* newStruct(ObjString* name);
ObjInstance* newInstance(ObjStruct* type);
ObjBound* newBound(Value receiver, Value method);
ObjRange* newRange(int64_t start, int64_t end, int64_t step);

/* 字典操作 */
bool mapGet(ObjMap* map, Value key, Value* out);
void mapSet(ObjMap* map, Value key, Value value);       /* 调用前须确认 key 可哈希 */
bool mapDelete(ObjMap* map, Value key);
int64_t rangeLength(ObjRange* r);

/* UTF-8 工具 */
int utf8CharLen(unsigned char c);
int utf8Count(const char* s, int len);
int utf8Offset(ObjString* s, int charIndex);           /* 第 charIndex 个码点的字节偏移 */

#endif
