"""用真实浏览器（Edge 无头模式 + DevTools 协议）检查网页版的交互：
翻页、过渡动画结束后的状态、图表切换、2048 自动/手动、总览模式、脚本错误。截图存到 build/web/live_*.png。
用法：.venv/Scripts/python docs/web/check_web.py"""
import base64
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

import websocket

ROOT = Path(__file__).resolve().parents[2]
HTML = ROOT / "docs" / "GZG语言介绍.html"
OUT = ROOT / "build" / "web"
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"


class Page:
    def __init__(self, port=9333):
        self.proc = subprocess.Popen([EDGE, "--headless=new", f"--remote-debugging-port={port}", "--window-size=1600,900",
                                      "--hide-scrollbars", f"--user-data-dir={tempfile.mkdtemp()}", "about:blank"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                tabs = json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json"))
                break
            except OSError:
                time.sleep(0.2)
        url = next(t["webSocketDebuggerUrl"] for t in tabs if t["type"] == "page")
        self.ws = websocket.create_connection(url, timeout=30)
        self.n = 0
        self.errors = []
        self.send("Runtime.enable")
        self.send("Page.enable")

    def send(self, method, **params):
        self.n += 1
        self.ws.send(json.dumps({"id": self.n, "method": method, "params": params}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("method") == "Runtime.exceptionThrown":
                self.errors.append(msg["params"]["exceptionDetails"].get("exception", {}).get("description", "?"))
            if msg.get("id") == self.n:
                return msg.get("result", {})

    def js(self, expr):
        r = self.send("Runtime.evaluate", expression=expr, returnByValue=True, awaitPromise=True)
        if "exceptionDetails" in r:
            self.errors.append(r["exceptionDetails"].get("text", "?"))
        return r.get("result", {}).get("value")

    def key(self, key, code=None):
        for t in ("keyDown", "keyUp"):
            self.send("Input.dispatchKeyEvent", type=t, key=key, code=code or key,
                      windowsVirtualKeyCode={"ArrowRight": 39, "ArrowLeft": 37, " ": 32}.get(key, ord(key.upper()[0])))

    def shot(self, name):
        data = self.send("Page.captureScreenshot", format="png")["data"]
        (OUT / f"live_{name}.png").write_bytes(base64.b64decode(data))

    def close(self):
        self.ws.close()
        self.proc.kill()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    p = Page()
    p.send("Page.navigate", url=HTML.as_uri())
    time.sleep(3.5)
    report = {"slides": p.js("document.querySelectorAll('.slide').length"),
              "cover_title_opacity": p.js("getComputedStyle(document.querySelector('.slide.active .t1 span')).opacity")}
    p.shot("01_cover")
    p.key("ArrowRight")
    time.sleep(0.25)
    report["mid_transition_blur"] = p.js("getComputedStyle(document.querySelector('.slide.active')).filter")
    time.sleep(2.5)
    report["stat_counted"] = p.js("[...document.querySelectorAll('.slide.active .count')].map(e=>e.textContent)")
    p.shot("02_stats")
    p.js("location.hash='#12'; dispatchEvent(new HashChangeEvent('hashchange'))")
    for _ in range(10):
        p.key("ArrowRight")
        time.sleep(0.12)
    time.sleep(2.5)
    report["now_on"] = p.js("location.hash")
    report["chart_bars"] = p.js("document.querySelectorAll('.slide.active .chart .bar').length")
    p.js("document.querySelector('.slide.active .chart button[data-m=ratio]').click()")
    time.sleep(1.6)
    report["ratio_bars"] = p.js("document.querySelectorAll('.slide.active .chart .bar').length")
    p.shot("12_chart_ratio")
    for _ in range(3):
        p.key("ArrowRight")
        time.sleep(0.15)
    time.sleep(5)
    report["g2048_auto"] = p.js("document.querySelector('.slide.active .g2048 .info').innerText.split('\\n')[0]")
    p.shot("15_2048_auto")
    for k in "wasdwasd":
        p.key(k)
        time.sleep(0.05)
    report["g2048_manual"] = p.js("document.querySelector('.slide.active .g2048 .info').innerText.split('\\n')[0]")
    p.key("o")
    time.sleep(1.5)
    report["overview"] = p.js("document.body.classList.contains('overview')")
    p.shot("overview")
    report["errors"] = p.errors
    p.close()
    print(json.dumps(report, ensure_ascii=False, indent=1))
    return 1 if p.errors else 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
