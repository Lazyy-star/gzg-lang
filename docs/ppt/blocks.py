"""把 content_*.py 里的 block 画到 PPT 上。每个函数返回若干“动画组”（每组是一组 shape id，按顺序出现）。"""
import math

from pptx.chart.data import CategoryChartData
from pptx.enum.chart import XL_CHART_TYPE, XL_LEGEND_POSITION
from pptx.util import Emu, Inches, Pt

from theme import COLORS, MONO, SANS, SERIF, highlight, rgb, rich, set_run, shape, text

I = Inches


def ids(*shapes):
    return [s.shape_id for s in shapes]


def card_bg(s, x, y, w, h, accent="crimson"):
    """半透明玻璃卡片 + 顶部强调细线"""
    bg = shape(s, x, y, w, h, fill="FFFFFF", a=0.055, line="FFFFFF", la=0.16, radius=0.05)
    bar = shape(s, x + I(0.25), y, I(0.6), Emu(30000), fill=accent, kind="rect")
    return [bg, bar]


def para_lines(t, width_in, size):
    """估算一段文字在给定宽度内换几行（中文按 1 个字宽，ASCII 按 0.55）"""
    w = sum(1.0 if ord(c) > 0x2E80 else 0.6 for c in t) * size / 72 * 1.04
    return max(1, math.ceil(w / max(width_in, 0.1)))


def r_stat(s, b, x, y, w, h):
    parts = card_bg(s, x, y, w, h)
    num = text(s, x + I(0.3), y + I(0.35), w - I(0.6), I(1.2),
               [[(b["value"], {"size": 50, "color": "white", "font": SERIF}), (" " + b["unit"], {"size": 18, "color": "ice"})]])
    lab = text(s, x + I(0.3), y + I(1.55), w - I(0.6), I(0.4), b["label"], size=16, color="ice", bold=True)
    note = text(s, x + I(0.3), y + I(2.05), w - I(0.6), h - I(2.2), b["note"], size=12, color="muted", line=1.3)
    return [ids(*parts, num), ids(lab, note)]


def bullets_height(items, w_in, size):
    tot = 0
    for item in items:
        tot += para_lines(item.replace("::", "  "), w_in - 0.3, size) * size / 72 * 1.38 + size / 72 * 0.62
    return tot


def r_bullets(s, b, x, y, w, h):
    size = b.get("size", 14)
    while size > 10.5 and bullets_height(b["items"], w / 914400, size) > h / 914400:
        size -= 0.5
    groups, cy = [], y
    gap = size / 72 * 0.62
    for item in b["items"]:
        dot = shape(s, x, cy + I(size / 72 * 0.62), I(0.07), I(0.07), fill="crimson", kind="oval")
        plain = item.replace("::", "  ")
        nl = para_lines(plain, w / 914400 - 0.3, size)
        hh = I(nl * size / 72 * 1.38)
        tb = text(s, x + I(0.25), cy, w - I(0.25), hh, [rich(item)], size=size, line=1.25)
        groups.append(ids(dot, tb))
        cy += hh + I(gap)
    return [sum(groups[i:i + 2], []) for i in range(0, len(groups), 2)]


def r_code(s, b, x, y, w, h):
    size = b.get("size", 13)
    src_lines = b["src"].split(chr(10))
    longest = max(sum(1.0 if ord(c) > 0x2E80 else 0.55 for c in ln) for ln in src_lines)
    size = min(size, (h / 914400 - 0.7) * 72 / (len(src_lines) * 1.45), (w / 914400 - 0.55) * 72 / max(longest, 1))
    panel = shape(s, x, y, w, h, fill=COLORS["panel"], a=0.78, line="FFFFFF", la=0.14, radius=0.035)
    dots = [shape(s, x + I(0.2 + i * 0.2), y + I(0.17), I(0.11), I(0.11), fill=c, kind="oval")
            for i, c in enumerate(["E2554F", "F2A65A", "5DBB7A"])]
    title = text(s, x + I(0.95), y + I(0.09), w - I(1.2), I(0.3), b.get("title", ""), size=11, color="dim", font=MONO)
    paras = [[(t, {"color": c, "font": MONO}) for t, c in line] or [(" ", {})] for line in highlight(b["src"], b.get("lang", "gzg"))]
    body = text(s, x + I(0.28), y + I(0.5), w - I(0.45), h - I(0.6), paras, size=size, font=MONO, line=1.12)
    return [ids(panel, *dots, title), ids(body)]


