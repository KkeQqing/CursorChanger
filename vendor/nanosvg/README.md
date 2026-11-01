# vendor/nanosvg/

| 字段 | 值 |
|---|---|
| 用途 | SVG 光栅化（DESIGN D4 降级方案，M4 通道使用） |
| 项目 / 作者 | nanosvg / Mikko Mononen，zlib/libpng 风格许可 |
| 上游 | `https://github.com/memononen/nanosvg` · `master` · `src/` |
| 实际拉取地址（镜像） | `https://fastly.jsdelivr.net/gh/memononen/nanosvg@master/src/nanosvg.h`<br>`.../src/nanosvgrast.h` |
| 拉取日期 | 2026-11-01 |

| 文件 | 字节 | sha256 |
|---|---|---|
| `nanosvg.h` | 88980 | `E34FD5D084BE106CEA972D19CE5D27FD96D17BA89F8D06BDCEEE058420C8B2B0` |
| `nanosvgrast.h` | 39398 | `79A9C5F4DB19DEBF9F3A648A1589E96D92854F245A5CB4F3D823F263785234D8` |

两份都是**纯 LF**（实测 CRLF 计数 0），所以 `* text=auto eol=lf` 不会改变 checkout 后的字节，
上表 sha256 在「工作区 / git blob / 重新下载」三处可比。已实测：工作区 == git blob。

## 溯源缺口（如实记录，不当已解决）

- nanosvg **没有版本宏**（搜遍 `nanosvg.h` 无 `NANOSVG_*_VERSION`），也**没有 release/tag**：
  实测 `https://data.jsdelivr.com/v1/packages/gh/memononen/nanosvg` 返回 `"tags": {}, "versions": []`。
- 因此 stb 那种「sha256 + 头部版本行」双锚在这里**只剩 sha256 一个锚**，
  且拉取用的是会移动的 `master` 分支 —— 无法把来源固定到某个上游 commit。
- 后果与缓解：日后重新拉取若与上表不符，只能靠 sha 发现「变了」，无法证明「变成哪个 commit」。
  需要更强溯源时，应改从有 tag 的 fork 或上游打 tag 后拉，并同步更新本表。

## 为什么是它（B-01 的选型理由）

本机无 Rust 工具链（`cargo`/`rustc`/`rustup` 实测 ABSENT），首选的 resvg 无法自建。
nanosvg 是纯 C 单头文件、零工具链、可直接镜像拉取；代价是支持的 SVG 子集明显更小，
所以 `src/core/svg_guard.*` 必须把不支持的构造**拒绝渲染并报因**，
绝不产出「看起来正常但少画了东西」的图（nanosvg 对未知元素是静默忽略，这正是最危险的失败形态）。
