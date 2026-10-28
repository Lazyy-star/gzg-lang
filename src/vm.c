/* 虚拟机：基于栈的字节码解释器。
 * GCC/Clang 下使用 computed goto（标签地址表）分派指令，其他编译器退化为 switch。
 * 异常：try 进入时记录处理器（调用深度、栈顶、catch 地址）；抛出时回退到最近的处理器。 */
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>

#include "builtins.h"
#include "compiler.h"
#include "memory.h"
#include "platform.h"
#include "vm.h"

VM vm;
static bool noUserCode = false;   /* 打印未捕获异常时不调用用户的 to_str */

static InterpretResult run(int baseFrame);

static void resetStack(void) {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.handlerCount = 0;
    vm.openUpvalues = NULL;
}

void push(Value value) { *vm.stackTop++ = value; }
Value pop(void) { return *--vm.stackTop; }
static inline Value peek(int distance) { return vm.stackTop[-1 - distance]; }

/* ---------------- 全局变量 ---------------- */

int globalSlot(ObjString* name) {
    Value idx;
    if (tableGet(&vm.globalNames, name, &idx)) return (int)AS_INT(idx);
    push(OBJ_VAL(name));
    int slot = vm.globalValues.count;
    writeValueArray(&vm.globalValues, UNDEF_VAL);
    writeValueArray(&vm.globalNameList, OBJ_VAL(name));
    tableSet(&vm.globalNames, name, INT_VAL(slot));
    pop();
    if (slot >= vm.globalConstCap) {
        int old = vm.globalConstCap;
        vm.globalConstCap = old < 64 ? 64 : old * 2;
        vm.globalConst = (uint8_t*)realloc(vm.globalConst, vm.globalConstCap);
        memset(vm.globalConst + old, 0, vm.globalConstCap - old);
    }
    return slot;
}

void vmSetConstGlobal(int slot, bool isConst) { vm.globalConst[slot] = isConst ? 1 : 0; }
bool vmIsConstGlobal(int slot) { return slot < vm.globalConstCap && vm.globalConst[slot]; }

void defineNative(const char* name, NativeFn fn, int minArgs, int maxArgs) {
    ObjString* s = cstr(name);
    push(OBJ_VAL(s));
    ObjNative* n = newNative(fn, name, minArgs, maxArgs);
    push(OBJ_VAL(n));
    int slot = globalSlot(s);
    vm.globalValues.values[slot] = OBJ_VAL(n);
    pop();
    pop();
}

void defineNativeMethod(Table* table, const char* name, NativeFn fn, int minArgs, int maxArgs) {
    ObjString* s = cstr(name);
    push(OBJ_VAL(s));
    ObjNative* n = newNative(fn, name, minArgs, maxArgs);
    n->isMethod = true;
    push(OBJ_VAL(n));
    tableSet(table, s, OBJ_VAL(n));
    pop();
    pop();
}

/* ---------------- 错误与异常 ---------------- */

bool vmThrowValue(Value v) {
    vm.pendingError = v;
    vm.hasError = true;
    return false;
}

bool vmError(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    return vmThrowValue(OBJ_VAL(cstr(buf)));
}

static int levenshtein(const char* a, int la, const char* b, int lb) {
    if (la > 40 || lb > 40) return 99;
    int d[41][41];
    for (int i = 0; i <= la; i++) d[i][0] = i;
    for (int j = 0; j <= lb; j++) d[0][j] = j;
    for (int i = 1; i <= la; i++)
        for (int j = 1; j <= lb; j++) {
            int c = a[i - 1] == b[j - 1] ? 0 : 1;
            int m = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < m) m = d[i][j - 1] + 1;
            if (d[i - 1][j - 1] + c < m) m = d[i - 1][j - 1] + c;
            d[i][j] = m;
        }
    return d[la][lb];
}

typedef struct {
    const char* target;
    int targetLen;
    const char* best;
    int bestDist;
} Suggest;

static void suggestConsider(Suggest* s, const char* cand, int len) {
    if (len == 0 || cand[0] == ' ') return;
    int d = levenshtein(s->target, s->targetLen, cand, len);
    if (s->targetLen <= 2 || len <= 2) return;   /* 名字太短时建议没有意义 */
    int limit = s->targetLen <= 5 ? 1 : 2;
    if (d <= limit && d < s->bestDist) {
        s->bestDist = d;
        s->best = cand;
    }
}

static void suggestTable(Suggest* s, Table* t) {
    for (int i = 0; i < t->capacity; i++)
        if (t->entries[i].key != NULL) suggestConsider(s, t->entries[i].key->chars, t->entries[i].key->length);
}

/* 返回 "，你是不是想写 'xxx'？" 或空串 */
static const char* hint(Suggest* s) {
    static char buf[160];
    if (s->best == NULL) return "";
    snprintf(buf, sizeof buf, "，你是不是想写 '%s'？", s->best);
    return buf;
}

/* 其他语言里的常见写法 -> GZG 的写法（编辑距离找不到时使用） */
static const char* foreignNames[][2] = {
    {"append", "push"}, {"add", "push"}, {"length", "len"}, {"size", "len"},
    {"strip", "trim"}, {"lstrip", "trim_start"}, {"rstrip", "trim_end"},
    {"startswith", "starts_with"}, {"endswith", "ends_with"}, {"startsWith", "starts_with"},
    {"endsWith", "ends_with"}, {"toUpperCase", "upper"}, {"toLowerCase", "lower"},
    {"indexOf", "index_of"}, {"index", "index_of"}, {"includes", "contains"}, {"forEach", "each"},
    {"splitlines", "lines"}, {"isdigit", "is_digit"}, {"isalpha", "is_alpha"}, {"pop_front", "remove_at(0)"},
    {"del", "remove"}, {"delete", "remove"}, {"contains_key", "has"}, {"has_key", "has"}, {"update", "merge"},
    {"ljust", "pad_right"}, {"rjust", "pad_left"}, {"zfill", "pad_left(n, \"0\")"}, {"format", "插值 \"{x}\""},
    {NULL, NULL},
};

static void suggestForeign(Suggest* s, ObjString* name) {
    if (s->best != NULL) return;
    for (int i = 0; foreignNames[i][0] != NULL; i++) {
        if (strcmp(foreignNames[i][0], name->chars) == 0) {
            s->best = foreignNames[i][1];
            return;
        }
    }
}

static Suggest newSuggest(ObjString* name) {
    Suggest s = {name->chars, name->length, NULL, 99};
    return s;
}

static const char* foreignGlobals[][2] = {
    {"None", "nil"}, {"null", "nil"}, {"True", "true"}, {"False", "false"}, {"println", "print"},
    {"printf", "print"}, {"puts", "print"}, {"console", "print"}, {"echo", "print"}, {"raw_input", "input"},
    {"length", "len"}, {"size", "len"}, {"def", "fn"}, {"function", "fn"}, {"var", "let"}, {"elif_", "elif"},
    {"self_", "self"}, {"this", "self"}, {"sorted", "arr.sorted()"}, {"sum", "arr.sum()"}, {"list", "array"},
    {"dict", "{}"}, {"isinstance", "type(x) == \"名字\""}, {"math", "sqrt / floor 等全局函数"},
    {NULL, NULL},
};

static const char* globalHint(ObjString* name) {
    Suggest s = newSuggest(name);
    for (int i = 0; i < vm.globalNameList.count; i++) {
        if (IS_UNDEF(vm.globalValues.values[i])) continue;
        ObjString* n = AS_STRING(vm.globalNameList.values[i]);
        suggestConsider(&s, n->chars, n->length);
    }
    for (int i = 0; s.best == NULL && foreignGlobals[i][0] != NULL; i++)
        if (strcmp(foreignGlobals[i][0], name->chars) == 0) s.best = foreignGlobals[i][1];
    return hint(&s);
}

static int frameLine(CallFrame* frame) {
    ObjFunction* f = frame->closure->function;
    size_t instruction = frame->ip - f->chunk.code - 1;
    if (frame->ip <= f->chunk.code) instruction = 0;
    return f->chunk.lines[instruction];
}

static void printTraceback(void) {
    fflush(stdout);
    ObjString* msg = NULL;
    Value err = vm.pendingError;
    if (IS_STRING(err)) msg = AS_STRING(err);
    else {
        noUserCode = true;
        if (!vmStringify(err, true, &msg)) msg = cstr("<无法显示的异常值>");
        noUserCode = false;
    }
    fprintf(stderr, "运行错误: %s\n", msg->chars);
    fprintf(stderr, "调用栈（最近的调用在最后）:\n");
    int n = vm.frameCount;
    for (int i = 0; i < n; i++) {
        if (n > 20 && i == 10) {
            fprintf(stderr, "  ……省略 %d 层……\n", n - 20);
            i = n - 10;
        }
        CallFrame* frame = &vm.frames[i];
        ObjFunction* f = frame->closure->function;
        fprintf(stderr, "  %s:%d  %s\n", f->file ? f->file->chars : "?", frameLine(frame),
                f->name == NULL ? "<主程序>" : f->name->chars);
    }
}

/* ---------------- 调用（供原生函数回调） ---------------- */

static bool callValue(Value callee, int argCount);

