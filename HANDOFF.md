# GZG 作业交接文档（给接手的 AI 助手）

你接手的是用户（郭志罡，学号 2024210937）的课程作业「程序设计实践：设计并实现自己的脚本语言」。作业已经全部完成，接下来的事情是：**随时就这份作业指导他，帮他准备答辩讲解、现场演示和应对老师提问**。他之前的 AI 可能无法再使用，这份文档就是全部交接。

## 0. 开始前必读

- **仓库：** `C:\Users\32915\Desktop\大三\程序设计实践\gzg-lang`（Windows，git bash；编译器 MinGW gcc 8.1，用 `mingw32-make`；Python 虚拟环境在 `.venv`）。
- **GitHub（公开）：** https://github.com/Lazyy-star/gzg-lang 。本地仓库丢了也可以 `git clone` 这个地址恢复。
- **状态：** 全部交付物已完成，并已推送到上面的 GitHub 仓库 `main` 分支。之后的改动：本地提交后 `git push` 即可。
- **写于 2026-10-29。** 文中数字是当时实测的。数字有疑问时以第 11 节的命令重新核对为准，**不要凭记忆或猜测回答**。
- **用户偏好：**
  - 一律用中文回答（包括中途的进度汇报）；
  - 要准确，不确定就运行命令或读代码确认，不要猜；
  - 改动要小、改完要验证；推送和对外发布由他决定。
- **验收形式：** 线下答辩。讲 PPT、演示、回答老师关于实现细节的提问。评分：解释器 20、自动测试 20、性能 10、语法说明与开发指南 20、Agent 开发应用 30。

## 1. 作业要求与交付物对照

作业要求设计一门以自己名字命名的解释型语言，实现解释器；**测试、性能程序和应用都必须用这门语言自己写**（解释器本身可以用任何语言实现）。

| 要求 | 交付物 | 关键事实 |
|---|---|---|
| 语言设计（语法 + 语义） | `docs/GZG语法规则.md` | 含完整 EBNF；名字 GZG 取自姓名拼音首字母 |
| 解释器 | `src/`（C99，7897 行，零依赖） | 单遍编译成字节码 + 栈式虚拟机 + 标记清除 GC |
| 黑盒测试，一个命令运行 | `tests/`，运行器 `tests/run_tests.gzg` | 327 个测试、20 个分类，**测试和运行器都是 GZG 写的**；行覆盖率 94.4% |
| 性能测试与报告 | `bench/`，`docs/性能测试报告.md` | 9 组 GZG/Python 同功能程序；**计时驱动 `bench/run_bench.gzg` 也是 GZG 写的**；几何平均快 1.41 倍 |
| 开发指南（人用 + AI 用） | `docs/GZG开发指南.md`；`.claude/skills/gzg/`（SKILL.md、reference.md、pitfalls.md）；`AGENTS.md` | Skill 文档有 18 条“与 Python/JS 的差异”规则 |
| Agent 开发 ≥200 行应用 + 开发记录 | `apps/game2048/`（925 行），`apps/game2048/DEVLOG.md` | 开发它的 Agent **只读过 Skill 文档**；AI 6 局全部达成 2048 |
| Git 历史 | 仓库本身 | 20 余次提交，含 2048 Agent 的 5 次阶段性提交 |
| 系统介绍 PPT | `docs/GZG语言介绍.pptx`（18 页，带动画）；同内容网页 `docs/GZG语言介绍.html` | 两者由同一份内容数据生成 |
| 全项目开发记录 | `docs/开发记录.md` | Agent 分工、时间线、关键决策、发现与修复的问题 |

另有 `docs/GZG实现说明.md`：解释器架构，按答辩讲解顺序写的，**准备实现细节提问时先读它**。

## 2. 一分钟讲清楚（电梯陈述）

> 我设计了一门叫 GZG 的脚本语言：写起来像 Python 一样简单，用花括号分块；支持中英双语关键字、字符串插值、match 模式匹配、管道运算符和命名参数；报错是中文的，还会提示“你是不是想写 xxx”。解释器用 C 写，约 7900 行：源码先单遍编译成字节码，再由虚拟机执行，内存由标记清除垃圾回收管理。我用 GZG 自己写了 327 个测试，一个命令跑完，代码覆盖率 94%；在 9 组同功能程序上，平均比 CPython 快 1.41 倍。最后让一个只读过我写的 AI 文档的 Agent 用 GZG 开发了 925 行的 2048 游戏和 AI，它只犯了 2 次错，6 局全部拼出 2048。

