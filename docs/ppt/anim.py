"""给 python-pptx 生成的幻灯片加切换与动画（直接写 PowerPoint 的 <p:transition>/<p:timing> XML）。

- 每页切换：淡入（0.7 秒）
- 每页内容：按组依次“淡入 + 上浮”，进入页面后自动播放，不需要点击
- 封面背景：12 秒缓慢放大（推镜效果）"""
from lxml import etree

NS = ('xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
      'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
      'xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"')


class Ids:
    def __init__(self):
        self.n = 2

    def __call__(self):
        self.n += 1
        return self.n


def _entrance(ids, spid, delay, dur=600, rise=True, first=False):
    node = "afterEffect" if first else "withEffect"
    rise_xml = (f'<p:anim calcmode="lin" valueType="num"><p:cBhvr><p:cTn id="{ids()}" dur="{dur}" fill="hold"/>'
                f'<p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl><p:attrNameLst><p:attrName>ppt_y</p:attrName>'
                f'</p:attrNameLst></p:cBhvr><p:tavLst><p:tav tm="0"><p:val><p:strVal val="#ppt_y+0.04"/></p:val></p:tav>'
                f'<p:tav tm="100000"><p:val><p:strVal val="#ppt_y"/></p:val></p:tav></p:tavLst></p:anim>') if rise else ""
    return (f'<p:par><p:cTn id="{ids()}" fill="hold"><p:stCondLst><p:cond delay="{delay}"/></p:stCondLst><p:childTnLst>'
            f'<p:par><p:cTn id="{ids()}" presetID="10" presetClass="entr" presetSubtype="0" fill="hold" nodeType="{node}">'
            f'<p:stCondLst><p:cond delay="0"/></p:stCondLst><p:childTnLst>'
            f'<p:set><p:cBhvr><p:cTn id="{ids()}" dur="1" fill="hold"><p:stCondLst><p:cond delay="0"/></p:stCondLst>'
            f'</p:cTn><p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl><p:attrNameLst><p:attrName>style.visibility</p:attrName>'
            f'</p:attrNameLst></p:cBhvr><p:to><p:strVal val="visible"/></p:to></p:set>'
            f'<p:animEffect transition="in" filter="fade"><p:cBhvr><p:cTn id="{ids()}" dur="{dur}"/>'
            f'<p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl></p:cBhvr></p:animEffect>{rise_xml}'
            f'</p:childTnLst></p:cTn></p:par></p:childTnLst></p:cTn></p:par>')


def _zoom(ids, spid, dur=12000, scale=108000):
    return (f'<p:par><p:cTn id="{ids()}" fill="hold"><p:stCondLst><p:cond delay="0"/></p:stCondLst><p:childTnLst>'
            f'<p:par><p:cTn id="{ids()}" presetID="6" presetClass="emph" presetSubtype="0" fill="hold" nodeType="withEffect">'
            f'<p:stCondLst><p:cond delay="0"/></p:stCondLst><p:childTnLst>'
            f'<p:animScale><p:cBhvr><p:cTn id="{ids()}" dur="{dur}" fill="hold"/><p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl>'
            f'</p:cBhvr><p:by x="{scale}" y="{scale}"/></p:animScale>'
            f'</p:childTnLst></p:cTn></p:par></p:childTnLst></p:cTn></p:par>')


def animate(slide, groups, zoom_spid=None, step=170, max_total=2600):
    """groups：[[shape_id, ...], ...]。同一组内的形状同时出现，组与组之间错开 step 毫秒；
    组很多时自动压缩间隔，保证整页在 max_total 毫秒内出齐。"""
    ids = Ids()
    pars, first = [], True
    if zoom_spid:
        pars.append(_zoom(ids, zoom_spid))
    gap = min(step, max_total / max(len(groups), 1))
    for gi, g in enumerate(groups):
        for spid in g:
            pars.append(_entrance(ids, spid, int(gi * gap), first=first and not zoom_spid))
            first = False
    if not pars:
        return
    if zoom_spid:   # 第一个效果必须是 afterEffect：把推镜效果改成 afterEffect
        pars[0] = pars[0].replace('nodeType="withEffect"', 'nodeType="afterEffect"', 1)
    xml = (f'<p:timing {NS}><p:tnLst><p:par><p:cTn id="1" dur="indefinite" restart="never" nodeType="tmRoot"><p:childTnLst>'
           f'<p:seq concurrent="1" nextAc="seek"><p:cTn id="2" dur="indefinite" nodeType="mainSeq"><p:childTnLst>'
           f'<p:par><p:cTn id="{ids()}" fill="hold"><p:stCondLst><p:cond delay="indefinite"/>'
           f'<p:cond evt="onBegin" delay="0"><p:tn val="2"/></p:cond></p:stCondLst><p:childTnLst>'
           + "".join(pars) +
           '</p:childTnLst></p:cTn></p:par></p:childTnLst></p:cTn>'
           '<p:prevCondLst><p:cond evt="onPrev" delay="0"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:prevCondLst>'
           '<p:nextCondLst><p:cond evt="onNext" delay="0"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:nextCondLst>'
           '</p:seq></p:childTnLst></p:cTn></p:par></p:tnLst></p:timing>')
    _attach(slide, xml)


def transition(slide, dur=700):
    _attach(slide, f'<p:transition {NS} spd="slow" p14:dur="{dur}" '
                   'xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main"><p:fade/></p:transition>')


def _attach(slide, xml):
    el = etree.fromstring(xml)
    root = slide._element
    # 子元素顺序：cSld, clrMapOvr, transition, timing, extLst
    order = ["cSld", "clrMapOvr", "transition", "timing", "extLst"]
    tag = etree.QName(el).localname
    for old in root.findall(el.tag):
        root.remove(old)
    pos = len(root)
    for i, child in enumerate(root):
        name = etree.QName(child).localname
        if name in order and order.index(name) > order.index(tag):
            pos = i
            break
    root.insert(pos, el)