/* 栈上已有 [槽0 实参...]，调用 callee 并运行到返回；成功时结果写入 *result 且栈恢复原状 */
static bool callAndRun(Value callee, int argc, Value* result) {
    Value* base = vm.stackTop - argc - 1;
    int frameBefore = vm.frameCount;
    if (!callValue(callee, argc)) {
        vm.stackTop = base;
        return false;
    }
    if (vm.frameCount > frameBefore && run(frameBefore) != INTERPRET_OK) {
        vm.stackTop = base;
        return false;
    }
    *result = pop();
    vm.stackTop = base;
    return true;
}

bool vmCall(Value callee, int argc, Value* args, Value* result) {
    if (vm.stackTop + argc + 1 >= vm.stack + STACK_MAX) return vmError("栈空间耗尽");
    push(callee);
    for (int i = 0; i < argc; i++) push(args[i]);
    return callAndRun(callee, argc, result);
}

/* ---------------- 转字符串 ---------------- */

typedef struct {
    char* buf;
    int len, cap;
} StrBuf;

static void sbPut(StrBuf* sb, const char* s, int n) {
    if (sb->len + n + 1 > sb->cap) {
        while (sb->len + n + 1 > sb->cap) sb->cap = sb->cap < 64 ? 64 : sb->cap * 2;
        sb->buf = (char*)realloc(sb->buf, sb->cap);
    }
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

static void sbStr(StrBuf* sb, const char* s) { sbPut(sb, s, (int)strlen(s)); }

static void sbQuoted(StrBuf* sb, ObjString* s) {
    sbPut(sb, "\"", 1);
    for (int i = 0; i < s->length; i++) {
        unsigned char c = (unsigned char)s->chars[i];
        switch (c) {
            case '\n': sbPut(sb, "\\n", 2); break;
            case '\t': sbPut(sb, "\\t", 2); break;
            case '\r': sbPut(sb, "\\r", 2); break;
            case '"': sbPut(sb, "\\\"", 2); break;
            case '\\': sbPut(sb, "\\\\", 2); break;
            default:
                if (c < 0x20) {
                    char e[8];
                    snprintf(e, sizeof e, "\\x%02x", c);
                    sbStr(sb, e);
                } else {
                    sbPut(sb, (const char*)&c, 1);
                }
        }
    }
    sbPut(sb, "\"", 1);
}

static Obj* printing[64];   /* 正在打印的容器，用于检测循环引用 */
static int printingCount = 0;

static bool stringifyInto(StrBuf* sb, Value v, bool quoted) {
    char num[64];
    switch (v.type) {
        case VAL_NIL: sbStr(sb, "nil"); return true;
        case VAL_UNDEF: sbStr(sb, "<undefined>"); return true;
        case VAL_BOOL: sbStr(sb, AS_BOOL(v) ? "true" : "false"); return true;
        case VAL_INT: snprintf(num, sizeof num, "%" PRId64, AS_INT(v)); sbStr(sb, num); return true;
        case VAL_FLOAT: formatFloat(AS_FLOAT(v), num, sizeof num); sbStr(sb, num); return true;
        case VAL_OBJ: break;
    }
    Obj* o = AS_OBJ(v);
    if (o->type == OBJ_STRING) {
        if (quoted) sbQuoted(sb, (ObjString*)o);
        else sbPut(sb, ((ObjString*)o)->chars, ((ObjString*)o)->length);
        return true;
    }
    if (o->type == OBJ_ARRAY || o->type == OBJ_MAP || o->type == OBJ_INSTANCE) {
        for (int i = 0; i < printingCount; i++)
            if (printing[i] == o) {
                sbStr(sb, o->type == OBJ_ARRAY ? "[...]" : "{...}");
                return true;
            }
        if (printingCount >= 64) {
            sbStr(sb, "...");
            return true;
        }
    }
    bool ok = true;
    switch (o->type) {
        case OBJ_ARRAY: {
            printing[printingCount++] = o;
            ObjArray* a = (ObjArray*)o;
            sbPut(sb, "[", 1);
            for (int i = 0; i < a->items.count && ok; i++) {
                if (i > 0) sbPut(sb, ", ", 2);
                ok = stringifyInto(sb, a->items.values[i], true);
            }
            sbPut(sb, "]", 1);
            printingCount--;
            return ok;
        }
        case OBJ_MAP: {
            printing[printingCount++] = o;
            ObjMap* m = (ObjMap*)o;
            sbPut(sb, "{", 1);
            bool first = true;
            for (int i = 0; i < m->entryCount && ok; i++) {
                if (!m->entries[i].live) continue;
                if (!first) sbPut(sb, ", ", 2);
                first = false;
                ok = stringifyInto(sb, m->entries[i].key, true);
                sbPut(sb, ": ", 2);
                if (ok) ok = stringifyInto(sb, m->entries[i].value, true);
            }
            sbPut(sb, "}", 1);
            printingCount--;
            return ok;
        }
        case OBJ_INSTANCE: {
            ObjInstance* inst = (ObjInstance*)o;
            Value method;
            if (!noUserCode && tableGet(&inst->type->methods, vm.initString, &method)) {
                Value r;
                push(v);
                if (!callAndRun(method, 0, &r)) return false;
                if (!IS_STRING(r)) return vmError("%s.to_str() 必须返回字符串", inst->type->name->chars);
                sbPut(sb, AS_STRING(r)->chars, AS_STRING(r)->length);
                return true;
            }
            printing[printingCount++] = o;
            sbStr(sb, inst->type->name->chars);
            sbPut(sb, "(", 1);
            for (int i = 0; i < inst->fieldCount && ok; i++) {
                if (i > 0) sbPut(sb, ", ", 2);
                sbStr(sb, inst->type->fieldNames[i]->chars);
                sbPut(sb, ": ", 2);
                ok = stringifyInto(sb, inst->fields[i], true);
            }
            sbPut(sb, ")", 1);
            printingCount--;
            return ok;
        }
        case OBJ_STRUCT:
            sbStr(sb, "<struct ");
            sbStr(sb, ((ObjStruct*)o)->name->chars);
            sbStr(sb, ">");
            return true;
        case OBJ_CLOSURE: case OBJ_FUNCTION: {
            ObjFunction* f = o->type == OBJ_CLOSURE ? ((ObjClosure*)o)->function : (ObjFunction*)o;
            sbStr(sb, "<fn ");
            sbStr(sb, f->name ? f->name->chars : "匿名函数");
            sbStr(sb, ">");
            return true;
        }
        case OBJ_NATIVE:
            sbStr(sb, "<builtin ");
            sbStr(sb, ((ObjNative*)o)->name);
            sbStr(sb, ">");
            return true;
        case OBJ_BOUND: {
            Value m = ((ObjBound*)o)->method;
            sbStr(sb, "<method ");
            if (IS_NATIVE(m)) sbStr(sb, AS_NATIVE(m)->name);
            else sbStr(sb, AS_CLOSURE(m)->function->name ? AS_CLOSURE(m)->function->name->chars : "?");
            sbStr(sb, ">");
            return true;
        }
        case OBJ_RANGE: {
            ObjRange* r = (ObjRange*)o;
            if (r->step == 1) snprintf(num, sizeof num, "%" PRId64 "..%" PRId64, r->start, r->end);
            else snprintf(num, sizeof num, "range(%" PRId64 ", %" PRId64 ", %" PRId64 ")", r->start, r->end, r->step);
            sbStr(sb, num);
            return true;
        }
        default:
            sbStr(sb, "<object>");
            return true;
    }
}

bool vmStringify(Value v, bool quoted, ObjString** out) {
    if (IS_STRING(v) && !quoted) {
        *out = AS_STRING(v);
        return true;
    }
    StrBuf sb = {NULL, 0, 0};
    push(v);
    bool ok = stringifyInto(&sb, v, quoted);
    pop();
    if (!ok) {
        free(sb.buf);
        return false;
    }
    *out = copyString(sb.buf ? sb.buf : "", sb.len);
    free(sb.buf);
    return true;
}

/* ---------------- 调用机制 ---------------- */

static const char* fnName(ObjFunction* f) { return f->name ? f->name->chars : "匿名函数"; }

static bool pushFrame(ObjClosure* closure, int argCount) {
    if (vm.frameCount == FRAMES_MAX) return vmError("调用层数超过 %d 层（是不是无限递归？）", FRAMES_MAX);
    if (vm.stackTop + 300 >= vm.stack + STACK_MAX) return vmError("栈空间耗尽（是不是无限递归？）");
    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

static bool callClosure(ObjClosure* closure, int argCount) {
    ObjFunction* f = closure->function;
    if (argCount != f->arity) {
        if (argCount > f->arity || argCount < f->minArity) {
            if (f->minArity == f->arity)
                return vmError("函数 %s 需要 %d 个参数，实际传入 %d 个", fnName(f), f->arity, argCount);
            return vmError("函数 %s 需要 %d 到 %d 个参数，实际传入 %d 个", fnName(f), f->minArity, f->arity, argCount);
        }
        for (int i = argCount; i < f->arity; i++) push(UNDEF_VAL);   /* 由默认参数代码填充 */
        argCount = f->arity;
    }
    return pushFrame(closure, argCount);
}

/* 原生函数：callee 位于 args 前一个槽位；方法时 args[0]（即 callee 槽）为接收者 */
static bool callNative(ObjNative* native, int argCount, bool withReceiver) {
    int explicitArgs = argCount;
    if (explicitArgs < native->minArgs || (native->maxArgs >= 0 && explicitArgs > native->maxArgs)) {
        if (native->minArgs == native->maxArgs)
            return vmError("%s() 需要 %d 个参数，实际传入 %d 个", native->name, native->minArgs, explicitArgs);
        if (native->maxArgs < 0)
            return vmError("%s() 至少需要 %d 个参数，实际传入 %d 个", native->name, native->minArgs, explicitArgs);
        return vmError("%s() 需要 %d 到 %d 个参数，实际传入 %d 个", native->name, native->minArgs, native->maxArgs,
                       explicitArgs);
    }
    Value* args = withReceiver ? vm.stackTop - argCount - 1 : vm.stackTop - argCount;
    int argc = withReceiver ? argCount + 1 : argCount;
    Value result = NIL_VAL;
    if (!native->fn(argc, args, &result)) return false;
    vm.stackTop -= argCount + 1;
    push(result);
    return true;
}

/* 结构体构造：Point(1, 2) 或 Point(x: 1, y: 2)；kwNames 为 NULL 表示全是位置参数 */
static bool construct(ObjStruct* st, int argCount, ObjArray* kwNames) {
    int kwCount = kwNames ? kwNames->items.count : 0;
    int positional = argCount - kwCount;
    if (positional > st->fieldCount)
        return vmError("结构体 %s 只有 %d 个字段，传入了 %d 个参数", st->name->chars, st->fieldCount, argCount);
    ObjInstance* inst = newInstance(st);
    Value* args = vm.stackTop - argCount;
    for (int i = 0; i < st->fieldCount; i++) inst->fields[i] = UNDEF_VAL;
    for (int i = 0; i < positional; i++) inst->fields[i] = args[i];
    for (int k = 0; k < kwCount; k++) {
        Value idx;
        ObjString* nm = AS_STRING(kwNames->items.values[k]);
        if (!tableGet(&st->fieldIndex, nm, &idx)) {
            Suggest s = newSuggest(nm);
            suggestTable(&s, &st->fieldIndex);
            return vmError("结构体 %s 没有字段 '%s'%s", st->name->chars, nm->chars, hint(&s));
        }
        int fi = (int)AS_INT(idx);
        if (!IS_UNDEF(inst->fields[fi])) return vmError("字段 '%s' 被重复赋值", nm->chars);
        inst->fields[fi] = args[positional + k];
    }
    vm.stackTop[-argCount - 1] = OBJ_VAL(inst);   /* 先放到栈上保护，再分配默认值副本 */
    for (int i = 0; i < st->fieldCount; i++) {
        if (!IS_UNDEF(inst->fields[i])) continue;
        Value d = st->defaults[i];
        if (IS_UNDEF(d)) return vmError("创建 %s 时缺少字段 '%s'", st->name->chars, st->fieldNames[i]->chars);
        if (IS_ARRAY(d)) d = OBJ_VAL(newArray());
        else if (IS_MAP(d)) d = OBJ_VAL(newMap());
        inst->fields[i] = d;
    }
    vm.stackTop -= argCount;
    return true;
}

static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE: return callClosure(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: return callNative(AS_NATIVE(callee), argCount, false);
            case OBJ_BOUND: {
                ObjBound* b = AS_BOUND(callee);
                vm.stackTop[-argCount - 1] = b->receiver;
                if (IS_NATIVE(b->method)) return callNative(AS_NATIVE(b->method), argCount, true);
                return callClosure(AS_CLOSURE(b->method), argCount);
            }
            case OBJ_STRUCT: return construct(AS_STRUCT(callee), argCount, NULL);
            default: break;
        }
    }
    return vmError("%s 类型的值不能被调用", valueTypeName(callee));
}

