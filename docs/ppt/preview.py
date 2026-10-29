"""用 PowerPoint 把 PPT 导出为图片，并拼成一张总览图 build/slides/contact.png，便于检查版式。
用法：.venv/Scripts/python docs/ppt/preview.py   （需要本机安装 PowerPoint；路径含中文时先复制到临时目录）"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / "build" / "slides"


def main():
    tmp = Path(tempfile.gettempdir()) / "gzgppt_preview"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir()
    shutil.copy(ROOT / "docs" / "GZG语言介绍.pptx", tmp / "deck.pptx")
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(HERE / "render.ps1"),
                    "-Pptx", str(tmp / "deck.pptx"), "-OutDir", str(tmp / "png")], check=True, timeout=300)
    OUT.mkdir(parents=True, exist_ok=True)
    pngs = sorted((tmp / "png").glob("slide*.png"))
    for p in pngs:
        shutil.copy(p, OUT / p.name)
    cols, tw = 3, 640
    th = tw * 9 // 16
    sheet = Image.new("RGB", (cols * tw + (cols + 1) * 10, ((len(pngs) + cols - 1) // cols) * (th + 10) + 10), "#111")
    for i, p in enumerate(pngs):
        im = Image.open(p).convert("RGB").resize((tw, th), Image.LANCZOS)
        sheet.paste(im, (10 + (i % cols) * (tw + 10), 10 + (i // cols) * (th + 10)))
    sheet.save(OUT / "contact.png")
    print("预览图：", OUT / "contact.png", f"（{len(pngs)} 页）")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
