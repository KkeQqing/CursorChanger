# 换皮肤需要准备哪些文件（文件名 · 说明）

> 本表镜像唯一真源：`src/core/claim.cpp` 的别名表 + `src/apply/pipeline.cpp` 的扩展名扫描。
> 不要在别处另立一份命名规则；要改命名先改代码与 `DESIGN.md` §5.2，本表随之更新。

## 通用规则（命名/数量/扩展名）

- **一个槽位放一个文件**即可；文件名去掉扩展名的部分（stem）决定它认领哪个指针槽位。
- **大小写不敏感**；名字里的 `-`、`.`、空格 都被当作 `_`（`top-left-corner`＝`top_left_corner`）。
- **扩展名决定用哪条通道**：`.png` / `.svg` / `.cur`（同一个 stem 三选一即可，不必都给）。
- **数量**：最多 15 个文件（15 槽各一张）；少放就是部分皮肤，缺的槽保持系统原样，属正常。
- **同槽多张**（如 `arrow.png` 和 `arrow.svg` 都在）：程序按名排序取第一张、其余**报告**不静默；想稳定就一槽一张。
- 认不出来的文件名会被报告并忽略，不影响其它槽。

## 文件名 ↔ 说明（15 个槽位）

| 可用文件名（任选其一，接 `.png`/`.svg`/`.cur`） | 说明 |
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

## 热点（扎点）说明

- 每个槽位的热点由**程序内置的默认规则**决定，**目前不能通过文件覆盖**。
- 设计文档 §5.3 设想的 `<stem>.meta`（写 `hotspot = x,y`）**尚未实现**，放了也不会被读取——请不要依赖它。
