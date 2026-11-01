# BLOCKED

> 被外部事实卡住的项。每条写清：卡住什么 / 已尝试什么 / 需要谁做什么。

## B-01｜SVG 光栅器选型（阻塞 M4，不阻塞 M0–M3）

- **卡住什么**：`DESIGN.md` D4 选了 resvg，而 resvg 需要 Rust 工具链自建静态库 + C header。
- **已尝试（实测）**：`cargo` / `rustc` / `rustup` 三个命令本机 **ABSENT**；`Get-Command` 与 PATH 均无。GitHub raw 直连被 reset，只能走 CDN 镜像（见 D20）。
- **需要谁做**：用户拍板三选一 —— ① 装 Rust 工具链（引入 ~几百 MB 工具链成本，仍需再验能否产出可链库 + 稳定 C ABI）；② **改选 `nanosvg`**（纯 C 头文件、零工具链、可镜像拉取，代价是支持子集更窄，§4.3.2 特性守卫必须加宽）；③ 找第三方预编译 resvg 包（依赖不可控发布物，需单独判可信度）。
- **默认倾向**：②，因为它不新增工具链、不引入不可控二进制，且「守卫加宽」与本设计既有的红线（宁拒不交付错图）同向。选 ② 需回写 D4 与 §4.3.2。
- **状态：已按默认方案落地（M4），仍请追认。** 实际选 ②：`nanosvg` 已 vendored
  （`vendor/nanosvg/`，含 sha256 与溯源缺口说明），守卫按 §4.3.2 实现为白名单默认拒绝，
  M4 83 用例 / 4156 断言全绿。
  若追认结果改为 ①（装 Rust 换 resvg），守卫与测试可原样复用 —— 通道层与光栅器解耦，
  只需替换 `svg_source.cpp` 的光栅化调用并重测探针。
- **残留缺口**：nanosvg 上游无 tag、无版本宏，拉取用的是会移动的 `master`，
  因此只能以 sha256 为锚，无法固定到上游某个 commit（详见 `vendor/nanosvg/README.md`）。

## B-02｜vendor 拉取通道（已绕过，留档）

- **卡住什么**：入库 stb_image 需要上游字节。
- **实测**：`raw.githubusercontent.com` Connection reset；`cdn.jsdelivr.net` schannel 中断；`fastly.jsdelivr.net` 可用（283010 B，头部 `stb_image - v2.30` 已核对，sha256 记于 `vendor/README.md`）。
- **残余风险**：镜像与上游可能不一致 → R9。做法是入库即固定指纹，构建期不联网。
