---
name: gzg
description: Write, run, test and debug programs in the GZG scripting language (.gzg files). Use whenever the task involves GZG code, the gzg interpreter, or files ending in .gzg — GZG looks like a mix of Python, Rust and Go but has its own rules, so read this before writing any GZG.
---

# GZG 语言编写指南（给编程 Agent）

GZG 是动态类型脚本语言：**语义接近 Python**（缩进无关、用 `{}` 分块），**语法接近 Rust/Go**。
写代码前通读本文件；需要查某个内建函数的细节时看 [reference.md](reference.md)；遇到报错先查 [pitfalls.md](pitfalls.md)。

## 运行与调试

```bash
build/gzg.exe app.gzg [参数...]     # Windows；Linux/macOS 为 build/gzg
build/gzg.exe -e 'print(1 + 1)'     # 快速试一段代码（不确定语义时先试，别猜）
build/gzg.exe --dis app.gzg         # 看字节码
```

退出码：0 正常 / 65 语法错误 / 70 运行错误。报错信息是中文，带 `文件:行:列`、源码行、调用栈，常常附带“你是不是想写 'xxx'？”——**照着建议改**。

## 30 秒语法速览

```gzg
# 注释；#[ 块注释 ]#
let name = "GZG"               # 变量（必须先 let 再赋值）
const MAX = 100                # 常量
let [q, r] = [17 // 5, 17 % 5] # 解构

fn add(a, b = 1) {             # 默认参数
    return a + b
}
fn square(x) => x * x          # 单表达式函数
let inc = fn(x) => x + 1       # 匿名函数
add(1, b: 2)                   # 命名参数

if x > 0 {
    print("正")
} elif x < 0 {                 # 也可写 else if
    print("负")
} else {
    print("零")
}

while i < 10 { i += 1 }
for i in 0..10 { }             # 0..9；0..=10 含 10；10..0 step -1 倒序
for x in arr { }               # 元素
for i, x in arr { }            # 下标, 元素
for k, v in map { }            # 键, 值（插入顺序）

let grade = match score {      # match 表达式
    90..=100 => "A",
    80..90 => "B",
    0, 1, 2 => "几乎零分",
    _ => "C",
}

struct Point {
    x, y = 0                   # y 有默认值
    fn dist() => sqrt(self.x ** 2 + self.y ** 2)
    fn to_str() => "({self.x}, {self.y})"   # 自定义显示
}
let p = Point(3, 4)            # 或 Point(x: 3, y: 4)
p.x += 1

try {
    risky()
} catch e {
    print("出错: {e}")
}
throw "自定义错误"

print("插值: {name} 有 {len(name)} 个字符")
print(data |> sorted_desc |> take(3))   # 管道：x |> f(a) 等于 f(x, a)
```

## 必须牢记的 15 条规则（与 Python/JS 的差异）

1. **代码块用 `{ }`，没有冒号，缩进无意义**。`if`/`while`/`for`/`match` 的条件不加括号。
2. **语句以换行结束**，不写分号；一行多条语句用 `;`。二元运算符放在行尾可以续行；链式调用可以以 `.method()` 开头另起一行。
3. **变量必须先 `let` 声明**；给未声明的变量赋值是运行错误。函数内的 `let` 是局部变量，不需要 `global`。
4. **`/` 永远得到浮点数**：`7 / 2 == 3.5`。整除用 `//`，取模 `%`（与 Python 一致，向下取整）。
5. **字符串不能和数字直接相加**：`"分数" + 90` 报错。用插值 `"分数{score}"` 或 `"分数" + str(score)`。
6. **数组追加用 `push`**，不是 `append`；长度用 `len(x)` 或 `x.len()`。没有列表推导式，用 `.map(fn(x) => ...)` / `.filter(...)`。
7. **字典取不存在的键会报错**（不返回 nil）。安全读取用 `m.get(k, 默认值)`，判断用 `m.has(k)` 或 `k in m`。字典键可以用 `m.key` 或 `m["key"]` 访问。
8. **结构体字段在定义时固定**：给实例设置未声明的字段会报错；需要动态键就用字典。结构体没有继承。
9. **语句开头的 `{` 是代码块，不是字典**。字典字面量要出现在表达式里：`let m = {a: 1}`、`return {a: 1}` 都没问题。
10. **`match` 里单独的标识符是“绑定”而不是比较**：`match x { limit => ... }` 会匹配一切。和变量比较写 `(limit) =>` 或 `_ if x == limit =>`。表达式形式的 match（`let r = match ...`）分支必须是表达式。
11. **真值规则同 Python**：`0`、`0.0`、`""`、`[]`、`{}`、`nil`、`false` 为假。`and`/`or` 返回操作数本身：`let n = name or "匿名"`。
12. **`==` 是深比较**（数组、字典、同类型结构体实例按内容比较）。没有 `is`。
13. **没有 tuple、没有 class、没有 `None`/`True`/`False`**，用数组、`struct`、`nil`/`true`/`false`。
14. **整数是 64 位**，溢出会回绕（不是大整数）。
15. 中文关键字（`如果`、`否则`、`遍历`、`函数` …）与英文完全等价；**写给人看的程序也优先用英文关键字**，变量名、字符串可以用中文。

