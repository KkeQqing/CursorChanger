// src/apply/registry.h — HKCU 光标方案的注册表访问（DESIGN §2：只有 apply/ 允许碰注册表）
//
// 本文件目前只提供**只读**能力（枚举值名、读单值、键是否存在）。
// 写入/删除属于 M2 后续步骤，且真机写入必须先取得用户单独授权（DESIGN §8 末段）。
// 为了在没有授权时也能测出真实的键路径行为，所有接口都以「键路径」为参数，
// 测试可以用沙箱键（HKCU\Software\CursorSwitch-selftest）跑同一段代码。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cs::apply {

// 注册表根键的窄封装，避免把 LSTATUS/HKEY 泄漏到调用方。
enum class Root : uint8_t { CurrentUser, UsersDefault, LocalMachine };

struct ValueInfo {
    std::wstring name;      // 值名；默认值用空串表示
    uint32_t type{0};       // REG_*
    bool target_exists{false};  // 仅当值是文件路径时有意义：该文件是否存在
};

enum class QueryStatus : uint8_t {
    Ok,
    KeyNotFound,
    AccessDenied,
    Unexpected,
};

struct QueryResult {
    QueryStatus status{QueryStatus::Ok};
    uint32_t raw_code{0};   // 原始 LSTATUS，仅作可诊断信息，不作为判据
};

// 枚举一个键下的所有值名。只读，不改变任何状态。
QueryResult list_values(Root root, const wchar_t* sub_key, std::vector<ValueInfo>& out);

// 读单个 REG_SZ 值。found 为 false 表示**值不存在**（与「存在但为空」严格区分 ——
// §7.2 的三态基线就依赖这个区分）。
QueryResult read_sz(Root root, const wchar_t* sub_key, const wchar_t* value_name,
                   std::wstring& out, bool& found);

// 键本身是否存在（§7.2 的第三态：整个键不存在）。
QueryResult key_exists(Root root, const wchar_t* sub_key, bool& exists);

}  // namespace cs::apply
