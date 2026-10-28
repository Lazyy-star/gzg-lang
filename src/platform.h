/* 平台适配层：Windows 下用宽字符 API 处理中文路径与控制台 UTF-8 */
#ifndef GZG_PLATFORM_H
#define GZG_PLATFORM_H

#include "common.h"

void platformInit(void);
char** platformArgs(int argc, char** argv, int* outArgc);   /* 返回 UTF-8 参数 */
FILE* platformFopen(const char* utf8Path, const char* mode);
char* readFileAll(const char* path, size_t* outLen);        /* 失败返回 NULL */
char* readLine(bool* eof);                                  /* 读一行(不含换行)，返回 malloc 缓冲区 */
double platformClock(void);                                 /* 单调时钟，秒 */
double platformTime(void);                                  /* Unix 时间戳，秒 */
void platformSleep(int ms);
bool platformFileExists(const char* path);
/* 列出目录项（不含 . 和 ..），回调返回 false 中止 */
bool platformListDir(const char* path, void (*cb)(const char* name, bool isDir, void* ud), void* ud);
/* 执行 shell 命令，捕获 stdout+stderr；返回退出码，失败返回 -1 */
int platformRun(const char* cmd, char** output, size_t* outLen);
char* platformRealPath(const char* path);                   /* malloc 的绝对路径 */
bool platformIsTTY(void);

#endif
