/* 各版块的交互：数字滚动、条形图、流水线光点、步骤轮播、性能图表、可玩的 2048、悬浮提示。 */
const Widgets = (() => {
  const tip = document.createElement("div");
  tip.className = "tip";
  document.body.append(tip);
  document.addEventListener("mousemove", (e) => {
    const t = e.target.closest && e.target.closest("[data-tip]");
    if (!t) { tip.classList.remove("on"); return; }
    tip.innerHTML = t.dataset.tip;
    tip.style.left = e.clientX + "px";
    tip.style.top = e.clientY + "px";
    tip.classList.add("on");
  });

  const timers = new Map();   // 每页离开时清理的定时器
  const later = (slide, fn, ms, every) => {
    const id = every ? setInterval(fn, ms) : setTimeout(fn, ms);
    (timers.get(slide) || timers.set(slide, []).get(slide)).push([id, every]);
  };

  function countUp(node) {
    if (document.body.classList.contains("capture")) return;
    const raw = node.dataset.to, dec = (raw.split(".")[1] || "").length, to = parseFloat(raw.replace(/,/g, ""));
    const t0 = performance.now(), fmt = (v) => dec ? v.toFixed(dec) : Math.round(v).toLocaleString("en-US");
    const step = (t) => {
      const k = Math.min(1, (t - t0) / 1400), e = 1 - Math.pow(1 - k, 3);
      node.textContent = fmt(to * e);
      if (k < 1) requestAnimationFrame(step);
    };
    node.textContent = fmt(0);
    setTimeout(() => requestAnimationFrame(step), 500);
  }

  function pipeline(slide, p) {
    const st = [...p.querySelectorAll(".st")], pk = p.querySelector(".packet");
    let i = 0;
    const tick = () => {
      st.forEach((s, k) => s.classList.toggle("lit", k === i));
      const r = st[i].offsetLeft + st[i].offsetWidth / 2 - 6;
      pk.style.transition = i ? "left .9s cubic-bezier(.2,.8,.2,1)" : "none";
      pk.style.left = r + "px";
      i = (i + 1) % st.length;
    };
    later(slide, tick, 600);
    later(slide, tick, 1100, true);
  }

  function steps(slide, s) {
    const items = [...s.querySelectorAll(".s")], rail = s.querySelector(".rail b");
    let i = 0;
    const tick = () => {
      items.forEach((x, k) => x.classList.toggle("on", k <= i));
      rail.style.width = (i / (items.length - 1)) * 100 + "%";
      i = (i + 1) % (items.length + 1);
      if (i === 0) { items.forEach((x) => x.classList.remove("on")); rail.style.width = "0"; }
    };
    later(slide, tick, 700);
    later(slide, tick, 1000, true);
  }

  /* ---------------- 性能图表 ---------------- */
  function chart(node, bench, mode = "time") {
    const W = node.clientWidth, H = node.clientHeight - 40, L = 56, R = 10, T = 16, Bm = 34;
    const res = bench.results, n = res.length, gw = (W - L - R) / n;
    const time = mode === "time";
    const top = Math.max(...res.map((r) => (time ? r.python : r.speedup)));
    const stepV = time ? 0.25 : 0.5, ticks = Math.ceil(top / stepV), max = ticks * stepV;
    const y = (v) => T + (H - T - Bm) * (1 - v / max);
    let svg = `<svg viewBox="0 0 ${W} ${H}"><g class="grid">`;
    for (let k = 0; k <= ticks; k++) {
      const v = k * stepV;
      svg += `<line x1="${L}" x2="${W - R}" y1="${y(v)}" y2="${y(v)}"/><text x="${L - 8}" y="${y(v) + 4}" text-anchor="end">${time ? v.toFixed(2) + "s" : v.toFixed(1) + "×"}</text>`;
    }
    svg += "</g>";
    if (!time) svg += `<line x1="${L}" x2="${W - R}" y1="${y(1)}" y2="${y(1)}" stroke="#e2554f" stroke-dasharray="6 6" stroke-width="1.5"/>
      <text x="${L + 6}" y="${y(1) + 18}" style="fill:#e2554f">1× = 与 CPython 持平</text>`;
    res.forEach((r, i) => {
      const x0 = L + i * gw;
      const tipTxt = `<b>${r.name}</b>　${r.desc}<br>GZG ${r.gzg.toFixed(3)}s · Python ${r.python.toFixed(3)}s<br>Python/GZG = <b>${r.speedup.toFixed(2)}×</b>`;
      const bars = time ? [[r.gzg, "#5b95f7", 0.16], [r.python, "#f2a65a", 0.52]] : [[r.speedup, r.speedup >= 1 ? "#5b95f7" : "#e2554f", 0.25]];
      bars.forEach(([v, c, off]) => {
        const bw = gw * (time ? 0.32 : 0.5);
        svg += `<rect class="bar" data-tip="${tipTxt.replace(/"/g, "&quot;")}" x="${x0 + gw * off}" width="${bw}" rx="4" fill="${c}" y="${y(v)}" height="${y(0) - y(v)}" style="transform-origin:0 ${y(0)}px;transform:scaleY(0)"/>`;
      });
      if (!time) svg += `<text x="${x0 + gw / 2}" y="${y(r.speedup) - 8}" text-anchor="middle" style="fill:#fff;font-weight:700">${r.speedup.toFixed(2)}×</text>`;
      svg += `<text x="${x0 + gw / 2}" y="${H - 10}" text-anchor="middle">${r.name}</text>`;
    });
    node.querySelector("svg")?.remove();
    node.insertAdjacentHTML("beforeend", svg + "</svg>");
    node.querySelector(".legend").innerHTML = time
      ? '<span><i style="background:#5b95f7"></i>GZG</span><span><i style="background:#f2a65a"></i>Python 3.12</span>'
      : `<span>几何平均 <b style="color:#fff">${bench.meta.geomean.toFixed(2)}×</b>（蓝色 = GZG 更快）</span>`;
    const capture = document.body.classList.contains("capture");
    (capture ? (f) => f() : (f) => requestAnimationFrame(() => setTimeout(f, 60)))(() => node.querySelectorAll(".bar").forEach((b, i) => {
      b.style.transitionDelay = i * 45 + "ms";
      b.style.transform = "scaleY(1)";
    }));
  }

  function initChart(node, bench) {
    node.innerHTML = '<div class="legend"></div><div class="tabs"><button data-m="time" class="on">耗时</button><button data-m="ratio">倍数</button></div>';
    node.querySelectorAll("button").forEach((b) => (b.onclick = () => {
      node.querySelectorAll("button").forEach((x) => x.classList.toggle("on", x === b));
      chart(node, bench, b.dataset.m);
    }));
    node.mode = () => node.querySelector("button.on").dataset.m;
  }
  /* ---------------- 可玩的 2048：默认 AI 自动玩，按 WASD 接管，空格切换 AI，R 重开 ---------------- */
  const COLORS = { 2: "#1e2c5a", 4: "#24356b", 8: "#2a417f", 16: "#2f4e95", 32: "#3459ab", 64: "#3a66c2", 128: "#4a78d2",
    256: "#4f80d8", 512: "#5a8ce2", 1024: "#6a96e6", 2048: "#e2554f", 4096: "#d84843", 8192: "#c73b3a", 16384: "#f2a65a" };
  function rng(seed) { return () => { seed |= 0; seed = (seed + 0x6d2b79f5) | 0; let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }

  function slide4(row) {   // 一行向左合并，返回 [新行, 得分, 合并位置]
    const xs = row.filter((v) => v), out = [], merged = [];
    let score = 0;
    for (let i = 0; i < xs.length; i++) {
      if (xs[i] === xs[i + 1]) { out.push(xs[i] * 2); score += xs[i] * 2; merged.push(out.length - 1); i++; } else out.push(xs[i]);
    }
    while (out.length < 4) out.push(0);
    return [out, score, merged];
  }
  const LINES = { a: (i) => [0, 1, 2, 3].map((k) => i * 4 + k), d: (i) => [3, 2, 1, 0].map((k) => i * 4 + k),
    w: (i) => [0, 1, 2, 3].map((k) => k * 4 + i), s: (i) => [3, 2, 1, 0].map((k) => k * 4 + i) };
  function move(cells, dir) {
    const next = cells.slice(), merged = new Set();
    let score = 0;
    for (let i = 0; i < 4; i++) {
      const idx = LINES[dir](i), [row, sc, m] = slide4(idx.map((k) => cells[k]));
      idx.forEach((k, j) => (next[k] = row[j]));
      m.forEach((j) => merged.add(idx[j]));
      score += sc;
    }
    return { next, score, merged, moved: next.some((v, i) => v !== cells[i]) };
  }
  const SNAKE = [15, 14, 13, 12, 8, 9, 10, 11, 7, 6, 5, 4, 0, 1, 2, 3].map((k) => Math.pow(4, k));
  const heur = (c) => c.reduce((a, v, i) => a + v * SNAKE[i], 0) + c.filter((v) => !v).length * 4096;
  function aiMove(cells) {
    let best = null, bv = -1;
    for (const d of "wasd") {
      const r = move(cells, d);
      if (!r.moved) continue;
      const empties = r.next.map((v, i) => (v ? -1 : i)).filter((i) => i >= 0);
      let tot = 0;   // 对每个空格放 2 取平均（期望值的简化）
      for (const e of empties) { const n = r.next.slice(); n[e] = 2; tot += heur(n); }
      const v = empties.length ? tot / empties.length : heur(r.next);
      if (v > bv) { bv = v; best = d; }
    }
    return best;
  }

  function game2048(node, initial) {
    const size = Math.min(node.clientWidth, node.clientHeight - 80), gap = size * 0.03, cell = (size - gap * 5) / 4;
    node.innerHTML = `<div class="board" style="width:${size}px;height:${size}px"></div><div class="info"></div>`;
    const board = node.querySelector(".board"), info = node.querySelector(".info");
    const st = { cells: initial.flat(), score: 290724, ai: true, rand: rng(3), over: false, timer: null, born: new Set(), merged: new Set() };
    const draw = () => {
      board.innerHTML = st.cells.map((v, i) => v ? `<div class="tile${st.born.has(i) ? " new" : ""}${st.merged.has(i) ? " merged" : ""}"
        style="left:${gap + (i % 4) * (cell + gap)}px;top:${gap + Math.floor(i / 4) * (cell + gap)}px;width:${cell}px;height:${cell}px;
        background:${COLORS[v] || "#f2a65a"};font-size:${cell * (v < 100 ? 0.42 : v < 1000 ? 0.34 : v < 10000 ? 0.27 : 0.22)}px">${v}</div>` : "").join("");
      const mx = Math.max(...st.cells);
      info.innerHTML = `分数 <b>${st.score.toLocaleString()}</b> · 最大块 <b>${mx}</b> · ${st.ai ? "AI 自动玩中" : "手动模式"}${st.over ? " · <b>游戏结束</b>" : ""}<br>
        <kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> 接管 · <kbd>空格</kbd> 切换 AI · <kbd>R</kbd> 重开`;
    };
    const spawn = () => {
      const e = st.cells.map((v, i) => (v ? -1 : i)).filter((i) => i >= 0);
      if (!e.length) return;
      const k = e[Math.floor(st.rand() * e.length)];
      st.cells[k] = st.rand() < 0.9 ? 2 : 4;
      st.born = new Set([k]);
    };
    const step = (dir) => {
      if (st.over) return;
      const r = move(st.cells, dir);
      if (!r.moved) return;
      st.cells = r.next;
      st.score += r.score;
      st.merged = r.merged;
      spawn();
      st.over = !"wasd".split("").some((d) => move(st.cells, d).moved);
      draw();
    };
    const reset = () => { st.cells = Array(16).fill(0); st.score = 0; st.over = false; st.merged = new Set(); spawn(); spawn(); draw(); };
    let pause = false;
    const tick = () => {
      if (!st.ai || pause) return;
      if (st.over) { pause = true; setTimeout(() => { reset(); pause = false; }, 2000); return; }
      const d = aiMove(st.cells);
      if (d) step(d); else { st.over = true; draw(); }
    };
    draw();
    return {
      start() { reset(); st.ai = true; st.timer = setInterval(tick, 140); },
      stop() { clearInterval(st.timer); },
      key(k) {
        k = k.toLowerCase();
        if ("wasd".includes(k) && k.length === 1) { st.ai = false; step(k); return true; }
        if (k === " ") { st.ai = !st.ai; draw(); return true; }
        if (k === "r") { reset(); return true; }
        return false;
      },
    };
  }

  let game = null;
  return {
    init(slides, deck, bench) {
      slides.forEach((s) => {
        const c = s.querySelector(".chart");
        if (c) initChart(c, bench);
        const g = s.querySelector(".g2048");
        if (g) s.game = game2048(g, deck[slides.indexOf(s)].blocks.find((b) => b.type === "board2048").board);
      });
    },
    enter(s) {
      s.querySelectorAll(".count").forEach(countUp);
      s.querySelectorAll(".bars b").forEach((b, i) => setTimeout(() => (b.style.width = b.dataset.w + "%"), 450 + i * 40));
      s.querySelectorAll(".pipe").forEach((p) => pipeline(s, p));
      s.querySelectorAll(".steps").forEach((x) => steps(s, x));
      s.querySelectorAll(".chart").forEach((c) => (document.body.classList.contains("capture") ? chart(c, window.BENCH, c.mode())
        : setTimeout(() => chart(c, window.BENCH, c.mode()), 350)));
      if (s.game) { game = s.game; if (!document.body.classList.contains("capture")) later(s, () => game.start(), 1600); }
    },
    leave(s) {
      (timers.get(s) || []).forEach(([id, every]) => (every ? clearInterval(id) : clearTimeout(id)));
      timers.delete(s);
      s.querySelectorAll(".bars b").forEach((b) => (b.style.width = "0"));
      if (s.game) { s.game.stop(); game = null; }
    },
    key(k) { return game ? game.key(k) : false; },
  };
})();
