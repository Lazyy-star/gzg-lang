#include <math.h>

#include "memory.h"
#include "object.h"
#include "value.h"

void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }
    array->values[array->count++] = value;
}

void freeValueArray(ValueArray* array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

static bool equalDepth(Value a, Value b, int depth);

static bool equalDepth(Value a, Value b, int depth) {
    if (depth > 100000) return false;  /* 防止循环引用导致无限递归 */
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        if (IS_INT(a) && IS_INT(b)) return AS_INT(a) == AS_INT(b);
        return AS_NUMBER(a) == AS_NUMBER(b);
    }
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_NIL: case VAL_UNDEF: return true;
        case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
        case VAL_OBJ: break;
        default: return false;
    }
    if (AS_OBJ(a) == AS_OBJ(b)) return true;
    if (OBJ_TYPE(a) != OBJ_TYPE(b)) return false;
    switch (OBJ_TYPE(a)) {
        case OBJ_ARRAY: {
            ValueArray* x = &AS_ARRAY(a)->items;
            ValueArray* y = &AS_ARRAY(b)->items;
            if (x->count != y->count) return false;
            for (int i = 0; i < x->count; i++)
                if (!equalDepth(x->values[i], y->values[i], depth + 1)) return false;
            return true;
        }
        case OBJ_MAP: {
            ObjMap* x = AS_MAP(a);
            ObjMap* y = AS_MAP(b);
            if (x->count != y->count) return false;
            for (int i = 0; i < x->entryCount; i++) {
                if (!x->entries[i].live) continue;
                Value other;
                if (!mapGet(y, x->entries[i].key, &other)) return false;
                if (!equalDepth(x->entries[i].value, other, depth + 1)) return false;
            }
            return true;
        }
        case OBJ_INSTANCE: {
            ObjInstance* x = AS_INSTANCE(a);
            ObjInstance* y = AS_INSTANCE(b);
            if (x->type != y->type) return false;
            for (int i = 0; i < x->fieldCount; i++)
                if (!equalDepth(x->fields[i], y->fields[i], depth + 1)) return false;
            return true;
        }
        case OBJ_RANGE: {
            ObjRange* x = AS_RANGE(a);
            ObjRange* y = AS_RANGE(b);
            return x->start == y->start && x->end == y->end && x->step == y->step;
        }
        default:
            return false;   /* 字符串已驻留，指针不同即不等；函数等按身份比较 */
    }
}

bool valuesEqual(Value a, Value b) { return equalDepth(a, b, 0); }

bool isTruthy(Value v) {
    switch (v.type) {
        case VAL_NIL: case VAL_UNDEF: return false;
        case VAL_BOOL: return AS_BOOL(v);
        case VAL_INT: return AS_INT(v) != 0;
        case VAL_FLOAT: return AS_FLOAT(v) != 0.0;
        case VAL_OBJ:
            switch (OBJ_TYPE(v)) {
                case OBJ_STRING: return AS_STRING(v)->length > 0;
                case OBJ_ARRAY: return AS_ARRAY(v)->items.count > 0;
                case OBJ_MAP: return AS_MAP(v)->count > 0;
                case OBJ_RANGE: return rangeLength(AS_RANGE(v)) > 0;
                default: return true;
            }
    }
    return true;
}

const char* valueTypeName(Value v) {
    switch (v.type) {
        case VAL_NIL: return "nil";
        case VAL_BOOL: return "bool";
        case VAL_INT: return "int";
        case VAL_FLOAT: return "float";
        case VAL_UNDEF: return "undefined";
        case VAL_OBJ: break;
    }
    switch (OBJ_TYPE(v)) {
        case OBJ_STRING: return "string";
        case OBJ_FUNCTION: case OBJ_NATIVE: case OBJ_CLOSURE: case OBJ_BOUND: return "function";
        case OBJ_ARRAY: return "array";
        case OBJ_MAP: return "map";
        case OBJ_STRUCT: return "struct";
        case OBJ_INSTANCE: return AS_INSTANCE(v)->type->name->chars;
        case OBJ_RANGE: return "range";
        case OBJ_UPVALUE: return "upvalue";
    }
    return "unknown";
}

static uint32_t hashInt(int64_t n) {
    uint64_t x = (uint64_t)n;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (uint32_t)x;
}

bool isHashable(Value v) {
    if (!IS_OBJ(v)) return !IS_UNDEF(v);
    return IS_STRING(v);
}

uint32_t hashValue(Value v) {
    switch (v.type) {
        case VAL_NIL: return 0x9e3779b9u;
        case VAL_BOOL: return AS_BOOL(v) ? 1u : 2u;
        case VAL_INT: return hashInt(AS_INT(v));
        case VAL_FLOAT: {
            double d = AS_FLOAT(v);
            /* 1.0 与 1 作为键时视为同一个键（与 Python 一致） */
            if (d == floor(d) && d >= -9.2e18 && d <= 9.2e18) return hashInt((int64_t)d);
            uint64_t bits;
            memcpy(&bits, &d, sizeof bits);
            return hashInt((int64_t)bits);
        }
        case VAL_OBJ: return AS_STRING(v)->hash;
        default: return 0;
    }
}

/* 与 Python repr(float) 一致：找能精确往返的最短有效位数；指数在 [-4, 16) 内用定点表示 */
int formatFloat(double d, char* buf, size_t size) {
    if (isnan(d)) return snprintf(buf, size, "nan");
    if (isinf(d)) return snprintf(buf, size, d > 0 ? "inf" : "-inf");
    if (d == 0) return snprintf(buf, size, signbit(d) ? "-0.0" : "0.0");

    char tmp[64];
    for (int prec = 1; prec <= 17; prec++) {
        snprintf(tmp, sizeof tmp, "%.*e", prec - 1, d);
        if (strtod(tmp, NULL) == d) break;
    }
    /* tmp 形如 "-1.2345e+05" */
    char digits[32];
    int nd = 0;
    bool neg = false;
    const char* p = tmp;
    if (*p == '-') { neg = true; p++; }
    while (*p && *p != 'e' && *p != 'E') {
        if (*p >= '0' && *p <= '9') digits[nd++] = *p;
        p++;
    }
    while (nd > 1 && digits[nd - 1] == '0') nd--;   /* 去掉尾随 0 */
    digits[nd] = '\0';
    int exp10 = (*p == 'e' || *p == 'E') ? atoi(p + 1) : 0;

    char out[80];
    int n = 0;
    if (neg) out[n++] = '-';
    if (exp10 >= -4 && exp10 < 16) {
        if (exp10 >= 0) {
            for (int i = 0; i <= exp10; i++) out[n++] = i < nd ? digits[i] : '0';
            out[n++] = '.';
            if (nd > exp10 + 1) {
                for (int i = exp10 + 1; i < nd; i++) out[n++] = digits[i];
            } else {
                out[n++] = '0';
            }
        } else {
            out[n++] = '0';
            out[n++] = '.';
            for (int i = 0; i < -exp10 - 1; i++) out[n++] = '0';
            for (int i = 0; i < nd; i++) out[n++] = digits[i];
        }
        out[n] = '\0';
    } else {
        out[n++] = digits[0];
        if (nd > 1) {
            out[n++] = '.';
            for (int i = 1; i < nd; i++) out[n++] = digits[i];
        }
        n += snprintf(out + n, sizeof out - n, "e%c%02d", exp10 < 0 ? '-' : '+', exp10 < 0 ? -exp10 : exp10);
    }
    return snprintf(buf, size, "%s", out);
}
