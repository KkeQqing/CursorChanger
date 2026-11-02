// src/tests/tests_m5.cpp — M5a：GUI/CLI 共用表现模型 + 动作意图
//
// 本文件刻意不拿 report_model 自己当期望（AGENTS §4「禁止同义反复的自表自检」）。
// 两个独立 oracle：
//   * 动作等价性用**真解析器** core::cli::parse_args 验收（模型发的 argv 必须被路由到对应 Verb）；
//   * 逐槽投影用**真槽表** core::slots 验收（covered/missing 计数来自表内容，不是模型内部常量）。
#include "cases.h"

#include "apply/pipeline.h"
#include "apply/report_model.h"
#include "core/cli.h"
#include "core/slots.h"

#include <cstddef>
#include <string>
#include <vector>

using namespace cs::apply;
using cs::core::Arity;
using cs::core::Parsed;
using cs::core::SlotId;
using cs::core::Status;
using cs::core::Verb;
using cs::test::Runner;

namespace {

// 把 ActionPlan.argv 当作 CLI 参数喂回真解析器（argv[0] 用占位 exe 名）。
Parsed feed_parser(const ActionPlan& plan) {
    std::vector<const char*> a;
    a.push_back("cursorswitch");
    for (const std::string& t : plan.argv) a.push_back(t.c_str());
    return cs::core::parse_args(static_cast<int>(a.size()), a.data());
}

bool verb_token_in_table(const std::string& token) {
    std::size_t n = 0;
    const cs::core::VerbSpec* t = cs::core::verb_table(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (token == std::string(t[i].name)) return true;
    }
    return false;
}

ProducedFile mk_produced(const wchar_t* reg, const wchar_t* path, bool l1) {
    ProducedFile p;
    p.registry_name = reg;
    p.path = path;
    p.l1_ok = l1;
    return p;
}

// ---------- 正例 ----------

// 判据「GUI 每个动作有等价 CLI」的机器化版本：动作 argv 必须被真解析器路由到对应动词。
void gui_action_argv_routes_through_real_parser(Runner& runner) {
    const ActionPlan ap = plan_for_action(GuiAction::Apply, "D:\\skin");
    const Parsed pa = feed_parser(ap);
    CHECK(pa.status == Status::Ok);
    CHECK(pa.verb == Verb::Apply);
    CHECK(pa.operand == std::string_view("D:\\skin"));

    const Parsed pr = feed_parser(plan_for_action(GuiAction::Restore, ""));
    CHECK(pr.status == Status::Ok);
    CHECK(pr.verb == Verb::Restore);

    const Parsed pz = feed_parser(plan_for_action(GuiAction::Reset, ""));
    CHECK(pz.status == Status::Ok);
    CHECK(pz.verb == Verb::Reset);
}

// 三个动作的 verb_token 必须都是 verb_table 里真实存在的名字（模型与表不得各说各话）。
void gui_action_tokens_are_real_verbs(Runner& runner) {
    CHECK(verb_token_in_table(plan_for_action(GuiAction::Apply, "x").verb_token));
    CHECK(verb_token_in_table(plan_for_action(GuiAction::Restore, "").verb_token));
    CHECK(verb_token_in_table(plan_for_action(GuiAction::Reset, "").verb_token));
}

// D13：两种还原动作必须可区分（不同 verb、不同确认文案）。
void two_restore_actions_are_distinct(Runner& runner) {
    const ActionPlan r = plan_for_action(GuiAction::Restore, "");
    const ActionPlan z = plan_for_action(GuiAction::Reset, "");
    CHECK(r.verb_token != z.verb_token);
    CHECK(r.confirm_key != z.confirm_key);
    CHECK(!r.confirm_key.empty() && !z.confirm_key.empty());
    CHECK(feed_parser(r).verb != feed_parser(z).verb);
}

// ReportView 覆盖整张槽表，且 covered+missing == 行数（结构恒等式，防行表静默漏槽）。
void report_rows_cover_whole_slot_table(Runner& runner) {
    PipelineResult r;
    r.scanned = 2;
    r.produced.push_back(mk_produced(L"Arrow", L"C:\\applied\\Arrow.cur", true));
    r.produced.push_back(mk_produced(L"Wait", L"C:\\applied\\Wait.cur", false));

    // 前置校验：夹具用的名字必须真的是表内有效槽，否则下面的计数就是空对空。
    CHECK(cs::core::slot_from_registry_name("Arrow") != SlotId::Invalid);
    CHECK(cs::core::slot_from_registry_name("Wait") != SlotId::Invalid);

    const ReportView v = make_report(r);
    std::size_t table_n = 0;
    cs::core::slot_table(table_n);
    CHECK(v.rows.size() == table_n);
    CHECK(v.covered_count() + v.missing_count() == v.rows.size());
    CHECK(v.covered_count() == 2);   // 只有 Arrow、Wait 命中
    CHECK(v.missing_count() == table_n - 2);
    CHECK(v.unmapped_produced.empty());

    bool arrow_seen = false, wait_seen = false, ibeam_uncovered = false;
    for (const SlotRow& row : v.rows) {
        if (row.slot == SlotId::Arrow) {
            arrow_seen = row.covered && row.l1_ok;
        } else if (row.slot == SlotId::Wait) {
            wait_seen = row.covered && !row.l1_ok;   // L1 失败要如实标出，不得当成成功
        } else if (row.slot == SlotId::IBeam) {
            ibeam_uncovered = !row.covered;           // 部分认领：缺的槽要显式可见
        }
    }
    CHECK(arrow_seen);
    CHECK(wait_seen);
    CHECK(ibeam_uncovered);
}

// v1 行数与 slot_table_v1_count 一致（判据里的「15 槽」有单一来源）。
void report_v1_count_matches_slot_table(Runner& runner) {
    const ReportView v = make_report(PipelineResult{});
    CHECK(v.v1_row_count() == cs::core::slot_table_v1_count());
    CHECK(cs::core::slot_table_v1_count() >= 1);   // 自证：否则上面一条恒等于 0 也是「绿」
}

// 冲突槽必须既显示 covered（取到的第一个）又显示 conflict。
void report_marks_conflict_on_slot(Runner& runner) {
    PipelineResult r;
    r.produced.push_back(mk_produced(L"Arrow", L"C:\\applied\\Arrow.cur", true));
    Conflict c;
    c.slot_name = L"Arrow";
    c.files.push_back(L"arrow.cur");
    c.files.push_back(L"arrow-2.cur");
    r.conflicts.push_back(c);

    const ReportView v = make_report(r);
    bool conflict_row_ok = false;
    for (const SlotRow& row : v.rows) {
        if (row.slot == SlotId::Arrow) conflict_row_ok = (row.covered && row.conflict);
    }
    CHECK(conflict_row_ok);
}

// ---------- 负对照 ----------

// 禁止静默缩水：一个不在槽表里的产物（如别名指向了表外槽 / 未来加新名忘了进表）
// 必须出现在 unmapped_produced，而不是被「建行循环走不到」悄悄丢掉。
// 若把 make_report 的 unmapped 逻辑删掉，这条必红。
void report_surfaces_unmapped_produced(Runner& runner) {
    PipelineResult r;
    r.produced.push_back(mk_produced(L"Arrow", L"C:\\applied\\Arrow.cur", true));
    r.produced.push_back(mk_produced(L"GhostSlot", L"C:\\applied\\GhostSlot.cur", true));
    CHECK(cs::core::slot_from_registry_name("GhostSlot") == SlotId::Invalid);   // 前提：确实不在表里

    const ReportView v = make_report(r);
    CHECK(v.unmapped_produced.size() == 1);
    if (!v.unmapped_produced.empty()) {
        CHECK(v.unmapped_produced[0].registry_name == std::wstring(L"GhostSlot"));
    }
    // Ghost 不能被错误归到任何真实槽行上（covered 计数仍是 1：只有 Arrow）。
    CHECK(v.covered_count() == 1);
}

// Apply 未选目录时，发出的命令喂解析器应得 MissingOperand（UI 据此禁用按钮）。
// 取反方向：这不是「成功」，正是它证明按钮的启用条件绑在真解析器的 arity 上。
void apply_without_dir_is_missing_operand(Runner& runner) {
    const ActionPlan ap = plan_for_action(GuiAction::Apply, "");
    const Parsed p = feed_parser(ap);
    CHECK(p.verb == Verb::Apply);
    CHECK(p.status == Status::MissingOperand);
    CHECK(cs::core::verb_arity(Verb::Apply) == Arity::NeedDir);
}

}  // namespace

namespace cs::test {

void register_ui_cases(Runner& runner) {
    runner.add("gui_action_argv_routes_through_real_parser", &gui_action_argv_routes_through_real_parser);
    runner.add("gui_action_tokens_are_real_verbs", &gui_action_tokens_are_real_verbs);
    runner.add("two_restore_actions_are_distinct", &two_restore_actions_are_distinct);
    runner.add("report_rows_cover_whole_slot_table", &report_rows_cover_whole_slot_table);
    runner.add("report_v1_count_matches_slot_table", &report_v1_count_matches_slot_table);
    runner.add("report_marks_conflict_on_slot", &report_marks_conflict_on_slot);
    runner.add("report_surfaces_unmapped_produced", &report_surfaces_unmapped_produced);
    runner.add("apply_without_dir_is_missing_operand", &apply_without_dir_is_missing_operand);
}

}  // namespace cs::test
