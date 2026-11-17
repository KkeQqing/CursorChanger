# ANI-GUIDE — 动画光标（.ani）使用指南

> 面向使用者的一页文档。所有结论都来自当前代码与 DESIGN 的已核销项；
> 「未验」的边界如实标出，不夸大。事实来源标注在「实现速查」一节，便于核对。

## 1. 支持范围：哪些槽能动画

**15 个 v1 槽全部支持动画**。动画能力不取决于槽，取决于最终写进注册表的文件是 `.cur` 还是 `.ani`
（Windows 自己也是这么用的：aero 主题的 Wait/AppStarting 原生就是 `.ani`，普通指针是 `.cur`）。

| 常见用法 | 槽 | 认领名（文件名 stem，不区分大小写） |
|---|---|---|
| 忙等转圈 | Wait | `wait` / `busy` / `loading` |
| 后台运行带箭头 | AppStarting | `appstarting` / `working_busy` |
| 任何其它槽的动画 | 如 Arrow | 对应别名（`arrow` / `normal` …）同样有效 |

真机已确证的两条路（不是"加载成功"，是人眼看到动）：
- **透传**：把现成 `.ani` 应用到槽上，系统按动画播放（V0.6 用 `aero_working.ani` 实测，箭头转圈）。
- **合成**：用多张 png/svg 帧自制 `.ani`，按 `rate` 播放（M6 用 `rate=10` 两帧实测，约 6 次/秒交替）。

## 2. 两条路径，二选一

### 路径 A · 透传（拿到现成 .ani，最省事）——2 个文件

```
skin\busy.ani          ← 现成的动画光标文件（RIFF/ACON 容器），stem 认领 Wait 槽
skin\busy.meta         ← 可选，只写来源备注；动画参数是 .ani 自带的，不生效
```

规则：
- **原样搬运，绝不重编码**；合法性由系统加载器在 L1 校验，**坏 `.ani` 会挡在写注册表之前**，不会进系统。
- `.meta` 里的 `size` / `hotspot` / `frames` / `rate` 对 `.ani` 文件**不生效**（`.ani` 走透传，参数在它容器里）。

### 路径 B · 合成（用 png/svg 帧自己做动画）——主文件 + .meta + 每帧 1 个文件

```
skin\wait.png          ← 主文件：只用于"认领槽位"（身份由它的 stem 决定），也是缺省第 1 帧
skin\wait.meta         ← 必需：声明帧序列与速率
skin\wait_f1.png       ← 帧 1（png 或 svg 均可，见 .meta 的列表）
skin\wait_f2.png       ← 帧 2
skin\wait_f3.png       ← 帧 3 …（帧数不限，实用一般 4~12）
```

`wait.meta` 示例（UTF-8，制表符/空格围绕 `=` 都可，`;` 后是注释）：

```ini
frames = wait_f1.png, wait_f2.png, wait_f3.png   ; 按此顺序播放；相对本 .meta 所在目录
rate   = 6                                       ; 每帧停留，单位 jiffies = 1/60 秒（6 ≈ 0.1s/帧）
hotspot = 16,16                                  ; 可选，以主文件源尺寸坐标给，逐帧套用
size    = 32, 48                                 ; 可选，覆盖输出档位（默认 16,32,48）
```

规则（全部是代码实测）：
- 有 `frames=` ⇒ 该槽改走合成，产出 `.ani`；主文件**必须是 png/svg**（`.cur`/`.ani` 上写 `frames=` 会报错拒产）。
- **帧文件只能是 png/svg**；`.meta` 里没列的帧不会被用；列了但文件缺失 ⇒ 该槽失败、挡在写注册表前。
- `rate` 单位是 **jiffies（1/60 秒）不是毫秒**；不给默认 6（settings.ini 里规划的 `ani.default_rate_jiffies` 键**当前代码未实装**，默认值硬编码为 6，见「实现速查」）。
- 帧内嵌的是**完整单帧 `.cur`**（`bfAttributes` bit0 模式），已实测被 OS 采纳并按 `iDispRate` 播放。