## 3. 答辩要背熟的数字（及来源）

| 数字 | 含义 | 来源 / 复核方式 |
|---|---|---|
| 7,897 行 | 解释器源码（`src/*.c` + `*.h`） | `cat src/*.c src/*.h \| wc -l` |
| 最大的三个文件 | builtins.c 1826、compiler.c 1749、vm.c 1712 行 | `wc -l src/*.c` |
| 71 条 | 字节码指令（`OpCode` 枚举） | `src/chunk.h` |
| 25 个 | 关键字，每个都有中文写法（如果/否则/遍历/函数/结构/匹配…） | `src/lexer.c` 的 `keywords[]` |
| 50 + 7 / 71 | 全局内建函数 50 个 + 7 个中文别名；内建方法 71 个 | `src/builtins.c` 的 `initBuiltins` |
| 327 个、20 类 | 黑盒测试数与分类数 | `build/gzg tests/run_tests.gzg` 末行 |
| 94.4% | gcov 行覆盖率（5043 行可执行代码） | `mingw32-make coverage` |
| 全部通过 | GC 压力模式（每次分配都触发回收）下的测试 | `mingw32-make stress`（约 1 分钟） |
| 12 / 11 / 1 | 测试 Agent 发现 12 处实现与文档不一致，修了 11 处，1 处列为已知限制 | `docs/开发记录.md` 第 4 节 |
| 1.41× | 9 组性能用例的几何平均（Python 耗时 ÷ GZG 耗时），8 组 GZG 更快 | `bench/results/results.json` |
| loop 3.16×、sieve 2.23×、fib 1.39× | 优势最大的几项 | 同上 |
| strings 0.28× | 唯一更慢的一项（原因见第 6 节问 8） | 同上 |
| 925 行 | 2048 应用的 GZG 代码（6 个文件） | `wc -l apps/game2048/*.gzg` |
| 2 次 | 2048 Agent 开发中的报错（语法 1、逻辑 1），另发现 1 个解释器 bug | `apps/game2048/DEVLOG.md` 第 8 节 |
| 4096 ~ 16384 | AI 在 6 个种子上的最大块（seed 3 最好：16384，290724 分） | DEVLOG 第 7 节 |
| 15 → 18 条 | 根据 2048 Agent 的反馈，Skill 文档规则从 15 条扩到 18 条 | `.claude/skills/gzg/SKILL.md` |

性能数字每次重跑会有 ±10% 的波动，答辩时说“约 1.4 倍”最稳妥。

## 4. 语言特色（讲“创新点”时用）

1. **中英双语关键字**：`如果 x > 0 { 打印("正") }` 与英文写法编译出的字节码完全相同。中文关键字与标识符之间要有空格（`如果x` 会被当成一个变量名）。
2. **会纠错的报错**：编译错误给出 `文件:行:列`、源码行和 `^` 标记；运行错误给出调用栈。找不到名字时按编辑距离给建议（相邻字母换位也算一次，如 `nmae→name`）；还有一张跨语言对照表：`append→push`、`None→nil`、`a if c else b → c ? a : b`。全角标点（`（，；`）会被拒绝并提示改半角。
3. **换行即语句结束**：仿 Go 的自动分号规则；括号内可以随意换行；下一行以 `.方法` 或 `|>` 开头时自动续行。
4. **字符串插值** `"{表达式}"`，可以嵌套。
5. **match 模式匹配**：字面量、多值 `1, 2, 3`、区间 `90..=100`、守卫 `if`、变量绑定、通配 `_`；既能当语句也能当表达式。注意：**单独的标识符是绑定而不是比较**（与 Rust 相同），要比较变量的值写 `(limit) =>`。
6. **管道** `x |> f(a)` 等于 `f(x, a)`。
7. **默认参数与命名参数**：默认值每次调用重新求值（没有 Python 的“可变默认值共享”坑）；`Point(y: 1, x: 2)`。
8. **严格结构体**：字段在定义时固定，给实例加新字段直接报错；`to_str()` 自定义显示；`==` 按字段深比较。
9. **整数与浮点分开**：`/` 总是浮点，`//` 向下取整（同 Python）；整数是 64 位，溢出回绕。
10. **有序字典**：遍历顺序 = 插入顺序。
11. 其他：闭包、`try/catch/throw`（可抛任意值）、`import`、解构 `let [a, b] = ...`、REPL、`--dis` 反汇编、`--stats` GC 统计。

