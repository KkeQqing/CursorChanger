// src/tests/tests_registry_probe.cpp — 只读探查真实的光标方案键，为 U1 取**佐证**
//
// 诚实边界（重要）：本探查只能回答「这台机器的注册表里现在有哪些值名」，
// **不能**证明「Windows 接受哪些值名」。写进去一个没人认的名字，注册表照样存得下，
// 只是不生效 —— 那正是 U1 担心的静默无效。所以：
//   · 本 probe 的结果记为「佐证」，不核销 U1；
//   · U1 的正式核销必须靠 L3（逐槽真 apply 后看生效帧），需要先取得用户授权。
// 全程只读：RegOpenKeyExW(KEY_READ) + RegEnumValueW，不写、不删、不广播。
#include "cases.h"

#include "apply/registry.h"
#include "core/slots.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace cs::core;
using cs::apply::QueryStatus;
using cs::apply::Root;
using cs::test::Runner;

namespace {

constexpr const wchar_t* kCursorsSubKey = L"Control Panel\\Cursors";
constexpr const wchar_t* kSchemesSubKey =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Control Panel\\Cursors\\Schemes";

// 打印值名与类型；**不打印路径值**（那是用户个人数据，且与判据无关）。
void dump_key(const char* label, Root root, const wchar_t* sub_key) {
    std::vector<cs::apply::ValueInfo> values;
    const cs::apply::QueryResult r = cs::apply::list_values(root, sub_key, values);
    const char* st = r.status == QueryStatus::Ok ? "Ok"
                 : r.status == QueryStatus::KeyNotFound ? "KeyNotFound"
                 : r.status == QueryStatus::AccessDenied ? "AccessDenied"
                 : "Unexpected";
    std::printf("REGPROBE %-26s status=%s values=%zu\n", label, st, values.size());
    for (const auto& v : values) {
        std::string narrow;
        narrow.reserve(v.name.size());
        for (wchar_t c : v.name) narrow.push_back(c < 0x80 ? static_cast<char>(c) : static_cast<char>('?'));
        std::printf("  name='%s' type=%u target_exists=%d\n", narrow.c_str(), v.type,
                    v.target_exists ? 1 : 0);
    }
    std::fflush(stdout);
}

}  // namespace

namespace cs::test {

void probe_registry_names(std::FILE* out) {
    (void)out;
    std::printf("== registry probe (KEY_READ only; nothing written) ==\n");

    dump_key("HKCU Control Panel\\Cursors", Root::CurrentUser, kCursorsSubKey);
    dump_key("HKU .DEFAULT Cursors", Root::UsersDefault, L".DEFAULT\\Control Panel\\Cursors");
    dump_key("HKLM Schemes", Root::LocalMachine, kSchemesSubKey);

    // 与 §5.2 槽表对照：报告「我们准备写的名字」有哪些**已被 OS 自己写过**（佐证），
    // 哪些本机注册表里没见过（不等于不接受，只是没有证据）。
    std::vector<cs::apply::ValueInfo> observed;
    cs::apply::list_values(Root::CurrentUser, kCursorsSubKey, observed);
    std::vector<cs::apply::ValueInfo> dflt;
    cs::apply::list_values(Root::UsersDefault, L".DEFAULT\\Control Panel\\Cursors", dflt);

    std::size_t n = 0;
    const SlotSpec* table = slot_table(n);
    std::size_t seen = 0;
    std::size_t unseen = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::wstring want(table[i].registry_name.begin(), table[i].registry_name.end());
        bool found = false;
        for (const auto& v : observed) { if (v.name == want) found = true; }
        for (const auto& v : dflt) { if (v.name == want) found = true; }
        std::string narrow;
        for (wchar_t c : want) narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        std::printf("SLOT %-12s observed_in_registry=%d\n", narrow.c_str(), found ? 1 : 0);
        if (found) ++seen; else ++unseen;
    }
    std::printf("SUMMARY slots=%zu seen=%zu unseen=%zu\n", n, seen, unseen);
    std::fflush(stdout);
}

void register_registry_cases(Runner&) {
    // 同 L1 的纪律：先看真值，再决定哪些能写成断言。U1 的正式判据在 L3，不在此处。
}

}  // namespace cs::test
