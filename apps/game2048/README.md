# 终端 2048 + expectimax AI（GZG 实现）

## 玩法

4x4 棋盘，每次把所有方块推向一个方向，相邻且相等的两块合并成一块（数值翻倍，得分加上合并后的数值；每块一步内只合并一次）。
每次有效移动后，随机空格上出现一个新块：90% 是 2，10% 是 4。拼出 2048 即胜利（之后可以继续玩）；棋盘满且没有可合并的相邻块时游戏结束。

| 输入 | 作用 |
|---|---|
| `w` `a` `s` `d` / `up` `left` `down` `right` / `上` `左` `下` `右` | 移动 |
| `u` | 撤销上一步（可连续撤销，最多 1000 步） |
| `r` | 重新开始 |
| `auto` | AI 接管，逐步显示直到结束 |
| `h` | 帮助 |
| `q` | 退出 |

空行只重绘；`#` 开头的行当注释忽略（方便写重放文件）。最高分保存在 `apps/game2048/highscore.txt`，不存在时自动创建。

## 运行（在仓库根目录）

```bash
build/stable/gzg.exe apps/game2048/main.gzg                          # 交互模式
build/stable/gzg.exe apps/game2048/main.gzg --seed 1                 # 固定随机种子，一局完全可复现
build/stable/gzg.exe apps/game2048/main.gzg --seed 1 --plain < apps/game2048/moves.txt   # 重放
build/stable/gzg.exe apps/game2048/main.gzg --auto                   # AI 自动玩，每步打印棋盘和分数
build/stable/gzg.exe apps/game2048/main.gzg --auto --fast --seed 42  # AI 只打印最终结果
build/stable/gzg.exe apps/game2048/test_board.gzg                    # 单元测试
```

其他参数：`--plain` 不用 ANSI 颜色、不清屏（适合重定向到文件）；`--depth D` 固定搜索深度（默认按空格数自适应）；`--help`。
输入结束（`input()` 返回 nil）时保存分数并正常退出。

**注意：AI 一局通常要走 3000~5000 步，每步约 0.1~0.15 秒，`--auto --fast` 一局要 5~10 分钟以上**（多个进程并行时更久）。
想快点看结果可以用 `--depth 1`，但分数会明显降低。

最高分文件路径：GZG 的文件函数相对当前目录，拿不到脚本所在目录，所以程序在当前目录下找得到 `apps/game2048/main.gzg` 时写 `apps/game2048/highscore.txt`，否则写当前目录的 `highscore.txt`。

## AI 算法（ai.gzg）

expectimax 搜索：

- **玩家节点**：尝试 4 个方向，取期望值最大的；没有方向能动时值为 0（死局）。
- **机会节点**：对每个空格分别放 2（概率 0.9）和 4（概率 0.1），取所有空格的平均。
- **深度自适应**：空格 ≥ 8 时深度 1，4~7 时深度 2，< 4 时深度 3（局面越危险搜得越深）。
- **剪枝**：到达某个节点的累计概率 < 0.001 时直接用评估值。
- **置换表**：一次 `best_move` 内，相同的（棋盘, 剩余深度）只算一次。

叶子评估在 log2 等级上逐“线”（4 行 + 4 列）计算，再求和；每条线的结果按 4 个数缓存：

- 基础分 200000（保证活着的局面为正）
- \+ 270 × 空格数
- \+ 700 × 可合并的相邻相等块
- − 47 × 单调性违反量（等级的 4 次方之差，取“向左递增”和“向右递增”中较小的）——让大块排成一条有序的链，自然靠到角落
- − 11 × Σ 等级^3.5（大数惩罚，鼓励尽早合并）

## 文件结构

| 文件 | 内容 |
|---|---|
| `board.gzg` | 纯逻辑：棋盘表示（16 元素一维数组）、`slide_row`/`move_cells` 合并、`spawn_tile`、胜负判断、`Game`/`Snapshot` 结构体（撤销栈）。不做输入输出 |
| `ai.gzg` | expectimax 与启发式评估（纯计算），`best_move(cells, depth?)` |
| `ui.gzg` | ANSI 渲染、状态行、帮助文字、`parse_command`（返回字符串，不直接输出） |
| `main.gzg` | 入口：参数解析、`App` 结构体、交互循环、AI 模式、最高分文件 |
| `test_board.gzg` | 单元测试（62 项断言），通过后打印“全部测试通过” |
| `moves.txt` | 重放用的输入：几十步移动、两次 `u` 撤销、`h`、最后 `q` |
| `bench_ai.gzg` | 开发调参用的 AI 跑分脚本：`gzg apps/game2048/bench_ai.gzg <seed>`，每 200 步打印进度，结束时打印分数、最大块、步数、用时、搜索节点数 |
| `DEVLOG.md` | 开发记录（实验用） |
| `.gitignore` | 忽略 `highscore.txt` |

依赖关系：`main.gzg` → `ui.gzg`、`ai.gzg` → `board.gzg`（GZG 的 `import "x.gzg"` 相对导入方文件，名字进入全局，重复导入只执行一次）。
