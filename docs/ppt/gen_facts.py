"""收集 PPT 需要的统计数字，写入 docs/ppt/facts.json。
自动统计：解释器行数、测试数量与分类、应用行数、git 历史、覆盖率（若有 build/cov）。
人工撰写的内容（开发记录摘要、应用要点等）放在 facts_manual.json，原样合并。"""
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / "docs" / "ppt"


def lines(paths):
    return sum(len(p.read_text(encoding="utf-8").splitlines()) for p in paths)


def sh(cmd):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", shell=True).stdout


src = ROOT / "src"
groups = [["lexer.c"], ["compiler.c"], ["vm.c"], ["builtins.c"], ["object.c", "table.c", "value.c", "chunk.c"],
          ["memory.c"], ["platform.c"]]
counts = [lines([src / f for f in g]) for g in groups]
all_src = list(src.glob("*.c")) + list(src.glob("*.h"))
total = lines(all_src)
line_counts = [str(c) for c in counts] + [str(total)]

tests = [p for p in (ROOT / "tests").rglob("*.gzg")
         if not p.name.startswith("_") and p.name != "run_tests.gzg" and "_pending" not in p.parts]
cats = {}
for p in tests:
    cats[p.relative_to(ROOT / "tests").parts[0]] = cats.get(p.relative_to(ROOT / "tests").parts[0], 0) + 1
cat_list = sorted(cats.items(), key=lambda kv: -kv[1])

app_dir = ROOT / "apps" / "game2048"
app_files = [p for p in app_dir.glob("*.gzg") if not p.name.startswith("_")] if app_dir.exists() else []
app_lines = lines(app_files) if app_files else 0

coverage = "（运行 make coverage 后生成）"
cov_dir = ROOT / "build" / "cov"
if cov_dir.exists():
    hit = allc = 0
    for f in src.glob("*.c"):
        out = subprocess.run(["gcov", "-n", "-o", ".", f"../../src/{f.name}"], cwd=cov_dir, capture_output=True,
                             text=True).stdout
        m = re.search(r"Lines executed:([\d.]+)% of (\d+)", out)
        if m:
            hit += float(m.group(1)) * int(m.group(2)) / 100
            allc += int(m.group(2))
    if allc:
        coverage = f"{hit * 100 / allc:.1f}%（{allc} 行可执行代码）"

log = sh('git log --reverse --format="%h %s"').strip().splitlines()
log = [l if len(l) < 70 else l[:68] + "…" for l in log]
if len(log) > 22:
    log = log[:10] + [f"   …（共 {len(log)} 次提交）…"] + log[-11:]

facts = {
    "interp_lines": f"{total:,}",
    "line_counts": line_counts,
    "test_count": len(tests),
    "test_dirs": len(cats),
    "test_categories": [[k, v] for k, v in cat_list],
    "app_lines": app_lines,
    "coverage": coverage,
    "git_log": "\n".join(log),
    "commit_count": len(sh("git rev-list --all").split()),
}
manual = json.loads((HERE / "facts_manual.json").read_text(encoding="utf-8"))
facts.update(manual)
(HERE / "facts.json").write_text(json.dumps(facts, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({k: v for k, v in facts.items() if k not in ("git_log",)}, ensure_ascii=False, indent=1)[:1500])
