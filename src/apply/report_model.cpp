// src/apply/report_model.cpp — 见头文件注释：GUI/CLI 共用的表现模型（无 Win32）
#include "apply/report_model.h"

#include <cstddef>
#include <string>

namespace cs::apply {
namespace {

// registry / slot 名在本项目里恒为纯 ASCII（R10 守卫强制），因此宽→窄是有损也安全的比较键：
// 非 ASCII 折成 '?'，两边同样处理即可稳定相等。绝不用于展示。
std::string narrow_ascii(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w) s.push_back(c < 0x80 ? static_cast<char>(c) : static_cast<char>('?'));
    return s;
}

ActionPlan base_plan(GuiAction a, const char* verb, const char* confirm) {
    ActionPlan p;
    p.action = a;
    p.verb_token = verb;
    p.confirm_key = confirm;
    p.argv.push_back(std::string("--") + verb);
    return p;
}

}  // namespace

ActionPlan plan_for_action(GuiAction a, const std::string& skin_dir) {
    switch (a) {
    case GuiAction::Apply: {
        ActionPlan p = base_plan(GuiAction::Apply, "apply", "apply_skin");
        if (!skin_dir.empty()) {
            p.argv.push_back(skin_dir);
            p.cli_display = "cursorswitch --apply \"" + skin_dir + "\"";
        } else {
            // 没选目录时不伪造参数：让 argv 回灌解析器得到 MissingOperand，
            // UI 由此知道此刻「应用」不可执行（而不是发出一条跑不通的命令）。
            p.cli_display = "cursorswitch --apply <dir>";
        }
        return p;
    }
    case GuiAction::Restore: {
        ActionPlan p = base_plan(GuiAction::Restore, "restore", "restore_baseline");
        p.cli_display = "cursorswitch --restore";
        return p;
    }
    case GuiAction::Reset: {
        ActionPlan p = base_plan(GuiAction::Reset, "reset", "reset_default");
        p.cli_display = "cursorswitch --reset";
        return p;
    }
    }
    // 不可达：枚举已穷尽。留兜底以免将来加动作时静默走到 Apply。
    return base_plan(GuiAction::Reset, "reset", "reset_default");
}

ConfirmText confirm_text_for(GuiAction a) {
    switch (a) {
    case GuiAction::Apply:
        // 常规动作，不弹确认（避免每次应用都打断）。
        return ConfirmText{false, std::wstring(), std::wstring()};
    case GuiAction::Restore:
        return ConfirmText{true, L"还原到基线",
                           L"将把系统指针恢复到你首次应用前保存的基线快照。\n"
                           L"（若从没应用过，则无可还原内容，会用复位默认兜底。）"};
    case GuiAction::Reset:
        return ConfirmText{true, L"复位系统默认",
                           L"将清除 CursorSwitch 写入的全部自定义光标值，回到 Windows 默认方案。\n"
                           L"这一步不依赖基线快照，是「退不回去」时的兜底出口。"};
    }
    return ConfirmText{true, L"确认操作", L"确定要执行该操作吗？"};
}

std::size_t ReportView::covered_count() const {
    std::size_t n = 0;
    for (const SlotRow& r : rows) {
        if (r.covered) ++n;
    }
    return n;
}

std::size_t ReportView::missing_count() const {
    std::size_t n = 0;
    for (const SlotRow& r : rows) {
        if (!r.covered) ++n;
    }
    return n;
}

std::size_t ReportView::v1_row_count() const {
    std::size_t n = 0;
    for (const SlotRow& r : rows) {
        if (r.used_in_v1) ++n;
    }
    return n;
}

ReportView make_report(const PipelineResult& r) {
    ReportView v;
    v.scanned = r.scanned;
    v.unclaimed = r.unclaimed;
    v.conflicts = r.conflicts;

    std::size_t n = 0;
    const core::SlotSpec* t = core::slot_table(n);

    // 逐槽建行（整表，除 Invalid 外）。匹配键 = registry_name（ASCII），与 pipeline 的 join 键同源。
    for (std::size_t i = 0; i < n; ++i) {
        SlotRow row;
        row.slot = t[i].id;
        row.registry_name = std::string(t[i].registry_name);
        row.display_name = std::string(t[i].display_name);
        row.used_in_v1 = t[i].used_in_v1;
        row.observed_on_this_machine = t[i].observed_on_this_machine;

        for (const ProducedFile& p : r.produced) {
            if (narrow_ascii(p.registry_name) == row.registry_name) {
                row.covered = true;
                row.l1_ok = p.l1_ok;
                row.output_path = p.path;
                row.l1_detail = p.l1_detail;
                break;
            }
        }
        for (const Conflict& c : r.conflicts) {
            if (narrow_ascii(c.slot_name) == row.registry_name) {
                row.conflict = true;
                break;
            }
        }
        // kind：covered+l1_ok 才是可用；covered 但失败的，只有「loader rejected」才是加载器拒绝，
        // 其余（解码/光栅化/writer/落地）都归「源文件不行」，因为 pipeline 就是这么标记 l1_detail 的。
        if (!row.covered) {
            row.kind = RowKind::Missing;
        } else if (row.l1_ok) {
            row.kind = RowKind::Applied;
        } else if (row.l1_detail.find("loader rejected") != std::string::npos) {
            row.kind = RowKind::LoaderRejected;
        } else {
            row.kind = RowKind::BadSource;
        }
        v.rows.push_back(row);
    }

    // 禁止静默缩水：任何 registry_name 不在槽表里的产物，单独记账，绝不因「建行没走到」而消失。
    for (const ProducedFile& p : r.produced) {
        if (core::slot_from_registry_name(narrow_ascii(p.registry_name)) == core::SlotId::Invalid) {
            v.unmapped_produced.push_back(p);
        }
    }

    return v;
}