## 5. 解释器实现要点（老师追问实现细节时用）

完整讲解在 `docs/GZG实现说明.md`。下面是要点和代码位置（行号是 2026-10-29 的，可能略有偏移，用函数名搜索）。

**整体流程**：源码 → `lexer.c` 词法分析 → `compiler.c` 单遍编译（不建 AST，边解析边生成字节码）→ `chunk.c` 字节码 → `vm.c` 虚拟机执行；`memory.c` 是 GC，`builtins.c` 是内建函数库，`platform.c` 处理 Windows 中文路径与 UTF-8 控制台。`main.c` 是命令行入口。

| 主题 | 怎么做的 | 代码位置 |
|---|---|---|
| 值的表示 | 16 字节带标签联合体：nil / bool / int64 / double / 对象指针；另有内部标签 `UNDEF` 表示“未定义全局 / 未传参数” | `src/value.h` |
| 字符串 | 不可变，**全部驻留**（相同内容只存一份），相等比较就是比指针；同时存字节数和码点数，纯 ASCII 下标 O(1) | `object.c` 的 `copyString`、`concatStrings`、`hashString`（每次处理 8 字节） |
| 字典 | 有序哈希表：条目数组按插入顺序 + 开放寻址的索引数组（同 CPython 3.7+），删除留墓碑 | `object.c` 的 `mapSet` / `mapGet` |
| 自动分号 | 括号栈：`(` `[` 内忽略换行，`{` 内保留；换行前的记号能结束表达式才产生 NEWLINE；向前看下一行是否以 `.` 或 `\|>` 开头 | `lexer.c` 的 `skipWhitespace` |
| 字符串插值 | 遇到 `{` 返回插值片段并在括号栈压入特殊标记，匹配的 `}` 回到字符串扫描，所以可以嵌套 | `lexer.c` 的 `stringPart`；`compiler.c` 的 `interpolation` |
| 表达式解析 | Pratt 解析，18 级优先级；`**` 右结合且高于一元负号（`-2 ** 2 == -4`）；`not` 低于比较（同 Python） | `compiler.c` 的 `initRules`、`parsePrecedence` |
| 变量解析 | **编译期**决定：局部变量 → 栈槽下标 `GET_LOCAL n`；外层函数变量 → upvalue；其余 → 全局数组下标 `GET_GLOBAL n`。运行时不按名字查找 | `compiler.c` 的 `namedVariable`、`resolveLocal`、`resolveUpvalue` |
| 闭包 | 被内层函数捕获的局部变量，在离开作用域时由 `CLOSE_UPVALUE` 从栈上搬到堆上；for 循环每轮关闭循环变量，所以每轮的闭包捕获的是不同的变量 | `vm.c` 的 `captureUpvalue`、`closeUpvalues` |
| 数值 for 循环 | `for i in a..b` 编译成三条专用指令 `FOR_PREP / FOR_RANGE / FOR_STEP`，不创建区间对象 | `compiler.c` 的 `forStatement` |
| 默认参数 | 函数开头为每个有默认值的参数生成 `JUMP_IF_NOT_UNDEF`；调用方没传的参数填 `UNDEF`，所以默认值每次调用时求值 | `compiler.c` 的 `function`；`vm.c` 的 `callClosure` |
| 命名参数 | `CALL_KW` 带一个“参数名数组”常量，虚拟机按形参名重排实参 | `vm.c` 的 `callClosureKw`、`construct` |
| match | subject 留在栈顶，每个模式 `DUP` 后比较；命中后、执行分支体前先弹出 subject，所以分支体里的局部变量槽位正确；绑定模式直接把 subject 当新局部变量 | `compiler.c` 的 `matchBody` |
| 指令分派 | GCC 的 computed goto（标签地址表 `dispatch[256]`），每条指令末尾直接跳到下一条，比 switch 少一次边界检查 | `vm.c` 的 `run` |
| 函数调用 | 实参已经在栈上，新调用帧的 `slots` 直接指向它们，不复制；“闭包 + 参数个数正好匹配”走内联快速路径 | `vm.c` 的 `CASE(OP_CALL)` |
| 方法调用 | `obj.m(x)` 编译成 `INVOKE`，直接查方法表调用，不创建绑定方法对象 | `vm.c` 的 `invoke` |
| 异常 | 处理器栈记录（调用深度、栈顶、catch 地址）。抛出时取最近的处理器，关闭将丢弃的 upvalue、恢复栈和帧、压入异常值、跳到 catch。原生函数（如 `map`）回调 GZG 函数时递归进入 `run()`；异常属于外层时先退出内层 `run()` 再由外层处理，所以能穿过任意层回调 | `vm.c` 的 `handleThrow`、`callAndRun` |
| GC | 三色标记-清除：标记根（值栈、调用帧、开放 upvalue、全局表、内建方法表、编译器正在构造的函数、临时根）→ 灰栈追踪（字符串等叶子对象不入灰栈）→ 删除驻留池中未标记的字符串（弱引用）→ 清扫对象链表。阈值 = 存活量 × 2，最小 8MB | `memory.c` 的 `collectGarbage`、`markRoots` |
| GC 正确性 | 规则：新对象在下一次分配之前必须可达（先 `push` 到栈上）。`-DGC_STRESS` 编译后每次分配都触发完整 GC，用它跑全部测试 | `memory.c` 的 `reallocate`；`Makefile` 的 `stress` |
| 拼写建议 | Damerau-Levenshtein 编辑距离；名字 ≤2 个字符不建议；局部变量的候选在编译期记录 | `vm.c` 的 `editDistance`、`foreignNames` |
| 限制常量 | 调用深度 10000、值栈 64 万格、try 嵌套 1024、单函数局部变量 256、原生回调嵌套 20000；Windows 下链接 64MB C 栈 | `src/common.h`；`vm.c`；`Makefile` |
| 退出码 | 0 正常、65 语法错误、70 运行错误、74 文件打不开、64 命令行用法错误 | `src/common.h`、`main.c` |

