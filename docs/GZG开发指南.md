# GZG 开发指南

面向第一次使用 GZG 的开发者：怎么装、怎么写、怎么调试、怎么组织一个稍大的程序。语言的精确定义见《GZG语法规则》，全部内建函数见 [reference.md](../.claude/skills/gzg/reference.md)。

## 1. 安装与运行

需要一个 C99 编译器（gcc / clang / MinGW）。

```bash
git clone <仓库地址> gzg-lang && cd gzg-lang
make                 # Windows 下用 mingw32-make，产物为 build/gzg.exe
build/gzg --version
```

| 命令 | 作用 |
|---|---|
| `gzg` | 进入交互式环境，表达式的值自动显示，`.exit` 退出 |
| `gzg hello.gzg 参数1 参数2` | 运行脚本，脚本里用 `args()` 取参数 |
| `gzg -e 'print(2 ** 10)'` | 运行一行代码 |
| `gzg --dis hello.gzg` | 打印编译出的字节码，理解代码如何执行 |
| `gzg --tokens hello.gzg` | 打印词法记号，排查换行/分号问题 |

建议把 `build/` 加入 `PATH`。VS Code 中把 `.gzg` 关联到 Rust 或 Go 的语法高亮即可获得不错的显示效果。

## 2. 第一个程序

```gzg
# hello.gzg
let name = input("你叫什么名字？") or "朋友"
print("你好，{name}！")
for i in 1..=3 {
    print("第 {i} 次问候")
}
```

GZG 没有 `main` 入口要求，文件从上到下执行。

## 3. 十分钟教程

### 3.1 变量与基本类型

```gzg
let age = 20            # int
let height = 1.75       # float
let name = "小明"       # string
let ok = true           # bool
let nothing = nil
const PASS = 60         # 常量

print(type(age), type(height), type(name))   # int float string
print(7 / 2, 7 // 2, 7 % 2, 2 ** 8)          # 3.5 3 1 256
```

注意 `/` 总是浮点除法，整除用 `//`。

### 3.2 字符串

```gzg
let s = "Hello, 世界"
print(len(s))                 # 9（按字符计）
print(s[0], s[-2:])           # H 世界
print(s.upper(), s.lower())
print(s.split(", "))          # ["Hello", "世界"]
print("-".repeat(20))
print("价格：{19.9 * 3} 元")  # 插值里可以写任意表达式
print("保留两位：{fmt(2 / 3, 2)}")
print('原样输出 {不插值} \n')
```

字符串和数字拼接要用插值或 `str()`：`"年龄" + str(age)`。

### 3.3 数组

```gzg
let nums = [5, 3, 8, 1]
nums.push(10)
print(nums[0], nums[-1], len(nums))
print(nums.sorted(), nums.sum(), nums.max())
let evens = nums.filter(fn(x) => x % 2 == 0)
let doubled = nums.map(fn(x) => x * 2)
print(evens, doubled, 8 in nums)
let grid = []
for r in 0..3 { grid.push([0] * 3) }   # 3x3 二维数组
grid[1][2] = 7
```

### 3.4 字典

```gzg
let scores = {"张三": 90, "李四": 75}
scores["王五"] = 88
print(scores.get("赵六", 0))            # 不存在时给默认值
for name, score in scores {
    print("{name.pad_right(4)} {score}")
}
let counts = {}
for ch in "hello" { counts[ch] = counts.get(ch, 0) + 1 }
print(counts)    # {"h": 1, "e": 1, "l": 2, "o": 1}
```

键是合法标识符时可以省略引号：`{name: "x", age: 3}`，并可用 `p.name` 读取。

### 3.5 控制流

```gzg
for i in 1..=15 {
    let out = match 0 {
        _ if i % 15 == 0 => "FizzBuzz",
        _ if i % 3 == 0 => "Fizz",
        _ if i % 5 == 0 => "Buzz",
        _ => str(i),
    }
    print(out)
}

let n = 27
let steps = 0
while n != 1 {
    n = n % 2 == 0 ? n // 2 : 3 * n + 1
    steps += 1
}
print("步数", steps)
```

### 3.6 函数与闭包

```gzg
fn area(w, h = w) => w * h        # h 默认等于 w
print(area(3), area(3, 4), area(h: 2, w: 5))

fn compose(f, g) => fn(x) => f(g(x))
let inc_then_double = compose(fn(x) => x * 2, fn(x) => x + 1)
print(inc_then_double(3))         # 8

fn memo_fib() {
    let cache = {}
    fn fib(n) {
        if n < 2 { return n }
        if cache.has(n) { return cache[n] }
        let v = fib(n - 1) + fib(n - 2)
        cache[n] = v
        return v
    }
    return fib
}
print(memo_fib()(80))
```

### 3.7 结构体

