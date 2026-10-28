"""生成《GZG 语言介绍》PPT：  .venv/Scripts/python docs/ppt/build_ppt.py
数据来源：bench/results/results.json（性能）、docs/ppt/facts.json（测试/应用等统计）。"""
import json
from pathlib import Path

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
from pptx.oxml.ns import qn
from pptx.util import Inches, Pt, Emu

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "GZG语言介绍.pptx"
FACTS = json.loads((ROOT / "docs" / "ppt" / "facts.json").read_text(encoding="utf-8"))
BENCH = json.loads((ROOT / "bench" / "results" / "results.json").read_text(encoding="utf-8"))

ACCENT = RGBColor(0x1F, 0x4E, 0x8C)
ACCENT_LIGHT = RGBColor(0xE8, 0xEE, 0xF6)
ORANGE = RGBColor(0xE8, 0xA3, 0x3D)
DARK = RGBColor(0x2B, 0x2B, 0x2B)
GRAY = RGBColor(0x6B, 0x6B, 0x6B)
CODE_BG = RGBColor(0xF5, 0xF6, 0xF8)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)
FONT = "Microsoft YaHei"
MONO = "Consolas"

prs = Presentation()
prs.slide_width = Inches(13.333)
prs.slide_height = Inches(7.5)
BLANK = prs.slide_layouts[6]
W, H = prs.slide_width, prs.slide_height


def set_font(run, size, color=DARK, bold=False, mono=False):
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.color.rgb = color
    name = MONO if mono else FONT
    run.font.name = name
    rpr = run._r.get_or_add_rPr()
    for tag in ("a:latin", "a:ea", "a:cs"):
        el = rpr.find(qn(tag))
        if el is None:
            el = rpr.makeelement(qn(tag), {})
            rpr.append(el)
        el.set("typeface", FONT if (mono and tag == "a:ea") else name)


def textbox(slide, x, y, w, h, lines, size=18, color=DARK, bold=False, align=PP_ALIGN.LEFT, mono=False,
            spacing=1.15, anchor=MSO_ANCHOR.TOP):
    """lines: 字符串列表；以 '• ' 开头的行作为项目符号；元组 (文本, 颜色, 加粗) 可单独设置样式"""
    tb = slide.shapes.add_textbox(x, y, w, h)
    tf = tb.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = anchor
    tf.margin_left = tf.margin_right = Inches(0.05)
    for i, line in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        p.line_spacing = spacing
        c, b, text = color, bold, line
        if isinstance(line, tuple):
            text, c, b = line
        if text.startswith("• "):
            r = p.add_run()
            r.text = "■  "
            set_font(r, size * 0.6, ACCENT)
            text = text[2:]
        r = p.add_run()
        r.text = text
        set_font(r, size, c, b, mono)
    return tb


def rect(slide, x, y, w, h, fill, line=None, shape=MSO_SHAPE.RECTANGLE):
    s = slide.shapes.add_shape(shape, x, y, w, h)
    s.fill.solid()
    s.fill.fore_color.rgb = fill
    if line is None:
        s.line.fill.background()
    else:
        s.line.color.rgb = line
        s.line.width = Pt(1)
    s.shadow.inherit = False
    return s


def code(slide, x, y, w, h, src, size=13):
    rect(slide, x, y, w, h, CODE_BG, RGBColor(0xDD, 0xDF, 0xE3))
    textbox(slide, x + Inches(0.15), y + Inches(0.1), w - Inches(0.3), h - Inches(0.2),
            src.strip("\n").split("\n"), size=size, mono=True, spacing=1.0)


def page(title, subtitle=None):
    s = prs.slides.add_slide(BLANK)
    rect(s, 0, 0, Inches(0.18), H, ACCENT)
    textbox(s, Inches(0.55), Inches(0.35), Inches(12), Inches(0.7), [title], size=30, color=ACCENT, bold=True)
    if subtitle:
        textbox(s, Inches(0.55), Inches(1.0), Inches(12), Inches(0.5), [subtitle], size=15, color=GRAY)
    n = len(prs.slides)
    textbox(s, W - Inches(1.2), H - Inches(0.5), Inches(1), Inches(0.35), [str(n)], size=11, color=GRAY,
            align=PP_ALIGN.RIGHT)
    textbox(s, Inches(0.55), H - Inches(0.5), Inches(6), Inches(0.35), ["GZG 语言 · 设计与实现"], size=11, color=GRAY)
    return s


