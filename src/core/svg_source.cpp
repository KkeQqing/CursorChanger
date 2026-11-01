#include "core/svg_source.h"

#include "core/pixel.h"
#include "core/ir.h"
#include "core/slots.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// vendored nanosvg 的已知警告只对这 include 段抑制（逐条列举，不整体降 /W4）：
//   C4244 double->float 收窄；C4456 内层声明遮蔽外层同名变量；C4702 不可达代码。
// 两者都在第三方代码内、非本项目可行动项。新增类别不会被这条压住。
#pragma warning(push)
#pragma warning(disable : 4244 4456 4702)
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"
#pragma warning(pop)

namespace cs::core {
namespace {

// 把 SVG 文本光栅化成**未经任何处理**的 RGBA（nanosvg 直接写出的字节）。
// 单独暴露的原因：nanosvg 输出到底是否已预乘，属于**必须实测**的事实，
// 不能照文档/记忆假定 —— 若它已预乘而我们又乘一次，颜色整体变暗，
// 且「每通道 <= alpha」这条判据抓不到双重预乘（仍然满足）。
bool rasterize_raw(char* svg_mutable, uint32_t resolution, std::vector<uint8_t>& rgba,
                   uint32_t& nat_w, uint32_t& nat_h, std::string& detail) {
    rgba.clear();
    nat_w = nat_h = 0;
    NSVGimage* img = nsvgParse(svg_mutable, "px", 96.0f);
    if (img == nullptr) {
        detail = "nsvgParse returned null";
        return false;
    }
    if (img->width <= 0.0f || img->height <= 0.0f) {
        detail = "parsed image has non-positive size";
        nsvgDelete(img);
        return false;
    }

    nat_w = resolution;
    nat_h = resolution;
    const float scale = static_cast<float>(resolution) /
                        (img->width > img->height ? img->width : img->height);
    std::vector<unsigned char> buf(static_cast<std::size_t>(resolution) * resolution * 4u, 0);

    NSVGrasterizer* r = nsvgCreateRasterizer();
    if (r == nullptr) {
        detail = "nsvgCreateRasterizer failed";
        nsvgDelete(img);
        return false;
    }
    nsvgRasterize(r, img, 0.0f, 0.0f, scale, buf.data(), static_cast<int>(resolution),
                  static_cast<int>(resolution), static_cast<int>(resolution) * 4);
    nsvgDeleteRasterizer(r);
    nsvgDelete(img);
    rgba.assign(buf.begin(), buf.end());
    return true;
}

struct BBox {
    uint32_t x0{0}, y0{0}, x1{0}, y1{0};
    bool valid{false};
};

BBox alpha_bbox(const std::vector<uint8_t>& img, uint32_t w, uint32_t h) {
    BBox b;
    uint32_t minx = w, miny = h, maxx = 0, maxy = 0;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const std::size_t k = (static_cast<std::size_t>(y) * w + x) * 4u;
            if (img[k + 3] == 0) continue;
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
        }
    }
    if (maxx < minx || maxy < miny) return b;  // 全空
    b.x0 = minx;
    b.y0 = miny;
    b.x1 = maxx;
    b.y1 = maxy;
    b.valid = true;
    return b;
}

}  // namespace

bool svg_probe_raw_pixel(const uint8_t* data, std::size_t size, uint32_t resolution, uint32_t x,
                         uint32_t y, uint8_t out_raw_rgba[4], std::string& detail) {
    if (data == nullptr || size == 0 || out_raw_rgba == nullptr) {
        detail = "empty input";
        return false;
    }
    std::vector<char> buf(data, data + size);
    buf.push_back('\0');
    std::vector<uint8_t> rgba;
    uint32_t nw = 0, nh = 0;
    if (!rasterize_raw(buf.data(), resolution, rgba, nw, nh, detail)) return false;
    if (x >= nw || y >= nh) {
        detail = "probe coordinate outside raster";
        return false;
    }
    const std::size_t k = (static_cast<std::size_t>(y) * nw + x) * 4u;
    out_raw_rgba[0] = rgba[k + 0];
    out_raw_rgba[1] = rgba[k + 1];
    out_raw_rgba[2] = rgba[k + 2];
    out_raw_rgba[3] = rgba[k + 3];
    return true;
}

