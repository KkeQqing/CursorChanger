// src/app/main.cpp — 入口：只负责分发，业务全在 apply/ 与 selfcheck/（§2 层规则）
#include "app/verbs.h"
#include "core/cli.h"
#include "core/version.h"
#include "ui/main_window.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstddef>
#include <cstdio>
#include <string>

namespace {

void print_usage() {
    std::size_t n = 0;
    const cs::core::VerbSpec* table = cs::core::verb_table(n);

    std::printf("cursorswitch V%s\n", cs::core::kVersion);
    std::printf("usage:\n");
    for (std::size_t i = 0; i < n; ++i) {
        const std::string name(table[i].name);
        const std::string summary(table[i].summary);
        const std::string arg = table[i].arity == cs::core::Arity::NeedDir ? " <dir>" : "";
        std::printf("  --%-12s%s %s\n", name.c_str(), arg.c_str(), summary.c_str());
    }
    std::printf("\nAll three channels are wired end-to-end: .cur passthrough, PNG->cur, SVG->cur.\n");
    std::printf("Run with no arguments for the window. Skin dir files are claimed by filename stem.\n");
    std::printf("Note: --live-check restores even if a middle step fails (RAII guard).\n");
}

}  // namespace

int main(int argc, char** argv) {
    using cs::core::Parsed;
    using cs::core::Status;
    using cs::core::Verb;

    // 无参数 = 启动窗口（DESIGN §9.3）。exe 保持 CONSOLE 子系统（M5 追认决策）；
    // 控制台的隐藏只在 run_window 的交互分支里做，避免截图自检时把调用方的控制台也藏掉。
    // CLI 各动词路径完全不受影响，可验证性不降级（§9.2）。
    if (argc < 2) {
        return cs::ui::run_window();
    }

    const Parsed p = cs::core::parse_args(argc, const_cast<const char* const*>(argv));

    if (p.status == Status::Unknown) {
        std::fprintf(stderr, "error: unknown verb '%s'\n", std::string(p.unknown).c_str());
        print_usage();
        return 2;
    }
    if (p.status == Status::MissingOperand) {
        std::fprintf(stderr, "error: this verb needs a skin directory, e.g. --apply .\\skin\n");
        return 2;
    }

    const std::string_view dir = p.operand;
    switch (p.verb) {
    case Verb::Version:
        std::printf("cursorswitch V%s  (%s)\n", cs::core::kVersion, cs::core::kBuildFlags);
        return 0;
    case Verb::Apply:
        return cs::app::run_apply(dir);
    case Verb::Restore:
        return cs::app::run_restore();
    case Verb::Reset:
        return cs::app::run_reset();
    case Verb::LiveCheck:
        return cs::app::run_live_check(dir);
    case Verb::Help:
    case Verb::None:
        print_usage();
        return 0;
    }
    return 0;
}
