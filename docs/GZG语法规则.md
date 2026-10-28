# GZG 语言语法规则（v1.0）

GZG 取自作者姓名「郭志罡」的拼音首字母，是一门动态类型、解释执行的通用脚本语言。解释器用 C 实现：源码经词法分析、单遍编译成字节码，再由栈式虚拟机执行，内存由标记-清除垃圾回收管理。

本文是语言的规范性说明。入门教程见《GZG开发指南》，给编程 Agent 用的速查见 `docs/ai/`。

## 0. 语言特色一览

| 特色 | 示例 |
|---|---|
| 中英双语关键字，可混用 | `如果 x > 0 { 打印("正数") } 否则 { print("非正") }` |
| 换行即语句结束，无需分号 | `let a = 1` 换行即结束；下一行以 `.` 或 `\|>` 开头时自动续行 |
| 字符串插值支持任意表达式，可嵌套 | `"总价 {price * qty} 元"` |
| 管道运算符 | `data \|> filter_bad \|> sum` |
| `match` 模式匹配：值、多值、区间、守卫、绑定、通配 | `match s { 90..=100 => "A", 60, 61 => "及格", _ => "?" }` |
| 区间与步长 | `for i in 10..0 step -2 { }` |
| 默认参数与命名参数 | `greet("Bob", greeting: "Hi")` |
| 严格结构体：访问不存在的字段立即报错并给出拼写建议 | `p.nmae` → `Point 没有字段或方法 'nmae'，你是不是想写 'name'？` |
| 中文错误信息、源码定位与调用栈 | 见第 9 节 |
| 解构声明 | `let [q, r] = [17 // 5, 17 % 5]` |

## 1. 词法结构

### 1.1 源文件

源文件使用 UTF-8 编码，扩展名为 `.gzg`。文件开头的 UTF-8 BOM 和 `#!` 行会被忽略（`#!` 行本身就是注释）。

### 1.2 注释

```gzg
# 单行注释，直到行尾
#[ 块注释
   可以跨行，#[ 也可以嵌套 ]# ]#
```

未闭合的 `#[` 会把文件剩余部分全部当作注释。

### 1.3 标识符

标识符由字母、数字、下划线和任意非 ASCII 字符（如汉字）组成，不能以数字开头。区分大小写。

```gzg
let count = 1
let 学生名单 = []
let _tmp2 = nil
```

> 中文关键字与标识符之间必须用空格或符号隔开：`如果 x` 是关键字加变量，`如果x` 是一个名为 `如果x` 的标识符。

单独的 `_` 在 `match` 中是通配符，在其他地方是普通标识符。

数字中的 `_` 只用于分隔，可以出现在任意数字之间或末尾（`1_000`、`1__0` 均合法，后者不推荐）。

### 1.4 关键字

每个关键字都有中英两种写法，完全等价，可在同一文件中混用。

| 英文 | 中文 | 含义 | 英文 | 中文 | 含义 |
|---|---|---|---|---|---|
| `let` | `令` | 声明变量 | `const` | `常量` | 声明常量 |
| `fn` | `函数` | 函数 | `return` | `返回` | 返回 |
| `if` | `如果` | 条件 | `elif` | `否则如果` | 否则如果（也可写 `else if`） |
| `else` | `否则` | 否则 | `while` | `当` | 当型循环 |
| `for` | `遍历` | 遍历循环 | `in` | `在` | 成员/遍历 |
| `break` | `跳出` | 跳出循环 | `continue` | `继续` | 下一轮 |
| `struct` | `结构` | 结构体 | `self` | `自身` | 方法中的接收者 |
| `match` | `匹配` | 模式匹配 | `try` | `尝试` | 异常保护块 |
| `catch` | `捕获` | 捕获异常 | `throw` | `抛出` | 抛出异常 |
| `import` | `导入` | 导入文件 | `and` | `且` | 逻辑与 |
| `or` | `或` | 逻辑或 | `not` | `非` | 逻辑非 |
| `true` | `真` | 真 | `false` | `假` | 假 |
| `nil` | `空` | 空值 | | | |

