/* 翻页、键盘 / 滚轮 / 触摸、总览模式、全屏、URL 定位。 */
(() => {
  if (location.search.includes("capture")) document.body.classList.add("capture");
  const slides = Render.build(window.DECK);
  Widgets.init(slides, window.DECK, window.BENCH);
  const bgs = Object.fromEntries([...document.querySelectorAll("#bg .img")].map((e) => [e.dataset.k, e]));
  const progress = document.getElementById("progress"), dots = document.getElementById("dots");
  let cur = -1, lock = 0;

  slides.forEach((s, i) => {
    const a = document.createElement("a");
    a.innerHTML = `<span>${String(i + 1).padStart(2, "0")} · ${s.dataset.title}</span>`;
    a.onclick = () => go(i);
    dots.append(a);
    s.addEventListener("click", () => { if (document.body.classList.contains("overview")) { toggleOverview(false); go(i); } });
  });

  function go(n, warp = true) {
    n = Math.max(0, Math.min(slides.length - 1, n));
    if (n === cur) return;
    const prev = cur;
    cur = n;
    slides.forEach((s, i) => {
      s.classList.toggle("active", i === n);
      s.classList.toggle("before", i < n);
      s.classList.toggle("after", i > n);
    });
    if (prev >= 0) Widgets.leave(slides[prev]);
    Widgets.enter(slides[n]);
    const k = slides[n].dataset.bg;
    Object.entries(bgs).forEach(([key, e]) => e.classList.toggle("on", key === k));
    Stars.set(k, n);
    if (warp && prev >= 0) Stars.warp(Math.min(1.2, 0.5 + Math.abs(n - prev) * 0.25));
    progress.style.width = ((n + 1) / slides.length) * 100 + "%";
    [...dots.children].forEach((d, i) => d.classList.toggle("on", i === n));
    history.replaceState(null, "", "#" + (n + 1));
  }

  const next = () => go(cur + 1), prev = () => go(cur - 1);

  function toggleOverview(force) {
    const on = document.body.classList.toggle("overview", force);
    const cols = 6, rows = Math.ceil(slides.length / cols);
    slides.forEach((s, i) => {
      s.style.setProperty("--ox", ((i % cols) - (cols - 1) / 2) * 1700);
      s.style.setProperty("--oy", (Math.floor(i / cols) - (rows - 1) / 2) * 960);
    });
    if (!on) slides.forEach((s) => { s.style.removeProperty("--ox"); s.style.removeProperty("--oy"); });
  }

  addEventListener("keydown", (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    if (Widgets.key(e.key)) { e.preventDefault(); return; }
    const k = e.key;
    if (["ArrowRight", "ArrowDown", "PageDown", " ", "Enter"].includes(k)) { e.preventDefault(); next(); }
    else if (["ArrowLeft", "ArrowUp", "PageUp", "Backspace"].includes(k)) { e.preventDefault(); prev(); }
    else if (k === "Home") go(0);
    else if (k === "End") go(slides.length - 1);
    else if (k === "o" || k === "O" || k === "Escape") toggleOverview(k === "Escape" ? false : undefined);
    else if (k === "f" || k === "F") document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen();
    else if (/^[1-9]$/.test(k)) go(+k - 1);
  });

  addEventListener("wheel", (e) => {
    if (document.body.classList.contains("overview")) return;
    const now = Date.now();
    if (now - lock < 900 || Math.abs(e.deltaY) < 20) return;
    lock = now;
    e.deltaY > 0 ? next() : prev();
  }, { passive: true });

  let tx = 0, ty = 0;
  addEventListener("touchstart", (e) => { tx = e.touches[0].clientX; ty = e.touches[0].clientY; }, { passive: true });
  addEventListener("touchend", (e) => {
    const dx = e.changedTouches[0].clientX - tx, dy = e.changedTouches[0].clientY - ty;
    if (Math.max(Math.abs(dx), Math.abs(dy)) < 50) return;
    (Math.abs(dx) > Math.abs(dy) ? dx : dy) < 0 ? next() : prev();
  }, { passive: true });

  document.querySelector(".brand").onclick = () => go(0);

  function resize() {
    const s = Math.min(innerWidth / 1600, innerHeight / 900);
    document.documentElement.style.setProperty("--scale", s);
  }
  addEventListener("resize", resize);
  resize();
  const start = parseInt(location.hash.slice(1), 10);
  go(Number.isFinite(start) ? start - 1 : 0, false);
})();
