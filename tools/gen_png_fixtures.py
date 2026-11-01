#!/usr/bin/env python3
"""tools/gen_png_fixtures.py — 用纯标准库造 PNG 测试夹具（不依赖 Pillow）。

为什么自己写：
  * 本机 Pillow 不一定装得到，且 pip 返回码不可信（本机已知坑）；
  * 夹具是「PNG -> 预乘 BGRA」这条链的**唯一真值来源**，若用被测同一侧的库生成，
    golden 就成了实现自比。这里按 PNG 规范独立编码，C++ 侧解码结果必须与之吻合。

生成的每一张图都在 fixtures.json 里记录：尺寸、color type、原始 RGBA、以及
    **手算好的预乘结果**（半透明像素用整数 round，和 C++ 侧同一套舍入规则）。

真值来源说明（本轮实测得到）：不要用 GDI+ / System.Drawing 的 GetPixel 当 alpha 图的真值 ——
它内部按**预乘**存储，取像素时再反预乘，会引入舍入损失。本机实测：写入 straight
(200,150,100, a=128) 的 PNG，GDI+ 回读成 R199 G149 B99（200 -> 100.4 -> 199.2），差 1。
因此本文件的 premul_rgba 是唯一真值，C++ 解码结果必须与它逐字节相等；
拿 GDI+ 当参照只会得到「差 1」的假失败。
"""
import json
import os
import struct
import zlib

REPO = os.path.dirname(os.path.abspath(os.path.join(__file__, "..")))
OUT_DIR = os.path.join(REPO, "tests", "fixtures")


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path, w, h, rgba_rows, color_type=6):
    """rgba_rows: top-down list of (r,g,b[,a])。color_type 6=RGBA, 2=RGB(无 alpha)。"""
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter type 0 = None
        for x in range(w):
            px = rgba_rows[y][x]
            r, g, b = px[0], px[1], px[2]
            if color_type == 6:
                a = px[3] if len(px) > 3 else 255
                raw += bytes((r, g, b, a))
            else:
                raw += bytes((r, g, b))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, color_type, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def premultiply(r, g, b, a):
    """straight -> premultiplied，整数四舍五入。C++ 侧必须用同一公式，否则逐字节比对必红。"""
    def one(v):
        return (v * a + 127) // 255 if a else 0
    return (one(r), one(g), one(b), a)


def solid(w, h, px):
    return [[px for _ in range(w)] for _ in range(h)]


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    fixtures = []

    # 1) 半透明灰：唯一能暴露「忘预乘」的形状（通道值 > alpha）
    a = 128
    grey = (200, 150, 100, a)
    write_png(os.path.join(OUT_DIR, "semi_grey_4x4.png"), 4, 4, solid(4, 4, grey))
    fixtures.append({
        "file": "semi_grey_4x4.png", "w": 4, "h": 4, "color_type": 6,
        "src_rgba": grey, "premul_rgba": premultiply(*grey),
        "why": "通道 > alpha：不预乘就会被 IR 的 <=alpha 判据抓到",
    })

    # 2) 全不透明：alpha=255 时预乘是恒等 —— 单靠它测不出预乘（U13 的漏检面）
    white = (255, 255, 255, 255)
    write_png(os.path.join(OUT_DIR, "opaque_white_8x8.png"), 8, 8, solid(8, 8, white))
    fixtures.append({
        "file": "opaque_white_8x8.png", "w": 8, "h": 8, "color_type": 6,
        "src_rgba": white, "premul_rgba": premultiply(*white),
        "why": "记录用途边界：这张图预乘前后相同，不能用来证明预乘",
    })

    # 3) 非对称图案：抓行序（PNG 是 top-down，容器是 bottom-up）
    rows = []
    for y in range(4):
        row = []
        for x in range(4):
            v = 255 if (x + y) % 2 == 0 else 0
            row.append((v, 255 - v, 128, 255))
        rows.append(row)
    write_png(os.path.join(OUT_DIR, "checker_4x4.png"), 4, 4, rows)
    fixtures.append({
        "file": "checker_4x4.png", "w": 4, "h": 4, "color_type": 6,
        "src_rgba": None, "premul_rgba": None,
        "why": "棋盘 + 逐像素不同：上下颠倒一定测得出",
    })

    # 4) 无 alpha 的 RGB 图：走 §4.2「alpha 全 255、AND 全 0」，不做亮度抠图
    orange = (255, 140, 0, 255)
    write_png(os.path.join(OUT_DIR, "rgb_no_alpha_4x4.png"), 4, 4,
              solid(4, 4, (255, 140, 0)), color_type=2)
    fixtures.append({
        "file": "rgb_no_alpha_4x4.png", "w": 4, "h": 4, "color_type": 2,
        "src_rgba": (255, 140, 0), "premul_rgba": (255, 140, 0, 255),
        "why": "无 alpha 通道 -> 视为完全不透明",
    })

    # 5) 全透明：必须被 H10 拒收
    trans = (0, 0, 0, 0)
    write_png(os.path.join(OUT_DIR, "fully_transparent_4x4.png"), 4, 4, solid(4, 4, trans))
    fixtures.append({
        "file": "fully_transparent_4x4.png", "w": 4, "h": 4, "color_type": 6,
        "src_rgba": trans, "premul_rgba": trans,
        "why": "负对照输入：解码可以成功，但进 IR 必须被拒",
    })

    manifest = {"note": "由 tools/gen_png_fixtures.py 生成；premul_rgba 用 (v*a+127)//255",
                "fixtures": fixtures}
    with open(os.path.join(OUT_DIR, "fixtures.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)

    for fx in fixtures:
        p = os.path.join(OUT_DIR, fx["file"])
        print("WROTE %-30s %6d bytes  ct=%d" % (fx["file"], os.path.getsize(p), fx["color_type"]))
    print("manifest fixtures.json", os.path.getsize(os.path.join(OUT_DIR, "fixtures.json")), "bytes")


if __name__ == "__main__":
    main()
