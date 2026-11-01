// src/core/svg_source.h — SVG 通道：守卫 → 光栅化 → 贴边 → 与 PNG 共用预乘/缩放路径
//
// 顺序是刻意的：守卫在光栅化**之前**。nanosvg 对不支持的构造静默忽略，
// 一旦先画再检查，我们就已经拿到一张"看起来对"的错图，检查只能事后追认。
#pragma once

#include "core/ir.h"
#include "core/svg_guard.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cs::core {

struct SvgOptions {
    uint32_t work_resolution{1024};      // §10 svg.work_resolution
    bool trim_transparent{true};         // §10 svg.trim_transparent
    double min_content_ratio{0.35};      // §10 svg.min_content_ratio（出血启发）
    std::vector<uint32_t> sizes{16, 32, 48};
    bool allow_upscale{false};
    bool has_hotspot_override{false};
    Hotspot hotspot_at_source{};         // 以贴边后的尺寸为参照
};

struct SvgReport {
    GuardResult guard;
    uint32_t raster_w{0};      // 光栅化整幅
    uint32_t raster_h{0};
    uint32_t trimmed_w{0};     // 贴边后的内容尺寸
    uint32_t trimmed_h{0};
    double content_ratio{0.0}; // 贴边面积 / 整幅面积
    bool low_ratio_warning{false};
};

enum class SvgStatus : uint8_t {
    Ok,
    EmptyInput,
    GuardRejected,     // 含违规清单，未光栅化
    RasterFailed,
    NoUsableFrame,     // 尺寸表与贴边后尺寸完全无交集且原生尺寸不合法
    InternalMismatch,
};

// 探针：光栅化一张图并回报**指定坐标处 nanosvg 原始输出的 RGBA 字节**（未经预乘处理）。
// 存在的理由是一个必须实测的事实：若 nanosvg 已经输出预乘数据，而我们又乘一次，
// 颜色会整体变暗，而「每通道 <= alpha」这条判据**抓不到双重预乘**（双重预乘仍满足该式）。
// 所以这里先取原始值，据此决定要不要预乘，而不是照文档/记忆假定。
bool svg_probe_raw_pixel(const uint8_t* data, std::size_t size, uint32_t resolution,
                         uint32_t x, uint32_t y, uint8_t out_rgba[4], std::string& detail);

SvgStatus load_svg_to_spec(const uint8_t* data, std::size_t size, SlotId slot,
                          const SvgOptions& opt, CursorSpec& out, SvgReport& report,
                          std::string& detail);

}  // namespace cs::core
