#include "core/pixel.h"

#include <cstring>

namespace cs::core {

void rgba_straight_to_bgra_premul(const uint8_t* src, std::size_t pixel_count,
                                 std::vector<uint8_t>& dst) {
    dst.resize(pixel_count * 4u);
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const uint8_t r = src[i * 4u + 0];
        const uint8_t g = src[i * 4u + 1];
        const uint8_t b = src[i * 4u + 2];
        const uint8_t a = src[i * 4u + 3];
        dst[i * 4u + 0] = premul_one(b, a);
        dst[i * 4u + 1] = premul_one(g, a);
        dst[i * 4u + 2] = premul_one(r, a);
        dst[i * 4u + 3] = a;  // alpha 本身不变
    }
}

void rgb_to_bgra_opaque(const uint8_t* src, std::size_t pixel_count, std::vector<uint8_t>& dst) {
    dst.resize(pixel_count * 4u);
    for (std::size_t i = 0; i < pixel_count; ++i) {
        dst[i * 4u + 0] = src[i * 3u + 2];  // B
        dst[i * 4u + 1] = src[i * 3u + 1];  // G
        dst[i * 4u + 2] = src[i * 3u + 0];  // R
        dst[i * 4u + 3] = 255;
    }
}

bool resize_box_premul(const Image& in, uint32_t dw, uint32_t dh, Image& out) {
    if (in.width == 0 || in.height == 0 || dw == 0 || dh == 0) return false;
    if (in.pixels.size() != static_cast<std::size_t>(in.width) * in.height * 4u) return false;
    if (dw == in.width && dh == in.height) {
        out = in;
        return true;
    }

    out.width = dw;
    out.height = dh;
    out.pixels.resize(static_cast<std::size_t>(dw) * dh * 4u);

    for (uint32_t y = 0; y < dh; ++y) {
        const uint32_t sy0 = static_cast<uint32_t>((static_cast<uint64_t>(y) * in.height) / dh);
        uint32_t sy1 = static_cast<uint32_t>((static_cast<uint64_t>(y + 1) * in.height) / dh);
        if (sy1 <= sy0) sy1 = sy0 + 1;  // 极端比例下保证至少取一行
        if (sy1 > in.height) sy1 = in.height;
        for (uint32_t x = 0; x < dw; ++x) {
            const uint32_t sx0 = static_cast<uint32_t>((static_cast<uint64_t>(x) * in.width) / dw);
            uint32_t sx1 = static_cast<uint32_t>((static_cast<uint64_t>(x + 1) * in.width) / dw);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > in.width) sx1 = in.width;

            uint64_t sum[4] = {0, 0, 0, 0};
            uint64_t count = 0;
            for (uint32_t sy = sy0; sy < sy1; ++sy) {
                for (uint32_t sx = sx0; sx < sx1; ++sx) {
                    const std::size_t k =
                        (static_cast<std::size_t>(sy) * in.width + sx) * 4u;
                    for (int c = 0; c < 4; ++c) sum[c] += in.pixels[k + c];
                    ++count;
                }
            }
            const std::size_t dk = (static_cast<std::size_t>(y) * dw + x) * 4u;
            for (int c = 0; c < 4; ++c) {
                // 向下取整：与 DESIGN §4.2 的 floor(sum/count) 同式
                out.pixels[dk + c] = static_cast<uint8_t>(count ? sum[c] / count : 0);
            }
        }
    }
    return true;
}

bool any_visible(const Image& img) {
    if (img.pixels.size() != static_cast<std::size_t>(img.width) * img.height * 4u) return false;
    for (std::size_t i = 3; i < img.pixels.size(); i += 4) {
        if (img.pixels[i] != 0) return true;
    }
    return false;
}

}  // namespace cs::core
