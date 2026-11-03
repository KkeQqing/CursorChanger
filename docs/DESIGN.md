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

**V0.7 起新增目标**（从下方「非目标」毕业，见 §3 决策 D21/D22/D23 与 §11 里程碑 M6–M7）：

4. **序列帧动画 `.ani`**：槽位值可指向动画光标；支持 (a) 现成 `.ani` 透传，(b) 由多帧素材合成 `.ani`。
5. **目录 watcher（可选）**：皮肤目录变更后**自动重新应用**当前皮肤；默认关闭、显式开启。
6. **输出尺寸可配置**：`frames.sizes` / `allow_upscale` 由 `config/settings.ini` 注入并可在全选/单档间选择。

> 4 与 5 都会**当场替换用户正在使用的指针**，其「真的会动画 / 真的自动生效」只能由真机 L3 判定（§8；相关未验项见 §13 U7/新增 U 项）。文档只承诺能做到的部分，不以「加载成功」冒充「动画成功」。

**非目标（v1 明确排除，不做即不测）**

| 排除项 | 原因 |
|---|---|
| UAC 安全桌面 / 登录界面光标 | 需提权 + 写 `HKU\.DEFAULT`，风险等级不同 |
| `HKLM` 写入、全用户生效 | 同上，零注入原则的边界 |
| DPI 逐档自适应素材集 | 交给系统按帧挑选，见 D8 |
| 光标包分发/商店、云同步 | 超范围 |

