#include "apply/registry.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>

namespace cs::apply {
namespace {

HKEY to_hkey(Root root) {
    switch (root) {
    case Root::CurrentUser: return HKEY_CURRENT_USER;
    case Root::UsersDefault: return HKEY_USERS;
    case Root::LocalMachine: return HKEY_LOCAL_MACHINE;
    }
    return HKEY_CURRENT_USER;
}

QueryStatus classify(LSTATUS s) {
    if (s == ERROR_SUCCESS) return QueryStatus::Ok;
    if (s == ERROR_FILE_NOT_FOUND) return QueryStatus::KeyNotFound;
    if (s == ERROR_ACCESS_DENIED) return QueryStatus::AccessDenied;
    return QueryStatus::Unexpected;
}

bool path_resolves_to_file(const std::wstring& raw) {
    if (raw.empty()) return false;
    std::wstring expanded = raw;
    if (raw.find(L'%') != std::wstring::npos) {
        wchar_t buf[4096];
        const DWORD n = ExpandEnvironmentStringsW(raw.c_str(), buf, 4096);
        if (n == 0 || n > 4096) return false;
        expanded.assign(buf, n - 1);  // n 含结尾 NUL
    }
    const DWORD attr = GetFileAttributesW(expanded.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

QueryResult list_values(Root root, const wchar_t* sub_key, std::vector<ValueInfo>& out) {
    out.clear();
    if (sub_key == nullptr) return QueryResult{QueryStatus::Unexpected, (uint32_t)ERROR_INVALID_PARAMETER};

    HKEY h = nullptr;
    LSTATUS s = RegOpenKeyExW(to_hkey(root), sub_key, 0, KEY_READ, &h);
    if (s != ERROR_SUCCESS) return QueryResult{classify(s), static_cast<uint32_t>(s)};

    QueryResult res{QueryStatus::Ok, 0};
    wchar_t name[512];
    for (DWORD idx = 0;; ++idx) {
        DWORD name_len = 512;
        DWORD type = 0;
        s = RegEnumValueW(h, idx, name, &name_len, nullptr, &type, nullptr, nullptr);
        if (s == ERROR_NO_MORE_ITEMS) break;
        if (s != ERROR_SUCCESS) {
            res = QueryResult{classify(s), static_cast<uint32_t>(s)};
            break;
        }
        ValueInfo vi;
        vi.name.assign(name, name_len);  // name_len 不含结尾 NUL
        vi.type = type;
        if (type == REG_SZ || type == REG_EXPAND_SZ) {
            std::wstring data;
            bool found = false;
            const QueryStatus rs = read_sz(root, sub_key, vi.name.c_str(), data, found).status;
            if (rs == QueryStatus::Ok && found) vi.target_exists = path_resolves_to_file(data);
        }
        out.push_back(vi);
    }
    RegCloseKey(h);
    return res;
}

QueryResult read_sz(Root root, const wchar_t* sub_key, const wchar_t* value_name,
                    std::wstring& out, bool& found) {
    out.clear();
    found = false;
    if (sub_key == nullptr || value_name == nullptr) {
        return QueryResult{QueryStatus::Unexpected, static_cast<uint32_t>(ERROR_INVALID_PARAMETER)};
    }

    HKEY h = nullptr;
    LSTATUS s = RegOpenKeyExW(to_hkey(root), sub_key, 0, KEY_READ, &h);
    if (s != ERROR_SUCCESS) return QueryResult{classify(s), static_cast<uint32_t>(s)};

    DWORD type = 0;
    DWORD bytes = 0;
    s = RegQueryValueExW(h, value_name, nullptr, &type, nullptr, &bytes);
    if (s == ERROR_FILE_NOT_FOUND) {
        // 值不存在 —— 与「存在但为空串」必须区分，§7.2 的三态基线依赖这里
        RegCloseKey(h);
        return QueryResult{QueryStatus::Ok, static_cast<uint32_t>(ERROR_SUCCESS)};
    }
    if (s != ERROR_SUCCESS) {
        const QueryResult r{classify(s), static_cast<uint32_t>(s)};
        RegCloseKey(h);
        return r;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        RegCloseKey(h);
        // WIN32_LEAN_AND_MEAN 下 ERROR_WIN32_REGISTRY_VALUE 不可见，
        // 用必然存在的 ERROR_INVALID_DATA 表达「值类型不符」
        return QueryResult{QueryStatus::Unexpected, static_cast<uint32_t>(ERROR_INVALID_DATA)};
    }

    std::wstring buf;
    buf.resize((bytes / sizeof(wchar_t)) + 1);
    bytes = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
    s = RegQueryValueExW(h, value_name, nullptr, &type, reinterpret_cast<BYTE*>(buf.data()), &bytes);
    RegCloseKey(h);
    if (s != ERROR_SUCCESS) return QueryResult{classify(s), static_cast<uint32_t>(s)};

    const std::size_t chars = (bytes % sizeof(wchar_t) == 0) ? bytes / sizeof(wchar_t) : 0;
    if (chars == 0) {
        found = true;   // 存在但空串：仍然是「存在」
        return QueryResult{QueryStatus::Ok, 0};
    }
    std::size_t use = chars;
    if (buf[use - 1] == L'\0') --use;  // 去掉可能的结尾 NUL
    out.assign(buf.data(), use);
    found = true;
    return QueryResult{QueryStatus::Ok, 0};
}

QueryResult key_exists(Root root, const wchar_t* sub_key, bool& exists) {
    exists = false;
    if (sub_key == nullptr) return QueryResult{QueryStatus::Unexpected, static_cast<uint32_t>(ERROR_INVALID_PARAMETER)};
    HKEY h = nullptr;
    const LSTATUS s = RegOpenKeyExW(to_hkey(root), sub_key, 0, KEY_READ, &h);
    if (s == ERROR_FILE_NOT_FOUND) return QueryResult{QueryStatus::Ok, 0};  // exists=false
    if (s != ERROR_SUCCESS) return QueryResult{classify(s), static_cast<uint32_t>(s)};
    RegCloseKey(h);
    exists = true;
    return QueryResult{QueryStatus::Ok, 0};
}

}  // namespace cs::apply
