# vendor/

第三方源码，**入库**（D16：本机 GitHub 直连实测被 reset，构建期不联网）。
每个文件必须在此登记来源与指纹（D19/D20）。镜像内容与上游可能不一致 → **校验不通过就不入库**（R9）。

## stb_image.h

| 字段 | 值 |
|---|---|
| 用途 | PNG 解码（DESIGN D3，M3 通道使用；M0 仅入库，未被任何源文件 `#include`） |
| 项目 / 作者 | stb / Sean Barrett，public domain |
| 上游 | `https://github.com/nothings/stb` · `master` · `stb_image.h` |
| 实际拉取地址（镜像） | `https://fastly.jsdelivr.net/gh/nothings/stb@master/stb_image.h` |
| 拉取日期 | 2026-11-01 |
| sha256 | `594C2FE35D49488B4382DBFAEC8F98366DEFCA819D916AC95BECF3E75F4200B3` |
| 字节数 | 283010 |
| 版本签名 | 首行 `/* stb_image - v2.30 - public domain image loader ...`（sha 之外的第二道内容核对） |

拉取命令（本机实测：`raw.githubusercontent.com` Connection reset、`cdn.jsdelivr.net` schannel 中断、`fastly.jsdelivr.net` 可用）：

```powershell
C:\Windows\System32\curl.exe -sSL --max-time 40 `
  -o vendor\stb_image.h `
  https://fastly.jsdelivr.net/gh/nothings/stb@master/stb_image.h
```

核验命令：

```powershell
(Get-FileHash -LiteralPath vendor\stb_image.h -Algorithm SHA256).Hash
# 期望：594C2FE35D49488B4382DBFAEC8F98366DEFCA819D916AC95BECF3E75F4200B3
```

**归一风险（M0 实测）**：`.gitattributes` 用 `* text=auto eol=lf`，若上游文件本身是 CRLF，checkout 后工作区字节会变，sha256 与上表**必然不符**。本次实测该文件 CRLF 计数为 0，工作区 sha == git blob sha == 记录值，故无冲突。**若将来某个 vendor 文件出现不符，先给它加 `-text` 关闭转换，再重记 sha，不要反过来改 sha。**

## 编译警告抑制（必须留档）

`src/core/png_source.cpp` 里对 stb 的 include 外面包了一层局部抑制：

```cpp
#pragma warning(push)
#pragma warning(disable : 4505)   // 仅此一条，仅包住这一个 include
#include "stb_image.h"
#pragma warning(pop)
```

- 原因：项目判据是「0 编译警告」。本机实测 stb_image 2.30 在 `/W4 + STBI_ONLY_PNG` 下报
  3 条 C4505（`stbi__cpuid3`、`stbi__addints_valid`、`stbi__mul2shorts_valid` 未被引用 ——
  前两者属于其它格式与 SSE 探测路径，只编 PNG 时用不到）。
- 边界：**不降整个项目的 `/W4`**，抑制范围严格到该 include 之内。
- 升级 stb 时若出现**新的警告种类**，日志会立刻变脏，而不是被这条 disable 顺手压掉。

## 未入库 / 待定

| 依赖 | 状态 |
|---|---|
| `resvg`（SVG 光栅化，D4） | **未入库**。本机无 Rust 工具链（`cargo`/`rustc`/`rustup` ABSENT），无法自建 → 见 `docs/BLOCKED.md` B-01，选型待追认 |
| `nanosvg`（D4 降级候选） | 未入库（等 B-01 定案，避免入库后又删） |
| gtest | 不引（D17：harness 自写，省一层构建依赖） |