**能现场展示实现的命令**：`build\gzg.exe --dis 文件.gzg`（反汇编，看到 `GET_LOCAL 1`、`GET_GLOBAL 61` 等）、`build\gzg.exe --tokens 文件.gzg`（看词法记号和自动插入的换行）、`build\gzg.exe --stats 文件.gzg`（GC 次数与耗时）。

## 6. 预测提问与参考回答

先给结论，再给一个具体例子或数字；不确定的问题当场说“这一点我需要查代码确认”，不要编。

1. **为什么用 C 而不是 Python 写解释器？**
   作业允许任何实现语言。Python 写的树遍历解释器一般比 CPython 慢 50~100 倍，性能对比就失去意义；C 写字节码虚拟机才能讨论“为什么快、为什么慢”。代价是要自己管内存，所以写了 GC，并用压力模式验证。
2. **为什么不建 AST，单遍编译有什么好处和代价？**
   好处是快、省内存，变量在编译期就解析成下标。代价是不能做需要“回头看”的优化，有些语法要用前瞻解决（比如区分命名参数 `name: v` 用了两个记号的前瞻）。
3. **变量查找为什么快？**
   编译期就确定位置：局部变量是栈槽下标，全局变量是数组下标。用 `--dis` 演示 `GET_LOCAL 1`、`GET_GLOBAL 61`。CPython 的全局变量和内建函数需要查字典。
4. **闭包怎么实现？循环里的闭包为什么不会都捕获到最后一个值？**
   upvalue 机制：变量还在栈上时 upvalue 指向栈槽，离开作用域时“关闭”，把值搬进 upvalue 对象。for 循环每一轮结束都关闭循环变量，所以每轮是新的变量。测试 `tests/closures/` 里有对应用例。
5. **GC 怎么保证不会误回收？**
   三色标记-清除，根集合包括值栈、调用帧、全局表、编译器正在构造的函数等。实现上的规则是新对象分配后立即放到栈上保护。验证手段是 `make stress`：每次分配都触发一次完整 GC，全部测试照样通过。
6. **异常怎么穿过 `map` 这类内建函数的回调？**
   内建函数回调 GZG 函数时会递归进入 `run()`；`handleThrow` 发现最近的处理器属于外层时，先结束内层 `run()` 并返回失败，原生函数把失败往上传，外层 `run()` 再跳到 catch。