def table(slide, x, y, w, rows, col_w, size=13, row_h=0.42, header=True):
    shape = slide.shapes.add_table(len(rows), len(rows[0]), x, y, w, Inches(row_h * len(rows)))
    t = shape.table
    for j, cw in enumerate(col_w):
        t.columns[j].width = Inches(cw)
    for i, row in enumerate(rows):
        t.rows[i].height = Inches(row_h)
        for j, val in enumerate(row):
            cell = t.cell(i, j)
            cell.margin_left = cell.margin_right = Inches(0.08)
            cell.vertical_anchor = MSO_ANCHOR.MIDDLE
            cell.fill.solid()
            if header and i == 0:
                cell.fill.fore_color.rgb = ACCENT
            else:
                cell.fill.fore_color.rgb = WHITE if i % 2 else ACCENT_LIGHT
            tf = cell.text_frame
            tf.paragraphs[0].text = ""
            r = tf.paragraphs[0].add_run()
            r.text = str(val)
            set_font(r, size, WHITE if (header and i == 0) else DARK, bold=(header and i == 0),
                     mono=str(val).startswith("`"))
            if str(val).startswith("`"):
                r.text = str(val).strip("`")
    return t


def card(slide, x, y, w, h, title, body, size=14):
    rect(slide, x, y, w, h, WHITE, RGBColor(0xD5, 0xDC, 0xE6))
    rect(slide, x, y, w, Inches(0.08), ACCENT)
    textbox(slide, x + Inches(0.2), y + Inches(0.2), w - Inches(0.4), Inches(0.5), [title], size=17, color=ACCENT,
            bold=True)
    textbox(slide, x + Inches(0.2), y + Inches(0.75), w - Inches(0.4), h - Inches(0.9), body, size=size)


# ---------------- 1. 封面 ----------------
s = prs.slides.add_slide(BLANK)
rect(s, 0, 0, W, H, ACCENT)
rect(s, Inches(0.9), Inches(2.2), Inches(0.12), Inches(2.6), ORANGE)
textbox(s, Inches(1.3), Inches(2.0), Inches(11), Inches(1.4), ["GZG 语言"], size=60, color=WHITE, bold=True)
textbox(s, Inches(1.3), Inches(3.3), Inches(11), Inches(0.8), ["一门中英双语的解释型脚本语言：设计、实现、测试与 AI 辅助开发"],
        size=24, color=WHITE)
textbox(s, Inches(1.3), Inches(4.2), Inches(11), Inches(0.6), [FACTS["author_line"]], size=18,
        color=RGBColor(0xC8, 0xD6, 0xEA))
textbox(s, Inches(1.3), Inches(6.2), Inches(11), Inches(0.5),
        ["C99 字节码虚拟机  ·  {} 行解释器  ·  {} 个黑盒测试  ·  Agent 开发的 2048".format(
            FACTS["interp_lines"], FACTS["test_count"])], size=15, color=RGBColor(0xC8, 0xD6, 0xEA))

