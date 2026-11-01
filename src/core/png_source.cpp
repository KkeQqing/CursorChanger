#include "core/png_source.h"

#include "core/slots.h"

// 第三方头的警告只对它自己抑制，不降整个项目的 /W4。
// 本机实测 stb_image 2.30 在 /W4 + STBI_ONLY_PNG 下会报 3 条 C4505
// （stbi__cpuid3 / stbi__addints_valid / stbi__mul2shorts_valid 未被引用：
//  前两者属于其它格式与 SSE 探测路径，我们只编 PNG，所以确实用不到）。
// 抑制范围严格到 #include 之内，并登记在 vendor/README.md：
// 一旦升级 stb 出现新警告种类，日志会立刻变脏而不是被顺手压掉。
#pragma warning(push)
#pragma warning(disable : 4505)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#pragma warning(pop)

#include <algorithm>
#include <cstring>

namespace cs::core {
int png_probe_channels(const uint8_t* data, std::size_t size, uint32_t& w, uint32_t& h) {
    w = h = 0;
    if (data == nullptr || size == 0) return 0;
    int comp = 0, iw = 0, ih = 0;
    if (stbi_info_from_memory(data, static_cast<int>(size), &iw, &ih, &comp) == 0) return 0;
    w = static_cast<uint32_t>(iw);
    h = static_cast<uint32_t>(ih);
    return comp;
}

PngStatus load_png_to_spec(const uint8_t* data, std::size_t size, SlotId slot,
                          const PngOptions& opt, CursorSpec& out, std::string& detail) {
    out = CursorSpec{};
    detail.clear();
    if (data == nullptr || size == 0) {
        detail = "empty input";
        return PngStatus::EmptyInput;
    }

    int iw = 0, ih = 0, comp = 0;
    // 强制 4 通道：stb 对无 alpha 的 RGB 图会把 alpha 填成 255，
    // 于是 §4.2「无 alpha -> 完全不透明」不需要额外分支（少一处可能写错的规则）。
    unsigned char* rgba = stbi_load_from_memory(data, static_cast<int>(size), &iw, &ih, &comp, 4);
    if (rgba == nullptr) {
        detail = "stb decode failed";
        return PngStatus::DecodeFailed;
    }

    const std::vector<uint8_t> src(rgba, rgba + static_cast<std::size_t>(iw) * ih * 4u);
    stbi_image_free(rgba);

    if (iw <= 0 || ih <= 0) {
        detail = "decoded size is zero";
        return PngStatus::DecodeFailed;
    }
    if (src.size() != static_cast<std::size_t>(iw) * ih * 4u) {
        detail = "decoded buffer length mismatch";
        return PngStatus::InternalMismatch;
    }

    const uint32_t sw = static_cast<uint32_t>(iw);
    const uint32_t sh = static_cast<uint32_t>(ih);

    Image base;
    base.width = sw;
    base.height = sh;
    rgba_straight_to_bgra_premul(src.data(), static_cast<std::size_t>(sw) * sh, base.pixels);

    const SlotSpec* sp = find_slot(slot);
    if (sp == nullptr) {
        detail = "slot not in table";
        return PngStatus::DecodeFailed;
    }

    // 档位选择：只做下采样（放大只会糊，见 §4.2 第 3 条）。
    // 若所有请求档都比源大，则保留源尺寸这一档，而不是悄悄产出空 spec。
    std::vector<uint32_t> picks;
    const uint32_t native_min = sw < sh ? sw : sh;
    for (uint32_t s : opt.sizes) {
        if (s == 0 || s > kMaxCursorDimension) continue;
        if (s <= native_min) picks.push_back(s);
    }
    if (picks.empty()) {
        if (!opt.allow_upscale && native_min >= 1 && native_min <= kMaxCursorDimension) {
            picks.push_back(native_min);  // 不放大：用原生这一档
        } else {
            detail = "no usable frame";
            return PngStatus::NoUsableFrame;
        }
    }
    std::sort(picks.begin(), picks.end());
    picks.erase(std::unique(picks.begin(), picks.end()), picks.end());

    // 参考尺寸：非方图取短边作为热点换算基准（sidecar 覆盖的绝对热点以源尺寸为参照，
    // 因此参照用 native_min，与上面 picks 的依据一致）
    out.slot = slot;
    out.source_kind = SourceKind::Png;
    for (uint32_t dim : picks) {
        Image scaled;
        if (!resize_box_premul(base, dim, dim, scaled)) {
            detail = "resize failed";
            return PngStatus::InternalMismatch;
        }
        if (scaled.pixels.size() != static_cast<std::size_t>(dim) * dim * 4u) {
            detail = "resized buffer mismatch";
            return PngStatus::InternalMismatch;
        }
        Frame f;
        f.width = dim;
        f.height = dim;
        f.hotspot = opt.has_hotspot_override ? scale_hotspot(opt.hotspot_at_source, native_min, dim)
                                             : default_hotspot(sp->rule, dim, dim);
        f.pixels = std::move(scaled.pixels);
        out.frames.push_back(std::move(f));
    }

    // 升序不变量由 picks 保证；这里再验一次，避免以后有人往 picks 里加逻辑时静默打破 H8
    for (std::size_t i = 1; i < out.frames.size(); ++i) {
        const std::size_t prev = static_cast<std::size_t>(out.frames[i - 1].height) * out.frames[i - 1].width;
        const std::size_t cur = static_cast<std::size_t>(out.frames[i].height) * out.frames[i].width;
        if (cur < prev) {
            detail = "frame order broken";
            return PngStatus::InternalMismatch;
        }
    }
    return PngStatus::Ok;
}

}  // namespace cs::core