`step`（`步长`）是上下文关键字，只在 `for ... in a..b step s` 中有特殊含义，其他地方可以用作变量名。

### 1.5 字面量

```gzg
42  -7  1_000_000          # 十进制整数，_ 可作分隔
0xff  0b1010  0o17         # 十六进制、二进制、八进制
3.14  1e-9  2.5E3          # 浮点数（必须有小数点或指数）
true  false  nil
"双引号字符串，支持转义 \n 和插值 {expr}"
'单引号是原始字符串：不转义 \n，也不插值 {x}'
```

双引号字符串的转义序列：

| 序列 | 含义 | 序列 | 含义 |
|---|---|---|---|
| `\n` | 换行 | `\t` | 制表符 |
| `\r` | 回车 | `\0` | 空字符 |
| `\\` | 反斜杠 | `\"` `\'` | 引号 |
| `\{` `\}` | 字面花括号 | `\e` | ESC（ANSI 颜色） |
| `\xHH` | 一个字节 | `\uHHHH` / `\u{H...}` | Unicode 码点 |

字符串可以跨行书写，换行符会被保留；行尾的 `\` 表示续行，不保留换行。

### 1.6 字符串插值

`{` 和 `}` 之间可以写任意表达式，结果用 `str()` 规则转为字符串，插值可以嵌套：

```gzg
let name = "世界"
print("你好，{name}！1 + 1 = {1 + 1}")
print("{"内层 {name}"}")          # 嵌套插值
print("花括号本身要转义：\{ \}")
```

插值内部是完整的表达式，所以 `"{{a: 1}}"` 是“插入一个字典字面量”；想输出字面的 `{` 必须写 `\{`。

### 1.7 换行与语句结束

GZG 不需要分号。词法分析器按下面的规则决定换行是否结束一条语句：

1. 在 `( )` 或 `[ ]` 内部，换行被忽略，因此参数列表和数组可以随意折行。
2. 在顶层或 `{ }` 内部，如果换行前的最后一个记号能够结束表达式（标识符、字面量、`)`、`]`、`}`、`return`、`break`、`continue`），换行就结束语句。
3. 如果下一个非空行以 `.标识符` 或 `|>` 开头，则视为续行，可以写链式调用：

```gzg
let r = [1, 2, 3, 4]
    .filter(fn(x) => x % 2 == 0)
    .map(fn(x) => x * 10)