7. **性能对比公平吗？**
   9 组程序算法和数据规模完全相同，随机数用同一个线性同余公式生成；计时前先校验两边输出逐字相同；每项 5 次取中位数；计时驱动 `bench/run_bench.gzg` 用 GZG 写，两边进程启动开销相同。
8. **strings 为什么比 Python 慢那么多？**
   热点是 2 万次 `s += "ab"`。GZG 字符串不可变且驻留，每次拼接都要复制整串，整体 O(n²)；CPython 在字符串只有一个引用时会原地扩容，均摊 O(1)。对照实验：让 CPython 无法用这个优化（多留一个引用），它也要 0.15s，与 GZG 的 0.16s 相当。另外 20 万个存活字符串让每次 GC 扫描约 15ms。已优化“只复制一次 + 8 字节哈希”，拼接段从 0.63s 降到 0.16s。推荐写法 `push` + `join("")`，20 万片段只要 8ms。
9. **为什么字符串要全部驻留？**
   相等比较和字典查找变成比指针，`wordfreq` 这类字典密集的程序因此很快；代价就是第 8 问的拼接成本。
10. **其他用例为什么比 Python 快？**
    数字是值类型，不需要堆分配和引用计数（fib、loop、nbody、methods 全程零堆分配，`--stats` 可证）；编译期变量解析；数值循环专用指令（loop 快 3 倍多）；computed goto 和调用快速路径。
11. **中文关键字怎么实现？**
    关键字表里每个记号类型有中英文两个条目，编译器只看记号类型，所以两种写法生成的字节码完全一样。
12. **没有分号，怎么知道语句在哪结束？**
    仿 Go：换行前的记号能结束一个表达式（标识符、字面量、右括号等）时就插入语句结束标记；括号里的换行忽略；下一行以 `.` 或 `|>` 开头时续行。可以用 `--tokens` 演示。
13. **match 是怎么编译的？**
    见第 5 节表格。关键点是分支体执行前先弹出 subject，这样分支体里声明的局部变量槽位不会错位。
14. **测试怎么保证覆盖全面？**
    20 个分类覆盖词法、类型、运算符、字符串、数组、字典、控制流、函数、闭包、结构体、match、异常、模块、输入输出、内建函数、中文、综合程序、压力、命令行；gcov 行覆盖率 94.4%；测试由只看文档的 Agent 黑盒编写，反过来发现了 12 处实现与文档不一致，包括 3 个会卡死或崩溃的 bug（结构体默认值写错时编译器死循环、深层回调递归导致 C 栈溢出、`to_str` 深递归崩溃）。
15. **给 AI 的文档是怎么设计的？效果怎么验证？**
    按“与你熟悉的语言哪里不同”组织（18 条规则），先给能照抄的代码，再给规则；报错信息本身也是文档（跨语言对照表）。验证：让一个全新的 Agent 只读这三份文档开发 2048，它全程报错 2 次，并指出文档缺了 import 规则等 4 处，已据此修订。
16. **你自己做了什么，Agent 做了什么？**（很可能被问，要如实说）
    作业允许并要求使用编程 Agent。整个项目在 Claude Code 中完成，`docs/开发记录.md` 第 1 节写明了分工：语言设计、头文件接口、词法/编译器/虚拟机/GC、文档、性能用例和 PPT 由主 Agent 完成；`builtins.c` 和 `platform.c` 由一个子 Agent 按接口约定编写；测试和 2048 分别由两个限制了可见范围的子 Agent 编写。**建议用户把精力放在讲清楚设计取舍和关键实现，而不是声称每行都是亲手写的**；提问时能对着代码讲明白原理就好。
17. **GZG 和现有语言有什么不同？**
    语义接近 Python，语法接近 Rust/Go，但组合不同于任何现存语言：双语关键字、严格结构体加命名构造、match 区间与绑定、管道、默认参数每次求值、会给跨语言修改建议的中文报错。
18. **2048 的 AI 用的什么算法？**
    Expectimax：玩家节点取四个方向的最大值，机会节点对每个空格按 0.9/0.1 的概率放 2 或 4 求期望；按空格数调整搜索深度；评估函数看空格数、单调性、可合并数等；加了线评估缓存和置换表，每步从 1.3 秒降到约 0.13 秒。
