#include "selfcheck/verify_live.h"

#include <cstring>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace cs::selfcheck {
namespace {

constexpr uint32_t kMaxCanvas = 96;  // 容得下 48/64 档帧；更大的帧会被判「无法核验」而不是硬画

uint64_t fnv1a(const uint8_t* p, std::size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

// 把生效句柄画进 32bpp 内存 DIB，取回 top-down BGRA 像素。
bool render_cursor(HCURSOR h, std::vector<uint8_t>& pixels, uint32_t& w, uint32_t& h_out,
                   std::string& detail) {
    pixels.clear();
    w = h_out = 0;

    ICONINFO ii{};
    uint32_t fw = 0, fh = 0;
    bool have_size = false;
    if (GetIconInfo(h, &ii)) {
        if (ii.hbmColor != nullptr) {
            BITMAP bm{};
            if (GetObjectW(ii.hbmColor, sizeof(bm), &bm) == sizeof(bm) && bm.bmWidth > 0 &&
                bm.bmHeight > 0) {
                fw = static_cast<uint32_t>(bm.bmWidth);
                fh = static_cast<uint32_t>(bm.bmHeight);
                have_size = true;
            }
        }
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
    }
    if (!have_size) {
        detail = "GetIconInfo gave no color bitmap size (monochrome cursor?)";
        return false;
    }
    if (fw > kMaxCanvas || fh > kMaxCanvas) {
        detail = "natural frame larger than canvas";
        return false;
    }

    HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        detail = "GetDC failed";
        return false;
    }
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = static_cast<LONG>(fw);
    bi.bmiHeader.biHeight = -static_cast<LONG>(fh);  // 负值 = top-down，读回无需再翻转
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = (mem != nullptr) ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0)
                                   : nullptr;
    if (dib == nullptr || bits == nullptr) {
        if (mem) DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        detail = "CreateDIBSection/CompatibleDC failed";
        return false;
    }

    std::memset(bits, 0, static_cast<std::size_t>(fw) * fh * 4u);
    HGDIOBJ old = SelectObject(mem, dib);
    // cxSrc/cySrc = 0：按帧自身尺寸绘制；DI_NORMAL 保留 alpha。
    const BOOL drew = DrawIconEx(mem, 0, 0, h, 0, 0, 0, nullptr, DI_NORMAL);
    if (old) SelectObject(mem, old);

    bool ok = (drew != 0);
    if (ok) {
        pixels.assign(static_cast<std::size_t>(fw) * fh * 4u, 0);
        std::memcpy(pixels.data(), bits, pixels.size());
        w = fw;
        h_out = fh;
    } else {
        detail = "DrawIconEx failed";
    }
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

}  // namespace

L2Result l2_verify_registry(apply::Root root, const wchar_t* sub_key,
                            const std::vector<ExpectedSlot>& expected) {
    L2Result r;
    for (const ExpectedSlot& e : expected) {
        std::wstring actual;
        bool found = false;
        const apply::QueryResult q =
            apply::read_sz(root, sub_key, e.registry_name.c_str(), actual, found);
        ++r.checked;
        if (q.status == apply::QueryStatus::Ok && found && actual == e.expected_path) {
            ++r.matched;
            continue;
        }
        L2Mismatch m;
        m.registry_name = e.registry_name;
        m.expected = e.expected_path;
        m.actual = actual;
        m.actual_missing = !found;
        r.mismatches.push_back(m);
    }
    return r;
}

L3Capture l3_capture_cursor_at(uint32_t x, uint32_t y, uint32_t settle_ms) {
    L3Capture c;
    c.x = x;
    c.y = y;

    if (SetCursorPos(static_cast<int>(x), static_cast<int>(y)) == 0) {
        c.detail = "SetCursorPos refused (remote session or secure desktop?)";
        return c;
    }
    if (settle_ms > 0) Sleep(settle_ms);

    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci) == 0 || ci.hCursor == nullptr) {
        c.detail = "GetCursorInfo failed";
        return c;
    }
    // 只读取当前生效句柄；绝不调用 SetSystemCursor（v1 非目标，也是零注入原则的边界）。
    std::string detail;
    if (!render_cursor(ci.hCursor, c.pixels, c.width, c.height, detail)) {
        c.detail = detail;
        return c;
    }

    std::vector<uint32_t> keys;
    for (std::size_t i = 0; i < c.pixels.size(); i += 4) {
        if (c.pixels[i + 3] == 0) continue;
        ++c.opaque_pixels;
        const uint32_t key = (static_cast<uint32_t>(c.pixels[i + 0]) << 16) |
                             (static_cast<uint32_t>(c.pixels[i + 1]) << 8) |
                             static_cast<uint32_t>(c.pixels[i + 2]);
        bool dup = false;
        for (uint32_t k : keys) {
            if (k == key) { dup = true; break; }
        }
        if (!dup) keys.push_back(key);
    }
    c.distinct_colors = static_cast<uint32_t>(keys.size());
    c.fingerprint = fnv1a(c.pixels.data(), c.pixels.size());
    c.got_cursor = true;
    c.detail = "ok";
    return c;
}

L3Verdict l3_evaluate(const L3Capture& cap, const std::vector<uint8_t>& expected_bgra,
                      uint32_t expected_w, uint32_t expected_h) {
    L3Verdict v;
    v.capture = cap;
    if (!cap.got_cursor || expected_bgra.empty()) return v;

    v.size_matches_expected = (cap.width == expected_w && cap.height == expected_h);
    if (!v.size_matches_expected) return v;  // 尺寸不同，逐点差值无意义；用 changed_vs 判生效

    const std::size_t count = static_cast<std::size_t>(expected_w) * expected_h;
    if (cap.pixels.size() != count * 4u) return v;

    uint64_t sum = 0;
    uint32_t worst = 0;
    for (std::size_t i = 0; i < count * 4u; ++i) {
        const uint32_t a = expected_bgra[i];
        const uint32_t b = cap.pixels[i];
        const uint32_t d = a > b ? a - b : b - a;
        if (d > worst) worst = d;
        sum += d;
    }
    v.max_channel_diff = worst;
    v.mean_diff = static_cast<uint32_t>(sum / (count * 4u));
    return v;
}

}  // namespace cs::selfcheck
