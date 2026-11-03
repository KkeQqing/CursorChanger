#!/usr/bin/env python3
"""tools/gen_skin.py — 生成一套 15 槽「真素材」皮肤到 skin/（PNG + SVG 混装）。

为什么手写（不用 Pillow）：与 tools/gen_png_fixtures.py 同一取向 —— 本机 pip 返回码不可信、
且这里要让 PNG 与 SVG 共享**同一套矢量坐标**，避免两条实现各画各的。纯标准库 = 确定性、离线可重放。

设计要点（对齐 docs/DESIGN.md）：
  * 每个槽位用一个可被别名表认领的 stem 命名（arrow/ibeam/... 见 §5.2），文件名定身份；
    形状只负责「认得出 + 非透明」，像素级热点/朝向的目测校正仍是 U8/U17，不在这里假装已验。
  * PNG 走 M3 通道、SVG 走 M4 通道（白名单：只 polygon/circle，无 style/filter/text/mask）。
  * arrow 同时出 .png 与 .svg —— 同槽双认领，用来把 §5.2「重复认领必须报告」做成活体证据。
  * 两个对角槽 SizeNWSE/SizeNESW 故意画成**反对称**（一端实心箭头、另一端只留渐细短尾），
    为 U19（OS 是否自动镜像对角）备好可判读的夹具；本轮只离线自检，镜像与否留待真机目测。
  * 白底实心块那种「测不出问题」的对称夹具是本项目已知教训（U19 方法缺陷），这里刻意避开。
"""
import os
import struct
import zlib

REPO = os.path.dirname(os.path.abspath(os.path.join(__file__, "..")))
OUT_DIR = os.path.join(REPO, "skin")
W = H = 48  # 源画布；PNG 48 原生 + SVG viewBox 0 0 48 48，sizes{16,32,48} 下都产出三档

# ---- 调色板 ----
DARK = (24, 24, 24, 255)      # 轮廓 / 实心暗特征
LIGHT = (245, 245, 245, 255)  # 内芯，深底可辨
RED = (200, 45, 45, 255)
AMBER = (224, 160, 40, 255)
TRANS = (0, 0, 0, 0)

K_INNER = 0.74  # 轮廓留边比例（越大边越细）


# ---- 几何工具 ----
def inset(pts, k):
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    return [(cx + (x - cx) * k, cy + (y - cy) * k) for (x, y) in pts]


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def outlined(pts):
    """暗描边 + 亮内芯（两笔），使同一形状在深/浅背景都能被看见。"""
    return [op_fill('poly', pts, DARK), op_fill('poly', inset(pts, K_INNER), LIGHT)]


def op_fill(kind, geom, color):
    return {'a': 'fill', 'k': kind, 'g': geom, 'c': color}


def op_erase(kind, geom):
    return {'a': 'erase', 'k': kind, 'g': geom}


# ---- 各槽位形状：返回 ops 列表（PNG 栅格化 + SVG 发射共用）----
def s_arrow():
    return outlined([(14, 8), (14, 36), (21, 30), (25, 40), (30, 38), (25, 29), (33, 29)])


def s_ibeam():
    return [
        op_fill('poly', rect(15, 10, 33, 13), DARK),   # 上衬线
        op_fill('poly', rect(15, 35, 33, 38), DARK),   # 下衬线
        op_fill('poly', rect(22, 10, 26, 38), DARK),   # 竖干
    ]


def s_wait():
    return [
        op_fill('poly', rect(12, 9, 36, 12), DARK),            # 上框
        op_fill('poly', rect(12, 36, 36, 39), DARK),           # 下框
        op_fill('poly', [(15, 12), (33, 12), (24, 24)], AMBER),  # 上砂锥
        op_fill('poly', [(15, 36), (33, 36), (24, 24)], AMBER),  # 下砂锥
    ]


def s_hand():
    fist = [(17, 24), (31, 24), (35, 28), (35, 37), (31, 41), (17, 41), (13, 37), (13, 28)]
    ops = outlined(fist)
    ops += outlined([(21, 12), (25, 12), (25, 26), (21, 26)])            # 食指
    ops += [op_fill('poly', [(13, 27), (18, 21), (22, 24), (17, 31)], DARK)]  # 拇指
    return ops


def s_cross():
    return [
        op_fill('poly', rect(22, 6, 26, 20), DARK),   # 上臂
        op_fill('poly', rect(22, 28, 26, 42), DARK),  # 下臂
        op_fill('poly', rect(6, 22, 20, 26), DARK),   # 左臂
        op_fill('poly', rect(28, 22, 42, 26), DARK),  # 右臂
        op_fill('circle', (24, 24, 2.2), DARK),       # 中心点
    ]


