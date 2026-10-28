/* 值表示：带标签的联合体（tagged union），16 字节。
 * 整数与浮点分开存储：整数是 64 位有符号整数，浮点是 double。 */
#ifndef GZG_VALUE_H
#define GZG_VALUE_H

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_INT,
    VAL_FLOAT,
    VAL_OBJ,
    VAL_UNDEF,   /* 内部哨兵：未定义的全局变量 / 未传入的参数，用户不可见 */
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        int64_t i;
        double f;
        Obj* obj;
    } as;
} Value;

#define IS_NIL(v)    ((v).type == VAL_NIL)
#define IS_BOOL(v)   ((v).type == VAL_BOOL)
#define IS_INT(v)    ((v).type == VAL_INT)
#define IS_FLOAT(v)  ((v).type == VAL_FLOAT)
#define IS_NUMBER(v) ((v).type == VAL_INT || (v).type == VAL_FLOAT)
#define IS_OBJ(v)    ((v).type == VAL_OBJ)
#define IS_UNDEF(v)  ((v).type == VAL_UNDEF)

#define AS_BOOL(v)   ((v).as.boolean)
#define AS_INT(v)    ((v).as.i)
#define AS_FLOAT(v)  ((v).as.f)
#define AS_OBJ(v)    ((v).as.obj)
#define AS_NUMBER(v) (IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v))

#define NIL_VAL         ((Value){VAL_NIL, {.i = 0}})
#define UNDEF_VAL       ((Value){VAL_UNDEF, {.i = 0}})
#define BOOL_VAL(b)     ((Value){VAL_BOOL, {.boolean = (b)}})
#define INT_VAL(n)      ((Value){VAL_INT, {.i = (n)}})
#define FLOAT_VAL(d)    ((Value){VAL_FLOAT, {.f = (d)}})
#define OBJ_VAL(o)      ((Value){VAL_OBJ, {.obj = (Obj*)(o)}})

typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);

bool valuesEqual(Value a, Value b);   /* 深比较：数组/字典/结构体实例按内容比较 */
bool isTruthy(Value v);
const char* valueTypeName(Value v);   /* 给用户看的类型名，如 "int"、"array"、"Point" */
uint32_t hashValue(Value v);          /* 仅用于可作字典键的值 */
bool isHashable(Value v);

/* 把浮点数格式化为与 Python repr 一致的最短往返形式，返回写入长度 */
int formatFloat(double d, char* buf, size_t size);

#endif
