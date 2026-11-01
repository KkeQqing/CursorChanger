# CursorSwitch 设计文档

版本：**V0.0**（设计阶段）｜日期：2026-11-01｜状态：待 M0 启动
真源地位：本文件是设计唯一真源。改需求的硬顺序是 **§3 决策表 → §5/§4 契约 → 才动代码**；任何代码不得与本文冲突，冲突时以实现错为准并回写本文。

---

## 0. 阅读顺序与文档分层

| 文件 | 职责 |
|---|---|
| `AGENTS.md` | 操作规程（薄）：提交/测试/前缀/总结纪律 + 导航 |
| `docs/DESIGN.md` | 本文件：目标、决策、契约、字节级要求、判据、风险 |
| `docs/summaries/V*.md` | 里程碑总结（附同名 tag，回滚锚点） |
| `docs/BACKLOG.md` | 未纳入 v1 的想法，只进不出，不直接实施 |
| `docs/BLOCKED.md` | 阻塞项（含需实测但未实测的外部事实） |

---

## 1. 目标与非目标

**目标**

单个 exe + 同级 `skin/` 文件夹。把素材按文件名认领到 Windows 系统指针槽位，写 HKCU 并全局生效，一键应用 / 一键还原。**三条输入通道并存**：

1. `.cur` 直接应用（passthrough，不重编码）
2. `PNG` → `.cur`（解码 + 预乘 + 缩放 + 封装）
3. `SVG` → `.cur`（光栅化 + 同上）

交付形态是**窗口程序**；同一条业务链必须同时可由 CLI 参数驱动（见 §9 可验证性）。

**非目标（v1 明确排除，不做即不测）**

| 排除项 | 原因 |
|---|---|
| UAC 安全桌面 / 登录界面光标 | 需提权 + 写 `HKU\.DEFAULT`，风险等级不同 |
| `HKLM` 写入、全用户生效 | 同上，零注入原则的边界 |
| 动画光标 `.ani` | RIFF 另一套格式 + 系统动画光标尺寸策略上限，性价比低 |
| 目录 watcher 常驻自动生效 | 换图后需手动「重新应用」，见 D12 |
| DPI 逐档自适应素材集 | 交给系统按帧挑选，见 D8 |
| 光标包分发/商店、云同步 | 超范围 |

---

## 2. 总体架构

依赖单向，`core` 不依赖任何 Win32 符号 —— 这是「能在本机跑确定性单元测试」的前提。

```
        ┌───────────────────────── sources ─────────────────────────┐
        │  PngSource        SvgSource         CurSource             │
        │  (stb_image)      (resvg, 待实测)   (passthrough + 校验)   │
        └───────┬───────────────┬──────────────────┬────────────────┘
                ▼               ▼                  ▼
                    IR: CursorSpec（多帧 + hotspot）
                │                                    │
                ▼                                    ▼
        writers/CurWriter                     apply/（Win32 层）
        （DIB + AND 掩码，纯字节）        scheme 认领 → 落盘 → HKCU → SPI
                │                                    │
                └──────────────► selfcheck ◄─────────┘
                        （L1 文件合法性 / L2 注册表回读 / L3 生效帧像素）
                                    │
                                    ▼
                          ui/（Win32 窗口，只做输入输出与状态显示）
```

**层规则（违反即视为架构缺陷，不接受「先这样写」）**

- `sources/*` 与 `writers/*` 只碰内存字节，不读文件路径、不碰注册表、不弹对话框。
- `apply/` 是唯一允许触碰 `HKCU` 与 `SystemParametersInfo*` 的模块；`ui/` 只能通过 `apply/` 的接口发起，不得自己写注册表。
- `core` 里出现任何 `<windows.h>` 即算破窗（`CurSource` 若要直接用 `LoadCursorFromFileW` 校验，必须放在 `selfcheck/` 而不是 `sources/`）。

---

## 3. 决策表

状态列含义：`已定` = 可直接实现；`待实测` = 必须在**本机**验证后才能写死为代码事实；`需追认` = 超出实现细节，交付汇报中单列。