```

4. 以二元运算符（如 `+`、`and`、`,`）结尾的行会自动延续到下一行。
5. 同一行写多条语句时用 `;` 分隔：`let a = 1; let b = 2`。
6. `}` 之后换行再写 `else` / `elif` / `catch` 是允许的。

### 1.8 运算符与分隔符

```
+  -  *  /  //  %  **        算术
== != <  <= >  >=            比较
and or not  &&  ||  !        逻辑
&  |  ^  ~  <<  >>           位运算（仅整数）
=  +=  -=  *=  /=  %=        赋值
..  ..=                      区间
|>                           管道
? :                          三元条件
=>                           函数体 / match 分支
.  ,  ;  :  ( )  [ ]  { }
```

半角符号以外的全角标点（如 `（`、`，`、`；`）会被拒绝，并提示改用半角，避免中文输入法造成的隐蔽错误。

## 2. 数据类型

GZG 是动态类型语言：变量没有类型，值有类型。`type(x)` 返回类型名字符串。

| 类型 | `type()` | 示例 | 说明 |
|---|---|---|---|
| 空值 | `"nil"` | `nil` | 表示“没有值” |
| 布尔 | `"bool"` | `true` `false` | |
| 整数 | `"int"` | `42` | 64 位有符号整数，溢出时按补码回绕 |
| 浮点 | `"float"` | `3.14` | IEEE 754 双精度 |
| 字符串 | `"string"` | `"abc"` | 不可变，UTF-8，长度与下标按字符（码点）计 |
| 数组 | `"array"` | `[1, "a", nil]` | 可变、有序、元素类型不限 |
| 字典 | `"map"` | `{name: "张三", "k": 1}` | 可变，**保持插入顺序**；键可以是 string/int/float/bool/nil |
| 区间 | `"range"` | `0..10` | 整数区间，惰性，不占用额外内存 |
| 函数 | `"function"` | `fn(x) => x` | 一等值，可作参数、返回值，支持闭包 |
| 结构体类型 | `"struct"` | `Point` | 由 `struct` 声明 |
| 结构体实例 | 结构体名，如 `"Point"` | `Point(1, 2)` | |

### 2.1 真值规则

在 `if`、`while`、`and`/`or`/`not`、`?:` 中，以下值视为假，其余都为真：

`false`、`nil`、`0`、`0.0`、`""`、`[]`、`{}`、空区间。

### 2.2 相等性

`==` 是**深比较**：

- `1 == 1.0` 为 `true`（数值按大小比较）；
- 数组按元素逐个比较，字典按键值对比较（与顺序无关）；
- 同一结构体类型的两个实例按字段逐个比较；
- 函数、结构体类型按身份比较（中文别名是单独注册的函数对象，所以 `打印 == print` 为 false；函数内声明的局部结构体每次调用都是一个新类型）；
- 不同类型（除 int/float）一律不等，`nil == false` 为 `false`。

### 2.3 值语义与引用语义

整数、浮点、布尔、nil、字符串、区间是值（字符串不可变，因此等同于值）。数组、字典、结构体实例是引用：赋值和传参不会复制，多个变量指向同一个对象。需要副本时用 `copy(x)` 或 `.copy()`（浅拷贝）。

## 3. 变量与作用域

```gzg
let x = 10           # 声明变量
let y                # 未初始化时为 nil
const PI2 = 6.28     # 常量，重新赋值是编译错误
x = 20               # 赋值（变量必须先声明）
let [a, b, c] = [1, 2, 3]   # 解构声明：按下标取出数组元素
```

- **块作用域**：每个 `{ }` 是一个新作用域，内层可以声明与外层同名的变量（遮蔽）。
- 遮蔽时，新变量在自己的初始化表达式中已经可见：块内写 `let y = y + 1` 会报“不能在变量自己的初始化表达式中读取它”，请换个名字。
- 同一作用域内重复 `let` 同名局部变量是编译错误；顶层（全局）允许重复声明，便于交互式环境中重新定义。
- 给未声明的变量赋值是运行错误：`变量 'cout' 未定义，请先用 let 声明`。
- 全局变量在整个程序（包括 `import` 的文件）中共享。
- 函数可以引用在它之后才定义的全局函数（调用时才查找）。

## 4. 表达式

### 4.1 运算符优先级（从低到高）

| 级别 | 运算符 | 结合性 | 说明 |
|---|---|---|---|
| 1 | `=` `+=` `-=` `*=` `/=` `%=` | 右 | 赋值，本身也是表达式，值为所赋的值 |
| 2 | `\|>` | 左 | 管道 |
| 3 | `? :` | 右 | 三元条件 |
| 4 | `or` `\|\|` | 左 | 短路，返回决定结果的那个操作数 |
| 5 | `and` `&&` | 左 | 短路，同上 |
| 6 | `not` | 前缀 | `not a == b` 等于 `not (a == b)` |
| 7 | `==` `!=` | 左 | 深比较 |
| 8 | `<` `<=` `>` `>=` `in` `not in` | 左 | |
| 9 | `..` `..=` | 左 | 区间 |
| 10 | `\|` | 左 | 按位或 |
| 11 | `^` | 左 | 按位异或 |
| 12 | `&` | 左 | 按位与 |
| 13 | `<<` `>>` | 左 | 移位，位数必须在 0~63 |
| 14 | `+` `-` | 左 | |
| 15 | `*` `/` `//` `%` | 左 | |
| 16 | `-` `+` `!` `~` | 前缀 | 一元运算 |
| 17 | `**` | 右 | 乘方：`-2 ** 2 == -4`，`2 ** 3 ** 2 == 512` |
| 18 | `()` `[]` `.` | 左 | 调用、下标/切片、成员访问 |

