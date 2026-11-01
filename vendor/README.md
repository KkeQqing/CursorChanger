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

## 未入库 / 待定

| 依赖 | 状态 |
|---|---|
| `resvg`（SVG 光栅化，D4） | **未入库**。本机无 Rust 工具链（`cargo`/`rustc`/`rustup` ABSENT），无法自建 → 见 `docs/BLOCKED.md` B-01，选型待追认 |
| `nanosvg`（D4 降级候选） | 未入库（等 B-01 定案，避免入库后又删） |
| gtest | 不引（D17：harness 自写，省一层构建依赖） |
