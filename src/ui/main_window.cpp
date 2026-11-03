// src/ui/main_window.cpp — 见头文件。只做 I/O 与状态显示，业务全在 apply/（§2 层规则）。
#include "ui/main_window.h"

#include "apply/pipeline.h"
#include "apply/report_model.h"
#include "core/debounce.h"

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
using apply::RowKind;
using apply::Outcome;

// ---- 控件 ID（无业务含义，仅 WM_COMMAND 分发用）----
enum : int {
    IDC_SKIN_EDIT = 101,
    IDC_BROWSE_BTN = 106,
    IDC_PREVIEW_BTN = 102,
    IDC_APPLY_BTN = 103,
    IDC_RESTORE_BTN = 104,
    IDC_RESET_BTN = 105,
    IDC_WATCHER_TOGGLE = 107,
    IDC_STATUS_LIST = 200,
    IDC_SUMMARY = 201,
    IDC_CLI_HINT = 202,
};

// watcher 的线程→UI marshal 与到点重放（§7.5）
constexpr UINT WM_APP_WATCHER_EVENT = WM_APP + 1;   // 后台线程报「目录有变更」
constexpr UINT WM_APP_WATCHER_DIED = WM_APP + 2;   // 后台线程意外退出（如目录被删）
constexpr UINT_PTR kWatcherTimer = 0xB7u;          // 去抖到点定时器

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
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    HWND list = nullptr;
    HWND summary = nullptr;
    HWND cli_hint = nullptr;
    HWND watcher_toggle = nullptr;
    std::wstring skin_dir;  // 当前编辑框内容

    // watcher（D22）状态：默认关闭，只有用户显式开启后才有线程/句柄
    bool watcher_on = false;
    HANDLE dir_handle = nullptr;
    HANDLE stop_ev = nullptr;
    HANDLE worker = nullptr;
    core::Debouncer* deb = nullptr;
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