def s_no():
    # PNG-only：红环 + 白斜杠，靠 erase 抠中心（SVG 不支持这种镂空，故不用于 SVG 槽）
    return [
        op_fill('circle', (24, 24, 17), RED),
        op_erase('circle', (24, 24, 11)),
        op_fill('poly', [(16, 10), (36, 30), (32, 34), (12, 14)], LIGHT),
    ]


def s_move():
    return [
        op_fill('poly', rect(22, 12, 26, 36), DARK),               # 竖干
        op_fill('poly', rect(12, 22, 36, 26), DARK),               # 横干
        op_fill('poly', [(24, 6), (31, 15), (17, 15)], DARK),       # 北箭头
        op_fill('poly', [(24, 42), (31, 33), (17, 33)], DARK),       # 南箭头
        op_fill('poly', [(6, 24), (15, 17), (15, 31)], DARK),       # 西箭头
        op_fill('poly', [(42, 24), (33, 17), (33, 31)], DARK),       # 东箭头
    ]


def s_up():
    return outlined([(24, 6), (36, 24), (28, 24), (28, 40), (20, 40), (20, 24), (12, 24)])


def s_help():
    ops = outlined([(12, 8), (12, 30), (18, 25), (22, 34), (25, 33), (21, 24), (27, 24)])
    ops += [op_fill('circle', (35, 34, 4.5), DARK),
            op_fill('circle', (35, 34, 2.2), LIGHT)]  # 帮助标记（? 的实心替形，见 U8）
    return ops


def s_appstarting():
    ops = outlined([(10, 8), (10, 28), (16, 24), (20, 32), (23, 31), (19, 23), (25, 23)])
    for cx in (28, 34, 40):
        ops.append(op_fill('circle', (cx, 38, 2.2), DARK))  # 后台忙碌三点
    return ops


def s_sizewe():
    return [
        op_fill('poly', rect(10, 22, 38, 26), DARK),          # 水平干
        op_fill('poly', [(4, 24), (16, 14), (16, 34)], DARK),  # 西头
        op_fill('poly', [(44, 24), (32, 14), (32, 34)], DARK),  # 东头
    ]


def s_sizens():
    return [
        op_fill('poly', rect(22, 10, 26, 38), DARK),          # 垂直干
        op_fill('poly', [(24, 4), (14, 16), (34, 16)], DARK),  # 北头
        op_fill('poly', [(24, 44), (14, 32), (34, 32)], DARK),  # 南头
    ]


def s_sizenw():
    # 反对称：NW 端实心大箭头，SE 端仅渐细短尾（U19 夹具）
    return [
        op_fill('poly', [(4, 4), (20, 4), (4, 20)], DARK),            # 实心 NW 头
        op_fill('poly', [(6, 16), (16, 6), (40, 30), (30, 40)], DARK),  # 渐细尾巴
    ]


def s_sizene():
    # 反对称：NE 端实心大箭头，SW 端仅渐细短尾
    return [
        op_fill('poly', [(44, 4), (28, 4), (44, 20)], DARK),            # 实心 NE 头
        op_fill('poly', [(42, 16), (32, 6), (8, 30), (18, 40)], DARK),   # 渐细尾巴
    ]


def s_nwpen():
    return [
        op_fill('poly', [(24, 26), (34, 16), (42, 24), (32, 34)], DARK),  # 笔身
        op_fill('poly', [(24, 26), (32, 34), (14, 40)], DARK),            # 笔尖（左下）
    ]


# stem -> (format, builder)。PNG 槽 + SVG 槽；arrow 额外 .svg 造冲突。
PNG_SLOTS = {
    'arrow': s_arrow, 'ibeam': s_ibeam, 'wait': s_wait, 'hand': s_hand,
    'cross': s_cross, 'no': s_no, 'move': s_move,
}
SVG_SLOTS = {
    'arrow': s_arrow,  # 冲突对（与 arrow.png 同槽）
    'up': s_up, 'help': s_help, 'appstarting': s_appstarting,
    'sizewe': s_sizewe, 'sizens': s_sizens, 'sizenw': s_sizenw, 'sizene': s_sizene,
    'nwpen': s_nwpen,
}


# ---- 栅格化（even-odd scanline）----
def circle_poly(cx, cy, r, n=72):
    import math
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n))
            for i in range(n)]


