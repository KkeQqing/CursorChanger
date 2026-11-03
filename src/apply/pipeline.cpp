#include "apply/pipeline.h"

#include "apply/spi.h"
#include "core/claim.h"
#include "core/cur_writer.h"
#include "core/png_source.h"
#include "core/slots.h"
#include "core/svg_source.h"
#include "selfcheck/l1.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace cs::apply {
namespace {

constexpr const wchar_t* kSchemeLabel = L"CursorSwitch";

std::wstring widen(std::string_view sv) { return std::wstring(sv.begin(), sv.end()); }

std::string narrow(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t c : w) out.push_back(c < 0x80 ? static_cast<char>(c) : static_cast<char>(0x3F));
    return out;
}

bool read_file(const std::wstring& path, std::vector<uint8_t>& out) {
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.assign(n > 0 ? static_cast<std::size_t>(n) : 0u, 0);
    const std::size_t got = out.empty() ? 0u : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

std::wstring absolute(const std::wstring& p) {
    wchar_t buf[4096];
    const DWORD n = GetFullPathNameW(p.c_str(), 4096, buf, nullptr);
    if (n == 0 || n >= 4096) return p;  // 原样返回，由后续 API 报错；不静默截断
    return std::wstring(buf, n);
}

// 原子替换：先写 .tmp，再 MoveFileExW 覆盖正式名（DESIGN D11）。
bool publish_file(const std::vector<uint8_t>& bytes, const std::wstring& final_path,
                  std::string& error) {
    const std::wstring tmp = final_path + L".tmp";
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || f == nullptr) {
        error = "cannot open temp file";
        return false;
    }
    const std::size_t put = bytes.empty() ? 0u : std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (put != bytes.size()) {
        DeleteFileW(tmp.c_str());
        error = "short write to temp file";
        return false;
    }
    if (MoveFileExW(tmp.c_str(), final_path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        DeleteFileW(tmp.c_str());
        error = "atomic move failed";
        return false;
    }
    return true;
}

struct Claimed {
    core::SlotId slot{core::SlotId::Invalid};
    std::wstring registry_name;
    std::wstring source;
};

enum class ExtKind : uint8_t { Cur,
                               Png,
                               Svg,
                               Ani,
                               Other };

ExtKind ext_kind(const std::wstring& name) {
    const std::wstring::size_type dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return ExtKind::Other;
    std::string e;
    for (std::wstring::size_type i = dot + 1; i < name.size(); ++i) {
        const wchar_t c = name[i];
        if (c >= 0x80) return ExtKind::Other;   // 非 ASCII 扩展名不当任何通道
        e.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (e == "cur") return ExtKind::Cur;
    if (e == "png") return ExtKind::Png;
    if (e == "svg") return ExtKind::Svg;
    if (e == "ani") return ExtKind::Ani;
    return ExtKind::Other;
}

// 把一个已认领的源文件变成 .cur 字节：.cur 透传；png/svg 走通道解码 -> CursorSpec -> CurWriter
// 单点封装。任何一步失败都必须返回带**观测事实**的错误串，绝不产出「看起来对」的半坏文件（§4）。
bool build_source_bytes(const std::wstring& source, core::SlotId slot, std::vector<uint8_t>& bytes,
                        std::string& error) {
    std::vector<uint8_t> raw;
    if (!read_file(source, raw)) {
        error = "cannot read source file";
        return false;
    }
    const ExtKind kind = ext_kind(source);
    if (kind == ExtKind::Cur || kind == ExtKind::Ani) {
        // passthrough：原样搬运、绝不重编码（D5；.ani 见 D21a）。
        // .ani 合法性不在这里判容器结构（格式语义未吃透，硬解析会假绿）——交给 L1 的
        // LoadCursorFromFileW 这条**独立** oracle（§4.4.1）；坏 .ani 会在 L1 变红并挡在写册前。
        bytes = std::move(raw);
        return true;
    }

    core::CursorSpec spec;
    std::string detail;
    if (kind == ExtKind::Png) {
        core::PngOptions po;   // 默认 sizes{16,32,48} + allow_upscale=false（§10 参数总表）
        const core::PngStatus st =
            core::load_png_to_spec(raw.data(), raw.size(), slot, po, spec, detail);
        if (st != core::PngStatus::Ok) {
            error = "png decode refused (status=" + std::to_string(static_cast<int>(st)) + ")";
            if (!detail.empty()) error += ": " + detail;
            return false;
        }
    } else if (kind == ExtKind::Svg) {
        core::SvgOptions so;   // 默认 work_resolution=1024 + sizes{16,32,48}（§10）
        core::SvgReport rep;
        const core::SvgStatus st =
            core::load_svg_to_spec(raw.data(), raw.size(), slot, so, spec, rep, detail);
        if (st != core::SvgStatus::Ok) {
            error = "svg raster refused (status=" + std::to_string(static_cast<int>(st)) + ")";
            if (!rep.guard.violations.empty()) error += ": disallowed feature";
            if (!detail.empty()) error += ": " + detail;
            return false;
        }
    } else {
        error = "unsupported source extension";
        return false;
    }

    const core::WriteError we = core::write_cursor_bytes(spec, bytes);
    if (we.status != core::WriteStatus::Ok) {   // reject_fully_transparent 默认开（H10）
        error = "cur writer rejected spec (status=" + std::to_string(static_cast<int>(we.status)) +
                ")";
        bytes.clear();   // 绝不交半坏文件（H9）
        return false;
    }
    return true;
}

}  // namespace

uint32_t count_source_files(const std::wstring& dir, std::vector<std::wstring>& names_out) {
    names_out.clear();
    // 四条通道一并扫描（§1 目标 + D21a）；通配大小写不敏感，ext_kind 再统一小写。
    for (const wchar_t* pat : {L"\\*.cur", L"\\*.png", L"\\*.svg", L"\\*.ani"}) {
        const std::wstring pattern = dir + pat;
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) names_out.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(names_out.begin(), names_out.end());  // 确定性顺序：冲突择一必须可复现
    return static_cast<uint32_t>(names_out.size());
}

PipelineResult build_only(const PipelineOptions& opt) {
    PipelineResult r;
    std::vector<std::wstring> names;
    r.scanned = count_source_files(opt.skin_dir, names);
    if (r.scanned == 0) {
        r.aborted_before_registry = true;
        r.abort_reason = "no .cur/.png/.svg/.ani files in skin dir";
        return r;
    }

    // 1) 认领。未认领与冲突都必须进报告，绝不静默缩水（DESIGN §4）。
    std::vector<Claimed> claimed;
    char scratch[256];
    for (const std::wstring& wname : names) {
        std::string ascii;
        for (wchar_t c : wname) ascii.push_back(c < 0x80 ? static_cast<char>(c) : static_cast<char>('?'));
        const core::ClaimResult cr = core::claim_by_stem(ascii, scratch, sizeof(scratch));
        if (cr.status != core::ClaimStatus::Claimed) {
            Unclaimed u;
            u.file = wname;
            u.reason = cr.status == core::ClaimStatus::NotInTable
                           ? "NotInTable"
                           : (cr.status == core::ClaimStatus::EmptyStem ? "EmptyStem" : "TooLong");
            r.unclaimed.push_back(u);
            continue;
        }
        const core::SlotSpec* sp = core::find_slot(cr.slot);
        if (sp == nullptr) {
            Unclaimed u;
            u.file = wname;
            u.reason = "AliasPointsAtMissingSlot";
            r.unclaimed.push_back(u);
            continue;
        }
        Claimed c;
        c.slot = cr.slot;
        c.registry_name = widen(sp->registry_name);
        c.source = opt.skin_dir + L"\\" + wname;
        claimed.push_back(c);
    }

    // 2) 同槽多文件的冲突：择第一个（顺序已排序，可复现），但必须报告。
    std::sort(claimed.begin(), claimed.end(), [](const Claimed& a, const Claimed& b) {
        if (a.registry_name != b.registry_name) return a.registry_name < b.registry_name;
        return a.source < b.source;   // 同槽不同扩展名（arrow.cur 与 arrow.png）也要确定性择一
    });
    for (std::size_t i = 0; i < claimed.size();) {
        std::size_t j = i;
        while (j < claimed.size() && claimed[j].registry_name == claimed[i].registry_name) ++j;
        if (j - i > 1) {
            Conflict cf;
            cf.slot_name = claimed[i].registry_name;
            for (std::size_t k = i; k < j; ++k) cf.files.push_back(claimed[k].source);
            r.conflicts.push_back(cf);
        }
        i = j;
    }

    // 3) 落地 applied\ + L1。任一 L1 失败 -> 中止且不写注册表（§7.1 第 3 步）。
    CreateDirectoryW(opt.applied_dir.c_str(), nullptr);
    const std::wstring applied_abs = absolute(opt.applied_dir);
    for (const Claimed& c : claimed) {
        if (!r.produced.empty() && r.produced.back().registry_name == c.registry_name) continue;  // 冲突只取第一个
        std::vector<uint8_t> bytes;
        ProducedFile p;
        p.registry_name = c.registry_name;
        // 输出扩展名跟随源类型：.ani 槽产出 .ani（scheme 值本就应指向动画容器），其余仍 .cur。
        const wchar_t* out_ext = (ext_kind(c.source) == ExtKind::Ani) ? L".ani" : L".cur";
        p.path = applied_abs + L"\\" + c.registry_name + out_ext;
        std::string cerr;
        if (!build_source_bytes(c.source, c.slot, bytes, cerr)) {
            p.l1_ok = false;
            p.l1_detail = cerr;
            r.produced.push_back(p);
            continue;
        }
        std::string err;
        if (!publish_file(bytes, p.path, err)) {
            p.l1_ok = false;
            p.l1_detail = "publish: " + err;
            r.produced.push_back(p);
            continue;
        }
        const selfcheck::L1Result l1 = selfcheck::l1_load_and_probe(p.path.c_str());
        p.l1_ok = l1.loaded;
        // 按 l1.h 的实测结论：失败判据只有 loaded；win_error 只能作为附加信息，
        // 不能写成「原因：GetLastError」那种看起来有依据、其实是空的解释。
        p.l1_detail = l1.loaded ? "ok"
                        : ("loader rejected (win_error=" + std::to_string(l1.win_error) +
                           ", 附加信息而非原因)");
        r.produced.push_back(p);
    }

    for (const ProducedFile& p : r.produced) {
        if (!p.l1_ok) {
            r.aborted_before_registry = true;
            r.abort_reason = "L1 rejected " + narrow(p.registry_name) + ": " + p.l1_detail;
            return r;
        }
    }
    if (r.produced.empty()) {
        r.aborted_before_registry = true;
        r.abort_reason = "nothing claimed";
    }
    return r;
}

PipelineResult apply_skin(const PipelineOptions& opt) {
    PipelineResult r = build_only(opt);
    if (r.aborted_before_registry) return r;

    // 4) 基线：只在首次 apply 创建，之后不覆盖（R5）。
    // 安全文件必须「读得出来」才允许 apply，仅仅「存在」不够。
    // 之前的做法是：文件存在就跳过快照、然后照常写注册表——可如果它已损坏，
    // apply 之后 restore_from_file 会 loaded=false 直接失败，用户只能靠「复位默认」逃生。
    // 那是把可恢复的操作做成了只剩兜底出口，方向错了。
    std::FILE* probe = nullptr;
    const bool file_exists =
        (_wfopen_s(&probe, opt.baseline_path.c_str(), L"rb") == 0 && probe != nullptr);
    if (file_exists) std::fclose(probe);

    if (file_exists) {
        Baseline prior;
        std::wstring err;
        if (!load_baseline(prior, opt.baseline_path.c_str(), err)) {
            r.aborted_before_registry = true;
            r.abort_reason = "baseline exists but is unreadable; refusing to apply";
            return r;
        }
        r.baseline_preexisted = true;
    } else {
        const Baseline snap = capture_baseline(opt.root, opt.sub_key.c_str());
        std::wstring err;
        r.baseline_saved = save_baseline(snap, opt.baseline_path.c_str(), err);
        if (!r.baseline_saved) {
            r.aborted_before_registry = true;
            r.abort_reason = "cannot save baseline";
            return r;
        }
    }

    // 5) 写注册表。键不存在时先建（ensure_key）—— 这一步是真机改动，须授权。
    bool key_there = false;
    if (key_exists(opt.root, opt.sub_key.c_str(), key_there).status == QueryStatus::Ok &&
        !key_there) {
        ensure_key(opt.root, opt.sub_key.c_str());
    }
    for (const ProducedFile& p : r.produced) {
        const WriteStatus w = write_sz(opt.root, opt.sub_key.c_str(), p.registry_name.c_str(),
                                       p.path.c_str());
        if (w == WriteStatus::Ok) ++r.registry_written; else ++r.registry_failed;
    }
    // 方案显示名：写默认值。Scheme 的确切写法仍未验证（§7.1 第 5 步），故只写显示名。
    r.scheme_label_written =
        write_sz(opt.root, opt.sub_key.c_str(), L"", kSchemeLabel) == WriteStatus::Ok;

    // 6) 让系统重新读取方案。do_spi=false 时根本不调用，且跳过不算成功：那一步会当场换掉用户正在用的指针，
    //    必须显式打开（沙箱测试验证写入与还原逻辑，不需要真的影响屏幕）。
    r.spi_expected = opt.do_spi;
    if (opt.do_spi) {
        r.spi = set_cursors_from_registry(opt.spi_flags);
    }

    // 7) L2 回读。
    std::vector<selfcheck::ExpectedSlot> expected;
    for (const ProducedFile& p : r.produced) {
        expected.push_back(selfcheck::ExpectedSlot{p.registry_name, p.path});
    }
    r.l2 = l2_verify_registry(opt.root, opt.sub_key.c_str(), expected);
    return r;
}

RestoreResult restore_from_file(const PipelineOptions& opt, SpiResult& spi_out, bool& loaded) {
    loaded = false;
    Baseline b;
    std::wstring err;
    if (!load_baseline(b, opt.baseline_path.c_str(), err)) return RestoreResult{};
    b.root = opt.root;
    b.sub_key = opt.sub_key;  // 快照文件里的键路径不可信，以调用方为准（沙箱测试依赖这点）
    loaded = true;
    RestoreResult r = restore_baseline(b);
    // 还原也必须尊重 do_spi：上一版无条件调 SPI，
    // 等于沙箱测试也会把用户真机的光标方案重载一遍。
    if (opt.do_spi) spi_out = set_cursors_from_registry(opt.spi_flags);
    return r;
}

RestoreResult reset_default(const PipelineOptions& opt, SpiResult& spi_out) {
    RestoreResult r = reset_to_default_scheme(opt.root, opt.sub_key.c_str());
    if (opt.do_spi) spi_out = set_cursors_from_registry(opt.spi_flags);
    return r;
}

std::wstring exe_dir() {
    wchar_t buf[4096];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    std::wstring p = (n == 0 || n >= 4096) ? std::wstring(L".") : std::wstring(buf, n);
    const std::wstring::size_type s = p.find_last_of(L"\\/");
    if (s == std::wstring::npos) return std::wstring(L".");
    return p.substr(0, s);
}

PipelineOptions make_live_opts(const std::wstring& skin_dir, bool do_spi) {
    const std::wstring base = exe_dir();
    PipelineOptions o;
    o.skin_dir = skin_dir;
    o.applied_dir = base + L"\\applied";
    o.root = Root::CurrentUser;
    o.sub_key = L"Control Panel\\Cursors";
    o.baseline_path = base + L"\\state\\baseline.ini";
    o.do_spi = do_spi;
    o.spi_flags = 0;   // U2 实测：flags=0 已生效，SPIF_SENDCHANGE 无可观测差别
    return o;
}

}  // namespace cs::apply
