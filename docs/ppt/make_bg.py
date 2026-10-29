"""生成 PPT / 网页共用的星轨背景图（参考“深蓝星轨漩涡 + 地平线蓝光 + 底部暗红”的配色）。
输出 docs/ppt/assets/bg_cover.jpg、bg_dark.jpg、bg_end.jpg（1920x1080）。
用法：.venv/Scripts/python docs/ppt/make_bg.py"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

OUT = Path(__file__).resolve().parent / "assets"
W, H = 1920, 1080
rng = np.random.default_rng(20261028)


def lerp_stops(t, stops):
    """t∈[0,1] 的数组，按 (位置, (r,g,b)) 色标插值"""
    out = np.zeros(t.shape + (3,))
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        m = (t >= p0) & (t <= p1)
        k = ((t[m] - p0) / (p1 - p0))[:, None]
        out[m] = np.array(c0) * (1 - k) + np.array(c1) * k
    return out


def trails(cx, cy, ax, ay, tilt=-0.35, rings=650, rmin=0.12, rmax=2.4):
    """倾斜椭圆同心星轨。每环随机半径抖动、亮度、颜色、点线长度（按像素长度而非角度，避免摩尔纹）。"""
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float64)
    x0, y0 = xs - cx, ys - cy
    c, s_ = np.cos(tilt), np.sin(tilt)
    u, v = (x0 * c + y0 * s_) / ax, (-x0 * s_ + y0 * c) / ay
    r = np.sqrt(u * u + v * v)
    th = np.arctan2(v, u)
    edges = np.sort(rmin + (rmax - rmin) * rng.random(rings) ** 0.85)
    idx = np.clip(np.searchsorted(edges, r) - 1, 0, rings - 2)
    width = (edges[idx + 1] - edges[idx]).clip(1e-4)
    frac = (r - edges[idx]) / width
    bright = rng.random(rings) ** 2.6
    dash_px = rng.uniform(6, 60, rings)                               # 每段亮线的像素长度
    gap = rng.uniform(0.25, 0.75, rings)
    phase = rng.random(rings) * 1000
    hue = rng.random(rings)
    palette = np.array([[0.70, 0.82, 1.0], [0.40, 0.58, 1.0], [1.0, 1.0, 1.0], [1.0, 0.42, 0.38], [1.0, 0.78, 0.55]])
    choose = np.select([hue < 0.5, hue < 0.78, hue < 0.9, hue < 0.97], [0, 1, 2, 3], 4)
    col = palette[choose]
    # 每环的周长取整为若干段，角度绕回 ±π 时首尾相接，不会出现接缝
    mid = (edges[:-1] + edges[1:]) / 2
    nseg = np.maximum(8, np.round(2 * np.pi * mid * (ax + ay) / 2 / dash_px[:-1]))
    k = (th / (2 * np.pi) * nseg[idx] + phase[idx]) % 1.0
    dash = (k > gap[idx]).astype(float) * (0.35 + 0.65 * rng.random(rings)[idx])
    line = np.exp(-((frac - 0.5) ** 2) / 0.035)
    inten = (line * dash * bright[idx])[..., None] * col[idx]
    inten *= ((r > rmin) & (r < rmax))[..., None]
    return inten, r, v


def compose(name, cx, cy, ax, ay, trail_gain, stops, tilt=-0.35, rim=0.0, vignette_left=0.0, depth_blur=True):
    t = np.linspace(0, 1, H)[:, None].repeat(W, 1)
    img = lerp_stops(t, stops)
    tr, r, v = trails(cx, cy, ax, ay, tilt)
    eye = np.clip((r - 0.22) / 0.55, 0, 1) ** 1.4                     # 漩涡中心（眼）更暗、无星轨
    img *= (0.35 + 0.65 * eye)[..., None]
    tr = tr * eye[..., None] * trail_gain * 255
    glow = np.asarray(Image.fromarray(np.clip(tr, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(9)))
    img = img + tr + glow.astype(float) * 1.1
    if rim > 0:                                                        # 漩涡下缘的弧形蓝白光
        band = np.exp(-((r - 1.25) ** 2) / (2 * 0.16 ** 2)) / (1 + np.exp(-v * 6))   # 平滑过渡，避免硬边
        img = img + band[..., None] * np.array([90, 130, 200]) * rim
    if vignette_left > 0:                                              # 内容页左侧压暗，保证文字可读
        xs = np.linspace(0, 1, W)[None, :]
        k = 1 - vignette_left * np.clip(1 - xs / 0.8, 0, 1) ** 1.3
        img = img * k[..., None]
    pil = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    if depth_blur:                                                     # 越往下越虚（景深）
        soft = np.asarray(pil.filter(ImageFilter.GaussianBlur(6))).astype(float)
        sharp = np.asarray(pil).astype(float)
        wgt = np.clip((t - 0.55) / 0.35, 0, 1)[..., None]
        pil = Image.fromarray(np.clip(sharp * (1 - wgt) + soft * wgt, 0, 255).astype(np.uint8))
    OUT.mkdir(exist_ok=True)
    pil.save(OUT / name, quality=90)
    print("saved", OUT / name)


NAVY = [(0.0, (3, 7, 26)), (0.40, (9, 24, 78)), (0.66, (20, 54, 140)), (0.78, (60, 108, 200)),
        (0.84, (150, 184, 236)), (0.89, (120, 112, 160)), (0.94, (86, 26, 28)), (1.0, (26, 6, 8))]
DARK = [(0.0, (3, 6, 20)), (0.55, (7, 16, 52)), (0.86, (14, 34, 96)), (0.95, (40, 26, 60)), (1.0, (50, 12, 14))]

if __name__ == "__main__":
    compose("bg_cover.jpg", W * 0.58, H * 0.50, W * 0.55, H * 0.42, 1.25, NAVY, tilt=-0.28, rim=1.0)
    compose("bg_dark.jpg", W * 0.86, H * 0.62, W * 0.62, H * 0.5, 0.45, DARK, tilt=-0.35, rim=0.35, vignette_left=0.6)
    compose("bg_end.jpg", W * 0.42, H * 0.48, W * 0.58, H * 0.44, 1.15, NAVY, tilt=0.25, rim=1.0)
