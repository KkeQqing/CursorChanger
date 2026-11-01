// src/tests/tests_m4.cpp — M4 SVG 通道：探针、特性守卫、贴边、端到端
#include "cases.h"

#include "core/cur_writer.h"
#include "core/ir.h"
#include "core/slots.h"
#include "core/svg_guard.h"
#include "core/svg_source.h"
#include "selfcheck/l1.h"
#include "testfs.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace cs::core;
using cs::test::Runner;

namespace {

CursorSpec mk_spec(const std::string& svg, SlotId slot, SvgReport& rep, SvgStatus& st,
                   std::string& detail) {
    SvgOptions o;
    o.work_resolution = 128;   // 测试用小分辨率，省时间
    o.sizes = {4};
    CursorSpec s;
    st = load_svg_to_spec(reinterpret_cast<const uint8_t*>(svg.data()), svg.size(), slot, o, s, rep,
                          detail);
    return s;
}

bool has_token(const GuardResult& g, const std::string& token) {
    for (const GuardViolation& v : g.violations) {
        if (v.token == token) return true;
    }
    return false;
}

// 50% 不透明灰圆，覆盖画布中心
const char* kHalfGreyCircle =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
    "<circle cx=\"50\" cy=\"50\" r=\"45\" fill=\"#808080\" fill-opacity=\"0.5\"/></svg>";

const char* kPlainPath =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\">"
    "<path d=\"M2 2 L30 2 L30 30 Z\" fill=\"#ffffff\"/></svg>";

const char* kGradient =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\">"
    "<defs><linearGradient id=\"g\"><stop offset=\"0\" stop-color=\"#fff\"/>"
    "<stop offset=\"1\" stop-color=\"#000\"/></linearGradient></defs>"
    "<rect width=\"32\" height=\"32\" fill=\"url(#g)\"/></svg>";

}  // namespace