/* 命名参数调用：栈上 [callee 位置参数... 命名参数值...]，kwNames 给出命名参数名 */
static bool callClosureKw(ObjClosure* closure, int argCount, ObjArray* kwNames) {
    ObjFunction* f = closure->function;
    int kwCount = kwNames->items.count;
    int positional = argCount - kwCount;
    if (positional > f->arity)
        return vmError("函数 %s 最多接受 %d 个参数，实际传入 %d 个", fnName(f), f->arity, positional);
    Value ordered[256];
    Value* args = vm.stackTop - argCount;
    for (int i = 0; i < f->arity; i++) ordered[i] = i < positional ? args[i] : UNDEF_VAL;
    for (int k = 0; k < kwCount; k++) {
        ObjString* nm = AS_STRING(kwNames->items.values[k]);
        int pi = -1;
        for (int i = 0; i < f->params.count; i++)
            if (AS_OBJ(f->params.values[i]) == (Obj*)nm) pi = i;
        if (pi < 0) {
            Suggest s = newSuggest(nm);
            for (int i = 0; i < f->params.count; i++)
                suggestConsider(&s, AS_CSTRING(f->params.values[i]), AS_STRING(f->params.values[i])->length);
            return vmError("函数 %s 没有名为 '%s' 的参数%s", fnName(f), nm->chars, hint(&s));
        }
        if (!IS_UNDEF(ordered[pi])) return vmError("参数 '%s' 被重复传入", nm->chars);
        ordered[pi] = args[positional + k];
    }
    for (int i = 0; i < f->minArity; i++)
        if (IS_UNDEF(ordered[i]))
            return vmError("调用 %s 时缺少参数 '%s'", fnName(f), AS_CSTRING(f->params.values[i]));
    vm.stackTop -= argCount;
    for (int i = 0; i < f->arity; i++) push(ordered[i]);
    return pushFrame(closure, f->arity);
}

static bool callValueKw(Value callee, int argCount, ObjArray* kwNames) {
    if (IS_CLOSURE(callee)) return callClosureKw(AS_CLOSURE(callee), argCount, kwNames);
    if (IS_STRUCT(callee)) return construct(AS_STRUCT(callee), argCount, kwNames);
    if (IS_BOUND(callee) && IS_CLOSURE(AS_BOUND(callee)->method)) {
        vm.stackTop[-argCount - 1] = AS_BOUND(callee)->receiver;
        return callClosureKw(AS_CLOSURE(AS_BOUND(callee)->method), argCount, kwNames);
    }
    if (IS_NATIVE(callee) || IS_BOUND(callee)) return vmError("内建函数不支持命名参数");
    return vmError("%s 类型的值不能被调用", valueTypeName(callee));
}

static Table* builtinMethods(Value v) {
    if (!IS_OBJ(v)) return NULL;
    switch (OBJ_TYPE(v)) {
        case OBJ_STRING: return &vm.stringMethods;
        case OBJ_ARRAY: return &vm.arrayMethods;
        case OBJ_MAP: return &vm.mapMethods;
        case OBJ_RANGE: return &vm.rangeMethods;
        default: return NULL;
    }
}

/* 查找 receiver.name 对应的可调用值。*keepReceiver=true 表示方法需要接收者留在槽 0。 */
static bool resolveMember(Value receiver, ObjString* name, Value* out, bool* keepReceiver) {
    *keepReceiver = false;
    if (IS_INSTANCE(receiver)) {
        ObjInstance* inst = AS_INSTANCE(receiver);
        Value idx;
        if (tableGet(&inst->type->fieldIndex, name, &idx)) {
            *out = inst->fields[AS_INT(idx)];
            return true;
        }
        if (tableGet(&inst->type->methods, name, out)) {
            *keepReceiver = true;
            return true;
        }
        Suggest s = newSuggest(name);
        suggestTable(&s, &inst->type->fieldIndex);
        suggestTable(&s, &inst->type->methods);
        return vmError("%s 没有字段或方法 '%s'%s", inst->type->name->chars, name->chars, hint(&s));
    }
    if (IS_STRUCT(receiver)) {
        if (tableGet(&AS_STRUCT(receiver)->methods, name, out)) {
            *keepReceiver = true;
            return true;
        }
        Suggest s = newSuggest(name);
        suggestTable(&s, &AS_STRUCT(receiver)->methods);
        return vmError("结构体 %s 没有方法 '%s'%s", AS_STRUCT(receiver)->name->chars, name->chars, hint(&s));
    }
    Table* methods = builtinMethods(receiver);
    if (methods != NULL && tableGet(methods, name, out)) {
        *keepReceiver = true;
        return true;
    }
    if (IS_MAP(receiver) && mapGet(AS_MAP(receiver), OBJ_VAL(name), out)) return true;
    if (methods != NULL) {
        Suggest s = newSuggest(name);
        suggestTable(&s, methods);
        suggestForeign(&s, name);
        return vmError("%s 类型没有方法 '%s'%s", valueTypeName(receiver), name->chars, hint(&s));
    }
    return vmError("%s 类型的值没有方法 '%s'", valueTypeName(receiver), name->chars);
}

static bool invoke(ObjString* name, int argCount) {
    Value receiver = peek(argCount);
    Value callee;
    bool keep;
    if (!resolveMember(receiver, name, &callee, &keep)) return false;
    if (keep) {
        if (IS_NATIVE(callee)) return callNative(AS_NATIVE(callee), argCount, true);
        return callClosure(AS_CLOSURE(callee), argCount);
    }
    vm.stackTop[-argCount - 1] = callee;
    return callValue(callee, argCount);
}

