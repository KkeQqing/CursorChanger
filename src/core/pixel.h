// src/core/pixel.h — 像素约定与两种确定性变换（预乘、面积平均）
//
// 纯内存操作，不碰文件与 Win32（§2 层规则）。公式与取整方向是**契约**（DESIGN §4.2）：
// 两侧（C++ 与 tools/gen_png_fixtures.py）必须逐字一致，否则 golden 会出现「差 1」的假失败。
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cs::core {

// top-down BGRA32，premultiplied
struct Image {
    uint32_t width{0};
    uint32_t height{0};
    std::vector<uint8_t> pixels;
};

// 单通道预乘：(v*a + 127) / 255，即四舍五入；a==0 时为 0。
inline uint8_t premul_one(uint8_t v, uint8_t a) {
    return a == 0 ? static_cast<uint8_t>(0)
                  : static_cast<uint8_t>((static_cast<uint32_t>(v) * a + 127u) / 255u);
}

// straight RGBA（stb 的输出顺序）-> premultiplied BGRA
void rgba_straight_to_bgra_premul(const uint8_t* src, std::size_t pixel_count,
                                  std::vector<uint8_t>& dst);

// 无 alpha 通道的 RGB 输入：alpha 一律 255（§4.2，不做亮度抠图）
void rgb_to_bgra_opaque(const uint8_t* src, std::size_t pixel_count, std::vector<uint8_t>& dst);

// box-average 缩放，作用于**已预乘**的数据。目标像素覆盖源矩形
// [x*W/dw, (x+1)*W/dw) × [y*H/dh, (y+1)*H/dh)，每通道 floor(sum / count)。
// 向上取整会与本文件的 Python 参照不一致，故这里刻意用向下取整。
bool resize_box_premul(const Image& in, uint32_t dw, uint32_t dh, Image& out);

// 是否至少有一个 alpha>0 的像素（H10 的前置，IR 里也有一份，这里供通道早报错）
bool any_visible(const Image& img);

}  // namespace cs::core
