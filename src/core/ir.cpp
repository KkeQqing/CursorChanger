#include "core/ir.h"

namespace cs::core {

bool premultiply_ok(const Frame& f) {
    if (f.pixels.size() != frame_bytes(f) || f.pixels.empty()) return false;
    for (std::size_t i = 0; i < f.pixels.size(); i += 4) {
        const uint8_t b = f.pixels[i + 0];
        const uint8_t g = f.pixels[i + 1];
        const uint8_t r = f.pixels[i + 2];
        const uint8_t a = f.pixels[i + 3];
        // 预乘的必要条件：每个通道不超过 alpha。违反即说明「忘了预乘」或算错了。
        // 该判据也可能误拒（U13），因此这里只报告，不改像素。
        if (b > a || g > a || r > a) return false;
    }
    return true;
}

bool any_visible_pixel(const CursorSpec& spec) {
    for (const Frame& f : spec.frames) {
        if (f.pixels.size() != frame_bytes(f)) continue;  // 尺寸不符的帧在 validate 里先报错
        for (std::size_t i = 3; i < f.pixels.size(); i += 4) {
            if (f.pixels[i] != 0) return true;
        }
    }
    return false;
}

Hotspot scale_hotspot(Hotspot ref, uint32_t ref_dim, uint32_t to_dim) {
    // 四舍五入：与 OS 重采样时的换算形式一致（M2a 观测：(4,4)@16 -> (8,8)@32，
    // (12,12)@48 -> (8,8)@32，两者都精确等于 round(v*to/from)）。
    if (ref_dim == 0 || to_dim == 0) return Hotspot{0, 0};
    auto one = [](uint16_t v, uint32_t from, uint32_t to) {
        return static_cast<uint16_t>((static_cast<uint64_t>(v) * to + from / 2) / from);
    };
    Hotspot out;
    out.x = one(ref.x, ref_dim, to_dim);
    out.y = one(ref.y, ref_dim, to_dim);
    // 换算结果必须仍在界内：256 -> 1 这种极端档位可能把非零热点挤到 1，
    // 但热点==边长是非法的，这里夹到边长-1 而不是让它越界。
    if (out.x >= to_dim) out.x = static_cast<uint16_t>(to_dim - 1);
    if (out.y >= to_dim) out.y = static_cast<uint16_t>(to_dim - 1);
    return out;
}

SpecError validate_spec(const CursorSpec& spec, const ValidateOptions& opts) {
    SpecError e;

    if (spec.frames.empty()) {
        e.status = SpecStatus::EmptyFrames;
        return e;
    }

    for (std::size_t i = 0; i < spec.frames.size(); ++i) {
        const Frame& f = spec.frames[i];
        e.frame_index = i;

        if (f.width == 0 || f.height == 0) {
            e.status = SpecStatus::ZeroDimension;
            return e;
        }
        if (f.width > kMaxCursorDimension || f.height > kMaxCursorDimension) {
            e.status = SpecStatus::SizeTooLarge;
            return e;
        }
        if (f.pixels.size() != frame_bytes(f)) {
            e.status = SpecStatus::PixelBufferSize;
            e.detail = f.pixels.size();
            return e;
        }
        // 顺序不可调换：上面两条确立了 width/height <= 256，
        // 于是这里的 x<width / y<height 蕴含 hotspot <= 255，目录项的 WORD 一定能装下。
        if (f.hotspot.x >= f.width || f.hotspot.y >= f.height) {
            e.status = SpecStatus::HotspotOutOfBounds;
            return e;  // 帧号已在 frame_index；不打包诊断值，免得多一个魔数
        }
        if (!premultiply_ok(f)) {
            e.status = SpecStatus::NotPremultiplied;
            return e;
        }
    }

    if (opts.reject_fully_transparent && !any_visible_pixel(spec)) {
        e.frame_index = 0;
        e.status = SpecStatus::FullyTransparent;
        return e;
    }

    e.frame_index = 0;
    e.status = SpecStatus::Ok;
    return e;
}

}  // namespace cs::core
