"""根据 bench/results/results.json（由 bench/run_bench.gzg 生成）画 GZG 与 CPython 的耗时对比图。

计时与结果校验都在 GZG 写的 bench/run_bench.gzg 中完成；本脚本只负责画图（需要 matplotlib）。
用法:  python bench/plot_chart.py
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "bench" / "results"


def main():
    data = json.loads((OUT / "results.json").read_text(encoding="utf-8"))
    results = data["results"]
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
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
    ax.set_title("GZG 与 CPython 执行时间对比（标注为 Python/GZG 倍数，几何平均 {:.2f}x）".format(data["meta"]["geomean"]))
    ax.legend()
    fig.tight_layout()
    fig.savefig(OUT / "chart.png", dpi=150)
    print(f"图表已保存到 {OUT / 'chart.png'}")


if __name__ == "__main__":
    main()
