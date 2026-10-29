"""第 7~12 页：架构、编译器、虚拟机、GC、测试、性能。"""


def slides_b(F, B):
    lc = F["line_counts"]
    mods = ["lexer", "compiler", "vm", "builtins", "对象/表/值", "memory", "platform"]
    return [
        {"kicker": "05 · ARCHITECTURE", "title": "解释器架构", "sub": "单遍编译 + 字节码虚拟机 + 标记清除 GC，C99 实现，无第三方依赖",
         "blocks": [
            {"type": "pipeline", "area": (0, 0, 12, 2.3), "stages": [
                ["源码", ".gzg · UTF-8"], ["词法分析", "双语关键字 · 插值 · 自动分号"],
                ["编译器", "Pratt 解析 · 作用域 · 闭包"], ["字节码", "71 条指令 · 常量池 · 行号表"],
                ["虚拟机", "computed goto · 调用帧 · 异常"]]},
            {"type": "cards", "area": (0, 2.6, 7.4, 3.4), "size": 12, "items": [
                {"title": "memory.c", "body": ["三色标记-清除", "字符串池为弱引用"]},
                {"title": "builtins.c", "body": ["50 个全局函数 + 7 个中文别名", "71 个内建方法"]},
                {"title": "platform.c", "body": ["中文路径与参数", "UTF-8 控制台"]}]},
            {"type": "bars", "area": (7.7, 2.6, 4.3, 3.4), "title": "各模块代码行数", "unit": "行",
             "items": [[m, int(v)] for m, v in zip(mods, lc[:-1])]},
        ]},

        {"kicker": "06 · COMPILER", "title": "编译器：一遍扫描直接生成字节码", "sub": "不构建 AST；变量在编译期解析成栈槽 / upvalue / 全局下标",
         "blocks": [
            {"type": "code", "area": (0, 0, 4.6, 2.3), "size": 14, "title": "fib.gzg", "src": '''fn fib(n) {
    if n < 2 { return n }
    return fib(n - 1) + fib(n - 2)
}'''},
            {"type": "code", "area": (4.9, 0, 7.1, 6), "size": 13, "lang": "dis", "title": "gzg --dis fib.gzg", "src": '''== fib ==
0000    2 GET_LOCAL            1        ← n 在栈槽 1
0002    | CONSTANT             0 '2'
0005    | LESS
0006    | POP_JUMP_IF_FALSE    6 -> 12
0009    | GET_LOCAL            1
0011    | RETURN
0012    3 GET_GLOBAL          61 (fib)  ← 编译期定好下标
0015    | GET_LOCAL            1
0017    | CONSTANT             1 '1'
0020    | SUB
0021    | CALL                 1
0023    | GET_GLOBAL          61 (fib)
  ...
0034    | ADD
0035    | RETURN'''},
            {"type": "bullets", "area": (0, 2.6, 4.6, 3.4), "size": 13, "items": [
                "Pratt 解析::18 级优先级，** 右结合且高于一元负号",
                "闭包::被捕获的局部变量离开作用域时 CLOSE_UPVALUE 搬到堆上",
                "数值循环::for i in a..b 编译成 3 条专用指令",
                "match::每个模式 DUP 后比较，分支体执行前弹出 subject"]},
        ]},

        {"kicker": "07 · VIRTUAL MACHINE", "title": "虚拟机：值、调用与异常", "blocks": [
            {"type": "cards", "area": (0, 0, 12, 2.8), "size": 13, "items": [
                {"title": "16 字节带标签值", "body": ["nil / bool / int64 / double / 指针", "整数与浮点分开：7 // 2 精确为 3", "字符串全部驻留，相等即比指针"]},
                {"title": "零拷贝调用", "body": ["实参已在栈上，新帧直接指向它们", "参数个数匹配时走内联快速路径", "obj.m() 编译为 INVOKE，不创建绑定对象"]},
                {"title": "computed goto", "body": ["dispatch[256] 标签地址表", "每条指令末尾直接跳到下一条", "比 switch 少一次边界检查"]}]},
            {"type": "cards", "area": (0, 3.1, 12, 2.9), "size": 13, "items": [
                {"title": "异常：处理器栈", "body": ["TRY 登记（调用深度、栈顶、catch 地址）", "throw 时关闭 upvalue、恢复栈与帧，再跳到 catch",
                                                   "原生回调（map / sort）里抛出的异常也能穿出来"]},
                {"title": "有序字典（同 CPython 3.7+）", "body": ["条目数组按插入顺序 + 开放寻址索引", "删除留墓碑，满时原地压缩", "遍历顺序 = 插入顺序，输出可预测"]}]},
        ]},

        {"kicker": "08 · MEMORY", "title": "三色标记-清除垃圾回收", "blocks": [
            {"type": "steps", "area": (0, 0, 12, 2.1), "items": [
                ["标记根", "值栈 · 调用帧 · upvalue · 全局表 · 编译器"], ["追踪", "灰栈逐个出栈，叶子对象不入栈"],
                ["清理驻留池", "字符串池是弱引用"], ["清扫", "遍历对象链表，释放未标记对象"]]},
            {"type": "bullets", "area": (0, 2.4, 6.4, 3.6), "size": 14, "items": [
                "触发::已分配字节数超过阈值时回收；阈值 = 存活量 × 2，最小 8MB",
                "可观察::--stats 输出 GC 次数、耗时、分配总量、堆峰值，用数据指导调优",
                "正确性::新对象在下一次分配前必须可达（先 push 到栈上）",
                "压力测试::-DGC_STRESS 让每次分配都触发完整 GC，再跑全部测试，结果必须完全一致"]},
            {"type": "code", "area": (6.7, 2.4, 5.3, 3.6), "size": 12, "lang": "text", "title": "gzg --stats", "src": F["stats_sample"]},
        ]},

        {"kicker": "09 · TESTING", "title": f"{F['test_count']} 个黑盒测试，一个命令跑完", "sub": "测试脚本和测试运行器 tests/run_tests.gzg 都用 GZG 编写",
         "blocks": [
            {"type": "code", "area": (0, 0, 5.6, 2.5), "size": 13, "title": "tests/errors/div_zero.gzg", "src": '''print("before")     # expect: before
let x = 10 // 0
# expect-error: 整除的除数不能为 0'''},
            {"type": "bullets", "area": (0, 2.8, 5.6, 3.2), "size": 13, "items": [
                "比对::# expect 逐行比对 stdout；expect-error 校验 stderr 与退出码",
                "黑盒::测试由另一个 Agent 只看语法文档编写，发现 12 处实现与文档不一致，修复 11 处",
                "质量::GC 压力模式双跑；gcov 行覆盖率 " + F["coverage"].split("（")[0]]},
            {"type": "bars", "area": (5.9, 0, 6.1, 4.9), "title": "各分类测试数", "unit": "个",
             "items": [c for c in F["test_categories"] if c[0] not in ("smoke",)][:14]},
            {"type": "pills", "area": (5.9, 5.15, 6.1, 0.85), "items": F["test_summary"]},
        ]},

        {"kicker": "10 · PERFORMANCE", "title": "GZG vs CPython", "sub": "9 组同功能脚本，输出逐字校验一致；每项 {} 次取中位数".format(B["meta"]["runs"]),
         "blocks": [
            {"type": "chart", "area": (0, 0, 7.6, 6)},
            {"type": "table", "area": (7.9, 0, 4.1, 6), "size": 12, "widths": [1.3, 1.0, 1.0, 0.9], "align": "right", "rows":
                [["用例", "GZG", "Python", "倍数"]] +
                [[r["name"], f"{r['gzg']:.3f}s", f"{r['python']:.3f}s", f"{r['speedup']:.2f}×"] for r in B["results"]] +
                [["几何平均", "", "", f"{B['meta']['geomean']:.2f}×"]]},
        ]},
    ]
