/* 单遍编译器：Pratt 解析表达式，递归下降解析语句，边解析边生成字节码。
 * 局部变量放在栈槽中（编译期确定下标），全局变量编译成全局表下标，闭包捕获用 upvalue。 */
#include <errno.h>

#include "compiler.h"
#include "lexer.h"
#include "memory.h"
#include "vm.h"

typedef struct {
    Token previous, current, next;
    bool hadError;
    bool panicMode;
    bool needMore;       /* 输入在未闭合处结束（REPL 续行用） */
    int errorCount;
    ObjString* file;
    const char* fileName;
    bool replMode;
    bool armMode;        /* 正在解析 match 分支体：允许逗号结束语句 */
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  /* = += */
    PREC_PIPE,        /* |> */
    PREC_TERNARY,     /* ?: */
    PREC_OR,          /* or || */
    PREC_AND,         /* and && */
    PREC_NOT,         /* not */
    PREC_EQUALITY,    /* == != */
    PREC_COMPARISON,  /* < > <= >= in */
    PREC_RANGE,       /* .. ..= */
    PREC_BITOR,       /* | */
    PREC_BITXOR,      /* ^ */
    PREC_BITAND,      /* & */
    PREC_SHIFT,       /* << >> */
    PREC_TERM,        /* + - */
    PREC_FACTOR,      /* * / // % */
    PREC_UNARY,       /* - ! ~ */
    PREC_POWER,       /* ** 右结合 */
    PREC_CALL,        /* . () [] */
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth;           /* -1 表示已声明未初始化 */
    bool isCaptured;
    bool isConst;
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
    bool isConst;
} Upvalue;

typedef enum { TYPE_FUNCTION, TYPE_METHOD, TYPE_LAMBDA, TYPE_SCRIPT } FunctionType;

#define LOOP_JUMPS_MAX 256

typedef struct Loop {
    struct Loop* enclosing;
    int start;           /* while 的 continue 目标；for 为 -1（continue 前向跳转） */
    int scopeDepth;      /* 循环外层作用域深度：break/continue 弹出比它深的局部变量 */
    int tryDepth;
    int breaks[LOOP_JUMPS_MAX];
    int breakCount;
    int continues[LOOP_JUMPS_MAX];
    int continueCount;
} Loop;

typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;
    Local locals[LOCALS_MAX];
    int localCount;
    Upvalue upvalues[UPVALUES_MAX];
    int scopeDepth;
    Loop* loop;
    int tryDepth;
} Compiler;

static Parser parser;
static Compiler* current = NULL;

static Chunk* currentChunk(void) { return &current->function->chunk; }

/* ---------------- 错误报告 ---------------- */

static void printSourceLine(int line, int column) {
    const char* src = lexerSource();
    const char* p = src;
    for (int l = 1; l < line && *p; p++)
        if (*p == '\n') l++;
    const char* end = p;
    while (*end && *end != '\n' && *end != '\r') end++;
    fprintf(stderr, "  %4d | %.*s\n", line, (int)(end - p), p);
    fprintf(stderr, "       | %*s^\n", column - 1, "");
}

static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    parser.hadError = true;
    if (token->type == TOKEN_EOF) parser.needMore = true;
    if (++parser.errorCount > 20) return;
    fflush(stdout);
    fprintf(stderr, "%s:%d:%d: 语法错误: %s", parser.fileName, token->line, token->column, message);
    if (token->type == TOKEN_EOF) fprintf(stderr, "（在文件结尾）");
    else if (token->type == TOKEN_NEWLINE) fprintf(stderr, "（在行尾）");
    else if (token->type != TOKEN_ERROR) fprintf(stderr, "（在 '%.*s' 处）", token->length, token->start);
    fprintf(stderr, "\n");
    printSourceLine(token->line, token->column);
}

static void error(const char* message) { errorAt(&parser.previous, message); }
static void errorAtCurrent(const char* message) { errorAt(&parser.current, message); }

/* ---------------- 记号流（两个记号的前瞻） ---------------- */

static Token scanValid(void) {
    for (;;) {
        Token t = scanToken();
        if (t.type != TOKEN_ERROR) return t;
        if (strstr(t.start, "没有闭合") != NULL) parser.needMore = true;
        errorAt(&t, t.start);
    }
}

static void advance(void) {
    parser.previous = parser.current;
    parser.current = parser.next;
    if (parser.current.type != TOKEN_EOF) parser.next = scanValid();
}

static bool check(TokenType type) { return parser.current.type == type; }
static bool checkNext(TokenType type) { return parser.next.type == type; }

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

static void consume(TokenType type, const char* message) {
    if (check(type)) {
        advance();
        return;
    }
    errorAtCurrent(message);
}

static void skipNewlines(void) {
    while (check(TOKEN_NEWLINE) || check(TOKEN_SEMICOLON)) advance();
}

/* ---------------- 字节码生成 ---------------- */

static void emitByte(uint8_t byte) { writeChunk(currentChunk(), byte, parser.previous.line); }
static void emitBytes(uint8_t a, uint8_t b) { emitByte(a); emitByte(b); }
static void emitShort(uint16_t v) { emitByte((uint8_t)(v >> 8)); emitByte((uint8_t)(v & 0xff)); }
static void emitOpShort(uint8_t op, uint16_t v) { emitByte(op); emitShort(v); }

static int emitJump(uint8_t op) {
    emitByte(op);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;
    if (jump > UINT16_MAX) error("跳转距离过长（代码块太大）");
    currentChunk()->code[offset] = (uint8_t)((jump >> 8) & 0xff);
    currentChunk()->code[offset + 1] = (uint8_t)(jump & 0xff);
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);
    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("循环体过长");
    emitShort((uint16_t)offset);
}

static uint16_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT16_MAX) {
        error("单个函数中的常量过多");
        return 0;
    }
    return (uint16_t)constant;
}

static void emitConstant(Value value) { emitOpShort(OP_CONSTANT, makeConstant(value)); }

static uint16_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static uint16_t globalIndex(Token* name) {
    ObjString* s = copyString(name->start, name->length);
    int slot = globalSlot(s);
    if (slot > UINT16_MAX) error("全局变量过多");
    return (uint16_t)slot;
}

static void emitReturn(void) {
    emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

static void initCompiler(Compiler* compiler, FunctionType type, ObjString* name) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->loop = NULL;
    compiler->tryDepth = 0;
    if (name != NULL) push(OBJ_VAL(name));   /* newFunction 可能触发 GC */
    compiler->function = newFunction();
    if (name != NULL) pop();
    current = compiler;
    compiler->function->name = name;
    compiler->function->file = parser.file;
    compiler->function->isMethod = type == TYPE_METHOD;

    /* 槽位 0：方法中是 self，其他情况下保留给被调用者本身 */
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    local->isConst = true;
    if (type == TYPE_METHOD) {
        local->name.start = "self";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

static ObjFunction* endCompiler(void) {
    emitReturn();
    ObjFunction* function = current->function;
    current = current->enclosing;
    return function;
}

/* ---------------- 作用域与变量 ---------------- */

static void beginScope(void) { current->scopeDepth++; }

static void endScope(void) {
    current->scopeDepth--;
    while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) emitByte(OP_CLOSE_UPVALUE);
        else emitByte(OP_POP);
        current->localCount--;
    }
}