### 4.2 算术

| 表达式 | 结果 | 规则 |
|---|---|---|
| `7 + 2` | `9` | int 与 int 得 int；有 float 参与则得 float |
| `7 / 2` | `3.5` | `/` **总是**得到 float |
| `7 // 2`、`-7 // 2` | `3`、`-4` | 整除向下取整（与 Python 相同） |
| `-7 % 3` | `2` | 取模结果的符号与除数相同 |
| `2 ** 10`、`2 ** -1` | `1024`、`0.5` | 指数为非负整数时得 int |
| `"ab" + "cd"` | `"abcd"` | 字符串拼接；字符串加数字会报错，请用插值或 `str()` |
| `"ab" * 3`、`[0] * 3` | `"ababab"`、`[0, 0, 0]` | 重复 |
| `[1] + [2]` | `[1, 2]` | 数组拼接（返回新数组） |

除以 0、对 0 取模会抛出运行错误。整数运算溢出时按 64 位补码回绕，`9223372036854775807 + 1 == -9223372036854775808`。

### 4.3 比较

`<` `<=` `>` `>=` 可用于两个数字或两个字符串（按字节序，即 Unicode 码点序）。其他类型比较会报错。

### 4.4 逻辑运算

`and` / `or` 短路求值，并返回操作数本身（不一定是布尔值），因此可以写默认值：

```gzg
let name = input_name or "匿名"
let ok = user and user.active
```

`not` / `!` 总是返回布尔值。

### 4.5 成员测试 `in`

| 右侧类型 | `x in y` 的含义 |
|---|---|
| 数组 | 某个元素 `== x` |
| 字典 | `x` 是其中的键 |
| 字符串 | `x` 是子串 |
| 区间 | `x` 是区间内的整数 |

`x not in y` 等价于 `not (x in y)`。

### 4.6 区间

`a..b` 表示 `[a, b)`，`a..=b` 表示 `[a, b]`，`a`、`b` 必须是整数。（已知限制：区间内部按左闭右开存储，`..=INT_MAX` 这类端点恰为 64 位整数极值的闭区间会被当成空区间。）区间可以遍历、取长度 `len(r)`、下标 `r[i]`、用 `in` 测试（只对整数为真，`2.5 in 0..5` 为 false；而 match 的区间模式按大小比较，`2.5` 能匹配 `2..3`）、用 `array(r)` 转为数组。区间内部统一存为左闭右开，所以 `print(1..=3)` 显示为 `1..4`。带步长的区间用 `range(start, stop, step)` 函数（返回数组），或在 `for` 中写 `step`。

### 4.7 下标与切片

```gzg
let a = [10, 20, 30, 40]
a[0]        # 10
a[-1]       # 40，负数下标从末尾数
a[1:3]      # [20, 30]，左闭右开
a[:2]       # [10, 20]
a[2:]       # [30, 40]
a[1] = 99   # 修改元素
"你好世界"[1:3]   # "好世"，字符串按字符切片
m["key"]    # 字典取值，键不存在时报错；安全读取用 m.get("key", 默认值)
```

下标越界会报错；切片的边界越界时自动截断，不会报错。字符串不可修改，`s[0] = "x"` 会报错。

### 4.8 成员访问

- `obj.field`：结构体字段；`map.key` 等价于 `map["key"]`（键必须是合法标识符）。
- `obj.method(args)`：调用方法。对字典来说，内建方法名（`keys`、`get` 等）优先于同名的键。
- `obj.method`（不调用）得到绑定了接收者的函数，可以存起来稍后调用。

### 4.9 三元条件

```gzg
let sign = x > 0 ? "正" : x < 0 ? "负" : "零"
```

### 4.10 管道 `|>`

`x |> f` 等价于 `f(x)`；`x |> f(a, b)` 等价于 `f(x, a, b)`，即左侧的值作为第一个参数。管道右侧只能是函数名或括号括起来的表达式（之后可带参数列表）：

