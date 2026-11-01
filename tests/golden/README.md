# tests/golden/

字节级基线。**不入库生成脚本的产物而不留验证路径** —— 本目录每个夹具都必须在
`README.md` 里写明：谁生成、怎么重生成、期望指纹。

## cursor_4x4_8x8_premult.cur

| 字段 | 值 |
|---|---|
| 用途 | M1 容器层 golden：`write_cursor_bytes` 的逐字节对照物 |
| 生成器 | `tools/gen_golden.py`（**独立实现**，不是被测的 C++ writer） |
| 重新生成 | `python tools\gen_golden.py`（幂等；内容变了会报 `OVERWRITTEN`） |
| 字节数 | 1974 |
| sha256 | `79642A13934A64D39DAF36AD4AF64E14D8871BC68F8BDE5DDA2B7E994C7D0B8D` |
| FNV-1a 64 | `0xE54CE2E80CCF5B9A`（= 16522830619190713242，测试侧自证用） |

结构（小端）：

```
ICONDIR  : reserved=0, type=2, count=2
entry[0] : 4x4   hotspot(1,1)  bytes_in_res=616   offset=38
entry[1] : 8x8   hotspot(1,1)  bytes_in_res=1320  offset=654
帧 i     : BITMAPINFOHEADER(biHeight=2h, biWidth=真实值, bitCount=32, BI_RGB)
           + XOR 底向上 (w*h*4)
           + AND 掩码（每行 max(对齐值,128)，32bpp 下全 0）
```

像素内容与行序约定见 `docs/DESIGN.md` §5.1 / §6（H1-H11）。

## 为什么 golden 不由被测实现自己生成

自己生成再自己比，等于把实现里的错误固化成「基线」，golden 从此只能回答
「有没有改动」，永远回答不了「对不对」。因此：

1. `tools/gen_golden.py` 按 §6 的布局**独立**编码；
2. 同一脚本内另有**分开写的第二套解码规则** `check()`，只负责挑错，不过就不落盘；
3. 生成能力本身也做过负对照：把 `check()` 里的 `biHeight != 2h` 弱化后重跑，
   它必须拒绝落盘（实测：报出两帧的 `biHeight != 2h (H2)`，退出码 1）；
4. C++ 侧 `writer_output_equals_independent_golden` 是三套实现的相遇点。

## 已知不覆盖项（M2 才核销）

- U12：hotspot 落在目录项的哪两个 WORD，`LoadCursorFromFileW` 是否真按此解释
- `min_dir_gap_bytes=128` 的 AND 行过填充，系统加载器是否照单全收