| 编号 | 议题 | 决定 | 理由 / 代价 | 状态 |
|---|---|---|---|---|
| D1 | 语言 | C++17，MSVC；`/std:c++17 /W4 /utf-8 /EHsc` | 零运行时依赖、单 exe。代价：alpha 与字节活儿手写 | 已定 |
| D2 | UI 框架 | 原生 Win32 + comctl32 v6，不引 Qt | 与「小 exe」一致；控件少（一个列表 + 几个按钮）够用。代价：布局手写 | 已定 |
| D3 | PNG 解码 | vendored `stb_image` | 单头文件，无依赖 | 已定 |
| D4 | SVG 光栅化 | 首选 `resvg`（C API 静态库）；**前置条件实测失败：本机无 Rust 工具链（`cargo` / `rustc` / `rustup` 均 ABSENT）**。降级候选 `nanosvg`（纯 C 头文件，无工具链依赖，但支持子集更窄 → §4.3.2 守卫必须更宽）。**选型未决，需追认**（见 BLOCKED B-01） | 理由不变：子集越窄，「渲染错了但不报错」的风险越大。代价实测：走 resvg 必须先装 Rust 工具链，且仍需验证能否产出可链静态库 + 稳定 C ABI | **已实测（前置不成立）/ 选型待追认** |
| D5 | `.cur` 通道 | **passthrough**：原样复制文件，绝不重编码 | 重编码会丢多帧/多尺寸并引入 alpha 误差；passthrough 成本≈0 且不会把好素材做坏 | 已定 |
| D6 | 中间表示 | `CursorSpec{ frames[], hotspots[], source_kind }`，像素统一为 **BGRA32 premultiplied** | 单点定义像素约定，三条通道归一 | 已定 |
| D7 | CUR 编码 | 只写 **DIB(BMP) 条目**，不写 PNG 压缩条目 | PNG-in-CUR 的合法性由系统光标加载路径决定，未证实；DIB 是无争议路径。省掉一整类不确定 | 已定（不依赖实测） |
| D8 | 帧集合 | 每素材产出 **16/32/48** 三档；`svg/cur` 若自带多尺寸则保留全部 | 最终显示尺寸由「设置→指针大小」+ DPI 决定，猜单尺寸是错的；多帧让系统挑 | 已定 |
| D9 | 槽位识别 | 文件名 stem（去后缀、去路径）经**别名表**归一，大小写不敏感；不支持按尺寸/DPI 后缀区分 | 别名表是数据，不是 if；见 §5.2 | 已定 |
| D10 | hotspot | 每槽一条默认规则（§5.3）+ sidecar `<stem>.meta` 覆盖 | 文件名只回答「我是哪个槽位」，不承载参数 —— 这条必须守住 | 已定（默认值为**需目测校正初值**） |
| D11 | 产物落盘 | `<程序目录>\applied\`（可配置），先写 `*.tmp` 再 `MoveFileExW(REPLACE_EXISTENT)` | 系统可能正持有旧文件；原子替换避免半写状态被读到 | 已定 |
| D12 | 生效触发 | v1 = 窗口「应用」按钮 / CLI `--apply`；不做 watcher | 「换图即换皮肤」需常驻进程，成本与体验另议 → BACKLOG | 已定 |
| D13 | 还原语义 | 两个独立动作：① 还原到运行前快照 ② 复位系统默认方案 | 用户「一键还原」有两种意图，合成一个必然有一方是错的 | 已定 |
| D14 | 提权 | 一律不做。检测不到权限就直接报错，不重试不绕路 | 非目标 | 已定 |
| D15 | 目录布局 | 单 exe + `skin/` + `config/settings.ini` + `state/baseline.json` | — | 已定 |
| D16 | vendor | `vendor/` 入库 | 本机 GitHub 直连偶发不可达，必须离线可构建 | 已定 |
| D17 | 测试形态 | 自写极简断言 harness（返回码 = 失败数），`tests/golden/` 存基线字节 | 不引 gtest 以免再加一层构建依赖 | 已定 |
| D18 | CRT 链接 | **`/MT` 静态 CRT**，M0–M4 用 `CONSOLE` 子系统 | 单 exe 免装 VC 运行时；也让 §11-M0 的尺寸阈值有意义（`/MD` 下 15KB 是正常值，阈值判据会假红）。子系统问题（GUI 与 CLI 共用单 exe 时 Windows subsystem + `AttachConsole`）推迟到 M5 决定 | 已定 |
| D19 | vendor 溯源 | 每个 vendor 文件在 `vendor/README.md` 记录 **来源 URL + sha256 + 许可 + 拉取命令**；文件头版本行作一致性对照 | 本机 GitHub raw 直连已被实测 reset（见 D20），无法依赖构建期联网校验，只能入库时固定并留可核对指纹 | 已定 |
| D20 | vendor 拉取通道 | 允许改用 CDN 镜像（实测 `fastly.jsdelivr.net` 可用，`cdn.jsdelivr.net` 被中断、`raw.githubusercontent.com` 被 reset）；**入库前必须校验内容签名**（如 stb 头部 `stb_image - v2.30`）+ 记录 sha256 | 镜像内容不保证逐字节等于上游：校验不通过就不入库，禁止「下下来了就用」 | 已定 |

**SVG 通道是本设计的外部硬不确定点，M0 已把它收窄成一个决策题。** 实测结论：本机无 Rust → resvg 无法在本机自建。退路按优先级：① 装 Rust 工具链后自库（引入工具链成本，仍须验证 C ABI）；② **`nanosvg` + 加宽的特性守卫**（无工具链成本，代价是支持的 SVG 子集更小，守卫必须把不支持项**拒绝渲染并报因**而不是凑一张错的图）；③ 找预编译的 resvg C API 包（依赖第三方发布物，来源可信度需单独判定，暂不作为默认路径）。
另：先前写的「系统 `WIC` 不支持 SVG」属**凭既有知识、本机未实测**，若真要依赖它必须实测（见 U11）。
**红线不变：禁止为了「看起来支持 SVG」而渲染一个错误的图然后沉默交付。**

---

## 4. 三条输入通道契约

共同契约：任一通道对**单个文件**的输出只有两种结局 —— 成功产出合法 `CursorSpec`，或**带原因的失败**。禁止「跳过并继续」；所有被拒文件必须在结果面板与 CLI 输出中逐条列出（静默缩水是本项目的头号缺陷类别）。

### 4.1 `.cur` 通道（D5 passthrough）

```
读文件 → 解析 ICONDIR/CURSORDIR（type=2）
       → 逐条目解析 DIB 头，取 width/height/bpp/hotspot
       → 校验通过 ⇒ 原样把该文件视为产物，仅登记认领关系
       → 校验失败 ⇒ 报错（条目数 0 / 尺寸越界 / 头不一致 / type≠2 …）
