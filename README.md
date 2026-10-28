# GZG 语言

GZG（取自作者姓名拼音首字母）是一门解释执行的通用脚本语言：**中英双语关键字**、**换行即语句结束**、**字符串插值**、**match 模式匹配**、**管道运算符**、**命名/默认参数**、**严格结构体**，以及带拼写建议的中文错误信息。解释器用 C99 实现（约 7900 行，字节码虚拟机 + 标记清除 GC），9 个基准测试几何平均比 CPython 3.12 快 1.44 倍；327 个黑盒测试、行覆盖率 94%；一个只读过 Skill 文档的 Agent 用它写出了 925 行的 2048 + Expectimax AI（6 个种子全部达成 2048）。

```gzg
struct Student {
    name, score = 0
    fn grade() => match self.score {
        90..=100 => "优", 60..90 => "良", _ => "差",
    }
}

let students = [Student("小红", 95), Student("小明", 72), Student(name: "小刚")]
for s in students.sorted(fn(s) => -s.score) {
    print("{s.name}  {str(s.score).pad_left(3)}  {s.grade()}")
}

如果 students.any(fn(s) => s.score < 60) {
    打印("有同学需要补考")
}
```

## 快速开始

```bash
make                                 # Windows: mingw32-make，生成 build/gzg.exe
build/gzg examples/hello.gzg         # 运行脚本
build/gzg                            # 交互式环境
build/gzg tests/run_tests.gzg        # 运行全部测试（一个命令）
python bench/run_bench.py            # 与 Python 的性能对比
build/gzg apps/game2048/main.gzg     # 用 GZG 写的 2048 游戏（含 AI）
```

## 交付物索引

| 内容 | 位置 |
|---|---|
| 语法规则（规范 + EBNF） | [docs/GZG语法规则.md](docs/GZG语法规则.md) |
| 开发指南（教程、调试、测试、工程实践） | [docs/GZG开发指南.md](docs/GZG开发指南.md) |
| 给 AI 的开发技能 | [.claude/skills/gzg/SKILL.md](.claude/skills/gzg/SKILL.md)（+ [reference](.claude/skills/gzg/reference.md)、[pitfalls](.claude/skills/gzg/pitfalls.md)）、[AGENTS.md](AGENTS.md) |
| 解释器源码 | [src/](src/)，架构见 [docs/GZG实现说明.md](docs/GZG实现说明.md) |
| 黑盒测试集 | [tests/](tests/)，运行器 [tests/run_tests.gzg](tests/run_tests.gzg) 本身用 GZG 编写 |
| 性能测试与报告 | [bench/](bench/)，[docs/性能测试报告.md](docs/性能测试报告.md) |
| Agent 开发的应用 | [apps/game2048/](apps/game2048/)，开发记录 [DEVLOG.md](apps/game2048/DEVLOG.md) |
| 系统介绍 PPT | [docs/GZG语言介绍.pptx](docs/GZG语言介绍.pptx) |

## Make 目标

| 命令 | 作用 |
|---|---|
| `make` | 编译解释器 |
| `make test` | 运行全部黑盒测试 |
| `make stress` | 用“每次分配都触发 GC”的版本运行全部测试，检查 GC 正确性 |
| `make coverage` | 带 gcov 插桩运行测试，输出各源文件的行覆盖率 |
| `make bench` | 性能对比并生成报告 |
