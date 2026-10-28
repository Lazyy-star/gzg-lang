# GZG 内建函数与方法参考

记号：`x?` 表示可选参数。“报错”指抛出可被 `try/catch` 捕获的运行错误。

## 全局函数

### 输入输出
| 函数 | 说明 |
|---|---|
| `print(a, b, ...)` | 各参数转字符串后用空格连接，末尾换行。无参数时只输出换行 |
| `write(a, b, ...)` | 各参数转字符串后**直接拼接**输出，不换行、不加空格，立即刷新 |
| `input(prompt?)` | 输出提示后读一行（不含换行符）；输入结束(EOF)返回 `nil` |

### 类型与转换
| 函数 | 说明 |
|---|---|
| `type(x)` | `"nil" "bool" "int" "float" "string" "array" "map" "range" "function" "struct"`，实例返回结构体名 |
| `int(x)` | float 向零截断；字符串按十进制解析（允许首尾空白和正负号），失败报错；bool → 0/1 |
| `float(x)` | 数字或字符串转浮点 |
| `str(x)` | 转字符串（与 print 显示一致；实例会调用 `to_str()`） |
| `bool(x)` | 按真值规则 |
| `len(x)` | 字符串（字符数）/数组/字典/区间的长度 |
| `array(x)` | 区间/字符串(按字符)/字典(键)/数组(浅拷贝) 转为数组 |
| `copy(x)` | 数组/字典的浅拷贝，其他值原样返回 |
| `fmt(x, n)` | 保留 n 位小数的字符串：`fmt(2.0/3, 3) == "0.667"` |
| `hex(n)` `bin(n)` | `"0xff"`、`"0b101"` |
| `chr(n)` `ord(s)` | 码点与字符互转，支持中文 |

### 数学
`abs(x)`，`min(a, b, ...)` / `min(数组)`，`max(...)`，`floor(x)`、`ceil(x)`（返回 int），`round(x)`（四舍五入到 int，.5 远离零），`round(x, n)`（保留 n 位，返回 float），`sqrt(x)`，`pow(a, b)`，`sin cos tan atan2(y, x) exp`，`log(x)` 自然对数 / `log(x, base)`。常量：`PI` `E` `INT_MAX` `INT_MIN`。

### 随机数（确定性的 xorshift 生成器）
| 函数 | 说明 |
|---|---|
| `random()` | [0, 1) 浮点 |
| `randint(a, b)` | [a, b] 闭区间整数 |
| `seed(n)` | 设定种子，之后的序列完全可复现 |
| `choice(arr)` | 随机取一个元素 |
| `shuffle(arr)` | 原地打乱 |

### 时间
`clock()` 单调时钟（秒，浮点，用于计时）；`time()` Unix 时间戳（秒）；`sleep(ms)` 暂停毫秒数。

### 序列与测试
| 函数 | 说明 |
|---|---|
| `range(stop)` / `range(start, stop, step?)` | 返回**数组**，语义同 Python |
| `assert(cond, msg?)` | 条件为假时报错 `断言失败: msg` |
| `assert_eq(actual, expected, msg?)` | 不相等时报错 `断言失败: 期望 <expected>，实际 <actual>` |

### 系统与文件
| 函数 | 说明 |
|---|---|
| `args()` | 命令行参数数组（不含解释器和脚本名） |
| `exit(code?)` | 立即结束程序 |
| `platform()` | `"windows"`、`"linux"`、`"macos"` 等 |
| `read_file(path)` | 读取整个文件为字符串，失败报错 |
| `write_file(path, s)` / `append_file(path, s)` | 写入 / 追加 |
| `file_exists(path)` | bool |
| `list_dir(path)` | 目录下的文件名数组（已排序） |
| `run(cmd)` | 执行 shell 命令，返回 `{code: 退出码, output: 标准输出+标准错误}` |

### 中文别名
`打印=print` `输入=input` `长度=len` `类型=type` `整数=int` `字符串=str` `随机整数=randint`

## 字符串方法（字符串不可变，方法都返回新值）

| 方法 | 示例 → 结果 |
|---|---|
| `len()` | `"你好".len()` → `2` |
| `upper()` `lower()` | 只转换 ASCII 字母 |
| `trim()` `trim_start()` `trim_end()` | 去空白 |
| `split(sep?)` | `"a,b".split(",")` → `["a", "b"]`；无参数时按连续空白切分 |
| `lines()` | 按换行切分（兼容 `\r\n`） |
| `find(sub)` | 字符下标，找不到 `-1` |
| `count(sub)` `contains(sub)` `starts_with(p)` `ends_with(p)` | |
| `replace(old, new)` | 替换全部 |
| `chars()` | 字符数组 |
| `repeat(n)` | 同 `s * n` |
| `slice(a, b?)` | 同 `s[a:b]`，支持负数 |
| `reverse()` | 按字符反转 |
| `pad_left(n, ch?)` `pad_right(n, ch?)` `center(n, ch?)` | 补齐到 n 个字符，默认空格 |
| `is_digit()` `is_alpha()` `is_space()` | 非空且每个字符都满足 |

## 数组方法

| 方法 | 说明 |
|---|---|
| `len()` `is_empty()` `first()` `last()` | `first/last` 对空数组报错 |
| `push(x, ...)` | 末尾追加（可多个），返回 nil |
| `pop()` | 删除并返回最后一个，空数组报错 |
| `insert(i, x)` `remove_at(i)` | 按下标插入 / 删除并返回 |
| `remove(x)` | 删除第一个等于 x 的元素，返回是否删除 |
| `index_of(x)` `contains(x)` `count(x)` | 查找 |
| `join(sep?)` | 元素转字符串后连接，默认 `""` |
| `reverse()` `sort(key?)` | **原地**修改，返回 nil |
| `sorted(key?)` | 返回排好序的新数组（稳定排序） |
| `map(f)` `filter(f)` `each(f)` | 高阶函数 |
| `reduce(f, init?)` | `[1,2,3].reduce(fn(a, b) => a + b, 0)` |
| `find(f)` `find_index(f)` | 第一个满足的元素 / 下标，找不到为 `nil` / `-1` |
| `any(f)` `all(f)` | |
| `sum()` `min()` `max()` | |
| `slice(a, b?)` | 同 `arr[a:b]` |
| `extend(other)` `clear()` `unique()` `copy()` | |

## 字典方法

| 方法 | 说明 |
|---|---|
| `len()` `is_empty()` | |
| `keys()` `values()` `items()` | 按插入顺序；`items()` 返回 `[[k, v], ...]` |
| `has(k)` | 是否有键（同 `k in m`） |
| `get(k, default?)` | 键不存在时返回 default（默认 nil），**不报错** |
| `set(k, v)` | 同 `m[k] = v` |
| `remove(k)` | 删除并返回值，不存在返回 nil |
| `merge(other)` | 把 other 的键值合并进来（原地） |
| `clear()` `copy()` | |

## 区间方法
`len()`、`to_array()`、`contains(n)`。

## 运算符速查
`+ - * / // % **`，`== != < <= > >=`，`and or not`（`&& || !`），`& | ^ ~ << >>`，`in`、`not in`，`a..b`、`a..=b`，`c ? x : y`，`x |> f(a)`。
