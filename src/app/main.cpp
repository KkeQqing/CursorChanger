// src/app/main.cpp — 入口：只负责分发，业务全在 apply/ 与 selfcheck/（§2 层规则）
#include "app/verbs.h"
#include "core/cli.h"
#include "core/version.h"

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
    std::printf("\nMilestones: .cur passthrough + apply/restore live now; PNG in M3, SVG in M4, window in M5.\n");
    std::printf("Note: --live-check restores even if a middle step fails (RAII guard).\n");
}

}  // namespace

int main(int argc, char** argv) {
    using cs::core::Parsed;
    using cs::core::Status;
    using cs::core::Verb;

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
