"""PPT 与网页共用的幻灯片内容（第 1~9 页）。
每页：kicker（页眉小字）、title、sub、bg（cover/dark/end）、blocks。
每个 block 的 area = (列, 行, 列宽, 行高)，在 12 列 × 6 行的内容网格上定位；两种渲染器用同一套坐标。
条目里的 "标签::说明" 会把标签渲染成强调色。"""


def slides_a(F, B):
    sp = {r["name"]: r["speedup"] for r in B["results"]}
    geo = B["meta"]["geomean"]
    return [
        {"kind": "cover", "bg": "cover",
         "title": "GZG 语言", "en": "A bilingual scripting language, built from scratch",
         "sub": "设计 · 实现 · 测试 · 性能 · AI 辅助开发", "author": F["author_line"]},

        {"kicker": "OVERVIEW", "title": "一页看懂", "sub": "作业要求的六项交付，用数字说话", "blocks": [
            {"type": "stat", "area": (0, 0, 3, 4), "value": F["interp_lines"], "unit": "行",
             "label": "C99 解释器", "note": "单遍编译 + 字节码虚拟机 + 标记清除 GC，零依赖"},
            {"type": "stat", "area": (3, 0, 3, 4), "value": str(F["test_count"]), "unit": "个",
             "label": "黑盒测试", "note": "全部用 GZG 编写，一个命令运行；行覆盖率 94%"},
            {"type": "stat", "area": (6, 0, 3, 4), "value": f"{geo:.2f}", "unit": "×",
             "label": "比 CPython 快", "note": "9 组同功能程序的几何平均，计时驱动也用 GZG 写"},
            {"type": "stat", "area": (9, 0, 3, 4), "value": str(F["app_lines"]), "unit": "行",
             "label": "Agent 写的 2048", "note": "只读 Skill 文档开发，AI 6 局全部达成 2048"},
            {"type": "bullets", "area": (0, 4, 12, 2), "size": 15, "items": [
                "交付物::语法规则 · 解释器源码 · 测试集 · 性能报告 · 开发指南与 AI 文档 · 应用与开发记录 · Git 历史 · 本 PPT",
                "关键原则::被解释器执行的测试、性能程序和应用，全部使用 GZG 自己编写"]},
        ]},

        {"kicker": "01 · DESIGN", "title": "设计目标与取舍", "sub": "像 Python 一样好写，像 Rust/Go 一样严谨，对中文用户和 AI 友好",
         "blocks": [{"type": "table", "area": (0, 0, 12, 6), "size": 14, "widths": [1.3, 4.6, 4.4], "rows": [
             ["目标", "做法", "取舍"],
             ["好写", "换行即语句结束、动态类型、字符串插值、Python 式真值与整除", "用 { } 分块：复制粘贴、AI 生成都不怕缩进错乱"],
             ["早报错", "变量必须先 let；结构体字段封闭；字典缺键直接报错", "比 Python/JS 严格，但拼写错误第一次运行就暴露"],
             ["中文友好", "中英双语关键字、中文错误信息、全角符号检测", "中文关键字与标识符之间要空格"],
             ["对 AI 友好", "“你是不是想写 xxx”建议；append→push 等跨语言对照", "额外维护一张对照表"],
             ["够快", "单遍编译成字节码；computed goto；局部变量用栈槽", "不做 JIT，保持实现讲得清楚"]]}]},

        {"kicker": "02 · SYNTAX", "title": "语法速览", "sub": "一段代码看完主要语法", "blocks": [
            {"type": "code", "area": (0, 0, 7, 6), "size": 13, "title": "demo.gzg", "src": '''struct Point {
    x, y = 0          # 字段与默认值
    fn len() => sqrt(self.x ** 2 + self.y ** 2)
    fn to_str() => "({self.x}, {self.y})"
}
fn scale(p, k = 2) => Point(p.x * k, p.y * k)

let pts = [Point(3, 4), Point(y: 1, x: 2)]
for i, p in pts {
    print("{i}: {p} 长度 {fmt(p.len(), 2)}")
}
let far = pts.filter(fn(p) => p.len() > 3)

try {
    print({name: "GZG"}["age"])
} catch e {
    print("出错：{e}")
}'''},
            {"type": "bullets", "area": (7, 0, 5, 6), "size": 15, "items": [
                "变量::let 变量、const 常量，块作用域",
                "函数::fn 定义，=> 单表达式函数体",
                "参数::默认参数每次调用重新求值，支持命名参数",
                "循环::for 遍历区间、数组、字符串、字典",
                "插值::\"{表达式}\" 直接嵌入，可以嵌套",
                "异常::try / catch / throw，可抛任意值",
                "算术::/ 总是浮点除法，// 向下取整"]},
        ]},

        {"kicker": "03 · FEATURE", "title": "中英双语 + 会纠错的报错", "sub": "关键字两套写法完全等价；报错给出位置、原因和修改建议",
         "blocks": [
            {"type": "code", "area": (0, 0, 6, 3), "size": 13, "title": "中文关键字", "src": '''结构 学生 {
    姓名, 分数 = 0
    函数 及格() => 自身.分数 >= 60
}
令 名单 = [学生("小红", 95), 学生("小明", 52)]
遍历 生 在 名单 {
    如果 非 生.及格() { 打印("{生.姓名} 需补考") }
}'''},
            {"type": "code", "area": (6, 0, 6, 3), "size": 12, "lang": "text", "title": "报错示例", "src": '''demo.gzg:2:11: 语法错误: 实参列表缺少 ')'（在 'b' 处）
     2 | print(a b)
       |         ^
运行错误: array 类型没有方法 'append'，
          你是不是想写 'push'？
调用栈（最近的调用在最后）:
  demo.gzg:7  <主程序>
  demo.gzg:3  add_item'''},
            {"type": "bullets", "area": (0, 3, 12, 3), "size": 14, "items": [
                "双语::25 个关键字都有中文写法（如果 / 否则 / 遍历 / 函数 / 结构 / 匹配 …），常用内建函数有中文别名",
                "拼写建议::找不到变量、字段、方法、参数名时，用编辑距离找最接近的候选（相邻字母换位也算一次）",
                "跨语言对照::append→push、startswith→starts_with、None→nil、a if c else b→c ? a : b",
                "全角检测::中文输入法打出的（，；会被拒绝并提示改成半角，避免“看不见的错误”"]},
        ]},

        {"kicker": "04 · FEATURE", "title": "模式匹配、管道与命名参数", "blocks": [
            {"type": "code", "area": (0, 0, 6, 4), "size": 13, "title": "match", "src": '''let level = match score {
    90..=100 => "A",           # 闭区间
    80..90   => "B",           # 左闭右开
    0, 1, 2  => "几乎零分",    # 多个值
    _ if score < 0 => "非法",  # 守卫
    _ => "C",
}
match cmd {
    "q" => break
    n if n.is_digit() => go(int(n))   # 绑定
    _ => print("未知命令")
}'''},
            {"type": "code", "area": (6, 0, 6, 4), "size": 13, "title": "管道 / 参数", "src": '''fn top(xs, n = 3) => xs.sorted(fn(x) => -x)[:n]
fn mean(xs) => xs.sum() / len(xs)

print(scores |> top |> mean)
print(scores |> top(5) |> mean)

fn connect(host, port = 80, timeout = 5) { }
connect("gzg.dev", timeout: 30)'''},
            {"type": "bullets", "area": (0, 4, 12, 2), "size": 14, "items": [
                "match::字面量、多值、区间、守卫、绑定、通配；可作语句也可作表达式，自上而下、不贯穿",
                "管道::x |> f(a) 等价于 f(x, a)，数据处理从左往右读；默认参数没有 Python 的“可变默认值共享”陷阱"]},
        ]},
    ]
