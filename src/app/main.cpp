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

// app 已切 WINDOWS 子系统（D18 V0.6.2）：从交互式终端直接带参运行时进程没有继承控制台，
// printf 会落空。这里**只在没有可用输出句柄时**才 AttachConsole 接回父控制台 —— 若 stdout
// 已被重定向成管道/文件（Job Object 无人值守捕获的形态），句柄本就有效，绝不去动它，
// 否则会打断这条已验证的输出捕获链。
void wire_cli_stdio_if_needed() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) return;  // 已继承/重定向，printf 有效
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::FILE* tmp = nullptr;
        freopen_s(&tmp, "CONOUT$", "w", stdout);
        freopen_s(&tmp, "CONOUT$", "w", stderr);
    }
}

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

    // 无参数 = 启动窗口（DESIGN §9.3）。app 是 WINDOWS 子系统（D18 V0.6.2）：双击/无参启动
    // 根本不会分配控制台，故无需再 ShowWindow(SW_HIDE) 去藏那个闪框 —— 闪框已从源头消失。
    // 带参数走 CLI 时，按需接回父控制台（见 wire_cli_stdio_if_needed），可验证性不降级（§9.2）。
    if (argc < 2) {
        return cs::ui::run_window();
    }
    wire_cli_stdio_if_needed();

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