```gzg
fn double(x) => x * 2
fn add(x, y) => x + y
print(5 |> double |> add(1))          # 11
print([3, 1, 2] |> (fn(a) => a.sorted()))
```

匿名函数作为管道目标时要加括号，否则 `=>` 后面的表达式会把后续的 `|>` 也吞进去。

## 5. 语句

### 5.1 条件

```gzg
if score >= 90 {
    print("优秀")
} elif score >= 60 {       # 也可以写 else if
    print("及格")
} else {
    print("不及格")
}
```

条件不需要括号，代码块的 `{ }` 不能省略。

### 5.2 while 循环

```gzg
let i = 0
while i < 3 {
    i += 1
}
```

### 5.3 for 循环

```gzg
for i in 0..5 { }                 # 0 1 2 3 4
for i in 1..=5 { }                # 1 2 3 4 5
for i in 10..0 step -2 { }        # 10 8 6 4 2
for x in [1, 2, 3] { }            # 遍历数组元素
for i, x in ["a", "b"] { }        # 下标与元素
for ch in "你好" { }              # 遍历字符
for i, ch in "你好" { }           # 字符下标与字符
for k in {a: 1, b: 2} { }         # 遍历字典的键（按插入顺序）
for k, v in {a: 1, b: 2} { }      # 键与值
```

- 循环变量在每一轮都是新的绑定，闭包捕获的是当轮的值。
- 数值循环 `for i in a..b` 会被编译成专用指令，不创建区间对象，速度最快。
- `in` 后面的表达式比较复杂（含 `?:`、`|>` 等低优先级运算）时请加括号。
- 遍历过程中修改正在遍历的字典，结果未定义。

### 5.4 break 与 continue

`break` 跳出最内层循环，`continue` 进入下一轮。在 `try` 块中使用也是安全的。

### 5.5 match 模式匹配

```gzg
match value {
    0 => print("零")                    # 字面量（用 == 比较）
    1, 2, 3 => print("小")              # 多个值，逗号分隔
    4..10 => print("4 到 9")            # 区间：左闭右开
    10..=99 => print("两位数")          # 区间：闭区间（字符串区间也可以，如 "a"..="z"）
    n if n < 0 => print("负数 {n}")     # 绑定：把值绑定到 n，可配合守卫条件
    _ => print("其他")                  # 通配符
}
```

- 自上而下尝试，执行第一个匹配的分支，之后不会“贯穿”到下一个分支。
- 分支体可以是一条语句或 `{ }` 代码块；分支之间用换行或逗号分隔。
- 模式中的表达式在匹配时求值，可以是常量、变量、算术表达式：`(LIMIT) => ...`、`x + 1 => ...`。
- **单独的标识符是绑定**，不是比较（与 Rust 相同）。要和变量的值比较，请写成 `(LIMIT) =>` 或使用守卫 `_ if v == LIMIT =>`。
- 模式后可以加守卫 `if 条件`，守卫为假时继续尝试下一个分支。

`match` 也可以作为表达式使用，此时每个分支体必须是表达式，没有分支匹配时结果为 `nil`：

```gzg
let grade = match score {
    90..=100 => "A",
    80..90 => "B",
    _ => "C"
}
```

表达式形式的 `match` 不支持绑定模式，可以用 `_ if 条件 =>` 代替。

### 5.6 代码块

`{ ... }` 单独出现在语句位置时是一个代码块，引入新的作用域。注意：**在语句开头写 `{` 总是代码块，而不是字典字面量**，需要字典表达式语句时写成 `let m = {...}` 或加括号 `({a: 1})`。

## 6. 函数

### 6.1 定义与调用

```gzg
fn add(a, b) {
    return a + b
}
fn square(x) => x * x               # 单表达式函数体
let cube = fn(x) => x * x * x       # 匿名函数（lambda）
let hello = fn() {                   # 多行匿名函数
    print("hello")
}
print(add(1, 2), square(3), cube(2))
```

- 没有 `return` 或 `return` 后无值时返回 `nil`。
- 参数个数不符会报错：`函数 add 需要 2 个参数，实际传入 3 个`。
- 函数是一等值，可以存进变量、数组、字典，作为参数或返回值。
- 函数内部可以定义局部函数，局部函数可以递归。

