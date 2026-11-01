#!/usr/bin/env python3
"""tools/gen_golden.py — 用**独立实现**生成 CUR golden 夹具。

为什么不用 C++ writer 自己产出 golden：
    那等于「实现自己和自己比」，任何实现错误都会被固化成基线，
    golden 测试从此只测「有没有改动」，永远测不到「对不对」——
    这正是 DESIGN §6 的 H2/H3/H11 三类错误最喜欢藏身的地方。
所以本文件按 cur_format.h 注释里的字节布局**独立**编码一遍，
再用与编码规则分开写的第二套解码规则验证，最后才落盘。
C++ 侧的 golden 测试因此是两个独立实现的一致性对照。

参数默认值须与 DESIGN §10 / cur_writer.h 的 WriteOptions 同步：
    max_dir_entries=12, min_frame_bytes=128, min_dir_gap_bytes=64
"""
import hashlib
import json
import os
import sys

REPO = os.path.dirname(os.path.abspath(os.path.join(__file__, "..")))
OUT_DIR = os.path.join(REPO, "tests", "golden")

DIR_HEADER_BYTES = 6
DIR_ENTRY_BYTES = 16
DIB_BYTES = 40
ICON_TYPE_CURSOR = 2
MAX_DIMENSION = 256
MIN_AND_ROW_BYTES = 128      # writer.min_frame_bytes
MIN_ENTRY_BYTES = 64         # writer.min_dir_gap_bytes

# ---------- 夹具规格（top-down，行 0 在最上；BGRA 且预乘） ----------
SPEC = {
    "hotspot": [1, 1],
    "frames": [
        {
            "name": "4x4",
            "width": 4,
            "height": 4,
            # 非对称图案：上 3 行不透明、底行半透明、col0 全透明、(2,1) 半透明
            # —— 只有非对称才能同时抓住底向上/底向上偏移类错误
            "rows": [
                [[0, 0, 0, 0],       [255, 0, 0, 255], [128, 128, 128, 128], [0, 0, 255, 255]],
                [[0, 0, 0, 0],       [0, 255, 0, 255], [255, 255, 255, 255], [128, 0, 0, 255]],
                [[0, 0, 0, 0],       [64, 32, 16, 128], [0, 255, 255, 255],  [255, 0, 255, 255]],
                [[0, 0, 0, 0],       [0, 0, 0, 128],   [0, 0, 0, 64],        [0, 0, 0, 32]],
            ],
        },
        {
            "name": "8x8",
            "width": 8,
            "height": 8,
            # 半透明纯灰：premultiplied 下 BGR == A == 128
            "solid": [128, 128, 128, 128],
        },
    ],
}


def xor_row_bytes(w):
    return w * 4