# ---------------- 2. 一页看懂 ----------------
s = page("一页看懂 GZG", "四项任务的交付物与关键数字")
cards = [
    ("语言与解释器", ["• 中英双语关键字、自动分号", "• 插值、match、管道、命名参数", "• 结构体、闭包、异常、模块",
                 "• C99 实现，{} 行，零依赖".format(FACTS["interp_lines"])]),
    ("自动化测试", ["• {} 个黑盒测试，{} 个分类".format(FACTS["test_count"], FACTS["test_dirs"]),
               "• 运行器本身用 GZG 编写", "• 一个命令：gzg tests/run_tests.gzg", "• GC 压力模式 + gcov 覆盖率"]),
    ("性能", ["• 9 组同功能 GZG / Python 脚本", "• 几何平均快 {:.2f} 倍".format(BENCH["meta"]["geomean"]),
            "• 8 胜 1 负，负项有根因分析", "• 输出逐字校验一致"]),
    ("文档与 AI 开发", ["• 语法规则（含 EBNF）+ 开发指南", "• Claude Code Skill + AGENTS.md",
                   "• 新 Agent 仅凭文档写出 2048", "• 开发记录反哺文档改进"]),
]
for i, (t, b) in enumerate(cards):
    card(s, Inches(0.55 + i * 3.1), Inches(1.7), Inches(2.9), Inches(4.6), t, b, size=15)

# ---------------- 3. 设计目标 ----------------
s = page("设计目标与取舍", "像 Python 一样好写，像 Rust/Go 一样严谨，对中文用户和 AI 友好")
table(s, Inches(0.55), Inches(1.7), Inches(12.2), [
    ["目标", "做法", "取舍"],
    ["好写", "换行即语句结束、动态类型、插值、Python 式真值与整除", "用 { } 分块而不是缩进：复制粘贴、AI 生成都不怕缩进错乱"],
    ["早报错", "变量必须先 let；结构体字段封闭；字典缺键报错", "比 Python/JS 严格，但拼写错误在第一次运行就暴露"],
    ["中文友好", "中英双语关键字；中文错误信息；全角符号检测", "中文关键字与标识符之间须有空格"],
    ["对 AI 友好", "“你是不是想写 xxx”建议；append→push 等跨语言对照", "额外维护一张对照表"],
    ["够快", "单遍编译成字节码；computed goto；局部变量用栈槽", "不做 AST 优化与 JIT，保持实现可讲清楚"],
], [1.6, 5.4, 5.2], size=14, row_h=0.62)

# ---------------- 4. 语法速览 ----------------
s = page("语法速览", "一段代码看完主要语法")
code(s, Inches(0.55), Inches(1.6), Inches(7.4), Inches(5.3), '''
struct Point {
    x, y = 0                      # 字段与默认值
    fn len() => sqrt(self.x ** 2 + self.y ** 2)
    fn to_str() => "({self.x}, {self.y})"
}
fn scale(p, k = 2) => Point(p.x * k, p.y * k)

let pts = [Point(3, 4), Point(y: 1, x: 2)]
for i, p in pts {
    print("{i}: {p} 长度 {fmt(p.len(), 2)}")
}
let big = pts.filter(fn(p) => p.len() > 3)
let total = pts |> (fn(a) => a.map(fn(p) => p.x).sum())

try {
    let m = {name: "GZG"}
    print(m.name, m["age"])
} catch e {
    print("出错：{e}")
}''', size=14)
textbox(s, Inches(8.3), Inches(1.7), Inches(4.6), Inches(5.2), [
    "• 变量 let / 常量 const，块作用域",
    "• fn 定义函数，=> 单表达式函数体",
    "• 默认参数每次调用重新求值",
    "• 命名参数 Point(y: 1, x: 2)",
    "• for 支持区间、数组、字符串、字典，可同时取下标",
    "• \"{表达式}\" 字符串插值，可嵌套",
    "• |> 管道：x |> f(a) 即 f(x, a)",
    "• try / catch / throw 可抛任意值",
    "• / 总是浮点除法，// 整除向下取整",
], size=16, spacing=1.35)

# ---------------- 5. 特色：双语与友好报错 ----------------
s = page("特色一：中英双语 + 会“纠错”的报错信息", "关键字两套写法完全等价；错误信息给出位置、原因和修改建议")
code(s, Inches(0.55), Inches(1.6), Inches(5.9), Inches(2.6), '''
结构 学生 {
    姓名, 分数 = 0
    函数 及格() => 自身.分数 >= 60
}
令 名单 = [学生("小红", 95), 学生("小明", 52)]
遍历 生 在 名单 {
    如果 非 生.及格() { 打印("{生.姓名} 需补考") }
}''', size=14)
code(s, Inches(6.8), Inches(1.6), Inches(6.0), Inches(2.6), '''
demo.gzg:2:11: 语法错误: 实参列表缺少 ')'（在 'b' 处）
     2 | print(a b)
       |         ^

运行错误: array 类型没有方法 'append'，
          你是不是想写 'push'？
调用栈（最近的调用在最后）:
  demo.gzg:7  <主程序>
  demo.gzg:3  add_item''', size=13)
