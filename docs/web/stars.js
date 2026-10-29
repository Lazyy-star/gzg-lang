/* 实时星轨：粒子沿倾斜椭圆旋转，叠加在静态背景图上。翻页时短暂加速并拉长（跃迁效果），随鼠标轻微视差。 */
const Stars = (() => {
  const cv = document.querySelector("#bg canvas");
  const ctx = cv.getContext("2d");
  const COLORS = [[170, 200, 255], [100, 145, 255], [255, 255, 255], [255, 105, 95], [255, 190, 130]];
  const WEIGHTS = [0.5, 0.28, 0.12, 0.07, 0.03];
  const PRESETS = {
    cover: { cx: 0.58, cy: 0.5, rx: 0.55, ry: 0.42, tilt: -0.28, alpha: 1 },
    dark: { cx: 0.86, cy: 0.62, rx: 0.62, ry: 0.5, tilt: -0.35, alpha: 0.42 },
    end: { cx: 0.42, cy: 0.48, rx: 0.58, ry: 0.44, tilt: 0.25, alpha: 1 },
  };
  let W = 0, H = 0, dpr = 1, parts = [], boost = 0, running = true;
  let target = { ...PRESETS.cover }, cur = { ...PRESETS.cover };
  const mouse = { x: 0.5, y: 0.5 }, sm = { x: 0.5, y: 0.5 };

  function pick() {
    let r = Math.random();
    for (let i = 0; i < WEIGHTS.length; i++) { if ((r -= WEIGHTS[i]) < 0) return i; }
    return 0;
  }

  function spawn(n) {
    parts = [];
    for (let i = 0; i < n; i++) {
      const r = 0.2 + Math.pow(Math.random(), 0.75) * 2.1;
      parts.push({
        r, a: Math.random() * Math.PI * 2,
        v: (0.0005 + Math.random() * 0.0011) / Math.sqrt(r),
        len: 0.008 + Math.random() * 0.045,
        c: COLORS[pick()], b: 0.25 + Math.random() * 0.75, w: 0.5 + Math.random() * 1.3,
        tw: Math.random() * Math.PI * 2,
      });
    }
  }

  function resize() {
    dpr = Math.min(window.devicePixelRatio || 1, 1.5);
    W = cv.width = Math.round(innerWidth * dpr);
    H = cv.height = Math.round(innerHeight * dpr);
  }

  function frame(t) {
    if (!running) return;
    for (const k of ["cx", "cy", "rx", "ry", "tilt", "alpha"]) cur[k] += (target[k] - cur[k]) * 0.035;
    sm.x += (mouse.x - sm.x) * 0.05;
    sm.y += (mouse.y - sm.y) * 0.05;
    boost *= 0.94;
    const cx = (cur.cx + (sm.x - 0.5) * 0.03) * W, cy = (cur.cy + (sm.y - 0.5) * 0.03) * H;
    const rx = cur.rx * W, ry = cur.ry * H, speed = 1 + boost * 14, stretch = 1 + boost * 7;
    ctx.clearRect(0, 0, W, H);
    ctx.globalCompositeOperation = "lighter";
    ctx.lineCap = "round";
    for (const p of parts) {
      p.a += p.v * speed;
      const eye = Math.min(1, Math.max(0, (p.r - 0.28) / 0.5));
      const twinkle = 0.75 + 0.25 * Math.sin(t * 0.002 + p.tw);
      const al = p.b * eye * twinkle * cur.alpha;
      if (al < 0.03) continue;
      ctx.strokeStyle = `rgba(${p.c[0]},${p.c[1]},${p.c[2]},${al.toFixed(3)})`;
      ctx.lineWidth = p.w * dpr * (p.r > 1.2 ? 1.25 : 1);
      ctx.beginPath();
      ctx.ellipse(cx, cy, p.r * rx, p.r * ry, cur.tilt, p.a, p.a + p.len * stretch);
      ctx.stroke();
    }
    requestAnimationFrame(frame);
  }

  addEventListener("resize", resize);
  addEventListener("mousemove", (e) => { mouse.x = e.clientX / innerWidth; mouse.y = e.clientY / innerHeight; });
  document.addEventListener("visibilitychange", () => {
    running = !document.hidden;
    if (running) requestAnimationFrame(frame);
  });
  resize();
  spawn(innerWidth < 900 ? 700 : 1400);
  requestAnimationFrame(frame);

  return {
    /* kind：cover / dark / end；i：页码，用来给内容页一点变化 */
    set(kind, i = 0) {
      const p = { ...PRESETS[kind] };
      if (kind === "dark") { p.cy += Math.sin(i * 1.7) * 0.05; p.tilt += Math.cos(i * 1.3) * 0.08; }
      target = p;
    },
    warp(strength = 1) { boost = Math.min(1.4, boost + strength); },
  };
})();