static bool identifiersEqual(Token* a, Token* b) {
    return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name, bool* isConst) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) error("不能在变量自己的初始化表达式中读取它");
            if (isConst) *isConst = local->isConst;
            return i;
        }
    }
    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal, bool isConst) {
    int count = compiler->function->upvalueCount;
    for (int i = 0; i < count; i++) {
        Upvalue* u = &compiler->upvalues[i];
        if (u->index == index && u->isLocal == isLocal) return i;
    }
    if (count == UPVALUES_MAX) {
        error("闭包捕获的变量过多");
        return 0;
    }
    compiler->upvalues[count].isLocal = isLocal;
    compiler->upvalues[count].index = index;
    compiler->upvalues[count].isConst = isConst;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name, bool* isConst) {
    if (compiler->enclosing == NULL) return -1;
    int local = resolveLocal(compiler->enclosing, name, isConst);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true, isConst ? *isConst : false);
    }
    int upvalue = resolveUpvalue(compiler->enclosing, name, isConst);
    if (upvalue != -1) return addUpvalue(compiler, (uint8_t)upvalue, false, isConst ? *isConst : false);
    return -1;
}

static void addLocal(Token name, bool isConst) {
    if (current->localCount == LOCALS_MAX) {
        error("单个函数中的局部变量过多（上限 256）");
        return;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
    local->isConst = isConst;
}

static void declareLocal(Token* name, bool isConst) {
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) break;
        if (identifiersEqual(name, &local->name)) {
            char msg[160];
            snprintf(msg, sizeof msg, "变量 '%.*s' 已在当前作用域中声明过", name->length, name->start);
            error(msg);
        }
    }
    addLocal(*name, isConst);
}