def and_row_bytes(w):
    exact = ((w + 31) // 32) * 4
    return max(exact, MIN_AND_ROW_BYTES)


def frame_rows_in_res(w, h):
    raw = DIB_BYTES + xor_row_bytes(w) * h + and_row_bytes(w) * h
    return max(raw, MIN_ENTRY_BYTES)


def frame_top_down(w, h, rows=None, solid=None):
    """把一帧展开成 top-down 的 (h, w, 4) 像素序列。"""
    if rows is not None:
        assert len(rows) == h, "row count mismatch"
        out = []
        for r in rows:
            assert len(r) == w, "column count mismatch"
            out.extend(list(p) for p in r)
        return out
    assert solid is not None
    return [list(solid) for _ in range(w * h)]


def build(spec):
    hx, hy = spec["hotspot"]
    frames = spec["frames"]

    entries = []      # (w, h, top_down_pixels)
    for f in frames:
        w, h = f["width"], f["height"]
        assert 0 < w <= MAX_DIMENSION and 0 < h <= MAX_DIMENSION
        assert hx < w and hy < h, "hotspot out of bounds"
        px = frame_top_down(w, h, f.get("rows"), f.get("solid"))
        assert len(px) == w * h
        # 预乘必要条件：每通道 <= alpha
        for b, g, r, a in px:
            assert b <= a and g <= a and r <= a, "not premultiplied"
        # 至少一个可见像素（H10）
        assert any(p[3] != 0 for p in px), "fully transparent"
        entries.append((w, h, px))

    # 面积升序（H8）
    areas = [w * h for w, h, _ in entries]
    assert areas == sorted(areas), "frames not ascending by area"
    assert len(entries) <= 12, "too many frames"

    blob = bytearray()
    blob += (0).to_bytes(2, "little")
    blob += ICON_TYPE_CURSOR.to_bytes(2, "little")
    blob += len(entries).to_bytes(2, "little")

    def size_byte(v):
        return 0 if v == MAX_DIMENSION else v

    offset = DIR_HEADER_BYTES + DIR_ENTRY_BYTES * len(entries)
    dir_rows = []
    for (w, h, _), n in zip(entries, [frame_rows_in_res(w, h) for w, h, _ in entries]):
        dir_rows.append((w, h, n, offset))
        offset += n

    for (w, h, _), (dw, dh, n, off) in zip(entries, dir_rows):
        blob += bytes([size_byte(w), size_byte(h), 0, 0])
        blob += hx.to_bytes(2, "little")
        blob += hy.to_bytes(2, "little")
        blob += n.to_bytes(4, "little")
        blob += off.to_bytes(4, "little")

    for (w, h, px), (_, _, n, _) in zip(entries, dir_rows):
        and_bytes = and_row_bytes(w) * h
        blob += DIB_BYTES.to_bytes(4, "little")
        blob += w.to_bytes(4, "little")
        blob += (2 * h).to_bytes(4, "little", signed=True)   # H2
        blob += (1).to_bytes(2, "little")                    # planes
        blob += (32).to_bytes(2, "little")                   # bit count
        blob += (0).to_bytes(4, "little")                    # BI_RGB
        blob += (xor_row_bytes(w) * h).to_bytes(4, "little")
        blob += (0).to_bytes(4, "little") * 4                # ppm x/y, colors used/important
        for y in range(h - 1, -1, -1):                       # 底向上（H11）
            for x in range(w):
                blob += bytes(px[y * w + x])
        blob += bytes(and_bytes)                             # AND 全 0（H3）

    return bytes(blob), dir_rows


def check(blob, spec):
    """与 build() 分开写的第二套解码规则：只负责挑错。"""
    problems = []
    if len(blob) < DIR_HEADER_BYTES:
        return ["file shorter than header"]
    if int.from_bytes(blob[0:2], "little") != 0:
        problems.append("header reserved != 0")
    if int.from_bytes(blob[2:4], "little") != ICON_TYPE_CURSOR:
        problems.append("type != 2")
    count = int.from_bytes(blob[4:6], "little")
    if count == 0:
        problems.append("count == 0")
    dir_end = DIR_HEADER_BYTES + DIR_ENTRY_BYTES * count
    if len(blob) < dir_end:
        problems.append("truncated directory")
        return problems

    expect_off = dir_end
    for i in range(count):
        # 目录项紧跟 6 字节头（起点是 DIR_HEADER_BYTES，不是 dir_end —— dir_end 是图像数据起点）
        s = DIR_HEADER_BYTES + DIR_ENTRY_BYTES * i
        e = blob[s: s + DIR_ENTRY_BYTES]
        w = e[0] or MAX_DIMENSION
        h = e[1] or MAX_DIMENSION
        hx, hy = int.from_bytes(e[4:6], "little"), int.from_bytes(e[6:8], "little")
        n = int.from_bytes(e[8:12], "little")
        off = int.from_bytes(e[12:16], "little")
        if hx >= w or hy >= h:
            problems.append("frame %d hotspot out of bounds" % i)
        if off != expect_off:
            problems.append("frame %d offset not contiguous" % i)
        if off + n > len(blob):
            problems.append("frame %d runs past EOF" % i)
            return problems
        hdr = blob[off: off + DIB_BYTES]
        if int.from_bytes(hdr[0:4], "little") != DIB_BYTES:
            problems.append("frame %d biSize != 40" % i)
        if int.from_bytes(hdr[8:12], "little", signed=True) != 2 * h:
            problems.append("frame %d biHeight != 2h (H2)" % i)
        if int.from_bytes(hdr[4:8], "little", signed=True) != w:
            problems.append("frame %d biWidth != dir width" % i)
        if int.from_bytes(hdr[12:14], "little") != 1:
            problems.append("frame %d planes != 1" % i)
        if int.from_bytes(hdr[14:16], "little") != 32:
            problems.append("frame %d bitCount != 32" % i)
        if int.from_bytes(hdr[16:20], "little") != 0:
            problems.append("frame %d compression != BI_RGB" % i)
        and_region = blob[off + DIB_BYTES + xor_row_bytes(w) * h: off + n]
        if any(b != 0 for b in and_region):
            problems.append("frame %d AND mask not all zero (H3)" % i)
        expect_off = off + n

    if expect_off > len(blob):
        problems.append("directory claims more data than present")
    return problems


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    blob, dir_rows = build(SPEC)

    problems = check(blob, SPEC)
    if problems:
        print("REFUSING to write golden; independent checker says:")
        for p in problems:
            print("   -", p)
        return 1

    name = "cursor_4x4_8x8_premult.cur"
    path = os.path.join(OUT_DIR, name)
    old = open(path, "rb").read() if os.path.exists(path) else None
    with open(path, "wb") as f:
        f.write(blob)

    digest = hashlib.sha256(blob).hexdigest().upper()
    action = "unchanged" if old == blob else ("created" if old is None else "OVERWRITTEN")
    print(json.dumps({
        "file": os.path.relpath(path, REPO).replace("\\", "/"),
        "bytes": len(blob),
        "sha256": digest,
        "action": action,
        "frames": [{"w": w, "h": h, "bytes_in_res": n, "offset": o} for w, h, n, o in dir_rows],
        "hotspot": SPEC["hotspot"],
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
