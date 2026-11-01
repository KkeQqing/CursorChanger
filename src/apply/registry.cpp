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
QueryResult read_dword(Root root, const wchar_t* sub_key, const wchar_t* value_name,
                       uint32_t& out, bool& found) {
    out = 0;
    found = false;
    if (sub_key == nullptr || value_name == nullptr) {
        return QueryResult{QueryStatus::Unexpected, static_cast<uint32_t>(ERROR_INVALID_PARAMETER)};
    }
    HKEY h = nullptr;
    LSTATUS s = RegOpenKeyExW(to_hkey(root), sub_key, 0, KEY_READ, &h);
    if (s != ERROR_SUCCESS) return QueryResult{classify(s), static_cast<uint32_t>(s)};

    DWORD type = 0;
    DWORD bytes = sizeof(out);
    s = RegQueryValueExW(h, value_name, nullptr, &type, reinterpret_cast<BYTE*>(&out), &bytes);
    RegCloseKey(h);
    if (s == ERROR_FILE_NOT_FOUND) return QueryResult{QueryStatus::Ok, 0};  // found=false
    if (s != ERROR_SUCCESS) return QueryResult{classify(s), static_cast<uint32_t>(s)};
    if (type != REG_DWORD || bytes != sizeof(out)) {
        return QueryResult{QueryStatus::Unexpected, static_cast<uint32_t>(ERROR_INVALID_DATA)};
    }
    found = true;
    return QueryResult{QueryStatus::Ok, 0};
}

WriteStatus write_sz(Root root, const wchar_t* sub_key, const wchar_t* value_name,
                     const wchar_t* data) {
    if (sub_key == nullptr || value_name == nullptr || data == nullptr) {
        return WriteStatus::Unexpected;
    }
    HKEY h = nullptr;
    // 不自动建键：路径写错时静默创建一个新键，比报错更难发现（§7.1 的失败模式偏好）。
    const LSTATUS o = RegOpenKeyExW(to_hkey(root), sub_key, 0, KEY_SET_VALUE, &h);
    if (o == ERROR_FILE_NOT_FOUND) return WriteStatus::KeyNotFound;
    if (o == ERROR_ACCESS_DENIED) return WriteStatus::AccessDenied;
    if (o != ERROR_SUCCESS) return WriteStatus::Unexpected;

    const DWORD bytes = static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t));
    const LSTATUS s = RegSetValueExW(h, value_name, 0, REG_SZ,
                                     reinterpret_cast<const BYTE*>(data), bytes);
    RegCloseKey(h);
    return s == ERROR_SUCCESS ? WriteStatus::Ok : WriteStatus::Unexpected;
}

WriteStatus delete_value(Root root, const wchar_t* sub_key, const wchar_t* value_name) {
    if (sub_key == nullptr || value_name == nullptr) return WriteStatus::Unexpected;
    HKEY h = nullptr;
    const LSTATUS o = RegOpenKeyExW(to_hkey(root), sub_key, 0, KEY_SET_VALUE, &h);
    if (o == ERROR_FILE_NOT_FOUND) return WriteStatus::Ok;  // 键都没有 = 没有可删的，幂等
    if (o != ERROR_SUCCESS) return WriteStatus::Unexpected;

    const LSTATUS s = RegDeleteValueW(h, value_name);
    RegCloseKey(h);
    if (s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND) return WriteStatus::Ok;  // 值不存在也算删成
    return WriteStatus::Unexpected;
}

WriteStatus ensure_key(Root root, const wchar_t* sub_key) {
    if (sub_key == nullptr) return WriteStatus::Unexpected;
    HKEY h = nullptr;
    const LSTATUS s = RegCreateKeyExW(to_hkey(root), sub_key, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                      KEY_WRITE, nullptr, &h, nullptr);
    if (s != ERROR_SUCCESS) return WriteStatus::Unexpected;
    RegCloseKey(h);
    return WriteStatus::Ok;
}

WriteStatus delete_empty_key(Root root, const wchar_t* sub_key) {
    if (sub_key == nullptr) return WriteStatus::Unexpected;
    // 先数一下有没有值。有值就拒绝 —— 这个接口存在的唯一理由是安全：
    // 误把真机键传进来时，它必须什么都不做，而不是把用户的光标方案清空。
    HKEY h = nullptr;
    const LSTATUS o = RegOpenKeyExW(to_hkey(root), sub_key, 0, KEY_READ | KEY_WRITE, &h);
    if (o == ERROR_FILE_NOT_FOUND) return WriteStatus::Ok;  // 本来就没有，视为已删
    if (o != ERROR_SUCCESS) return WriteStatus::Unexpected;

    DWORD count = 0;
    const LSTATUS q = RegQueryInfoKeyW(h, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                       &count, nullptr, nullptr, nullptr, nullptr);
    if (q != ERROR_SUCCESS) {
        RegCloseKey(h);
        return WriteStatus::Unexpected;
    }
    if (count > 0) {  // 有值：拒绝，绝不递归删
        RegCloseKey(h);
        return WriteStatus::Unexpected;
    }
    RegCloseKey(h);
    return RegDeleteKeyW(to_hkey(root), sub_key) == ERROR_SUCCESS ? WriteStatus::Ok
                                                                   : WriteStatus::Unexpected;
}

}  // namespace cs::apply
