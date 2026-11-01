#include "core/svg_guard.h"

#include <cctype>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace cs::core {
namespace {

// nanosvg 确认支持的结构。加条目前必须先实测它真的画得出来，不能凭"SVG 标准里有"放行。
const std::set<std::string>& allow_elements() {
    static const std::set<std::string> k = {
        "svg", "g", "path", "rect", "circle", "ellipse", "line", "polyline", "polygon",
        "defs", "title", "desc", "metadata", "stop", "lineargradient", "radialgradient",
    };
    return k;
}

// 元素名黑名单：出现即拒（nanosvg 会静默忽略，导致内容凭空消失）
const std::set<std::string>& block_elements() {
    static const std::set<std::string> k = {
        "text",     "tspan",        "filter",   "mask",      "clippath",  "image",
        "use",      "style",        "switch",   "pattern",   "font",      "foreignobject",
        "animate",  "animatetransform", "animatecolor", "set", "discard",
        "fegaussianblur", "feblend", "fecomposite", "feflood", "feoffset",
    };
    return k;
}

// 属性黑名单：值交给 nanosvg 处理不了的机制
const std::set<std::string>& block_attrs() {
    static const std::set<std::string> k = {
        "style", "class", "filter", "mask", "clip-path", "href", "xlink:href",
        "requiredfeatures", "requiredextensions", "systemlanguage", "onload",
        "externalresourcesrequired",
    };
    return k;
}

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool is_name_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool is_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' || c == '.' ||
           c == ':';
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ns:rect -> rect
std::string local_of(const std::string& q) {
    const std::string::size_type p = q.find_last_of(':');
    return p == std::string::npos ? q : q.substr(p + 1);
}

struct Attr {
    std::string name;   // 原始名（含命名空间前缀）
    std::string value;
};

struct Tag {
    std::string raw_name;   // 原始元素名
    std::string local;      // local-name，小写
    std::vector<Attr> attrs;
    bool self_closing{false};
};

// 从 svg[i]=='<' 处解析一个开始标签；失败返回 false。i 前进到标签结尾（'>' 之后）。
bool parse_tag(const std::string& s, std::string::size_type& i, Tag& out) {
    std::string::size_type p = i + 1;
    if (p >= s.size() || !is_name_start(s[p])) return false;
    const std::string::size_type ns = p;
    while (p < s.size() && is_name_char(s[p])) ++p;
    out.raw_name = s.substr(ns, p - ns);
    out.local = lower(local_of(out.raw_name));

    while (p < s.size()) {
        while (p < s.size() && is_ws(s[p])) ++p;
        if (p >= s.size()) return false;
        if (s[p] == '>') {
            ++p;
            i = p;
            return true;
        }
        if (s[p] == '/' && p + 1 < s.size() && s[p + 1] == '>') {
            out.self_closing = true;
            p += 2;
            i = p;
            return true;
        }
        if (!is_name_start(s[p])) {
            ++p;  // 不认识的东西跳过，属性解析失败不静默当作「没有属性」
            continue;
        }
        const std::string::size_type as = p;
        while (p < s.size() && is_name_char(s[p])) ++p;
        std::string aname = s.substr(as, p - as);
        while (p < s.size() && is_ws(s[p])) ++p;
        std::string aval;
        if (p < s.size() && s[p] == '=') {
            ++p;
            while (p < s.size() && is_ws(s[p])) ++p;
            if (p < s.size() && (s[p] == '"' || s[p] == '\'')) {
                const char q = s[p++];
                const std::string::size_type vs = p;
                while (p < s.size() && s[p] != q) ++p;
                aval = s.substr(vs, p - vs);
                if (p < s.size()) ++p;
            } else {
                const std::string::size_type vs = p;
                while (p < s.size() && !is_ws(s[p]) && s[p] != '>' && s[p] != '/') ++p;
                aval = s.substr(vs, p - vs);
            }
        }
        out.attrs.push_back(Attr{aname, aval});
    }
    return false;  // 未闭合：当作解析失败，而不是「属性都没了」
}

bool has_numeric_len(const std::string& v) {
    // 允许 "24"、"24px"、"2.5"，拒绝空串与纯单位
    std::string::size_type k = 0;
    while (k < v.size() && (std::isdigit(static_cast<unsigned char>(v[k])) || v[k] == '.')) ++k;
    return k > 0;
}

// 抽出 url(#id) 里的 id
std::string url_target(const std::string& v) {
    const std::string::size_type a = v.find("url(");
    if (a == std::string::npos) return std::string();
    const std::string::size_type b = v.find('#', a);
    if (b == std::string::npos) return std::string("<external>");  // url(http://...) 一律视为外部
    const std::string::size_type e = v.find_first_of(")>", b);
    return v.substr(b + 1, (e == std::string::npos ? v.size() : e) - (b + 1));
}

void add(GuardResult& r, const std::string& token, const std::string& why) {
    for (const GuardViolation& v : r.violations) {
        if (v.token == token && v.reason == why) return;  // 去重，报告不刷屏
    }
    r.ok = false;
    r.violations.push_back(GuardViolation{token, why});
}

}  // namespace

