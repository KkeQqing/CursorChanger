// src/app/main.cpp — 入口（M0：无窗口骨架，仅动词分发）
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
        // 动词名/说明用 string_view 承载，打印前物化成 std::string 保证 c_str() 有效。
        const std::string name(table[i].name);
        const std::string summary(table[i].summary);
        std::printf("  --%-8s %s\n", name.c_str(), summary.c_str());
    }
    std::printf("milestone: M0 skeleton. convert / apply / restore land in M1-M4.\n");
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

    switch (p.verb) {
    case Verb::Version:
        std::printf("cursorswitch V%s  (%s)\n", cs::core::kVersion, cs::core::kBuildFlags);
        return 0;
    case Verb::Help:
    case Verb::None:
        print_usage();
        return 0;
    }
    return 0;
}