19. **怎么给语言加一个新内建函数 / 新语法？**
    内建函数：在 `builtins.c` 写一个 `static bool fn(int argc, Value* args, Value* result)`，在 `initBuiltins` 里 `defineNative` 注册，失败时 `return vmError("...")`。新语法：词法加记号 → `compiler.c` 加解析与字节码生成 → 如需要在 `chunk.h` 加指令、`vm.c` 实现 → 加测试 → 更新文档。`AGENTS.md` 有说明。
20. **有什么不足？**
    如实说第 8 节的已知限制，并说明已经分析过原因、知道怎么改（例如分代 GC 或字符串构建器）。

## 7. PPT 讲解提纲（18 页，建议 10~12 分钟）

| 页 | 标题 | 讲什么（每页 30~50 秒） |
|---|---|---|
| 1 | GZG 语言 | 名字来源；一句话：设计、实现、测试、性能、AI 辅助开发 |
| 2 | 一页看懂 | 四个数字：7897 行解释器、327 个测试、快 1.41 倍、925 行 2048；强调测试 / 性能 / 应用全用 GZG 写 |
| 3 | 设计目标与取舍 | 好写、早报错、中文友好、对 AI 友好、够快；每条都有代价（如花括号分块） |
| 4 | 语法速览 | 指着代码讲 let / fn / struct / for / 插值 / try；提一句 `/` 和 `//` |
| 5 | 中英双语 + 会纠错的报错 | 左边中文程序，右边报错示例；拼写建议与跨语言对照（append→push） |
| 6 | 模式匹配、管道与命名参数 | match 的区间、多值、守卫、绑定；管道从左往右读 |
| 7 | 解释器架构 | 五个阶段的流水线；三个支撑模块；代码行数分布 |
| 8 | 编译器 | fib 的反汇编：n 在栈槽 1，fib 是全局下标 61，运行时不查名字 |
| 9 | 虚拟机 | 16 字节值、零拷贝调用、computed goto、异常处理器栈、有序字典 |
| 10 | GC | 四步标记清除；`--stats` 的数据；压力模式保证正确性 |
| 11 | 测试 | 测试文件格式（`# expect`）；黑盒 Agent 发现 12 处问题；各分类数量 |
| 12 | 性能 | 图表与表格；8 胜 1 负；几何平均 1.41 倍 |
| 13 | 快在哪里，慢在哪里 | 快：值类型数字、编译期变量解析、专用循环指令；慢：strings 的 O(n²) 拼接与 GC 扫描，已定位并给出推荐写法 |
| 14 | 给 AI 的文档 | 三份 Skill 文档各管什么；写法原则；“限制 Agent 可见范围来检验文档”的思路 |
| 15 | 2048 | 只读 Skill 的 Agent 写了 925 行；AI 6 局全部达成 2048 |
| 16 | 开发记录 | Agent 踩的坑 → 文档怎么改；15 条规则扩到 18 条 |
| 17 | 工程过程 | Git 历史；先定接口再并行开发；质量手段 |
| 18 | 谢谢 | 五句话总结，进入提问 |

PPT 带动画：每页淡入切换，内容自动按组出现（不用点鼠标），封面背景缓慢放大。**只有放映模式才看得到动画。**网页版 `docs/GZG语言介绍.html` 内容相同、交互更多，可以作为备用或补充演示。

## 8. 现场演示脚本

在仓库根目录执行（Windows 命令行用反斜杠 `build\gzg.exe`，git bash 用 `build/gzg.exe`）。演示前先 `mingw32-make` 确认能编译。

```bash
build/gzg.exe examples/hello.gzg                 # 1. 语言特色：结构体、match、中文关键字、管道、命名参数、异常
build/gzg.exe tests/run_tests.gzg                # 2. 一键跑完 327 个测试（约 25 秒），最后显示分类统计
build/gzg.exe -e 'let a = []
a.append(1)'                                     # 3. 报错会提示“你是不是想写 'push'？”
build/gzg.exe --dis examples/hello.gzg | head -30    # 4. 看编译出的字节码
build/gzg.exe apps/game2048/main.gzg             # 5. 手动玩 2048（w/a/s/d，u 撤销，q 退出）
build/gzg.exe apps/game2048/main.gzg --seed 1 --plain < apps/game2048/moves.txt   # 6. 几秒重放一局，结果固定为 316 分
build/gzg.exe bench/run_bench.gzg --runs 1 fib loop strings   # 7. 现场跑性能对比（约 10 秒）
build/gzg.exe                                    # 8. REPL，输入表达式立即看结果，.exit 退出
```