const std::set<std::string>& svg_guard_allow_elements() { return allow_elements(); }
const std::set<std::string>& svg_guard_block_elements() { return block_elements(); }

GuardResult svg_guard_scan(const std::string& svg) {
    GuardResult r;
    if (svg.empty()) {
        r.ok = false;
        r.detail = "empty svg";
        r.violations.push_back(GuardViolation{"(empty)", "no content to render"});
        return r;
    }

    // 第一遍：收集文档内定义的渐变 id（url(#id) 只允许指向它们）
    std::set<std::string> gradient_ids;
    {
        std::string::size_type i = 0;
        while ((i = svg.find('<', i)) != std::string::npos) {
            const char nx = i + 1 < svg.size() ? svg[i + 1] : '\0';
            if (nx == '!' || nx == '?' || nx == '/') {
                i += 1;
                continue;
            }
            std::string::size_type j = i;
            Tag t;
            if (!parse_tag(svg, j, t)) {
                i = j > i ? j : i + 1;
                continue;
            }
            i = j;
            if (t.local != "lineargradient" && t.local != "radialgradient") continue;
            for (const Attr& a : t.attrs) {
                if (lower(a.name) == "id" && !a.value.empty()) gradient_ids.insert(a.value);
            }
        }
    }

    // 第二遍：正式扫描
    bool saw_root = false;
    bool root_ok_dims = false;
    std::string::size_type i = 0;
    while ((i = svg.find('<', i)) != std::string::npos) {
        const char nx = i + 1 < svg.size() ? svg[i + 1] : '\0';
        if (nx == '!' || nx == '?' || nx == '/') {
            i += 1;
            continue;
        }
        std::string::size_type j = i;
        Tag t;
        if (!parse_tag(svg, j, t)) {
            add(r, "(malformed)", "unterminated tag; cannot prove content is renderable");
            i = j > i ? j : i + 1;
            continue;
        }
        i = j;

        if (block_elements().count(t.local)) {
            add(r, t.raw_name, "unsupported element: nanosvg silently drops it");
            continue;
        }
        if (!allow_elements().count(t.local)) {
            add(r, t.raw_name, "element not in allow-list (support unproven)");
            continue;
        }

        std::string id_width, id_height, id_viewbox;
        for (const Attr& a : t.attrs) {
            const std::string an = lower(a.name);
            if (block_attrs().count(a.name) || block_attrs().count(an)) {
                add(r, an, "unsupported attribute: content it introduces is not rendered");
            }
            if (an == "width") id_width = a.value;
            if (an == "height") id_height = a.value;
            if (an == "viewbox") id_viewbox = a.value;
            if (a.value.find("url(") != std::string::npos) {
                const std::string target = url_target(a.value);
                if (target.empty()) {
                    // 形如 url() 之类，交给 nanosvg 也不会画东西
                    add(r, an, "url() reference is empty or unparsable");
                } else if (!gradient_ids.count(target)) {
                    // 渐变之外的引用（filter/pattern/mask/未定义 id）都会让内容凭空消失
                    add(r, "url(#" + target + ")",
                        "reference is not a gradient defined in this document");
                }
            }
        }

        if (t.local == "svg" && !saw_root) {
            saw_root = true;
            const bool has_box = !id_viewbox.empty();
            const bool has_wh = has_numeric_len(id_width) && has_numeric_len(id_height);
            root_ok_dims = has_box || has_wh;
            if (!root_ok_dims) {
                add(r, "(root)", "no viewBox and no width/height: target size is undecidable");
            }
        }
    }

    if (!saw_root) add(r, "(root)", "no <svg> root element");

    r.detail = r.ok ? "ok"
                    : ("guard rejected: " + std::to_string(r.violations.size()) + " violation(s)");
    return r;
}

}  // namespace cs::core
