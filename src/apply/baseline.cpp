// src/apply/baseline.cpp — 三态基线：采集 / 落盘 / 载入 / 还原 / 复位
#include "apply/baseline.h"

#include "apply/registry.h"
#include "core/slots.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace cs::apply {
namespace {

constexpr const wchar_t* kSchemeValue = L"Scheme";
constexpr char kFileMagic = 'V';          // 行格式：见 save_baseline 的注释
constexpr const char* kDefaultToken = "@default";  // 默认值的名是空串，需要一个可写替身

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

// 非法 UTF-8 会返回空串；调用方用 src 非空来判定损坏。
std::wstring wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                      static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                        out.data(), n);
    return out;
}

bool split_fields(const std::string& line, std::vector<std::string>& out) {
    out.clear();
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            out.push_back(line.substr(start));
            return true;
        }
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

bool read_line(FILE* f, std::string& out) {
    char buf[4096];
    if (std::fgets(buf, static_cast<int>(sizeof(buf)), f) == nullptr) return false;
    std::size_t len = std::strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) --len;
    out.assign(buf, len);
    return true;
}

char code_of(SlotState s) { return s == SlotState::KeyAbsent ? 'K' : (s == SlotState::ValueAbsent ? 'A' : 'P'); }

bool state_of(char c, SlotState& out) {
    if (c == 'K') { out = SlotState::KeyAbsent; return true; }
    if (c == 'A') { out = SlotState::ValueAbsent; return true; }
    if (c == 'P') { out = SlotState::ValuePresent; return true; }
    return false;
}

bool has_break_char(const std::wstring& s) {
    return s.find(L'\t') != std::wstring::npos || s.find(L'\n') != std::wstring::npos ||
           s.find(L'\r') != std::wstring::npos;
}

void read_state(Root root, const std::wstring& sub_key, const wchar_t* name, std::wstring& value,
                SlotState& state, bool key_absent) {
    value.clear();
    if (key_absent) {
        state = SlotState::KeyAbsent;
        return;
    }
    bool found = false;
    const QueryResult r = read_sz(root, sub_key.c_str(), name, value, found);
    if (r.status != QueryStatus::Ok) {
        state = SlotState::ValueAbsent;  // 读失败当不存在：宁可少还原，不写没读到的东西
        value.clear();
        return;
    }
    state = found ? SlotState::ValuePresent : SlotState::ValueAbsent;
}

bool put_line(FILE* f, const std::string& line) {
    return std::fwrite(line.data(), 1, line.size(), f) == line.size();
}

std::wstring slot_name(std::size_t index) {
    std::size_t n = 0;
    const core::SlotSpec* table = core::slot_table(n);
    if (index >= n) return std::wstring();
    return std::wstring(table[index].registry_name.begin(), table[index].registry_name.end());
}

}  // namespace

Baseline capture_baseline(Root root, const wchar_t* sub_key) {
    Baseline b;
    b.root = root;
    b.sub_key = sub_key ? sub_key : L"";

    bool exists = false;
    const QueryResult k = key_exists(root, b.sub_key.c_str(), exists);
    b.key_absent_before = !(k.status == QueryStatus::Ok && exists);

    std::size_t n = 0;
    const core::SlotSpec* table = core::slot_table(n);
    b.slots.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        SlotSnapshot s;
        s.slot = table[i].id;
        const std::wstring name(table[i].registry_name.begin(), table[i].registry_name.end());
        read_state(root, b.sub_key, name.c_str(), s.value, s.state, b.key_absent_before);
        b.slots.push_back(s);
    }
    for (const wchar_t* extra : {static_cast<const wchar_t*>(L""), kSchemeValue}) {
        NamedSnapshot e;
        e.name = extra;
        read_state(root, b.sub_key, extra, e.value, e.state, b.key_absent_before);
        b.extras.push_back(e);
    }
    return b;
}

// 行格式（UTF-8，制表符分隔，每行一条）：
//   首行   V\t<k|a>            k = 采集时整个键不存在
//   槽位   S\t<值名>\t<K|A|P>\t<值>
//   额外   X\t<值名|@default>\t<K|A|P>\t<值>
bool save_baseline(const Baseline& b, const wchar_t* path, std::wstring& error) {
    for (const SlotSnapshot& s : b.slots) {
        if (has_break_char(s.value)) {
            error = L"value contains tab/newline; refusing to write a corrupt baseline";
            return false;
        }
    }
    for (const NamedSnapshot& e : b.extras) {
        if (has_break_char(e.name) || has_break_char(e.value)) {
            error = L"name or value contains tab/newline; refusing to write a corrupt baseline";
            return false;
        }
    }

    std::FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") != 0 || f == nullptr) {
        error = L"cannot open baseline file for writing";
        return false;
    }
    // 首行必须自带换行：漏掉它会让头部与第一条记录粘成一行，load 因字段数不符而拒收 ——
    // 后果不是报错，是「还原永远失败」，即用户 apply 之后退不回去。
    bool ok = put_line(f, std::string("V\t") + (b.key_absent_before ? "k" : "a") + "\n");
    std::size_t n = 0;
    core::slot_table(n);
    for (std::size_t i = 0; i < b.slots.size(); ++i) {
        if (i >= n) { ok = false; break; }
        const std::string line = "S\t" + narrow(slot_name(i)) + "\t" + code_of(b.slots[i].state) +
                                 "\t" + narrow(b.slots[i].value);
        ok = put_line(f, line + "\n") && ok;
    }
    for (const NamedSnapshot& e : b.extras) {
        const std::string name = e.name.empty() ? kDefaultToken : narrow(e.name);
        const std::string line = "X\t" + name + "\t" + code_of(e.state) + "\t" + narrow(e.value);
        ok = put_line(f, line + "\n") && ok;
    }
    std::fclose(f);
    if (!ok) error = L"short write while saving baseline";
    return ok;
}

