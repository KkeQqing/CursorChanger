// src/core/ir.h — 中间表示 CursorSpec 与它的入口不变量（DESIGN §5.1）
//
// 三条输入通道（PNG / SVG / .cur）都归一到这个结构，再由 CurWriter 单点封装。
// 本文件**不含任何 Win32 符号**（§2 层规则），像素约定：BGRA32、premultiplied、
// top-down（index 0 为最上一行，容器层的底向上翻转见 H11）。
#pragma once

#include "core/slots.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cs::core {

enum class SourceKind : uint8_t { Png,
                                  Svg,
                                  CurPassthrough };

// 容器约束（DESIGN §6）：目录项用 1 字节表达尺寸，0 代表 256（H6）。
// hotspot 无需独立上限：validate 先验 width<=256 再验 x<width，蕴含 x<=255。
inline constexpr uint32_t kMaxCursorDimension = 256;

struct Hotspot {
    uint16_t x{0};
    uint16_t y{0};
};

struct Frame {
    uint32_t width{0};
    uint32_t height{0};
    std::vector<uint8_t> pixels;  // 恰为 width*height*4 字节，行内无填充
};

struct CursorSpec {
    SlotId slot{SlotId::Invalid};
    std::vector<Frame> frames;  // 至少 1 个；按 height*width 升序
    Hotspot hotspot{};
    SourceKind source_kind{SourceKind::Png};
};

enum class SpecStatus : uint8_t {
    Ok,
    EmptyFrames,        // 帧数 0
    ZeroDimension,      // 某帧宽或高为 0
    SizeTooLarge,       // 某帧超过 256
    PixelBufferSize,    // 某帧字节数 != w*h*4
    HotspotOutOfBounds, // 某帧 hotspot 不在帧内（H5）
    NotPremultiplied,   // 某像素有通道 > alpha（预乘的必要条件，U13）
    FullyTransparent,   // 整份没有任何可见像素（H10）
};

struct SpecError {
    SpecStatus status{SpecStatus::Ok};
    std::size_t frame_index{0};  // 出错帧；无帧上下文时为 0
    std::size_t detail{0};       // 例如实际字节数
};

struct ValidateOptions {
    bool reject_fully_transparent{true};  // H10；仅调试路径可关（DESIGN §10）
};

// top-down BGRA32 构帧；尺寸不匹配时把 pixels 原样带着，由 validate 报错（不夹带修正）。
inline Frame make_frame(uint32_t width, uint32_t height, std::vector<uint8_t> top_down_bgra) {
    Frame f;
    f.width = width;
    f.height = height;
    f.pixels = std::move(top_down_bgra);
    return f;
}

inline std::size_t frame_bytes(const Frame& f) {
    return static_cast<std::size_t>(f.width) * f.height * 4u;
}

// 单帧像素是否满足预乘必要条件（B/G/R <= A）。空 buffer 视为不通过。
bool premultiply_ok(const Frame& f);

// 整份 spec 是否存在至少一个 alpha > 0 的像素。
bool any_visible_pixel(const CursorSpec& spec);

// 入口不变量检查。检查顺序有意义：尺寸先于 hotspot
// （width<=256 且 x<width 蕴含 x<=255，因此 uint16 的目录项写入无需额外上限判断）。
SpecError validate_spec(const CursorSpec& spec, const ValidateOptions& opts = {});

}  // namespace cs::core
