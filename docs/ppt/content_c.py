"""第 13~18 页：性能分析、AI 文档、2048、开发记录、工程过程、结尾。"""

BOARD = [[16384, 4096, 512, 2], [2048, 1024, 256, 8], [128, 64, 32, 4], [4, 8, 16, 2]]   # seed 3 的终局


def slides_c(F, B):
    sp = {r["name"]: r["speedup"] for r in B["results"]}
    geo = B["meta"]["geomean"]
    return [
        {"kicker": "11 · ANALYSIS", "title": "快在哪里，慢在哪里", "blocks": [
            {"type": "cards", "area": (0, 0, 12, 6), "size": 13, "items": [
                {"title": "为什么多数场景比 CPython 快", "body": [
                    "数字是值类型：不需要堆分配和引用计数",
                    "变量在编译期解析成下标，运行时不查字典",
                    f"数值 for 循环用专用指令：loop 快 {sp['loop']:.1f} 倍",
                    f"computed goto + 调用快速路径：fib 快 {sp['fib']:.1f} 倍",
                    "fib / loop / nbody / methods 全程零堆分配"]},
                {"title": "strings 为什么慢（根因与取舍）", "accent": "warm",
                 "body": [x.lstrip("• ") for x in F["strings_analysis"]]}]},
        ]},

        {"kicker": "12 · AI DOCS", "title": "让 Agent 写对一门没见过的语言", "blocks": [
            {"type": "table", "area": (0, 0, 7, 4.2), "size": 13, "widths": [3.0, 4.0], "rows": [
                ["文件", "作用"],
                [".claude/skills/gzg/SKILL.md", "Claude Code 技能：速览 + 18 条与 Python/JS 的差异 + 性能提示"],
                ["reference.md", "全部内建函数与方法：签名、返回值、边界行为"],
                ["pitfalls.md", "报错信息 → 原因 → 改法"],
                ["AGENTS.md / CLAUDE.md", "通用 Agent 入口：构建、测试、目录、修改规范"],
                ["docs/GZG语法规则.md", "完整规范与 EBNF"]]},
            {"type": "bullets", "area": (7.3, 0, 4.7, 6), "size": 14, "items": [
                "先给代码::能直接照抄的片段放在最前，再讲规则",
                "写差异::Agent 的错误几乎都来自迁移习惯，所以按“与你熟悉的语言哪里不同”组织",
                "报错即文档::解释器会提示 append→push、None→nil",
                "鼓励实测::不确定就 gzg -e 跑一下，不要猜",
                "闭环::新 Agent 只读这三份文档开发 2048，按它的开发记录修订文档"]},
            {"type": "quote", "area": (0, 4.5, 7, 1.5),
             "text": "测试 Agent 只看语法文档，应用 Agent 只看 Skill 文档——它们踩到的坑，就是文档还缺的地方。"},
        ]},

        {"kicker": "13 · APPLICATION", "title": "Agent 开发的 2048 + Expectimax AI", "sub": F["app_subtitle"], "blocks": [
            {"type": "board2048", "area": (0, 0, 4.4, 6), "board": BOARD, "caption": "seed 3 终局 · 290724 分"},
            {"type": "bullets", "area": (4.8, 0, 7.2, 4.6), "size": 14, "items": [
                "模块化::board（纯逻辑）· ai · ui · main 用 import 组织，另有 62 项断言的单元测试",
                "交互::wasd / 方向词 / 中文“上下左右”，多步撤销、重来、ANSI 256 色",
                "AI::Expectimax + 线评估缓存 + 置换表，每步 1.3s → 0.13s",
                "可复现::--seed 固定随机数，moves.txt 重放两次输出逐字节相同",
                "开发记录::报错仅 2 次（语法 1、逻辑 1），另发现 1 个解释器 bug"]},
            {"type": "pills", "area": (4.8, 4.85, 7.2, 1.15), "items": [
                "seed 42 · 4096", "seed 1 · 8192", "seed 2 · 8192", "seed 3 · 16384", "seed 4 · 4096", "seed 5 · 4096"]},
        ]},

        {"kicker": "14 · DEV LOG", "title": "Agent 踩了哪些坑，文档因此改了什么", "blocks": [
            {"type": "table", "area": (0, 0, 12, 4.9), "size": 12, "widths": [4.3, 3.6, 4.1],
             "rows": [["Agent 遇到的问题", "原因", "改进"]] + F["devlog_rows"]},
            {"type": "quote", "area": (0, 5.1, 12, 0.9), "text": F["devlog_summary"]},
        ]},

        {"kicker": "15 · PROCESS", "title": "工程过程：分工与 Git 历史", "sub": f"{F['commit_count']} 次提交，包含 2048 Agent 的分阶段提交",
         "blocks": [
            {"type": "code", "area": (0, 0, 7.2, 6), "size": 11, "lang": "git", "title": "git log --oneline --reverse", "src": F["git_log"]},
            {"type": "bullets", "area": (7.5, 0, 4.5, 6), "size": 14, "items": [
                "先定接口::先写头文件固定接口，核心与内建库并行开发",
                "黑盒测试::独立 Agent 只看文档编写，发现的问题回到解释器修复",
                "应用开发::另一个 Agent 只读 Skill，按阶段提交",
                "质量手段::全部测试 + GC 压力模式双跑；覆盖率 " + F["coverage"].split("（")[0],
                "可复现::性能对比先校验输出一致；开发记录写明每一步"]},
        ]},

        {"kind": "end", "bg": "end", "title": "谢谢", "en": "Questions welcome", "items": [
            "语言::双语关键字、插值、match、管道、命名参数，报错会教你改",
            f"实现::{F['interp_lines']} 行 C99，单遍编译 + computed goto 虚拟机 + 标记清除 GC",
            f"测试::{F['test_count']} 个 GZG 编写的黑盒测试一键运行，覆盖率 " + F["coverage"].split("（")[0],
            f"性能::9 组用例几何平均比 CPython 快 {geo:.2f} 倍，弱项有根因分析",
            f"AI::新 Agent 只读 Skill 文档写出 {F['app_lines']} 行的 2048 + AI，开发记录反哺文档"]},
    ]


def all_slides(F, B):
    from content_a import slides_a
    from content_b import slides_b
    return slides_a(F, B) + slides_b(F, B) + slides_c(F, B)