static bool invokeKw(ObjString* name, int argCount, ObjArray* kwNames) {
    Value receiver = peek(argCount);
    Value callee;
    bool keep;
    if (!resolveMember(receiver, name, &callee, &keep)) return false;
    if (keep) {
        if (IS_NATIVE(callee)) return vmError("内建方法 %s 不支持命名参数", name->chars);
        return callClosureKw(AS_CLOSURE(callee), argCount, kwNames);
    }
    vm.stackTop[-argCount - 1] = callee;
    return callValueKw(callee, argCount, kwNames);
}

static ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* prev = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prev = upvalue;
        upvalue = upvalue->next;
    }
    if (upvalue != NULL && upvalue->location == local) return upvalue;
    ObjUpvalue* created = newUpvalue(local);
    created->next = upvalue;
    if (prev == NULL) vm.openUpvalues = created;
    else prev->next = created;
    return created;
}

static void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

/* ---------------- 下标、切片、成员测试 ---------------- */

static bool normIndex(int64_t i, int64_t len, int64_t* out, const char* what) {
    int64_t j = i < 0 ? i + len : i;
    if (j < 0 || j >= len) return vmError("%s下标 %" PRId64 " 越界（长度为 %" PRId64 "）", what, i, len);
    *out = j;
    return true;
}

static ObjString* stringCharAt(ObjString* s, int64_t ci) {
    if (s->charCount == s->length) return copyString(s->chars + ci, 1);
    int off = utf8Offset(s, (int)ci);
    return copyString(s->chars + off, utf8CharLen((unsigned char)s->chars[off]));
}

static bool getIndex(Value obj, Value index, Value* out) {
    if (IS_ARRAY(obj)) {
        if (!IS_INT(index)) return vmError("数组下标必须是整数，实际是 %s", valueTypeName(index));
        ValueArray* a = &AS_ARRAY(obj)->items;
        int64_t i;
        if (!normIndex(AS_INT(index), a->count, &i, "数组")) return false;
        *out = a->values[i];
        return true;
    }
    if (IS_MAP(obj)) {
        if (!isHashable(index)) return vmError("字典的键不能是 %s 类型", valueTypeName(index));
        if (mapGet(AS_MAP(obj), index, out)) return true;
        ObjString* ks;
        if (!vmStringify(index, true, &ks)) return false;
        return vmError("字典中没有键 %s（可以用 .get(键, 默认值) 安全读取）", ks->chars);
    }
    if (IS_STRING(obj)) {
        if (!IS_INT(index)) return vmError("字符串下标必须是整数，实际是 %s", valueTypeName(index));
        ObjString* s = AS_STRING(obj);
        int64_t i;
        if (!normIndex(AS_INT(index), s->charCount, &i, "字符串")) return false;
        *out = OBJ_VAL(stringCharAt(s, i));
        return true;
    }
    if (IS_RANGE(obj)) {
        if (!IS_INT(index)) return vmError("区间下标必须是整数");
        ObjRange* r = AS_RANGE(obj);
        int64_t i;
        if (!normIndex(AS_INT(index), rangeLength(r), &i, "区间")) return false;
        *out = INT_VAL(r->start + i * r->step);
        return true;
    }
    return vmError("%s 类型的值不支持下标访问", valueTypeName(obj));
}

static bool setIndex(Value obj, Value index, Value value) {
    if (IS_ARRAY(obj)) {
        if (!IS_INT(index)) return vmError("数组下标必须是整数，实际是 %s", valueTypeName(index));
        ValueArray* a = &AS_ARRAY(obj)->items;
        int64_t i;
        if (!normIndex(AS_INT(index), a->count, &i, "数组")) return false;
        a->values[i] = value;
        return true;
    }
    if (IS_MAP(obj)) {
        if (!isHashable(index)) return vmError("字典的键不能是 %s 类型", valueTypeName(index));
        mapSet(AS_MAP(obj), index, value);
        return true;
    }
    if (IS_STRING(obj)) return vmError("字符串不可修改（可以用切片和 + 构造新字符串）");
    return vmError("%s 类型的值不支持下标赋值", valueTypeName(obj));
}

static void clampSlice(Value lo, Value hi, int64_t len, int64_t* a, int64_t* b) {
    int64_t x = IS_NIL(lo) ? 0 : AS_INT(lo);
    int64_t y = IS_NIL(hi) ? len : AS_INT(hi);
    if (x < 0) x += len;
    if (y < 0) y += len;
    if (x < 0) x = 0;
    if (y > len) y = len;
    if (x > len) x = len;
    if (y < x) y = x;
    *a = x;
    *b = y;
}

static bool sliceValue(Value obj, Value lo, Value hi, Value* out) {
    if ((!IS_NIL(lo) && !IS_INT(lo)) || (!IS_NIL(hi) && !IS_INT(hi))) return vmError("切片的边界必须是整数");
    int64_t a, b;
    if (IS_ARRAY(obj)) {
        ValueArray* src = &AS_ARRAY(obj)->items;
        clampSlice(lo, hi, src->count, &a, &b);
        ObjArray* r = newArray();
        push(OBJ_VAL(r));
        for (int64_t i = a; i < b; i++) writeValueArray(&r->items, AS_ARRAY(obj)->items.values[i]);
        pop();
        *out = OBJ_VAL(r);
        return true;
    }
    if (IS_STRING(obj)) {
        ObjString* s = AS_STRING(obj);
        clampSlice(lo, hi, s->charCount, &a, &b);
        int oa = utf8Offset(s, (int)a);
        int ob = utf8Offset(s, (int)b);
        *out = OBJ_VAL(copyString(s->chars + oa, ob - oa));
        return true;
    }
    return vmError("%s 类型的值不支持切片", valueTypeName(obj));
}

static bool containsValue(Value container, Value item, bool* out) {
    if (IS_ARRAY(container)) {
        ValueArray* a = &AS_ARRAY(container)->items;
        *out = false;
        for (int i = 0; i < a->count; i++)
            if (valuesEqual(a->values[i], item)) {
                *out = true;
                break;
            }
        return true;
    }
    if (IS_MAP(container)) {
        Value dummy;
        *out = isHashable(item) && mapGet(AS_MAP(container), item, &dummy);
        return true;
    }
    if (IS_STRING(container)) {
        if (!IS_STRING(item)) return vmError("'in' 左侧必须是字符串才能在字符串中查找");
        ObjString* s = AS_STRING(container);
        ObjString* sub = AS_STRING(item);
        if (sub->length == 0) { *out = true; return true; }
        *out = false;
        for (int i = 0; i + sub->length <= s->length; i++)
            if (memcmp(s->chars + i, sub->chars, sub->length) == 0) {
                *out = true;
                break;
            }
        return true;
    }
    if (IS_RANGE(container)) {
        ObjRange* r = AS_RANGE(container);
        *out = false;
        if (IS_INT(item)) {
            int64_t n = AS_INT(item);
            if (r->step > 0) *out = n >= r->start && n < r->end && (n - r->start) % r->step == 0;
            else *out = n <= r->start && n > r->end && (r->start - n) % (-r->step) == 0;
        }
        return true;
    }
    return vmError("'in' 右侧必须是数组、字典、字符串或区间，实际是 %s", valueTypeName(container));
}

/* ---------------- 算术 ---------------- */

