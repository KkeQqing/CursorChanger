// src/core/png_source.h — PNG 通道：解码 → 预乘 → 多档缩放 → CursorSpec（DESIGN §4.2）
//
// 归一到 IR 之后与 SVG 通道共用同一个 CurWriter，三通道互不影响（模块化原则）。
// 本文件**不判**「全透明输入」：那是 IR 的不变量（H10），在一份地方判第二次就变成
// 两处规则，改一处忘一处。这里只负责解码与几何。
#pragma once

#include "core/ir.h"
#include "core/pixel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cs::core {

struct PngOptions {
    std::vector<uint32_t> sizes{16, 32, 48};  // §10 frames.sizes
    bool allow_upscale{false};                // §10 frames.allow_upscale
    // sidecar 覆盖的热点：以**源尺寸**为单位给出；缺省时用槽位默认规则逐帧求值
    bool has_hotspot_override{false};
    Hotspot hotspot_at_source{};
};

enum class PngStatus : uint8_t {
    Ok,
    EmptyInput,
    DecodeFailed,      // 不是 PNG / 数据损坏
    NoUsableFrame,     // 尺寸表与源尺寸完全无交集且源尺寸非法
    InternalMismatch,  // 解码后缓冲长度与声明尺寸不符（stb 用法错误）
};

// 解 PNG 字节并产出 CursorSpec（帧按面积升序，满足 IR 的 H8 约定）。
// detail 只在失败时有意义；不包含「猜测的原因」，只写观测到的事实。
PngStatus load_png_to_spec(const uint8_t* data, std::size_t size, SlotId slot,
                          const PngOptions& opt, CursorSpec& out, std::string& detail);

// 供报告：源图像的通道数（3 = 无 alpha，4 = 有 alpha），失败时 0
int png_probe_channels(const uint8_t* data, std::size_t size, uint32_t& w, uint32_t& h);

}  // namespace cs::core