### 6.2 默认参数

```gzg
fn greet(name, greeting = "你好", times = 1) {
    return ("{greeting}, {name}! " * times).trim()
}
greet("小明")              # "你好, 小明!"
greet("Bob", "Hi", 2)      # "Hi, Bob! Hi, Bob!"
```

默认值表达式**每次调用时**重新求值（不会像 Python 那样在多次调用间共享同一个可变对象），可以引用前面的参数：`fn f(a, b = a * 2)`。有默认值的参数必须放在没有默认值的参数之后。

### 6.3 命名参数

调用时可以用 `名字: 值` 按名字传参，命名参数必须放在位置参数之后：

```gzg
greet("Bob", times: 3)
greet(times: 2, name: "李雷")
```

传入不存在的参数名会报错并给出拼写建议。内建函数不支持命名参数。

### 6.4 闭包

函数会捕获外层作用域的变量（按引用捕获，修改对双方可见）：

```gzg
fn make_counter() {
    let n = 0
    return fn() { n += 1; return n }
}
let c = make_counter()
c(); c()
print(c())   # 3
```

## 7. 结构体

### 7.1 声明

```gzg
struct Point {
    x                      # 必填字段
    y = 0                  # 带默认值的字段
    tags = []              # [] 和 {} 默认值在每次创建实例时都是新的空容器

    fn len() => sqrt(self.x ** 2 + self.y ** 2)
    fn move(dx, dy) {
        self.x += dx
        self.y += dy
        return self        # 返回 self 可以链式调用
    }
    fn to_str() => "({self.x}, {self.y})"   # 可选：自定义 print / str / 插值时的显示
}
```

- 成员之间用换行或逗号分隔，字段和方法可以按任意顺序出现。
- 字段默认值只能是字面量：数字、字符串、`true`/`false`/`nil`、`[]`、`{}`。
- 方法内用 `self`（或 `自身`）访问接收者，`self` 不能被重新赋值。
- 结构体可以在函数内部声明（局部结构体）。

### 7.2 创建实例

```gzg
let a = Point(3, 4)            # 按字段顺序
let b = Point(y: 2, x: 1)      # 按字段名，顺序任意
let c = Point(5)               # y 使用默认值 0
let d = Point(1, tags: ["x"])  # 位置参数与命名参数混用
```

缺少必填字段、字段名写错、参数过多都会报错。

### 7.3 字段与方法

```gzg
print(a.x, a.len())
a.x = 10                 # 修改字段
a.move(1, 1).move(2, 2)  # 链式调用
let f = a.len            # 绑定方法，稍后调用 f()
a.z = 1                  # 错误：Point 没有字段 'z'（结构体的字段在定义时确定）
```

结构体是**封闭的**：不能在运行时添加新字段，这能尽早发现拼写错误。需要动态键时请用字典。

### 7.4 “静态方法”

在结构体类型本身上调用方法时，`self` 绑定的是结构体类型，可用于工厂函数：

```gzg
struct Vec { x, y
    fn zero() => self(0, 0)      # self 是 Vec 类型本身
}
let v = Vec.zero()
```

### 7.5 显示与比较

`print`、`str()`、字符串插值显示实例时，若定义了 `to_str()` 方法就调用它（必须返回字符串），否则显示为 `Point(x: 3, y: 4, tags: [])`。两个同类型实例用 `==` 比较时逐字段深比较。

## 8. 异常处理

```gzg
try {
    let data = read_file("config.txt")
    process(data)
} catch e {                  # 也可以写 catch (e)；不需要异常值时可以省略变量名
    print("出错了: {e}")
}

fn check_age(n) {
    if n < 0 { throw "年龄不能为负数: {n}" }
    return n
}
throw {code: 404, msg: "未找到"}   # 可以抛出任意值
```