static inline int64_t floorDiv(int64_t a, int64_t b) {
    if (b == -1) return (int64_t)(0 - (uint64_t)a);   /* 避免 INT64_MIN / -1 溢出崩溃 */
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

static inline int64_t floorMod(int64_t a, int64_t b) {
    if (b == -1) return 0;
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

static inline double floatMod(double a, double b) {
    double r = fmod(a, b);
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

static bool powValue(Value a, Value b, Value* out) {
    if (IS_INT(a) && IS_INT(b) && AS_INT(b) >= 0) {
        uint64_t base = (uint64_t)AS_INT(a), result = 1;
        int64_t e = AS_INT(b);
        while (e > 0) {
            if (e & 1) result *= base;
            base *= base;
            e >>= 1;
        }
        *out = INT_VAL((int64_t)result);
        return true;
    }
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        *out = FLOAT_VAL(pow(AS_NUMBER(a), AS_NUMBER(b)));
        return true;
    }
    return vmError("'**' 的操作数必须是数字，实际是 %s 和 %s", valueTypeName(a), valueTypeName(b));
}

/* 字符串、数组的 + 与 * */
static bool addObjects(Value a, Value b, Value* out) {
    if (IS_STRING(a) && IS_STRING(b)) {
        *out = OBJ_VAL(concatStrings(AS_STRING(a), AS_STRING(b)));
        return true;
    }
    if (IS_ARRAY(a) && IS_ARRAY(b)) {
        ObjArray* r = newArray();
        push(OBJ_VAL(r));
        ValueArray* x = &AS_ARRAY(a)->items;
        ValueArray* y = &AS_ARRAY(b)->items;
        for (int i = 0; i < x->count; i++) writeValueArray(&r->items, x->values[i]);
        for (int i = 0; i < y->count; i++) writeValueArray(&r->items, y->values[i]);
        pop();
        *out = OBJ_VAL(r);
        return true;
    }
    if (IS_STRING(a) || IS_STRING(b))
        return vmError("不能把 %s 和 %s 相加（字符串拼接其他类型请用 str(x) 或插值 \"{x}\"）",
                       valueTypeName(a), valueTypeName(b));
    return vmError("'+' 不支持 %s 和 %s", valueTypeName(a), valueTypeName(b));
}

static bool repeatValue(Value seq, int64_t n, Value* out) {
    if (n < 0) n = 0;
    if (IS_STRING(seq)) {
        ObjString* s = AS_STRING(seq);
        if ((int64_t)s->length * n > 1 << 30) return vmError("字符串重复后过长");
        int len = (int)(s->length * n);
        char* buf = (char*)malloc(len + 1);
        for (int64_t i = 0; i < n; i++) memcpy(buf + i * s->length, s->chars, s->length);
        buf[len] = '\0';
        *out = OBJ_VAL(takeString(buf, len));
        return true;
    }
    ObjArray* r = newArray();
    push(OBJ_VAL(r));
    ValueArray* src = &AS_ARRAY(seq)->items;
    for (int64_t k = 0; k < n; k++)
        for (int i = 0; i < src->count; i++) writeValueArray(&r->items, src->values[i]);
    pop();
    *out = OBJ_VAL(r);
    return true;
}

static int compareValues(Value a, Value b, bool* ok) {
    *ok = true;
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        if (IS_INT(a) && IS_INT(b)) return AS_INT(a) < AS_INT(b) ? -1 : AS_INT(a) > AS_INT(b);
        double x = AS_NUMBER(a), y = AS_NUMBER(b);
        return x < y ? -1 : x > y;
    }
    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString* x = AS_STRING(a);
        ObjString* y = AS_STRING(b);
        int n = x->length < y->length ? x->length : y->length;
        int c = memcmp(x->chars, y->chars, n);
        if (c != 0) return c < 0 ? -1 : 1;
        return x->length < y->length ? -1 : x->length > y->length;
    }
    *ok = false;
    return 0;
}

/* ---------------- 异常分派 ---------------- */

/* 处理挂起的异常。返回 true 表示已跳转到本次 run 内的 catch；false 表示需要向外传播（或未捕获）。 */
static bool handleThrow(int baseFrame) {
    vm.hasError = false;
    printingCount = 0;
    if (vm.handlerCount == 0) {
        if (!vm.uncaughtReported) {
            printTraceback();
            vm.uncaughtReported = true;
        }
        return false;
    }
    Handler* h = &vm.handlers[vm.handlerCount - 1];
    if (h->frameCount <= baseFrame) {
        /* 处理器属于外层（在某个原生函数调用之外）：退回到本次 run 的起点，交给外层 */
        if (vm.frameCount > baseFrame) closeUpvalues(vm.frames[baseFrame].slots);
        vm.frameCount = baseFrame;
        vm.hasError = true;
        return false;
    }
    vm.handlerCount--;
    closeUpvalues(h->stackTop);
    vm.frameCount = h->frameCount;
    vm.stackTop = h->stackTop;
    push(vm.pendingError);
    vm.pendingError = NIL_VAL;
    vm.frames[vm.frameCount - 1].ip = h->catchIp;
    return true;
}

/* ---------------- import ---------------- */

static ObjString* dirOf(const char* path) {
    const char* slash = NULL;
    for (const char* p = path; *p; p++)
        if (*p == '/' || *p == '\\') slash = p;
    if (slash == NULL) return copyString(".", 1);
    return copyString(path, (int)(slash - path));
}

/* 相对路径相对于“当前正在执行的文件”所在目录解析；同一文件只执行一次 */
static bool importModule(ObjString* path, ObjFunction* from) {
    char full[4096];
    bool absolute = path->chars[0] == '/' || path->chars[0] == '\\' || (path->length > 1 && path->chars[1] == ':');
    if (absolute || from->file == NULL) {
        snprintf(full, sizeof full, "%s", path->chars);
    } else {
        ObjString* dir = dirOf(from->file->chars);
        snprintf(full, sizeof full, "%s/%s", dir->chars, path->chars);
    }
    char* real = platformRealPath(full);
    ObjString* key = cstr(real ? real : full);
    free(real);
    Value dummy;
    if (tableGet(&vm.imports, key, &dummy)) {
        push(NIL_VAL);
        return true;
    }
    push(OBJ_VAL(key));
    tableSet(&vm.imports, key, BOOL_VAL(true));
    pop();
    size_t len;
    char* source = readFileAll(full, &len);
    if (source == NULL) return vmError("无法导入文件 '%s'（文件不存在或无法读取）", path->chars);
    ObjFunction* fn = compile(source, full, false);
    free(source);
    if (fn == NULL) return vmError("导入的文件 '%s' 有语法错误", path->chars);
    push(OBJ_VAL(fn));
    ObjClosure* closure = newClosure(fn);
    pop();
    push(OBJ_VAL(closure));
    return callClosure(closure, 0);
}

/* ---------------- 主循环 ---------------- */

#ifdef TRACE_EXEC
#define TRACE() do { \
        printf("          "); \
        for (Value* s = vm.stack; s < vm.stackTop; s++) { ObjString* t; printf("[ "); \
            if (vmStringify(*s, true, &t)) printf("%s", t->chars); printf(" ]"); } \
        printf("\n"); \
        disassembleInstruction(&frame->closure->function->chunk, (int)(ip - frame->closure->function->chunk.code)); \
    } while (0)
#else
#define TRACE() ((void)0)
#endif