// 从 ReportView 生成每槽一行文本，填进列表框 + 汇总。状态用人话（不再只印内部码 [L1x]），
// 失败行把 ASCII 原因（如「content is JPEG, not a PNG」）也带上，让用户当场看得懂。
void render_report(UiState* st, const ReportView& v) {
    if (st->list) SendMessageW(st->list, LB_RESETCONTENT, 0, 0);
    std::size_t applied = 0, bad = 0, rejected = 0, missing = 0;
    for (const SlotRow& row : v.rows) {
        std::wstring tag;
        switch (row.kind) {
        case RowKind::Applied: tag = L"[可用]"; ++applied; break;
        case RowKind::BadSource: tag = L"[源文件无效]"; ++bad; break;
        case RowKind::LoaderRejected: tag = L"[加载器拒绝]"; ++rejected; break;
        case RowKind::Missing: tag = L"[缺]"; ++missing; break;
        }
        std::wstring line = L"  ";
        line += tag;
        line += L" ";
        line += u8_to_w(row.display_name);
        line += L" (";
        line += u8_to_w(row.registry_name);
        line += L")";
        if (row.conflict) line += L"  同槽多文件(取首)";
        if (row.covered && !row.l1_ok && !row.l1_detail.empty())
            line += L"  —  " + u8_to_w(row.l1_detail);   // ASCII 也是合法 UTF-8
        if (st->list) SendMessageW(st->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    // 「可用」按 kind 精确计数，不能拿 covered_count（那会把解码失败的行也算成可用，谎报成功）。
    wchar_t buf[256];
    swprintf(buf, 256, L"扫描 %u ｜ 可用 %zu ｜ 源无效 %zu ｜ 加载拒绝 %zu ｜ 缺 %zu ｜ 冲突 %zu ｜ 未认领 %zu",
             v.scanned, applied, bad, rejected, missing, v.conflicts.size(), v.unclaimed.size());
    if (st->summary) SetWindowTextW(st->summary, buf);
}

// 动作结果横幅：文案来自模型层 outcome_*（与 CLI 同源、可单测），UI 只负责原样显示。
void set_banner(UiState* st, const Outcome& o) {
    if (st->cli_hint) SetWindowTextW(st->cli_hint, o.banner.c_str());
}

// 阻塞式动作前先把「正在…」画出来：WINDOWS 子系统下 apply 在主线程同步跑，
// 不强制同步重绘的话这条“正在”会被动作本身盖掉，用户就只见结果、不见过程。
void show_busy(UiState* st, const wchar_t* msg) {
    if (st->cli_hint) SetWindowTextW(st->cli_hint, msg);
    if (st->hwnd)
        RedrawWindow(st->hwnd, nullptr, nullptr,
                     RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// 只预览（build_only）：不写注册表、不触发 SPI。截图与打开窗口时都走这条安全路径。
void preview_only(UiState* st) {
    const PipelineOptions opt = apply::make_live_opts(st->skin_dir, /*do_spi=*/false);
    const PipelineResult r = apply::build_only(opt);
    render_report(st, apply::make_report(r));
    set_banner(st, apply::outcome_preview(r));
}

// 刷新逐槽行/汇总（供还原/复位后重画列表用）；不回写横幅，横幅留给动作结果。
void refresh_rows(UiState* st) {
    const PipelineOptions opt = apply::make_live_opts(st->skin_dir, /*do_spi=*/false);
    const PipelineResult r = apply::build_only(opt);
    render_report(st, apply::make_report(r));
}

void do_apply(UiState* st) {
    show_busy(st, L"正在应用…");
    const PipelineOptions opt = apply::make_live_opts(st->skin_dir, /*do_spi=*/true);
    const PipelineResult r = apply::apply_skin(opt);
    render_report(st, apply::make_report(r));
    set_banner(st, apply::outcome_apply(r));
}

void do_restore(UiState* st) {
    show_busy(st, L"正在还原…");
    const PipelineOptions opt = apply::make_live_opts(st->skin_dir, /*do_spi=*/true);
    apply::SpiResult spi;
    bool loaded = false;
    const apply::RestoreResult rr = apply::restore_from_file(opt, spi, loaded);
    Outcome o;
    if (loaded) {
        o = apply::outcome_restore(rr.restored, rr.failed, rr.skipped, true,
                                   spi.performed && spi.called_ok);
    } else {
        const PipelineOptions o2 = apply::make_live_opts(L"", /*do_spi=*/true);
        apply::SpiResult spi2;
        const apply::RestoreResult rz = apply::reset_default(o2, spi2);   // 无基线时的兜底出口
        o = apply::outcome_reset(rz.restored, rz.failed, spi2.performed && spi2.called_ok);
    }
    refresh_rows(st);
    set_banner(st, o);   // 覆盖 preview 横幅，让结果行显示「刚刚这次还原/复位」成没成
}

void do_reset(UiState* st) {
    show_busy(st, L"正在复位…");
    const PipelineOptions opt = apply::make_live_opts(st->skin_dir, /*do_spi=*/true);
    apply::SpiResult spi;
    const apply::RestoreResult rz = apply::reset_default(opt, spi);
    refresh_rows(st);
    set_banner(st, apply::outcome_reset(rz.restored, rz.failed, spi.performed && spi.called_ok));
}

// ---------------- watcher（D22；§7.5）：默认关闭，开启后才起后台线程 ----------------

void set_hint(UiState* st, const std::wstring& s) {
    if (st->cli_hint) SetWindowTextW(st->cli_hint, s.c_str());
}

// 后台线程：阻塞式 ReadDirectoryChangesW（overlapped + stop 事件可唤醒），只认原子替换后的最终名
// （D11 用 *.tmp 临时名，忽略之），有相关事件就 PostMessage 回 UI 线程去走去抖。
DWORD WINAPI WatcherWorker(LPVOID param) {
    UiState* st = static_cast<UiState*>(param);
    for (;;) {
        BYTE buf[4096];
        DWORD bytes = 0;
        OVERLAPPED ov;
        ZeroMemory(&ov, sizeof(ov));
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ov.hEvent == nullptr) return 0;
        const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                             FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE |
                             FILE_NOTIFY_CHANGE_CREATION;
        const BOOL ok = ReadDirectoryChangesW(st->dir_handle, buf, sizeof(buf), FALSE, filter, &bytes,
                                              &ov, nullptr);
        if (!ok) {  // 目录可能已被删/句柄失效
            CloseHandle(ov.hEvent);
            PostMessageW(st->hwnd, WM_APP_WATCHER_DIED, 0, 0);
            return 0;
        }
        HANDLE handles[2] = {ov.hEvent, st->stop_ev};
        const DWORD w = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (w != WAIT_OBJECT_0) {  // 收到停止
            CancelIoEx(st->dir_handle, &ov);
            DWORD dummy = 0;
            GetOverlappedResult(st->dir_handle, &ov, &dummy, TRUE);
            CloseHandle(ov.hEvent);
            return 0;
        }
        if (!GetOverlappedResult(st->dir_handle, &ov, &bytes, FALSE)) {
            const DWORD e = GetLastError();
            CloseHandle(ov.hEvent);
            if (e == ERROR_NOTIFY_ENUM_DIR) {  // 溢出：保守起见当作「有变更」重扫一次
                PostMessageW(st->hwnd, WM_APP_WATCHER_EVENT, 0, 0);
            }
            continue;
        }
        CloseHandle(ov.hEvent);
        bool relevant = false;
        BYTE* base = buf;
        for (;;) {
            FILE_NOTIFY_INFORMATION* fn = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(base);
            const std::wstring name(fn->FileName, fn->FileNameLength / sizeof(WCHAR));
            const bool is_tmp = name.size() >= 4 &&
                                _wcsicmp(name.c_str() + name.size() - 4, L".tmp") == 0;
            if (!is_tmp) { relevant = true; break; }  // 只认最终名（原子替换后）
            if (fn->NextEntryOffset == 0) break;
            base += fn->NextEntryOffset;
        }
        if (relevant) PostMessageW(st->hwnd, WM_APP_WATCHER_EVENT, 0, 0);
    }
}

void stop_watcher(UiState* st);

void start_watcher(UiState* st) {
    if (st->watcher_on) return;
    if (st->skin_dir.empty()) { set_hint(st, L"自动生效：请先选皮肤目录"); return; }
    st->dir_handle = CreateFileW(st->skin_dir.c_str(), FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                 nullptr);
    if (st->dir_handle == INVALID_HANDLE_VALUE) {
        st->dir_handle = nullptr;
        set_hint(st, L"自动生效：无法打开皮肤目录");
        return;
    }
    core::Settings s;
    std::string serr;
    apply::load_settings(apply::exe_dir() + L"\\config\\settings.ini", s, serr);  // 读不到=用默认
    const uint64_t debounce = s.has_debounce_ms ? s.debounce_ms : 300;
    const uint64_t max_wait = s.has_max_wait_ms ? s.max_wait_ms : 1000;
    st->deb = new core::Debouncer(debounce, max_wait);
    st->stop_ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    st->worker = CreateThread(nullptr, 0, WatcherWorker, st, 0, nullptr);
    if (st->worker == nullptr) {
        stop_watcher(st);  // 起线程失败 → 干净回收（内部会关掉已建的句柄）
        set_hint(st, L"自动生效：无法启动监听线程");
        return;
    }
    st->watcher_on = true;
    if (st->watcher_toggle) SetWindowTextW(st->watcher_toggle, L"停止自动");
    set_hint(st, L"自动生效已开：改图后约 " + std::to_wstring(debounce) +
                 L"ms 内自动重应用（会真的切换你的指针）");
}

void stop_watcher(UiState* st) {
    if (st->stop_ev) SetEvent(st->stop_ev);
    if (st->dir_handle) CancelIoEx(st->dir_handle, nullptr);  // 唤醒可能阻塞的读
    if (st->worker) {
        WaitForSingleObject(st->worker, 5000);
        CloseHandle(st->worker);
        st->worker = nullptr;
    }
    if (st->dir_handle) { CloseHandle(st->dir_handle); st->dir_handle = nullptr; }
    if (st->stop_ev) { CloseHandle(st->stop_ev); st->stop_ev = nullptr; }
    delete st->deb;
    st->deb = nullptr;
    const bool was = st->watcher_on;
    st->watcher_on = false;
    if (st->hwnd) KillTimer(st->hwnd, kWatcherTimer);
    if (was && st->watcher_toggle) SetWindowTextW(st->watcher_toggle, L"自动生效");
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
        p->hwnd = hwnd;   // show_busy 强制同步重绘要用主窗句柄

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
        p->watcher_toggle = mk(hwnd, L"BUTTON", L"自动生效", BS_PUSHBUTTON,
                               kMargin + (kBtnW + 8) * 3, y, kBtnW, kBtnH, IDC_WATCHER_TOGGLE, font);
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
                preview_only(st);   // 只 build_only，不碰注册表；横幅由 outcome_preview 给出
            }
            return 0;
        }
        case IDC_PREVIEW_BTN:
            preview_only(st);
            return 0;
        case IDC_APPLY_BTN:
            do_apply(st);
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
            return 0;
        }
        case IDC_WATCHER_TOGGLE: {
            // 开启即等于用户授权「换图后自动改指针」；关闭/选目录变化会干净停线程。
            if (st->watcher_on) {
                stop_watcher(st);
                set_hint(st, L"自动生效已关闭");
            } else {
                st->skin_dir = edit_text(st->edit);
                start_watcher(st);
            }
            return 0;
        }
        default:
            return 0;
        }
    }
    case WM_APP_WATCHER_EVENT: {
        if (!st || !st->watcher_on || st->deb == nullptr) return 0;
        const uint64_t now = GetTickCount64();
        st->deb->on_event(now);
        const uint64_t dl = st->deb->next_deadline();
        const UINT ms = dl > now ? static_cast<UINT>(dl - now + 1) : 1;
        SetTimer(hwnd, kWatcherTimer, ms, nullptr);
        return 0;
    }
    case WM_APP_WATCHER_DIED: {
        if (!st) return 0;
        if (st->watcher_on) { stop_watcher(st); set_hint(st, L"自动生效：目录失效，已停止"); }
        return 0;
    }
    case WM_TIMER: {
        if (wp != kWatcherTimer || !st || !st->watcher_on || st->deb == nullptr) return 0;
        const uint64_t now = GetTickCount64();
        if (st->deb->due(now)) {
            st->deb->fire(now);
            do_apply(st);   // 复用与手动「应用皮肤」完全同一条管线（含坏图中止、原子替换、L1）
            if (st->deb->pending()) {   // 仍有余波（风暴）→ 续一个定时器，避免饿死/漏放
                const uint64_t dl = st->deb->next_deadline();
                SetTimer(hwnd, kWatcherTimer, dl > now ? static_cast<UINT>(dl - now + 1) : 1, nullptr);
            }
        } else {
            const uint64_t dl = st->deb->next_deadline();
            SetTimer(hwnd, kWatcherTimer, dl > now ? static_cast<UINT>(dl - now + 1) : 1, nullptr);
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        // 让 STATIC 文字随窗口背景（不画成黑底）。
        static HBRUSH bg = GetSysColorBrush(COLOR_WINDOW);
        SetBkMode(reinterpret_cast<HDC>(lp), TRANSPARENT);
        return reinterpret_cast<LRESULT>(bg);
    }
    case WM_DESTROY:
        if (st && st->watcher_on) stop_watcher(st);   // 关窗前干净回收线程/句柄，不 leak
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

    // 仅作防御：exe 现为 WINDOWS 子系统（D18 V0.6.2），无参启动根本不会有控制台，
    // GetConsoleWindow() 通常返回 NULL、这里就是 no-op。保留一次隐藏是为了万一被某种
    // 脱离了父且分配了新控制台的启动方式带进来时也不漏黑框。
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
