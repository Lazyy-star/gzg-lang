/* 平台适配层实现：Windows 走宽字符 API（中文路径、控制台 UTF-8），其余平台走 POSIX */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "platform.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#include <fcntl.h>
#include <wchar.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

static void* xmalloc(size_t n) {
    void* p = malloc(n ? n : 1);
    if (p == NULL) {
        fprintf(stderr, "致命错误: 内存不足\n");
        exit(EXIT_RUNTIME_ERROR);
    }
    return p;
}

static void* xrealloc(void* p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (p == NULL) {
        fprintf(stderr, "致命错误: 内存不足\n");
        exit(EXIT_RUNTIME_ERROR);
    }
    return p;
}

#ifdef _WIN32
static wchar_t* toWide(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) n = 1;
    wchar_t* w = (wchar_t*)xmalloc(sizeof(wchar_t) * n);
    w[0] = 0;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char* fromWide(const wchar_t* w, int wlen) {
    /* wlen = -1 表示以 0 结尾 */
    int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, NULL, 0, NULL, NULL);
    if (n < 0) n = 0;
    char* s = (char*)xmalloc((size_t)n + 1);
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, wlen, s, n, NULL, NULL);
    s[n] = '\0';
    return s;
}
#endif

void platformInit(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    HANDLE handles[2] = {GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE)};
    for (int i = 0; i < 2; i++) {
        DWORD mode;
        if (handles[i] != INVALID_HANDLE_VALUE && GetConsoleMode(handles[i], &mode))
            SetConsoleMode(handles[i], mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
}

char** platformArgs(int argc, char** argv, int* outArgc) {
#ifdef _WIN32
    int n = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &n);
    if (wargv == NULL) {
        *outArgc = argc;
        return argv;
    }
    char** out = (char**)xmalloc(sizeof(char*) * ((size_t)n + 1));
    for (int i = 0; i < n; i++) out[i] = fromWide(wargv[i], -1);
    out[n] = NULL;
    LocalFree(wargv);
    *outArgc = n;
    return out;
#else
    *outArgc = argc;
    return argv;
#endif
}

FILE* platformFopen(const char* utf8Path, const char* mode) {
#ifdef _WIN32
    wchar_t* wp = toWide(utf8Path);
    wchar_t* wm = toWide(mode);
    FILE* f = _wfopen(wp, wm);
    free(wp);
    free(wm);
    return f;
#else
    return fopen(utf8Path, mode);
#endif
}

char* readFileAll(const char* path, size_t* outLen) {
    FILE* f = platformFopen(path, "rb");
    if (f == NULL) return NULL;
    size_t cap = 4096, len = 0;
    char* buf = (char*)xmalloc(cap);
    for (;;) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char*)xrealloc(buf, cap);
        }
        size_t n = fread(buf + len, 1, cap - len - 1, f);
        len += n;
        if (n == 0) break;
    }
    bool bad = ferror(f) != 0;
    fclose(f);
    if (bad) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    if (outLen) *outLen = len;
    return buf;
}

/* 通用读行：fgetc 读到 '\n' 或 EOF */
static char* readLineStdio(bool* eof) {
    size_t cap = 128, len = 0;
    char* buf = (char*)xmalloc(cap);
    int c;
    bool any = false;
    while ((c = fgetc(stdin)) != EOF) {
        any = true;
        if (c == '\n') break;
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char*)xrealloc(buf, cap);
        }
        buf[len++] = (char)c;
    }
    if (!any) {
        free(buf);
        *eof = true;
        return NULL;
    }
    if (len > 0 && buf[len - 1] == '\r') len--;
    buf[len] = '\0';
    *eof = false;
    return buf;
}

char* readLine(bool* eof) {
    fflush(stdout);
    *eof = false;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_CHAR) {
        DWORD mode;
        if (GetConsoleMode(h, &mode)) {
            /* 控制台：用 ReadConsoleW 读宽字符，避免代码页导致中文乱码 */
            size_t cap = 256, len = 0;
            wchar_t* w = (wchar_t*)xmalloc(sizeof(wchar_t) * cap);
            bool gotNewline = false;
            for (;;) {
                wchar_t chunk[256];
                DWORD got = 0;
                if (!ReadConsoleW(h, chunk, 256, &got, NULL) || got == 0) break;
                for (DWORD i = 0; i < got; i++) {
                    if (chunk[i] == L'\n') { gotNewline = true; break; }
                    if (len + 1 >= cap) {
                        cap *= 2;
                        w = (wchar_t*)xrealloc(w, sizeof(wchar_t) * cap);
                    }
                    w[len++] = chunk[i];
                }
                if (gotNewline) break;
            }
            if (len > 0 && w[len - 1] == L'\r') len--;
            /* Ctrl+Z 开头视为 EOF */
            if ((!gotNewline && len == 0) || (len >= 1 && w[0] == 0x1A)) {
                free(w);
                *eof = true;
                return NULL;
            }
            char* s = len > 0 ? fromWide(w, (int)len) : (char*)xmalloc(1);
            if (len == 0) s[0] = '\0';
            free(w);
            return s;
        }
    }