static InterpretResult run(int baseFrame) {
    CallFrame* frame;
    uint8_t* ip;
    Value* slots;

#define LOAD_FRAME() (frame = &vm.frames[vm.frameCount - 1], ip = frame->ip, slots = frame->slots)
#define SAVE_IP() (frame->ip = ip)
#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_SHORT()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define THROW() do { SAVE_IP(); goto throw_; } while (0)
#define ERROR(...) do { SAVE_IP(); vmError(__VA_ARGS__); goto throw_; } while (0)
#define TRY(expr) do { SAVE_IP(); if (!(expr)) goto throw_; } while (0)
#define DISPATCH() do { TRACE(); goto *dispatch[*ip++]; } while (0)
#define CASE(op) L_##op:

    static void* dispatch[256];
    static bool tableReady = false;
    if (!tableReady) {
        for (int i = 0; i < 256; i++) dispatch[i] = &&L_BAD;
#define SET(op) dispatch[op] = &&L_##op
        SET(OP_CONSTANT); SET(OP_NIL); SET(OP_TRUE); SET(OP_FALSE); SET(OP_UNDEF);
        SET(OP_POP); SET(OP_DUP); SET(OP_DUP2); SET(OP_SWAP);
        SET(OP_GET_LOCAL); SET(OP_SET_LOCAL); SET(OP_GET_GLOBAL); SET(OP_SET_GLOBAL); SET(OP_DEFINE_GLOBAL);
        SET(OP_GET_UPVALUE); SET(OP_SET_UPVALUE); SET(OP_CLOSE_UPVALUE);
        SET(OP_GET_FIELD); SET(OP_SET_FIELD); SET(OP_GET_INDEX); SET(OP_SET_INDEX); SET(OP_SLICE);
        SET(OP_EQUAL); SET(OP_NOT_EQUAL); SET(OP_LESS); SET(OP_LESS_EQUAL); SET(OP_GREATER); SET(OP_GREATER_EQUAL);
        SET(OP_ADD); SET(OP_SUB); SET(OP_MUL); SET(OP_DIV); SET(OP_IDIV); SET(OP_MOD); SET(OP_POW);
        SET(OP_NEGATE); SET(OP_NOT);
        SET(OP_BAND); SET(OP_BOR); SET(OP_BXOR); SET(OP_SHL); SET(OP_SHR); SET(OP_BNOT);
        SET(OP_IN);
        SET(OP_JUMP); SET(OP_JUMP_IF_FALSE); SET(OP_POP_JUMP_IF_FALSE); SET(OP_JUMP_IF_TRUE); SET(OP_LOOP);
        SET(OP_JUMP_IF_NOT_UNDEF);
        SET(OP_CALL); SET(OP_CALL_KW); SET(OP_INVOKE); SET(OP_INVOKE_KW); SET(OP_CLOSURE); SET(OP_RETURN);
        SET(OP_ARRAY); SET(OP_MAP); SET(OP_BUILD_STRING); SET(OP_RANGE); SET(OP_STRUCT); SET(OP_METHOD);
        SET(OP_FOR_PREP); SET(OP_FOR_RANGE); SET(OP_FOR_STEP); SET(OP_FOR_ITER); SET(OP_IN_RANGE);
        SET(OP_TRY); SET(OP_END_TRY); SET(OP_THROW); SET(OP_IMPORT); SET(OP_REPL_PRINT);
#undef SET
        tableReady = true;
    }

    LOAD_FRAME();
    DISPATCH();

throw_:
    if (!handleThrow(baseFrame)) return INTERPRET_RUNTIME_ERROR;
    LOAD_FRAME();
    DISPATCH();

L_BAD:
    fprintf(stderr, "内部错误: 未知指令 %d\n", ip[-1]);
    return INTERPRET_RUNTIME_ERROR;

    CASE(OP_CONSTANT) push(READ_CONSTANT()); DISPATCH();
    CASE(OP_NIL) push(NIL_VAL); DISPATCH();
    CASE(OP_TRUE) push(BOOL_VAL(true)); DISPATCH();
    CASE(OP_FALSE) push(BOOL_VAL(false)); DISPATCH();
    CASE(OP_UNDEF) push(UNDEF_VAL); DISPATCH();
    CASE(OP_POP) vm.stackTop--; DISPATCH();
    CASE(OP_DUP) push(peek(0)); DISPATCH();
    CASE(OP_DUP2) { Value a = peek(1), b = peek(0); push(a); push(b); DISPATCH(); }
    CASE(OP_SWAP) { Value t = vm.stackTop[-1]; vm.stackTop[-1] = vm.stackTop[-2]; vm.stackTop[-2] = t; DISPATCH(); }
    CASE(OP_GET_LOCAL) push(slots[READ_BYTE()]); DISPATCH();
    CASE(OP_SET_LOCAL) slots[READ_BYTE()] = peek(0); DISPATCH();
    CASE(OP_GET_GLOBAL) {
        uint16_t idx = READ_SHORT();
        Value v = vm.globalValues.values[idx];
        if (IS_UNDEF(v)) {
            ObjString* name = AS_STRING(vm.globalNameList.values[idx]);
            ERROR("变量 '%s' 未定义%s", name->chars, globalHint(name));
        }
        push(v);
        DISPATCH();
    }
    CASE(OP_SET_GLOBAL) {
        uint16_t idx = READ_SHORT();
        if (IS_UNDEF(vm.globalValues.values[idx])) {
            ObjString* name = AS_STRING(vm.globalNameList.values[idx]);
            ERROR("变量 '%s' 未定义，请先用 let 声明%s", name->chars, globalHint(name));
        }
        vm.globalValues.values[idx] = peek(0);
        DISPATCH();
    }
    CASE(OP_DEFINE_GLOBAL) vm.globalValues.values[READ_SHORT()] = pop(); DISPATCH();
    CASE(OP_GET_UPVALUE) push(*frame->closure->upvalues[READ_BYTE()]->location); DISPATCH();
    CASE(OP_SET_UPVALUE) *frame->closure->upvalues[READ_BYTE()]->location = peek(0); DISPATCH();
    CASE(OP_CLOSE_UPVALUE) closeUpvalues(vm.stackTop - 1); vm.stackTop--; DISPATCH();

    CASE(OP_GET_FIELD) {
        ObjString* name = READ_STRING();
        Value obj = peek(0);
        if (IS_INSTANCE(obj)) {
            ObjInstance* inst = AS_INSTANCE(obj);
            Value idx;
            if (tableGet(&inst->type->fieldIndex, name, &idx)) {
                vm.stackTop[-1] = inst->fields[AS_INT(idx)];
                DISPATCH();
            }
        } else if (IS_MAP(obj)) {
            Value v;
            if (mapGet(AS_MAP(obj), OBJ_VAL(name), &v)) {
                vm.stackTop[-1] = v;
                DISPATCH();
            }
        }
        Value member;
        bool keep;
        TRY(resolveMember(obj, name, &member, &keep));
        if (keep) {
            ObjBound* b = newBound(obj, member);
            vm.stackTop[-1] = OBJ_VAL(b);
        } else {
            vm.stackTop[-1] = member;
        }
        DISPATCH();
    }
    CASE(OP_SET_FIELD) {
        ObjString* name = READ_STRING();
        Value obj = peek(1);
        Value v = peek(0);
        if (IS_INSTANCE(obj)) {
            ObjInstance* inst = AS_INSTANCE(obj);
            Value idx;
            if (!tableGet(&inst->type->fieldIndex, name, &idx)) {
                Suggest s = newSuggest(name);
                suggestTable(&s, &inst->type->fieldIndex);
                ERROR("%s 没有字段 '%s'（结构体的字段在定义时确定）%s", inst->type->name->chars, name->chars, hint(&s));
            }
            inst->fields[AS_INT(idx)] = v;
        } else if (IS_MAP(obj)) {
            SAVE_IP();
            mapSet(AS_MAP(obj), OBJ_VAL(name), v);
        } else {
            ERROR("不能给 %s 类型的值设置字段 '%s'", valueTypeName(obj), name->chars);
        }
        vm.stackTop -= 2;
        push(v);
        DISPATCH();
    }
    CASE(OP_GET_INDEX) {
        Value r;
        TRY(getIndex(peek(1), peek(0), &r));
        vm.stackTop -= 2;
        push(r);
        DISPATCH();
    }
    CASE(OP_SET_INDEX) {
        Value v = peek(0);
        TRY(setIndex(peek(2), peek(1), v));
        vm.stackTop -= 3;
        push(v);
        DISPATCH();
    }
    CASE(OP_SLICE) {
        Value r;
        TRY(sliceValue(peek(2), peek(1), peek(0), &r));
        vm.stackTop -= 3;
        push(r);
        DISPATCH();
    }

    CASE(OP_EQUAL) { Value b = pop(); vm.stackTop[-1] = BOOL_VAL(valuesEqual(vm.stackTop[-1], b)); DISPATCH(); }
    CASE(OP_NOT_EQUAL) { Value b = pop(); vm.stackTop[-1] = BOOL_VAL(!valuesEqual(vm.stackTop[-1], b)); DISPATCH(); }

#define COMPARE(op, sym) do { \
        Value b = vm.stackTop[-1], a = vm.stackTop[-2]; \
        bool r; \
        if (IS_INT(a) && IS_INT(b)) r = AS_INT(a) op AS_INT(b); \
        else { \
            bool ok; int c = compareValues(a, b, &ok); \
            if (!ok) ERROR("不能用 '%s' 比较 %s 和 %s", sym, valueTypeName(a), valueTypeName(b)); \
            if ((IS_FLOAT(a) && isnan(AS_FLOAT(a))) || (IS_FLOAT(b) && isnan(AS_FLOAT(b)))) r = false; \
            else r = c op 0; \
        } \
        vm.stackTop--; \
        vm.stackTop[-1] = BOOL_VAL(r); \
    } while (0)

    CASE(OP_LESS) COMPARE(<, "<"); DISPATCH();
    CASE(OP_LESS_EQUAL) COMPARE(<=, "<="); DISPATCH();
    CASE(OP_GREATER) COMPARE(>, ">"); DISPATCH();
    CASE(OP_GREATER_EQUAL) COMPARE(>=, ">="); DISPATCH();

    CASE(OP_ADD) {
        Value b = vm.stackTop[-1], a = vm.stackTop[-2];
        if (IS_INT(a) && IS_INT(b)) {
            vm.stackTop--;
            vm.stackTop[-1] = INT_VAL((int64_t)((uint64_t)AS_INT(a) + (uint64_t)AS_INT(b)));
        } else if (IS_NUMBER(a) && IS_NUMBER(b)) {
            vm.stackTop--;
            vm.stackTop[-1] = FLOAT_VAL(AS_NUMBER(a) + AS_NUMBER(b));
        } else {
            Value r;
            TRY(addObjects(a, b, &r));
            vm.stackTop -= 2;
            push(r);
        }
        DISPATCH();
    }
    CASE(OP_SUB) {
        Value b = vm.stackTop[-1], a = vm.stackTop[-2];
        if (IS_INT(a) && IS_INT(b)) vm.stackTop[-2] = INT_VAL((int64_t)((uint64_t)AS_INT(a) - (uint64_t)AS_INT(b)));
        else if (IS_NUMBER(a) && IS_NUMBER(b)) vm.stackTop[-2] = FLOAT_VAL(AS_NUMBER(a) - AS_NUMBER(b));
        else ERROR("'-' 不支持 %s 和 %s", valueTypeName(a), valueTypeName(b));
        vm.stackTop--;
        DISPATCH();
    }
    CASE(OP_MUL) {
        Value b = vm.stackTop[-1], a = vm.stackTop[-2];
        if (IS_INT(a) && IS_INT(b)) {
            vm.stackTop[-2] = INT_VAL((int64_t)((uint64_t)AS_INT(a) * (uint64_t)AS_INT(b)));
            vm.stackTop--;
        } else if (IS_NUMBER(a) && IS_NUMBER(b)) {
            vm.stackTop[-2] = FLOAT_VAL(AS_NUMBER(a) * AS_NUMBER(b));
            vm.stackTop--;
        } else if ((IS_STRING(a) || IS_ARRAY(a)) && IS_INT(b)) {
            Value r;
            TRY(repeatValue(a, AS_INT(b), &r));
            vm.stackTop -= 2;
            push(r);
        } else if (IS_INT(a) && (IS_STRING(b) || IS_ARRAY(b))) {
            Value r;
            TRY(repeatValue(b, AS_INT(a), &r));
            vm.stackTop -= 2;
            push(r);
        } else {
            ERROR("'*' 不支持 %s 和 %s", valueTypeName(a), valueTypeName(b));
        }
        DISPATCH();
    }
    CASE(OP_DIV) {
        Value b = vm.stackTop[-1], a = vm.stackTop[-2];
        if (!IS_NUMBER(a) || !IS_NUMBER(b)) ERROR("'/' 不支持 %s 和 %s", valueTypeName(a), valueTypeName(b));
        if (AS_NUMBER(b) == 0) ERROR("除数不能为 0");
        vm.stackTop[-2] = FLOAT_VAL(AS_NUMBER(a) / AS_NUMBER(b));
        vm.stackTop--;
        DISPATCH();
    }
    CASE(OP_IDIV) {
        Value b = vm.stackTop[-1], a = vm.stackTop[-2];
        if (IS_INT(a) && IS_INT(b)) {
            if (AS_INT(b) == 0) ERROR("整除的除数不能为 0");
            vm.stackTop[-2] = INT_VAL(floorDiv(AS_INT(a), AS_INT(b)));
        } else if (IS_NUMBER(a) && IS_NUMBER(b)) {
            if (AS_NUMBER(b) == 0) ERROR("整除的除数不能为 0");
            vm.stackTop[-2] = FLOAT_VAL(floor(AS_NUMBER(a) / AS_NUMBER(b)));
        } else {
            ERROR("'//' 不支持 %s 和 %s", valueTypeName(a), valueTypeName(b));
        }
        vm.stackTop--;
        DISPATCH();
    }
    CASE(OP_MOD) {
        Value b = vm.stackTop[-1], a = vm.stackTop[-2];
        if (IS_INT(a) && IS_INT(b)) {
            if (AS_INT(b) == 0) ERROR("取模的除数不能为 0");
            vm.stackTop[-2] = INT_VAL(floorMod(AS_INT(a), AS_INT(b)));
        } else if (IS_NUMBER(a) && IS_NUMBER(b)) {
            if (AS_NUMBER(b) == 0) ERROR("取模的除数不能为 0");
            vm.stackTop[-2] = FLOAT_VAL(floatMod(AS_NUMBER(a), AS_NUMBER(b)));
        } else {
            ERROR("'%%' 不支持 %s 和 %s", valueTypeName(a), valueTypeName(b));
        }
        vm.stackTop--;
        DISPATCH();
    }
    CASE(OP_POW) {
        Value r;
        TRY(powValue(peek(1), peek(0), &r));
        vm.stackTop--;
        vm.stackTop[-1] = r;
        DISPATCH();
    }
    CASE(OP_NEGATE) {
        Value a = peek(0);
        if (IS_INT(a)) vm.stackTop[-1] = INT_VAL((int64_t)(0 - (uint64_t)AS_INT(a)));
        else if (IS_FLOAT(a)) vm.stackTop[-1] = FLOAT_VAL(-AS_FLOAT(a));
        else ERROR("'-' 不能用于 %s", valueTypeName(a));
        DISPATCH();
    }
    CASE(OP_NOT) vm.stackTop[-1] = BOOL_VAL(!isTruthy(vm.stackTop[-1])); DISPATCH();

