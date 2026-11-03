# 换皮肤需要准备哪些文件（文件名 · 说明）

> 本表镜像唯一真源：`src/core/claim.cpp` 的别名表 + `src/apply/pipeline.cpp` 的扩展名扫描。
> 不要在别处另立一份命名规则；要改命名先改代码与 `DESIGN.md` §5.2，本表随之更新。

## 通用规则（命名/数量/扩展名）

- **一个槽位放一个文件**即可；文件名去掉扩展名的部分（stem）决定它认领哪个指针槽位。
- **大小写不敏感**；名字里的 `-`、`.`、空格 都被当作 `_`（`top-left-corner`＝`top_left_corner`）。
- **扩展名决定用哪条通道**：`.png` / `.svg` / `.cur` / `.ani`（同一 stem 取其一即可）。前三种会转成 `.cur`；`.ani` 只做**透传**（原样应用，见下方「动画」）。
- **数量**：最多 15 个文件（15 槽各一张）；少放就是部分皮肤，缺的槽保持系统原样，属正常。
- **同槽多张**（如 `arrow.png` 和 `arrow.svg` 都在）：程序按名排序取第一张、其余**报告**不静默；想稳定就一槽一张。
- 认不出来的文件名会被报告并忽略，不影响其它槽。

## 文件名 ↔ 说明（15 个槽位）

| 可用文件名（任选其一，接 `.png`/`.svg`/`.cur`/`.ani`） | 说明 |
|---|---|
| `arrow` · `normal` | 正常选择（默认箭头） |
| `ibeam` · `text` · `beam` | 文本选择（I 形光标） |
| `wait` · `busy` · `loading` | 忙（等待/忙碌） |
| `up` · `alternate` · `uparrow` | 替代选择 |
| `help` · `question` · `question_arrow` | 帮助选择 |
| `hand` · `link` · `pointer` | 链接选择（手型） |
| `appstarting` · `working_busy` | 后台运行 |
| `cross` · `crosshair` · `precision` | 精确选择（十字） |
| `no` · `not_allowed` · `denied` · `unavailable` | 不可用 |
| `move` · `sizeall` | 移动 |
| `ew_resize` · `sizewe` | 水平调整 |
| `ns_resize` · `sizens` | 垂直调整 |
| `nw_resize` · `sizenw` · `se_resize` · `sizese` · `top_left_corner` · `bottom_right_corner` | 左上↔右下 对角调整 |
| `ne_resize` · `sizene` · `sw_resize` · `sizesw` · `top_right_corner` · `bottom_left_corner` | 右上↔左下 对角调整 |
| `nwpen` | 手写笔 |

> 注：Windows 实测只有 **SizeNWSE / SizeNESW 两个**对角槽（不是一个方向一个），所以上面两行各收 6 个别名指向同一个槽；把某方向的图（如 `se_resize.png`）放进去也会被认到 SizeNWSE。

## 动画（`.ani`）说明

两种做法都支持：
1. **透传现成动画**：把已有的 `.ani` 按槽名放进来（如 `wait.ani`、`busy.ani`），程序原样应用到该槽（不重编码）。速率由那个 `.ani` 自身决定。
2. **用序列帧合成动画**：在该槽的 `<stem>.meta` 里写 `frames=` + `rate=`，程序把这些帧合成一个动画 `.ani` 应用到该槽。例（`wait.meta`）：
   ```ini
   frames = w0.png,w1.png,w2.png    ; 帧文件名，逗号分隔，相对本目录，按此顺序播放
   rate = 6                          ; 每帧停留，单位 jiffies = 1/60 秒（6≈100ms/帧，约 10 次/秒）
   ```
   - `rate` 越小越快：**填的是 1/60 秒，不是毫秒**（想 ~6 次/秒填 10，想很快填 2~3；按毫秒填会慢得像卡住）。
   - 帧本身仍是普通 `.png`/`.svg`（各自可再带 hotspot/size 覆盖）；缺帧文件会报错、不会写入系统。

## 热点 / 尺寸 / 贴边（`.meta` sidecar）

同目录放一个 `<槽位名>.meta`（如 `arrow.meta`）即可覆盖默认参数（只承载参数，不决定身份）：
```ini
hotspot = 8,8          ; 以“源图尺寸”为坐标的扎点，逐帧自动换算
size = 16,24,48        ; 覆盖默认档位 {16,32,48}；只下采样不放大
trim = auto            ; 仅 SVG：自动贴去透明边
frames = f0.png,f1.png ; 仅动画：合成 .ani 的帧序列（见上）
rate = 6               ; 仅动画：每帧 jiffies
```
未知 key 或坏值会**报错并挡在写入系统之前**（不会静默忽略）。`hotspot` 越界同样报错。
