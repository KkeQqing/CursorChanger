#include "selfcheck/l1.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace cs::selfcheck {
namespace {

L1Result probe_handle(HCURSOR h, uint32_t last_error) {
    L1Result r;
    if (h == nullptr) {
        r.win_error = last_error;
        return r;
    }
    r.loaded = true;

    ICONINFO ii{};
    if (GetIconInfo(reinterpret_cast<HICON>(h), &ii)) {
        r.has_color_bitmap = (ii.hbmColor != nullptr);
        r.has_mask_bitmap = (ii.hbmMask != nullptr);
        r.hotspot_x = ii.xHotspot;   // DWORD：U12 的观测值
        r.hotspot_y = ii.yHotspot;

        // 帧尺寸取彩色位图。32bpp 光标必须有 hbmColor；取不到就留 0，由调用方判失败。
        if (ii.hbmColor != nullptr) {
            BITMAP bm{};
            if (GetObjectW(ii.hbmColor, sizeof(bm), &bm) == sizeof(bm)) {
                r.width = static_cast<uint32_t>(bm.bmWidth);
                r.height = static_cast<uint32_t>(bm.bmHeight);
            }
        }
    } else {
        r.win_error = GetLastError();
    }

    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    DestroyIcon(h);
    return r;
}

}  // namespace

L1Result l1_load_and_probe(const wchar_t* path) {
    if (path == nullptr) return L1Result{};
    SetLastError(0);
    HCURSOR h = LoadCursorFromFileW(path);
    return probe_handle(h, GetLastError());
}

L1Result l1_load_frame(const wchar_t* path, uint32_t w, uint32_t h) {
    if (path == nullptr) return L1Result{};
    SetLastError(0);
    void* got = LoadImageW(nullptr, path, IMAGE_CURSOR, static_cast<int>(w), static_cast<int>(h),
                           LR_LOADFROMFILE);
    return probe_handle(static_cast<HCURSOR>(got), GetLastError());
}

uint32_t system_cursor_default_size() {
    const int cx = GetSystemMetrics(SM_CXCURSOR);
    return cx > 0 ? static_cast<uint32_t>(cx) : 0u;
}

}  // namespace cs::selfcheck
