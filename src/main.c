/* GZG 命令行入口
 *   gzg                 交互式 REPL
 *   gzg 文件.gzg [参数]  运行脚本
 *   gzg -e "代码"        运行一段代码
 *   gzg --dis 文件.gzg   反汇编（查看字节码）
 *   gzg --tokens 文件    查看词法分析结果 */
#include "chunk.h"
#include "compiler.h"
#include "lexer.h"
#include "memory.h"
#include "platform.h"
#include "vm.h"

static void usage(void) {
    printf("GZG 语言解释器 %s\n\n", GZG_VERSION);
    printf("用法:\n");
    printf("  gzg                      进入交互式环境 (REPL)\n");
    printf("  gzg <文件.gzg> [参数...]  运行脚本，参数可用 args() 读取\n");
    printf("  gzg -e \"<代码>\"          直接运行一段代码\n");
    printf("  gzg --dis <文件.gzg>      反汇编，打印编译出的字节码\n");
    printf("  gzg --tokens <文件.gzg>   打印词法分析得到的记号\n");
    printf("  gzg --version            显示版本\n");
}

static void setScriptArgs(int argc, char** argv, int from) {
    vm.scriptArgs = newArray();
    for (int i = from; i < argc; i++) {
        ObjString* s = cstr(argv[i]);
        push(OBJ_VAL(s));
        writeValueArray(&vm.scriptArgs->items, OBJ_VAL(s));
        pop();
    }
}

static void setScriptDir(const char* path) {
    const char* slash = NULL;
    for (const char* p = path; *p; p++)
        if (*p == '/' || *p == '\\') slash = p;
    vm.scriptDir = slash ? copyString(path, (int)(slash - path)) : cstr(".");
}

static char* loadOrDie(const char* path) {
    size_t len;
    char* source = readFileAll(path, &len);
    if (source == NULL) {
        fprintf(stderr, "错误: 无法打开文件 '%s'\n", path);
        exit(EXIT_IO_ERROR);
    }
    return source;
}

static int runFile(const char* path) {
    char* source = loadOrDie(path);
    setScriptDir(path);
    InterpretResult result = interpret(source, path);
    free(source);
    fflush(stdout);
    if (result == INTERPRET_COMPILE_ERROR) return EXIT_COMPILE_ERROR;
    if (result == INTERPRET_RUNTIME_ERROR) return EXIT_RUNTIME_ERROR;
    return 0;
}

static int disassembleFile(const char* path) {
    char* source = loadOrDie(path);
    ObjFunction* fn = compile(source, path, false);
    free(source);
    if (fn == NULL) return EXIT_COMPILE_ERROR;
    push(OBJ_VAL(fn));
    disassembleChunk(&fn->chunk, "<主程序>");
    pop();
    return 0;
}

static int dumpTokens(const char* path) {
    char* source = loadOrDie(path);
    initLexer(source);
    for (;;) {
        Token t = scanToken();
        if (t.type == TOKEN_NEWLINE) printf("%4d:%-3d %-10s\n", t.line, t.column, "换行");
        else printf("%4d:%-3d %-10s '%.*s'\n", t.line, t.column, tokenTypeName(t.type), t.length, t.start);
        if (t.type == TOKEN_EOF) break;
    }
    free(source);
    return 0;
}

/* REPL：粗略统计未闭合的括号与字符串，决定是否需要继续读入下一行 */
static int openDepth(const char* s) {
    int depth = 0;
    char quote = 0;
    for (const char* p = s; *p; p++) {
        if (quote) {
            if (*p == '\\' && quote == '"' && p[1]) p++;
            else if (*p == quote) quote = 0;
            continue;
        }
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            if (!*p) break;
            continue;
        }
        if (*p == '"' || *p == '\'') quote = *p;
        else if (*p == '(' || *p == '[' || *p == '{') depth++;
        else if (*p == ')' || *p == ']' || *p == '}') depth--;
    }
    return quote ? depth + 1 : depth;
}

static void repl(void) {
    printf("GZG %s 交互式环境。输入表达式查看结果，输入 .exit 或按 Ctrl+Z/Ctrl+D 退出。\n", GZG_VERSION);
    vm.replMode = true;
    vm.scriptDir = cstr(".");
    setScriptArgs(0, NULL, 0);
    char* buf = NULL;
    size_t len = 0;
    for (;;) {
        printf(len == 0 ? "gzg> " : "...> ");
        fflush(stdout);
        bool eof = false;
        char* line = readLine(&eof);
        if (line == NULL) {
            printf("\n");
            break;
        }
        if (len == 0 && strcmp(line, ".exit") == 0) {
            free(line);
            break;
        }
        size_t ll = strlen(line);
        buf = (char*)realloc(buf, len + ll + 2);
        memcpy(buf + len, line, ll);
        len += ll;
        buf[len++] = '\n';
        buf[len] = '\0';
        free(line);
        if (openDepth(buf) > 0) continue;
        interpret(buf, "<repl>");
        fflush(stdout);
        len = 0;
        vm.uncaughtReported = false;
    }
    free(buf);
}

int main(int argc, char** argv) {
    platformInit();
    argv = platformArgs(argc, argv, &argc);
    initVM();
    int code = 0;
    if (argc == 1) {
        repl();
    } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage();
    } else if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("GZG %s\n", GZG_VERSION);
    } else if (strcmp(argv[1], "-e") == 0) {
        if (argc < 3) {
            usage();
            code = 64;
        } else {
            vm.scriptDir = cstr(".");
            setScriptArgs(argc, argv, 3);
            InterpretResult r = interpret(argv[2], "<命令行>");
            code = r == INTERPRET_OK ? 0 : r == INTERPRET_COMPILE_ERROR ? EXIT_COMPILE_ERROR : EXIT_RUNTIME_ERROR;
        }
    } else if (strcmp(argv[1], "--dis") == 0 && argc >= 3) {
        code = disassembleFile(argv[2]);
    } else if (strcmp(argv[1], "--tokens") == 0 && argc >= 3) {
        code = dumpTokens(argv[2]);
    } else if (argv[1][0] == '-' && argv[1][1] != '\0') {
        fprintf(stderr, "未知选项 '%s'\n\n", argv[1]);
        usage();
        code = 64;
    } else {
        setScriptArgs(argc, argv, 2);
        code = runFile(argv[1]);
    }
    fflush(stdout);
    fflush(stderr);
    return code;   /* 进程退出时由操作系统回收内存，省去逐个释放对象的时间 */
}