static void markInitialized(void) {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

/* 隐藏局部变量（for 循环的计数器等），名字以空格开头，用户代码无法引用 */
static void addHiddenLocal(const char* name) {
    Token t;
    t.start = name;
    t.length = (int)strlen(name);
    t.line = parser.previous.line;
    t.column = 0;
    t.type = TOKEN_IDENTIFIER;
    addLocal(t, false);
    markInitialized();
}

static Token syntheticToken(const char* text) {
    Token t;
    t.start = text;
    t.length = (int)strlen(text);
    t.type = TOKEN_IDENTIFIER;
    t.line = parser.previous.line;
    t.column = 0;
    return t;
}

/* ---------------- 表达式：字面量 ---------------- */

static void expression(void);
static void statement(void);
static void declaration(void);
static void block(void);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static void function(FunctionType type, ObjString* name);
static void matchExpr(bool canAssign);

static bool allowIntMin = false;   /* 紧跟在一元负号后：允许 9223372036854775808，即 INT_MIN */

static Value numberValue(Token* tok) {
    char buf[128];
    int n = 0;
    for (int i = 0; i < tok->length && n < 126; i++)
        if (tok->start[i] != '_') buf[n++] = tok->start[i];
    buf[n] = '\0';
    if (tok->type == TOKEN_FLOAT) return FLOAT_VAL(strtod(buf, NULL));
    int base = 10;
    const char* digits = buf;
    if (n > 2 && buf[0] == '0') {
        char b = buf[1];
        if (b == 'x' || b == 'X') base = 16;
        else if (b == 'b' || b == 'B') base = 2;
        else if (b == 'o' || b == 'O') base = 8;
        if (base != 10) digits = buf + 2;
    }
    errno = 0;
    char* end;
    unsigned long long v = strtoull(digits, &end, base);
    if (*end != '\0' || end == digits) {
        error("数字格式不正确");
        return INT_VAL(0);
    }
    if (allowIntMin && base == 10 && v == (unsigned long long)INT64_MAX + 1) return INT_VAL(INT64_MIN);   /* 取负后仍是 INT_MIN */
    if (errno == ERANGE || (base == 10 && v > (unsigned long long)INT64_MAX)) {
        error("整数字面量超出 64 位范围");
        return INT_VAL(0);
    }
    return INT_VAL((int64_t)v);
}

static void number(bool canAssign) {
    (void)canAssign;
    Value v = numberValue(&parser.previous);
    allowIntMin = false;
    emitConstant(v);
}

/* 处理转义序列，返回 malloc 的缓冲区 */
static char* unescape(const char* s, int len, int* outLen) {
    char* buf = (char*)malloc(len + 1);
    int n = 0;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (c != '\\') {
            buf[n++] = c;
            continue;
        }
        if (++i >= len) break;
        char e = s[i];
        switch (e) {
            case 'n': buf[n++] = '\n'; break;
            case 't': buf[n++] = '\t'; break;
            case 'r': buf[n++] = '\r'; break;
            case '0': buf[n++] = '\0'; break;
            case 'e': buf[n++] = 27; break;
            case '\\': buf[n++] = '\\'; break;
            case '"': buf[n++] = '"'; break;
            case '\'': buf[n++] = '\''; break;
            case '{': buf[n++] = '{'; break;
            case '}': buf[n++] = '}'; break;
            case '\n': break;   /* 行尾反斜杠：续行 */
            case 'x': case 'u': {
                int digits = e == 'x' ? 2 : 4;
                if (e == 'u' && i + 1 < len && s[i + 1] == '{') {
                    /* \u{1F600}：i 指向 '{'，下面的循环用 s[++i] 读取数字 */
                    int j = i + 2;
                    while (j < len && s[j] != '}') j++;
                    digits = j - (i + 2);
                    i++;
                }
                unsigned cp = 0;
                int k = 0;
                for (; k < digits && i + 1 < len; k++) {
                    char h = s[++i];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= h - '0';
                    else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                    else { error("\\x / \\u 后面必须是十六进制数字"); break; }
                }
                if (i + 1 < len && s[i + 1] == '}' && e == 'u') i++;
                if (e == 'x' || cp < 0x80) buf[n++] = (char)cp;
                else if (cp < 0x800) { buf[n++] = (char)(0xC0 | (cp >> 6)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) {
                    buf[n++] = (char)(0xE0 | (cp >> 12));
                    buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    buf[n++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    buf[n++] = (char)(0xF0 | (cp >> 18));
                    buf[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                    buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    buf[n++] = (char)(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: {
                char msg[64];
                snprintf(msg, sizeof msg, "未知的转义序列 '\\%c'", e);
                error(msg);
            }
        }
    }
    buf[n] = '\0';
    *outLen = n;
    return buf;
}

static void emitStringPart(Token* t) {
    int len;
    char* s = unescape(t->start + 1, t->length - 2, &len);
    ObjString* str = copyString(s, len);
    free(s);
    emitConstant(OBJ_VAL(str));
}

static void string(bool canAssign) {
    (void)canAssign;
    emitStringPart(&parser.previous);
}

static void rawString(bool canAssign) {
    (void)canAssign;
    Token* t = &parser.previous;
    emitConstant(OBJ_VAL(copyString(t->start + 1, t->length - 2)));
}

/* "a{x}b{y}c" => 片段与表达式交替压栈，最后 BUILD_STRING 拼接 */
static void interpolation(bool canAssign) {
    (void)canAssign;
    int parts = 0;
    for (;;) {
        Token seg = parser.previous;   /* INTERP 片段 */
        if (seg.length > 2) {
            emitStringPart(&seg);
            parts++;
        }
        if ((check(TOKEN_STRING) || check(TOKEN_INTERP)) && parser.current.start[0] == '}') {
            errorAtCurrent("插值 {} 中缺少表达式");
            return;
        }
        expression();
        parts++;
        if (match(TOKEN_INTERP)) continue;
        consume(TOKEN_STRING, "插值表达式后面缺少 '}'");
        if (parser.previous.length > 2) {
            emitStringPart(&parser.previous);
            parts++;
        }
        break;
    }
    if (parts > UINT16_MAX) error("插值片段过多");
    emitOpShort(OP_BUILD_STRING, (uint16_t)parts);
}

static void literal(bool canAssign) {
    (void)canAssign;
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        default: return;
    }
}

static void grouping(bool canAssign) {
    (void)canAssign;
    expression();
    consume(TOKEN_RIGHT_PAREN, "缺少 ')'");
}

/* [a, b, c]：每 256 个元素生成一个数组再用 + 拼接，避免占用过多栈空间 */
static void arrayLiteral(bool canAssign) {
    (void)canAssign;
    int count = 0;
    bool chained = false;
    skipNewlines();
    if (!check(TOKEN_RIGHT_BRACKET)) {
        do {
            skipNewlines();
            if (check(TOKEN_RIGHT_BRACKET)) break;   /* 允许尾随逗号 */
            expression();
            skipNewlines();
            if (++count == 256) {
                emitOpShort(OP_ARRAY, (uint16_t)count);
                if (chained) emitByte(OP_ADD);
                chained = true;
                count = 0;
            }
        } while (match(TOKEN_COMMA));
    }
    skipNewlines();
    consume(TOKEN_RIGHT_BRACKET, "数组字面量缺少 ']'");
    if (count > 0 || !chained) {
        emitOpShort(OP_ARRAY, (uint16_t)count);
        if (chained) emitByte(OP_ADD);
    }
}

/* {name: v, "k": v, (expr): v}；标识符键视为字符串 */
static void mapLiteral(bool canAssign) {
    (void)canAssign;
    int count = 0;
    skipNewlines();
    if (!check(TOKEN_RIGHT_BRACE)) {
        do {
            skipNewlines();
            if (check(TOKEN_RIGHT_BRACE)) break;
            if (check(TOKEN_IDENTIFIER) && checkNext(TOKEN_COLON)) {
                advance();
                emitConstant(OBJ_VAL(copyString(parser.previous.start, parser.previous.length)));
            } else {
                parsePrecedence(PREC_TERNARY + 1);
            }
            consume(TOKEN_COLON, "字典字面量中键的后面缺少 ':'");
            skipNewlines();
            expression();
            skipNewlines();
            if (++count > 2000) {
                error("字典字面量过大（上限 2000 项），请改用循环插入");
                return;
            }
        } while (match(TOKEN_COMMA));
    }
    skipNewlines();
    consume(TOKEN_RIGHT_BRACE, "字典字面量缺少 '}'");
    emitOpShort(OP_MAP, (uint16_t)count);
}

/* ---------------- 表达式：变量与赋值 ---------------- */

static uint8_t compoundOp(TokenType t) {
    switch (t) {
        case TOKEN_PLUS_EQUAL: return OP_ADD;
        case TOKEN_MINUS_EQUAL: return OP_SUB;
        case TOKEN_STAR_EQUAL: return OP_MUL;
        case TOKEN_SLASH_EQUAL: return OP_DIV;
        case TOKEN_PERCENT_EQUAL: return OP_MOD;
        default: return 0;
    }
}

static bool matchCompound(uint8_t* op) {
    uint8_t o = compoundOp(parser.current.type);
    if (o == 0) return false;
    advance();
    *op = o;
    return true;
}

static void constError(Token* name) {
    char msg[160];
    snprintf(msg, sizeof msg, "不能给常量 '%.*s' 重新赋值", name->length, name->start);
    error(msg);
}

/* 函数里引用的名字落到了全局：若与某个可见局部变量拼写接近，记下来供运行时报错提示 */
static void recordLocalHint(Token* name) {
    const char* best = NULL;
    int bestLen = 0, bestD = 99;
    for (Compiler* c = current; c != NULL; c = c->enclosing) {
        for (int i = 0; i < c->localCount; i++) {
            Token* t = &c->locals[i].name;
            if (t->length == 0 || t->start[0] == ' ') continue;
            int d = editDistance(name->start, name->length, t->start, t->length);
            if (d < bestD && suggestLimitOk(name->length, t->length, d)) {
                bestD = d;
                best = t->start;
                bestLen = t->length;
            }
        }
    }
    if (best == NULL) return;
    ObjString* key = copyString(name->start, name->length);
    push(OBJ_VAL(key));
    ObjString* val = copyString(best, bestLen);
    push(OBJ_VAL(val));
    tableSet(&vm.localHints, key, OBJ_VAL(val));
    pop();
    pop();
}

static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    bool isConst = false;
    int arg = resolveLocal(current, &name, &isConst);
    bool wide = false;
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name, &isConst)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        if (current->enclosing != NULL) recordLocalHint(&name);
        arg = globalIndex(&name);
        isConst = vmIsConstGlobal(arg);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
        wide = true;
    }
#define EMIT_VAR(op) do { if (wide) emitOpShort(op, (uint16_t)arg); else emitBytes(op, (uint8_t)arg); } while (0)
    uint8_t cop;
    if (canAssign && match(TOKEN_EQUAL)) {
        if (isConst) constError(&name);
        expression();
        EMIT_VAR(setOp);
    } else if (canAssign && matchCompound(&cop)) {
        if (isConst) constError(&name);
        EMIT_VAR(getOp);
        expression();
        emitByte(cop);
        EMIT_VAR(setOp);
    } else {
        EMIT_VAR(getOp);
    }
#undef EMIT_VAR
}

static void variable(bool canAssign) { namedVariable(parser.previous, canAssign); }

static void selfExpr(bool canAssign) {
    (void)canAssign;
    Token name = syntheticToken("self");
    if (resolveLocal(current, &name, NULL) == -1 && resolveUpvalue(current, &name, NULL) == -1) {
        error("'self' 只能在结构体的方法中使用");
        return;
    }
    namedVariable(name, false);
}

/* ---------------- 表达式：调用、成员、下标 ---------------- */

/* 解析实参列表（左括号已消费）。命名参数 name: expr 必须在位置参数之后。
 * 返回实参总数；*kwConst 为命名参数名数组的常量下标，没有命名参数时为 -1。 */
static uint8_t argumentList(int* kwConst) {
    int argCount = 0;
    ObjArray* names = NULL;
    *kwConst = -1;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            if (check(TOKEN_RIGHT_PAREN)) break;   /* 允许尾随逗号 */
            if (check(TOKEN_IDENTIFIER) && checkNext(TOKEN_COLON)) {
                advance();
                ObjString* nm = copyString(parser.previous.start, parser.previous.length);
                if (names == NULL) {
                    push(OBJ_VAL(nm));
                    names = newArray();
                    *kwConst = makeConstant(OBJ_VAL(names));   /* 放进常量池即被 GC 根引用 */
                    pop();
                }
                for (int i = 0; i < names->items.count; i++)
                    if (AS_OBJ(names->items.values[i]) == (Obj*)nm) error("命名参数重复");
                push(OBJ_VAL(nm));
                writeValueArray(&names->items, OBJ_VAL(nm));
                pop();
                advance();   /* ':' */
            } else if (names != NULL) {
                error("位置参数不能出现在命名参数之后");
            }
            expression();
            if (argCount == 255) error("实参不能超过 255 个");
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "实参列表缺少 ')'");
    return (uint8_t)argCount;
}

static void call(bool canAssign) {
    (void)canAssign;
    int kw;
    uint8_t argCount = argumentList(&kw);
    if (kw < 0) {
        emitBytes(OP_CALL, argCount);
    } else {
        emitBytes(OP_CALL_KW, argCount);
        emitShort((uint16_t)kw);
    }
}

static void dot(bool canAssign) {
    if (!check(TOKEN_IDENTIFIER)) {
        /* 允许关键字作为字段名之外的情况报错 */
        errorAtCurrent("'.' 后面需要字段名或方法名");
        return;
    }
    advance();
    uint16_t name = identifierConstant(&parser.previous);
    uint8_t cop;
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitOpShort(OP_SET_FIELD, name);
    } else if (canAssign && matchCompound(&cop)) {
        emitByte(OP_DUP);
        emitOpShort(OP_GET_FIELD, name);
        expression();
        emitByte(cop);
        emitOpShort(OP_SET_FIELD, name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        int kw;
        uint8_t argCount = argumentList(&kw);
        if (kw < 0) {
            emitOpShort(OP_INVOKE, name);
            emitByte(argCount);
        } else {
            emitOpShort(OP_INVOKE_KW, name);
            emitByte(argCount);
            emitShort((uint16_t)kw);
        }
    } else {
        emitOpShort(OP_GET_FIELD, name);
    }
}

/* a[i]、a[i] = v、a[i] += v、切片 a[lo:hi]（lo/hi 可省略） */
static void subscript(bool canAssign) {
    bool isSlice = false;
    if (check(TOKEN_COLON)) {
        emitByte(OP_NIL);
        isSlice = true;
        advance();
    } else {
        expression();
        if (match(TOKEN_COLON)) isSlice = true;
    }
    if (isSlice) {
        if (check(TOKEN_RIGHT_BRACKET)) emitByte(OP_NIL);
        else expression();
        consume(TOKEN_RIGHT_BRACKET, "切片缺少 ']'");
        emitByte(OP_SLICE);
        return;
    }
    consume(TOKEN_RIGHT_BRACKET, "下标缺少 ']'");
    uint8_t cop;
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitByte(OP_SET_INDEX);
    } else if (canAssign && matchCompound(&cop)) {
        emitByte(OP_DUP2);
        emitByte(OP_GET_INDEX);
        expression();
        emitByte(cop);
        emitByte(OP_SET_INDEX);
    } else {
        emitByte(OP_GET_INDEX);
    }
}