```

要点：
- 不重排字节、不合并帧、不改 hotspot。
- `.ico` 是否接受？→ **不接受**（v1）。`.ico` 无 hotspot 字段，强行套默认规则会给出一堆「看着能用但点击点偏了」的产物。列入 BACKLOG。
- `LoadCursorFromFileW` 的成功与否归 `selfcheck/`（L1），不作为认领阶段的前置，以便「文件坏」与「槽位不认」两类错误分开报。

### 4.2 PNG → `.cur`

```
stb_image 解码（强制 4 通道）
  → straight alpha → **premultiplied** BGRA32
  → 按目标档位缩放（D8；缩放必须在预乘空间做，缩完不再反预乘）
  → CurWriter 封装（§6）
  → hotspot = sidecar 或 §5.3 默认
```

- **无 alpha 通道的 PNG**（调色板 / RGB）：AND 掩码取**全 0**、alpha 全 255。不做亮度阈值抠图 —— 那不是转换，是猜测。
- 输入尺寸 < 某档目标尺寸时**不放大**（放大只会糊），只保留原生尺寸帧；这是 D8 的例外规则，须在 UI 里说明。
- 16-bit PNG / 灰度+alpha：stb_image 归 8bit 4 通道处理，容差不承诺。

### 4.3 SVG → `.cur`

```
读 SVG 文本 → **特性守卫**（§4.3.2）
  → resvg 光栅化到工作分辨率 1024
  → 归一化到 CursorSpec 帧（同 §4.2 的预乘与缩放）
  → 可选自动贴边（trim）