def geom_poly(op):
    if op['k'] == 'circle':
        cx, cy, r = op['g']
        return circle_poly(cx, cy, r)
    return op['g']


def fill_mask(img, pts, val):
    if len(pts) < 3:
        return
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    y0 = max(0, int(min(ys)))
    y1 = min(H - 1, int(max(ys)))
    n = len(pts)
    for py in range(y0, y1 + 1):
        yc = py + 0.5
        xints = []
        for i in range(n):
            ax, ay = pts[i]
            bx, by = pts[(i + 1) % n]
            if (ay <= yc < by) or (by <= yc < ay):
                t = (yc - ay) / (by - ay)
                xints.append(ax + t * (bx - ax))
        xints.sort()
        for a, b in zip(xints[0::2], xints[1::2]):
            xa = max(0, int(a + 0.5))
            xb = min(W - 1, int(b + 0.5))
            for px in range(xa, xb + 1):
                img[py][px] = val


def render_png(ops):
    img = [[TRANS for _ in range(W)] for _ in range(H)]
    for op in ops:
        pts = geom_poly(op)
        if op['a'] == 'erase':
            fill_mask(img, pts, TRANS)
        else:
            fill_mask(img, pts, op['c'])
    return img


def encode_png(img):
    raw = bytearray()
    for row in img:
        raw.append(0)  # filter type 0
        for (r, g, b, a) in row:
            raw += bytes((r, g, b, a))
    comp = zlib.compress(bytes(raw), 9)
    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data
                + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))
    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', comp)
            + chunk(b'IEND', b''))


# ---- SVG 发射（白名单子集：polygon / circle；填充色即颜色，不用 style）----
def svg_num(v):
    return ('%g' % round(float(v), 2))


def svg_shape(op):
    color = op['c']
    hexc = '#%02x%02x%02x' % color[:3]
    alpha = color[3] / 255.0
    fa = ' fill-opacity="%s"' % svg_num(alpha) if alpha < 1.0 else ''
    if op['k'] == 'circle':
        cx, cy, r = op['g']
        return '<circle cx="%s" cy="%s" r="%s" fill="%s"%s/>' % (
            svg_num(cx), svg_num(cy), svg_num(r), hexc, fa)
    pts = ' '.join('%s,%s' % (svg_num(x), svg_num(y)) for (x, y) in op['g'])
    return '<polygon points="%s" fill="%s"%s/>' % (pts, hexc, fa)


def render_svg(ops):
    body = ''.join(svg_shape(op) for op in ops if op['a'] == 'fill')
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d">%s</svg>'
            % (W, H, body))


def count_opaque(img):
    return sum(1 for row in img for px in row if px[3] != 0)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    written = []
    for stem, builder in PNG_SLOTS.items():
        ops = builder()
        img = render_png(ops)
        data = encode_png(img)
        path = os.path.join(OUT_DIR, stem + '.png')
        with open(path, 'wb') as f:
            f.write(data)
        written.append(('png', stem, len(data), count_opaque(img)))
    for stem, builder in SVG_SLOTS.items():
        ops = builder()
        data = render_svg(ops).encode('utf-8')
        path = os.path.join(OUT_DIR, stem + '.svg')
        with open(path, 'wb') as f:
            f.write(data)
        written.append(('svg', stem, len(data), None))

    # 自证：槽位覆盖 = 15，PNG+SVG 文件数与格式表一致
    slots = set(PNG_SLOTS) | set(SVG_SLOTS)
    distinct_slots_expected = 15
    print('distinct stems: %d (expected %d)' % (len(slots), distinct_slots_expected))
    assert len(slots) == distinct_slots_expected, 'stem set != 15 slots'
    n_png = sum(1 for w in written if w[0] == 'png')
    n_svg = sum(1 for w in written if w[0] == 'svg')
    assert n_png == len(PNG_SLOTS) and n_svg == len(SVG_SLOTS), 'per-channel emit short'
    for kind, stem, size, opaque in written:
        flag = '' if (opaque is None or opaque > 0) else '  <-- ZERO OPAQUE!'
        print('%-4s %-14s %6d bytes%s' % (kind, stem, size, flag))
    assert all((opaque is None or opaque > 0) for _, _, _, opaque in written), 'a PNG is fully transparent'
    print('OK: wrote %d files (%d png + %d svg) into skin/' % (len(written), n_png, n_svg))


if __name__ == '__main__':
    main()
