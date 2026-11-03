// src/core/sidecar.cpp — 见头文件。纯字符串解析，无 IO、无 Win32。
#include "core/sidecar.h"

#include <cctype>

namespace cs::core {
namespace {

std::string strip(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// 把逗号分隔的非负整数列表解析进 out；任一段非法即 false。
bool parse_uint_list(const std::string& v, std::vector<uint32_t>& out) {
    out.clear();
    std::string cur;
    for (std::size_t i = 0; i <= v.size(); ++i) {
        if (i == v.size() || v[i] == ',') {
            std::string tok = strip(cur);
            cur.clear();
            if (tok.empty()) return false;             // 空段（如 "16,,32"）非法
            for (char c : tok) {
                if (std::isdigit(static_cast<unsigned char>(c)) == 0) return false;
            }
            unsigned long n = 0;
            for (char c : tok) n = n * 10u + static_cast<unsigned>(c - '0');
            if (n == 0 || n > kMaxCursorDimension) return false;  // 0 无意义，>256 越界
            out.push_back(static_cast<uint32_t>(n));
        } else {
            cur.push_back(v[i]);
        }
    }
    return !out.empty();
}

bool parse_pair(const std::string& v, Hotspot& h) {
    const std::size_t comma = v.find(',');
    if (comma == std::string::npos) return false;
    std::vector<uint32_t> two;
    if (!parse_uint_list(v, two) || two.size() != 2) return false;
    h.x = static_cast<uint16_t>(two[0]);
    h.y = static_cast<uint16_t>(two[1]);
    return true;
}

}  // namespace

SidecarParse parse_sidecar(const std::string& text, Sidecar& out) {
    out = Sidecar{};
    SidecarParse r;
    bool any = false;

    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        const std::size_t end = (nl == std::string::npos) ? text.size() : nl;
        std::string line = text.substr(pos, end - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

        const std::size_t semi = line.find(';');  // 行内注释
        if (semi != std::string::npos) line = line.substr(0, semi);
        line = strip(line);
        if (line.empty()) continue;

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            r.status = SidecarStatus::BadSyntax;
            r.detail = "line without '=': " + line;
            return r;
        }
        const std::string key = lower(strip(line.substr(0, eq)));
        const std::string val = strip(line.substr(eq + 1));
        any = true;

        if (key == "hotspot") {
            if (!parse_pair(val, out.hotspot)) {
                r.status = SidecarStatus::BadValue;
                r.detail = "bad hotspot (want x,y uint): " + val;
                return r;
            }
            out.has_hotspot = true;
        } else if (key == "size") {
            if (!parse_uint_list(val, out.sizes)) {
                r.status = SidecarStatus::BadValue;
                r.detail = "bad size list (want comma-separated 1..256): " + val;
                return r;
            }
            out.has_size = true;
        } else if (key == "trim") {
            const std::string t = lower(val);
            if (t != "none" && t != "auto") {
                r.status = SidecarStatus::BadValue;
                r.detail = "trim must be none|auto: " + val;
                return r;
            }
            out.trim = t;
            out.has_trim = true;
        } else {
            r.status = SidecarStatus::UnknownKey;
            r.detail = "unknown key: " + key;
            return r;
        }
    }

    if (!any) {
        r.status = SidecarStatus::Empty;
        return r;
    }
    r.status = SidecarStatus::Ok;
    return r;
}

}  // namespace cs::core
