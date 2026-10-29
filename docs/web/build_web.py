"""生成网页版介绍 docs/GZG语言介绍.html：单个文件，内嵌样式、脚本、数据与背景图，双击即可在浏览器打开。
内容与 PPT 共用 docs/ppt/content_*.py。用法：.venv/Scripts/python docs/web/build_web.py"""
import base64
import io
import json
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PPT = ROOT / "docs" / "ppt"
sys.path.insert(0, str(PPT))
from content_c import all_slides  # noqa: E402
from theme import highlight  # noqa: E402

OUT = ROOT / "docs" / "GZG语言介绍.html"

PAGE = """<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GZG 语言 · 设计与实现</title>
<style>{css}</style>
</head>
<body>
<div id="bg">{imgs}<canvas></canvas><div class="vignette"></div></div>
<div id="progress"></div>
<div id="stage">
  <div class="brand" title="回到封面"><span class="mark"><i></i><i></i><i></i><i></i></span>GZG</div>
</div>
<div class="tagline">Build a language,<br>prove it works.</div>
<nav id="dots"></nav>
<div id="help"><kbd>←</kbd><kbd>→</kbd> 翻页 · <kbd>O</kbd> 总览 · <kbd>F</kbd> 全屏 · <kbd>1</kbd>–<kbd>9</kbd> 跳页</div>
<script>window.DECK = {deck};
window.BENCH = {bench};</script>
<script>{js}</script>
</body>
</html>
"""


def data_uri(path, width=1600, quality=82):
    im = Image.open(path).convert("RGB")
    im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, "JPEG", quality=quality, optimize=True, progressive=True)
    return "data:image/jpeg;base64," + base64.b64encode(buf.getvalue()).decode()


def main():
    facts = json.loads((PPT / "facts.json").read_text(encoding="utf-8"))
    bench = json.loads((ROOT / "bench" / "results" / "results.json").read_text(encoding="utf-8"))
    deck = all_slides(facts, bench)
    for d in deck:
        for b in d.get("blocks", []):
            if b["type"] == "code":
                b["hl"] = highlight(b["src"], b.get("lang", "gzg"))
            b["area"] = list(b["area"])
    imgs = "".join(f'<div class="img" data-k="{k}" style="background-image:url({data_uri(PPT / "assets" / f"bg_{k}.jpg")})"></div>'
                   for k in ("cover", "dark", "end"))
    css = (HERE / "style.css").read_text(encoding="utf-8")
    js = "\n".join((HERE / f).read_text(encoding="utf-8") for f in ("stars.js", "render.js", "widgets.js", "app.js"))
    dump = lambda o: json.dumps(o, ensure_ascii=False).replace("</", "<\\/")
    html = PAGE.format(css=css, imgs=imgs, deck=dump(deck), bench=dump(bench), js=js)
    OUT.write_text(html, encoding="utf-8")
    print(f"已生成 {OUT}（{len(deck)} 页，{len(html.encode()) / 1024:.0f} KB）")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