SvgStatus load_svg_to_spec(const uint8_t* data, std::size_t size, SlotId slot,
                          const SvgOptions& opt, CursorSpec& out, SvgReport& report,
                          std::string& detail) {
    out = CursorSpec{};
    report = SvgReport{};
    detail.clear();
    if (data == nullptr || size == 0) {
        detail = "empty input";
        return SvgStatus::EmptyInput;
    }

    // 1) 守卫先行：不支持的构造直接拒，不给 nanosvg 静默丢内容的机会
    report.guard = svg_guard_scan(std::string(reinterpret_cast<const char*>(data), size));
    if (!report.guard.ok) {
        detail = report.guard.detail;
        return SvgStatus::GuardRejected;
    }

    // 2) 光栅化（工作分辨率）
    std::vector<char> buf(data, data + size);
    buf.push_back('\0');
    std::vector<uint8_t> raw;
    if (!rasterize_raw(buf.data(), opt.work_resolution, raw, report.raster_w, report.raster_h,
                       detail)) {
        return SvgStatus::RasterFailed;
    }

    // 3) 转 BGRA + 预乘。
    //    前提**已实测**（不再是假设）：50% 不透明灰圆的中心点，nanosvg 原始输出为
    //    RGBA (126,126,126,127) —— 若它已预乘，R 应为 ~63。故这里需要乘一次。
    //    判据由 tests_m4 的 svg_probe_shows_straight_alpha_not_premultiplied 守住：
    //    该用例变红意味着 nanosvg 改了行为，届时必须删掉这次乘法而不是改期望值。
    //    （双重预乘不会被「每通道 <= alpha」抓到，只能靠这种数值对照发现。）
    const std::size_t px_count = static_cast<std::size_t>(report.raster_w) * report.raster_h;
    std::vector<uint8_t> bgra;
    rgba_straight_to_bgra_premul(raw.data(), px_count, bgra);

    // 4) 贴边（§4.3：手写/导出的光标常把图形画在画布一角或带出血）
    Image full;
    full.width = report.raster_w;
    full.height = report.raster_h;
    full.pixels = std::move(bgra);

    Image src = full;
    const BBox b = alpha_bbox(full.pixels, full.width, full.height);
    if (!b.valid) {
        // 全透明：交给 IR 的 H10 拒收，这里不另立规则（一处判一次）
        out.slot = slot;
        out.source_kind = SourceKind::Svg;
        Frame f;
        f.width = 1;
        f.height = 1;
        f.pixels.assign(4, 0);
        out.frames.push_back(f);
        return SvgStatus::Ok;
    }
    report.trimmed_w = b.x1 - b.x0 + 1;
    report.trimmed_h = b.y1 - b.y0 + 1;

    if (opt.trim_transparent) {
        Image crop;
        crop.width = report.trimmed_w;
        crop.height = report.trimmed_h;
        crop.pixels.resize(static_cast<std::size_t>(crop.width) * crop.height * 4u);
        for (uint32_t y = 0; y < crop.height; ++y) {
            std::memcpy(&crop.pixels[static_cast<std::size_t>(y) * crop.width * 4u],
                        &full.pixels[(static_cast<std::size_t>(b.y0 + y) * full.width + b.x0) * 4u],
                        static_cast<std::size_t>(crop.width) * 4u);
        }
        src = std::move(crop);
    } else {
        report.trimmed_w = full.width;
        report.trimmed_h = full.height;
    }

    report.content_ratio = static_cast<double>(report.trimmed_w) * report.trimmed_h /
                           (static_cast<double>(full.width) * full.height);
    report.low_ratio_warning = report.content_ratio < opt.min_content_ratio;

    const SlotSpec* sp = find_slot(slot);
    if (sp == nullptr) {
        detail = "slot not in table";
        return SvgStatus::InternalMismatch;
    }

    // 5) 档位：只做下采样；全被过滤掉时用原生尺寸这一档
    const uint32_t native_min = src.width < src.height ? src.width : src.height;
    std::vector<uint32_t> picks;
    for (uint32_t s : opt.sizes) {
        if (s == 0 || s > kMaxCursorDimension) continue;
        if (s <= native_min) picks.push_back(s);
    }
    if (picks.empty()) {
        if (native_min >= 1 && native_min <= kMaxCursorDimension) picks.push_back(native_min);
        else return SvgStatus::NoUsableFrame;
    }
    std::sort(picks.begin(), picks.end());
    picks.erase(std::unique(picks.begin(), picks.end()), picks.end());

    out.slot = slot;
    out.source_kind = SourceKind::Svg;
    for (uint32_t dim : picks) {
        Image scaled;
        if (!resize_box_premul(src, dim, dim, scaled)) return SvgStatus::InternalMismatch;
        Frame f;
        f.width = dim;
        f.height = dim;
        f.hotspot = opt.has_hotspot_override
                        ? scale_hotspot(opt.hotspot_at_source, native_min, dim)
                        : default_hotspot(sp->rule, dim, dim);
        f.pixels = std::move(scaled.pixels);
        out.frames.push_back(std::move(f));
    }
    return SvgStatus::Ok;
}

}  // namespace cs::core