注意：
- **不要现场跑 `--auto --fast`**：AI 自动玩一局要 7~22 分钟。要展示 AI，用网页版第 15 页（浏览器里的简化 AI 会自动玩，按 W/A/S/D 接管），或者讲 DEVLOG 里的 6 局结果。
- 第 7 步如果 CPU 繁忙，数字会和报告略有出入，正常。
- 演示 2048 前会写 `apps/game2048/highscore.txt`（已被 `.gitignore` 忽略），无影响。

## 9. 已知限制（被问到时如实说）

- **字符串反复 `+=` 拼接是 O(n²)**，原因见第 6 节问 8；推荐写法是 `push` + `join`。
- **追踪式 GC 对大量存活对象有扫描开销**；没有做分代回收。
- **`..=INT_MAX` 这类端点恰为 64 位整数极值的闭区间会被当成空区间**（区间内部按左闭右开存储）。这是测试 Agent 发现的 12 处问题中唯一没修的，已写入语法文档。
- 没有继承、没有大整数（溢出回绕）、没有生成器 / 协程、所有文件共享一个全局命名空间。
- `pad_left` / `center` 按字符数补齐，中文在终端里占两格，中英混排表格可能对不齐。
- 文件读写的路径相对当前工作目录，而 `import` 相对写 import 的文件；没有 `__file__`。
- PPT 第 4 页的代码、第 17 页的 git 记录字号偏小（为放进框里自动缩小了）。

## 10. 怎么修改与重新生成

| 想改什么 | 改哪里 | 然后运行 |
|---|---|---|
| PPT / 网页的文字内容 | `docs/ppt/content_a.py`（第 1~6 页）、`content_b.py`（7~12）、`content_c.py`（13~18）；封面作者行在 `docs/ppt/facts_manual.json` | `.venv/Scripts/python docs/ppt/gen_facts.py` → `.venv/Scripts/python docs/ppt/build_ppt.py` → `.venv/Scripts/python docs/web/build_web.py` |
| PPT 的版式 / 动画 | `docs/ppt/blocks.py`（各版块）、`build_ppt.py`（页框）、`anim.py`（切换与入场动画 XML）、`theme.py`（配色字体） | 同上；`.venv/Scripts/python docs/ppt/preview.py` 用 PowerPoint 导出每页并拼成 `build/slides/contact.png` 供检查 |
| 网页的样式 / 交互 | `docs/web/style.css`、`stars.js`（星轨）、`render.js`、`widgets.js`（图表、2048 等）、`app.js`（翻页） | `.venv/Scripts/python docs/web/build_web.py`；`.venv/Scripts/python docs/web/check_web.py` 在真实浏览器里检查交互与脚本错误 |
| 背景图 | `docs/ppt/make_bg.py` | 运行后重新生成 PPT 和网页 |
| 解释器 | `src/` | `mingw32-make`，再跑 `build/gzg.exe tests/run_tests.gzg`；改了内存相关代码还要 `mingw32-make stress` |
| 性能数据 | — | `build/gzg.exe bench/run_bench.gzg`（覆盖 `bench/results/`）→ `.venv/Scripts/python bench/plot_chart.py` → 更新 `docs/性能测试报告.md` 的表格 → 重新生成 PPT 和网页 |

**注意：**`bench/run_bench.gzg` 会覆盖 `bench/results/results.json`，PPT 和网页都从这里取数。只想演示不想改数据时，跑完用 `git checkout -- bench/results` 还原。

## 11. 核对现状的命令

```bash
cd /c/Users/32915/Desktop/大三/程序设计实践/gzg-lang
git log --oneline | head; git status --short     # 提交历史；应当没有未提交的改动
mingw32-make && build/gzg.exe tests/run_tests.gzg   # 编译并跑全部测试，末行应为“全部通过：327/327”
mingw32-make coverage | tail -12                 # 各文件与合计行覆盖率
mingw32-make stress                              # GC 压力模式（约 1 分钟）
build/gzg.exe apps/game2048/test_board.gzg       # 2048 单元测试：共 62 项断言，全部测试通过
cat src/*.c src/*.h | wc -l                      # 解释器行数
```

