#ifndef GZG_COMPILER_H
#define GZG_COMPILER_H

#include "object.h"

/* 编译整段源码为顶层函数；出错返回 NULL（错误已打印到 stderr） */
ObjFunction* compile(const char* source, const char* filename, bool replMode);
void markCompilerRoots(void);
/* REPL 用：上次编译失败是否因为输入不完整（未闭合的括号/字符串） */
bool compileNeedsMore(void);

#endif