/* ---------------- 表达式：运算符 ---------------- */

static void unary(bool canAssign) {
    (void)canAssign;
    TokenType op = parser.previous.type;
    allowIntMin = op == TOKEN_MINUS && check(TOKEN_INT) && !checkNext(TOKEN_STAR_STAR);
    if (op == TOKEN_NOT) parsePrecedence(PREC_NOT);
    else parsePrecedence(PREC_UNARY);
    switch (op) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        case TOKEN_BANG: case TOKEN_NOT: emitByte(OP_NOT); break;
        case TOKEN_TILDE: emitByte(OP_BNOT); break;
        case TOKEN_PLUS: break;
        default: return;
    }
}

static void binary(bool canAssign) {
    (void)canAssign;
    TokenType op = parser.previous.type;
    ParseRule* rule = getRule(op);
    if (op == TOKEN_STAR_STAR) parsePrecedence(PREC_POWER);   /* 右结合 */
    else parsePrecedence((Precedence)(rule->precedence + 1));
    switch (op) {
        case TOKEN_BANG_EQUAL: emitByte(OP_NOT_EQUAL); break;
        case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
        case TOKEN_GREATER: emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitByte(OP_GREATER_EQUAL); break;
        case TOKEN_LESS: emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL: emitByte(OP_LESS_EQUAL); break;
        case TOKEN_PLUS: emitByte(OP_ADD); break;
        case TOKEN_MINUS: emitByte(OP_SUB); break;
        case TOKEN_STAR: emitByte(OP_MUL); break;
        case TOKEN_SLASH: emitByte(OP_DIV); break;
        case TOKEN_SLASH_SLASH: emitByte(OP_IDIV); break;
        case TOKEN_PERCENT: emitByte(OP_MOD); break;
        case TOKEN_STAR_STAR: emitByte(OP_POW); break;
        case TOKEN_AMP: emitByte(OP_BAND); break;
        case TOKEN_PIPE: emitByte(OP_BOR); break;
        case TOKEN_CARET: emitByte(OP_BXOR); break;
        case TOKEN_SHL: emitByte(OP_SHL); break;
        case TOKEN_SHR: emitByte(OP_SHR); break;
        case TOKEN_IN: emitByte(OP_IN); break;
        default: return;
    }
}

/* x not in y */
static void notIn(bool canAssign) {
    (void)canAssign;
    consume(TOKEN_IN, "'not' 作中缀时只能写成 'not in'");
    parsePrecedence((Precedence)(PREC_COMPARISON + 1));
    emitByte(OP_IN);
    emitByte(OP_NOT);
}

static void and_(bool canAssign) {
    (void)canAssign;
    int endJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    parsePrecedence(PREC_AND);
    patchJump(endJump);
}

static void or_(bool canAssign) {
    (void)canAssign;
    int endJump = emitJump(OP_JUMP_IF_TRUE);
    emitByte(OP_POP);
    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

/* cond ? a : b */
static void ternary(bool canAssign) {
    (void)canAssign;
    int elseJump = emitJump(OP_POP_JUMP_IF_FALSE);
    parsePrecedence(PREC_TERNARY);
    int endJump = emitJump(OP_JUMP);
    patchJump(elseJump);
    consume(TOKEN_COLON, "三元表达式缺少 ':'");
    parsePrecedence(PREC_TERNARY);
    patchJump(endJump);
}

/* a..b（左闭右开）与 a..=b（闭区间） */
static void range(bool canAssign) {
    (void)canAssign;
    bool inclusive = parser.previous.type == TOKEN_DOT_DOT_EQ;
    parsePrecedence((Precedence)(PREC_RANGE + 1));
    emitBytes(OP_RANGE, inclusive ? 1 : 0);
}

/* x |> f(a, b)  等价于 f(x, a, b)；x |> f 等价于 f(x) */
static void pipe(bool canAssign) {
    (void)canAssign;
    parsePrecedence(PREC_PRIMARY);   /* 只解析被调函数本身（标识符或括号表达式），不吃掉后面的 (...) */
    emitByte(OP_SWAP);
    if (match(TOKEN_LEFT_PAREN)) {
        int kw;
        uint8_t argCount = argumentList(&kw);
        if (kw >= 0) error("管道调用暂不支持命名参数");
        emitBytes(OP_CALL, (uint8_t)(argCount + 1));
    } else {
        emitBytes(OP_CALL, 1);
    }
}

static ObjString* pendingName = NULL;   /* let f = fn ... 时把 f 作为匿名函数的名字 */

static void lambda(bool canAssign) {
    (void)canAssign;
    ObjString* name = pendingName;
    pendingName = NULL;
    if (name == NULL) name = copyString("匿名函数", (int)strlen("匿名函数"));   /* 调用栈里显示为“匿名函数” */
    function(TYPE_LAMBDA, name);
}

static ParseRule rules[TOKEN_COUNT];

static void initRules(void) {
    static bool done = false;
    if (done) return;
    done = true;
#define R(tok, pre, in, prec) rules[tok] = (ParseRule){pre, in, prec}
    R(TOKEN_LEFT_PAREN, grouping, call, PREC_CALL);
    R(TOKEN_LEFT_BRACKET, arrayLiteral, subscript, PREC_CALL);
    R(TOKEN_LEFT_BRACE, mapLiteral, NULL, PREC_NONE);
    R(TOKEN_DOT, NULL, dot, PREC_CALL);
    R(TOKEN_QUESTION, NULL, ternary, PREC_TERNARY);
    R(TOKEN_MINUS, unary, binary, PREC_TERM);
    R(TOKEN_PLUS, unary, binary, PREC_TERM);
    R(TOKEN_SLASH, NULL, binary, PREC_FACTOR);
    R(TOKEN_SLASH_SLASH, NULL, binary, PREC_FACTOR);
    R(TOKEN_STAR, NULL, binary, PREC_FACTOR);
    R(TOKEN_PERCENT, NULL, binary, PREC_FACTOR);
    R(TOKEN_STAR_STAR, NULL, binary, PREC_POWER);
    R(TOKEN_BANG, unary, NULL, PREC_NONE);
    R(TOKEN_TILDE, unary, NULL, PREC_NONE);
    R(TOKEN_NOT, unary, notIn, PREC_COMPARISON);
    R(TOKEN_BANG_EQUAL, NULL, binary, PREC_EQUALITY);
    R(TOKEN_EQUAL_EQUAL, NULL, binary, PREC_EQUALITY);
    R(TOKEN_GREATER, NULL, binary, PREC_COMPARISON);
    R(TOKEN_GREATER_EQUAL, NULL, binary, PREC_COMPARISON);
    R(TOKEN_LESS, NULL, binary, PREC_COMPARISON);
    R(TOKEN_LESS_EQUAL, NULL, binary, PREC_COMPARISON);
    R(TOKEN_IN, NULL, binary, PREC_COMPARISON);
    R(TOKEN_AMP, NULL, binary, PREC_BITAND);
    R(TOKEN_PIPE, NULL, binary, PREC_BITOR);
    R(TOKEN_CARET, NULL, binary, PREC_BITXOR);
    R(TOKEN_SHL, NULL, binary, PREC_SHIFT);
    R(TOKEN_SHR, NULL, binary, PREC_SHIFT);
    R(TOKEN_AMP_AMP, NULL, and_, PREC_AND);
    R(TOKEN_PIPE_PIPE, NULL, or_, PREC_OR);
    R(TOKEN_AND, NULL, and_, PREC_AND);
    R(TOKEN_OR, NULL, or_, PREC_OR);
    R(TOKEN_PIPE_GT, NULL, pipe, PREC_PIPE);
    R(TOKEN_DOT_DOT, NULL, range, PREC_RANGE);
    R(TOKEN_DOT_DOT_EQ, NULL, range, PREC_RANGE);
    R(TOKEN_IDENTIFIER, variable, NULL, PREC_NONE);
    R(TOKEN_STRING, string, NULL, PREC_NONE);
    R(TOKEN_RAW_STRING, rawString, NULL, PREC_NONE);
    R(TOKEN_INTERP, interpolation, NULL, PREC_NONE);
    R(TOKEN_INT, number, NULL, PREC_NONE);
    R(TOKEN_FLOAT, number, NULL, PREC_NONE);
    R(TOKEN_TRUE, literal, NULL, PREC_NONE);
    R(TOKEN_FALSE, literal, NULL, PREC_NONE);
    R(TOKEN_NIL, literal, NULL, PREC_NONE);
    R(TOKEN_SELF, selfExpr, NULL, PREC_NONE);
    R(TOKEN_FN, lambda, NULL, PREC_NONE);
    R(TOKEN_MATCH, matchExpr, NULL, PREC_NONE);
#undef R
}

static ParseRule* getRule(TokenType type) { return &rules[type]; }

static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        if (parser.previous.type == TOKEN_NEWLINE || parser.previous.type == TOKEN_EOF)
            error("这里缺少表达式");
        else
            error("这里需要一个表达式");
        return;
    }
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }
    if (canAssign && (check(TOKEN_EQUAL) || compoundOp(parser.current.type) != 0)) {
        errorAtCurrent("赋值目标无效（只能给变量、字段或下标赋值）");
    }
}