textbox(s, Inches(0.55), Inches(4.5), Inches(12.2), Inches(2.4), [
    "• 25 个关键字各有中文写法（如果/否则/遍历/函数/结构/匹配/尝试/捕获…），常用内建函数有中文别名（打印、输入、长度）",
    "• 拼写建议：未定义的变量 / 字段 / 方法 / 参数名，用编辑距离在可见名字里找最接近的一个",
    "• 跨语言对照：append→push、startswith→starts_with、None→nil、this→self 等 30 余条，专门照顾 Python/JS 习惯（也包括 AI）",
    "• 全角符号检测：中文输入法打出的（，；：会被拒绝并提示改为半角，避免“看不见的错误”",
], size=16, spacing=1.4)

# ---------------- 6. 特色：match / 管道 / 参数 ----------------
s = page("特色二：模式匹配、管道、命名与默认参数")
code(s, Inches(0.55), Inches(1.3), Inches(6.1), Inches(3.0), '''
let level = match score {
    90..=100 => "A",          # 闭区间
    80..90   => "B",          # 左闭右开
    0, 1, 2  => "几乎零分",   # 多个值
    _ if score < 0 => "非法", # 守卫
    _ => "C",
}
match cmd {
    "q" => break
    n if n.is_digit() => jump(int(n))  # 绑定
    _ => print("未知命令")
}''', size=13)
code(s, Inches(6.9), Inches(1.3), Inches(5.9), Inches(3.0), '''
fn top(xs, n = 3) => xs.sorted(fn(x) => -x)[:n]
fn mean(xs) => xs.sum() / len(xs)

print(scores |> top |> mean)
print(scores |> top(5) |> mean)

fn connect(host, port = 80, timeout = 5) { ... }
connect("gzg.dev", timeout: 30)
Point(y: 2, x: 1)''', size=13)
textbox(s, Inches(0.55), Inches(4.6), Inches(12.2), Inches(2.3), [
    "• match 支持字面量、多值、区间（整数与字符串）、守卫、变量绑定、通配；既可作语句也可作表达式；自上而下、不贯穿",
    "• 管道 |> 让数据处理从左往右读：x |> f(a) 等价于 f(x, a)",
    "• 默认参数在每次调用时求值（没有 Python 的“可变默认值共享”陷阱），可以引用前面的参数",
    "• 命名参数同时用于函数和结构体构造，写错参数名会给出拼写建议",
], size=16, spacing=1.4)

# ---------------- 7. 架构 ----------------
s = page("解释器架构", "单遍编译 + 字节码虚拟机 + 标记清除 GC，C99 实现，无第三方依赖")
stages = [("源码", ".gzg  UTF-8"), ("词法分析", "lexer.c\n双语关键字\n插值 / 自动分号"),
          ("编译器", "compiler.c\nPratt 解析\n作用域 / 闭包"), ("字节码", "chunk.c\n76 条指令\n常量池 / 行号表"),
          ("虚拟机", "vm.c\ncomputed goto\n调用帧 / 异常")]
x = Inches(0.55)
for i, (t, d) in enumerate(stages):
    bx = rect(s, x, Inches(1.8), Inches(2.15), Inches(1.9), ACCENT if i in (2, 4) else ACCENT_LIGHT,
              shape=MSO_SHAPE.ROUNDED_RECTANGLE)
    col = WHITE if i in (2, 4) else ACCENT
    textbox(s, x, Inches(1.9), Inches(2.15), Inches(0.5), [t], size=19, color=col, bold=True, align=PP_ALIGN.CENTER)
    textbox(s, x, Inches(2.45), Inches(2.15), Inches(1.2), d.split("\n"), size=13, color=col, align=PP_ALIGN.CENTER)
    if i < len(stages) - 1:
        a = s.shapes.add_shape(MSO_SHAPE.RIGHT_ARROW, x + Inches(2.2), Inches(2.55), Inches(0.35), Inches(0.4))
        a.fill.solid()
        a.fill.fore_color.rgb = ORANGE
        a.line.fill.background()
    x += Inches(2.55)
