/* GZG 语言解释器 —— 公共头文件
 * 架构：源码 -> 词法分析(lexer) -> 单遍编译(compiler, Pratt 解析) -> 字节码(chunk)
 *       -> 栈式虚拟机(vm) 执行，标记-清除垃圾回收(memory)。
 */
#ifndef GZG_COMMON_H
#define GZG_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GZG_VERSION "1.0.0"

#define FRAMES_MAX   10000          /* 最大调用深度（递归层数） */
#define STACK_MAX    (FRAMES_MAX * 64)
#define HANDLERS_MAX 1024           /* 最多同时活跃的 try 块 */
#define LOCALS_MAX   256            /* 单个函数的局部变量上限 */
#define UPVALUES_MAX 256

#define EXIT_COMPILE_ERROR 65
#define EXIT_RUNTIME_ERROR 70
#define EXIT_IO_ERROR      74

/* 编译时加 -DGC_STRESS 可在每次分配时触发 GC，用于发现 GC 根遗漏 */
/* 编译时加 -DTRACE_EXEC 打印每条指令的执行轨迹 */

#endif