## 12. 环境里的坑（之前踩过的）

- **中文路径**：仓库路径含中文。PowerShell 的 `Resolve-Path` 和 PowerPoint COM 处理中文路径会失败，`docs/ppt/preview.py` 已经先把文件复制到临时目录再导出。
- **PowerPoint COM** 偶尔报 `RPC_E_CALL_REJECTED`（PowerPoint 正忙，例如用户正打开着 PPT），稍后重试即可。
- **无头浏览器截图**：Edge `--headless --screenshot` 不推进 CSS 过渡，截到的是动画开始前的状态。网页加 `?capture` 参数会关掉过渡直接显示最终状态，`check_web.py` 用 DevTools 协议做真实交互检查。
- **控制台编码**：Python 往 git bash 管道打印中文会变成 GBK 乱码，命令前加 `PYTHONIOENCODING=utf-8`，或在脚本里 `sys.stdout.reconfigure(encoding="utf-8")`。
- **用 heredoc 写 Python 补丁时**，字符串里的 `\n` 容易被转义成真实换行导致语法错误；用 `chr(10)` 或直接编辑文件更安全。
- **Git 换行符**：`.gitattributes` 统一 LF；`*.in` 设为 `-text`，保留 `tests/io/input_crlf.in` 的 CRLF，不要改。
- **测试运行器**每次使用独立的 stderr 临时文件，可以并行跑多个；以 `_` 开头的文件和目录（如 `tests/_pending`）不会被运行。

## 13. 文件地图

```
src/                解释器（lexer → compiler → chunk → vm；memory=GC；builtins=内建库；platform=系统接口；main=命令行）
tests/              327 个测试，按 20 个分类分目录；run_tests.gzg 是运行器；_pending/ 放待定用例
bench/cases/        9 对 .gzg/.py 同功能程序；bench/run_bench.gzg 计时驱动；plot_chart.py 画图；results/ 结果
apps/game2048/      2048 + AI（board/ai/ui/main/test_board/bench_ai.gzg）、README.md、DEVLOG.md、moves.txt
examples/hello.gzg  演示用的特色程序
docs/               GZG语法规则.md、GZG开发指南.md、GZG实现说明.md、性能测试报告.md、开发记录.md、
                    GZG语言介绍.pptx / .html；ppt/ 与 web/ 是生成它们的脚本
.claude/skills/gzg/ 给 AI 的 Skill 文档（SKILL.md、reference.md、pitfalls.md）
AGENTS.md / CLAUDE.md  给编程 Agent 的项目入口说明
HANDOFF.md          本文档
```

## 14. 怎么帮用户准备

- **先摸底**：问他离答辩还有多久、对哪部分最没把握（通常是解释器实现细节和“Agent 做了什么”）。
- **模拟答辩**：按第 6 节逐题提问，听他用自己的话回答，指出不准确的地方并给出对应的代码位置；每次只问一题，等他答完再追问一层（例如“那 upvalue 什么时候关闭？”）。
- **带读代码**：老师可能让他现场指着代码讲。建议按“一行 GZG 代码的一生”带他走一遍：`print(1 + 2)` → `--tokens` 看记号 → `compiler.c` 的 `parsePrecedence` / `binary` 生成 `GET_GLOBAL (print)  CONSTANT 1  CONSTANT 2  ADD  CALL 1`（可用 `--dis` 现场验证）→ `vm.c` 的 `run` 里 `CASE(OP_ADD)` 执行加法、`CASE(OP_CALL)` 调用 → `builtins.c` 的 `nat_print` 输出。再讲一个函数调用和一个闭包。
- **演示彩排**：让他在答辩用的电脑上从头跑一遍第 8 节，确认编译器、字体、PowerPoint 都正常；建议提前把仓库拷到答辩电脑并编译好。
- **改 PPT**：用户提修改意见时按第 10 节改内容文件再重新生成，改完用 `preview.py` 和 `check_web.py` 检查，然后提交。
- **保持诚实**：数字以实测为准；被问到 Agent 的分工如实讲；不足之处讲清原因和改进方向，比回避更好。