for i, (t, d) in enumerate([("memory.c", "三色标记-清除 GC\n字符串驻留池为弱引用"), ("builtins.c", "约 60 个全局函数\n约 80 个内建方法"),
                            ("platform.c", "Windows 宽字符路径\nUTF-8 控制台 / 进程")]):
    card(s, Inches(0.55 + i * 4.15), Inches(4.2), Inches(3.95), Inches(1.6), t, d.split("\n"), size=14)
table(s, Inches(0.55), Inches(6.05), Inches(12.2), [
    ["lexer", "compiler", "vm", "builtins", "object/table/value", "memory", "platform", "合计"],
    FACTS["line_counts"],
], [1.2, 1.4, 1.2, 1.4, 2.4, 1.3, 1.4, 1.9], size=12, row_h=0.36)

# ---------------- 8. 编译器 ----------------
s = page("编译器：一遍扫描直接生成字节码", "不构建 AST；变量在编译期解析为栈槽 / upvalue / 全局下标，运行时不按名字查找")
code(s, Inches(0.55), Inches(1.6), Inches(4.6), Inches(2.0), '''
fn fib(n) {
    if n < 2 { return n }
    return fib(n - 1) + fib(n - 2)
}''', size=15)
code(s, Inches(5.4), Inches(1.6), Inches(7.4), Inches(5.3), '''
gzg --dis fib.gzg
== fib ==
0000    2 GET_LOCAL            1        ← n 在栈槽 1
0002    | CONSTANT             0 '2'
0005    | LESS
0006    | POP_JUMP_IF_FALSE    6 -> 12
0009    | GET_LOCAL            1
0011    | RETURN
0012    3 GET_GLOBAL          61 (fib)  ← 全局下标
0015    | GET_LOCAL            1
0017    | CONSTANT             1 '1'
0020    | SUB
0021    | CALL                 1
0023    | GET_GLOBAL          61 (fib)
  ...
0034    | ADD
0035    | RETURN''', size=13)
textbox(s, Inches(0.55), Inches(3.9), Inches(4.6), Inches(3.0), [
    "• Pratt 解析：18 级优先级，** 右结合且高于一元负号",
    "• 闭包：被捕获的局部变量离开作用域时 CLOSE_UPVALUE 搬到堆上",
    "• for i in a..b 编译成 FOR_PREP / FOR_RANGE / FOR_STEP 三条专用指令",
    "• match 每个模式 DUP 后比较，分支体执行前弹出 subject",
], size=15, spacing=1.35)

# ---------------- 9. 虚拟机 ----------------
s = page("虚拟机：值表示、调用约定与异常")
card(s, Inches(0.55), Inches(1.3), Inches(4.0), Inches(2.6), "16 字节带标签值",
     ["• nil / bool / int64 / double / 对象指针", "• 整数与浮点分开：7 // 2 精确为 3", "• 11 种堆对象：字符串全部驻留，相等比较即比指针"], size=14)
card(s, Inches(4.7), Inches(1.3), Inches(4.0), Inches(2.6), "零拷贝调用",
     ["• 实参已在栈上，新帧 slots 直接指向它们", "• 闭包 + 参数个数匹配走内联快速路径", "• obj.m() 编译为 INVOKE，不创建绑定方法对象"], size=14)
card(s, Inches(8.85), Inches(1.3), Inches(4.0), Inches(2.6), "computed goto 分派",
     ["• dispatch[256] 标签地址表", "• 每条指令末尾直接 goto 下一条", "• 比 switch 少一次边界检查，分支预测更好"], size=14)