- 运行错误（除零、下标越界、类型错误、未定义变量、断言失败等）都可以被 `catch` 捕获，捕获到的是错误信息字符串。
- 异常会穿过函数调用和内建函数的回调（如 `arr.map(f)` 中 `f` 抛出的异常）向外传播。
- 未被捕获的异常会终止程序，打印错误信息和调用栈，进程退出码为 70。
- `try` 块中可以使用 `return`、`break`、`continue`。

## 9. 错误报告

**编译错误**（退出码 65）给出文件、行、列，以及源码行和指示位置的 `^`：

```
demo.gzg:3:11: 语法错误: 实参列表缺少 ')'（在 'b' 处）
     3 | print(a b)
       |         ^
```

**运行错误**（退出码 70）给出错误信息和调用栈（最近的调用在最后）：

```
运行错误: 数组下标 3 越界（长度为 1）
调用栈（最近的调用在最后）:
  demo.gzg:5  <主程序>
  demo.gzg:2  f
  demo.gzg:4  g
```

找不到变量、字段、方法、参数名时，会根据编辑距离给出“你是不是想写 'xxx'？”的建议。

## 10. 模块

```gzg
import "lib/math_utils.gzg"     # 相对于当前文件所在目录
```

`import` 执行目标文件一次（重复导入不会再次执行），被导入文件定义的全局变量、函数、结构体在之后都可以直接使用。所有文件共享同一个全局命名空间。

## 11. 内建函数与方法

完整列表与示例见《GZG开发指南》第 5 章，这里只列出名字：

- **输入输出**：`print` `write` `input`
- **类型与转换**：`type` `int` `float` `str` `bool` `array` `len` `copy` `hex` `bin` `fmt`
- **数学**：`abs` `min` `max` `floor` `ceil` `round` `sqrt` `pow` `sin` `cos` `tan` `atan2` `log` `exp`，常量 `PI` `E` `INT_MAX` `INT_MIN`
- **随机**：`random` `randint` `seed` `choice` `shuffle`
- **时间**：`clock` `time` `sleep`
- **字符**：`chr` `ord`
- **序列**：`range`
- **测试**：`assert` `assert_eq`
- **系统与文件**：`exit` `args` `read_file` `write_file` `append_file` `file_exists` `list_dir` `run`
- **中文别名**：`打印` `输入` `长度` `类型` `整数` `字符串` `随机整数`
- **字符串方法**：`len upper lower trim trim_start trim_end split lines find count contains starts_with ends_with replace chars repeat slice reverse pad_left pad_right center is_digit is_alpha is_space`
- **数组方法**：`len push pop insert remove_at remove index_of contains count join reverse sort sorted map filter reduce each find find_index any all sum min max slice clear extend unique copy first last is_empty`
- **字典方法**：`len keys values items has get set remove clear copy is_empty merge`
- **区间方法**：`len to_array contains`

## 12. 文法（EBNF）

记号约定：`'x'` 为终结符，`NL` 为换行记号，`[ ]` 可选，`{ }` 重复零次或多次，`|` 选择。中文关键字与对应英文关键字是同一个终结符。

