/* 内建函数与内建方法（字符串 / 数组 / 字典 / 区间）
 * 约定：失败时 return vmError(...)；每分配一个新对象，在下一次分配前 push 保护，返回前 pop。 */
#include <math.h>

#include "builtins.h"
#include "memory.h"
#include "object.h"
#include "platform.h"
#include "vm.h"

#define GZG_PI 3.14159265358979323846
#define GZG_E  2.71828182845904523536

/* ---------------- 工具：临时字符串缓冲区（系统 malloc，不经 GC） ---------------- */

typedef struct {
    char* data;
    size_t len, cap;
} Buf;

static void bufInit(Buf* b) {
    b->cap = 64;
    b->len = 0;
    b->data = (char*)malloc(b->cap);
    if (b->data == NULL) exit(EXIT_RUNTIME_ERROR);
}

static void bufAppend(Buf* b, const char* s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap) b->cap *= 2;
        b->data = (char*)realloc(b->data, b->cap);
        if (b->data == NULL) exit(EXIT_RUNTIME_ERROR);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static ObjString* bufFinish(Buf* b) {
    ObjString* s = copyString(b->data, (int)b->len);
    free(b->data);
    b->data = NULL;
    return s;
}

static void bufFree(Buf* b) {
    free(b->data);
    b->data = NULL;
}

/* ---------------- 工具：参数检查 ---------------- */

static bool needInt(const char* fn, Value v, int pos, int64_t* out) {
    if (!IS_INT(v)) return vmError("%s() 的第 %d 个参数必须是整数，实际是 %s", fn, pos, valueTypeName(v));
    *out = AS_INT(v);
    return true;
}

static bool needNum(const char* fn, Value v, int pos, double* out) {
    if (!IS_NUMBER(v)) return vmError("%s() 的第 %d 个参数必须是数字，实际是 %s", fn, pos, valueTypeName(v));
    *out = AS_NUMBER(v);
    return true;
}

static bool needStr(const char* fn, Value v, int pos, ObjString** out) {
    if (!IS_STRING(v)) return vmError("%s() 的第 %d 个参数必须是字符串，实际是 %s", fn, pos, valueTypeName(v));
    *out = AS_STRING(v);
    return true;
}

static bool needArray(const char* fn, Value v, int pos, ObjArray** out) {
    if (!IS_ARRAY(v)) return vmError("%s() 的第 %d 个参数必须是数组，实际是 %s", fn, pos, valueTypeName(v));
    *out = AS_ARRAY(v);
    return true;
}

/* 往已被保护的数组追加一个值；若值是新分配的对象，先保护它 */
static void arrPush(ObjArray* arr, Value v) {
    push(v);
    writeValueArray(&arr->items, v);
    pop();
}

static Value strVal(const char* s, int len) { return OBJ_VAL(copyString(s, len)); }

/* 三路比较：数字按数值、字符串按字节序、数组按字典序；其他报错 */
static bool compareValues(Value a, Value b, int* out) {
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        if (IS_INT(a) && IS_INT(b)) {
            *out = AS_INT(a) < AS_INT(b) ? -1 : (AS_INT(a) > AS_INT(b) ? 1 : 0);
        } else {
            double x = AS_NUMBER(a), y = AS_NUMBER(b);
            *out = x < y ? -1 : (x > y ? 1 : 0);
        }
        return true;
    }
    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString* x = AS_STRING(a);
        ObjString* y = AS_STRING(b);
        int n = x->length < y->length ? x->length : y->length;
        int c = memcmp(x->chars, y->chars, (size_t)n);
        if (c == 0) c = x->length - y->length;
        *out = c < 0 ? -1 : (c > 0 ? 1 : 0);
        return true;
    }
    if (IS_ARRAY(a) && IS_ARRAY(b)) {
        ValueArray* x = &AS_ARRAY(a)->items;
        ValueArray* y = &AS_ARRAY(b)->items;
        int n = x->count < y->count ? x->count : y->count;
        for (int i = 0; i < n; i++) {
            if (!compareValues(x->values[i], y->values[i], out)) return false;
            if (*out != 0) return true;
        }
        *out = x->count < y->count ? -1 : (x->count > y->count ? 1 : 0);
        return true;
    }
    return vmError("无法比较 %s 和 %s", valueTypeName(a), valueTypeName(b));
}

/* 把可能为负的下标规范化到 [0, len)；越界返回 false */
static bool normIndex(int64_t i, int64_t len, int64_t* out) {
    if (i < 0) i += len;
    if (i < 0 || i >= len) return false;
    *out = i;
    return true;
}

/* 切片边界：支持负数，越界截断到 [0, len] */
static int64_t clampSlice(int64_t i, int64_t len) {
    if (i < 0) i += len;
    if (i < 0) i = 0;
    if (i > len) i = len;
    return i;
}

/* ================= 输入输出 ================= */

static int ttyState = -1;   /* 缓存 platformIsTTY 的结果 */

static bool writeArgs(int argc, Value* args, bool spaced) {
    for (int i = 0; i < argc; i++) {
        ObjString* s;
        if (!vmStringify(args[i], false, &s)) return false;
        if (i > 0 && spaced) fputc(' ', stdout);
        fwrite(s->chars, 1, (size_t)s->length, stdout);
    }
    return true;
}

static bool nat_print(int argc, Value* args, Value* result) {
    if (!writeArgs(argc, args, true)) return false;
    fputc('\n', stdout);
    if (ttyState < 0) ttyState = platformIsTTY() ? 1 : 0;
    if (ttyState) fflush(stdout);
    *result = NIL_VAL;
    return true;
}

/* write 不换行、不加分隔符，便于逐段拼出一行（进度条、棋盘） */
static bool nat_platform(int argc, Value* args, Value* result) {
#if defined(_WIN32)
    *result = OBJ_VAL(cstr("windows"));
#elif defined(__APPLE__)
    *result = OBJ_VAL(cstr("macos"));
#elif defined(__linux__)
    *result = OBJ_VAL(cstr("linux"));
#else
    *result = OBJ_VAL(cstr("unix"));
#endif
    return true;
}

static bool nat_write(int argc, Value* args, Value* result) {
    if (!writeArgs(argc, args, false)) return false;
    fflush(stdout);
    *result = NIL_VAL;
    return true;
}

static bool nat_input(int argc, Value* args, Value* result) {
    if (argc > 0) {
        ObjString* s;
        if (!vmStringify(args[0], false, &s)) return false;
        fwrite(s->chars, 1, (size_t)s->length, stdout);
    }
    bool eof = false;
    char* line = readLine(&eof);
    if (line == NULL) {
        *result = NIL_VAL;
        return true;
    }
    *result = OBJ_VAL(takeString(line, (int)strlen(line)));
    return true;
}

/* ================= 类型与转换 ================= */

static bool nat_type(int argc, Value* args, Value* result) {
    (void)argc;
    *result = OBJ_VAL(cstr(valueTypeName(args[0])));
    return true;
}

