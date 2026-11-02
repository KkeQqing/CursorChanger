// src/apply/report_model.cpp — 见头文件注释：GUI/CLI 共用的表现模型（无 Win32）
#include "apply/report_model.h"

#include <cstddef>

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
                break;
            }
        }
        for (const Conflict& c : r.conflicts) {
            if (narrow_ascii(c.slot_name) == row.registry_name) {
                row.conflict = true;
                break;
            }
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

}  // namespace cs::apply