static void expression(void) { parsePrecedence(PREC_ASSIGNMENT); }

/* ---------------- 函数 ---------------- */

/* 编译形参列表与函数体（'fn' 与函数名已消费，当前应为 '('） */
static void function(FunctionType type, ObjString* name) {
    Compiler compiler;
    initCompiler(&compiler, type, name);
    beginScope();
    consume(TOKEN_LEFT_PAREN, "函数名后面缺少 '('");
    bool sawDefault = false;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            if (check(TOKEN_RIGHT_PAREN)) break;
            if (current->function->arity == 255) errorAtCurrent("形参不能超过 255 个");
            consume(TOKEN_IDENTIFIER, "这里需要形参名");
            Token pname = parser.previous;
            declareLocal(&pname, false);
            markInitialized();
            int slot = current->localCount - 1;
            ObjString* ps = copyString(pname.start, pname.length);
            push(OBJ_VAL(ps));
            writeValueArray(&current->function->params, OBJ_VAL(ps));
            pop();
            current->function->arity++;
            if (match(TOKEN_EQUAL)) {
                /* 默认参数：调用时未传入则求值（每次调用都重新求值） */
                sawDefault = true;
                emitBytes(OP_JUMP_IF_NOT_UNDEF, (uint8_t)slot);
                emitShort(0xffff);
                int jump = currentChunk()->count - 2;
                expression();
                emitBytes(OP_SET_LOCAL, (uint8_t)slot);
                emitByte(OP_POP);
                patchJump(jump);
            } else {
                if (sawDefault) error("没有默认值的形参不能放在有默认值的形参之后");
                current->function->minArity = current->function->arity;
            }
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "形参列表缺少 ')'");

    if (match(TOKEN_FAT_ARROW)) {
        /* fn(x) => 表达式 */
        expression();
        emitByte(OP_RETURN);
    } else {
        consume(TOKEN_LEFT_BRACE, "函数体需要以 '{' 开始（或使用 '=> 表达式'）");
        block();
    }
    ObjFunction* fn = endCompiler();
    push(OBJ_VAL(fn));
    uint16_t k = makeConstant(OBJ_VAL(fn));
    pop();
    emitOpShort(OP_CLOSURE, k);
    for (int i = 0; i < fn->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

/* ---------------- 语句 ---------------- */

/* 简单语句必须以换行、分号、'}' 或文件结尾结束 */
static void consumeTerminator(void) {
    if (match(TOKEN_NEWLINE) || match(TOKEN_SEMICOLON)) return;
    if (check(TOKEN_RIGHT_BRACE) || check(TOKEN_EOF)) return;
    if (parser.armMode && check(TOKEN_COMMA)) return;
    errorAtCurrent("语句后面多了内容（一行写多条语句请用 ';' 分隔）");
}

static void block(void) {
    bool savedArm = parser.armMode;
    parser.armMode = false;
    skipNewlines();
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
        skipNewlines();
    }
    consume(TOKEN_RIGHT_BRACE, "代码块缺少 '}'");
    parser.armMode = savedArm;
}

static void scopedBlock(void) {
    consume(TOKEN_LEFT_BRACE, "这里需要 '{'");
    beginScope();
    block();
    endScope();
}

/* let x = 1 / const N = 10 / let [a, b] = pair */
static void varDeclaration(bool isConst) {
    if (match(TOKEN_LEFT_BRACKET)) {
        /* 解构：先把右侧数组放进隐藏变量，再逐个取下标 */
        Token names[64];
        int n = 0;
        do {
            consume(TOKEN_IDENTIFIER, "解构列表中需要变量名");
            if (n < 64) names[n++] = parser.previous;
        } while (match(TOKEN_COMMA));
        consume(TOKEN_RIGHT_BRACKET, "解构列表缺少 ']'");
        consume(TOKEN_EQUAL, "解构声明需要 '=' 和初始值");
        expression();
        if (current->scopeDepth == 0) {
            for (int i = 0; i < n; i++) {
                emitByte(OP_DUP);
                emitConstant(INT_VAL(i));
                emitByte(OP_GET_INDEX);
                uint16_t g = globalIndex(&names[i]);
                emitOpShort(OP_DEFINE_GLOBAL, g);
                if (isConst) vmSetConstGlobal(g, true);
            }
            emitByte(OP_POP);
        } else {
            addHiddenLocal(" destructure");
            int src = current->localCount - 1;
            for (int i = 0; i < n; i++) {
                emitBytes(OP_GET_LOCAL, (uint8_t)src);
                emitConstant(INT_VAL(i));
                emitByte(OP_GET_INDEX);
                declareLocal(&names[i], isConst);
                markInitialized();
            }
        }
        consumeTerminator();
        return;
    }

    consume(TOKEN_IDENTIFIER, "这里需要变量名");
    Token name = parser.previous;
    bool global = current->scopeDepth == 0;
    uint16_t g = 0;
    if (global) {
        g = globalIndex(&name);
        if (vmIsConstGlobal(g)) constError(&name);
    } else {
        declareLocal(&name, isConst);
    }
    if (match(TOKEN_EQUAL)) {
        if (check(TOKEN_FN)) pendingName = copyString(name.start, name.length);
        expression();
        pendingName = NULL;
    } else {
        if (isConst) error("常量声明必须有初始值");
        emitByte(OP_NIL);
    }
    if (global) {
        emitOpShort(OP_DEFINE_GLOBAL, g);
        if (isConst) vmSetConstGlobal(g, true);
    } else {
        markInitialized();
    }
    consumeTerminator();
}