def is_path(cell):
    c = str(cell)
    return c.endswith((".md", ".py", ".gzg")) or c.startswith(".")


def table_height(rows, cws, size):
    return sum(max(para_lines(str(c), cw / 914400 - 0.2, size) for c, cw in zip(row, cws)) * size / 72 * 1.35 + 0.22
               for row in rows)


def r_table(s, b, x, y, w, h):
    rows, size = b["rows"], b.get("size", 13)
    tot = sum(b["widths"])
    cws = [w * c / tot for c in b["widths"]]
    while size > 10 and table_height(rows, cws, size) > h / 914400:
        size -= 0.5
    pad = max(0.0, (h / 914400 - table_height(rows, cws, size)) / len(rows) * 0.5)   # 有富余时把行距拉开一点
    groups, cy = [], y
    for ri, row in enumerate(rows):
        lines = max(para_lines(str(c), cw / 914400 - 0.2, size) for c, cw in zip(row, cws))
        rh = I(lines * size / 72 * 1.35 + 0.22 + min(pad, 0.18))
        shapes, cx = [], x
        if ri == 0:
            shapes.append(shape(s, x, cy, w, rh, fill="FFFFFF", a=0.07, kind="rect"))
        for ci, (cell, cw) in enumerate(zip(row, cws)):
            right = b.get("align") == "right" and ci > 0
            col = "ice" if ri == 0 else ("white" if ci == 0 else "text")
            if ri == len(rows) - 1 and str(row[0]).startswith("几何"):
                col = "warm"
            shapes.append(text(s, cx + I(0.12), cy, cw - I(0.24), rh, str(cell), size=size, anchor="m",
                               color=col, bold=(ri == 0 or ci == 0), align="r" if right else "l",
                               font=MONO if ri and is_path(cell) else SANS))
            cx += cw
        line = shape(s, x, cy + rh - Emu(6000), w, Emu(9000), fill="crimson" if ri == 0 else "FFFFFF",
                     a=0.9 if ri == 0 else 0.12, kind="rect")
        shapes.append(line)
        groups.append(ids(*shapes))
        cy += rh
    return groups


def r_cards(s, b, x, y, w, h):
    n, gap = len(b["items"]), I(0.25)
    cw = (w - gap * (n - 1)) / n
    groups = []
    for i, it in enumerate(b["items"]):
        cx = x + i * (cw + gap)
        accent = "warm" if it.get("accent") == "warm" else "crimson"
        parts = card_bg(s, cx, y, cw, h, accent)
        t = text(s, cx + I(0.3), y + I(0.28), cw - I(0.6), I(0.45), it["title"], size=17,
                 color="warm" if accent == "warm" else "ice", font=SERIF, bold=False)
        body = text(s, cx + I(0.3), y + I(0.85), cw - I(0.6), h - I(1.0),
                    [[("·  ", {"color": accent}), (ln, {})] for ln in it["body"]], size=b.get("size", 13),
                    color="text", line=1.3, space_after=5)
        groups.append(ids(*parts, t, body))
    return groups