bool load_baseline(Baseline& out, const wchar_t* path, std::wstring& error) {
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || f == nullptr) {
        error = L"cannot open baseline file";
        return false;
    }
    std::vector<std::string> fields;
    std::string line;
    if (!read_line(f, line) || !split_fields(line, fields) || fields.size() != 2 ||
        fields[0].size() != 1 || fields[0][0] != kFileMagic) {
        std::fclose(f);
        error = L"bad baseline header";
        return false;
    }
    out = Baseline{};
    out.key_absent_before = (fields[1] == "k");
    out.sub_key = L"Control Panel\\Cursors";  // 由 apply 侧在使用前覆盖成实际键
    out.root = Root::CurrentUser;

    std::size_t n = 0;
    core::slot_table(n);
    bool ok = true;
    while (read_line(f, line)) {
        if (!split_fields(line, fields) || fields.size() != 4) { ok = false; break; }
        SlotState st = SlotState::ValueAbsent;
        if (!state_of(fields[2].size() == 1 ? fields[2][0] : '?', st)) { ok = false; break; }
        const std::wstring value = wide(fields[3]);
        if (!fields[3].empty() && value.empty()) { ok = false; break; }  // 非法 UTF-8

        if (fields[0] == "S") {
            const std::wstring name = wide(fields[1]);
            std::size_t idx = n;
            for (std::size_t i = 0; i < n; ++i) {
                if (slot_name(i) == name) { idx = i; break; }
            }
            if (idx >= n) { ok = false; break; }  // 表里没有的名字 = 文件与当前槽表不符
            out.slots.push_back(SlotSnapshot{static_cast<core::SlotId>(idx), st, value});
        } else if (fields[0] == "X") {
            NamedSnapshot e;
            e.name = fields[1] == kDefaultToken ? std::wstring() : wide(fields[1]);
            e.state = st;
            e.value = value;
            out.extras.push_back(e);
        } else {
            ok = false;
            break;
        }
    }
    std::fclose(f);

    if (!ok) {
        error = L"corrupt baseline record";
        return false;
    }
    // 数量核对：漏记录会让「还原」变成只还原一半，所以必须当场拒。
    if (out.slots.size() != n || out.extras.size() != 2) {
        error = L"baseline record count mismatch";
        return false;
    }
    return true;
}

RestoreResult restore_baseline(const Baseline& b) {
    RestoreResult r;
    std::size_t n = 0;
    core::slot_table(n);
    for (std::size_t i = 0; i < b.slots.size() && i < n; ++i) {
        const SlotSnapshot& s = b.slots[i];
        const std::wstring name = slot_name(i);
        if (s.state == SlotState::KeyAbsent) {
            ++r.skipped;  // 键本来不存在：不创建、不写
            continue;
        }
        const WriteStatus w = s.state == SlotState::ValuePresent
                                  ? write_sz(b.root, b.sub_key.c_str(), name.c_str(), s.value.c_str())
                                  : delete_value(b.root, b.sub_key.c_str(), name.c_str());
        if (w == WriteStatus::Ok) ++r.restored; else ++r.failed;
    }
    for (const NamedSnapshot& e : b.extras) {
        if (e.state == SlotState::KeyAbsent) {
            ++r.skipped;
            continue;
        }
        const WriteStatus w = e.state == SlotState::ValuePresent
                                  ? write_sz(b.root, b.sub_key.c_str(), e.name.c_str(), e.value.c_str())
                                  : delete_value(b.root, b.sub_key.c_str(), e.name.c_str());
        if (w == WriteStatus::Ok) ++r.restored; else ++r.failed;
    }
    return r;
}

RestoreResult reset_to_default_scheme(Root root, const wchar_t* sub_key) {
    RestoreResult r;
    std::size_t n = 0;
    core::slot_table(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::wstring name = slot_name(i);
        if (delete_value(root, sub_key, name.c_str()) == WriteStatus::Ok) ++r.restored;
        else ++r.failed;
    }
    if (delete_value(root, sub_key, kSchemeValue) == WriteStatus::Ok) ++r.restored;
    else ++r.failed;
    if (delete_value(root, sub_key, L"") == WriteStatus::Ok) ++r.restored;
    else ++r.failed;
    return r;
}

}  // namespace cs::apply