#endif
    return readLineStdio(eof);
}

double platformClock(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

double platformTime(void) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    /* FILETIME 以 1601-01-01 为起点、单位 100ns */
    return (double)(u.QuadPart - 116444736000000000ULL) / 1e7;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

void platformSleep(int ms) {
    fflush(stdout);
    if (ms <= 0) return;
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

bool platformFileExists(const char* path) {
#ifdef _WIN32
    wchar_t* wp = toWide(path);
    DWORD attr = GetFileAttributesW(wp);
    free(wp);
    return attr != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

bool platformListDir(const char* path, void (*cb)(const char* name, bool isDir, void* ud), void* ud) {
#ifdef _WIN32
    size_t n = strlen(path);
    char* pattern = (char*)xmalloc(n + 3);
    memcpy(pattern, path, n);
    if (n > 0 && path[n - 1] != '/' && path[n - 1] != '\\') pattern[n++] = '\\';
    pattern[n++] = '*';
    pattern[n] = '\0';
    wchar_t* wp = toWide(pattern);
    free(pattern);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wp, &fd);
    free(wp);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        char* name = fromWide(fd.cFileName, -1);
        cb(name, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, ud);
        free(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
#else
    DIR* d = opendir(path);
    if (d == NULL) return false;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        bool isDir = false;
        size_t n = strlen(path) + strlen(e->d_name) + 2;
        char* full = (char*)xmalloc(n);
        snprintf(full, n, "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(full, &st) == 0) isDir = S_ISDIR(st.st_mode);
        free(full);
        cb(e->d_name, isDir, ud);
    }
    closedir(d);
    return true;
#endif
}

int platformRun(const char* cmd, char** output, size_t* outLen) {
    fflush(stdout);
    fflush(stderr);
    *output = NULL;
    *outLen = 0;
    FILE* p;
    /* 默认把 stderr 合并进输出；命令自己重定向了 stderr（含 "2>"）时尊重它 */
    const char* merge = strstr(cmd, "2>") != NULL ? "" : " 2>&1";
#ifdef _WIN32
    /* cmd.exe /c 会剥掉最外层引号，所以外面再包一层，内部引号得以保留 */
    wchar_t* wc = toWide(cmd);
    size_t wn = wcslen(wc);
    wchar_t* full = (wchar_t*)xmalloc(sizeof(wchar_t) * (wn + 16));
    wcscpy(full, L"\"");
    wcscat(full, wc);
    if (merge[0]) wcscat(full, L" 2>&1");
    wcscat(full, L"\"");
    free(wc);
    p = _wpopen(full, L"rb");
    free(full);
#else
    size_t n = strlen(cmd);
    char* full = (char*)xmalloc(n + 8);
    memcpy(full, cmd, n);
    memcpy(full + n, merge, strlen(merge) + 1);
    p = popen(full, "r");
    free(full);
#endif
    if (p == NULL) return -1;
    size_t cap = 4096, len = 0;
    char* buf = (char*)xmalloc(cap);
    for (;;) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char*)xrealloc(buf, cap);
        }
        size_t got = fread(buf + len, 1, cap - len - 1, p);
        len += got;
        if (got == 0) break;
    }
    buf[len] = '\0';
#ifdef _WIN32
    int code = _pclose(p);
#else
    int status = pclose(p);
    int code;
    if (status == -1) code = -1;
    else if (WIFEXITED(status)) code = WEXITSTATUS(status);
    else code = 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
    *output = buf;
    *outLen = len;
    return code;
}

/* 返回规范化的绝对路径（Windows 下统一用 '/' 分隔，便于作为导入缓存的键） */
char* platformRealPath(const char* path) {
#ifdef _WIN32
    wchar_t* wp = toWide(path);
    wchar_t* abs = _wfullpath(NULL, wp, 0);
    free(wp);
    if (abs == NULL) return NULL;
    char* s = fromWide(abs, -1);
    free(abs);
    for (char* c = s; *c; c++)
        if (*c == '\\') *c = '/';
    return s;
#else
    char* r = realpath(path, NULL);
    if (r != NULL) return r;
    if (path[0] == '/') {
        size_t n = strlen(path) + 1;
        char* s = (char*)xmalloc(n);
        memcpy(s, path, n);
        return s;
    }
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd) == NULL) return NULL;
    size_t n = strlen(cwd) + strlen(path) + 2;
    char* s = (char*)xmalloc(n);
    snprintf(s, n, "%s/%s", cwd, path);
    return s;
#endif
}

bool platformIsTTY(void) {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(1) != 0;
#endif
}
