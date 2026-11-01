#include "app/verbs.h"

#include "apply/baseline.h"
#include "apply/pipeline.h"
#include "apply/registry.h"
#include "apply/spi.h"
#include "selfcheck/verify_live.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace cs::app {

// 本文件是 cs::app，而被调用的类型都在 cs::apply —— 兄弟命名空间不会自动可见，
// 上一版直接在 cs::app 里写 PipelineOptions / Root / RestoreResult 才会整片报「未知类型」。
// 用 using 而不是逐处限定：本文件的职责就是编排 apply 层。
using namespace apply;

namespace {

// 唯一表述处：真机方案键。沙箱测试用别的键，靠 PipelineOptions 传进去。
constexpr const wchar_t* kCursorsSubKey = L"Control Panel\\Cursors";
constexpr uint32_t kSettleMs = 200;   // probe.settle_ms；落点后等一拍，避免抓到过渡帧

std::wstring exe_dir() {
    wchar_t buf[4096];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    std::wstring p = (n == 0 || n >= 4096) ? std::wstring(L".") : std::wstring(buf, n);
    const std::wstring::size_type s = p.find_last_of(L"\\/");
    if (s == std::wstring::npos) return std::wstring(L".");
    return p.substr(0, s);
}

// argv 走 UTF-8 转宽字符。已知限制：Windows 的 ANSI main 给的是本机代码页字节，
// 因此**含非 ASCII 的路径参数不可靠**（真机闭环用的目录由我们自己生成，全 ASCII）。
// 完整的宽字符入口属 GUI 阶段的工作，记在此处以免被当成已解决。
std::wstring to_wide(std::string_view sv) {
    if (sv.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, sv.data(),
                                      static_cast<int>(sv.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, sv.data(), static_cast<int>(sv.size()),
                        out.data(), n);
    return out;
}

PipelineOptions make_opts(const std::wstring& skin_dir, bool do_spi) {
    const std::wstring base = exe_dir();
    PipelineOptions o;
    o.skin_dir = skin_dir;
    o.applied_dir = base + L"\\applied";
    o.root = Root::CurrentUser;
    o.sub_key = kCursorsSubKey;
    o.baseline_path = base + L"\\state\\baseline.ini";
    o.do_spi = do_spi;
    o.spi_flags = 0;
    return o;
}

void ensure_parent_dir(const std::wstring& file_path) {
    const std::wstring::size_type s = file_path.find_last_of(L"\\/");
    if (s != std::wstring::npos) CreateDirectoryW(file_path.substr(0, s).c_str(), nullptr);
}

void report_pipeline(const PipelineResult& r) {
    std::printf("PLAN scanned=%u produced=%zu unclaimed=%zu conflicts=%zu\n", r.scanned,
                r.produced.size(), r.unclaimed.size(), r.conflicts.size());
    for (const Unclaimed& u : r.unclaimed) {
        std::wprintf(L"  UNCLAIMED %ls (%hs)\n", u.file.c_str(), u.reason.c_str());
    }
    for (const Conflict& c : r.conflicts) {
        std::wprintf(L"  CONFLICT slot=%ls files=%zu (first taken, rest reported)\n",
                     c.slot_name.c_str(), c.files.size());
    }
    for (const ProducedFile& p : r.produced) {
        std::wprintf(L"  %-12s l1=%d %ls\n", p.registry_name.c_str(), p.l1_ok ? 1 : 0, p.path.c_str());
    }
    std::printf("REGISTRY written=%u failed=%u label_written=%d baseline_saved=%d preexisted=%d\n",
                r.registry_written, r.registry_failed, r.scheme_label_written ? 1 : 0,
                r.baseline_saved ? 1 : 0, r.baseline_preexisted ? 1 : 0);
    std::printf("SPI performed=%d called_ok=%d expected=%d\n", r.spi.performed ? 1 : 0,
                r.spi.called_ok ? 1 : 0, r.spi_expected ? 1 : 0);
    std::printf("L2 checked=%u matched=%u mismatches=%zu\n", r.l2.checked, r.l2.matched,
                r.l2.mismatches.size());
    for (const selfcheck::L2Mismatch& m : r.l2.mismatches) {
        std::wprintf(L"  L2MISMATCH %ls expected=%ls actual=%ls missing=%d\n", m.registry_name.c_str(),
                     m.expected.c_str(), m.actual.c_str(), m.actual_missing ? 1 : 0);
    }
    if (r.aborted_before_registry) {
        std::printf("ABORTED before registry: %s\n", r.abort_reason.c_str());
    }
}

// 采样点：屏幕中心与两个偏移点。真机上只有「至少一处变了」才是可靠信号 ——
// 单点可能正好落在某个自绘光标的窗口上，那不算我们没生效。
struct ProbePoint {
    uint32_t x;
    uint32_t y;
};

std::vector<ProbePoint> probe_points() {
    const int w = GetSystemMetrics(SM_CXSCREEN);
    const int h = GetSystemMetrics(SM_CYSCREEN);
    std::vector<ProbePoint> v;
    if (w <= 0 || h <= 0) return v;
    v.push_back(ProbePoint{static_cast<uint32_t>(w / 2), static_cast<uint32_t>(h / 2)});
    if (w > 200 && h > 200) {
        v.push_back(ProbePoint{static_cast<uint32_t>(w / 3), static_cast<uint32_t>(h / 3)});
        v.push_back(ProbePoint{static_cast<uint32_t>(w * 2 / 3), static_cast<uint32_t>(h * 4 / 5)});
    }
    return v;
}

void dump_captures(const char* tag, const std::vector<selfcheck::L3Capture>& caps) {
    for (std::size_t i = 0; i < caps.size(); ++i) {
        const selfcheck::L3Capture& c = caps[i];
        std::printf("OBSERVE %-8s pt=%zu %ux%u got=%d frame=%ux%u opaque=%u colors=%u fp=%llu %s\n", tag, i,
                    c.x, c.y, c.got_cursor ? 1 : 0, c.width, c.height, c.opaque_pixels, c.distinct_colors,
                    static_cast<unsigned long long>(c.fingerprint), c.detail.c_str());
    }
}

std::vector<selfcheck::L3Capture> take_all(const std::vector<ProbePoint>& pts) {
    std::vector<selfcheck::L3Capture> v;
    for (const ProbePoint& p : pts) v.push_back(selfcheck::l3_capture_cursor_at(p.x, p.y, kSettleMs));
    return v;
}

// 中途任何 return 都要还原：靠显式 guard，而不是「记得最后调 restore」。
struct RestoreGuard {
    bool armed{false};
    PipelineOptions opt;

    ~RestoreGuard() {
        if (!armed) return;
        Baseline b;
        std::wstring err;
        if (load_baseline(b, opt.baseline_path.c_str(), err)) {
            b.root = opt.root;
            b.sub_key = opt.sub_key;
            const RestoreResult r = restore_baseline(b);
            std::printf("GUARD restored=%u failed=%u skipped=%u\n", r.restored, r.failed, r.skipped);
        } else {
            // 基线读不出来时用兜底出口：清掉我们写的所有值，绝不让用户留在皮肤状态
            const RestoreResult r = reset_to_default_scheme(opt.root, opt.sub_key.c_str());
            std::printf("GUARD baseline unusable (%ls) -> reset removed=%u failed=%u\n", err.c_str(),
                        r.restored, r.failed);
        }
        const SpiResult s = set_cursors_from_registry(opt.spi_flags);
        std::printf("GUARD spi performed=%d called_ok=%d\n", s.performed ? 1 : 0, s.called_ok ? 1 : 0);
    }
};

int report_restore(const RestoreResult& r) {
    std::printf("RESTORE restored=%u failed=%u skipped=%u\n", r.restored, r.failed, r.skipped);
    return r.failed == 0 ? 0 : 1;
}

}  // namespace

int run_apply(std::string_view skin_dir) {
    const std::wstring dir = to_wide(skin_dir);
    if (dir.empty()) {
        std::fprintf(stderr, "error: skin dir missing or not valid UTF-8\n");
        return 2;
    }
    ensure_parent_dir(exe_dir() + L"\\state\\baseline.ini");
    const PipelineResult r = apply_skin(make_opts(dir, true));
    report_pipeline(r);
    return r.ok() ? 0 : 1;
}

int run_restore() {
    ensure_parent_dir(exe_dir() + L"\\state\\baseline.ini");
    const PipelineOptions opt = make_opts(std::wstring(), true);
    SpiResult spi;
    bool loaded = false;
    const RestoreResult r = restore_from_file(opt, spi, loaded);
    if (!loaded) {
        std::fprintf(stderr, "error: no readable baseline at %ls\n", opt.baseline_path.c_str());
        std::fprintf(stderr, "hint: use --reset to drop everything we wrote\n");
        return 2;
    }
    report_restore(r);
    std::printf("SPI performed=%d called_ok=%d\n", spi.performed ? 1 : 0, spi.called_ok ? 1 : 0);
    return r.failed == 0 ? 0 : 1;
}

int run_reset() {
    const PipelineOptions opt = make_opts(std::wstring(), true);
    SpiResult spi;
    const RestoreResult r = reset_default(opt, spi);
    std::printf("RESET removed=%u failed=%u\n", r.restored, r.failed);
    std::printf("SPI performed=%d called_ok=%d\n", spi.performed ? 1 : 0, spi.called_ok ? 1 : 0);
    return r.failed == 0 ? 0 : 1;
}

int run_live_check(std::string_view skin_dir) {
    const std::wstring dir = to_wide(skin_dir);
    if (dir.empty()) {
        std::fprintf(stderr, "error: skin dir missing or not valid UTF-8\n");
        return 2;
    }
    ensure_parent_dir(exe_dir() + L"\\state\\baseline.ini");

    const std::vector<ProbePoint> pts = probe_points();
    if (pts.empty()) {
        std::fprintf(stderr, "error: no screen metrics; L3 impossible here\n");
        return 2;
    }

    PipelineOptions opt = make_opts(dir, true);
    int rc = 1;
    {
        RestoreGuard guard;
        guard.opt = opt;
        // 关键：先决定「要不要 arm」。基线不可用时 apply 也不能做，否则无从还原。
        // 先决定「基线到底在不在」，再决定「在但读不出来」要不要拒绝：
        // 上一版把这两种情况混成一锅（load 失败就 err 非空 -> 一律拒绝），
        // 结果首次 apply 分支根本不可达，live-check 在新机器上永远不敢跑。
        std::FILE* probe = nullptr;
        const bool base_present =
            (_wfopen_s(&probe, opt.baseline_path.c_str(), L"rb") == 0 && probe != nullptr);
        if (base_present) std::fclose(probe);

        Baseline pre;
        std::wstring err;
        const bool base_readable =
            !base_present || load_baseline(pre, opt.baseline_path.c_str(), err);
        std::printf("PRE baseline_present=%d readable=%d\n", base_present ? 1 : 0,
                    base_readable ? 1 : 0);
        if (base_present && !base_readable) {
            // 文件在但读不出来：绝不 apply，否则换完退不回去，只剩粗粒度兜底
            std::printf("PRE baseline present but unreadable -> refusing live check\n");
            return 1;
        }

        const std::vector<selfcheck::L3Capture> before = take_all(pts);
        dump_captures("before", before);

        const PipelineResult r = apply_skin(opt);
        report_pipeline(r);
        if (!r.ok()) {
            std::printf("LIVE abort: apply not ok (guard will still restore if we wrote anything)\n");
            if (r.registry_written > 0) guard.armed = true;
            return 1;
        }
        guard.armed = true;  // 从这一刻起，任何退出路径都必须还原

        const std::vector<selfcheck::L3Capture> during = take_all(pts);
        dump_captures("during", during);

        std::size_t changed = 0;
        for (std::size_t i = 0; i < during.size() && i < before.size(); ++i) {
            const bool diff = during[i].fingerprint != before[i].fingerprint ||
                              during[i].width != before[i].width || during[i].height != before[i].height;
            std::printf("LIVE pt=%zu changed=%d (fp%08lx -> fp%08lx, %ux%u -> %ux%u)\n", i, diff ? 1 : 0,
                        static_cast<unsigned long>(before[i].fingerprint & 0xFFFFFFFul),
                        static_cast<unsigned long>(during[i].fingerprint & 0xFFFFFFFul), before[i].width,
                        before[i].height, during[i].width, during[i].height);
            if (diff) ++changed;
        }
        std::printf("LIVE changed_points=%zu of %zu\n", changed, during.size());

        // U2 对照：同一份注册表内容，flags=0 已经生效；再显式跑一次带 SPIF_SENDCHANGE 的调用，
        // 观察是否有任何可差别 —— 这给「flags 到底要不要」一条本机证据而不是猜测。
        const SpiResult again = set_cursors_from_registry(SPIF_SENDCHANGE);
        std::printf("U2 second SPI with SPIF_SENDCHANGE performed=%d called_ok=%d\n",
                    again.performed ? 1 : 0, again.called_ok ? 1 : 0);
        const std::vector<selfcheck::L3Capture> after_flags = take_all(pts);
        dump_captures("afterFlg", after_flags);
        std::size_t flags_diff = 0;
        for (std::size_t i = 0; i < after_flags.size() && i < during.size(); ++i) {
            if (after_flags[i].fingerprint != during[i].fingerprint) ++flags_diff;
        }
        std::printf("U2 flags_made_visible_difference=%zu\n", flags_diff);

        rc = (changed >= 1 && r.l2.ok() && r.registry_written > 0) ? 0 : 1;
        std::printf("LIVE verdict apply+l2+changed => rc=%d\n", rc);
        // guard 析构在这里还原
    }

    const std::vector<selfcheck::L3Capture> restored = take_all(probe_points());
    dump_captures("restored", restored);

    Baseline b;
    std::wstring err;
    if (!load_baseline(b, make_opts(std::wstring(), false).baseline_path.c_str(), err)) {
        std::printf("POST cannot re-read baseline (expected if never saved)\n");
        return rc;
    }
    std::printf("POST baseline present; registry values after restore:\n");
    std::size_t n = 0;
    const core::SlotSpec* t = core::slot_table(n);
    uint32_t lingering = 0;
    for (std::size_t i = 0; i < n; ++i) {
        std::wstring v;
        bool found = false;
        const std::wstring nm(t[i].registry_name.begin(), t[i].registry_name.end());
        read_sz(Root::CurrentUser, kCursorsSubKey, nm.c_str(), v, found);
        const bool ours = found && v.find(L"\\applied\\") != std::wstring::npos;
        if (ours) ++lingering;
        std::wprintf(L"  %-12s present=%d ours=%d %ls\n", t[i].registry_name.data() ? nm.c_str() : L"?",
                     found ? 1 : 0, ours ? 1 : 0, v.c_str());
    }
    std::printf("POST lingering_our_values=%u\n", lingering);
    if (lingering != 0) {
        std::printf("POST FAIL: restore left our paths behind\n");
        return 1;
    }
    return rc;
}

}  // namespace cs::app