card(s, Inches(0.55), Inches(4.15), Inches(6.1), Inches(2.75), "异常：处理器栈",
     ["• TRY 登记 (调用深度, 栈顶, catch 地址)，END_TRY 注销", "• throw 时关闭将丢弃的 upvalue，恢复栈与帧，压入异常值",
      "• map/sort 等原生函数回调 GZG 函数时递归进入 run()；异常属于外层时先退出内层，因此能穿过任意层回调",
      "• return / break 跳出 try 时编译器补齐 END_TRY"], size=14)
card(s, Inches(6.8), Inches(4.15), Inches(6.05), Inches(2.75), "有序字典（同 CPython 3.7+）",
     ["• 条目数组按插入顺序存放 + 开放寻址索引数组", "• 删除留墓碑，条目区满且墓碑多时原地压缩",
      "• 遍历顺序 = 插入顺序，print 结果可预测，便于测试"], size=14)

# ---------------- 10. GC ----------------
s = page("内存管理：三色标记-清除垃圾回收")
steps = ["标记根\n值栈 / 调用帧 / upvalue\n全局表 / 编译器 / 临时根", "追踪\n灰栈逐个出栈\n叶子对象不入灰栈",
         "清理驻留池\n字符串池是弱引用\n删掉未标记的串", "清扫\n遍历对象链表\n释放未标记对象"]
for i, t in enumerate(steps):
    x = Inches(0.55 + i * 3.1)
    rect(s, x, Inches(1.4), Inches(2.85), Inches(1.75), ACCENT_LIGHT, shape=MSO_SHAPE.ROUNDED_RECTANGLE)
    lines = t.split("\n")
    textbox(s, x, Inches(1.5), Inches(2.85), Inches(0.5), [lines[0]], size=18, color=ACCENT, bold=True, align=PP_ALIGN.CENTER)
    textbox(s, x, Inches(2.05), Inches(2.85), Inches(1.0), lines[1:], size=13, align=PP_ALIGN.CENTER)
textbox(s, Inches(0.55), Inches(3.45), Inches(6.2), Inches(3.4), [
    ("触发与调优", ACCENT, True),
    "• 已分配字节数超过阈值时回收，阈值 = 存活量 × 2（最小 8MB）",
    "• --stats 输出 GC 次数、耗时、分配总量、堆峰值，用数据指导调优",
    ("正确性保证", ACCENT, True),
    "• 规则：新对象在下一次分配前必须可达（先 push 到栈上）",
    "• make stress：-DGC_STRESS 编译，每次分配都触发完整 GC，再跑全部测试，输出必须完全一致",
], size=15, spacing=1.35)
code(s, Inches(7.0), Inches(3.5), Inches(5.85), Inches(3.3), FACTS["stats_sample"], size=12)

# ---------------- 11. 测试 ----------------
s = page("自动化测试：{} 个黑盒测试，一个命令全部运行".format(FACTS["test_count"]),
         "测试脚本用 GZG 编写；运行器 tests/run_tests.gzg 本身也是 GZG 程序")
code(s, Inches(0.55), Inches(1.6), Inches(5.6), Inches(2.5), '''
# tests/errors/div_zero.gzg
print("before")          # expect: before
let x = 10 // 0
# expect-error: 整除的除数不能为 0

# tests/io/read_input.gzg  (+ read_input.in)
print(int(input()) * 2)  # expect: 84''', size=13)
textbox(s, Inches(0.55), Inches(4.35), Inches(5.6), Inches(2.6), [
    "• # expect: 逐行比对 stdout；expect-error / expect-compile-error 校验 stderr 与退出码",
    "• 同名 .in 文件作为标准输入；_ 开头的文件是被 import 的模块",
    "• 测试按特性分目录，黑盒编写：只依据语法文档，不看解释器源码",
    "• 与文档不符的行为记录为 bug 并修复，而不是改测试迁就实现",
], size=14, spacing=1.35)
rows = [["分类", "数量", "分类", "数量"]]
cats = FACTS["test_categories"]
half = (len(cats) + 1) // 2
for i in range(half):
    a = cats[i]
    b = cats[i + half] if i + half < len(cats) else ["", ""]
    rows.append([a[0], a[1], b[0], b[1]])