static void fnDeclaration(void) {
    consume(TOKEN_IDENTIFIER, "这里需要函数名");
    Token name = parser.previous;
    if (current->scopeDepth == 0) {
        uint16_t g = globalIndex(&name);
        if (vmIsConstGlobal(g)) constError(&name);
        function(TYPE_FUNCTION, copyString(name.start, name.length));
        emitOpShort(OP_DEFINE_GLOBAL, g);
    } else {
        declareLocal(&name, false);
        markInitialized();   /* 允许局部函数递归调用自己 */
        function(TYPE_FUNCTION, copyString(name.start, name.length));
    }
}

/* 结构体字段默认值只允许字面量：数字、字符串、true/false/nil、[]、{}
 * （[] 和 {} 在每次创建实例时复制一份，不会在实例间共享） */
static Value fieldDefault(void) {
    bool neg = match(TOKEN_MINUS);
    if (match(TOKEN_INT) || match(TOKEN_FLOAT)) {
        Value v = numberValue(&parser.previous);
        if (neg) v = IS_INT(v) ? INT_VAL(-AS_INT(v)) : FLOAT_VAL(-AS_FLOAT(v));
        return v;
    }
    if (neg) {
        error("'-' 后面需要数字");
        return NIL_VAL;
    }
    if (match(TOKEN_STRING)) {
        int len;
        char* s = unescape(parser.previous.start + 1, parser.previous.length - 2, &len);
        Value v = OBJ_VAL(copyString(s, len));
        free(s);
        return v;
    }
    if (match(TOKEN_RAW_STRING)) return OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2));
    if (match(TOKEN_TRUE)) return BOOL_VAL(true);
    if (match(TOKEN_FALSE)) return BOOL_VAL(false);
    if (match(TOKEN_NIL)) return NIL_VAL;
    if (match(TOKEN_LEFT_BRACKET)) {
        consume(TOKEN_RIGHT_BRACKET, "字段默认值只能是空数组 []");
        return OBJ_VAL(newArray());
    }
    if (match(TOKEN_LEFT_BRACE)) {
        consume(TOKEN_RIGHT_BRACE, "字段默认值只能是空字典 {}");
        return OBJ_VAL(newMap());
    }
    errorAtCurrent("字段默认值只能是字面量（数字、字符串、true/false/nil、[]、{}）");
    return NIL_VAL;
}

/* struct Point { x, y = 0, fn len() { ... } }
 * 字段描述放在一个常量数组里：[名1, 默认值1, 名2, 默认值2, ...]，UNDEF 表示必填 */
static void structDeclaration(void) {
    consume(TOKEN_IDENTIFIER, "这里需要结构体名");
    Token name = parser.previous;
    uint16_t nameConst = identifierConstant(&name);
    bool global = current->scopeDepth == 0;
    uint16_t g = 0;
    if (global) g = globalIndex(&name);
    else declareLocal(&name, false);

    ObjArray* desc = newArray();
    push(OBJ_VAL(desc));
    uint16_t descConst = makeConstant(OBJ_VAL(desc));
    pop();
    emitOpShort(OP_STRUCT, nameConst);
    emitShort(descConst);
    if (!global) markInitialized();

    consume(TOKEN_LEFT_BRACE, "结构体名后面缺少 '{'");
    skipNewlines();
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        if (match(TOKEN_FN)) {
            consume(TOKEN_IDENTIFIER, "这里需要方法名");
            uint16_t m = identifierConstant(&parser.previous);
            function(TYPE_METHOD, AS_STRING(currentChunk()->constants.values[m]));
            emitOpShort(OP_METHOD, m);
        } else {
            consume(TOKEN_IDENTIFIER, "这里需要字段名或 'fn'");
            ObjString* fname = copyString(parser.previous.start, parser.previous.length);
            for (int i = 0; i < desc->items.count; i += 2)
                if (AS_OBJ(desc->items.values[i]) == (Obj*)fname) error("字段名重复");
            push(OBJ_VAL(fname));
            writeValueArray(&desc->items, OBJ_VAL(fname));
            pop();
            Value dv = UNDEF_VAL;
            if (match(TOKEN_EQUAL)) dv = fieldDefault();
            push(dv);
            writeValueArray(&desc->items, dv);
            pop();
        }
        if (!match(TOKEN_COMMA) && !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_NEWLINE) && !check(TOKEN_SEMICOLON))
            errorAtCurrent("结构体成员之间用换行或 ',' 分隔");
        if (parser.panicMode) {
            /* 出错后跳到下一个成员，避免在同一个记号上反复报错（死循环） */
            while (!check(TOKEN_NEWLINE) && !check(TOKEN_COMMA) && !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) advance();
            match(TOKEN_COMMA);
        }
        skipNewlines();
    }
    consume(TOKEN_RIGHT_BRACE, "结构体缺少 '}'");
    if (global) emitOpShort(OP_DEFINE_GLOBAL, g);
}

static void ifStatement(void) {
    expression();
    int thenJump = emitJump(OP_POP_JUMP_IF_FALSE);
    scopedBlock();
    /* '}' 之后的换行允许 else/elif 出现在下一行 */
    if (check(TOKEN_NEWLINE) && (checkNext(TOKEN_ELSE) || checkNext(TOKEN_ELIF))) advance();
    if (match(TOKEN_ELIF) || (check(TOKEN_ELSE) && checkNext(TOKEN_IF) && (advance(), advance(), true))) {
        int elseJump = emitJump(OP_JUMP);
        patchJump(thenJump);
        ifStatement();
        patchJump(elseJump);
        return;
    }
    if (match(TOKEN_ELSE)) {
        int elseJump = emitJump(OP_JUMP);
        patchJump(thenJump);
        scopedBlock();
        patchJump(elseJump);
        return;
    }
    patchJump(thenJump);
}

static void pushLoop(Loop* loop, int start) {
    loop->enclosing = current->loop;
    loop->start = start;
    loop->scopeDepth = current->scopeDepth;
    loop->tryDepth = current->tryDepth;
    loop->breakCount = 0;
    loop->continueCount = 0;
    current->loop = loop;
}

static void popLoop(void) {
    Loop* loop = current->loop;
    for (int i = 0; i < loop->breakCount; i++) patchJump(loop->breaks[i]);
    current->loop = loop->enclosing;
}

static void whileStatement(void) {
    int loopStart = currentChunk()->count;
    expression();
    int exitJump = emitJump(OP_POP_JUMP_IF_FALSE);
    Loop loop;
    pushLoop(&loop, loopStart);
    scopedBlock();
    emitLoop(loopStart);
    patchJump(exitJump);
    popLoop();
}