#define BITOP(expr, sym) do { \
        Value b = vm.stackTop[-1], a = vm.stackTop[-2]; \
        if (!IS_INT(a) || !IS_INT(b)) ERROR("'%s' 的操作数必须是整数", sym); \
        int64_t x = AS_INT(a), y = AS_INT(b); \
        vm.stackTop--; \
        vm.stackTop[-1] = INT_VAL(expr); \
    } while (0)

    CASE(OP_BAND) BITOP(x & y, "&"); DISPATCH();
    CASE(OP_BOR) BITOP(x | y, "|"); DISPATCH();
    CASE(OP_BXOR) BITOP(x ^ y, "^"); DISPATCH();
    CASE(OP_SHL) {
        Value b = vm.stackTop[-1];
        if (IS_INT(b) && (AS_INT(b) < 0 || AS_INT(b) > 63)) ERROR("移位位数必须在 0 到 63 之间");
        BITOP((int64_t)((uint64_t)x << y), "<<");
        DISPATCH();
    }
    CASE(OP_SHR) {
        Value b = vm.stackTop[-1];
        if (IS_INT(b) && (AS_INT(b) < 0 || AS_INT(b) > 63)) ERROR("移位位数必须在 0 到 63 之间");
        BITOP(x >> y, ">>");
        DISPATCH();
    }
    CASE(OP_BNOT) {
        if (!IS_INT(peek(0))) ERROR("'~' 的操作数必须是整数");
        vm.stackTop[-1] = INT_VAL(~AS_INT(vm.stackTop[-1]));
        DISPATCH();
    }
    CASE(OP_IN) {
        bool r = false;
        TRY(containsValue(peek(0), peek(1), &r));
        vm.stackTop--;
        vm.stackTop[-1] = BOOL_VAL(r);
        DISPATCH();
    }

    CASE(OP_JUMP) { uint16_t o = READ_SHORT(); ip += o; DISPATCH(); }
    CASE(OP_JUMP_IF_FALSE) { uint16_t o = READ_SHORT(); if (!isTruthy(peek(0))) ip += o; DISPATCH(); }
    CASE(OP_JUMP_IF_TRUE) { uint16_t o = READ_SHORT(); if (isTruthy(peek(0))) ip += o; DISPATCH(); }
    CASE(OP_POP_JUMP_IF_FALSE) {
        uint16_t o = READ_SHORT();
        Value c = pop();
        if (c.type == VAL_BOOL ? !AS_BOOL(c) : !isTruthy(c)) ip += o;
        DISPATCH();
    }
    CASE(OP_LOOP) { uint16_t o = READ_SHORT(); ip -= o; DISPATCH(); }
    CASE(OP_JUMP_IF_NOT_UNDEF) {
        uint8_t slot = READ_BYTE();
        uint16_t o = READ_SHORT();
        if (!IS_UNDEF(slots[slot])) ip += o;
        DISPATCH();
    }

    CASE(OP_CALL) {
        int argc = READ_BYTE();
        Value callee = peek(argc);
        SAVE_IP();
        if (IS_CLOSURE(callee) && AS_CLOSURE(callee)->function->arity == argc && vm.frameCount < FRAMES_MAX &&
            vm.stackTop + 300 < vm.stack + STACK_MAX) {
            /* 快速路径：参数个数正好匹配的普通函数调用 */
            CallFrame* f = &vm.frames[vm.frameCount++];
            f->closure = AS_CLOSURE(callee);
            f->ip = f->closure->function->chunk.code;
            f->slots = vm.stackTop - argc - 1;
            LOAD_FRAME();
            DISPATCH();
        }
        if (!callValue(callee, argc)) goto throw_;
        LOAD_FRAME();
        DISPATCH();
    }
    CASE(OP_CALL_KW) {
        int argc = READ_BYTE();
        ObjArray* names = AS_ARRAY(READ_CONSTANT());
        SAVE_IP();
        if (!callValueKw(peek(argc), argc, names)) goto throw_;
        LOAD_FRAME();
        DISPATCH();
    }
    CASE(OP_INVOKE) {
        ObjString* name = READ_STRING();
        int argc = READ_BYTE();
        SAVE_IP();
        if (!invoke(name, argc)) goto throw_;
        LOAD_FRAME();
        DISPATCH();
    }
    CASE(OP_INVOKE_KW) {
        ObjString* name = READ_STRING();
        int argc = READ_BYTE();
        ObjArray* names = AS_ARRAY(READ_CONSTANT());
        SAVE_IP();
        if (!invokeKw(name, argc, names)) goto throw_;
        LOAD_FRAME();
        DISPATCH();
    }
    CASE(OP_CLOSURE) {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        SAVE_IP();
        ObjClosure* closure = newClosure(function);
        push(OBJ_VAL(closure));
        for (int i = 0; i < closure->upvalueCount; i++) {
            uint8_t isLocal = READ_BYTE();
            uint8_t index = READ_BYTE();
            if (isLocal) closure->upvalues[i] = captureUpvalue(slots + index);
            else closure->upvalues[i] = frame->closure->upvalues[index];
        }
        DISPATCH();
    }
    CASE(OP_RETURN) {
        Value result = pop();
        closeUpvalues(slots);
        vm.frameCount--;
        while (vm.handlerCount > 0 && vm.handlers[vm.handlerCount - 1].frameCount > vm.frameCount)
            vm.handlerCount--;   /* 在 try 块中 return：丢弃该帧的处理器 */
        vm.stackTop = slots;
        push(result);
        if (vm.frameCount == baseFrame) return INTERPRET_OK;
        LOAD_FRAME();
        DISPATCH();
    }

    CASE(OP_ARRAY) {
        int n = READ_SHORT();
        SAVE_IP();
        ObjArray* a = newArray();
        push(OBJ_VAL(a));
        if (n > 0) {
            a->items.values = GROW_ARRAY(Value, NULL, 0, n);
            a->items.capacity = n;
            memcpy(a->items.values, vm.stackTop - 1 - n, sizeof(Value) * n);
            a->items.count = n;
        }
        vm.stackTop -= n + 1;
        push(OBJ_VAL(a));
        DISPATCH();
    }
    CASE(OP_MAP) {
        int n = READ_SHORT();
        SAVE_IP();
        ObjMap* m = newMap();
        push(OBJ_VAL(m));
        Value* kv = vm.stackTop - 1 - 2 * n;
        for (int i = 0; i < n; i++) {
            if (!isHashable(kv[2 * i])) ERROR("字典的键不能是 %s 类型", valueTypeName(kv[2 * i]));
            mapSet(m, kv[2 * i], kv[2 * i + 1]);
        }
        vm.stackTop -= 2 * n + 1;
        push(OBJ_VAL(m));
        DISPATCH();
    }
    CASE(OP_BUILD_STRING) {
        int n = READ_SHORT();
        SAVE_IP();
        StrBuf sb = {NULL, 0, 0};
        for (int i = n - 1; i >= 0; i--) {
            Value part = peek(i);
            if (IS_STRING(part)) {
                sbPut(&sb, AS_STRING(part)->chars, AS_STRING(part)->length);
            } else if (!stringifyInto(&sb, part, false)) {
                free(sb.buf);
                goto throw_;
            }
        }
        ObjString* s = copyString(sb.buf ? sb.buf : "", sb.len);
        free(sb.buf);
        vm.stackTop -= n;
        push(OBJ_VAL(s));
        DISPATCH();
    }
    CASE(OP_RANGE) {
        int incl = READ_BYTE();
        Value b = peek(0), a = peek(1);
        if (!IS_INT(a) || !IS_INT(b)) ERROR("区间的边界必须是整数，实际是 %s 和 %s", valueTypeName(a), valueTypeName(b));
        SAVE_IP();
        ObjRange* r = newRange(AS_INT(a), AS_INT(b) + (incl ? 1 : 0), 1);
        vm.stackTop -= 2;
        push(OBJ_VAL(r));
        DISPATCH();
    }

    CASE(OP_STRUCT) {
        ObjString* name = READ_STRING();
        ObjArray* desc = AS_ARRAY(READ_CONSTANT());
        SAVE_IP();
        ObjStruct* st = newStruct(name);
        push(OBJ_VAL(st));
        int n = desc->items.count / 2;
        if (n > 0) {
            ObjString** names = ALLOCATE(ObjString*, n);
            for (int i = 0; i < n; i++) names[i] = AS_STRING(desc->items.values[2 * i]);
            Value* defs = ALLOCATE(Value, n);   /* names 尚未挂到 st 上，但它们都被 desc 引用着 */
            for (int i = 0; i < n; i++) defs[i] = desc->items.values[2 * i + 1];
            st->fieldNames = names;
            st->defaults = defs;
            st->fieldCount = n;
            for (int i = 0; i < n; i++) tableSet(&st->fieldIndex, names[i], INT_VAL(i));
        }
        DISPATCH();
    }
    CASE(OP_METHOD) {
        ObjString* name = READ_STRING();
        SAVE_IP();
        ObjStruct* st = AS_STRUCT(peek(1));
        tableSet(&st->methods, name, peek(0));
        vm.stackTop--;
        DISPATCH();
    }

    CASE(OP_FOR_PREP) {
        uint8_t base = READ_BYTE();
        uint8_t incl = READ_BYTE();
        Value a = slots[base], b = slots[base + 1], s = slots[base + 2];
        if (!IS_INT(a) || !IS_INT(b)) ERROR("for 循环的区间边界必须是整数，实际是 %s 和 %s", valueTypeName(a), valueTypeName(b));
        if (!IS_INT(s)) ERROR("for 循环的 step 必须是整数");
        if (AS_INT(s) == 0) ERROR("for 循环的 step 不能为 0");
        if (incl) slots[base + 1] = INT_VAL(AS_INT(b) + (AS_INT(s) > 0 ? 1 : -1));
        DISPATCH();
    }
    CASE(OP_FOR_RANGE) {
        uint8_t base = READ_BYTE();
        uint16_t o = READ_SHORT();
        int64_t i = AS_INT(slots[base]), end = AS_INT(slots[base + 1]), step = AS_INT(slots[base + 2]);
        if (step > 0 ? i >= end : i <= end) ip += o;
        else push(INT_VAL(i));
        DISPATCH();
    }
    CASE(OP_FOR_STEP) {
        uint8_t base = READ_BYTE();
        uint16_t o = READ_SHORT();
        slots[base].as.i += slots[base + 2].as.i;
        ip -= o;
        DISPATCH();
    }
    CASE(OP_FOR_ITER) {
        uint8_t base = READ_BYTE();
        uint8_t mode = READ_BYTE();
        uint16_t o = READ_SHORT();
        Value seq = slots[base];
        int64_t state = AS_INT(slots[base + 1]);
        int64_t count = AS_INT(slots[base + 2]);
        if (IS_ARRAY(seq)) {
            ValueArray* a = &AS_ARRAY(seq)->items;
            if (state >= a->count) { ip += o; DISPATCH(); }
            if (mode == 2) push(INT_VAL(state));
            push(a->values[state]);
            slots[base + 1] = INT_VAL(state + 1);
        } else if (IS_RANGE(seq)) {
            ObjRange* r = AS_RANGE(seq);
            if (state >= rangeLength(r)) { ip += o; DISPATCH(); }
            if (mode == 2) push(INT_VAL(state));
            push(INT_VAL(r->start + state * r->step));
            slots[base + 1] = INT_VAL(state + 1);
        } else if (IS_STRING(seq)) {
            ObjString* s = AS_STRING(seq);
            if (state >= s->length) { ip += o; DISPATCH(); }
            int n = utf8CharLen((unsigned char)s->chars[state]);
            if (state + n > s->length) n = (int)(s->length - state);
            SAVE_IP();
            if (mode == 2) push(INT_VAL(count));
            ObjString* ch = copyString(s->chars + state, n);
            push(OBJ_VAL(ch));
            slots[base + 1] = INT_VAL(state + n);
            slots[base + 2] = INT_VAL(count + 1);
        } else if (IS_MAP(seq)) {
            ObjMap* m = AS_MAP(seq);
            while (state < m->entryCount && !m->entries[state].live) state++;
            if (state >= m->entryCount) { ip += o; DISPATCH(); }
            push(m->entries[state].key);
            if (mode == 2) push(m->entries[state].value);
            slots[base + 1] = INT_VAL(state + 1);
        } else {
            ERROR("%s 类型的值不能用 for 遍历", valueTypeName(seq));
        }
        DISPATCH();
    }
    CASE(OP_IN_RANGE) {
        uint8_t incl = READ_BYTE();
        Value hi = peek(0), lo = peek(1), x = peek(2);
        bool ok1, ok2, r = false;
        int c1 = compareValues(x, lo, &ok1);
        int c2 = compareValues(x, hi, &ok2);
        if (ok1 && ok2) r = c1 >= 0 && (incl ? c2 <= 0 : c2 < 0);
        vm.stackTop -= 3;
        push(BOOL_VAL(r));
        DISPATCH();
    }

    CASE(OP_TRY) {
        uint16_t o = READ_SHORT();
        if (vm.handlerCount == HANDLERS_MAX) ERROR("嵌套的 try 过多");
        Handler* h = &vm.handlers[vm.handlerCount++];
        h->frameCount = vm.frameCount;
        h->stackTop = vm.stackTop;
        h->catchIp = ip + o;
        DISPATCH();
    }
    CASE(OP_END_TRY) vm.handlerCount--; DISPATCH();
    CASE(OP_THROW) {
        vm.pendingError = pop();
        vm.hasError = true;
        THROW();
    }
    CASE(OP_IMPORT) {
        ObjString* path = READ_STRING();
        SAVE_IP();
        if (!importModule(path, frame->closure->function)) goto throw_;
        LOAD_FRAME();
        DISPATCH();
    }
    CASE(OP_REPL_PRINT) {
        Value v = peek(0);
        if (!IS_NIL(v)) {
            ObjString* s;
            SAVE_IP();
            if (!vmStringify(v, true, &s)) goto throw_;
            printf("%s\n", s->chars);
        }
        vm.stackTop--;
        DISPATCH();
    }

