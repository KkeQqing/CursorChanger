// src/ui/main_window.cpp — 见头文件。只做 I/O 与状态显示，业务全在 apply/（§2 层规则）。
#include "ui/main_window.h"

#include "apply/pipeline.h"
#include "apply/report_model.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>   // InitCommonControlsEx：与 app.manifest 的 Common-Controls 6.0 依赖配对
#include <objbase.h>   // CoInitializeEx / CoTaskMemFree
#include <shlobj.h>    // SHBrowseForFolderW / SHGetPathFromIDListW / BROWSEINFOW

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

// PrintWindow 在较新系统上渲染子控件更完整；老头文件无此常量，本地定义（值来自 winuser.h）。
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace cs {
namespace ui {
namespace {

using apply::PipelineOptions;
using apply::PipelineResult;
using apply::ReportView;
using apply::SlotRow;

// ---- 控件 ID（无业务含义，仅 WM_COMMAND 分发用）----
enum : int {
    IDC_SKIN_EDIT = 101,
    IDC_BROWSE_BTN = 106,
    IDC_PREVIEW_BTN = 102,
    IDC_APPLY_BTN = 103,
    IDC_RESTORE_BTN = 104,
    IDC_RESET_BTN = 105,
    IDC_STATUS_LIST = 200,
    IDC_SUMMARY = 201,
    IDC_CLI_HINT = 202,
};

// ---- 窗口几何（手写布局，D2；纯像素字面量，非业务数值）----
enum {
    kClientW = 620,
    kClientH = 460,
    kMargin = 12,
    kLabelH = 20,
    kEditH = 24,
    kBtnW = 96,
    kBtnH = 28,
};

struct UiState {
    HWND edit = nullptr;
    HWND list = nullptr;
    HWND summary = nullptr;
    HWND cli_hint = nullptr;
    std::wstring skin_dir;  // 当前编辑框内容
};

// narrow(UTF-8)->wide。失败退化空串（表内 ASCII 恒成功；display 是 UTF-8）。
std::wstring u8_to_w(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring edit_text(HWND edit) {
    const int n = GetWindowTextLengthW(edit);
    std::wstring s(static_cast<std::size_t>(n) + 1, L'\0');
    GetWindowTextW(edit, s.data(), n + 1);
    s.resize(n);
    return s;
}

// 从 ReportView 生成每槽一行文本，填进列表框 + 汇总。
void render_report(UiState* st, const ReportView& v) {
    if (st->list) SendMessageW(st->list, LB_RESETCONTENT, 0, 0);
    for (const SlotRow& row : v.rows) {
        std::wstring line = L"  ";
        line += (row.covered ? (row.l1_ok ? L"[OK]   " : L"[L1x]  ") : L"[MISS] ");
        line += u8_to_w(row.display_name);
        line += L"  (";
        line += u8_to_w(row.registry_name);
        line += L")";
        if (row.conflict) line += L"  冲突(取首)";
        if (st->list) SendMessageW(st->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    wchar_t buf[256];
    swprintf(buf, 256, L"扫描 %u ｜ 覆盖 %zu ｜ 缺失 %zu ｜ 未认领 %zu ｜ 冲突 %zu ｜ 表外产物 %zu",
             v.scanned, v.covered_count(), v.missing_count(), v.unclaimed.size(), v.conflicts.size(),
             v.unmapped_produced.size());
    if (st->summary) SetWindowTextW(st->summary, buf);
}

// 只预览（build_only）：不写注册表、不触发 SPI。截图与打开窗口时都走这条安全路径。
void preview_only(UiState* st) {
    const PipelineOptions opt = apply::make_live_opts(st->skin_dir, /*do_spi=*/false);
    const PipelineResult r = apply::build_only(opt);
    const ReportView v = apply::make_report(r);
    render_report(st, v);
}

void do_apply(UiState* st) {
    const PipelineOptions opt = apply::make_live_opts(st->skin_dir, /*do_spi=*/true);
    const PipelineResult r = apply::apply_skin(opt);
    const ReportView v = apply::make_report(r);
    render_report(st, v);
}

void do_restore(UiState* st) {
    const PipelineOptions opt = apply::make_live_opts(st->skin_dir, /*do_spi=*/true);
    apply::SpiResult spi;
    bool loaded = false;
    apply::restore_from_file(opt, spi, loaded);
    if (!loaded) {
        const PipelineOptions o2 = apply::make_live_opts(L"", false);
        apply::SpiResult spi2;
        apply::reset_default(o2, spi2);   // 无基线时的兜底出口
    }
    preview_only(st);
}

void do_reset(UiState* st) {
    const PipelineOptions opt = apply::make_live_opts(st->skin_dir, /*do_spi=*/true);
    apply::SpiResult spi;
    apply::reset_default(opt, spi);
    preview_only(st);
}

HWND mk(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w,
         int h, int id, HFONT font) {
    HWND e = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                             reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), nullptr, nullptr);
    if (font) SendMessageW(e, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return e;
}

std::wstring env_w(const wchar_t* name) {
    wchar_t buf[4096];
    const DWORD n = GetEnvironmentVariableW(name, buf, 4096);
    if (n == 0 || n >= 4096) return std::wstring();
    return std::wstring(buf, n);
}

// 系统文件夹选择框；取消返回空串。
std::wstring browse_for_folder(HWND owner) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"选择皮肤目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl == nullptr) return std::wstring();
    wchar_t path[MAX_PATH] = {0};
    std::wstring out;
    if (SHGetPathFromIDListW(pidl, path)) out = path;
    CoTaskMemFree(pidl);
    return out;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    UiState* st = reinterpret_cast<UiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* p = new UiState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
        st = p;

        HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        mk(hwnd, L"STATIC", L"CursorSwitch 皮肤管理器", SS_LEFT, kMargin, kMargin, kClientW - 2 * kMargin,
           kLabelH, 0, font);

        int y = kMargin + 28;
        mk(hwnd, L"STATIC", L"皮肤目录:", SS_LEFT, kMargin, y, 80, kLabelH, 0, font);
        const int edit_w = kClientW - 2 * kMargin - 84 - (kBtnW * 2) - 12;
        p->edit = mk(hwnd, L"EDIT", L"", ES_AUTOHSCROLL, kMargin + 84, y, edit_w, kEditH,
                     IDC_SKIN_EDIT, font);
        mk(hwnd, L"BUTTON", L"浏览...", BS_PUSHBUTTON, kClientW - kMargin - (kBtnW * 2) - 6, y, kBtnW,
           kBtnH, IDC_BROWSE_BTN, font);
        mk(hwnd, L"BUTTON", L"预览", BS_PUSHBUTTON, kClientW - kMargin - kBtnW, y, kBtnW, kBtnH,
           IDC_PREVIEW_BTN, font);

        y += 34;
        p->list = mk(hwnd, L"LISTBOX", L"", WS_VSCROLL | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                     kMargin, y, kClientW - 2 * kMargin, 250, IDC_STATUS_LIST, font);

        y += 258;
        p->summary = mk(hwnd, L"STATIC", L"未扫描", SS_LEFT, kMargin, y, kClientW - 2 * kMargin, kLabelH,
                        IDC_SUMMARY, font);

        y += 26;
        // 三个动作按钮 + 各自等价命令提示（§9.2：按钮旁显示等价 CLI）。
        mk(hwnd, L"BUTTON", L"应用皮肤", BS_PUSHBUTTON, kMargin, y, kBtnW, kBtnH, IDC_APPLY_BTN, font);
        mk(hwnd, L"BUTTON", L"还原到基线", BS_PUSHBUTTON, kMargin + kBtnW + 8, y, kBtnW, kBtnH,
           IDC_RESTORE_BTN, font);
        mk(hwnd, L"BUTTON", L"复位默认", BS_PUSHBUTTON, kMargin + (kBtnW + 8) * 2, y, kBtnW, kBtnH,
           IDC_RESET_BTN, font);
        p->cli_hint = mk(hwnd, L"STATIC", L"", SS_LEFT, kMargin, y + kBtnH + 4,
                         kClientW - 2 * kMargin, kLabelH, IDC_CLI_HINT, font);

        // 初始内容：CS_GUI_SKIN 给了就用，否则留空（预览将显示全部缺失）。
        p->skin_dir = env_w(L"CS_GUI_SKIN");
        if (p->edit && !p->skin_dir.empty()) SetWindowTextW(p->edit, p->skin_dir.c_str());
        if (!p->skin_dir.empty()) preview_only(p);
        return 0;
    }
    case WM_COMMAND: {
        if (!st) return 0;
        const int id = LOWORD(wp);
        st->skin_dir = edit_text(st->edit);
        switch (id) {
        case IDC_BROWSE_BTN: {
            const std::wstring picked = browse_for_folder(hwnd);
            if (!picked.empty()) {
                st->skin_dir = picked;
                if (st->edit) SetWindowTextW(st->edit, picked.c_str());
                preview_only(st);   // 只 build_only，不碰注册表
                if (st->cli_hint)
                    SetWindowTextW(st->cli_hint,
                                   (L"等价: cursorswitch --apply \"" + picked + L"\" (仅预览)").c_str());
            }
            return 0;
        }
        case IDC_PREVIEW_BTN:
            preview_only(st);
            if (st->cli_hint)
                SetWindowTextW(st->cli_hint, (L"等价: cursorswitch --apply \"" + st->skin_dir + L"\" (仅预览)")
                                             .c_str());
            return 0;
        case IDC_APPLY_BTN:
            do_apply(st);
            if (st->cli_hint)
                SetWindowTextW(st->cli_hint, (L"等价: cursorswitch --apply \"" + st->skin_dir + L"\"").c_str());
            return 0;
        case IDC_RESTORE_BTN: {
            // D13：确认文案取自模型层 confirm_text_for（两还原两套），UI 不自己编。
            const apply::ConfirmText ct = apply::confirm_text_for(apply::GuiAction::Restore);
            if (ct.need_confirm && MessageBoxW(hwnd, ct.body.c_str(), ct.title.c_str(),
                                               MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) {
                if (st->cli_hint) SetWindowTextW(st->cli_hint, L"已取消还原");
                return 0;   // 取消 = 一个注册表写动作都不做
            }
            do_restore(st);
            if (st->cli_hint) SetWindowTextW(st->cli_hint, L"等价: cursorswitch --restore");
            return 0;
        }
        case IDC_RESET_BTN: {
            const apply::ConfirmText ct = apply::confirm_text_for(apply::GuiAction::Reset);
            if (ct.need_confirm && MessageBoxW(hwnd, ct.body.c_str(), ct.title.c_str(),
                                               MB_OKCANCEL | MB_ICONWARNING) != IDOK) {
                if (st->cli_hint) SetWindowTextW(st->cli_hint, L"已取消复位");
                return 0;
            }
            do_reset(st);
            if (st->cli_hint) SetWindowTextW(st->cli_hint, L"等价: cursorswitch --reset");
            return 0;
        }
        default:
            return 0;
        }
    }
    case WM_CTLCOLORSTATIC: {
        // 让 STATIC 文字随窗口背景（不画成黑底）。
        static HBRUSH bg = GetSysColorBrush(COLOR_WINDOW);
        SetBkMode(reinterpret_cast<HDC>(lp), TRANSPARENT);
        return reinterpret_cast<LRESULT>(bg);
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// 把整个窗口（含子控件）用 PrintWindow 抓进 32bpp DIB 并写成 BMP。
bool capture_window(HWND hwnd, const std::wstring& path) {
    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return false;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;

    HDC sw = GetWindowDC(hwnd);
    HDC dc = CreateCompatibleDC(sw);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(sw, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hb || !bits) {
        if (hb) DeleteObject(hb);
        DeleteDC(dc);
        ReleaseDC(hwnd, sw);
        return false;
    }
    HGDIOBJ old = SelectObject(dc, hb);
    PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT);

    BITMAPFILEHEADER bf;
    ZeroMemory(&bf, sizeof(bf));
    bf.bfType = 0x4D42;   // 'BM'
    bf.bfOffBits = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER));
    const DWORD img = static_cast<DWORD>(w) * static_cast<DWORD>(h) * 4u;
    bf.bfSize = bf.bfOffBits + img;

    std::FILE* f = nullptr;
    bool ok = (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f != nullptr);
    if (ok) {
        ok = std::fwrite(&bf, sizeof(bf), 1, f) == 1 &&
             std::fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f) == 1 &&
             std::fwrite(bits, 1, img, f) == img;
        std::fclose(f);
    }

    SelectObject(dc, old);
    DeleteObject(hb);
    DeleteDC(dc);
    ReleaseDC(hwnd, sw);
    return ok;
}

}  // namespace

int run_window() {
    // comctl32 v6 视觉样式：manifest 声明了 Common-Controls 6.0 依赖，这里显式加载标准公共控件类，
    // 二者配对才让 BUTTON/LISTBOX/EDIT 走主题化外观（D2）。缺任一侧都退回经典 Win95 方块。
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetSysColorBrush(COLOR_WINDOW + 1));
    wc.lpszClassName = L"CursorSwitch.Main";
    RegisterClassExW(&wc);

    const int cw = kClientW;
    const int ch = kClientH;
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wr{0, 0, cw, ch};
    AdjustWindowRect(&wr, style, FALSE);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"CursorSwitch", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, wc.hInstance,
                                nullptr);
    if (!hwnd) {
        if (SUCCEEDED(co)) CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    const std::wstring capture = env_w(L"CS_GUI_CAPTURE");
    if (!capture.empty()) {
        // 无人值守截图自检：画完就存图退出，不进入交互消息循环，绝不 apply/SPI。
        const bool ok = capture_window(hwnd, capture);
        DestroyWindow(hwnd);
        if (SUCCEEDED(co)) CoUninitialize();
        return ok ? 0 : 1;
    }

    // 交互启动才藏控制台：exe 是 CONSOLE 子系统（M5 追认），GUI 模式下不留黑框。
    HWND console = GetConsoleWindow();
    if (console) ShowWindow(console, SW_HIDE);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return static_cast<int>(m.wParam);
}

}  // namespace ui
}  // namespace cs