static bool isSpaceChar(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

static bool floatToInt(double d, int64_t* out) {
    if (isnan(d) || isinf(d)) return vmError("无法把 %s 转换为整数", isnan(d) ? "nan" : "inf");
    if (d >= 9223372036854775808.0 || d < -9223372036854775808.0) return vmError("浮点数超出整数范围");
    *out = (int64_t)d;
    return true;
}

static bool nat_int(int argc, Value* args, Value* result) {
    (void)argc;
    Value v = args[0];
    if (IS_INT(v)) { *result = v; return true; }
    if (IS_BOOL(v)) { *result = INT_VAL(AS_BOOL(v) ? 1 : 0); return true; }
    if (IS_FLOAT(v)) {
        int64_t n;
        if (!floatToInt(AS_FLOAT(v), &n)) return false;
        *result = INT_VAL(n);
        return true;
    }
    if (IS_STRING(v)) {
        ObjString* s = AS_STRING(v);
        const char* p = s->chars;
        const char* end = s->chars + s->length;
        while (p < end && isSpaceChar(*p)) p++;
        while (end > p && isSpaceChar(end[-1])) end--;
        bool neg = false;
        const char* q = p;
        if (q < end && (*q == '+' || *q == '-')) neg = *q++ == '-';
        if (q == end) goto bad;
        uint64_t acc = 0;
        for (; q < end; q++) {
            if (*q < '0' || *q > '9') goto bad;
            uint64_t d = (uint64_t)(*q - '0');
            if (acc > (UINT64_C(9223372036854775808) - d) / 10) return vmError("整数 '%s' 超出范围", s->chars);
            acc = acc * 10 + d;
        }
        if (!neg && acc > (uint64_t)INT64_MAX) return vmError("整数 '%s' 超出范围", s->chars);
        *result = INT_VAL(neg ? (int64_t)(0 - acc) : (int64_t)acc);
        return true;
    bad:
        return vmError("无法把字符串 '%s' 转换为整数", s->chars);
    }
    return vmError("无法把 %s 转换为整数", valueTypeName(v));
}

static bool nat_float(int argc, Value* args, Value* result) {
    (void)argc;
    Value v = args[0];
    if (IS_FLOAT(v)) { *result = v; return true; }
    if (IS_INT(v)) { *result = FLOAT_VAL((double)AS_INT(v)); return true; }
    if (IS_BOOL(v)) { *result = FLOAT_VAL(AS_BOOL(v) ? 1.0 : 0.0); return true; }
    if (IS_STRING(v)) {
        ObjString* s = AS_STRING(v);
        const char* p = s->chars;
        const char* end = s->chars + s->length;
        while (p < end && isSpaceChar(*p)) p++;
        while (end > p && isSpaceChar(end[-1])) end--;
        if (p == end) return vmError("无法把字符串 '%s' 转换为浮点数", s->chars);
        char tmp[128];
        size_t n = (size_t)(end - p);
        if (n >= sizeof tmp) return vmError("无法把字符串 '%s' 转换为浮点数", s->chars);
        memcpy(tmp, p, n);
        tmp[n] = '\0';
        char* stop;
        double d = strtod(tmp, &stop);
        if (*stop != '\0') return vmError("无法把字符串 '%s' 转换为浮点数", s->chars);
        *result = FLOAT_VAL(d);
        return true;
    }
    return vmError("无法把 %s 转换为浮点数", valueTypeName(v));
}

static bool nat_str(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* s;
    if (!vmStringify(args[0], false, &s)) return false;
    *result = OBJ_VAL(s);
    return true;
}

static bool nat_bool(int argc, Value* args, Value* result) {
    (void)argc;
    *result = BOOL_VAL(isTruthy(args[0]));
    return true;
}

static bool nat_len(int argc, Value* args, Value* result) {
    (void)argc;
    Value v = args[0];
    if (IS_STRING(v)) *result = INT_VAL(AS_STRING(v)->charCount);
    else if (IS_ARRAY(v)) *result = INT_VAL(AS_ARRAY(v)->items.count);
    else if (IS_MAP(v)) *result = INT_VAL(AS_MAP(v)->count);
    else if (IS_RANGE(v)) *result = INT_VAL(rangeLength(AS_RANGE(v)));
    else return vmError("%s 类型没有长度", valueTypeName(v));
    return true;
}

/* ================= 数学 ================= */

static bool nat_abs(int argc, Value* args, Value* result) {
    (void)argc;
    Value v = args[0];
    if (IS_INT(v)) {
        int64_t n = AS_INT(v);
        *result = INT_VAL(n < 0 ? (int64_t)(0 - (uint64_t)n) : n);
    } else if (IS_FLOAT(v)) {
        *result = FLOAT_VAL(fabs(AS_FLOAT(v)));
    } else {
        return vmError("abs() 的参数必须是数字，实际是 %s", valueTypeName(v));
    }
    return true;
}

/* min/max：单个数组参数取其元素，否则取全部参数。作为数组方法时 args[0] 就是数组。 */
static bool minmax(const char* name, int argc, Value* args, Value* result, int sign) {
    Value* items = args;
    int n = argc;
    if (argc == 1) {
        if (!IS_ARRAY(args[0])) return vmError("%s() 只有一个参数时，参数必须是数组", name);
        items = AS_ARRAY(args[0])->items.values;
        n = AS_ARRAY(args[0])->items.count;
    }
    if (n == 0) return vmError("%s() 的参数不能为空", name);
    Value best = items[0];
    for (int i = 1; i < n; i++) {
        int c;
        if (!compareValues(items[i], best, &c)) return false;
        if (c * sign > 0) best = items[i];
    }
    *result = best;
    return true;
}

static bool nat_min(int argc, Value* args, Value* result) { return minmax("min", argc, args, result, -1); }
static bool nat_max(int argc, Value* args, Value* result) { return minmax("max", argc, args, result, 1); }

static bool roundOp(const char* name, Value v, double (*op)(double), Value* result) {
    if (IS_INT(v)) { *result = v; return true; }
    if (!IS_FLOAT(v)) return vmError("%s() 的参数必须是数字，实际是 %s", name, valueTypeName(v));
    int64_t n;
    if (!floatToInt(op(AS_FLOAT(v)), &n)) return false;
    *result = INT_VAL(n);
    return true;
}

static bool nat_floor(int argc, Value* args, Value* result) { (void)argc; return roundOp("floor", args[0], floor, result); }
static bool nat_ceil(int argc, Value* args, Value* result) { (void)argc; return roundOp("ceil", args[0], ceil, result); }

static bool nat_round(int argc, Value* args, Value* result) {
    if (argc < 2 || IS_NIL(args[1])) return roundOp("round", args[0], round, result);
    double x;
    int64_t digits;
    if (!needNum("round", args[0], 1, &x)) return false;
    if (!needInt("round", args[1], 2, &digits)) return false;
    if (digits > 15) { *result = FLOAT_VAL(x); return true; }
    double scale = pow(10.0, (double)digits);
    *result = FLOAT_VAL(round(x * scale) / scale);
    return true;
}

#define MATH1(cname, op)                                              \
    static bool cname(int argc, Value* args, Value* result) {         \
        (void)argc;                                                   \
        double x;                                                     \
        if (!needNum(#op, args[0], 1, &x)) return false;              \
        *result = FLOAT_VAL(op(x));                                   \
        return true;                                                  \
    }

MATH1(nat_sin, sin)
MATH1(nat_cos, cos)
MATH1(nat_tan, tan)
MATH1(nat_exp, exp)

static bool nat_sqrt(int argc, Value* args, Value* result) {
    (void)argc;
    double x;
    if (!needNum("sqrt", args[0], 1, &x)) return false;
    if (x < 0) return vmError("sqrt() 的参数不能是负数");
    *result = FLOAT_VAL(sqrt(x));
    return true;
}

static bool nat_log(int argc, Value* args, Value* result) {
    double x;
    if (!needNum("log", args[0], 1, &x)) return false;
    if (x <= 0) return vmError("log() 的参数必须大于 0");
    if (argc >= 2) {
        double base;
        if (!needNum("log", args[1], 2, &base)) return false;
        if (base <= 0 || base == 1) return vmError("log() 的底数必须大于 0 且不等于 1");
        *result = FLOAT_VAL(log(x) / log(base));
    } else {
        *result = FLOAT_VAL(log(x));
    }
    return true;
}

static bool nat_pow(int argc, Value* args, Value* result) {
    (void)argc;
    double x, y;
    if (!needNum("pow", args[0], 1, &x) || !needNum("pow", args[1], 2, &y)) return false;
    *result = FLOAT_VAL(pow(x, y));
    return true;
}

static bool nat_atan2(int argc, Value* args, Value* result) {
    (void)argc;
    double y, x;
    if (!needNum("atan2", args[0], 1, &y) || !needNum("atan2", args[1], 2, &x)) return false;
    *result = FLOAT_VAL(atan2(y, x));
    return true;
}

/* ================= 随机数（xorshift64*，可用 seed 复现） ================= */

static uint64_t rngState = 0;

static uint64_t splitmix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

static uint64_t rngNext(void) {
    if (rngState == 0) {
        double t = platformTime() * 1e6 + platformClock() * 1e9;
        rngState = splitmix((uint64_t)t);
        if (rngState == 0) rngState = 1;
    }
    rngState ^= rngState >> 12;
    rngState ^= rngState << 25;
    rngState ^= rngState >> 27;
    return rngState * 0x2545F4914F6CDD1DULL;
}

/* [0, n) 内的均匀整数（拒绝采样消除偏差） */
static uint64_t rngBelow(uint64_t n) {
    uint64_t limit = UINT64_MAX - UINT64_MAX % n;
    uint64_t r;
    do r = rngNext(); while (r >= limit);
    return r % n;
}

static bool nat_seed(int argc, Value* args, Value* result) {
    (void)argc;
    int64_t n;
    if (!needInt("seed", args[0], 1, &n)) return false;
    rngState = splitmix((uint64_t)n);
    if (rngState == 0) rngState = 1;
    *result = NIL_VAL;
    return true;
}

static bool nat_random(int argc, Value* args, Value* result) {
    (void)argc; (void)args;
    *result = FLOAT_VAL((double)(rngNext() >> 11) * (1.0 / 9007199254740992.0));
    return true;
}

static bool nat_randint(int argc, Value* args, Value* result) {
    (void)argc;
    int64_t a, b;
    if (!needInt("randint", args[0], 1, &a) || !needInt("randint", args[1], 2, &b)) return false;
    if (a > b) return vmError("randint(a, b) 要求 a <= b，实际 a=%lld b=%lld", (long long)a, (long long)b);
    uint64_t span = (uint64_t)b - (uint64_t)a + 1;
    uint64_t r = span == 0 ? rngNext() : rngBelow(span);
    *result = INT_VAL((int64_t)((uint64_t)a + r));
    return true;
}

static bool nat_choice(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* arr;
    if (!needArray("choice", args[0], 1, &arr)) return false;
    if (arr->items.count == 0) return vmError("choice() 不能从空数组中选择");
    *result = arr->items.values[rngBelow((uint64_t)arr->items.count)];
    return true;
}

static bool nat_shuffle(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* arr;
    if (!needArray("shuffle", args[0], 1, &arr)) return false;
    Value* v = arr->items.values;
    for (int i = arr->items.count - 1; i > 0; i--) {
        int j = (int)rngBelow((uint64_t)i + 1);
        Value t = v[i];
        v[i] = v[j];
        v[j] = t;
    }
    *result = NIL_VAL;
    return true;
}

/* ================= 时间 ================= */

static bool nat_clock(int argc, Value* args, Value* result) {
    (void)argc; (void)args;
    *result = FLOAT_VAL(platformClock());
    return true;
}

static bool nat_time(int argc, Value* args, Value* result) {
    (void)argc; (void)args;
    *result = FLOAT_VAL(platformTime());
    return true;
}

static bool nat_sleep(int argc, Value* args, Value* result) {
    (void)argc;
    double ms;
    if (!needNum("sleep", args[0], 1, &ms)) return false;
    platformSleep((int)ms);
    *result = NIL_VAL;
    return true;
}

/* ================= 字符 ================= */

static int encodeUtf8(uint32_t cp, char* out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static bool nat_chr(int argc, Value* args, Value* result) {
    (void)argc;
    int64_t n;
    if (!needInt("chr", args[0], 1, &n)) return false;
    if (n < 0 || n > 0x10FFFF) return vmError("chr() 的参数超出 Unicode 范围: %lld", (long long)n);
    char buf[4];
    int len = encodeUtf8((uint32_t)n, buf);
    *result = strVal(buf, len);
    return true;
}

static bool nat_ord(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* s;
    if (!needStr("ord", args[0], 1, &s)) return false;
    if (s->length == 0) return vmError("ord() 的参数不能是空字符串");
    const unsigned char* p = (const unsigned char*)s->chars;
    int n = utf8CharLen(p[0]);
    if (n > s->length) n = 1;
    uint32_t cp;
    if (n == 1) cp = p[0];
    else if (n == 2) cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    else if (n == 3) cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    else cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
              ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    *result = INT_VAL((int64_t)cp);
    return true;
}

/* ================= range / 断言 / 进程 ================= */

static Value rangeToArray(int64_t start, int64_t end, int64_t step) {
    ObjRange tmp;
    tmp.start = start;
    tmp.end = end;
    tmp.step = step;
    int64_t n = rangeLength(&tmp);
    ObjArray* arr = newArray();
    push(OBJ_VAL(arr));
    for (int64_t i = 0; i < n; i++) writeValueArray(&arr->items, INT_VAL(start + i * step));
    pop();
    return OBJ_VAL(arr);
}

static bool nat_range(int argc, Value* args, Value* result) {
    int64_t a, b = 0, step = 1;
    if (!needInt("range", args[0], 1, &a)) return false;
    if (argc >= 2 && !needInt("range", args[1], 2, &b)) return false;
    if (argc >= 3 && !needInt("range", args[2], 3, &step)) return false;
    if (argc == 1) { b = a; a = 0; }
    if (step == 0) return vmError("range() 的步长不能为 0");
    int64_t len;
    if (step > 0) len = b > a ? (b - a + step - 1) / step : 0;
    else len = a > b ? (a - b - step - 1) / (-step) : 0;
    if (len > 100000000) return vmError("range() 生成的数组过大（%lld 个元素）", (long long)len);
    *result = rangeToArray(a, b, step);
    return true;
}

static bool nat_assert(int argc, Value* args, Value* result) {
    if (isTruthy(args[0])) { *result = NIL_VAL; return true; }
    if (argc >= 2) {
        ObjString* m;
        if (!vmStringify(args[1], false, &m)) return false;
        return vmError("断言失败: %s", m->chars);
    }
    return vmError("断言失败");
}

static char* dupCString(ObjString* s) {
    char* p = (char*)malloc((size_t)s->length + 1);
    if (p == NULL) exit(EXIT_RUNTIME_ERROR);
    memcpy(p, s->chars, (size_t)s->length + 1);
    return p;
}

static bool nat_assert_eq(int argc, Value* args, Value* result) {
    if (valuesEqual(args[0], args[1])) { *result = NIL_VAL; return true; }
    ObjString* s;
    if (!vmStringify(args[1], true, &s)) return false;
    char* expected = dupCString(s);   /* 第二次 stringify 可能触发 GC，先复制出来 */
    if (!vmStringify(args[0], true, &s)) { free(expected); return false; }
    char* actual = dupCString(s);
    char* msg = NULL;
    if (argc >= 3) {
        if (!vmStringify(args[2], false, &s)) { free(expected); free(actual); return false; }
        msg = dupCString(s);
    }
    if (msg) vmError("断言失败: %s: 期望 %s，实际 %s", msg, expected, actual);
    else vmError("断言失败: 期望 %s，实际 %s", expected, actual);
    free(expected);
    free(actual);
    free(msg);
    return false;
}

static bool nat_exit(int argc, Value* args, Value* result) {
    (void)result;
    int64_t code = 0;
    if (argc >= 1 && !needInt("exit", args[0], 1, &code)) return false;
    fflush(stdout);
    fflush(stderr);
    exit((int)code);
}

static bool nat_args(int argc, Value* args, Value* result) {
    (void)argc; (void)args;
    if (vm.scriptArgs != NULL) *result = OBJ_VAL(vm.scriptArgs);
    else *result = OBJ_VAL(newArray());
    return true;
}

/* ================= 文件 ================= */

static bool nat_read_file(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* path;
    if (!needStr("read_file", args[0], 1, &path)) return false;
    size_t len;
    char* data = readFileAll(path->chars, &len);
    if (data == NULL) return vmError("无法读取文件 '%s'", path->chars);
    *result = OBJ_VAL(takeString(data, (int)len));
    return true;
}

static bool writeFileMode(const char* name, Value* args, const char* mode, Value* result) {
    ObjString* path;
    if (!needStr(name, args[0], 1, &path)) return false;
    ObjString* s;
    if (!vmStringify(args[1], false, &s)) return false;
    FILE* f = platformFopen(path->chars, mode);
    if (f == NULL) return vmError("无法写入文件 '%s'", path->chars);
    size_t n = fwrite(s->chars, 1, (size_t)s->length, f);
    fclose(f);
    if (n != (size_t)s->length) return vmError("写入文件 '%s' 失败", path->chars);
    *result = NIL_VAL;
    return true;
}

static bool nat_write_file(int argc, Value* args, Value* result) { (void)argc; return writeFileMode("write_file", args, "wb", result); }
static bool nat_append_file(int argc, Value* args, Value* result) { (void)argc; return writeFileMode("append_file", args, "ab", result); }

static bool nat_file_exists(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* path;
    if (!needStr("file_exists", args[0], 1, &path)) return false;
    *result = BOOL_VAL(platformFileExists(path->chars));
    return true;
}

static void listDirCb(const char* name, bool isDir, void* ud) {
    (void)isDir;
    ObjArray* arr = (ObjArray*)ud;   /* 已被调用者压栈保护 */
    arrPush(arr, OBJ_VAL(cstr(name)));
}

static int cmpStringValues(const void* a, const void* b) {
    ObjString* x = AS_STRING(*(const Value*)a);
    ObjString* y = AS_STRING(*(const Value*)b);
    int n = x->length < y->length ? x->length : y->length;
    int c = memcmp(x->chars, y->chars, (size_t)n);
    return c != 0 ? c : x->length - y->length;
}

static bool nat_list_dir(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* path;
    if (!needStr("list_dir", args[0], 1, &path)) return false;
    ObjArray* arr = newArray();
    push(OBJ_VAL(arr));
    bool ok = platformListDir(path->chars, listDirCb, arr);
    pop();
    if (!ok) return vmError("无法列出目录 '%s'", path->chars);
    if (arr->items.count > 1) qsort(arr->items.values, (size_t)arr->items.count, sizeof(Value), cmpStringValues);
    *result = OBJ_VAL(arr);
    return true;
}

static bool nat_run(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* cmd;
    if (!needStr("run", args[0], 1, &cmd)) return false;
    char* out = NULL;
    size_t len = 0;
    int code = platformRun(cmd->chars, &out, &len);
    if (out == NULL) return vmError("无法执行命令 '%s'", cmd->chars);
    ObjString* output = takeString(out, (int)len);
    push(OBJ_VAL(output));
    ObjMap* map = newMap();
    push(OBJ_VAL(map));
    ObjString* k = cstr("code");
    push(OBJ_VAL(k));
    mapSet(map, OBJ_VAL(k), INT_VAL(code));
    pop();
    k = cstr("output");
    push(OBJ_VAL(k));
    mapSet(map, OBJ_VAL(k), OBJ_VAL(output));
    pop();
    pop();
    pop();
    *result = OBJ_VAL(map);
    return true;
}

/* ================= 拷贝与转换 ================= */

static ObjArray* copyArray(ObjArray* src) {
    ObjArray* arr = newArray();
    push(OBJ_VAL(arr));
    for (int i = 0; i < src->items.count; i++) writeValueArray(&arr->items, src->items.values[i]);
    pop();
    return arr;
}

static ObjMap* copyMap(ObjMap* src) {
    ObjMap* map = newMap();
    push(OBJ_VAL(map));
    for (int i = 0; i < src->entryCount; i++) {
        if (src->entries[i].live) mapSet(map, src->entries[i].key, src->entries[i].value);
    }
    pop();
    return map;
}

static bool nat_copy(int argc, Value* args, Value* result) {
    (void)argc;
    Value v = args[0];
    if (IS_ARRAY(v)) *result = OBJ_VAL(copyArray(AS_ARRAY(v)));
    else if (IS_MAP(v)) *result = OBJ_VAL(copyMap(AS_MAP(v)));
    else *result = v;
    return true;
}

static bool nat_fmt(int argc, Value* args, Value* result) {
    (void)argc;
    double x;
    int64_t digits;
    if (!needNum("fmt", args[0], 1, &x)) return false;
    if (!needInt("fmt", args[1], 2, &digits)) return false;
    if (digits < 0 || digits > 50) return vmError("fmt() 的小数位数必须在 0 到 50 之间");
    char buf[512];
    int n = snprintf(buf, sizeof buf, "%.*f", (int)digits, x);
    if (n < 0 || n >= (int)sizeof buf) return vmError("fmt() 的数值过大");
    *result = strVal(buf, n);
    return true;
}

static ObjArray* stringChars(ObjString* s) {
    ObjArray* arr = newArray();
    push(OBJ_VAL(arr));
    for (int off = 0; off < s->length;) {
        int n = utf8CharLen((unsigned char)s->chars[off]);
        if (off + n > s->length) n = s->length - off;
        arrPush(arr, strVal(s->chars + off, n));
        off += n;
    }
    pop();
    return arr;
}

static ObjArray* mapKeys(ObjMap* m) {
    ObjArray* arr = newArray();
    push(OBJ_VAL(arr));
    for (int i = 0; i < m->entryCount; i++)
        if (m->entries[i].live) writeValueArray(&arr->items, m->entries[i].key);
    pop();
    return arr;
}

static bool nat_array(int argc, Value* args, Value* result) {
    (void)argc;
    Value v = args[0];
    if (IS_RANGE(v)) {
        ObjRange* r = AS_RANGE(v);
        *result = rangeToArray(r->start, r->end, r->step);
    } else if (IS_STRING(v)) {
        *result = OBJ_VAL(stringChars(AS_STRING(v)));
    } else if (IS_MAP(v)) {
        *result = OBJ_VAL(mapKeys(AS_MAP(v)));
    } else if (IS_ARRAY(v)) {
        *result = OBJ_VAL(copyArray(AS_ARRAY(v)));
    } else {
        return vmError("无法把 %s 转换为数组", valueTypeName(v));
    }
    return true;
}

static bool radix(const char* name, Value v, int bits, const char* prefix, Value* result) {
    int64_t n;
    if (!needInt(name, v, 1, &n)) return false;
    char buf[80];
    int len = 0;
    uint64_t u = n < 0 ? (uint64_t)0 - (uint64_t)n : (uint64_t)n;
    char digits[70];
    int nd = 0;
    unsigned mask = (1u << bits) - 1;
    do {
        digits[nd++] = "0123456789abcdef"[u & mask];
        u >>= bits;
    } while (u != 0);
    if (n < 0) buf[len++] = '-';
    for (const char* p = prefix; *p; p++) buf[len++] = *p;
    while (nd > 0) buf[len++] = digits[--nd];
    *result = strVal(buf, len);
    return true;
}

static bool nat_hex(int argc, Value* args, Value* result) { (void)argc; return radix("hex", args[0], 4, "0x", result); }
static bool nat_bin(int argc, Value* args, Value* result) { (void)argc; return radix("bin", args[0], 1, "0b", result); }

/* ================= 字符串方法（args[0] 是接收者） ================= */

#define SELF_STR (AS_STRING(args[0]))

static bool str_len(int argc, Value* args, Value* result) {
    (void)argc;
    *result = INT_VAL(SELF_STR->charCount);
    return true;
}

static bool caseMap(Value* args, Value* result, bool upper) {
    ObjString* s = SELF_STR;
    char* buf = (char*)malloc((size_t)s->length + 1);
    if (buf == NULL) exit(EXIT_RUNTIME_ERROR);
    for (int i = 0; i < s->length; i++) {
        char c = s->chars[i];
        if (upper && c >= 'a' && c <= 'z') c = (char)(c - 32);
        else if (!upper && c >= 'A' && c <= 'Z') c = (char)(c + 32);
        buf[i] = c;
    }
    buf[s->length] = '\0';
    *result = OBJ_VAL(takeString(buf, s->length));
    return true;
}

static bool str_upper(int argc, Value* args, Value* result) { (void)argc; return caseMap(args, result, true); }
static bool str_lower(int argc, Value* args, Value* result) { (void)argc; return caseMap(args, result, false); }

static bool trimImpl(Value* args, Value* result, bool left, bool right) {
    ObjString* s = SELF_STR;
    int a = 0, b = s->length;
    if (left) while (a < b && isSpaceChar(s->chars[a])) a++;
    if (right) while (b > a && isSpaceChar(s->chars[b - 1])) b--;
    *result = strVal(s->chars + a, b - a);
    return true;
}

static bool str_trim(int argc, Value* args, Value* result) { (void)argc; return trimImpl(args, result, true, true); }
static bool str_trim_start(int argc, Value* args, Value* result) { (void)argc; return trimImpl(args, result, true, false); }
static bool str_trim_end(int argc, Value* args, Value* result) { (void)argc; return trimImpl(args, result, false, true); }

/* 字节级子串查找，从 from 开始；找不到返回 -1 */
static int findBytes(ObjString* s, ObjString* sub, int from) {
    if (sub->length == 0) return from <= s->length ? from : -1;
    for (int i = from; i + sub->length <= s->length; i++) {
        if (s->chars[i] == sub->chars[0] && memcmp(s->chars + i, sub->chars, (size_t)sub->length) == 0) return i;
    }
    return -1;
}

static bool str_split(int argc, Value* args, Value* result) {
    ObjString* s = SELF_STR;
    ObjArray* arr = newArray();
    push(OBJ_VAL(arr));
    if (argc < 2 || IS_NIL(args[1])) {
        int i = 0;
        while (i < s->length) {
            while (i < s->length && isSpaceChar(s->chars[i])) i++;
            int start = i;
            while (i < s->length && !isSpaceChar(s->chars[i])) i++;
            if (i > start) arrPush(arr, strVal(s->chars + start, i - start));
        }
    } else {
        ObjString* sep;
        if (!needStr("split", args[1], 1, &sep)) { pop(); return false; }
        if (sep->length == 0) { pop(); return vmError("split() 的分隔符不能是空字符串"); }
        int start = 0;
        for (;;) {
            int pos = findBytes(s, sep, start);
            if (pos < 0) break;
            arrPush(arr, strVal(s->chars + start, pos - start));
            start = pos + sep->length;
        }
        arrPush(arr, strVal(s->chars + start, s->length - start));
    }
    pop();
    *result = OBJ_VAL(arr);
    return true;
}

static bool str_lines(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* s = SELF_STR;
    ObjArray* arr = newArray();
    push(OBJ_VAL(arr));
    int start = 0;
    while (start < s->length) {
        int end = start;
        while (end < s->length && s->chars[end] != '\n') end++;
        int e = end;
        if (e > start && s->chars[e - 1] == '\r') e--;
        arrPush(arr, strVal(s->chars + start, e - start));
        start = end + 1;
    }
    pop();
    *result = OBJ_VAL(arr);
    return true;
}

static bool str_find(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* s = SELF_STR;
    ObjString* sub;
    if (!needStr("find", args[1], 1, &sub)) return false;
    int pos = findBytes(s, sub, 0);
    *result = INT_VAL(pos < 0 ? -1 : utf8Count(s->chars, pos));
    return true;
}

static bool str_count(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* s = SELF_STR;
    ObjString* sub;
    if (!needStr("count", args[1], 1, &sub)) return false;
    if (sub->length == 0) { *result = INT_VAL(s->charCount + 1); return true; }
    int64_t n = 0;
    for (int pos = findBytes(s, sub, 0); pos >= 0; pos = findBytes(s, sub, pos + sub->length)) n++;
    *result = INT_VAL(n);
    return true;
}

static bool str_contains(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* sub;
    if (!needStr("contains", args[1], 1, &sub)) return false;
    *result = BOOL_VAL(findBytes(SELF_STR, sub, 0) >= 0);
    return true;
}

static bool str_starts_with(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* s = SELF_STR;
    ObjString* p;
    if (!needStr("starts_with", args[1], 1, &p)) return false;
    *result = BOOL_VAL(p->length <= s->length && memcmp(s->chars, p->chars, (size_t)p->length) == 0);
    return true;
}

static bool str_ends_with(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* s = SELF_STR;
    ObjString* p;
    if (!needStr("ends_with", args[1], 1, &p)) return false;
    *result = BOOL_VAL(p->length <= s->length &&
                       memcmp(s->chars + s->length - p->length, p->chars, (size_t)p->length) == 0);
    return true;
}

static bool str_replace(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* s = SELF_STR;
    ObjString *from, *to;
    if (!needStr("replace", args[1], 1, &from) || !needStr("replace", args[2], 2, &to)) return false;
    if (from->length == 0) return vmError("replace() 要替换的子串不能是空字符串");
    Buf b;
    bufInit(&b);
    int start = 0;
    for (int pos = findBytes(s, from, 0); pos >= 0; pos = findBytes(s, from, start)) {
        bufAppend(&b, s->chars + start, (size_t)(pos - start));
        bufAppend(&b, to->chars, (size_t)to->length);
        start = pos + from->length;
    }
    bufAppend(&b, s->chars + start, (size_t)(s->length - start));
    *result = OBJ_VAL(bufFinish(&b));
    return true;
}

static bool str_chars(int argc, Value* args, Value* result) {
    (void)argc;
    *result = OBJ_VAL(stringChars(SELF_STR));
    return true;
}

static bool str_repeat(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* s = SELF_STR;
    int64_t n;
    if (!needInt("repeat", args[1], 1, &n)) return false;
    if (n <= 0 || s->length == 0) { *result = strVal("", 0); return true; }
    if (n > (int64_t)(0x7FFFFFFF / s->length)) return vmError("repeat() 的结果过长");
    Buf b;
    bufInit(&b);
    for (int64_t i = 0; i < n; i++) bufAppend(&b, s->chars, (size_t)s->length);
    *result = OBJ_VAL(bufFinish(&b));
    return true;
}

static bool str_slice(int argc, Value* args, Value* result) {
    ObjString* s = SELF_STR;
    int64_t a, b = s->charCount;
    if (!needInt("slice", args[1], 1, &a)) return false;
    if (argc >= 3 && !IS_NIL(args[2]) && !needInt("slice", args[2], 2, &b)) return false;
    a = clampSlice(a, s->charCount);
    b = clampSlice(b, s->charCount);
    if (b < a) b = a;
    int start = utf8Offset(s, (int)a);
    int end = utf8Offset(s, (int)b);
    *result = strVal(s->chars + start, end - start);
    return true;
}

static bool str_reverse(int argc, Value* args, Value* result) {
    (void)argc;
    ObjString* s = SELF_STR;
    char* buf = (char*)malloc((size_t)s->length + 1);
    if (buf == NULL) exit(EXIT_RUNTIME_ERROR);
    int w = s->length;
    for (int off = 0; off < s->length;) {
        int n = utf8CharLen((unsigned char)s->chars[off]);
        if (off + n > s->length) n = s->length - off;
        w -= n;
        memcpy(buf + w, s->chars + off, (size_t)n);
        off += n;
    }
    buf[s->length] = '\0';
    *result = OBJ_VAL(takeString(buf, s->length));
    return true;
}

/* mode: 0=左侧补齐(pad_left) 1=右侧补齐(pad_right) 2=居中 */
static bool padImpl(const char* name, int argc, Value* args, Value* result, int mode) {
    ObjString* s = SELF_STR;
    int64_t width;
    if (!needInt(name, args[1], 1, &width)) return false;
    const char* fill = " ";
    int fillLen = 1;
    if (argc >= 3) {
        ObjString* f;
        if (!needStr(name, args[2], 2, &f)) return false;
        if (f->charCount != 1) return vmError("%s() 的填充字符必须恰好是一个字符", name);
        fill = f->chars;
        fillLen = f->length;
    }
    int64_t total = width - s->charCount;
    if (total <= 0) { *result = args[0]; return true; }
    if (total > 100000000) return vmError("%s() 的宽度过大", name);
    int64_t left = mode == 0 ? total : (mode == 1 ? 0 : total / 2);
    int64_t right = total - left;
    Buf b;
    bufInit(&b);
    for (int64_t i = 0; i < left; i++) bufAppend(&b, fill, (size_t)fillLen);
    bufAppend(&b, s->chars, (size_t)s->length);
    for (int64_t i = 0; i < right; i++) bufAppend(&b, fill, (size_t)fillLen);
    *result = OBJ_VAL(bufFinish(&b));
    return true;
}

static bool str_pad_left(int argc, Value* args, Value* result) { return padImpl("pad_left", argc, args, result, 0); }
static bool str_pad_right(int argc, Value* args, Value* result) { return padImpl("pad_right", argc, args, result, 1); }
static bool str_center(int argc, Value* args, Value* result) { return padImpl("center", argc, args, result, 2); }

/* kind: 0=数字 1=字母 2=空白 */
static bool classify(Value* args, Value* result, int kind) {
    ObjString* s = SELF_STR;
    bool ok = s->length > 0;
    for (int i = 0; ok && i < s->length; i++) {
        unsigned char c = (unsigned char)s->chars[i];
        if (kind == 0) ok = c >= '0' && c <= '9';
        else if (kind == 1) ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
        else ok = isSpaceChar((char)c);
    }
    *result = BOOL_VAL(ok);
    return true;
}

static bool str_is_digit(int argc, Value* args, Value* result) { (void)argc; return classify(args, result, 0); }
static bool str_is_alpha(int argc, Value* args, Value* result) { (void)argc; return classify(args, result, 1); }
static bool str_is_space(int argc, Value* args, Value* result) { (void)argc; return classify(args, result, 2); }

/* ================= 数组方法 ================= */

#define SELF_ARR (AS_ARRAY(args[0]))

static bool arr_len(int argc, Value* args, Value* result) {
    (void)argc;
    *result = INT_VAL(SELF_ARR->items.count);
    return true;
}

static bool arr_push(int argc, Value* args, Value* result) {
    for (int i = 1; i < argc; i++) writeValueArray(&SELF_ARR->items, args[i]);
    *result = NIL_VAL;
    return true;
}

static bool arr_pop(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* a = SELF_ARR;
    if (a->items.count == 0) return vmError("不能对空数组 pop");
    *result = a->items.values[--a->items.count];
    return true;
}

static bool arr_insert(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* a = SELF_ARR;
    int64_t i;
    if (!needInt("insert", args[1], 1, &i)) return false;
    int64_t n = a->items.count;
    if (i < 0) i += n;
    if (i < 0 || i > n) return vmError("insert() 的下标 %lld 越界（数组长度 %lld）", (long long)AS_INT(args[1]), (long long)n);
    writeValueArray(&a->items, NIL_VAL);   /* 先扩容一个位置 */
    Value* v = a->items.values;
    memmove(v + i + 1, v + i, sizeof(Value) * (size_t)(n - i));
    v[i] = args[2];
    *result = NIL_VAL;
    return true;
}

static bool arr_remove_at(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* a = SELF_ARR;
    int64_t i;
    if (!needInt("remove_at", args[1], 1, &i)) return false;
    int64_t n = a->items.count;
    if (!normIndex(i, n, &i)) return vmError("remove_at() 的下标 %lld 越界（数组长度 %lld）", (long long)AS_INT(args[1]), (long long)n);
    Value* v = a->items.values;
    *result = v[i];
    memmove(v + i, v + i + 1, sizeof(Value) * (size_t)(n - i - 1));
    a->items.count--;
    return true;
}

static int arrIndexOf(ObjArray* a, Value x) {
    for (int i = 0; i < a->items.count; i++)
        if (valuesEqual(a->items.values[i], x)) return i;
    return -1;
}

static bool arr_remove(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* a = SELF_ARR;
    int i = arrIndexOf(a, args[1]);
    if (i >= 0) {
        memmove(a->items.values + i, a->items.values + i + 1, sizeof(Value) * (size_t)(a->items.count - i - 1));
        a->items.count--;
    }
    *result = BOOL_VAL(i >= 0);
    return true;
}

static bool arr_index_of(int argc, Value* args, Value* result) {
    (void)argc;
    *result = INT_VAL(arrIndexOf(SELF_ARR, args[1]));
    return true;
}

static bool arr_contains(int argc, Value* args, Value* result) {
    (void)argc;
    *result = BOOL_VAL(arrIndexOf(SELF_ARR, args[1]) >= 0);
    return true;
}

static bool arr_count(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* a = SELF_ARR;
    int64_t n = 0;
    for (int i = 0; i < a->items.count; i++)
        if (valuesEqual(a->items.values[i], args[1])) n++;
    *result = INT_VAL(n);
    return true;
}

static bool arr_join(int argc, Value* args, Value* result) {
    ObjString* sep = NULL;
    if (argc >= 2 && !needStr("join", args[1], 1, &sep)) return false;
    Buf b;
    bufInit(&b);
    for (int i = 0; i < SELF_ARR->items.count; i++) {
        if (i > 0 && sep != NULL) bufAppend(&b, sep->chars, (size_t)sep->length);
        ObjString* s;
        if (!vmStringify(SELF_ARR->items.values[i], false, &s)) { bufFree(&b); return false; }
        bufAppend(&b, s->chars, (size_t)s->length);
    }
    *result = OBJ_VAL(bufFinish(&b));
    return true;
}

static bool arr_reverse(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* a = SELF_ARR;
    Value* v = a->items.values;
    for (int i = 0, j = a->items.count - 1; i < j; i++, j--) {
        Value t = v[i];
        v[i] = v[j];
        v[j] = t;
    }
    *result = NIL_VAL;
    return true;
}

/* 稳定归并排序：对下标数组排序，比较 keys[idx] */
static bool mergeSort(int* idx, int* tmp, int n, Value* keys) {
    if (n < 2) return true;
    int mid = n / 2;
    if (!mergeSort(idx, tmp, mid, keys)) return false;
    if (!mergeSort(idx + mid, tmp, n - mid, keys)) return false;
    int i = 0, j = mid, k = 0;
    while (i < mid && j < n) {
        int c;
        if (!compareValues(keys[idx[j]], keys[idx[i]], &c)) return false;
        if (c < 0) tmp[k++] = idx[j++];
        else tmp[k++] = idx[i++];
    }
    while (i < mid) tmp[k++] = idx[i++];
    while (j < n) tmp[k++] = idx[j++];
    memcpy(idx, tmp, sizeof(int) * (size_t)n);
    return true;
}

/* 对数组 a（调用者已保护）原地排序；keyFn 为 NIL 表示按元素本身比较 */
static bool sortArray(ObjArray* a, Value keyFn) {
    int n = a->items.count;
    if (n < 2) return true;
    ObjArray* keys = NULL;
    if (!IS_NIL(keyFn)) {
        keys = newArray();
        push(OBJ_VAL(keys));
        for (int i = 0; i < n; i++) {
            if (i >= a->items.count) { pop(); return vmError("sort() 的 key 函数修改了数组"); }
            Value elem = a->items.values[i];
            Value k;
            if (!vmCall(keyFn, 1, &elem, &k)) { pop(); return false; }
            arrPush(keys, k);
        }
        if (a->items.count != n) { pop(); return vmError("sort() 的 key 函数修改了数组"); }
    }
    int* idx = (int*)malloc(sizeof(int) * (size_t)n * 2);
    if (idx == NULL) exit(EXIT_RUNTIME_ERROR);
    for (int i = 0; i < n; i++) idx[i] = i;
    Value* keyVals = keys != NULL ? keys->items.values : a->items.values;
    bool ok = mergeSort(idx, idx + n, n, keyVals);
    if (ok) {
        /* 按排好的下标重排（借用临时缓冲区，不分配 GC 内存） */
        Value* sorted = (Value*)malloc(sizeof(Value) * (size_t)n);
        if (sorted == NULL) exit(EXIT_RUNTIME_ERROR);
        for (int i = 0; i < n; i++) sorted[i] = a->items.values[idx[i]];
        memcpy(a->items.values, sorted, sizeof(Value) * (size_t)n);
        free(sorted);
    }
    free(idx);
    if (keys != NULL) pop();
    return ok;
}

static bool arr_sort(int argc, Value* args, Value* result) {
    if (!sortArray(SELF_ARR, argc >= 2 ? args[1] : NIL_VAL)) return false;
    *result = NIL_VAL;
    return true;
}

static bool arr_sorted(int argc, Value* args, Value* result) {
    ObjArray* copy = copyArray(SELF_ARR);
    push(OBJ_VAL(copy));
    bool ok = sortArray(copy, argc >= 2 ? args[1] : NIL_VAL);
    pop();
    if (!ok) return false;
    *result = OBJ_VAL(copy);
    return true;
}

static bool arr_map(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* out = newArray();
    push(OBJ_VAL(out));
    for (int i = 0; i < SELF_ARR->items.count; i++) {
        Value elem = SELF_ARR->items.values[i];
        Value r;
        if (!vmCall(args[1], 1, &elem, &r)) { pop(); return false; }
        arrPush(out, r);
    }
    pop();
    *result = OBJ_VAL(out);
    return true;
}

static bool arr_filter(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* out = newArray();
    push(OBJ_VAL(out));
    for (int i = 0; i < SELF_ARR->items.count; i++) {
        Value elem = SELF_ARR->items.values[i];
        Value r;
        if (!vmCall(args[1], 1, &elem, &r)) { pop(); return false; }
        if (isTruthy(r)) arrPush(out, elem);
    }
    pop();
    *result = OBJ_VAL(out);
    return true;
}

static bool arr_reduce(int argc, Value* args, Value* result) {
    ObjArray* a = SELF_ARR;
    int i = 0;
    Value acc;
    if (argc >= 3) {
        acc = args[2];
    } else {
        if (a->items.count == 0) return vmError("reduce() 不能对空数组求值（且没有提供初始值）");
        acc = a->items.values[0];
        i = 1;
    }
    push(acc);   /* 累加器放在栈顶受保护 */
    for (; i < SELF_ARR->items.count; i++) {
        Value pair[2];
        pair[0] = vm.stackTop[-1];
        pair[1] = SELF_ARR->items.values[i];
        Value r;
        if (!vmCall(args[1], 2, pair, &r)) { pop(); return false; }
        vm.stackTop[-1] = r;
    }
    *result = pop();
    return true;
}

static bool arr_each(int argc, Value* args, Value* result) {
    (void)argc;
    for (int i = 0; i < SELF_ARR->items.count; i++) {
        Value elem = SELF_ARR->items.values[i];
        Value r;
        if (!vmCall(args[1], 1, &elem, &r)) return false;
    }
    *result = NIL_VAL;
    return true;
}

/* mode: 0=find 1=find_index 2=any 3=all */
static bool predicateScan(Value* args, Value* result, int mode) {
    for (int i = 0; i < SELF_ARR->items.count; i++) {
        Value elem = SELF_ARR->items.values[i];
        Value r;
        if (!vmCall(args[1], 1, &elem, &r)) return false;
        bool t = isTruthy(r);
        if (mode == 3) {
            if (!t) { *result = BOOL_VAL(false); return true; }
        } else if (t) {
            if (mode == 0) *result = elem;
            else if (mode == 1) *result = INT_VAL(i);
            else *result = BOOL_VAL(true);
            return true;
        }
    }
    if (mode == 0) *result = NIL_VAL;
    else if (mode == 1) *result = INT_VAL(-1);
    else *result = BOOL_VAL(mode == 3);
    return true;
}

static bool arr_find(int argc, Value* args, Value* result) { (void)argc; return predicateScan(args, result, 0); }
static bool arr_find_index(int argc, Value* args, Value* result) { (void)argc; return predicateScan(args, result, 1); }
static bool arr_any(int argc, Value* args, Value* result) { (void)argc; return predicateScan(args, result, 2); }
static bool arr_all(int argc, Value* args, Value* result) { (void)argc; return predicateScan(args, result, 3); }

static bool arr_sum(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* a = SELF_ARR;
    int64_t isum = 0;
    double fsum = 0;
    bool isFloat = false;
    for (int i = 0; i < a->items.count; i++) {
        Value v = a->items.values[i];
        if (IS_INT(v)) {
            if (isFloat) fsum += (double)AS_INT(v);
            else isum = (int64_t)((uint64_t)isum + (uint64_t)AS_INT(v));
        } else if (IS_FLOAT(v)) {
            if (!isFloat) { isFloat = true; fsum = (double)isum; }
            fsum += AS_FLOAT(v);
        } else {
            return vmError("sum() 只能对数字求和，第 %d 个元素是 %s", i, valueTypeName(v));
        }
    }
    *result = isFloat ? FLOAT_VAL(fsum) : INT_VAL(isum);
    return true;
}

static bool arr_slice(int argc, Value* args, Value* result) {
    ObjArray* a = SELF_ARR;
    int64_t n = a->items.count;
    int64_t from, to = n;
    if (!needInt("slice", args[1], 1, &from)) return false;
    if (argc >= 3 && !IS_NIL(args[2]) && !needInt("slice", args[2], 2, &to)) return false;
    from = clampSlice(from, n);
    to = clampSlice(to, n);
    ObjArray* out = newArray();
    push(OBJ_VAL(out));
    for (int64_t i = from; i < to; i++) writeValueArray(&out->items, SELF_ARR->items.values[i]);
    pop();
    *result = OBJ_VAL(out);
    return true;
}

static bool arr_clear(int argc, Value* args, Value* result) {
    (void)argc;
    SELF_ARR->items.count = 0;
    *result = NIL_VAL;
    return true;
}

static bool arr_extend(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* other;
    if (!needArray("extend", args[1], 1, &other)) return false;
    int n = other->items.count;   /* 允许 a.extend(a) */
    for (int i = 0; i < n; i++) writeValueArray(&SELF_ARR->items, other->items.values[i]);
    *result = NIL_VAL;
    return true;
}

static bool arr_unique(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* out = newArray();
    push(OBJ_VAL(out));
    for (int i = 0; i < SELF_ARR->items.count; i++) {
        Value v = SELF_ARR->items.values[i];
        if (arrIndexOf(out, v) < 0) writeValueArray(&out->items, v);
    }
    pop();
    *result = OBJ_VAL(out);
    return true;
}

static bool arr_copy(int argc, Value* args, Value* result) {
    (void)argc;
    *result = OBJ_VAL(copyArray(SELF_ARR));
    return true;
}

static bool arr_first(int argc, Value* args, Value* result) {
    (void)argc;
    if (SELF_ARR->items.count == 0) return vmError("不能对空数组取 first()");
    *result = SELF_ARR->items.values[0];
    return true;
}

static bool arr_last(int argc, Value* args, Value* result) {
    (void)argc;
    if (SELF_ARR->items.count == 0) return vmError("不能对空数组取 last()");
    *result = SELF_ARR->items.values[SELF_ARR->items.count - 1];
    return true;
}

static bool arr_is_empty(int argc, Value* args, Value* result) {
    (void)argc;
    *result = BOOL_VAL(SELF_ARR->items.count == 0);
    return true;
}

static bool arr_min(int argc, Value* args, Value* result) { (void)argc; return minmax("min", 1, args, result, -1); }
static bool arr_max(int argc, Value* args, Value* result) { (void)argc; return minmax("max", 1, args, result, 1); }

/* ================= 字典方法 ================= */

#define SELF_MAP (AS_MAP(args[0]))

static bool checkKey(Value k) {
    if (!isHashable(k)) return vmError("字典的键不能是 %s 类型", valueTypeName(k));
    return true;
}

static bool map_len(int argc, Value* args, Value* result) {
    (void)argc;
    *result = INT_VAL(SELF_MAP->count);
    return true;
}

static bool map_keys(int argc, Value* args, Value* result) {
    (void)argc;
    *result = OBJ_VAL(mapKeys(SELF_MAP));
    return true;
}

static bool map_values(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* arr = newArray();
    push(OBJ_VAL(arr));
    ObjMap* m = SELF_MAP;
    for (int i = 0; i < m->entryCount; i++)
        if (m->entries[i].live) writeValueArray(&arr->items, m->entries[i].value);
    pop();
    *result = OBJ_VAL(arr);
    return true;
}

static bool map_items(int argc, Value* args, Value* result) {
    (void)argc;
    ObjArray* arr = newArray();
    push(OBJ_VAL(arr));
    ObjMap* m = SELF_MAP;
    for (int i = 0; i < m->entryCount; i++) {
        if (!m->entries[i].live) continue;
        ObjArray* pair = newArray();
        push(OBJ_VAL(pair));
        writeValueArray(&pair->items, m->entries[i].key);
        writeValueArray(&pair->items, m->entries[i].value);
        writeValueArray(&arr->items, OBJ_VAL(pair));
        pop();
    }
    pop();
    *result = OBJ_VAL(arr);
    return true;
}

static bool map_has(int argc, Value* args, Value* result) {
    (void)argc;
    Value v;
    *result = BOOL_VAL(isHashable(args[1]) && mapGet(SELF_MAP, args[1], &v));
    return true;
}

static bool map_get(int argc, Value* args, Value* result) {
    Value v;
    if (isHashable(args[1]) && mapGet(SELF_MAP, args[1], &v)) *result = v;
    else *result = argc >= 3 ? args[2] : NIL_VAL;
    return true;
}

static bool map_set(int argc, Value* args, Value* result) {
    (void)argc;
    if (!checkKey(args[1])) return false;
    mapSet(SELF_MAP, args[1], args[2]);
    *result = NIL_VAL;
    return true;
}

static bool map_remove(int argc, Value* args, Value* result) {
    (void)argc;
    Value v = NIL_VAL;
    if (isHashable(args[1]) && mapGet(SELF_MAP, args[1], &v)) mapDelete(SELF_MAP, args[1]);
    *result = v;
    return true;
}

static bool map_clear(int argc, Value* args, Value* result) {
    (void)argc;
    ObjMap* m = SELF_MAP;
    for (int i = 0; i < m->entryCount; i++) {
        m->entries[i].live = false;
        m->entries[i].key = NIL_VAL;
        m->entries[i].value = NIL_VAL;
    }
    m->count = 0;
    m->entryCount = 0;
    for (int i = 0; i < m->indexCap; i++) m->index[i] = 0;
    *result = NIL_VAL;
    return true;
}

static bool map_copy(int argc, Value* args, Value* result) {
    (void)argc;
    *result = OBJ_VAL(copyMap(SELF_MAP));
    return true;
}

static bool map_is_empty(int argc, Value* args, Value* result) {
    (void)argc;
    *result = BOOL_VAL(SELF_MAP->count == 0);
    return true;
}

static bool map_merge(int argc, Value* args, Value* result) {
    (void)argc;
    if (!IS_MAP(args[1])) return vmError("merge() 的参数必须是字典，实际是 %s", valueTypeName(args[1]));
    ObjMap* other = AS_MAP(args[1]);
    int n = other->entryCount;
    for (int i = 0; i < n && i < other->entryCount; i++) {
        if (other->entries[i].live) mapSet(SELF_MAP, other->entries[i].key, other->entries[i].value);
    }
    *result = NIL_VAL;
    return true;
}

/* ================= 区间方法 ================= */

#define SELF_RANGE (AS_RANGE(args[0]))

static bool rng_len(int argc, Value* args, Value* result) {
    (void)argc;
    *result = INT_VAL(rangeLength(SELF_RANGE));
    return true;
}

static bool rng_to_array(int argc, Value* args, Value* result) {
    (void)argc;
    ObjRange* r = SELF_RANGE;
    *result = rangeToArray(r->start, r->end, r->step);
    return true;
}

static bool rng_contains(int argc, Value* args, Value* result) {
    (void)argc;
    ObjRange* r = SELF_RANGE;
    Value x = args[1];
    int64_t n;
    if (IS_INT(x)) n = AS_INT(x);
    else if (IS_FLOAT(x) && AS_FLOAT(x) == floor(AS_FLOAT(x)) && fabs(AS_FLOAT(x)) < 9.2e18) n = (int64_t)AS_FLOAT(x);
    else { *result = BOOL_VAL(false); return true; }
    bool in;
    if (r->step > 0) in = n >= r->start && n < r->end && (n - r->start) % r->step == 0;
    else if (r->step < 0) in = n <= r->start && n > r->end && (r->start - n) % (-r->step) == 0;
    else in = false;
    *result = BOOL_VAL(in);
    return true;
}

/* ================= 注册 ================= */

static void defineConst(const char* name, Value v) {
    ObjString* s = cstr(name);
    push(OBJ_VAL(s));
    int slot = globalSlot(s);
    vm.globalValues.values[slot] = v;
    vmSetConstGlobal(slot, true);
    pop();
}

void initBuiltins(void) {
    /* 输入输出 */
    defineNative("print", nat_print, 0, -1);
    defineNative("write", nat_write, 0, -1);
    defineNative("input", nat_input, 0, 1);
    /* 类型与转换 */
    defineNative("type", nat_type, 1, 1);
    defineNative("int", nat_int, 1, 1);
    defineNative("float", nat_float, 1, 1);
    defineNative("str", nat_str, 1, 1);
    defineNative("bool", nat_bool, 1, 1);
    defineNative("len", nat_len, 1, 1);
    defineNative("array", nat_array, 1, 1);
    defineNative("copy", nat_copy, 1, 1);
    defineNative("fmt", nat_fmt, 2, 2);
    defineNative("hex", nat_hex, 1, 1);
    defineNative("bin", nat_bin, 1, 1);
    defineNative("chr", nat_chr, 1, 1);
    defineNative("ord", nat_ord, 1, 1);
    /* 数学 */
    defineNative("abs", nat_abs, 1, 1);
    defineNative("min", nat_min, 1, -1);
    defineNative("max", nat_max, 1, -1);
    defineNative("floor", nat_floor, 1, 1);
    defineNative("ceil", nat_ceil, 1, 1);
    defineNative("round", nat_round, 1, 2);
    defineNative("sqrt", nat_sqrt, 1, 1);
    defineNative("pow", nat_pow, 2, 2);
    defineNative("sin", nat_sin, 1, 1);
    defineNative("cos", nat_cos, 1, 1);
    defineNative("tan", nat_tan, 1, 1);
    defineNative("atan2", nat_atan2, 2, 2);
    defineNative("log", nat_log, 1, 2);
    defineNative("exp", nat_exp, 1, 1);
    /* 随机数 */
    defineNative("random", nat_random, 0, 0);
    defineNative("randint", nat_randint, 2, 2);
    defineNative("seed", nat_seed, 1, 1);
    defineNative("choice", nat_choice, 1, 1);
    defineNative("shuffle", nat_shuffle, 1, 1);
    /* 时间 */
    defineNative("clock", nat_clock, 0, 0);
    defineNative("time", nat_time, 0, 0);
    defineNative("sleep", nat_sleep, 1, 1);
    /* 其他 */
    defineNative("range", nat_range, 1, 3);
    defineNative("assert", nat_assert, 1, 2);
    defineNative("assert_eq", nat_assert_eq, 2, 3);
    defineNative("exit", nat_exit, 0, 1);
    defineNative("args", nat_args, 0, 0);
    /* 文件与进程 */
    defineNative("read_file", nat_read_file, 1, 1);
    defineNative("write_file", nat_write_file, 2, 2);
    defineNative("append_file", nat_append_file, 2, 2);
    defineNative("file_exists", nat_file_exists, 1, 1);
    defineNative("list_dir", nat_list_dir, 1, 1);
    defineNative("run", nat_run, 1, 1);
    defineNative("platform", nat_platform, 0, 0);

    /* 中文别名 */
    defineNative("打印", nat_print, 0, -1);
    defineNative("输入", nat_input, 0, 1);
    defineNative("长度", nat_len, 1, 1);
    defineNative("类型", nat_type, 1, 1);
    defineNative("整数", nat_int, 1, 1);
    defineNative("字符串", nat_str, 1, 1);
    defineNative("随机整数", nat_randint, 2, 2);

    /* 常量 */
    defineConst("PI", FLOAT_VAL(GZG_PI));
    defineConst("E", FLOAT_VAL(GZG_E));
    defineConst("INT_MAX", INT_VAL(INT64_MAX));
    defineConst("INT_MIN", INT_VAL(INT64_MIN));

    /* 字符串方法 */
    Table* t = &vm.stringMethods;
    defineNativeMethod(t, "len", str_len, 0, 0);
    defineNativeMethod(t, "upper", str_upper, 0, 0);
    defineNativeMethod(t, "lower", str_lower, 0, 0);
    defineNativeMethod(t, "trim", str_trim, 0, 0);
    defineNativeMethod(t, "trim_start", str_trim_start, 0, 0);
    defineNativeMethod(t, "trim_end", str_trim_end, 0, 0);
    defineNativeMethod(t, "split", str_split, 0, 1);
    defineNativeMethod(t, "lines", str_lines, 0, 0);
    defineNativeMethod(t, "find", str_find, 1, 1);
    defineNativeMethod(t, "count", str_count, 1, 1);
    defineNativeMethod(t, "contains", str_contains, 1, 1);
    defineNativeMethod(t, "starts_with", str_starts_with, 1, 1);
    defineNativeMethod(t, "ends_with", str_ends_with, 1, 1);
    defineNativeMethod(t, "replace", str_replace, 2, 2);
    defineNativeMethod(t, "chars", str_chars, 0, 0);
    defineNativeMethod(t, "repeat", str_repeat, 1, 1);
    defineNativeMethod(t, "slice", str_slice, 1, 2);
    defineNativeMethod(t, "reverse", str_reverse, 0, 0);
    defineNativeMethod(t, "pad_left", str_pad_left, 1, 2);
    defineNativeMethod(t, "pad_right", str_pad_right, 1, 2);
    defineNativeMethod(t, "center", str_center, 1, 2);
    defineNativeMethod(t, "is_digit", str_is_digit, 0, 0);
    defineNativeMethod(t, "is_alpha", str_is_alpha, 0, 0);
    defineNativeMethod(t, "is_space", str_is_space, 0, 0);

    /* 数组方法 */
    t = &vm.arrayMethods;
    defineNativeMethod(t, "len", arr_len, 0, 0);
    defineNativeMethod(t, "push", arr_push, 0, -1);
    defineNativeMethod(t, "pop", arr_pop, 0, 0);
    defineNativeMethod(t, "insert", arr_insert, 2, 2);
    defineNativeMethod(t, "remove_at", arr_remove_at, 1, 1);
    defineNativeMethod(t, "remove", arr_remove, 1, 1);
    defineNativeMethod(t, "index_of", arr_index_of, 1, 1);
    defineNativeMethod(t, "contains", arr_contains, 1, 1);
    defineNativeMethod(t, "count", arr_count, 1, 1);
    defineNativeMethod(t, "join", arr_join, 0, 1);
    defineNativeMethod(t, "reverse", arr_reverse, 0, 0);
    defineNativeMethod(t, "sort", arr_sort, 0, 1);
    defineNativeMethod(t, "sorted", arr_sorted, 0, 1);
    defineNativeMethod(t, "map", arr_map, 1, 1);
    defineNativeMethod(t, "filter", arr_filter, 1, 1);
    defineNativeMethod(t, "reduce", arr_reduce, 1, 2);
    defineNativeMethod(t, "each", arr_each, 1, 1);
    defineNativeMethod(t, "find", arr_find, 1, 1);
    defineNativeMethod(t, "find_index", arr_find_index, 1, 1);
    defineNativeMethod(t, "any", arr_any, 1, 1);
    defineNativeMethod(t, "all", arr_all, 1, 1);
    defineNativeMethod(t, "sum", arr_sum, 0, 0);
    defineNativeMethod(t, "min", arr_min, 0, 0);
    defineNativeMethod(t, "max", arr_max, 0, 0);
    defineNativeMethod(t, "slice", arr_slice, 1, 2);
    defineNativeMethod(t, "clear", arr_clear, 0, 0);
    defineNativeMethod(t, "extend", arr_extend, 1, 1);
    defineNativeMethod(t, "unique", arr_unique, 0, 0);
    defineNativeMethod(t, "copy", arr_copy, 0, 0);
    defineNativeMethod(t, "first", arr_first, 0, 0);
    defineNativeMethod(t, "last", arr_last, 0, 0);
    defineNativeMethod(t, "is_empty", arr_is_empty, 0, 0);

    /* 字典方法 */
    t = &vm.mapMethods;
    defineNativeMethod(t, "len", map_len, 0, 0);
    defineNativeMethod(t, "keys", map_keys, 0, 0);
    defineNativeMethod(t, "values", map_values, 0, 0);
    defineNativeMethod(t, "items", map_items, 0, 0);
    defineNativeMethod(t, "has", map_has, 1, 1);
    defineNativeMethod(t, "get", map_get, 1, 2);
    defineNativeMethod(t, "set", map_set, 2, 2);
    defineNativeMethod(t, "remove", map_remove, 1, 1);
    defineNativeMethod(t, "clear", map_clear, 0, 0);
    defineNativeMethod(t, "copy", map_copy, 0, 0);
    defineNativeMethod(t, "is_empty", map_is_empty, 0, 0);
    defineNativeMethod(t, "merge", map_merge, 1, 1);

    /* 区间方法 */
    t = &vm.rangeMethods;
    defineNativeMethod(t, "len", rng_len, 0, 0);
    defineNativeMethod(t, "to_array", rng_to_array, 0, 0);
    defineNativeMethod(t, "contains", rng_contains, 1, 1);
}