namespace {
// abort_reason / l1_detail 在本项目里恒为 ASCII（英文原因串），逐字节升宽即可安全显示。
std::wstring wfrom(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (unsigned char c : s) w.push_back(static_cast<wchar_t>(c));
    return w;
}
}  // namespace

Outcome outcome_preview(const PipelineResult& r) {
    std::size_t good = 0, bad = 0;
    for (const ProducedFile& p : r.produced) {
        if (p.l1_ok) ++good; else ++bad;
    }
    Outcome o;
    o.banner = L"预览：扫描 " + std::to_wstring(r.scanned) + L" 个文件，合格 " +
               std::to_wstring(good) + L" 槽";
    if (bad) o.banner += L"，不合格 " + std::to_wstring(bad) + L" 个";
    o.banner += L"。（预览不改动系统指针）";
    o.level = bad ? OutcomeLevel::Warn : OutcomeLevel::Info;
    return o;
}

Outcome outcome_apply(const PipelineResult& r) {
    Outcome o;
    if (r.aborted_before_registry) {
        std::size_t bad = 0;
        for (const ProducedFile& p : r.produced) {
            if (!p.l1_ok) ++bad;
        }
        o.level = OutcomeLevel::Err;
        o.banner = L"应用失败：0 槽生效，" + std::to_wstring(bad) +
                   L" 个文件不合格；系统指针未改动。原因：" + wfrom(r.abort_reason);
        return o;
    }
    if (r.registry_written > 0 && r.registry_failed == 0 && r.spi.performed && r.spi.called_ok) {
        o.level = OutcomeLevel::Ok;
        o.banner = L"已应用 " + std::to_wstring(r.registry_written) + L" 个槽位，系统指针已切换。";
        if (!r.conflicts.empty())
            o.banner += L"（" + std::to_wstring(r.conflicts.size()) + L" 处同槽取首，其余已在列表标注）";
        return o;
    }
    o.level = OutcomeLevel::Warn;
    o.banner = L"应用结果不明确：写入 " + std::to_wstring(r.registry_written) + L"、失败 " +
               std::to_wstring(r.registry_failed) + L"，SPI " +
               (r.spi.performed && r.spi.called_ok ? L"成功" : L"未成功");
    return o;
}

Outcome outcome_restore(uint32_t restored, uint32_t failed, uint32_t skipped, bool loaded, bool spi_ok) {
    Outcome o;
    if (!loaded) {
        o.level = OutcomeLevel::Err;
        o.banner = L"还原失败：没有可读的基线快照（可能从没应用过）。请用「复位默认」。";
        return o;
    }
    if (failed == 0 && spi_ok) {
        o.level = OutcomeLevel::Ok;
        o.banner =
            L"已还原 " + std::to_wstring(restored) + L" 项，系统指针回到你应用前的快照。";
        return o;
    }
    o.level = OutcomeLevel::Warn;
    o.banner = L"还原部分完成：成功 " + std::to_wstring(restored) + L"、失败 " +
               std::to_wstring(failed) + L"、跳过 " + std::to_wstring(skipped) + L"。";
    return o;
}

Outcome outcome_reset(uint32_t removed, uint32_t failed, bool spi_ok) {
    Outcome o;
    if (failed == 0 && spi_ok) {
        o.level = OutcomeLevel::Ok;
        o.banner = L"已复位：清除 " + std::to_wstring(removed) + L" 项，回到 Windows 默认方案。";
        return o;
    }
    o.level = OutcomeLevel::Warn;
    o.banner = L"复位部分完成：清除 " + std::to_wstring(removed) + L"、失败 " +
               std::to_wstring(failed) + L"。";
    return o;
}

}  // namespace cs::apply