/* break / continue：结束循环内进入的 try、弹出循环体内的局部变量（不改变编译期的局部变量表） */
static void unwindToLoop(Loop* loop) {
    for (int i = current->tryDepth; i > loop->tryDepth; i--) emitByte(OP_END_TRY);
    for (int i = current->localCount - 1; i >= 0 && current->locals[i].depth > loop->scopeDepth; i--) {
        emitByte(current->locals[i].isCaptured ? OP_CLOSE_UPVALUE : OP_POP);
    }
}

static void breakStatement(void) {
    Loop* loop = current->loop;
    if (loop == NULL) {
        error("'break' 只能出现在循环中");
        consumeTerminator();
        return;
    }
    unwindToLoop(loop);
    if (loop->breakCount == LOOP_JUMPS_MAX) error("单个循环中的 break 过多");
    else loop->breaks[loop->breakCount++] = emitJump(OP_JUMP);
    consumeTerminator();
}

static void continueStatement(void) {
    Loop* loop = current->loop;
    if (loop == NULL) {
        error("'continue' 只能出现在循环中");
        consumeTerminator();
        return;
    }
    unwindToLoop(loop);
    if (loop->start >= 0) emitLoop(loop->start);
    else if (loop->continueCount == LOOP_JUMPS_MAX) error("单个循环中的 continue 过多");
    else loop->continues[loop->continueCount++] = emitJump(OP_JUMP);
    consumeTerminator();
}

static void patchContinues(Loop* loop) {
    for (int i = 0; i < loop->continueCount; i++) patchJump(loop->continues[i]);
}

static bool checkStep(void) {
    if (!check(TOKEN_IDENTIFIER)) return false;
    Token* t = &parser.current;
    return (t->length == 4 && memcmp(t->start, "step", 4) == 0) ||
           (t->length == 6 && memcmp(t->start, "步长", 6) == 0);
}

/* for i in a..b [step s] { }   —— 数值循环，编译成专用指令
 * for x in 可迭代 { }          —— 数组/字符串/字典/区间
 * for i, x in 数组 { }         —— 下标与元素；字典为键与值 */
static void forStatement(void) {
    beginScope();   /* 隐藏变量的作用域 */
    consume(TOKEN_IDENTIFIER, "for 后面需要循环变量名");
    Token var1 = parser.previous;
    Token var2;
    bool two = false;
    if (match(TOKEN_COMMA)) {
        consume(TOKEN_IDENTIFIER, "',' 后面需要第二个循环变量名");
        var2 = parser.previous;
        two = true;
    }
    consume(TOKEN_IN, "循环变量后面缺少 'in'");
    int base = current->localCount;
    parsePrecedence(PREC_BITOR);

    if (!two && (check(TOKEN_DOT_DOT) || check(TOKEN_DOT_DOT_EQ))) {
        bool inclusive = check(TOKEN_DOT_DOT_EQ);
        advance();
        parsePrecedence(PREC_BITOR);
        if (checkStep()) {
            advance();
            parsePrecedence(PREC_BITOR);
        } else {
            emitConstant(INT_VAL(1));
        }
        addHiddenLocal(" i");
        addHiddenLocal(" end");
        addHiddenLocal(" step");
        emitBytes(OP_FOR_PREP, (uint8_t)base);
        emitByte(inclusive ? 1 : 0);
        int loopStart = currentChunk()->count;
        emitBytes(OP_FOR_RANGE, (uint8_t)base);
        emitShort(0xffff);
        int exitJump = currentChunk()->count - 2;
        Loop loop;
        pushLoop(&loop, -1);
        beginScope();
        declareLocal(&var1, false);
        markInitialized();
        consume(TOKEN_LEFT_BRACE, "for 循环体需要以 '{' 开始");
        block();
        endScope();
        patchContinues(&loop);
        emitBytes(OP_FOR_STEP, (uint8_t)base);
        int offset = currentChunk()->count - loopStart + 2;
        if (offset > UINT16_MAX) error("循环体过长");
        emitShort((uint16_t)offset);
        patchJump(exitJump);
        popLoop();
        endScope();
        return;
    }

    if (check(TOKEN_DOT_DOT) || check(TOKEN_DOT_DOT_EQ)) {
        /* for i, x in a..b：先构造区间对象再走通用迭代 */
        bool inclusive = check(TOKEN_DOT_DOT_EQ);
        advance();
        parsePrecedence(PREC_BITOR);
        emitBytes(OP_RANGE, inclusive ? 1 : 0);
    }
    if (!check(TOKEN_LEFT_BRACE)) {
        errorAtCurrent("for 循环的可迭代表达式较复杂时请用括号括起来，后面需要 '{'");
    }
    emitConstant(INT_VAL(0));
    emitConstant(INT_VAL(0));
    addHiddenLocal(" seq");
    addHiddenLocal(" state");
    addHiddenLocal(" count");
    int loopStart = currentChunk()->count;
    emitBytes(OP_FOR_ITER, (uint8_t)base);
    emitByte(two ? 2 : 1);
    emitShort(0xffff);
    int exitJump = currentChunk()->count - 2;
    Loop loop;
    pushLoop(&loop, loopStart);
    beginScope();
    declareLocal(&var1, false);
    markInitialized();
    if (two) {
        declareLocal(&var2, false);
        markInitialized();
    }
    consume(TOKEN_LEFT_BRACE, "for 循环体需要以 '{' 开始");
    block();
    endScope();
    emitLoop(loopStart);
    patchJump(exitJump);
    popLoop();
    endScope();
}

static void returnStatement(void) {
    if (check(TOKEN_NEWLINE) || check(TOKEN_SEMICOLON) || check(TOKEN_RIGHT_BRACE) || check(TOKEN_EOF)) {
        emitReturn();
    } else {
        expression();
        emitByte(OP_RETURN);
    }
    consumeTerminator();
}

/* try { ... } catch e { ... }  —— e 可省略，也可写成 catch (e) */
static void tryStatement(void) {
    int tryJump = emitJump(OP_TRY);
    current->tryDepth++;
    scopedBlock();
    current->tryDepth--;
    emitByte(OP_END_TRY);
    int endJump = emitJump(OP_JUMP);
    patchJump(tryJump);

    if (check(TOKEN_NEWLINE) && checkNext(TOKEN_CATCH)) advance();
    consume(TOKEN_CATCH, "try 块后面缺少 'catch'");
    beginScope();
    bool paren = match(TOKEN_LEFT_PAREN);
    if (check(TOKEN_IDENTIFIER)) {
        advance();
        Token name = parser.previous;
        declareLocal(&name, false);
    } else {
        addLocal(syntheticToken(" err"), false);
    }
    markInitialized();
    if (paren) consume(TOKEN_RIGHT_PAREN, "缺少 ')'");
    consume(TOKEN_LEFT_BRACE, "catch 后面需要 '{'");
    block();
    endScope();
    patchJump(endJump);
}

static void throwStatement(void) {
    expression();
    emitByte(OP_THROW);
    consumeTerminator();
}

static void importStatement(void) {
    if (!match(TOKEN_STRING) && !match(TOKEN_RAW_STRING)) {
        errorAtCurrent("import 后面需要文件路径字符串");
        return;
    }
    Token t = parser.previous;
    int len;
    char* s = unescape(t.start + 1, t.length - 2, &len);
    ObjString* path = copyString(s, len);
    free(s);
    emitOpShort(OP_IMPORT, makeConstant(OBJ_VAL(path)));
    emitByte(OP_POP);   /* 丢弃模块的返回值 */
    consumeTerminator();
}

static void expressionStatement(void) {
    expression();
    if (parser.replMode && current->type == TYPE_SCRIPT && current->scopeDepth == 0) emitByte(OP_REPL_PRINT);
    else emitByte(OP_POP);
    consumeTerminator();
}