```ebnf
program        = { NL } { declaration { NL } } EOF ;
terminator     = NL | ';' | (* 前瞻到 '}' 或 EOF *) ;

declaration    = letDecl | constDecl | fnDecl | structDecl | statement ;
letDecl        = 'let' ( IDENT [ '=' expression ] | '[' IDENT { ',' IDENT } ']' '=' expression ) terminator ;
constDecl      = 'const' ( IDENT '=' expression | '[' IDENT { ',' IDENT } ']' '=' expression ) terminator ;
fnDecl         = 'fn' IDENT functionBody ;
structDecl     = 'struct' IDENT '{' { NL } { member ( ',' | NL | ';' ) { NL } } '}' ;
member         = IDENT [ '=' literal ] | 'fn' IDENT functionBody ;

functionBody   = '(' [ params ] ')' ( '=>' expression | block ) ;
params         = param { ',' param } [ ',' ] ;
param          = IDENT [ '=' expression ] ;

statement      = ifStmt | whileStmt | forStmt | matchStmt | tryStmt
               | 'return' [ expression ] terminator
               | 'break' terminator | 'continue' terminator
               | 'throw' expression terminator
               | 'import' STRING terminator
               | block
               | expression terminator ;
block          = '{' { NL } { declaration { NL } } '}' ;
ifStmt         = 'if' expression block { [ NL ] ( 'elif' | 'else' 'if' ) expression block }
                 [ [ NL ] 'else' block ] ;
whileStmt      = 'while' expression block ;
forStmt        = 'for' IDENT [ ',' IDENT ] 'in' bitor
                 [ ( '..' | '..=' ) bitor [ 'step' bitor ] ] block ;
tryStmt        = 'try' block [ NL ] 'catch' [ '(' ] [ IDENT ] [ ')' ] block ;
matchStmt      = 'match' expression '{' { NL } { arm { ',' | NL | ';' } } '}' ;
arm            = ( IDENT [ 'if' expression ]                       (* 绑定模式，仅语句形式 *)
                 | pattern { ',' pattern } [ 'if' expression ] ) '=>' ( block | statement ) ;
pattern        = '_' | bitor [ ( '..' | '..=' ) bitor ] ;

expression     = assignment ;
assignment     = [ target assignOp ] pipe ;
target         = IDENT | call '.' IDENT | call '[' expression ']' ;
assignOp       = '=' | '+=' | '-=' | '*=' | '/=' | '%=' ;
pipe           = ternary { '|>' primary [ '(' [ args ] ')' ] } ;
ternary        = or [ '?' ternary ':' ternary ] ;
or             = and { ( 'or' | '||' ) and } ;
and            = not { ( 'and' | '&&' ) not } ;
not            = 'not' not | equality ;
equality       = comparison { ( '==' | '!=' ) comparison } ;
comparison     = range { ( '<' | '<=' | '>' | '>=' | 'in' | 'not' 'in' ) range } ;
range          = bitor { ( '..' | '..=' ) bitor } ;
bitor          = bitxor { '|' bitxor } ;
bitxor         = bitand { '^' bitand } ;
bitand         = shift { '&' shift } ;
shift          = term { ( '<<' | '>>' ) term } ;
term           = factor { ( '+' | '-' ) factor } ;
factor         = unary { ( '*' | '/' | '//' | '%' ) unary } ;
unary          = ( '-' | '+' | '!' | '~' ) unary | power ;
power          = call [ '**' unary ] ;
call           = primary { '(' [ args ] ')' | '[' subscript ']' | '.' IDENT } ;
subscript      = expression | [ expression ] ':' [ expression ] ;
args           = arg { ',' arg } [ ',' ] ;
arg            = [ IDENT ':' ] expression ;          (* 命名参数必须在位置参数之后 *)

primary        = INT | FLOAT | STRING | RAWSTRING | interpolation
               | 'true' | 'false' | 'nil' | 'self' | IDENT
               | '(' expression ')'
               | '[' { NL } [ expression { ',' { NL } expression } [ ',' ] ] { NL } ']'
               | '{' { NL } [ entry { ',' { NL } entry } [ ',' ] ] { NL } '}'
               | 'fn' functionBody
               | 'match' expression '{' { NL } { pattern { ',' pattern } [ 'if' expression ]
                     '=>' expression { ',' | NL } } '}' ;
entry          = ( IDENT | or ) ':' expression ;          (* IDENT 键视为字符串；计算键写成 (expr) *)
interpolation  = INTERP expression { INTERP expression } STRING ;
literal        = [ '-' ] ( INT | FLOAT ) | STRING | RAWSTRING | 'true' | 'false' | 'nil' | '[' ']' | '{' '}' ;
```

## 13. 运行方式

```
gzg                      交互式环境（REPL），表达式的值会自动显示
gzg file.gzg [参数...]   运行脚本，args() 返回参数数组
gzg -e "print(1 + 1)"    运行一段代码
gzg --dis file.gzg       反汇编，查看编译出的字节码
gzg --tokens file.gzg    查看词法分析结果
```

退出码：0 正常结束；65 编译错误；70 运行错误；74 文件无法读取；`exit(n)` 时为 n。

