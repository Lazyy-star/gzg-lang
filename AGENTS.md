# AGENTS.md — 给编程 Agent 的项目说明

本仓库是 GZG 语言（`.gzg`）的 C 语言解释器及其测试、文档、示例程序。

## 用 GZG 写程序时

**先读 `.claude/skills/gzg/SKILL.md`**（语法速览 + 18 条与 Python/JS 不同的规则），再按需查：

- `.claude/skills/gzg/reference.md`：全部内建函数与方法
- `.claude/skills/gzg/pitfalls.md`：报错信息 → 原因 → 改法
- `docs/GZG语法规则.md`：完整语言规范（含 EBNF）

不确定语义时用 `build/gzg.exe -e '代码'` 实际运行确认，不要按 Python 的习惯猜。

## 构建与测试

```bash
mingw32-make            # Windows (MinGW)；Linux/macOS 用 make。产物 build/gzg(.exe)
build/gzg.exe tests/run_tests.gzg        # 全部黑盒测试（一个命令）
build/gzg.exe tests/run_tests.gzg 字符串  # 只跑路径含关键字的测试
build/gzg.exe bench/run_bench.gzg        # 与 Python 的性能对比（驱动用 GZG 编写）
```

## 目录

```
src/          解释器源码（lexer → compiler → chunk 字节码 → vm；memory.c 是 GC；builtins.c 是内建函数）
tests/        黑盒测试，按特性分目录；run_tests.gzg 是用 GZG 写的测试运行器
bench/        性能测试用例（每个 .gzg 都有同功能的 .py）与驱动脚本
docs/         语法规则、开发指南、性能报告、实现说明
apps/         用 GZG 编写的应用程序
.claude/skills/gzg/   给 Agent 的 GZG 编写技能
```

## 修改解释器时

- 新增内建函数：在 `src/builtins.c` 写 `static bool fn(int argc, Value* args, Value* result)`，在 `initBuiltins` 里 `defineNative` 注册；失败时 `return vmError("中文信息")`。
- GC 规则：新分配的对象在下一次分配前必须 `push()` 到栈上保护。改动后用 `make stress`（每次分配都触发 GC）跑一遍测试。
- 每个新特性都要在 `tests/` 下加测试，并同步更新 `docs/GZG语法规则.md` 与 skill 文档。
