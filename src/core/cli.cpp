#include "core/cli.h"

#include <cctype>

namespace cs::core {
namespace {

constexpr std::size_t kMaxToken = 64;  // 超长输入直接判未知，不做截断比较（防前缀误命中）

bool fold_equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

std::string_view strip_dashes(std::string_view s) {
    if (s.size() >= 2 && s[0] == '-' && s[1] == '-') return s.substr(2);
    if (!s.empty() && s[0] == '-') return s.substr(1);
    return s;
}

}  // namespace

const VerbSpec* verb_table(std::size_t& count) {
    static constexpr VerbSpec kTable[] = {
        {{"help"}, {Verb::Help}, {{"print usage and milestone status"}}, {Arity::None}},
        {{"version"}, {Verb::Version}, {{"print version and build flags"}}, {Arity::None}},
        {{"apply"}, {Verb::Apply}, {{"apply a skin dir to the system pointers"}}, {Arity::NeedDir}},
        {{"restore"}, {Verb::Restore}, {{"restore the pre-apply baseline snapshot"}}, {Arity::None}},
        {{"reset"}, {Verb::Reset}, {{"clear our values and fall back to the OS default scheme"}}, {Arity::None}},
        {{"live-check"}, {Verb::LiveCheck},
         {{"apply -> L2/L3 verify -> restore, always restores"}}, {Arity::NeedDir}},
    };
    count = sizeof(kTable) / sizeof(kTable[0]);
    return kTable;
}

Arity verb_arity(Verb v) {
    std::size_t n = 0;
    const VerbSpec* t = verb_table(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (t[i].verb == v) return t[i].arity;
    }
    return Arity::None;
}

Parsed parse_args(int argc, const char* const* argv) {
    Parsed out;
    if (argc < 2 || argv == nullptr || argv[1] == nullptr) return out;

    const std::string_view raw(argv[1]);
    const std::string_view token = strip_dashes(raw);
    if (token.empty()) return out;  // 只有连字符 = 无动词
    if (token.size() > kMaxToken) {
        out.status = Status::Unknown;
        out.unknown = token.substr(0, kMaxToken);
        return out;
    }

    std::size_t n = 0;
    const VerbSpec* table = verb_table(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (!fold_equal(token, table[i].name)) continue;
        out.verb = table[i].verb;
        if (table[i].arity == Arity::NeedDir) {
            if (argc < 3 || argv[2] == nullptr || argv[2][0] == '\0') {
                out.status = Status::MissingOperand;
                return out;
            }
            out.operand = std::string_view(argv[2]);
        }
        return out;
    }
    out.status = Status::Unknown;
    out.unknown = token;
    return out;
}

}  // namespace cs::core