```

两个 SVG 特有陷阱，必须写进代码而不是靠用户自觉：

1. **viewBox 出血**：手写/从 AI 导出的光标 SVG 常把箭头画在 24×24 画布的 (0,0) 尖角处；若原样缩放填充整帧，热点会跑到图形内部。
2. **alpha 预乘**：光栅化器输出的直通 alpha 若被当预乘写入，半透明边缘会出现暗边/亮边（本类工具第一名视觉 bug）。

#### 4.3.2 特性守卫（不可省略）

渲染前解析 SVG XML，扫描是否出现「本项目所选光栅器不支持/易出错」的特性。命中即**拒绝渲染并报错**，不得降级成「大概差不多」：

| 检测对象 | 命中处置 |
|---|---|
| `<text>` / `<tspan>` | 拒绝：字体依赖本机，结果不可复现 |
| `<filter>` | 拒绝：光栅器支持度差异最大 |
| `<mask>` / `<clipPath>` 引用未定义 id | 拒绝 |
| `<image>` 内嵌位图 | 拒绝（v1 不级联 PNG 解码路径） |
| `<style>` / CSS class 选择器 | 拒绝：属性继承歧义 |
| 外部 `xlink:href` / `url(#...)` 未定义 | 拒绝 |
| 无 viewBox / 无 width-height | 拒绝：目标尺寸无从决定 |

**守卫自身的可信度要求**（沿用既证教训：守卫写完当天往往零强制）：
- 每条规则配一个**负对照测试**（构造含该特性的最小 SVG，断言必须被拒）；
- 每个里程碑至少**手动制造一次违规**确认守卫会红，并在下一个合规步骤确认不误报；
- 「遍历到底」型夹具（如扫描 skin/ 全目录）必须自带「应当访问到 N 个文件」的自证断言。

---

## 5. 中间表示与槽位模型（可扩展性契约）

### 5.1 CursorSpec

```cpp
enum class SourceKind { Png, Svg, CurPassthrough };

struct Hotspot {            // core 自带类型，不用 Win32 的 POINT（§2 层规则：core 不依赖 windows.h）
    uint16_t x, y;
};

struct Frame {
    uint32_t width, height;                 // 1..256；0 的编码约定只允许出现在 writer 内部
    std::vector<uint8_t> pixels;            // BGRA32, **premultiplied**
    // 行序：index 0 = 最上一行（top-down）。底向上翻转是 CUR 容器的事，见 H11。
    // 行内无填充：每行恰为 width*4 字节，4 字节对齐由 writer 在 AND 掩码侧负责。
};

struct CursorSpec {
    SlotId slot;                            // 强类型，禁止用裸 int 传「槽位」（参数语义防错）
    std::vector<Frame> frames;              // 至少 1 个；按 (height*width) 升序
    Hotspot hotspot;                        // v1：整份 spec 一个 hotspot；passthrough 保留原文件帧级值
    SourceKind source_kind;
};
```

字段级不变量（`CurWriter` 入口全部拒绝，不修正、不夹带）：

- `frames` 非空；每帧 `width/height ∈ [1,256]`。
- `pixels.size() == width*height*4`。
- **预乘可检**：每个像素的 B/G/R 三通道都必须 `<= A`（预乘的必要条件）。不满足即判「忘了预乘或算错了」并拒收 —— 这是该错误唯一能机器发现的形态（U13）。
- `hotspot.x < width && hotspot.y < height`（对**每一帧**成立，H5）。
- 全帧 alpha 全为 0 的输入**拒收**（§9.1 负对照 1）：产出「合法但看不见」的光标是缺陷，不是结果。
- 帧数上限、目录偏移等容器级约束见 §6。

### 5.2 槽位表（唯一表述处，别处不得重复）

注册表位置 `HKCU\Control Panel\Cursors`，值类型 `REG_SZ`，内容为 `.cur` 的**绝对路径**。

| SlotId | 注册表值名 | 语义 | Win32 常量 |
|---|---|---|---|
| Arrow | `Arrow` | 正常选择 | `OCR_NORMAL` |
| IBeam | `IBeam` | 文本选择 | `OCR_IBEAM` |
| Wait | `Wait` | 忙 | `OCR_WAIT` |
| Cross | `Cross` | 精确选择 | `OCR_CROSS` |
| UpArrow | `UpArrow` | 替代选择 | `OCR_UP` |
| Help | `Help` | 帮助选择 | `OCR_HELP` |
| Hand | `Hand` | 链接选择 | `OCR_HAND` |
| AppStarting | `AppStarting` | 后台忙碌 | `OCR_APPSTARTING` |
| Unavailable | `Unavailable` | 不可用 | `OCR_NO` |
| Size | `Size` | （遗留，通常不使用） | — |
| SizeNW / SizeSE | `SizeNW` / `SizeSE` | 左上↔右下对角 | `OCR_SIZENWSE` |
| SizeNE / SizeSW | `SizeNE` / `SizeSW` | 右上↔左下对角 | `OCR_SIZENESW` |
| SizeWE | `SizeWE` | 水平调整 | `OCR_SIZEWE` |
| SizeNS | `SizeNS` | 垂直调整 | `OCR_SIZENS` |
| SizeAll | `SizeAll` | 移动 | `OCR_SIZEALL` |

> 状态 **待实测**：本表按记忆列出，M2 第一步必须用程序实测枚举确认「写入的名字确实被系统接受」。写错值名是**静默无效**的（不报错、不生效），因此 M4 的判据不是「没报错」，而是 §8 的 L2/L3。

**别名表（数据，非代码分支）** —— 认领阶段把 stem 归一到 SlotId：

```
arrow, normal            -> Arrow
ibeam, text, beam        -> IBeam
wait, busy, loading      -> Wait
cross, precision         -> Cross
up, alternate, uparrow   -> UpArrow
help, question, question_arrow -> Help
hand, link, pointer      -> Hand
appstarting, working_busy-> AppStarting
no, not_allowed, denied, unavailable -> Unavailable
move, sizeall            -> SizeAll
ew_resize, sizewe        -> SizeWE
ns_resize, sizens        -> SizeNS
nw_resize, sizenw, top_left_corner -> SizeNW
se_resize, sizese, bottom_right_corner -> SizeSE
ne_resize, sizene, top_right_corner  -> SizeNE
sw_resize, sizesw, bottom_left_corner-> SizeSW
```

可扩展性判据（机器可检）：**新增一个槽位或一个别名 = 只在表里加一行，不需要改任何 `if/switch`**。若某次改动为「加一个别名」动了逻辑代码，视为违背契约。
`-resize` 系列别名是为了吃下 XCursor 风格命名；这是数据驱动的真实理由，不是装饰。

**未认领文件必须报告**，不得静默忽略。认领结果是一张表：`文件 → SlotId | 未认领(原因)`。

### 5.3 hotspot 默认规则与覆盖

| 组 | 默认 hotspot |
|---|---|
| Arrow, Help, Wait, AppStarting | `(0, 0)`（箭头尖在左上角） |
| IBeam | `(w/2, h*1/5)` |
| UpArrow | `(w/2, 0)` |
| Hand | `(w*3/10, h*1/5)`（食指尖） |
| Cross, SizeAll, SizeWE, SizeNS | `(w/2, h/2)` |
| SizeNW | `(0, 0)`；SizeSE `(w-1, h-1)` |
| SizeNE | `(w-1, 0)`；SizeSW `(0, h-1)` |
| Unavailable | `(w/2, h/2)` |

以上为**目测校正的初值**，不是权威数值；M5 必须提供「目测校正 + 写 sidecar」的闭环，且在总结里标注哪些槽位尚未目测确认。

**sidecar**：`<stem>.meta`（INI 片段），与图片同目录，只允许承载参数，不允许承载身份：

```ini
hotspot = 3,1          ; 覆盖 §5.3 默认
size = 32,48           ; 覆盖 D8 默认档位
trim = none            ; 仅 SVG：none | auto
```

未知 key → 报错（不忽略）；`hotspot` 越界 → 报错（§6 的 H5）。

---

## 6. CUR 容器字节级要求（M1 硬约束清单）

「违反时观察到什么」这一列是测试用例的来源 —— 每条都要有一个负对照。

| # | 约束 | 违反时的可观察症状 |
|---|---|---|
| H1 | `ICONDIR.idType == 2`（CURSOR），不是 1 | 当 `.cur` 加载失败；若扩展名写 `.cur` 但 type=1，`LoadCursorFromFileW` 报错 |
| H2 | `biHeight == 2 * 图像高`（XOR 位图 + AND 掩码叠为一张） | `LoadCursorFromFileW` 直接失败，指针不变 |
| H3 | AND 掩码 1bpp，**每行按 32 位对齐**；32 位带 alpha 时掩码**全 0** | 透明区变实心（黑块/白块），或边缘出现 8 像素周期的错位条纹 |
| H4 | XOR 数据 **BGRA32 且预乘 alpha** | 半透明边缘一圈暗边或亮边（本类工具第一名视觉 bug） |
| H5 | `hotspot` 必须 `0 <= x < w && 0 <= y < h` | 轻则点击点与视觉点差一格（极难归因），重则文件非法 |
| H6 | 尺寸 256 在目录项里写作 `0`，在 DIB 头里写作 `256` | 256 帧被判为 0×0 / 加载失败 |
| H7 | 各帧 `biPlanes==1`、`biBitCount ∈ {1,4,8,32}`（v1 只产 32） | 加载成功但颜色错乱 |
| H8 | 帧按 `w*h` 升序、目录项偏移严格连续无空洞 | 部分系统挑帧失败，只在特定「指针大小」下坏 —— 最难归因的一类 |
| H10 | **不产出「合法但看不见」的光标**：帧的 alpha 直方图若全为 0，writer 拒收 | 用户换完皮肤后光标消失，只能靠还原按钮救 —— 严重度高于任何显示瑕疵，必须前置拦 |
| H11 | XOR 位图与 AND 掩码都按**底向上（bottom-up）**存储；`CursorSpec` 是 top-down，writer 负责翻转 | 图形上下颠倒。非对称夹具（「上三行不透明、底一行半透明」）+ 独立实现生成的 golden 才能同时抓住行序与偏移错误；对称夹具抓不到 |

H9 特别说明：容错的加载器会让坏文件「看起来没问题」，所以自检绝不能以「没报错」为通过标准。

---

## 7. 应用与还原

### 7.1 应用序列（`apply/`，唯一写入者）

```
1. 采集基线快照（§7.2）→ state/baseline.json
2. 逐槽产出 .cur 到 applied\（D11 原子替换）
3. L1 自检：每个产物 LoadCursorFromFileW + GetIconInfo，校验尺寸/hotspot（§8）
   —— 任一失败 ⇒ 中止，不写注册表
4. 写 HKCU\Control Panel\Cursors：每个已认领槽一个 REG_SZ 绝对路径
5. 写方案名：该键的默认值 = "CursorSwitch - <皮肤名>"，Scheme 同步
   （状态 **待实测**：Scheme/默认值的确切写法与大小写需实测确认，不得凭记忆写死）
6. SystemParametersInfoW(SPI_SETCURSORS, ...)
   （状态 **待实测**：uiParam/pvParam/flags 的确切取值需查证；flags 传 0 还是
     SPIF_SENDCHANGE 属事实问题，M2 实测前不写死）
7. L2 + L3 自检（§8），把结果如实显示：含「已写入但不保证已生效」的显式标注
```

第 3 步必须在第 4 步之前 —— 顺序反了，坏文件会先进系统、再把用户的默认光标弄丢且没有回滚点。

### 7.2 基线快照：三态，不是两态

每个槽位的「之前」有三种可能，必须区分记录：

| 状态 | 记录 | 还原动作 |
|---|---|---|
| 值存在 | `{"has_value": true, "path": "..."}` | 写回原字符串 |
| 值不存在 | `{"has_value": false}` | `RegDeleteTreeW`/`RegDeleteValueW` **删除该值**（写空字符串是错的） |
| 整个键不存在 | `{"key_absent": true}` | 删除该值/键，不创建 |

`state/baseline.json` 首次 apply 才创建，之后不覆盖（避免把「原始默认」当成「上一次的我写的路径」）。另存 `baseline.prev.json` 供误操作追查。

### 7.3 还原（D13 两个动作）

- **还原到运行前**：按 §7.2 三态逐槽回写 → 删 `applied\` → `SPI_SETCURSORS`。
- **复位系统默认方案**：删 `HKCU\Control Panel\Cursors` 下全部自定义值 + 清 `Scheme` → `SPI_SETCURSORS`。此动作**不需要**基线，是兜底出口，UI 上必须与前者分列两个按钮、两种确认文案。

### 7.4 抢占探测（不写死 key path）

Windows 10/11「设置 → 辅助功能 → 鼠标指针 → 大小/颜色」的自定义值优先级可能高于 HKCU 自定义光标。**该设置的实际存储位置未证实**，因此本项目**不猜 key path**，改用行为探测：

> 应用后执行 §8 的 L3。若 L3 读到的生效帧与期望帧不符，且 L1/L2 均通过，则判定为「被系统设置抢占」，UI 明确提示「请检查 设置→辅助功能→鼠标指针 的大小与颜色是否为默认」，而不是报「应用成功」。

这是把外部事实的不确定性转成可诊断分支，而不是让用户当调试器。

---

## 8. 三级自检与证据规范

自检分三级，**每级证明不同的事，不得互相替代**：

| 级 | 手段 | 证明 | 不证明 |
|---|---|---|---|
| **L1 文件级** | `LoadCursorFromFileW` → `GetIconInfo`：断言返回非空、宽高/hotspot 与 `CursorSpec` 一致、`GetLastError` 干净；再加字节长度断言（H9） | 产物是**合法**的 .cur | 系统是否采用了它 |
| **L2 配置级** | 回读 `HKCU\Control Panel\Cursors` 全部值，断言每个已认领槽的路径 == 实际落盘路径且文件存在 | 注册表**写对了** | 生效（值名写错也可能「回读一致」→ 必须配合 L3） |
| **L3 生效级** | 由程序把箭头指向桌面 → `GetCursorInfo` 取生效句柄 → `DrawIconEx` 画进内存 DIB → 与期望帧像素比对（容差见 §10） | 系统指针**真的换成了这一帧** | — |

L3 是本项目的**唯一真判据**。L1+L2 全绿而 L3 不符 = 「静默失败」，必须报失败。
候选实现细节（`GetCursorInfo` 于目标线程调用、句柄比较 vs 像素比较）状态 **待实测**。

**证据规范（交付时必须给出，缺一视为未验证）**

1. 命令与返回码原文（`build.bat` / `tests.bat` 的 `$LASTEXITCODE`）；
2. `passed/failed` 计数原文；
3. 本轮**新产物**的文件名与字节数；
4. **未验证项清单**（本文件 §13 的当前状态快照）；
5. 自己本轮犯的错主动列出。

「日志一片绿」「代码看着没问题」= 验证失败。

---

## 9. 可验证性设计：测试与目录

### 9.1 为什么 core 必须无 Win32

确定性测试只能在「纯函数 + 纯字节」上做。`CurWriter` 的输入是 `CursorSpec`，输出是 `vector<uint8_t>` —— 于是能在本机跑：

- **golden 测试**：固定 4×4 半透明 PNG 语料 → 断言输出字节与 `tests/golden/4x4.cur.bin` **完全相等**。这一条同时抓住 H2/H3/H4 三类错误（预乘、掩码、双高）。
- **round-trip**：`CursorSpec → CurWriter → CurReader → CursorSpec'`，断言 `frames` 数、每帧 w/h/hotspot、以及 ≥1 个已知像素点的 RGBA 值（不做全字节往返，因为 AND 掩码路径会有对齐补零）。
- **负对照**（反「表演性测试」硬要求）：
  - 全透明输入必须**被拒**（不可产出「合法但看不见」的光标）；
  - hotspot 越界必须**被拒**（H5）；
  - 帧数 0 必须**被拒**；
  - 手写一个 `biHeight` 不双倍的坏文件，L1 必须**变红**。
- **遍历型夹具自带自证断言**：扫 `tests/fixtures/` 的夹具必须断言「实际访问了 N 个文件」，N 写在夹具清单里。教训来源：`WalkToEnd` 只走了 1 层就 break，全绿，覆盖面悄悄缩水。
- **守卫必须被制造一次违规**：§4.3 特性守卫、别名表冲突检测、premultiply 入口断言 —— 每条写完当天做一次「故意破坏 → 确认变红 → 恢复 → 确认不误报」，并把该动作记进里程碑总结。反射/扫描式守卫另配负对照。

### 9.2 CLI 与 GUI 同链

`ui/` 不含业务逻辑，只调用 `apply/` 的接口；`main.cpp` 的 `--apply/--restore/--reset/--probe/--build <dir>` 调同一批接口。
理由不是「顺便支持命令行」，而是**可验证性**：L3 探测、日志输出、回归脚本都必须能无人值守跑。GUI 里每个按钮旁显示等价 CLI 命令。

### 9.3 目录布局

```
CursorSwitch/
  AGENTS.md  DESIGN.md  README.md
  build.bat  tests.bat            # 必须 CRLF、纯 ASCII（本机 PS5.1 按 GBK 读 .ps1；
                                   #  且 cmd 会把 echo 里的裸 `>` 当重定向 —— 见 AGENTS 陷阱表）
  src/
    core/     ir.h cur_writer.h cur_reader.h png_source.* svg_source.* claim.* hotspot.*
    apply/    registry.* baseline.* apply.* selfcheck.* probe.*
    ui/       main_window.* resources.rc
    main.cpp  # CLI 入口 + GUI 入口（无参数时启动 GUI）
  vendor/     stb_image.h  resvg_*（入库，D16）
  skin/       # 用户素材目录（默认样例：arrow.png + arrow.meta，README 说明）
  tests/
    test_core.* test_claim.* test_selfcheck.*
    fixtures/  golden/
  docs/summaries/  docs/BACKLOG.md  docs/BLOCKED.md
  state/      # 不入库（基线快照）
```

---

## 10. 参数总表

原则：**数值与规则以配置注入，代码里不写字面量**。下表是唯一来源，代码从 `config/settings.ini` 读，缺省值即本表「默认」列。

| key | 默认 | 说明 | 所属里程碑 |
|---|---|---|---|
| `frames.sizes` | `16,32,48` | 生成档位（D8） | M1 |
| `frames.allow_upscale` | `false` | 小图不放大 | M3 |
| `svg.work_resolution` | `1024` | 光栅化工作分辨率 | M4 |
| `svg.trim_transparent` | `auto` | 贴边（none/auto） | M4 |
| `svg.min_content_ratio` | `0.35` | 内容占比低于此值 ⇒ 告警（出血启发） | M4 |
| `apply.atomic_tmp_suffix` | `.tmp` | 原子替换临时名 | M2 |
| `apply.abort_on_l1_fail` | `true` | §7.1 第 3 步顺序保证 | M2 |
| `probe.pixels_per_slot` | `48,200` | 逐槽落点网格（列=槽序号，行=1x/1.5x） | M2 |
| `probe.tolerance_per_channel` | `8` | L3 逐通道平均绝对差上限 | M2 |
| `probe.max_slot_columns` | `10` | 单列最多落点数（超出则多列，防触顶） | M2 |
| `probe.reapply_rounds` | `2` | 用「再应用一次皮肤」作对照实验次数 | M2 |
| `probe.settle_ms` | `200` | 落点后等待，防抖/防过渡动画 | M2 |
| `probe.debounce_ms` | `300` | 仅 BACKLOG watcher 用 | — |
| `writer.max_dir_entries` | `12` | 容器上限（D8 七档 + 余量） | M1 |
| `writer.min_frame_bytes` | `128` | AND 掩码最小行填充（1bpp，4 字节对齐） | M1 |
| `writer.min_dir_gap_bytes` | `64` | 帧数据偏移最小间隔 | M1 |
| `writer.reject_fully_transparent` | `true` | H10；设 false 仅供调试，默认必须 true | M1 |
| `hotspot.group.*` | §5.3 | 默认规则也走配置，不散落 if | M1 |

---

## 11. 里程碑与完成判据

| M | 版本 | 交付物 | **机器可验证判据** |
|---|---|---|---|
| M0 | V0.1 | git 仓库、`.gitattributes`、`build.bat`、`tests.bat`、无窗口 core 骨架（CLI 解析 + 动词表）、自写测试 harness、vendor 与 `vendor/README.md` 溯源 | ① `build.bat` 返回码 0，且 `build\cursorswitch.exe` 与 `build\cursorswitch_tests.exe` **两个产物各自**字节数 ≥ 65536（依据 D18 静态 CRT；此阈值的唯一目的是排除「13 字节竞态/echo 重定向写坏」，实测值须写进总结）；② `tests.bat` 正常模式 exit 0 且 `PASSED` 计数 ≥ 1；③ **harness 自证**：同一测试 exe 以 `--force-fail` 运行必须 exit ≠ 0，`tests.bat` 对这两种退出码都设了判据 —— 只验 ② 不验 ③ 即视为 M0 未完成（它证明这套链路能变红，不是永远绿）；④ 构建日志与测试输出落 `build\` 下文件（控制台按 GBK 会吞中文）；⑤ vendor 文件的 sha256 与 `vendor/README.md` 记录一致、头部版本行一致 |
| M1 | V0.2 | `CurWriter`/`CurReader` + `CursorSpec` + 别名/claim 表 | §9.1 全部：golden 字节相等、round-trip、**4 条负对照全红→全绿**；守卫违规制造记录写入总结 |
| M2 | V0.3 | `.cur` passthrough 端到端 + 基线三态 + HKCU + SPI + 两路还原 + **L2/L3** | ① 造一张「非法 .cur」夹具 → 断言中止且**未写注册表**；② 真实 apply 后 L2 全绿、L3 至少 Arrow 槽像素通过；③ 还原到运行前后 L2 回读 == 快照原文；④ 复位默认后系统恢复原生光标；⑤ §5.2 槽名实测表写入本文（**本步的实测把本文状态列的待实测项清掉**） |
| M3 | V0.4 | PNG 通道（解码/预乘/缩放/多帧） | 对 `arrow.png` 夹具：L1 通过 + 目测无暗边（以 golden 像素断言替代：边缘半透明像素值在容差内）；无 alpha PNG 走 §4.2 规则且不产生透明洞 |
| M4 | V0.5 | SVG 通道（**光栅器选型先落定**，见 B-01 + 特性守卫 + 出血/贴边） | 前置条件：D4 选型已被追认。§4.3.2 每条特性各有 1 个**必须被拒**的负对照；出血启发式在夹具上告警/不告警各 1 例；最终光栅器与其**不支持特性清单**写入本文 D4 与 §4.3.2 |
| M5 | V0.6 | Win32 窗口：皮肤选择 / 逐槽状态 / 应用 / 两种还原 / 结果面板 | GUI 每个动作有等价 CLI 且两者结果一致；窗口截图存档；hotspot 目测校正闭环可用 |
| — | **V1.0** | MVP 验收 | 13 槽（不含 `Size`）用一套真素材全链路 apply→L3 逐槽通过→还原无残留 |

### 11.1 「什么时候第一次能真的用」

| 想做的事 | 最早可用 |
|---|---|
| 拿现成 `.cur` 光标包换掉系统指针（并安全还原） | **M2 / V0.3** |
| 用 PNG 自己画的光标 | M3 / V0.4 |
| 用 SVG 素材 | M4 / V0.5 |
| 不开命令行、纯鼠标操作 | M5 / V0.6 |

顺序刻意把「能天天用」放在转换能力之前：M2 结束时工具已经对用户有价值，M3/M4 是增益。

---

## 12. 风险登记

| # | 风险 | 影响 | 处置 |
|---|---|---|---|
| R1 | resvg 需 Rust 工具链，本机实测**无 cargo/rustc/rustup** | SVG 通道延期或降级 | 已成立 → 转为 B-01 决策项；先交付 M2/M3（与 SVG 无关） |
| R2 | 设置里的指针大小/颜色抢占 HKCU 方案 | 「应用成功但没变」 | §7.4 行为探测，不猜 key path |
| R3 | 值名写错 → 静默无效 | 全绿但没生效 | L3 作真判据 + M2 实测枚举 |
| R4 | 未预乘 alpha / AND 掩码错 | 视觉质量差、难归因 | H2/H3/H4 + golden 测试 |
| R5 | 基线快照覆盖，原始默认丢失 | 用户回不到系统光标 | §7.2 首次不覆盖 + 独立「复位默认」出口 |
| R6 | 杀软误报（写注册表 + 全局指针） | 分发失败 | 原生小 exe、不注入、不常驻；如实报告，不做绕过 |
| R7 | 构建竞态：上一实例仍在运行 → 产出 13 字节 exe | 假成功 | `build.bat` 先 kill 再清后编、编译后断言字节数（M0 判据） |
| R8 | SVG 光栅化不确定性 | 皮肤视觉不一致 | 工作分辨率固定 + 守卫拒绝不可复现特性（`<text>`/`<filter>`） |
| R9 | CDN 镜像拉取的 vendor 文件与上游不一致（GitHub raw 实测被 reset，只能走镜像） | 构建出来的行为与上游文档不符，且难以归因 | D19/D20：入库时固定 sha256 + 头部版本行双签核对，记录在 `vendor/README.md`；不一致就不入库 |
| R10 | 同形字符（如西里尔 е）混入数据表 | 别名静默失配，认领阶段少测且不报错 | 已发生一次并修掉；`vendor`/别名/槽表纳入「表内容必须纯 ASCII（除显示名）」守卫，M1 落地并配正负对照 |

---

## 13. 未验证项清单（当前状态，交付时逐条核销）

| ID | 断言 | 状态 | 由哪一步核销 |
|---|---|---|---|
| U1 | `HKCU\Control Panel\Cursors` 的值名集合 == §5.2 表 | **未验证（凭记忆）** | M2 实测枚举 |
| U2 | `SPI_SETCURSORS` 的 uiParam/pvParam/flags 确切传法 | **未验证** | M2 查证 + 实测 |
| U3 | L3 能否稳定取到生效帧并做像素比对 | **未验证** | M2 原型 |
| U4 | 设置的指针大小/颜色是否抢占、存储位置 | **未验证** | 不做假设，走 §7.4 探测 |
| U5 | PNG 压缩条目在 `.cur` 中是否被光标路径接受 | **未验证** → 已按 D7 绕开 | 不需要（若未来优化再验） |
| U6 | resvg 在本机的可集成性 | **已实测：前置不成立（本机无 Rust 工具链）**；「能否产出可链静态库」在装好工具链前无从谈起 | 已由 B-01 选型决策接管 |
| U7 | 动画光标的系统尺寸策略上限 | **未验证** | v1 不做 |
| U8 | §5.3 各槽 hotspot 默认值的目测正确性 | **未验证** | M5 校正闭环 |
| U9 | M0 之前本项目无任何代码：`build.bat`/`tests.bat`/exe 均未创建、未实测 | 已核销（M0 实测，数字见 `docs/summaries/V0.1-*.md`） | M0 |
| U10 | 静态 CRT（`/MT`）下最小 exe 实际尺寸区间（M0 阈值 ≥65536 的依据） | 本轮构建实测 | M0 |
| U11 | 本机 WIC 是否支持 SVG 解码 | **未验证（先前凭既有知识写成「不支持」，已标注）** | 仅当有人提议走该路径时才验，否则不依赖 |
| U12 | `.cur` 目录项里 hotspot 占据 `wPlanes/wBitCount` 两个 WORD，且 `LoadCursorFromFileW` 读的就是这两个字段 | **未验证（按公开格式文档理解实现；writer 已按此写）** | M2 用 `GetIconInfo` 的 `ptHotSpot` 实测核销 |
| U13 | 「每通道 `<= alpha`」作为预乘必要条件，是否会把合法输入误拒（如上游输出 alpha=255 之外的边界） | **未验证** | M1 记录误拒率；若误拒则改为告警 + 就地预乘 |
| U14 | `BI_RGB` 下 `biSizeImage` 写 XOR 字节数是否被各加载路径接受（0 也常见） | **未验证（writer 写实际字节数；reader 明确不依赖该字段）** | M2 L1 |

---

## 14. BACKLOG 入口（只进不出，不顺手实施）

`.ico` 输入通道；`.ani` 动画光标；目录 watcher 常驻自动生效；游标包导入/导出（含方案打包）；逐 DPI 素材集；写入 `HKU\.DEFAULT`（提权，需另行立项）；`Size` 遗留槽位是否纳入；hotspot 可视化编辑器；多语言 UI。