#undef COMPARE
#undef BITOP
}

/* ---------------- 入口 ---------------- */

InterpretResult interpret(const char* source, const char* filename) {
    ObjFunction* function = compile(source, filename, vm.replMode);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;
    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    pushFrame(closure, 0);
    InterpretResult result = run(0);
    if (result == INTERPRET_OK) pop();
    resetStack();
    vm.hasError = false;
    vm.pendingError = NIL_VAL;
    return result;
}

void initVM(void) {
    memset(&vm, 0, sizeof vm);
    vm.stack = (Value*)malloc(sizeof(Value) * STACK_MAX);
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;
    vm.pendingError = NIL_VAL;
    initTable(&vm.strings);
    initTable(&vm.globalNames);
    initValueArray(&vm.globalValues);
    initValueArray(&vm.globalNameList);
    initTable(&vm.imports);
    initTable(&vm.stringMethods);
    initTable(&vm.arrayMethods);
    initTable(&vm.mapMethods);
    initTable(&vm.rangeMethods);
    vm.initString = cstr("to_str");
    vm.emptyString = cstr("");
    initBuiltins();
}

void freeVM(void) {
    freeTable(&vm.strings);
    freeTable(&vm.globalNames);
    freeValueArray(&vm.globalValues);
    freeValueArray(&vm.globalNameList);
    freeTable(&vm.imports);
    freeTable(&vm.stringMethods);
    freeTable(&vm.arrayMethods);
    freeTable(&vm.mapMethods);
    freeTable(&vm.rangeMethods);
    free(vm.globalConst);
    freeObjects();
    free(vm.stack);
}