## 常用内建（完整表见 reference.md）

| 用途 | 写法 |
|---|---|
| 输出 | `print(a, b)` 空格分隔+换行；`write(s)` 不换行，直接拼接 |
| 输入 | `let line = input("提示: ")`，EOF 时返回 `nil` |
| 类型转换 | `int("42")` `float("1.5")` `str(x)` `bool(x)` `type(x)` → `"int"`/`"string"`/`"Point"`… |
| 数学 | `abs min max floor ceil round(x) round(x, 2) sqrt pow`，`PI`，`randint(1, 6)`，`random()`，`seed(n)` |
| 格式化 | `fmt(3.14159, 2)` → `"3.14"`；`"7".pad_left(3, "0")` → `"007"`；`s.pad_right(10)` 对齐表格 |
| 字符串 | `split(",") join trim upper lower replace(a, b) find contains starts_with ends_with chars lines slice(a, b) repeat` |
| 数组 | `push pop insert remove_at remove index_of contains sort sorted(key) reverse map filter reduce each find any all sum min max slice join unique copy first last` |
| 字典 | `keys values items has get(k, d) set remove copy merge` |
| 时间 | `clock()` 秒（计时）；`sleep(ms)`；`time()` 时间戳 |
| 文件 | `read_file(p)` `write_file(p, s)` `append_file(p, s)` `file_exists(p)` `list_dir(p)` |
| 测试 | `assert(cond, "说明")`，`assert_eq(实际, 期望)` |
| 其他 | `args()` 命令行参数，`exit(code)`，`copy(x)` 浅拷贝，`range(a, b, step)` 返回数组，`array(0..5)` |

`sort()` 原地排序并返回 `nil`；`sorted()` 返回新数组。`sort`/`sorted` 接受“键函数”：`people.sorted(fn(p) => p.age)`，降序用 `fn(p) => -p.age` 或排完 `.reverse()`。

## 写程序的推荐结构

```gzg
# 1. 常量与结构体
const SIZE = 4
struct Game {
    board = []
    score = 0
    fn reset() { ... }
}

# 2. 纯函数（便于用 assert_eq 测试）
fn parse_command(line) { ... }

# 3. main 函数，避免大量顶层代码
fn main() {
    let g = Game()
    g.reset()
    while true {
        let line = input("> ")
        if line == nil or line == "q" { break }
        ...
    }
}
main()
```

- 交互程序读入要处理 `input()` 返回 `nil`（输入结束）的情况，否则管道/自动化测试时会死循环。
- 需要可复现的随机数时先 `seed(固定值)`。
- 终端颜色/清屏用 ANSI 转义：`"\e[31m红\e[0m"`，清屏 `write("\e[2J\e[H")`。
- 大量字符串拼接时先 `push` 到数组再 `join("")`，比反复 `s += x` 快。

## 工作流程建议

1. 写一小段 → 用 `gzg -e` 或运行文件验证 → 再继续。不确定某个内建是否存在时，**先运行试一下**，不要假设它和 Python 一样。
2. 报错时读完整信息：`文件:行:列` + `^` 指出位置；运行错误看调用栈最后一行。
3. 给关键函数写 `assert_eq` 自测，放在 `tests/` 或单独的 `*_test.gzg` 文件里。
