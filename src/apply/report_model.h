// src/apply/report_model.h — 窗口界面与 CLI 共用的**唯一表现模型**（DESIGN §9.2 / §11-M5）
//
// 为什么要有这一层：M5 判据要求「GUI 每个动作有等价 CLI 且两者结果一致」。
// 「一致」不能靠人眼看两边输出一不一样得出 —— 那本身又是一个没验过的断言。
// 唯一能做实的办法是让 GUI 和 CLI 读**同一份**数据、发**同一条**动作意图：
//   * ActionPlan   —— GUI 按钮 → 归一后的 CLI argv，反过来由真解析器 parse_args 验收；
//   * ReportView   —— PipelineResult 的逐槽投影，UI 与 CLI 报告都从它渲染。
// 本文件不含任何 Win32 符号（§2 层规则：core 不依赖 Win32，ui 不含业务逻辑；
// 业务在此，且只依赖已经无 Win32 的 core 与 apply 数据）。
#pragma once

#include "apply/pipeline.h"   // PipelineResult / Unclaimed / Conflict / ProducedFile
#include "core/slots.h"       // SlotId / SlotSpec / slot_table

#include <cstdint>
#include <string>
#include <vector>

namespace cs::apply {

// ---- 动作意图（D13：还原有两种，必须分列，两种确认文案不同）----
enum class GuiAction : uint8_t {
    Apply,     // 把选中的皮肤目录应用为系统指针
    Restore,   // 还原到首次 apply 前的基线快照（细粒度）
    Reset,     // 清掉我们写的一切、回到 OS 默认方案（兜底，不依赖基线）
};

struct ActionPlan {
    GuiAction action{GuiAction::Apply};

    // 可执行文件之后的 argv，逐 token（含前导 "--"）。GUI 直接把它交给同一进程入口即等价 CLI。
    std::vector<std::string> argv;

    // verb_table 里的规范动词名（"apply"/"restore"/"reset"）。它是「等价」的锚点：
    // 测试把 argv 回灌 parse_args，断言路由到的 Verb 与本 action 对应 —— 判据来自真解析器，
    // 不是本结构体自己，避免「拿模型证明自己」。
    std::string verb_token;

    // 展示在按钮旁的等价命令行，形如： cursorswitch --apply "<dir>"
    std::string cli_display;

    // D13：两种还原动作的稳定标识，UI 据此选不同确认文案；不得相同。
    std::string confirm_key;
};

// skin_dir 仅对 Apply 有意义；为空时 argv 只含动词（parse 侧应得到 MissingOperand，
// UI 据此在未选目录时禁用「应用」按钮 —— 这条约束由测试钉住，见 tests_m5）。
ActionPlan plan_for_action(GuiAction a, const std::string& skin_dir);

// D13：两种还原是**两个按钮、两种确认文案**，不可混为一谈。确认文案是数据/业务，
// 放这一层是为了能被单测钉住「两者必须不同」——UI 只负责原样显示，不自己编文案。
struct ConfirmText {
    bool need_confirm{false};
    std::wstring title;
    std::wstring body;
};

ConfirmText confirm_text_for(GuiAction a);

// ---- 逐槽状态 + 结果面板（§324：部分认领是正常的，UI 要列出缺哪几槽）----
// RowKind 是给 UI 的「人话」分类，取代以前把解码失败和加载器拒绝都塞进 [L1x] 的做法：
//   Missing        —— 该槽没有任何被认领的源文件
//   Applied        —— 有源、且产出的 .cur 被系统加载器接受（这一槽可用）
//   BadSource      —— 源文件本身不行：解不出/光栅化被拒/writer 拒绝/落地写失败（含「伪 PNG=JPEG」）
//   LoaderRejected —— 造出了 .cur 但 LoadCursorFromFileW 不接受（容器结构问题）
enum class RowKind : uint8_t { Missing, Applied, BadSource, LoaderRejected };

struct SlotRow {
    core::SlotId slot{core::SlotId::Invalid};
    std::string registry_name;   // ASCII，参与匹配（与 PipelineResult 的键同源）
    std::string display_name;    // 仅 UI 展示（UTF-8），不参与任何匹配
    bool used_in_v1{false};
    bool observed_on_this_machine{false};

    bool covered{false};         // produced 里有与本槽 registry_name 相等的项
    bool l1_ok{false};           // covered 时该产物的 L1 结果
    std::wstring output_path;    // covered 时的产物绝对路径
    bool conflict{false};        // conflicts 里有与本槽 registry_name 相等的项
    RowKind kind{RowKind::Missing};
    std::string l1_detail;       // covered 时产物上的失败/原因文本（供 UI 直接显示，不再只回内部码）
};

struct ReportView {
    // 覆盖**整张槽表**（除 Invalid），保证表里已知的槽不会从面板上消失。
    std::vector<SlotRow> rows;

    // 禁止静默缩水：registry_name 不在槽表里的产物必须出现在这里，而不是被行表漏掉。
    std::vector<ProducedFile> unmapped_produced;

    std::vector<Unclaimed> unclaimed;
    std::vector<Conflict> conflicts;
    uint32_t scanned{0};

    std::size_t covered_count() const;    // rows 里 covered 的数量
    std::size_t missing_count() const;    // rows 里 !covered 的数量
    std::size_t v1_row_count() const;     // rows 里 used_in_v1 的数量（应 == slot_table_v1_count()）
};

// 只读投影：不改动 r，不碰注册表。UI 与 CLI 报告都拿它渲染即得「一致」。
ReportView make_report(const PipelineResult& r);

// ---- 动作结果横幅（把「成没成」摆到界面上，不要求用户去翻命令行日志才知道，见 §9.2 GUI/CLI 同构）----
enum class OutcomeLevel : uint8_t { Info, Ok, Warn, Err };

struct Outcome {
    OutcomeLevel level{OutcomeLevel::Info};
    std::wstring banner;   // 单行人话，UI 直接显示；不掺 UI 自己的措辞（保证可被单测钉住）
};

// 预览（build_only，do_spi=false）：从没碰过系统指针，无论文件好坏都说明「未改动指针」。
Outcome outcome_preview(const PipelineResult& r);
// 应用：全链要么整体生效，要么在写注册表前中止。中止必须明说「指针未改动」+不合格数+原因。
Outcome outcome_apply(const PipelineResult& r);
// 还原 / 复位：UI 手上拿到的是 RestoreResult/SpiResult 的数字，这里按数字生成文案，保持可单测。
Outcome outcome_restore(uint32_t restored, uint32_t failed, uint32_t skipped, bool loaded,
                        bool spi_ok);
Outcome outcome_reset(uint32_t removed, uint32_t failed, bool spi_ok);

}  // namespace cs::apply
