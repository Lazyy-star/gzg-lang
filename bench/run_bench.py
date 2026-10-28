"""GZG 与 Python 的性能对比。

对 bench/cases 下每一对同名的 .gzg / .py 脚本：
  1. 先各运行一次，校验两者输出完全一致（保证比较的是相同的计算）；
  2. 各运行 N 次（默认 5 次），取墙钟时间的中位数；
  3. 生成 bench/results/report.md、results.json 与对比柱状图 chart.png。

用法:  python bench/run_bench.py [--runs 5] [--only fib,loop]
"""
import argparse
import json
import os
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CASES = ROOT / "bench" / "cases"
OUT = ROOT / "bench" / "results"
GZG = ROOT / "build" / ("gzg.exe" if os.name == "nt" else "gzg")

DESCRIPTIONS = {
    "fib": "递归函数调用 fib(32)",
    "loop": "300 万次整数运算的嵌套循环",
    "sieve": "埃氏筛 500 万（数组下标读写）",
    "quicksort": "手写快速排序 30 万个数（递归 + 数组）",
    "strings": "字符串插值、join/split、拼接",
    "wordfreq": "字典词频统计 40 万次 + 排序",
    "nbody": "N 体模拟 10 万步（结构体字段 + 浮点）",
    "closure": "闭包与 map/filter/reduce",
    "methods": "100 万次方法调用",
}


def run_once(cmd):
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, env=env)
    elapsed = time.perf_counter() - t0
    if p.returncode != 0:
        raise RuntimeError(f"{' '.join(map(str, cmd))} 失败:\n{p.stderr.decode('utf-8', 'replace')}")
    return elapsed, p.stdout.decode("utf-8").replace("\r\n", "\n").strip()


def measure(cmd, runs):
    times = []
    out = None
    for _ in range(runs):
        t, out = run_once(cmd)
        times.append(t)
    return statistics.median(times), min(times), out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    if not GZG.exists():
        sys.exit(f"找不到解释器 {GZG}，请先运行 make")
    only = set(filter(None, args.only.split(",")))
    names = [p.stem for p in sorted(CASES.glob("*.gzg")) if not only or p.stem in only]
    OUT.mkdir(exist_ok=True)

    py_version = subprocess.run([sys.executable, "--version"], capture_output=True, text=True).stdout.strip()
    gzg_version = subprocess.run([str(GZG), "--version"], capture_output=True, text=True, encoding="utf-8").stdout.strip()
    print(f"{gzg_version}  vs  {py_version}，每项运行 {args.runs} 次取中位数\n")

    results = []
    for name in names:
        gz = [str(GZG), str(CASES / f"{name}.gzg")]
        py = [sys.executable, str(CASES / f"{name}.py")]
        _, out_g = run_once(gz)
        _, out_p = run_once(py)
        same = out_g == out_p
        tg, tg_min, _ = measure(gz, args.runs)
        tp, tp_min, _ = measure(py, args.runs)
        ratio = tp / tg
        results.append({
            "name": name, "desc": DESCRIPTIONS.get(name, name),
            "gzg": tg, "gzg_min": tg_min, "python": tp, "python_min": tp_min,
            "speedup": ratio, "output_match": same, "output": out_g,
        })
        flag = "" if same else "  ⚠ 输出不一致!"
        print(f"{name:10s} GZG {tg:7.3f}s   Python {tp:7.3f}s   Python/GZG = {ratio:5.2f}x{flag}")
        if not same:
            print(f"    GZG:    {out_g}\n    Python: {out_p}")

    geo = statistics.geometric_mean([r["speedup"] for r in results])
    print(f"\n几何平均：Python 耗时 / GZG 耗时 = {geo:.2f}x")
    meta = {
        "gzg": gzg_version, "python": py_version, "runs": args.runs,
        "machine": f"{platform.system()} {platform.release()} / {platform.processor() or platform.machine()}",
        "date": time.strftime("%Y-%m-%d %H:%M"), "geomean": geo,
    }
    (OUT / "results.json").write_text(json.dumps({"meta": meta, "results": results}, ensure_ascii=False, indent=2),
                                      encoding="utf-8")
    write_report(meta, results)
    draw_chart(results)


def write_report(meta, results):
    lines = [
        "# 性能测试结果（自动生成）",
        "",
        f"- 环境：{meta['machine']}",
        f"- 解释器：{meta['gzg']}（gcc -O2）；对照：{meta['python']}（CPython）",
        f"- 方法：每个用例运行 {meta['runs']} 次，取进程墙钟时间中位数（含解释器启动）；运行前校验两种实现输出一致",
        f"- 时间：{meta['date']}",
        "",
        "| 用例 | 测试内容 | GZG (s) | Python (s) | Python/GZG | 输出一致 |",
        "|---|---|---:|---:|---:|:---:|",
    ]
    for r in results:
        lines.append(f"| {r['name']} | {r['desc']} | {r['gzg']:.3f} | {r['python']:.3f} | "
                     f"{r['speedup']:.2f}x | {'✓' if r['output_match'] else '✗'} |")
    lines += ["", f"几何平均：**{meta['geomean']:.2f}x**（大于 1 表示 GZG 更快）", "", "![对比图](chart.png)", ""]
    (OUT / "report.md").write_text("\n".join(lines), encoding="utf-8")


def draw_chart(results):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("未安装 matplotlib，跳过画图")
        return
    plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "Noto Sans CJK SC", "DejaVu Sans"]
    plt.rcParams["axes.unicode_minus"] = False
    names = [r["name"] for r in results]
    x = range(len(names))
    fig, ax = plt.subplots(figsize=(10, 4.8))
    w = 0.38
    ax.bar([i - w / 2 for i in x], [r["gzg"] for r in results], w, label="GZG", color="#1F4E8C")
    ax.bar([i + w / 2 for i in x], [r["python"] for r in results], w, label="Python 3", color="#E8A33D")
    for i, r in enumerate(results):
        ax.text(i, max(r["gzg"], r["python"]) * 1.02, f"{r['speedup']:.2f}x", ha="center", fontsize=9)
    ax.set_xticks(list(x))
    ax.set_xticklabels(names)
    ax.set_ylabel("耗时（秒，越低越好）")
    ax.set_title("GZG 与 CPython 执行时间对比（标注为 Python/GZG 倍数）")
    ax.legend()
    fig.tight_layout()
    fig.savefig(OUT / "chart.png", dpi=150)
    print(f"图表已保存到 {OUT / 'chart.png'}")


if __name__ == "__main__":
    main()