namespace cs::test {

void register_svg_cases(Runner& runner) {
    // ---------- 前提探针：nanosvg 的输出是不是已预乘 ----------
    runner.add("svg_probe_shows_straight_alpha_not_premultiplied", [](Runner& runner) {
        // 两种预测：
        //   straight  -> 中心 (R,G,B) ~= 128 且 A ~= 128
        //   已预乘    -> (R,G,B) ~= 64  （128*0.5）
        // 本用例若变红，说明 nanosvg 其实已预乘，**必须删掉 load_svg_to_spec 里的那次乘法**，
        // 而不是改期望值让它变绿。双重预乘不会被「每通道 <= alpha」抓到。
        uint8_t raw[4] = {0, 0, 0, 0};
        std::string detail;
        const bool ok = svg_probe_raw_pixel(
            reinterpret_cast<const uint8_t*>(kHalfGreyCircle), std::string(kHalfGreyCircle).size(),
            128, 64, 64, raw, detail);
        std::printf("PROBE nanosvg raw center RGBA = (%u,%u,%u,%u) detail=%s\n", raw[0], raw[1],
                    raw[2], raw[3], detail.c_str());
        CHECK(ok);
        CHECK(raw[3] > 100 && raw[3] < 160);           // alpha ~= 128
        CHECK(raw[0] > 100 && raw[0] < 160);           // 直 alpha：R ~= 128（预乘则 ~64）
        CHECK(raw[0] == raw[1] && raw[1] == raw[2]);   // 灰色
    });

    // ---------- 特性守卫：每条都必须拒 ----------
    runner.add("svg_guard_rejects_silently_dropped_constructs", [](Runner& runner) {
        struct Bad {
            const char* svg;
            const char* token;
        };
        const Bad bad[] = {
            {"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><text x=\"1\" "
             "y=\"1\">hi</text></svg>",
             "text"},
            {"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><filter id=\"f\"/"
             "></svg>",
             "filter"},
            {"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><image "
             "href=\"a.png\"/></svg>",
             "image"},
            {"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><use "
             "href=\"#x\"/></svg>",
             "use"},
            {"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><style>p{}</style>"
             "</svg>",
             "style"},
            {"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><rect "
             "width=\"8\" height=\"8\" clip-path=\"url(#c)\"/></svg>",
             "clip-path"},
            {"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><rect "
             "width=\"8\" height=\"8\" style=\"fill:red\"/></svg>",
             "style"},
            // 引用了一个文档里不存在的 id：会静默什么都不画
            {"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><rect width=\"8\" "
             "height=\"8\" fill=\"url(#nope)\"/></svg>",
             "url(#nope)"},
            // 未知元素：默认拒绝（宁可误拒，也不放过被静默丢弃的内容）
            {"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><weirdshape "
             "n=\"1\"/></svg>",
             "weirdshape"},
            // 无 viewBox 且无 width/height：目标尺寸无从决定
            {"<svg xmlns=\"http://www.w3.org/2000/svg\"><rect width=\"8\" height=\"8\"/></svg>",
             "(root)"},
        };
        std::size_t rejected = 0;
        for (const Bad& b : bad) {
            const GuardResult g = svg_guard_scan(b.svg);
            if (!g.ok && has_token(g, b.token)) ++rejected;
            else std::printf("GUARD MISS token=%s ok=%d n=%zu\n", b.token, g.ok ? 1 : 0,
                             g.violations.size());
        }
        CHECK(rejected == sizeof(bad) / sizeof(bad[0]));  // 一条都不能漏放

        // 反向对照：同一批图去掉问题构造后必须放行 —— 否则「全拒」也能骗过上面那条
        CHECK(svg_guard_scan(kPlainPath).ok);
        CHECK(svg_guard_scan(kHalfGreyCircle).ok);
    });

    runner.add("svg_guard_tables_are_disjoint_and_unknown_is_denied", [](Runner& runner) {
        // 动机：一条违规制造实验发现，从黑名单摘掉 text 后测试**没变红** ——
        // 因为 allow-list 的「未知即拒」补上了那一层。「被拒」不等于「被哪一层拒」，
        // 所以这里把两表的关系本身钉住。
        const std::set<std::string>& allow = svg_guard_allow_elements();
        const std::set<std::string>& block = svg_guard_block_elements();
        CHECK(!allow.empty());
        CHECK(!block.empty());

        std::size_t overlap = 0;
        for (const std::string& e : block) {
            if (allow.count(e)) ++overlap;  // 既放行又拉黑 = 行为取决于检查顺序
        }
        CHECK(overlap == 0);

        // 未知元素必须被拒（默认拒绝是可观察行为，不是内部实现细节）
        const GuardResult g = svg_guard_scan(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 8 8\"><mystery x=\"1\"/></svg>");
        CHECK(!g.ok);
        CHECK(has_token(g, "mystery"));

        // 黑名单里每一个元素都要逐个点名拒掉，不靠「至少一条违规」蒙过去
        std::size_t denied = 0;
        for (const std::string& e : block) {
            const std::string svg =
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 8 8\"><" + e + "/></svg>";
            const GuardResult r = svg_guard_scan(svg);
            if (!r.ok && has_token(r, e)) ++denied;
        }
        std::printf("GUARD block=%zu denied_by_name=%zu\n", block.size(), denied);
        CHECK(denied == block.size());
    });

    runner.add("svg_guard_allows_in_document_gradients", [](Runner& runner) {
        // 这条专门盯一个真实风险：把 url(#) 一律拒掉会误杀渐变填充，
        // 而渐变在光标 SVG 里极常见（本轮实现初版就犯过这个错）。
        const GuardResult g = svg_guard_scan(kGradient);
        for (const GuardViolation& v : g.violations) {
            std::printf("GRADIENT REJECTED token=%s why=%s\n", v.token.c_str(), v.reason.c_str());
        }
        CHECK(g.ok);

        // 且渐变确实被画出来了：非退化内容 + 多种颜色
        SvgReport rep;
        SvgStatus st = SvgStatus::Ok;
        std::string detail;
        const CursorSpec s = mk_spec(kGradient, SlotId::Arrow, rep, st, detail);
        CHECK(st == SvgStatus::Ok);
        CHECK(s.frames.size() == 1);
        if (s.frames.empty()) return;
        // 渐变 -> 同一行内不该全是同一个灰度值
        const Frame& f = s.frames[0];
        std::size_t distinct = 0;
        for (std::size_t i = 1; i < f.pixels.size(); i += 4) {
            if (f.pixels[i] != f.pixels[i - 4]) ++distinct;
        }
        CHECK(distinct > 0);
    });

    // ---------- 贴边与出血启发式 ----------
    runner.add("svg_trim_reports_content_ratio", [](Runner& runner) {
        // 小图形塞在大画布角落：ratio 应该小，且触发告警（不静默交付一个偏掉的指针）
        const char* bleed =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
            "<rect x=\"2\" y=\"2\" width=\"6\" height=\"6\" fill=\"#fff\"/></svg>";
        SvgReport rep;
        SvgStatus st = SvgStatus::Ok;
        std::string detail;
        const CursorSpec s = mk_spec(bleed, SlotId::Arrow, rep, st, detail);
        CHECK(st == SvgStatus::Ok);
        CHECK(rep.raster_w == 128 && rep.raster_h == 128);
        CHECK(rep.trimmed_w < rep.raster_w);   // 真的贴过边
        CHECK(rep.content_ratio > 0.0);
        CHECK(rep.content_ratio < 0.05);
        CHECK(rep.low_ratio_warning);

        // 反向对照：铺满画布的图形不该告警。
        // 这里最初用 kPlainPath（一个三角形）配 ratio > 0.9 的断言 —— 期望错而不是实现错：
        // 三角形只覆盖外接盒的一半不到，实测 0.766。改用满画布矩形来验「不告警」这一侧，
        // 并另存实测值作注释，避免下一个人又把三角形当铺满用例。
        const char* flush =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\">"
            "<rect width=\"32\" height=\"32\" fill=\"#ffffff\"/></svg>";
        SvgReport full;
        const CursorSpec s2 = mk_spec(flush, SlotId::Arrow, full, st, detail);
        CHECK(st == SvgStatus::Ok);
        CHECK(full.content_ratio > 0.98);
        CHECK(!full.low_ratio_warning);
        CHECK(!s2.frames.empty());

        // 三角形这类「正常但不满」的图形：贴边后 112/128 -> ratio 0.766，高于阈值故不告警。
        SvgReport tri;
        mk_spec(kPlainPath, SlotId::Arrow, tri, st, detail);
        CHECK(st == SvgStatus::Ok);
        std::printf("TRIM triangle ratio=%.3f warn=%d\n", tri.content_ratio,
                    tri.low_ratio_warning ? 1 : 0);
        CHECK(tri.content_ratio > 0.7 && tri.content_ratio < 0.9);
        CHECK(!tri.low_ratio_warning);
    });

    // ---------- 端到端 ----------
    runner.add("svg_end_to_end_to_loader", [](Runner& runner) {
        SvgReport rep;
        SvgStatus st = SvgStatus::Ok;
        std::string detail;
        const CursorSpec s = mk_spec(kPlainPath, SlotId::Hand, rep, st, detail);
        CHECK(st == SvgStatus::Ok);
        CHECK(s.frames.size() == 1);
        if (s.frames.empty()) return;
        // Hand 规则 HandTip：4x4 -> (4*3/10, 4/5) = (1, 0)
        CHECK(s.frames[0].hotspot.x == 1 && s.frames[0].hotspot.y == 0);

        std::vector<uint8_t> bytes;
        CHECK(write_cursor_bytes(s, bytes).status == WriteStatus::Ok);
        if (bytes.empty()) return;
        CHECK(cs::test::write_whole_file(L"build\\m4_hand.cur", bytes));
        const selfcheck::L1Result l1 = selfcheck::l1_load_and_probe(L"build\\m4_hand.cur");
        CHECK(l1.loaded);
        CHECK(l1.has_color_bitmap && l1.has_mask_bitmap);
        std::printf("SVG E2E: raster=%ux%u trimmed=%ux%u ratio=%.3f warn=%d loader=%ux%u hot=(%u,%u)\n",
                    rep.raster_w, rep.raster_h, rep.trimmed_w, rep.trimmed_h, rep.content_ratio,
                    rep.low_ratio_warning ? 1 : 0, l1.width, l1.height, l1.hotspot_x, l1.hotspot_y);
    });

    runner.add("svg_rejects_empty_and_guard_first", [](Runner& runner) {
        SvgReport rep;
        std::string detail;
        SvgStatus st = SvgStatus::Ok;
        mk_spec(std::string(), SlotId::Arrow, rep, st, detail);
        CHECK(st == SvgStatus::EmptyInput);

        // 守卫拒绝时不应留下任何帧，也不应走到光栅化
        const char* bad =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><text>x</text></svg>";
        mk_spec(bad, SlotId::Arrow, rep, st, detail);
        CHECK(st == SvgStatus::GuardRejected);
        CHECK(rep.guard.violations.size() >= 1);
        CHECK(rep.raster_w == 0);   // 没光栅化：守卫确实在前面
    });
}

}  // namespace cs::test