table(s, Inches(6.5), Inches(1.6), Inches(6.3), rows, [2.0, 1.15, 2.0, 1.15], size=12, row_h=0.33)
textbox(s, Inches(6.5), Inches(1.6 + 0.33 * len(rows) + 0.15), Inches(6.3), Inches(1.2), FACTS["test_summary"], size=13,
        color=ACCENT, bold=True)

# ---------------- 12. 性能 ----------------
s = page("性能测试：GZG vs CPython {}".format(BENCH["meta"]["python"].replace("Python ", "")),
         "9 组功能相同的脚本，输出逐字校验一致；每项运行 {} 次取中位数（含进程启动）".format(BENCH["meta"]["runs"]))
s.shapes.add_picture(str(ROOT / "bench" / "results" / "chart.png"), Inches(0.45), Inches(1.5), width=Inches(7.6))
rows = [["用例", "GZG", "Python", "倍数"]]
for r in BENCH["results"]:
    rows.append([r["name"], "{:.3f}s".format(r["gzg"]), "{:.3f}s".format(r["python"]), "{:.2f}x".format(r["speedup"])])
rows.append(["几何平均", "", "", "{:.2f}x".format(BENCH["meta"]["geomean"])])
table(s, Inches(8.3), Inches(1.5), Inches(4.6), rows, [1.4, 1.05, 1.1, 1.05], size=12, row_h=0.4)

# ---------------- 13. 性能分析 ----------------
s = page("性能分析：快在哪里，慢在哪里")
card(s, Inches(0.55), Inches(1.3), Inches(6.1), Inches(5.5), "为什么多数场景比 CPython 快", [
    "• 整数与浮点是值类型，不需要堆分配和引用计数（CPython 每个 int 都是对象）",
    "• 局部 / 全局变量在编译期解析成下标，运行时无字典查找",
    "• 数值 for 循环用专用指令，不创建迭代器对象：loop 用例快 {:.1f} 倍".format(
        next(r["speedup"] for r in BENCH["results"] if r["name"] == "loop")),
    "• computed goto 分派 + 调用快速路径：fib 用例快 {:.1f} 倍".format(
        next(r["speedup"] for r in BENCH["results"] if r["name"] == "fib")),
    "• 结构体字段是定长数组，方法调用 INVOKE 不创建绑定对象",
], size=15)
card(s, Inches(6.8), Inches(1.3), Inches(6.05), Inches(5.5), "strings 用例为什么慢（根因与取舍）", FACTS["strings_analysis"], size=14)

# ---------------- 14. AI 文档 ----------------
s = page("给 AI 的开发文档：让编程 Agent 写对一门没见过的语言")
table(s, Inches(0.55), Inches(1.3), Inches(7.2), [
    ["文件", "作用"],
    ["`.claude/skills/gzg/SKILL.md`", "Claude Code 技能：语法速览 + 15 条与 Python/JS 的差异"],
    ["`.claude/skills/gzg/reference.md`", "全部内建函数与方法（签名、返回值、边界行为）"],
    ["`.claude/skills/gzg/pitfalls.md`", "报错信息 → 原因 → 改法 对照表"],
    ["`AGENTS.md` / `CLAUDE.md`", "通用 Agent 入口：构建、测试、目录、修改规范"],
    ["`docs/GZG语法规则.md`", "完整规范与 EBNF，供深入查阅"],
], [3.2, 4.0], size=13, row_h=0.55)
textbox(s, Inches(8.0), Inches(1.3), Inches(4.9), Inches(5.6), [
    ("写法上的原则", ACCENT, True),
    "• 先给能直接照抄的代码，再给规则",
    "• 专门列出“与你熟悉的语言不同”的地方，因为 Agent 的错误几乎都来自迁移习惯",
    "• 报错信息本身也是文档：解释器会提示 append→push、None→nil",
    "• 鼓励用 gzg -e 实测，而不是猜",
    ("闭环验证", ACCENT, True),
    "• 让一个全新的 Agent 只读这三份文档开发 2048，记录每一次报错",
    "• 根据开发记录修订文档（见下一页）",
], size=15, spacing=1.35)

