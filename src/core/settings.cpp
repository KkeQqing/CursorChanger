// src/core/settings.cpp — 见头文件。纯解析，无 IO。
#include "core/settings.h"

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
bool parse_size_list(const std::string& v, std::vector<uint32_t>& out) {
    out.clear();
    std::string cur;
    for (std::size_t i = 0; i <= v.size(); ++i) {
        if (i == v.size() || v[i] == ',') {
            std::string tok = strip(cur);
            cur.clear();
            if (tok.empty()) return false;
            for (char c : tok) {
                if (std::isdigit(static_cast<unsigned char>(c)) == 0) return false;
            }
            unsigned long n = 0;
            for (char c : tok) n = n * 10u + static_cast<unsigned>(c - '0');
            if (n == 0 || n > kSettingsMaxDimension) return false;
            out.push_back(static_cast<uint32_t>(n));
        } else {
            cur.push_back(v[i]);
        }
    }
    return !out.empty();
}

}  // namespace

SettingsParse parse_settings(const std::string& text, Settings& out) {
    out = Settings{};
    SettingsParse r;
    bool any = false;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        const std::size_t end = (nl == std::string::npos) ? text.size() : nl;
        std::string line = text.substr(pos, end - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        const std::size_t semi = line.find(';');
        if (semi != std::string::npos) line = line.substr(0, semi);
        line = strip(line);
        if (line.empty()) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            r.status = SettingsStatus::BadSyntax;
            r.detail = "line without '=': " + line;
            return r;
        }
        const std::string key = lower(strip(line.substr(0, eq)));
        const std::string val = strip(line.substr(eq + 1));
        any = true;
        if (key == "frames.sizes") {
            if (!parse_size_list(val, out.frames_sizes)) {
                r.status = SettingsStatus::BadValue;
                r.detail = "bad frames.sizes (want comma 1..256): " + val;
                return r;
            }
            out.has_frames_sizes = true;
        } else {
            r.status = SettingsStatus::UnknownKey;
            r.detail = "unknown key: " + key;
            return r;
        }
    }
    if (!any) {
        r.status = SettingsStatus::Empty;
        return r;
    }
    r.status = SettingsStatus::Ok;
    return r;
}

}  // namespace cs::core