```gzg
struct Account {
    owner
    balance = 0
    history = []

    fn deposit(amount) {
        if amount <= 0 { throw "存款金额必须为正数" }
        self.balance += amount
        self.history.push("+{amount}")
        return self
    }
    fn to_str() => "{self.owner} 的账户：余额 {self.balance}"
}

let acc = Account("小红")
acc.deposit(100).deposit(50)
print(acc)              # 小红 的账户：余额 150
print(acc.history)      # ["+100", "+50"]
```

### 3.8 错误处理

```gzg
fn safe_int(s) {
    try {
        return int(s)
    } catch e {
        print("转换失败：{e}")
        return 0
    }
}
print(safe_int("42"), safe_int("abc"))
```

### 3.9 文件与模块

```gzg
write_file("note.txt", "第一行\n")
append_file("note.txt", "第二行\n")
for line in read_file("note.txt").lines() { print(line) }

import "lib/utils.gzg"     # 路径相对于当前文件
```

## 4. 调试技巧

**读懂报错。** 语法错误给出 `文件:行:列`，并用 `^` 指出位置；运行错误给出调用栈，最后一行是出错的位置：

```
运行错误: Account 没有字段或方法 'balanse'，你是不是想写 'balance'？
调用栈（最近的调用在最后）:
  bank.gzg:20  <主程序>
  bank.gzg:9   deposit
```

**打印中间值。** `print` 能显示任何值，包括嵌套的数组、字典和结构体实例：`print("调试", state)`。

**断言。** 在关键位置写 `assert(cond, "说明")`，条件不成立时立刻报错并显示调用栈。

**交互式试验。** 不确定某个写法的结果时，直接在 REPL 或 `gzg -e` 里试：

```
gzg> [3, 1, 2].sorted(fn(x) => -x)
[3, 2, 1]
gzg> "a,b,,c".split(",")
["a", "b", "", "c"]
```

**查看字节码。** `gzg --dis file.gzg` 可以看到每行代码被编译成了哪些指令，理解性能问题时很有用。

## 5. 组织较大的程序

1. **常量和结构体放在文件开头**，然后是函数，最后调用 `main()`。尽量少写顶层可执行代码。
2. **用结构体封装状态**，方法里通过 `self` 修改；返回 `self` 可以链式调用。
3. **把纯逻辑与输入输出分开**：例如游戏的“移动棋盘”函数只接收和返回数据，不调用 `print`/`input`，这样容易测试。
4. **拆分文件**：`import "board.gzg"`。被导入的文件共享全局命名空间，给公共函数加统一前缀（如 `board_`）可以避免重名。
5. **交互程序要能被自动化测试**：读入用 `input()` 并处理返回 `nil` 的情况；随机数用 `seed()` 固定；这样就能用 `gzg app.gzg < 输入.txt` 重放一局。

## 6. 编写测试

仓库的测试运行器是用 GZG 写的：`gzg tests/run_tests.gzg`。一个测试就是一个 `.gzg` 文件，用注释写期望：

```gzg
# tests/my/sum.gzg
print([1, 2, 3].sum())       # expect: 6
print("a" * 3)               # expect: aaa
let x = [1][5]
# expect-error: 数组下标 5 越界
```

- `# expect: 文本`：该程序的标准输出按顺序逐行与这些注释比对；
- `# expect-error: 片段`：期望运行错误（退出码 70），且 stderr 包含该片段；
- `# expect-compile-error: 片段`：期望语法错误（退出码 65）；
- 同名的 `.in` 文件会作为标准输入。

在自己的项目里也可以只用 `assert_eq` 写单元测试：

```gzg
import "../src/calc.gzg"
assert_eq(calc("1 + 2 * 3"), 7)
assert_eq(calc("(1 + 2) * 3"), 9)
print("calc 测试通过")
```

## 7. 性能建议

GZG 编译为字节码并由虚拟机执行，常见场景与 CPython 相当或更快（见《性能测试报告》）。写出快代码的几条经验：

- 数值循环用 `for i in a..b`，它被编译成专用指令，不会创建区间对象。
- 局部变量比全局变量快：把热点代码放进函数里。
- 大量拼接字符串时 `push` 到数组再 `join("")`。
- 需要频繁按键查找时用字典，而不是在数组里 `index_of`。
- 结构体字段访问比字典快，且能在写错字段名时立刻报错。

## 8. 常见问题

**为什么 `if x > 0 print(x)` 报错？** 代码块的花括号不能省略：`if x > 0 { print(x) }`。

**怎么写多行的长表达式？** 把运算符放在行尾，或者用括号括起来；括号内可以随意换行。

**怎么在一行写多条语句？** 用分号：`let a = 1; let b = 2`。

**怎么表示“没有返回值”？** 函数不写 `return` 或 `return` 后不跟值，结果是 `nil`。

**中文关键字和英文关键字可以混用吗？** 可以，它们完全等价。`遍历 x 在 列表 { 打印(x) }` 与 `for x in 列表 { print(x) }` 编译出的字节码相同。

**有没有类继承？** 没有。GZG 倾向于组合：把一个结构体实例作为另一个结构体的字段；需要多态时，不同结构体实现同名方法即可（鸭子类型）。