# ---------------- 15. Agent 开发 2048 ----------------
s = page("Agent 开发的应用：终端 2048 + Expectimax AI", FACTS["app_subtitle"])
img = ROOT / "docs" / "ppt" / "game2048.png"
if img.exists():
    s.shapes.add_picture(str(img), Inches(0.55), Inches(1.6), height=Inches(5.2))
else:
    code(s, Inches(0.55), Inches(1.6), Inches(5.2), Inches(5.2), FACTS["app_screen"], size=13)
textbox(s, Inches(6.2), Inches(1.6), Inches(6.7), Inches(5.3), FACTS["app_points"], size=15, spacing=1.35)

# ---------------- 16. 开发记录与反馈 ----------------
s = page("开发记录：Agent 踩了哪些坑，文档因此改了什么")
table(s, Inches(0.55), Inches(1.3), Inches(12.2), [["Agent 遇到的问题", "原因", "改进"]] + FACTS["devlog_rows"],
      [4.4, 3.8, 4.0], size=13, row_h=0.6)
textbox(s, Inches(0.55), Inches(1.3 + 0.6 * (len(FACTS["devlog_rows"]) + 1) + 0.2), Inches(12.2), Inches(1.0),
        [FACTS["devlog_summary"]], size=15, color=ACCENT, bold=True)

# ---------------- 17. 工程过程 ----------------
s = page("工程过程：Git 历史与分工")
code(s, Inches(0.55), Inches(1.3), Inches(7.6), Inches(5.6), FACTS["git_log"], size=12)
textbox(s, Inches(8.4), Inches(1.3), Inches(4.5), Inches(5.6), [
    ("开发方式", ACCENT, True),
    "• 先写头文件确定接口，再并行开发：核心（词法/编译/虚拟机）与内建函数库分头实现",
    "• 黑盒测试由独立的 Agent 只依据文档编写，发现的不一致回到解释器修复",
    "• 2048 由另一个只读过 Skill 文档的 Agent 开发，按阶段提交",
    ("质量手段", ACCENT, True),
    "• 全部测试 + GC 压力模式双跑",
    "• gcov 行覆盖率 {}".format(FACTS["coverage"]),
    "• 性能对比先校验输出一致",
], size=15, spacing=1.35)

# ---------------- 18. 总结 ----------------
s = prs.slides.add_slide(BLANK)
rect(s, 0, 0, W, H, ACCENT)
textbox(s, Inches(0.9), Inches(0.8), Inches(11), Inches(1), ["总结"], size=40, color=WHITE, bold=True)
textbox(s, Inches(0.9), Inches(2.0), Inches(11.5), Inches(4.5), [
    ("•  语言：双语关键字、插值、match、管道、命名参数、严格结构体，报错会“教你改”", WHITE, False),
    ("•  实现：{} 行 C99，单遍编译 + computed goto 虚拟机 + 标记清除 GC".format(FACTS["interp_lines"]), WHITE, False),
    ("•  测试：{} 个黑盒测试一键运行，GC 压力模式与覆盖率统计".format(FACTS["test_count"]), WHITE, False),
    ("•  性能：9 组用例几何平均比 CPython 快 {:.2f} 倍，弱项有根因分析".format(BENCH["meta"]["geomean"]), WHITE, False),
    ("•  AI：Skill 文档让新 Agent 独立写出 {} 行的 2048 + AI，开发记录反哺文档".format(FACTS["app_lines"]), WHITE, False),
], size=21, spacing=1.6)
textbox(s, Inches(0.9), Inches(6.3), Inches(11), Inches(0.6), ["谢谢！欢迎提问"], size=22, color=ORANGE, bold=True)

prs.save(OUT)
print("已生成", OUT, "共", len(prs.slides), "页")