> 变更（V0.7）：原列于此表的「动画光标 `.ani`」与「目录 watcher 常驻自动生效」已**毕业为目标 4/5**（用户拍板），非目标表相应删除这两行；`D12`/`D8` 相应修订，新增 `D21/D22/D23`。

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
| D4 | SVG 光栅化 | **已落地 `nanosvg`（vendored 纯 C 单头文件）+ 白名单式特性守卫**。首选 resvg 的前置条件实测失败：本机无 Rust 工具链（`cargo` / `rustc` / `rustup` 均 ABSENT）。 | 代价与边界：子集更窄，所以守卫必须**默认拒绝**（未知元素即拒），不能只拉黑已知项。M4 实测两件事：① nanosvg 输出是 **straight RGBA**（探针：50% 灰圆中心 RGBA=(126,126,126,127)，若已预乘应为 ~63）；② `url(#id)` 只允许指向文档内已定义的渐变，否则渐变填充会被误杀 | **已定（默认方案已落地；选型本身仍待追认）** |
| D5 | `.cur` 通道 | **passthrough**：原样复制文件，绝不重编码 | 重编码会丢多帧/多尺寸并引入 alpha 误差；passthrough 成本≈0 且不会把好素材做坏 | 已定 |
| D6 | 中间表示 | `CursorSpec{ frames[], hotspots[], source_kind }`，像素统一为 **BGRA32 premultiplied** | 单点定义像素约定，三条通道归一 | 已定 |
| D7 | CUR 编码 | 只写 **DIB(BMP) 条目**，不写 PNG 压缩条目 | PNG-in-CUR 的合法性由系统光标加载路径决定，未证实；DIB 是无争议路径。省掉一整类不确定 | 已定（不依赖实测） |
| D8 | 帧集合 | 每素材产出 **16/32/48** 三档；`svg/cur` 若自带多尺寸则保留全部。**V0.7 起这只是默认档位**，实际档位数由 `settings.ini` 注入（见 D23） | 最终显示尺寸由「设置→指针大小」+ DPI 决定，猜单尺寸是错的；多帧让系统挑 | 已定 |
| D9 | 槽位识别 | 文件名 stem（去后缀、去路径）经**别名表**归一，大小写不敏感；不支持按尺寸/DPI 后缀区分 | 别名表是数据，不是 if；见 §5.2 | 已定 |
| D10 | hotspot | 每槽一条默认规则（§5.3）+ sidecar `<stem>.meta` 覆盖 | 文件名只回答「我是哪个槽位」，不承载参数 —— 这条必须守住 | 已定（默认值为**需目测校正初值**） |
| D11 | 产物落盘 | `<程序目录>\applied\`（可配置），先写 `*.tmp` 再 `MoveFileExW(REPLACE_EXISTENT)` | 系统可能正持有旧文件；原子替换避免半写状态被读到 | 已定 |
| D12 | 生效触发 | v1 = 窗口「应用」按钮 / CLI `--apply`。**V0.7 起 watcher 作为可选自动生效模式毕业**（默认关闭、显式开启，见 D22）；核心手动触发不变 | 「换图即换皮肤」由 opt-in watcher 承担，不再一刀切排除；成本与风险记在 D22 | 已定（V0.7 修订） |
| D13 | 还原语义 | 两个独立动作：① 还原到运行前快照 ② 复位系统默认方案 | 用户「一键还原」有两种意图，合成一个必然有一方是错的 | 已定 |
| D14 | 提权 | 一律不做。检测不到权限就直接报错，不重试不绕路 | 非目标 | 已定 |
| D15 | 目录布局 | 单 exe + `skin/` + `config/settings.ini` + `state/baseline.ini` | — | 已定 |
| D16 | vendor | `vendor/` 入库 | 本机 GitHub 直连偶发不可达，必须离线可构建 | 已定 |
| D17 | 测试形态 | 自写极简断言 harness（返回码 = 失败数），`tests/golden/` 存基线字节 | 不引 gtest 以免再加一层构建依赖 | 已定 |
| D18 | CRT 链接 | **`/MT` 静态 CRT**。app 用 **`WINDOWS` 子系统**（入口 `/ENTRY:mainCRTStartup` 复用现有 `main()`），tests 保持 `CONSOLE` 子系统 | 单 exe 免装 VC 运行时；也让 §11-M0 的尺寸阈值有意义（`/MD` 下 15KB 是正常值，阈值判据会假红）。**M5 曾定「保持 CONSOLE + ShowWindow(SW_HIDE) 隐藏」**，代价是纯 GUI 启动闪一次控制台——该隐藏只在 `main()` 之后生效，控制台窗口早在进程创建时就被 OS 分配，闪框无法靠隐藏消除。**V0.6.2 改判（用户要求 GUI 启动静默、不分配控制台）**：app 切 `WINDOWS` 子系统后双击启动不再有任何控制台窗口。CLI 输出捕获链不削弱：① 重定向到文件/管道（Job Object 无人值守捕获的形态）时，`WINDOWS` 子系统同样经 STARTUPINFO 继承句柄，`printf` 照旧落进管道；② 从交互式终端直接带参运行时无继承控制台，CLI 分支启动时 `AttachConsole(ATTACH_PARENT_PROCESS)` 再 `freopen("CONOUT$","w",stdout/stderr)` 接回。tests 必须留 `CONSOLE`：harness 退出码=失败数、且 `--probe-*` 依赖 stdout。**代价**：从终端直接跑（未重定向）需父控制台存在才看得到输出；`Start-Process` 这类脱离控制台的启动方式下 CLI 的 stdout 取不到（动作仍执行）。| 已定（V0.6.2 改判） |
| D19 | vendor 溯源 | 每个 vendor 文件在 `vendor/README.md` 记录 **来源 URL + sha256 + 许可 + 拉取命令**；文件头版本行作一致性对照 | 本机 GitHub raw 直连已被实测 reset（见 D20），无法依赖构建期联网校验，只能入库时固定并留可核对指纹 | 已定 |
| D20 | vendor 拉取通道 | 允许改用 CDN 镜像（实测 `fastly.jsdelivr.net` 可用，`cdn.jsdelivr.net` 被中断、`raw.githubusercontent.com` 被 reset）；**入库前必须校验内容签名**（如 stb 头部 `stb_image - v2.30`）+ 记录 sha256 | 镜像内容不保证逐字节等于上游：校验不通过就不入库，禁止「下下来了就用」 | 已定 |
| D21 | 序列帧动画 `.ani` | 两形态：**(a) `.ani` 透传**（现成动画光标按槽名认领，原样作为 scheme 值，不重编码）；**(b) 由多帧合成 `.ani`**（帧序列与每帧停留取自槽位 sidecar `<stem>.meta` 的 `frames=`/`rate=`，见 §5.4）。容器格式**以 Step-0 反解微软自带 `aero_busy.ani` 为准**：`RIFF 'ACON'` + `'anih'`(9×DWORD) + `LIST 'fram'`，帧 chunk 的 FourCC 实测为 **`icon`**（**不是** `ani `），每帧内嵌一个完整光标资源；`anih.speed` 实测为 0，帧序引用 iCurCount/iIconCount | 让用户直接用已有动画光标包 + 可选自合成；`.meta` 解析是 (b) 的前置（当前未实现，见 BACKLOG） | **已定（结构）/ 待实测 U7（是否真会动画 + 帧数/尺寸/速率上限）** |
| D22 | watcher 自动生效 | 可选后台 `ReadDirectoryChangesW` 监听当前皮肤目录，变更经**去抖**（`watcher.debounce_ms`）后**重跑同一条 §7.1 apply 管线**并刷新面板；默认关闭，GUI/CLI 显式开启 | 满足「换图即生效」；代价：它**会自动改用户正在用的指针**（系统状态，须 §8 单独授权），且要防抖、防写盘风暴、防半写文件被读到（配合 D11 原子替换） | 已定（默认关；生效与去抖表现=待真机） |
| D23 | 输出尺寸可配置 | 落地 `config/settings.ini` 行式解析（复用 baseline/.meta 同一极简语法），把 `frames.sizes`（默认 `16,32,48`）与 `frames.allow_upscale`（默认 false）注入 PNG/SVG 通道；GUI 提供档位选择；`writer.max_dir_entries` 随所选档数放宽 | 兑现 §10 既有预留；单 exe、零外部依赖不变 | 已定 |

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
- **预乘公式（唯一规定，两侧必须逐字一致）**：`out = (v * a + 127) / 255`（整数除，即四舍五入）；`a == 0` 时输出 `0`。
- **缩放规则（新增，golden 的前提）**：
  1. 先转成 **premultiplied BGRA32**，再缩放；缩放全程在预乘数据上进行，**不反预乘**。
  2. 采样器只有 **box-average（面积平均）** 一种：目标像素 `(x,y)` 覆盖源矩形
     `[x*W/dw, (x+1)*W/dw) × [y*H/dh, (y+1)*H/dh)`，每通道取
     `floor(sum / count)`（**向下取整，不是四舍五入** —— 两侧算术必须同为这一个式子，
     否则 golden 会出现「差 1」的假失败）。
  3. 放大一律禁止：目标档 >= 源尺寸时不生成该帧（见 `frames.allow_upscale=false`），
     而不是插值放大。此规则要在 UI 的结果面板里说明「原生尺寸小于某档，该档被跳过」，
     别让用户以为皮肤缺件。
  4. 参考帧 = **最大的那一档**；热点用 `scale_hotspot()` 从参考帧换算到各档（U15）。
  > 为什么写死而不是留给实现：本轮实测发现 GDI+ 因内部预乘存储会给出差 1 的回读值，
  > 说明「差不多」的舍入差异在像素级判据里就是失败。规则不写死，golden 就没有意义。
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
| `<mask>` / `<clipPath>` / `<pattern>` 等元素 | 拒绝（nanosvg 不支持，会静默丢内容） |
| `<image>` 内嵌位图 | 拒绝（v1 不级联 PNG 解码路径） |
| `<style>` / CSS class 选择器 | 拒绝：属性继承歧义 |
| 外部 `xlink:href`，以及指向非渐变的 `url(#...)` | 拒绝 |
| 无 viewBox / 无 width-height | 拒绝：目标尺寸无从决定 |
| 引用 `url(#id)` | **只放行指向文档内已定义的 `linearGradient`/`radialGradient`**；未定义 id、filter、pattern、外部引用一律拒 |

**实现形式：白名单而不是黑名单**（M4 落地时定）。只放行确认支持的元素，其余（含我们没听过的）
一律拒绝 —— 漏放行的代价是"静默少画东西"，误拒的代价只是"多问一次"，两者不对等。

两条 M4 实测得到的补充：
- **守卫机制的可观察性**：违规制造时发现，从黑名单摘掉 `text` 后测试**没变红** ——
  allow-list 的"未知即拒"补上了那一层。「被拒」不等于「被哪一层拒」。
  处置：把两张表暴露给测试，断言 `allow ∩ block == ∅`、且黑名单逐元素**点名**可拒
  （`svg_guard_tables_are_disjoint_and_unknown_is_denied`）。
  注意边界：这个新守卫不能也不应抓「从黑名单删掉一个元素」——该元素仍会被 allow-list 的「未知即拒」拦下，行为上无损，测试不该为它变红。对元素而言 block 表只是双保（价值在于给出明确的拒绝理由）；真正非 block 表不可的是属性（如 style、clip-path、mask），因为元素白名单管不到它们。
- **渐变是白名单内的例外**：实现初版把 `url(` 一律拒绝，会误杀渐变填充 —— 而渐变在光标 SVG
  里极常见。该规则由 `svg_guard_allows_in_document_gradients` 守住。


**守卫自身的可信度要求**（沿用既证教训：守卫写完当天往往零强制）：
- 每条规则配一个**负对照测试**（构造含该特性的最小 SVG，断言必须被拒）；
- 每个里程碑至少**手动制造一次违规**确认守卫会红，并在下一个合规步骤确认不误报；
- 「遍历到底」型夹具（如扫描 skin/ 全目录）必须自带「应当访问到 N 个文件」的自证断言。

### 4.4 `.ani` 动画通道（D21；V0.7 新增）

两条子通道，都遵守 §4「成功产出 / 带原因失败，绝不静默」总契约。

**4.4.1 透传（先做，低风险）**：stem 认领某槽、扩展名为 `.ani` 的文件，原样复制到 `applied\`，
该槽 scheme 值指向它。与 D5 同理：不重编码、不解析动画。合法性仍走 L1 —— Step-0 实测
`LoadCursorFromFileW` 接受 `.ani`（`aero_busy.ani` → loaded=True）。但 **L1「加载成功」不等于「会动画」**：
动画是否发生只有 §8 真机 L3 能判，见 U7。

**4.4.2 合成（依赖 §5.3 sidecar 首次落地）**：当某槽 sidecar `<stem>.meta` 写了 `frames=…` / `rate=…`
（见 §5.3），把每帧按 §4.2/§4.3 解码为**单帧光标资源**，再封装成 RIFF ACON 容器：

```
RIFF [size] "ACON"
  "anih"  [36B = 9×DWORD, 权威顺序] :
      cbSizeOf=36, nFrames, nSteps, iWidth, iHeight, iBitCount, nPlanes, iDispRate, bfAttributes
      iWidth/iHeight=0 => 用各帧自带尺寸; iDispRate = 帧率, 单位 jiffies(1/60s≈16.67ms);
      bfAttributes bit0(0x01)=每帧是完整 CUR/ICO 文件; bit1(0x02)=有 seq 块(本实现不用)
  LIST  [size] "fram"
      每帧一个 chunk，FourCC 用 "icon"，体 = 一个完整内嵌光标资源(ICONDIR type=2 + CURSORDIR + DIB)
```

> 结构来源：本机 `C:\Windows\cursors\aero_busy.ani` 逐字节实测 `[36,18,18,0,0,32,1,3,1]`
> （nFrames=18、iBitCount=32、**iDispRate=3 jiffies≈50ms/帧**、bfAttributes=1），并与多篇权威
> 说明 + 一份 ANI2GIF 逐字节逆向交叉印证：帧率单位是 **jiffies 不是毫秒**、每帧内嵌一个完整 `.cur`。
> （订正：本文件早前版本误把 `iDispRate` 标成 `dwDefaultSpeed` 并误读成"speed=0/帧率不在 header"，
> 系字段贴错标签所致；正确字段如上。）
> **仍须真机确认（U22）**：OS 是否按我们的 `iDispRate` 播放、以及 U7 的帧数/尺寸上限；触顶必须
> **报错并拒产出**，不得静默截断帧序列（H9 精神：宁可失败，不可产「看着对」的残缺动画）。

> **实现状态（V0.7）**：**4.4.1 透传已落地** —— `pipeline` 识别 `.ani` 扩展名、原样搬运不重编码、
> 产出 `<槽名>.ani`、L1 以系统 `LoadCursorFromFileW` 作**独立 oracle**；坏 `.ani` 挡在写注册表前
> （测试 `tests_m10` 正/负各一，含"字节未被改写"与"`.ani` 输出名"两条守卫）。
> **4.4.2 合成已落地（V0.7，commit M12）**：`core/ani_format` 合成 RIFF/ACON（`iDispRate`=jiffies、
> 每帧=完整 `.cur`、`bfAttributes=1`），`sidecar frames=/rate=` 经 `build_only` 接通（`tests_m12` 端到端
> 用 `LoadCursorFromFileW` 证 OS **接受我们自造的 .ani** + reader 反解 nFrames/rate）。
> **U22 已真机确证**：用产品管线合成 `rate=10`(jiffies) 的两帧动画（箭头↔红圈）apply 到 Arrow，
> 用户肉眼见**约 6 次/秒交替**（10/60s≈166ms/帧 ⇔ ~6fps），证「OS 按我们设的 `iDispRate` 播放合成动画」；
> 验完已 `--restore`，独立导出逐字节 == 皮肤态。

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
    Hotspot hotspot;                        // 帧级：容器每个目录项本来就有自己的热点
    std::vector<uint8_t> pixels;            // BGRA32, **premultiplied**
    // 行序：index 0 = 最上一行（top-down）。底向上翻转是 CUR 容器的事，见 H11。
    // 行内无填充：每行恰为 width*4 字节，4 字节对齐由 writer 在 AND 掩码侧负责。
};

struct CursorSpec {
    SlotId slot;                            // 强类型，禁止用裸 int 传「槽位」（参数语义防错）
    std::vector<Frame> frames;              // 至少 1 个；按 (height*width) 升序
    SourceKind source_kind;
};
```

**热点为什么必须在帧级（U15 的实测结论，不是审美选择）**：
`LoadImage(exact N)` 回读到的热点**逐字等于**该帧目录项里写的值，加载器**不会**在帧之间换算热点
（多帧文件写 (1,1)，取 16 与取 48 都回读 (1,1)）；只有把帧重采样到系统默认尺寸时 OS 才按比例缩放。
因此若一份 spec 只有一个绝对热点，16/32/48 三档里必然有大帧的视觉热点是错的。
产出多帧的通道（PNG/SVG）必须以**参考帧**的热点为基准，按 `round(v * dim / ref_dim)` 逐帧换算后写入。

字段级不变量（`CurWriter` 入口全部拒绝，不修正、不夹带）：

- `frames` 非空；每帧 `width/height ∈ [1,256]`。
- `pixels.size() == width*height*4`。
- **预乘可检**：每个像素的 B/G/R 三通道都必须 `<= A`（预乘的必要条件）。不满足即判「忘了预乘或算错了」并拒收 —— 这是该错误唯一能机器发现的形态（U13）。
- `frame.hotspot.x < frame.width && frame.hotspot.y < frame.height`（H5，逐帧独立成立）。
- 全帧 alpha 全为 0 的输入**拒收**（§9.1 负对照 1）：产出「合法但看不见」的光标是缺陷，不是结果。
- 帧数上限、目录偏移等容器级约束见 §6。

### 5.2 槽位表（唯一表述处，别处不得重复）

注册表位置 `HKCU\Control Panel\Cursors`，值类型 `REG_SZ`，内容为 `.cur` 的**绝对路径**。

| SlotId | 注册表值名 | 语义 | Win32 常量 | 来源 |
|---|---|---|---|---|
| Arrow | `Arrow` | 正常选择 | `OCR_NORMAL` | 本机实测 |
| IBeam | `IBeam` | 文本选择 | `OCR_IBEAM` | 本机实测 |
| Wait | `Wait` | 忙 | `OCR_WAIT` | 本机实测 |
| UpArrow | `UpArrow` | 替代选择 | `OCR_UP` | 本机实测 |
| Help | `Help` | 帮助选择 | `OCR_HELP` | 本机实测 |
| Hand | `Hand` | 链接选择 | `OCR_HAND` | 本机实测 |
| AppStarting | `AppStarting` | 后台忙碌 | `OCR_APPSTARTING` | 本机实测 |
| Crosshair | `Crosshair` | 精确选择 | `OCR_CROSS` | **本机实测**（我原先凭文档记忆写成 `Cross`，**错**） |
| No | `No` | 不可用 | `OCR_NO` | **本机实测**（原先写成 `Unavailable`，**错**） |
| SizeNWSE | `SizeNWSE` | 左上↔右下对角 | `OCR_SIZENWSE` | **本机实测**（原先拆成 `SizeNW`/`SizeSE` 两个槽，**错**：实际是一个槽） |
| SizeNESW | `SizeNESW` | 右上↔左下对角 | `OCR_SIZENESW` | **本机实测**（原先拆成 `SizeNE`/`SizeSW`，**错**） |
| SizeWE | `SizeWE` | 水平调整 | `OCR_SIZEWE` | 本机实测 |
| SizeNS | `SizeNS` | 垂直调整 | `OCR_SIZENS` | 本机实测 |
| SizeAll | `SizeAll` | 移动 | `OCR_SIZEALL` | 本机实测 |
| NWPen | `NWPen` | 手写笔 | —（`OCR_*` 无对应） | **本机实测新增**：原计划完全没这个槽 |

**合计 15 个槽位**，全部参与 v1。原先列出的遗留 `Size` 在本机注册表里**不存在**，已删除。

同一次探查还看到这些**不是槽位**的值，列出来以免后续误当成槽：`CursorBaseSize`、`Scheme Source`、
`GestureVisualization`、`ContactVisualization`（均 REG_DWORD），以及该键的默认值（REG_SZ，方案显示名）。
`CursorBaseSize` 疑似 §7.4「指针大小」的存储位置，**未验证**（见 U4/U18）。

> 状态 **部分核销（M2b 本机实测枚举）**：值名取自这台 Win11 22631 的 `HKCU\Control Panel\Cursors` 实际内容，
> 因此原表的 4 处错名已纠正（见上表「来源」列），并新增了计划外的 `NWPen`。
> **仍未核销的部分**：注册表里存在某个名字，只证明「OS 自己写过它」，不证明「我们写任何名字都会被接受」——
> 写一个不被认的值同样存得下，只是不生效（这正是 U1 担心的静默无效）。
> 合法集合的完整边界、以及 `Cross`/`Unavailable` 这类 legacy 名是否仍然可用，必须由 §8 的 **L3** 逐槽实测；
> 所以判据从来不是「没报错」，而是「生效帧的像素对上了」。

**别名表（数据，非代码分支）** —— 认领阶段把 stem 归一到 SlotId：

```
arrow, normal            -> Arrow
ibeam, text, beam        -> IBeam
wait, busy, loading      -> Wait
cross, crosshair, precision -> Crosshair
up, alternate, uparrow   -> UpArrow
help, question, question_arrow -> Help
hand, link, pointer      -> Hand
appstarting, working_busy-> AppStarting
no, not_allowed, denied, unavailable -> No
move, sizeall            -> SizeAll
ew_resize, sizewe        -> SizeWE
ns_resize, sizens        -> SizeNS
nw_resize, sizenw, se_resize, sizese, top_left_corner, bottom_right_corner -> SizeNWSE
ne_resize, sizene, sw_resize, sizesw, top_right_corner, bottom_left_corner -> SizeNESW
nwpen                    -> NWPen
```
（合计 **45** 条别名，覆盖 15 个槽位。`nwpen` 是按值名直取的别名，其余每条都出自本表；
两个对角槽各收 6 条别名 —— 这是**实测的槽结构**决定的，不是命名偏好。）

别名表覆盖 **16 个 v1 槽位**；遗留 `Size` 有槽位、**无别名**（它不参与 §11 的 V1.0 验收，也不接受任何文件名认领）。这条既是设计决定也被 `claim_matches_independent_expected_table` 机器断言（「覆盖 ⟺ used_in_v1」）。
测试侧另立一张**逐字手抄自本节全部 43 条**的期望表（`kExpectedAliasRows`）。它的必要性由 M1a 的两次守卫制造实验确定：
① 用 `alias_table()` 自身当输入又当期望的自检（`claim_every_alias_hits_its_own_slot`），注入同形字符后**一字未红** —— 同义反复；
② 只「每个槽抄一条」仍不够，破坏落在没被抄到的别名上时照样全绿；
③ 纯 ASCII 拼写破坏（`pointer` → `pointre`）连 ASCII 守卫也看不见。
所以现在做**双向全表对照**：实现表每一行必须在手抄表里逐字存在（含指向的槽位），行数同为 43。

可扩展性判据（机器可检）：**新增一个槽位或一个别名 = 只在表里加一行，不需要改任何 `if/switch`**。若某次改动为「加一个别名」动了逻辑代码，视为违背契约。
`-resize` 系列别名是为了吃下 XCursor 风格命名；这是数据驱动的真实理由，不是装饰。

**未认领文件必须报告**，不得静默忽略。认领结果是一张表：`文件 → SlotId | 未认领(原因)`。

**对角槽的真实结构（M2b 实测纠正）**：Windows 里对角只有**两个**注册表值 —— `SizeNWSE` 与 `SizeNESW`，
不是一个方向一个值。所以同一张对角图（例如 `nw_resize.png` 与 `se_resize.png` 其实是同一形状）**都映射到同一个槽**。
由此产生两条必须处理的规则：

1. **重复认领必须报告，不得任选其一**：素材包里同时存在 `nw_resize.png` 和 `se_resize.png` 时，
   `SizeNWSE` 被两个文件认领。这不是错误，但选哪个必须是**显式结果**（报告「该槽由 X 提供，Y 被忽略」），
   绝不能静默取第一个 —— 静默会让用户永远查不出为什么皮肤不是他以为的那张。扫描器落地时必须实现（M2b 后续步）。
2. **方向靠谁翻？**：一个值要同时服务左上与右下两个方向，方向信息只能来自**镜像**：
   OS 是否自动镜像 `SizeNWSE` 到东南角，**未验证**（新增 U19）。
   若 OS 不镜像，则单槽单图必然有一个方向是错的 —— 那是对角图素材的固有约束，不是我们的 bug，
   但必须在 UI 上说清楚，别让用户以为皮肤坏了。

推论：**素材包覆盖不满 15 个槽是正常的部分认领**，不是 bug；UI 与 CLI 都要把「缺哪几槽」列出来。
另：§5.1 的「一份 spec 一个 hotspot」已由 U15 的实测结论改向为**帧级 hotspot**（M3 落地）。

### 5.3 hotspot 默认规则与覆盖

| 组 | 默认 hotspot |
|---|---|
| Arrow, Help, Wait, AppStarting | `(0, 0)`（箭头尖在左上角） |
| IBeam | `(w/2, h*1/5)` |
| UpArrow | `(w/2, 0)` |
| Hand | `(w*3/10, h*1/5)`（食指尖） |
| Crosshair, SizeAll, SizeWE, SizeNS, No | `(w/2, h/2)` |
| SizeNWSE | `(0, 0)`（图按西北角朝向画；东南端是否由 OS 镜像 = U19，未验证） |
| SizeNESW | `(w-1, 0)`（同上，NE 端为基准） |
| NWPen | `(w/2, h/2)`（**未目测**：笔形图形的热点语义无人确认，M5 必须实测） |

以上为**目测校正的初值**，不是权威数值；M5 必须提供「目测校正 + 写 sidecar」的闭环，且在总结里标注哪些槽位尚未目测确认。

**sidecar**：`<stem>.meta`（INI 片段），与图片同目录，只允许承载参数，不允许承载身份：

```ini
hotspot = 3,1          ; 覆盖 §5.3 默认
size = 32,48           ; 覆盖 D8 默认档位
trim = none            ; 仅 SVG：none | auto
frames = w0.png,w1.png,w2.png   ; 仅动画(D21b)：合成 .ani 的帧序列，路径相对本目录
rate = 6              ; 仅动画(D21b)：每帧停留 jiffies（1 jiffy=1/60s≈16.67ms；6≈100ms，等待类常用 6~8；勿按毫秒填否则慢如卡死）
```

未知 key → 报错（不忽略）；`hotspot` 越界 → 报错（§6 的 H5）。

> **实现状态（诚实标注）**：**`hotspot=` / `size=` / `trim=` 已落地并被 build 链消费**（commit M11）：
> pipeline 读同目录 `<stem>.meta`，解析后把 size 覆盖 D8 默认档、hotspot 走 `has_hotspot_override`
> 逐帧 `scale_hotspot`、trim 驱动 SVG 贴边；未知 key / 坏值 ⇒ 该槽带原因失败、挡在写注册表前
> （测试 `tests_m11` 含端到端正/负，用 cur_reader 独立 oracle 证明确实改变了产出）。
> **`frames=` / `rate=` 仍未接**（属 D21b 合成动画，随 `.ani` writer 落地）——现在出现会按「未知 key」拒，
> 绝不假装已支持动画合成。

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
| H7 | 各帧 `biPlanes==1`。**产出**只允许 32bpp；**接受**（`.cur` passthrough 的校验）允许 `biBitCount ∈ {1,4,8,24,32}`，其中 15/16bpp 明确拒绝 | 加载成功但颜色错乱；15/16bpp 无 alpha 通道，按其掩码解释透明会得到错误结论 |
| H8 | 帧按 `w*h` 升序、目录项偏移严格连续无空洞 | 部分系统挑帧失败，只在特定「指针大小」下坏 —— 最难归因的一类 |
| H9 | 目录项 `bytes_in_res` 必须**等于该帧实际写入的字节数**；文件总长 = `6 + 16*n + Σ条目长`，不得有多余尾部字节 | 短了：reader 把下一帧的头当本帧掩码读，偏移整体错位；长了：通常仍加载成功（**危险**，不报错不代表合法）→ 必须有长度断言 |
| H10 | **不产出「合法但看不见」的光标**：帧的 alpha 直方图若全为 0，writer 拒收 | 用户换完皮肤后光标消失，只能靠还原按钮救 —— 严重度高于任何显示瑕疵，必须前置拦 |
| H11 | XOR 位图与 AND 掩码都按**底向上（bottom-up）**存储；`CursorSpec` 是 top-down，writer 负责翻转 | 图形上下颠倒。非对称夹具（「上三行不透明、底一行半透明」）+ 独立实现生成的 golden 才能同时抓住行序与偏移错误；对称夹具抓不到 |

H9 特别说明：容错的加载器会让坏文件「看起来没问题」，所以自检绝不能以「没报错」为通过标准。

---

## 7. 应用与还原

### 7.1 应用序列（`apply/`，唯一写入者）

```
1. 采集基线快照（§7.2）→ state/baseline.ini
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

> **V0.7 补充（D21）**：第 2 步的产物可为 `.cur` 或 `.ani`（`.ani` 走 §4.4）。认领扫描相应接受 `.ani`
> 扩展名；第 3 步 L1 对 `.ani` 只验「可被加载器接受」，**动画真伪不在 apply 内判、留给 §8 L3 真机**（U7）。
> 「坏图不进系统」的总保护对 `.ani` 同样成立：合成失败或触 U7 上限 ⇒ 中止、不写注册表。

### 7.2 基线快照：三态，不是两态

每个槽位的「之前」有三种可能，必须区分记录：

| 状态 | 落盘表示（实现用的行格式见下） | 还原动作 |
|---|---|---|
| 值存在 | `S\t<值名>\tP\t<原字符串>` | 写回原字符串 |
| 值不存在 | `S\t<值名>\tA\t` | `RegDeleteValueW` **删除该值**（写空字符串是错的） |
| 整个键不存在 | 首行 `V\tk`，各行标 `K` | 跳过：不创建键、不写值 |

除 15 个槽位外，同一键下还有两个必须快照的值（`X\t` 前缀）：键的**默认值**（方案显示名，名字为空，落盘写作 `@default`）与 `Scheme`。
不快照它们就不是真还原（R5）。

文件是 `state/baseline.ini`，**首次 apply 才创建，之后永不覆盖**（避免把「原始默认」当成「上一次我写进去的路径」）。
两条实现期新增的规则：

1. **文件存在但读不出来 ⇒ 拒绝 apply**，不写注册表。原实现只看「存在与否」，于是损坏的基线会让 apply 照常执行、
   而之后的还原必然失败，用户只剩「复位默认」这条粗粒度退路——方向错了。
   这条已由 `m2_apply_refuses_when_baseline_is_unreadable` 锁住。
2. **不设 `baseline.prev`**：在「永不覆盖」语义下不存在「旧快照被新快照顶掉」的时刻，
   真正要防的是文件损坏，而损坏的正确反应是拒绝 apply，不是留一份副本让人事后猜。
   原设计的 prev 机制作废（记在这里以免被当成疏漏重新加回来）。

格式上刻意不用 JSON：本项目没有任何 JSON 解析器，而行格式（制表符分隔 + UTF-8）
与 sidecar 的 `.meta` 是同一种极简语法，能共用一套读写代码。读回时会核对**记录条数**
（15 槽 + 2 额外），条数不符直接拒——半份快照被当成完整快照是最坏的失效形态。

### 7.3 还原（D13 两个动作）

- **还原到运行前**：按 §7.2 三态逐槽回写 → 删 `applied\` → `SPI_SETCURSORS`。
- **复位系统默认方案**：删 `HKCU\Control Panel\Cursors` 下全部自定义值 + 清 `Scheme` → `SPI_SETCURSORS`。此动作**不需要**基线，是兜底出口，UI 上必须与前者分列两个按钮、两种确认文案。

### 7.4 抢占探测（不写死 key path）

Windows 10/11「设置 → 辅助功能 → 鼠标指针 → 大小/颜色」的自定义值优先级可能高于 HKCU 自定义光标。**该设置的实际存储位置未证实**，因此本项目**不猜 key path**，改用行为探测：

> 应用后执行 §8 的 L3。若 L3 读到的生效帧与期望帧不符，且 L1/L2 均通过，则判定为「被系统设置抢占」，UI 明确提示「请检查 设置→辅助功能→鼠标指针 的大小与颜色是否为默认」，而不是报「应用成功」。

这是把外部事实的不确定性转成可诊断分支，而不是让用户当调试器。

### 7.5 watcher 自动生效（D22；V0.7）

**默认关闭**，GUI/CLI 显式开启。开启后：后台线程用 `ReadDirectoryChangesW` 监听**当前已选皮肤目录**，
任一文件增/删/改名经 `watcher.debounce_ms` **去抖**后，**重跑同一条 §7.1 apply 管线**（含 L1 前置中止、
D11 原子替换），并把新结果刷新到 §9 面板。三条硬约束：

1. 仍受「坏图不进系统」保护：合成/L1 失败 ⇒ 中止、不改注册表（与手动 apply 完全同一条链，不另写一套）。
2. 它**会自动替换用户正在使用的指针** —— 属系统状态变更；「开启 watcher」这一动作本身即用户的授权，
   但 UI 文案必须写明白这一点（对齐 §8「L2/L3 改指针需授权」）。
3. 半写与风暴保护：只认原子替换后的最终名（D11）；去抖窗口内多事件**合并成一次重放**，避免抖动风暴、
   避免读到写一半的文件。

可测接缝：把「事件流 → 是否触发一次重放」的**去抖/合并**做成纯函数（喂带时间戳的事件序列断言输出），
其余（真的自动换指针、去抖体感、防抖风暴）只能真机判 → 新增 U23。

> **实现状态（M7，进行中）**：判据①（纯去抖/合并）**已落地** —— `core/debounce` 的 `Debouncer` 是单一
> 真身、`compute_replay_times` 是其批式折叠（M14 覆盖），另设**实时契约直测**（`on_event/due/fire`+封顶），
> 实测能抓到批式折叠看不见的 bug（`fire` 不清 pending 时仅直测红）。②接线**已落地并真机确证**：`ui` 后台线程
> `ReadDirectoryChangesW`(overlapped)→`PostMessage` marshal→主线程 `Debouncer`+`SetTimer`→复用同一条
> `do_apply`；`.tmp` 中间名忽略（只认原子替换后的最终名）；**默认关闭**、按钮显式开关、`WM_DESTROY` 回收
> 线程/句柄；`debounce_ms/max_wait_ms` 走 §10 `settings.ini`（缺则 300/1000）。**真机**：开自动后改 `watch-demo`
> 里一张图 → `build\applied\Arrow.cur` mtime 在**同一秒自动刷新**、用户肉眼见指针切换到手型 → **M7②核销**。
> **③风暴/半写体感仍未专门测**（未做连发改压力），U23 记「部分核销：自动生效已验、防抖风暴层未验」，**M7 未结**。

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

**L1 可以且应当早于任何真机 apply 跑**：`LoadCursorFromFileW` + `GetIconInfo` 只在**当前进程**内加载一个光标对象，不写注册表、不广播、不改变用户正在使用的指针。所以 M2a 用它就地核销三个对 Windows 加载器的假设 —— U12（hotspot 位于目录项的两个 WORD）、U14（`biSizeImage` 写实际 XOR 长度）、U16（AND 行过填充被接受）。
反过来，**L2/L3 必须先取得用户单独授权**：它们会改 `HKCU` 并当场替换用户正在用的指针。

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

**构建与测试的先后是判据的一部分**（M1a 实测踩中）：`tests.bat` 无法分辨手里的 exe 是新建的还是上一轮留下的。一次编译失败（`build.bat` exit 4）之后再跑 `tests.bat`，会得到一份**排版正确、内容属于已经不存在的源码树**的红名单。因此：判绿一律走 `verify.bat`（它要求 build 退出码为 0 才运行 tests）；单独贴 `tests.bat` 的输出时必须同时给同一轮的 `BUILD_EXIT`。

### 9.3 目录布局

```
CursorSwitch/
  AGENTS.md  DESIGN.md  README.md
  build.bat  tests.bat  verify.bat  # 三个脚本必须 CRLF、纯 ASCII（本机 PS5.1 按 GBK 读 .ps1；
                                    #  且 cmd 会把 echo 里的裸 `>` 当重定向 —— 见 AGENTS 陷阱表）
                                    #  verify.bat = build 退出码为 0 才允许跑 tests，防陈旧二进制冒充结果
  src/
    core/     ir.* slots.* claim.* cli.* version.h cur_format.h cur_writer.* cur_reader.*
    sources/  png_source.* svg_source.*        # M3/M4 才建；hotspot 默认规则并入 slots.*（一张表 + 一个求值函数）
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
| `frames.sizes` | `16,32,48` | 生成档位（D8）；**D23 已落地：由 `config/settings.ini` 注入**（缺失=默认；坏文件=中止） | M1 / 生效 M6 |
| `frames.allow_upscale` | `false` | 小图不放大；**暂不经 settings.ini 开放**（PNG/SVG 通道只做下采样，无放大实现；置 true 会 NoUsableFrame，故 settings 里按未知 key 拒） | M3 |
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
| `watcher.debounce_ms` | `300` | D22 目录变更合并窗口（原「仅 BACKLOG watcher 用」的 `probe.debounce_ms` 归此） | M7 |
| `watcher.max_wait_ms` | `1000` | D22 封顶：持续风暴最迟每 max_wait 触发一次，防「永不重放」的饿死（`§7.5`/debounce 纯函数） | M7 |
| `watcher.enable` | `false` | D22 自动生效总开关，默认关 | M7 |
| `watcher.backend` | `readdirchanges` | `ReadDirectoryChangesW`；轮询为后备 | M7 |
| `ani.default_rate_jiffies` | `6` | D21b 合成 `.ani` 每帧默认停留（jiffies，1/60 秒；非毫秒） | M6 |
| `ani.max_frames` | `待实测` | U7 上限；触顶**报错拒产出**、不静默截断 | M6 |
| `ani.max_dimension` | `待实测` | U7 单帧尺寸上限；同上 | M6 |
| `config.path` | `config/settings.ini` | D23 读取入口（行式，复用 baseline/.meta 语法）；文件缺失⇒全默认 | M6 |
| `writer.max_dir_entries` | `12` | 容器上限（D8 七档 + 余量） | M1 |
| `writer.min_frame_bytes` | `128` | AND 掩码最小行填充（1bpp，4 字节对齐） | M1 |
| `writer.min_dir_gap_bytes` | `64` | **帧条目总字节下限**：不足则用零补齐，保证 `bytes_in_res` 与实际写入字节一致（H9）。语义不是「偏移间隔」，也不得当作只影响目录项而不写数据 | M1 |
| `writer.reject_fully_transparent` | `true` | H10；设 false 仅供调试，默认必须 true | M1 |
| `reader.max_entries` | `64` | reader 的**宽松**上限（判「count 被写坏」用），与 `writer.max_dir_entries=12` 角色不同：产出侧严格，校验侧宽松 | M1 |
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
| M6 | V0.7 | 输出尺寸可配（D23，落地 `settings.ini`）+ `.meta` sidecar 首次解析 + `.ani` 通道（D21：透传 + 合成） | ① `settings.ini` 生效：改 `frames.sizes` → 产出帧集随之变（单测 e2e 用 cur_reader 独立核）；坏 settings.ini ⇒ 中止不写册；`allow_upscale` 通道只做下采样、放大未实现故按未知 key 拒（见 §10）；② `.meta` 解析：`size/hotspot/trim/frames/rate` 生效、未知 key/坏值/越界报错（负对照）；③ `.ani` 透传 L1 可加载 **且** 真机确证会动画（U7 已核销）；④ `.ani` 合成：`LoadCursorFromFileW` 接受自造 .ani（离线）+ **真机确证按 `iDispRate`(jiffies) 播放**（U22 已核销）；`ani.max_frames` 触顶报错拒产出不截断 |
| M7 | V0.8 | watcher 自动生效（D22） | ① 纯去抖/合并函数单测（喂时间戳事件序列→断言重放次数）；② 真机：改一个文件→去抖窗口后指针自动更新、关闭即停；③ 半写/风暴不产坏帧（D11+去抖），L1 失败仍中止不写册。**进度：① 已（M14，含实时契约直测+守卫制造）；② 已（真机确证：改图→`Arrow.cur` 同秒自动重写 + 肉眼见切换，已 `--restore` 逐字节还原）；③ 未专门测（防抖风暴/半写体感）→ M7 未结、暂不打 `V0.8`。** |
| — | **V1.0** | MVP 验收 | **15 个 v1 槽位**（§5.2 表，M2b 实测后的真实集合；原先写作 16/13 都是凭错误槽表得出的，已随实测纠正）用一套真素材全链路 apply→L3 逐槽通过→还原无残留。**验收范围随 §1 扩张**：动画/尺寸/watcher 的判据分别在 M6/M7 的**真机**条目里核销，不得因「加载成功」而判过 |

> **M5 期间的实现纠偏（如实记录）**：`build_only` 在 M2–M4 一直只做 `.cur` 透传 —— M3/M4 交付的是 PNG/SVG **通道模块 + 单元级测试**，但端到端 apply 从没调用它们，于是 §1 目标 2/3（PNG/SVG 可应用）实际断线，且因为 APP 构建列表当时不链接 `png_source/svg_source/cur_writer/pixel` 也照样「绿」而没被发现。commit `69a7efc` 把三条通道按扩展名接入 `build_only`，并用 `tests_m6` 的正/负/回归/坏源不写注册表钉死。**教训**：模块测试全绿 ≠ 通道在业务链上可达；端到端判据必须显式覆盖每条通道。

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
| R9 | `.ani` 容器/帧序引用凭记忆写错（本会话差点按记忆写成 FourCC=`ani `，实测是 `icon`） | 产出的动画 .ani OS 不认或不播放 | 以**反解本机 `aero_busy.ani`** 为格式依据；writer 产物**反解回读**比对；播放真伪走 M6 真机（U7/U22），不靠「能加载」下结论 |
| R10 | watcher 会**自动替换用户正在使用的指针**（系统状态），且可能有抖动风暴/读半写文件 | 用户指针被打断、竞态读到坏帧 | 默认关闭 + 显式授权；去抖合并（§7.5）；只认原子替换后的最终名（D11）；重跑同一条「L1 失败即中止不写册」管线 |
| R9 | CDN 镜像拉取的 vendor 文件与上游不一致（GitHub raw 实测被 reset，只能走镜像） | 构建出来的行为与上游文档不符，且难以归因 | D19/D20：入库时固定 sha256 + 头部版本行双签核对，记录在 `vendor/README.md`；不一致就不入库 |
| R10 | 同形字符（如西里尔 е）混入数据表 | 别名静默失配，认领阶段少测且不报错 | 已发生一次并修掉；`vendor`/别名/槽表纳入「表内容必须纯 ASCII（除显示名）」守卫，M1 落地并配正负对照 |

---

## 13. 未验证项清单（当前状态，交付时逐条核销）

| ID | 断言 | 状态 | 由哪一步核销 |
|---|---|---|---|
| U1 | `HKCU\Control Panel\Cursors` 的值名集合 == §5.2 表 | **部分核销（M2b 实测枚举）**：已按本机真值纠正 4 处错名并补 `NWPen`；「写任意名字是否被接受」仍需 L3 逐槽定。legacy 名（`Cross`/`Unavailable`）是否仍可用未验 | M2b 的 L3 |
| U2 | `SPI_SETCURSORS` 的 uiParam/pvParam/flags 确切传法 | **已核销（M2 真机实测）**：`SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0)` 生效；同一注册表内容下再调一次带 `SPIF_SENDCHANGE` 的调用，三个采样点指纹**毫无差别**（`flags_made_visible_difference=0`）→ flags 取 0 即可 | 已落地 |
| U3 | L3 能否稳定取到生效帧并做像素比对 | **已核销（M2 真机实测）**：`SetCursorPos` → `GetCursorInfo` → `DrawIconEx` 进 32bpp DIB 这条路可行。aero 箭头采到 `32x32 opaque=144 colors=17`，我们的实心块采到 `32x32 opaque=1024 colors=1`，还原后指纹逐位回到原值 → 指纹与不透明像素数都是可用的判据 | 已落地 |
| U4 | 设置的指针大小/颜色是否抢占、存储位置 | **未验证** | 不做假设，走 §7.4 探测 |
| U5 | PNG 压缩条目在 `.cur` 中是否被光标路径接受 | **未验证** → 已按 D7 绕开 | 不需要（若未来优化再验） |
| U6 | resvg 在本机的可集成性 | **已实测：前置不成立（本机无 Rust 工具链）**；「能否产出可链静态库」在装好工具链前无从谈起 | 已由 B-01 选型决策接管 |
| U7 | 动画光标的系统尺寸/帧数/速率策略上限，以及**合成/透传的 `.ani` 是否真的播放动画** | **透传路径已真机确证**：把 `aero_working.ani` 作 `arrow.ani` 经 D21a 透传应用，用户肉眼见箭头**转圈动画**（commit 记录见 V0.6.x/本轮），故「`.ani` 经 HKCU+SPI 会被 OS 当动画播放」= 已验（动画真伪靠人眼，L3 单帧判不了）。**仍未验**：帧数/尺寸/**速率**上限（`aero_busy` speed=0=交系统默认），以及 D21b **自造帧序列的播放** | 上限逐格逼近 + D21b 合成动画的真机目测，见 M6/U22 |
| U8 | §5.3 各槽 hotspot 默认值的目测正确性 | **未验证** | M5 校正闭环 |
| U9 | M0 之前本项目无任何代码：`build.bat`/`tests.bat`/exe 均未创建、未实测 | 已核销（M0 实测，数字见 `docs/summaries/V0.1-*.md`） | M0 |
| U10 | 静态 CRT（`/MT`）下最小 exe 实际尺寸区间（M0 阈值 ≥65536 的依据） | 本轮构建实测 | M0 |
| U11 | 本机 WIC 是否支持 SVG 解码 | **未验证（先前凭既有知识写成「不支持」，已标注）** | 仅当有人提议走该路径时才验，否则不依赖 |
| U12 | `.cur` 目录项里 hotspot 占据 `wPlanes/wBitCount` 两个 WORD，且 `LoadCursorFromFileW` 读的就是这两个字段 | **已核销（M2a 实测）**：hotspot 位于目录项的两个 WORD，被真实加载器接受。判据：`LoadImage(exact 16x16)` 回读 hotspot 逐字等于写入值 (4,4)；golden 的 (1,1) 在 4x4 / 8x8 两帧上均回读 (1,1)。取写位置：目录项偏移 4–5 / 6–7（即 ICO 布局里 planes/bitcount 的同一字节位） | M2a 已用 `GetIconInfo` 实测核销 |
| U13 | 「每通道 `<= alpha`」作为预乘必要条件，是否会把合法输入误拒（如上游输出 alpha=255 之外的边界） | **未验证** | M1 记录误拒率；若误拒则改为告警 + 就地预乘 |
| U14 | `BI_RGB` 下 `biSizeImage` 写 XOR 字节数是否被各加载路径接受（0 也常见） | **已核销（M2a 实测）**：`biSizeImage` 写 XOR 实际长度被加载器接受（golden 与三个单帧样本均 `loaded=1`，reader 侧仍不依赖该字段） | M2a L1 实测 |
| U15 | 多帧时单一 hotspot 的缩放规则 | **已核销（M2a 实测）**：加载器在**帧之间不做** hotspot 换算（多帧文件 hotspot 写 (1,1)，指名取 16 与取 48 都回读 (1,1)）；只有重采样到系统默认尺寸时才按比例缩放（(4,4)@16 → (8,8)@32）。因此多帧必须**逐帧写缩放后的热点**。**已落地（M2b）**：`CursorSpec` 改为帧级 `Frame::hotspot`，新增 `scale_hotspot()` 按 `round(v*to/from)` 逐帧换算。
回归证据：把换算退化为照抄原值后 `l1_system_loader_resamples_and_scales_hotspot` 变红 2 条（48 帧期望 (3,3) 未达 + `m16 != m48` 结构性断言） | 已落地 |
| U16 | AND 掩码行按 `max(对齐值, 128)` **过填充**，系统加载器是否照单全收 | **已核销（M2a 实测）**：AND 行过填充 `max(对齐值,128)` 被加载器接受 —— 两套非系统尺寸的 golden 帧（4x4 / 8x8，掩码行距 128）均可加载并按帧导航 | M2a L1 实测 |
| U17 | `NWPen`（手写笔）的正确热点语义，以及它是否受「指针大小」体系管辖 | **未验证**：该槽是 M2b 实测才发现的，原计划完全没有；默认热点目前猜的是中心 | M5 目测校正 |
| U18 | `CursorBaseSize` 是否就是「设置→辅助功能→鼠标指针→大小」滑条的存储位置 | **部分证据，仍未核销**：本机实测 `CursorBaseSize = 32`，与 `SM_CXCURSOR = 32` 一致（弱一致，不唯一）。要定案必须**动一次滑条**再读，属需要用户配合的实验，未做 | 用户配合时核销；在此之前 §7.4 仍用行为探测，不依赖此值 |
| U19 | OS 是否把 `SizeNWSE`/`SizeNESW` 自动镜像到对角另一端 | **仍未验证，且 M2 的实验设计无法验证它**：真机闭环用的皮肤是**实心对称方块**，镜像后一模一样 —— 即使 OS 不镜像，这个皮肤也测不出来。要验必须换一张**反对角不对称**的对角图（例如 NW 端粗、SE 端细）再目测两端。**记为方法缺陷而非结论**，别把「没报异常」当成「验过了」 | **反对称夹具已备好（M7，commit `bf0a1a0`）**：`skin/sizenw.svg` / `skin/sizene.svg` 画成一端实心箭头、另一端渐细短尾，可在真机 L3 目测判是否被镜像；M7 仅离线自检素材合法性（build_only/L1），**未验镜像**。V1.0 验收前用这套真素材做 apply→L3 目测 |
| U22 | 合成 `.ani` 的帧序引用语义（`iDispRate` 单位/落点、`bfAttributes` 内联完整 CUR 是否被采纳）能否被 OS 正确播放 | **已核销（真机）**：`rate=10` 两帧（箭头↔红圈）apply 到 Arrow，肉眼见约 6 次/秒交替（=10/60s），OS 按 `iDispRate`(jiffies) 播放合成动画；无 seq 顺序即帧序 | M6 真机（已完成） |
| U23 | watcher 去抖/合并与「自动换指针」的真机表现（防抖动风暴、防读到半写文件、关闭即停） | **部分核销**：自动生效端到端已真机确证（改图→Arrow.cur 同秒重写+肉眼见切换）；**防抖风暴/半写体感未专门测**；关闭即停由 `WM_DESTROY`/停止按钮走 `CancelIoEx`+join（代码在位，未压测）。 | M7 真机（③待补） |

---

## 14. BACKLOG 入口（只进不出，不顺手实施）

`.ico` 输入通道；`.ani` 动画光标；目录 watcher 常驻自动生效；游标包导入/导出（含方案打包）；逐 DPI 素材集；写入 `HKU\.DEFAULT`（提权，需另行立项）；`Size` 遗留槽位是否纳入；hotspot 可视化编辑器；多语言 UI。