/* match 主体（subject 已在栈顶）：
 *   match x { 1 => ..., 2, 3 => ..., 4..=9 => ..., "a" if cond => ..., _ => ... }
 * 每个分支：对每个模式 DUP subject 并比较，命中则跳到分支体；分支体执行前先弹出 subject，
 * 因此分支体内的局部变量槽位不受影响。isExpr 时分支体是表达式，结果留在栈上。 */
static void matchBody(bool isExpr) {
    consume(TOKEN_LEFT_BRACE, "match 表达式后面需要 '{'");
    skipNewlines();
    int endJumps[256];
    int endCount = 0;
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        bool isWildcard = check(TOKEN_IDENTIFIER) && parser.current.length == 1 && parser.current.start[0] == '_';
        if (check(TOKEN_IDENTIFIER) && !isWildcard && (checkNext(TOKEN_FAT_ARROW) || checkNext(TOKEN_IF))) {
            /* 绑定模式：n => ... / n if n > 0 => ...，subject 本身就成为局部变量 n */
            if (isExpr) {
                errorAtCurrent("表达式形式的 match 不支持绑定变量，请改用 '_ if 条件 =>' 或语句形式的 match");
                return;
            }
            advance();
            Token name = parser.previous;
            beginScope();
            addLocal(name, false);
            markInitialized();
            int guardJump = -1;
            if (match(TOKEN_IF)) {
                expression();
                guardJump = emitJump(OP_POP_JUMP_IF_FALSE);
            }
            consume(TOKEN_FAT_ARROW, "match 模式后面需要 '=>'");
            skipNewlines();
            if (match(TOKEN_LEFT_BRACE)) {
                block();
            } else {
                bool saved = parser.armMode;
                parser.armMode = true;
                statement();
                parser.armMode = saved;
            }
            endScope();   /* 弹出 subject（即绑定变量） */
            if (endCount < 256) endJumps[endCount++] = emitJump(OP_JUMP);
            if (guardJump >= 0) patchJump(guardJump);
            while (match(TOKEN_COMMA) || match(TOKEN_NEWLINE) || match(TOKEN_SEMICOLON)) {}
            continue;
        }
        int okJumps[64];
        int okCount = 0;
        for (;;) {
            if (check(TOKEN_IDENTIFIER) && parser.current.length == 1 && parser.current.start[0] == '_') {
                advance();
                emitByte(OP_TRUE);
            } else {
                emitByte(OP_DUP);
                parsePrecedence(PREC_BITOR);
                if (match(TOKEN_DOT_DOT) || match(TOKEN_DOT_DOT_EQ)) {
                    bool incl = parser.previous.type == TOKEN_DOT_DOT_EQ;
                    parsePrecedence(PREC_BITOR);
                    emitBytes(OP_IN_RANGE, incl ? 1 : 0);
                } else {
                    emitByte(OP_EQUAL);
                }
                /* 栈：subject dup cmp —— IN_RANGE/EQUAL 已把 dup 消耗掉，只剩 subject bool */
            }
            if (okCount < 64) okJumps[okCount++] = emitJump(OP_JUMP_IF_TRUE);
            else error("单个分支的模式过多");
            emitByte(OP_POP);
            if (!match(TOKEN_COMMA)) break;
        }
        int nextArm = emitJump(OP_JUMP);
        for (int i = 0; i < okCount; i++) patchJump(okJumps[i]);
        emitByte(OP_POP);   /* 弹出 true */
        int guardJump = -1;
        if (match(TOKEN_IF)) {
            expression();
            guardJump = emitJump(OP_POP_JUMP_IF_FALSE);
        }
        emitByte(OP_POP);   /* 弹出 subject */
        consume(TOKEN_FAT_ARROW, "match 模式后面需要 '=>'");
        skipNewlines();
        if (isExpr) {
            expression();
        } else if (check(TOKEN_LEFT_BRACE)) {
            advance();
            beginScope();
            block();
            endScope();
        } else {
            bool saved = parser.armMode;
            parser.armMode = true;
            statement();
            parser.armMode = saved;
        }
        if (endCount < 256) endJumps[endCount++] = emitJump(OP_JUMP);
        else error("match 的分支过多");
        /* 模式未命中 / 守卫失败：subject 仍在栈上，继续尝试下一个分支 */
        if (guardJump >= 0) patchJump(guardJump);
        patchJump(nextArm);
        while (match(TOKEN_COMMA) || match(TOKEN_NEWLINE) || match(TOKEN_SEMICOLON)) {}
    }
    emitByte(OP_POP);   /* 没有分支命中：弹出 subject */
    if (isExpr) emitByte(OP_NIL);
    for (int i = 0; i < endCount; i++) patchJump(endJumps[i]);
    consume(TOKEN_RIGHT_BRACE, "match 缺少 '}'");
}

static void matchStatement(void) {
    expression();
    matchBody(false);
}

static void matchExpr(bool canAssign) {
    (void)canAssign;
    expression();
    matchBody(true);
}

static void synchronize(void) {
    parser.panicMode = false;
    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_NEWLINE || parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_LET: case TOKEN_CONST: case TOKEN_FN: case TOKEN_STRUCT: case TOKEN_IF:
            case TOKEN_WHILE: case TOKEN_FOR: case TOKEN_RETURN: case TOKEN_MATCH: case TOKEN_TRY:
            case TOKEN_RIGHT_BRACE:
                return;
            default:;
        }
        advance();
    }
}

static void statement(void) {
    if (match(TOKEN_IF)) ifStatement();
    else if (match(TOKEN_WHILE)) whileStatement();
    else if (match(TOKEN_FOR)) forStatement();
    else if (match(TOKEN_RETURN)) returnStatement();
    else if (match(TOKEN_BREAK)) breakStatement();
    else if (match(TOKEN_CONTINUE)) continueStatement();
    else if (match(TOKEN_TRY)) tryStatement();
    else if (match(TOKEN_THROW)) throwStatement();
    else if (match(TOKEN_IMPORT)) importStatement();
    else if (match(TOKEN_MATCH)) matchStatement();
    else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else expressionStatement();
}

static void declaration(void) {
    if (match(TOKEN_LET)) varDeclaration(false);
    else if (match(TOKEN_CONST)) varDeclaration(true);
    else if (check(TOKEN_FN) && checkNext(TOKEN_IDENTIFIER)) {
        advance();
        fnDeclaration();
    } else if (match(TOKEN_STRUCT)) structDeclaration();
    else statement();
    if (parser.panicMode) synchronize();
}

ObjFunction* compile(const char* source, const char* filename, bool replMode) {
    initRules();
    initLexer(source);
    parser.hadError = false;
    parser.panicMode = false;
    parser.needMore = false;
    parser.errorCount = 0;
    parser.replMode = replMode;
    parser.armMode = false;
    parser.fileName = filename;
    parser.file = NULL;
    current = NULL;
    parser.file = cstr(filename);

    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT, NULL);
    parser.current = scanValid();
    parser.next = parser.current.type == TOKEN_EOF ? parser.current : scanValid();
    skipNewlines();
    while (!check(TOKEN_EOF)) {
        declaration();
        skipNewlines();
    }
    ObjFunction* function = endCompiler();
    if (parser.errorCount > 20) fprintf(stderr, "……共 %d 个错误，只显示前 20 个\n", parser.errorCount);
    parser.file = NULL;
    return parser.hadError ? NULL : function;
}

void markCompilerRoots(void) {
    for (Compiler* c = current; c != NULL; c = c->enclosing) markObject((Obj*)c->function);
    markObject((Obj*)parser.file);
    markObject((Obj*)pendingName);
}

bool compileNeedsMore(void) { return parser.needMore; }
