/* 把 DECK（由 build_web.py 从 content_*.py 生成）渲染成 DOM。坐标与 PPT 一致：1 英寸 = 120px。 */
const Render = (() => {
  const IN = 120, GRID_X = 0.75, GRID_W = 11.83, COL = GRID_W / 12, H_IN = 7.5, PT = IN / 72;
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  const el = (tag, cls, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; return e; };
  const richText = (s) => s.includes("::") ? `<b>${esc(s.split("::")[0])}</b>${esc(s.split("::").slice(1).join("::"))}` : esc(s);

  /* 缩小字号直到内容放得下 */
  function fit(box, inner, size, min = 10) {
    inner.style.fontSize = size + "px";
    while (size > min && (inner.scrollHeight > box.clientHeight + 1 || inner.scrollWidth > box.clientWidth + 1)) {
      size -= 0.5;
      inner.style.fontSize = size + "px";
    }
  }

  const B = {
    stat(b, box) {
      const c = el("div", "card stat");
      c.innerHTML = `<div class="value"><span class="count" data-to="${esc(b.value)}">${esc(b.value)}</span><small>${esc(b.unit)}</small></div>
        <div class="label">${esc(b.label)}</div><div class="note">${esc(b.note)}</div>`;
      box.append(c);
    },
    bullets(b, box) {
      const ul = el("ul", "bullets");
      b.items.forEach((it, i) => { const li = el("li", "rv", richText(it)); li.style.setProperty("--d", 120 + i * 90); ul.append(li); });
      box.append(ul);
      box.fit = () => fit(box, ul, (b.size || 14) * PT, 13);
    },
    code(b, box) {
      const c = el("div", "code");
      const lines = b.hl.map((ln, i) => `<span class="ln" style="--i:${i}">${ln.map(([t, k]) => `<span class="c-${k}">${esc(t)}</span>`).join("") || " "}</span>`);
      c.innerHTML = `<div class="bar"><i></i><i></i><i></i>${esc(b.title || "")}<span class="copy">复制</span></div><pre>${lines.join("")}</pre>`;
      c.querySelector(".copy").onclick = (e) => {
        navigator.clipboard && navigator.clipboard.writeText(b.src);
        e.target.textContent = "已复制";
        setTimeout(() => (e.target.textContent = "复制"), 1200);
      };
      box.append(c);
      const pre = c.querySelector("pre");
      box.fit = () => {
        let size = (b.size || 13) * PT;
        pre.style.fontSize = size + "px";
        while (size > 10 && (pre.scrollHeight > c.clientHeight - 40 || pre.scrollWidth > c.clientWidth)) pre.style.fontSize = (size -= 0.5) + "px";
      };
    },
    table(b, box) {
      const tot = b.widths.reduce((a, x) => a + x, 0);
      const t = el("table", "tbl" + (b.align === "right" ? " right" : ""));
      t.innerHTML = `<colgroup>${b.widths.map((w) => `<col style="width:${(w / tot) * 100}%">`).join("")}</colgroup>` +
        b.rows.map((r, i) => {
          const tag = i ? "td" : "th";
          const cls = i && String(r[0]).startsWith("几何") ? ' class="total rv"' : i ? ' class="rv"' : "";
          const cell = (c) => /^\.|\.(md|py|gzg)$/.test(String(c)) && i ? `<code>${esc(c)}</code>` : esc(c);
          return `<tr${cls} style="--d:${i * 70}">${r.map((c) => `<${tag}>${cell(c)}</${tag}>`).join("")}</tr>`;
        }).join("");
      box.append(t);
      box.fit = () => {   // 先按设计字号排，放不下再缩小；放得下且有富余就拉开行高
        let size = (b.size || 13) * PT;
        t.style.fontSize = size + "px";
        while (size > 12 && t.offsetHeight > box.clientHeight) t.style.fontSize = (size -= 0.5) + "px";
        const spare = box.clientHeight - t.offsetHeight;
        if (spare > 0) t.querySelectorAll("td,th").forEach((c) => (c.style.paddingBlock = 9 + Math.min(10, spare / b.rows.length / 2) + "px"));
      };
    },
    cards(b, box) {
      const wrap = el("div");
      wrap.style.cssText = "position:absolute;inset:0;display:flex;gap:30px";
      b.items.forEach((it, i) => {
        const c = el("div", "card rv" + (it.accent === "warm" ? " warm" : ""));
        c.style.cssText = `position:relative;flex:1;--d:${i * 140};font-size:${(b.size || 13) * PT}px`;
        c.innerHTML = `<h3>${esc(it.title)}</h3><ul>${it.body.map((x) => `<li>${esc(x)}</li>`).join("")}</ul>`;
        wrap.append(c);
      });
      box.append(wrap);
    },
    pipeline(b, box) {
      const p = el("div", "pipe");
      p.innerHTML = b.stages.map(([t, d], i) => `<div class="st${i === 2 || i === 4 ? " hot" : ""}"><h4>${esc(t)}</h4><p>${esc(d.split(" · ").join("\n"))}</p></div>`).join("")
        + '<span class="packet"></span>';
      box.append(p);
    },
    steps(b, box) {
      const s = el("div", "steps");
      s.innerHTML = '<div class="rail"><b></b></div>' + b.items.map(([t, d], i) => `<div class="s"><div class="dot">${i + 1}</div><h4>${esc(t)}</h4><p>${esc(d)}</p></div>`).join("");
      box.append(s);
    },
    bars(b, box) {
      const max = Math.max(...b.items.map((x) => x[1]));
      const w = el("div", "bars");
      w.innerHTML = `<div class="cap">${esc(b.title)}</div>` + b.items.map(([n, v]) =>
        `<div class="row" data-tip="${esc(n)}：${v.toLocaleString()} ${esc(b.unit)}"><span class="n">${esc(n)}</span><span class="t"><b data-w="${(v / max) * 100}"></b></span><span class="v">${v.toLocaleString()}</span></div>`).join("");
      box.append(w);
    },
    pills(b, box) { box.append(el("div", "pills", b.items.map((x, i) => `<span class="pill rv" style="--d:${300 + i * 80}">${esc(x)}</span>`).join(""))); },
    quote(b, box) { box.append(el("div", "quote", esc(b.text))); },
    chart(b, box) { box.append(el("div", "chart")); },
    board2048(b, box) { box.append(el("div", "g2048")); },
  };

  function frame(s, d, n, total) {
    s.append(el("div", "footer", `<span>GZG · 郭志罡 · 程序设计实践</span><span class="num">${String(n).padStart(2, "0")} / ${String(total).padStart(2, "0")}</span>`));
    s.append(el("div", "kicker rv", esc(d.kicker || "")));
    s.append(el("div", "title rv", esc(d.title)));
    s.querySelector(".title").style.setProperty("--d", 80);
    if (d.sub) { const sub = el("div", "sub rv", esc(d.sub)); sub.style.setProperty("--d", 160); s.append(sub); }
    const top = d.sub ? 2.55 : 2.12, row = (H_IN - 0.75 - top) / 6;
    d.blocks.forEach((b, i) => {
      const [c, r, cw, rh] = b.area;
      const box = el("div", "blk " + (["cards", "bullets", "pills"].includes(b.type) ? "" : "rv"));
      box.style.cssText = `left:${(GRID_X + c * COL) * IN}px;top:${(top + r * row) * IN}px;width:${(cw * COL - 0.12) * IN}px;height:${(rh * row - 0.12) * IN}px;--d:${220 + i * 160}`;
      box.dataset.type = b.type;
      s.append(box);
      (B[b.type] || (() => {}))(b, box);
      box.block = b;
    });
  }

  function cover(s, d) {
    s.classList.add("cover");
    s.innerHTML = `<div class="t1">${[...d.title].map((ch, i) => `<span style="--i:${i}">${ch === " " ? "&nbsp;" : esc(ch)}</span>`).join("")}</div>
      <div class="en rv" style="--d:900">${esc(d.en)}</div><div class="s2 rv" style="--d:1200">${esc(d.sub)}</div>
      <div class="rule"></div><div class="au rv" style="--d:1500">${esc(d.author)}</div>`;
  }

  function ending(s, d) {
    s.classList.add("cover", "end");
    s.innerHTML = `<div class="t1" style="top:150px">${[...d.title].map((ch, i) => `<span style="--i:${i}">${esc(ch)}</span>`).join("")}</div>
      <div class="en rv" style="top:278px;--d:600">${esc(d.en)}</div><div class="panel rv" style="--d:800"><ul class="bullets" style="font-size:22px">
      ${d.items.map((x, i) => `<li class="rv" style="--d:${1000 + i * 120}">${richText(x)}</li>`).join("")}</ul></div>`;
  }

  function build(deck) {
    const stage = document.getElementById("stage");
    deck.forEach((d, i) => {
      const s = el("section", "slide after");
      s.dataset.bg = d.bg || "dark";
      s.dataset.title = d.title;
      if (d.kind === "cover") cover(s, d); else if (d.kind === "end") ending(s, d); else frame(s, d, i + 1, deck.length);
      stage.append(s);
    });
    stage.querySelectorAll(".blk").forEach((box) => box.fit && box.fit());
    return [...stage.querySelectorAll(".slide")];
  }

  return { build };
})();