def r_pipeline(s, b, x, y, w, h):
    n, arrow = len(b["stages"]), I(0.42)
    bw = (w - arrow * (n - 1)) / n
    groups = []
    for i, (t, d) in enumerate(b["stages"]):
        bx = x + i * (bw + arrow)
        hot = i in (2, 4)
        box = shape(s, bx, y, bw, h, fill="5B95F7" if hot else "FFFFFF", a=0.30 if hot else 0.06,
                    line="A9CBFF" if hot else "FFFFFF", la=0.55 if hot else 0.18, radius=0.08)
        tt = text(s, bx, y + I(0.35), bw, I(0.5), t, size=19, color="white", font=SERIF, align="c")
        dd = text(s, bx + I(0.15), y + I(0.9), bw - I(0.3), h - I(0.95), d.replace(" · ", "\n"),
                  size=11.5, color="ice" if hot else "muted", align="c", line=1.15)
        g = [box, tt, dd]
        if i < n - 1:
            g.append(shape(s, bx + bw + I(0.1), y + h / 2 - I(0.13), I(0.22), I(0.26), fill="crimson", kind="chevron"))
        groups.append(ids(*g))
    return groups


def r_steps(s, b, x, y, w, h):
    n = len(b["items"])
    cw = w / n
    rail = shape(s, x + cw / 2, y + I(0.42), w - cw, Emu(12000), fill="FFFFFF", a=0.25, kind="rect")
    groups = [ids(rail)]
    for i, (t, d) in enumerate(b["items"]):
        cx = x + i * cw
        dot = shape(s, cx + cw / 2 - I(0.42), y, I(0.84), I(0.84), fill="0B1A48", line="A9CBFF", la=0.8, lw=1.5, kind="oval")
        num = text(s, cx + cw / 2 - I(0.42), y, I(0.84), I(0.84), str(i + 1), size=22, color="white", font=SERIF,
                   align="c", anchor="m")
        tt = text(s, cx, y + I(1.0), cw, I(0.4), t, size=17, color="ice", font=SERIF, align="c")
        dd = text(s, cx + I(0.2), y + I(1.45), cw - I(0.4), h - I(1.45), d, size=12, color="muted", align="c", line=1.3)
        groups.append(ids(dot, num, tt, dd))
    return groups


def r_bars(s, b, x, y, w, h):
    items = b["items"]
    title = text(s, x, y, w, I(0.35), b["title"], size=13, color="muted")
    top = y + I(0.45)
    rh = (h - I(0.45)) / len(items)
    mx = max(v for _, v in items)
    lab_w, val_w = I(1.15), I(0.6)
    groups = [ids(title)]
    for i, (name, v) in enumerate(items):
        ry = top + i * rh
        bh = min(rh * 0.5, I(0.2))
        lab = text(s, x, ry, lab_w, rh, name, size=11, color="text", anchor="m")
        track = shape(s, x + lab_w, ry + rh / 2 - bh / 2, w - lab_w - val_w, bh, fill="FFFFFF", a=0.06, radius=0.5)
        bar = shape(s, x + lab_w, ry + rh / 2 - bh / 2, max(I(0.05), (w - lab_w - val_w) * v / mx), bh,
                    fill="5B95F7" if i else "E2554F", a=0.9, radius=0.5)
        val = text(s, x + w - val_w + I(0.08), ry, val_w, rh, f"{v:,}", size=11, color="ice", anchor="m")
        groups.append(ids(lab, track, bar, val))
    return [groups[0] + groups[1]] + groups[2:]


def r_pills(s, b, x, y, w, h):
    cx, cy, size, out = x, y, 12, []
    ph = I(0.4)
    for t in b["items"]:
        tw = I(sum(1.0 if ord(c) > 0x2E80 else 0.56 for c in t) * size / 72 + 0.45)
        if cx + tw > x + w:
            cx, cy = x, cy + ph + I(0.12)
        p = shape(s, cx, cy, tw, ph, fill="5B95F7", a=0.16, line="A9CBFF", la=0.45, radius=0.5)
        tb = text(s, cx, cy, tw, ph, t, size=size, color="white", align="c", anchor="m")
        out.append(ids(p, tb))
        cx += tw + I(0.14)
    return [sum(out, [])]