## 3. 替换 / 改动的正确姿势

| 想做什么 | 动哪个文件 |
|---|---|
| 换动画内容 | 路径 A：直接替换 `busy.ani`；路径 B：替换帧文件或改 `frames=` 列表 |
| 改播放速度 | 路径 B 改 `.meta` 的 `rate`（数值 × 1/60 秒）；路径 A 需换 `.ani`（参数在容器里） |
| 改热点 | `.meta` 的 `hotspot = x,y`（源坐标，仅 png/svg/合成路径生效） |
| 改输出尺寸档 | `.meta` 的 `size`（只下采样不放大），或 `settings.ini` 的 `frames.sizes` |
| 某槽从动画换回静态 | 删掉该槽的 `.ani` / `.meta(frames=)`，留普通 `stem.png/.svg/.cur` 即可 |

同一槽被多个 stem 认领（如 `busy.ani` 与 `wait.png` 都在）⇒ **扫描会如实报冲突并任选首个**，不会静默混用：删掉不要的那个即可。

应用后 watcher 开着（V0.8 的自动生效）时，改完文件会**自动重放 apply**，无需手动点应用。

## 4. 边界与未验项（如实）

- **OS 真实上限未测**（U7 残留）：帧数/单帧尺寸的 Windows 策略上限未逐格逼近。代码里 `max_frames=1000` 只是防失控硬闸，触顶**报错拒产出、不静默截断**。实用帧数（≤12）远低于任何已知限制。
- 透传 `.ani` 的 `aero_busy speed=0` 场景（速率交给系统默认）未专门测。
- 动画生效与否的**最终判据是人眼**：L3 只采单帧，测不出"在转"。apply 后请悬到对应场景看一眼。

## 5. 需要多少文件（速查）

| 方式 | 必需 | 可选 |
|---|---|---|
| 透传 1 个动画槽 | `stem.ani` ×1 | `stem.meta` ×1（仅备注用） |
| 合成 1 个动画槽 | `stem.png\|svg` ×1 + `stem.meta` ×1 + 帧文件 ×N | — |
| 整套皮肤 15 槽全静态 | 每槽 1 个文件（≈15） | — |
| 静态 + 混合动画 | 静态槽同上；动画槽按上面两行叠加 | 帧文件可被多槽复用（列表引用即可） |

## 6. 实现速查（核对用，均指当前 HEAD）

- 透传：`src/apply/pipeline.cpp` `ExtKind::Ani` 分支（原样搬运）；负例守护 `src/tests/tests_m10.cpp`。
- 合成：`pipeline.cpp` `sc.has_frames` 段（帧解码→逐帧 .cur→封装）；容器读写 `src/core/ani_format.{h,cpp}`；端到端正/负例 `src/tests/tests_m12.cpp`。
- sidecar 语法与键：`src/core/sidecar.h`（`frames` / `rate`=jiffies / `hotspot` / `size` / `trim`；未知键报错）。
- 认领别名：`src/core/claim.cpp`（`busy`/`loading`→Wait，`working_busy`→AppStarting 等）。
- 扫描接受扩展名：`.cur` `.png` `.svg` `.ani` 四种（`pipeline.cpp` 扫描 glob）。
- 真机核销记录：U7 透传播放、U22 合成按 `iDispRate` 播放（`docs/DESIGN.md` §13 对应行）。
- **文档与实现的偏差（如实）**：DESIGN §10 登记的 `ani.default_rate_jiffies` / `ani.max_frames` / `ani.max_dimension` **未在 settings.ini 实装**——解析器（`settings.cpp`，白名单式）只认 `frames.sizes` 与 `watcher.debounce_ms` / `watcher.max_wait_ms`，其它键（含这三个 `ani.*`）会按 `UnknownKey` 报错。默认速率 6 与硬闸 1000 帧目前**硬编码**于 `pipeline.cpp` / `ani_format.h`。