def r_quote(s, b, x, y, w, h):
    bar = shape(s, x, y + I(0.08), Emu(38000), h - I(0.16), fill="crimson", kind="rect")
    tb = text(s, x + I(0.3), y, w - I(0.3), h, b["text"], size=17, color="white", font=SERIF, anchor="m", line=1.35)
    return [ids(bar, tb)]


TILE = {2: "1E2C5A", 4: "24356B", 8: "2A417F", 16: "2F4E95", 32: "3459AB", 64: "3A66C2", 128: "4A78D2",
        256: "4F80D8", 512: "5A8CE2", 1024: "6A96E6", 2048: "E2554F", 4096: "D84843", 8192: "C73B3A", 16384: "F2A65A"}


def r_board(s, b, x, y, w, h):
    size = min(w, h - I(0.6))
    gap = size * 0.03
    cell = (size - gap * 5) / 4
    bx = x + (w - size) / 2
    frame = shape(s, bx, y, size, size, fill="FFFFFF", a=0.06, line="FFFFFF", la=0.2, radius=0.04)
    groups = [ids(frame)]
    for r, row in enumerate(b["board"]):
        g = []
        for c, v in enumerate(row):
            tx, ty = bx + gap + c * (cell + gap), y + gap + r * (cell + gap)
            tile = shape(s, tx, ty, cell, cell, fill=TILE.get(v, "1E2C5A"), radius=0.08)
            fs = 26 if v < 100 else 22 if v < 1000 else 17 if v < 10000 else 14
            g += ids(tile, text(s, tx, ty, cell, cell, str(v), size=fs, color="white", bold=True, align="c", anchor="m"))
        groups.append(g)
    cap = text(s, x, y + size + I(0.15), w, I(0.4), b["caption"], size=13, color="muted", align="c")
    groups.append(ids(cap))
    return groups


def r_chart(s, b, x, y, w, h, bench):
    data = CategoryChartData()
    res = bench["results"]
    data.categories = [r["name"] for r in res]
    data.add_series("GZG", [round(r["gzg"], 3) for r in res])
    data.add_series("Python", [round(r["python"], 3) for r in res])
    gf = s.shapes.add_chart(XL_CHART_TYPE.COLUMN_CLUSTERED, x, y, w, h, data)
    ch = gf.chart
    ch.has_legend = True
    ch.legend.position = XL_LEGEND_POSITION.TOP
    ch.legend.include_in_layout = False
    ch.legend.font.size, ch.legend.font.color.rgb = Pt(12), rgb("text")
    for ser, col in zip(ch.plots[0].series, ["5B95F7", "F2A65A"]):
        ser.format.fill.solid()
        ser.format.fill.fore_color.rgb = rgb(col)
    ch.plots[0].gap_width, ch.plots[0].overlap = 60, -10
    for ax in (ch.category_axis, ch.value_axis):
        ax.tick_labels.font.size = Pt(11)
        ax.tick_labels.font.color.rgb = rgb("muted")
        ax.format.line.color.rgb = rgb("2A3A66")
    ch.value_axis.has_major_gridlines = True
    ch.value_axis.major_gridlines.format.line.color.rgb = rgb("1C2A50")
    ch.value_axis.has_title = True
    ch.value_axis.axis_title.text_frame.text = "耗时（秒，越低越好）"
    set_run(ch.value_axis.axis_title.text_frame.paragraphs[0].runs[0], 11, "muted")
    return [[gf.shape_id]]


RENDER = {"stat": r_stat, "bullets": r_bullets, "code": r_code, "table": r_table, "cards": r_cards,
          "pipeline": r_pipeline, "steps": r_steps, "bars": r_bars, "pills": r_pills, "quote": r_quote,
          "board2048": r_board}
