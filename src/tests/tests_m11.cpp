// src/tests/tests_m11.cpp — §5.3 `.meta` sidecar：纯解析 + 端到端接通。
//
// 两条腿缺一不可：只测解析器 = 又造一个「绿但没接上业务链」的模块（本项目栽过）。
// 所以既测 parse_* 的正/负，也测「一张 arrow.png + arrow.meta」确实改变了产物帧集与热点
// —— 后者用 cur_reader（独立实现）当 oracle，不是拿解析器自己证明自己。
#include "cases.h"
#include "testfs.h"

#include "apply/pipeline.h"
#include "core/cur_reader.h"
#include "core/ir.h"
#include "core/sidecar.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace cs::core;
using cs::apply::PipelineOptions;
using cs::apply::PipelineResult;
using cs::test::Runner;

namespace {

bool mkdir_p(const std::wstring& d) {
    return CreateDirectoryW(d.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool put_text(const std::wstring& path, const char* text) {
    const std::string s(text);
    std::vector<uint8_t> v(s.begin(), s.end());
    return cs::test::write_whole_file(path, v);
}

std::vector<uint32_t> frame_widths(const CursorInfo& ci) {
    std::vector<uint32_t> w;
    for (const CursorEntryInfo& e : ci.entries) w.push_back(e.width);
    std::sort(w.begin(), w.end());
    return w;
}

}  // namespace

namespace cs::test {

void register_sidecar_cases(Runner& runner) {
    // ---- 纯解析：正例 ----
    runner.add("sidecar_parses_hotspot_size_trim", [](Runner& runner) {
        Sidecar sc;
        const SidecarParse r = parse_sidecar(
            "size = 16,32\nhotspot = 8,8 ; 覆盖默认规则\ntrim=auto\n", sc);
        CHECK(r.status == SidecarStatus::Ok);
        const std::vector<uint32_t> exp_sizes{16, 32};
        CHECK(sc.has_size && sc.sizes == exp_sizes);
        CHECK(sc.has_hotspot && sc.hotspot.x == 8 && sc.hotspot.y == 8);
        CHECK(sc.has_trim && sc.trim == "auto");
    });

    // ---- 纯解析：负（未知 key / 坏值 / 空）----
    runner.add("sidecar_rejects_unknown_key", [](Runner& runner) {
        Sidecar sc;
        const SidecarParse r = parse_sidecar("color = red\n", sc);
        CHECK(r.status == SidecarStatus::UnknownKey);
        CHECK(r.detail.find("color") != std::string::npos);
        // frames/rate 属 D21b，尚未定义 → 现在必须被当未知 key 拒，不得假装支持动画
        const SidecarParse r2 = parse_sidecar("frames = a.png,b.png\n", sc);
        CHECK(r2.status == SidecarStatus::UnknownKey);
    });

    runner.add("sidecar_rejects_bad_values", [](Runner& runner) {
        Sidecar sc;
        CHECK(parse_sidecar("size = 16,,32\n", sc).status == SidecarStatus::BadValue);   // 空段
        CHECK(parse_sidecar("size = 0\n", sc).status == SidecarStatus::BadValue);         // 0 无意义
        CHECK(parse_sidecar("size = 300\n", sc).status == SidecarStatus::BadValue);       // >256 越界
        CHECK(parse_sidecar("hotspot = a,b\n", sc).status == SidecarStatus::BadValue);    // 非数字
        CHECK(parse_sidecar("hotspot = 5\n", sc).status == SidecarStatus::BadValue);      // 少一个分量
        CHECK(parse_sidecar("trim = yes\n", sc).status == SidecarStatus::BadValue);       // 非 none/auto
        CHECK(parse_sidecar("just_a_key\n", sc).status == SidecarStatus::BadSyntax);      // 没有 '='
    });

    runner.add("sidecar_empty_text_is_empty_not_ok", [](Runner& runner) {
        Sidecar sc;
        CHECK(parse_sidecar("   \n; 只有注释\n\n", sc).status == SidecarStatus::Empty);
        CHECK(!sc.has_size && !sc.has_hotspot && !sc.has_trim);
    });

    // ---- 端到端：.meta 真的改变产物（cur_reader 独立 oracle）----
    runner.add("sidecar_size_and_hotspot_change_output_e2e", [](Runner& runner) {
        const std::wstring dir = L"build\\m11";
        mkdir_p(dir);
        std::vector<uint8_t> png;
        if (!cs::test::read_whole_file(L"tests\\fixtures\\semi_grey_32x32.png", png)) {
            CHECK(false);
            return;
        }
        CHECK(cs::test::write_whole_file(dir + L"\\arrow.png", png));
        // 原生 32；size=16,24（既去掉默认里的 32/48、又引入默认不会有的 24 => 与默认档 {16,32} 可区分）；
        // hotspot 以源尺寸 (8,8) 给。
        CHECK(put_text(dir + L"\\arrow.meta", "size = 16,24\nhotspot = 8,8\n"));

        const PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
        const PipelineResult r = cs::apply::build_only(opt);
        CHECK(!r.aborted_before_registry);
        CHECK(r.produced.size() == 1u);
        if (r.produced.size() != 1u) return;
        CHECK(r.produced.front().l1_ok);

        std::vector<uint8_t> cur;
        CHECK(cs::test::read_whole_file(r.produced.front().path, cur));
        CursorInfo ci;
        CHECK(read_cursor_info(cur.data(), cur.size(), ci) == ReadStatus::Ok);
        // size=16,24 生效：帧集恰是 {16,24}（默认应是 {16,32}）——证明 size 覆盖被消费。
        const std::vector<uint32_t> expect{16, 24};
        CHECK(frame_widths(ci) == expect);
        // hotspot=8,8 @源32：各帧按 U15 逐帧换算 -> 16 帧 (4,4)、24 帧 (6,6)，非照抄原值。
        uint16_t h16x = 0, h16y = 0, h24x = 0, h24y = 0;
        for (const CursorEntryInfo& e : ci.entries) {
            if (e.width == 16) { h16x = e.hotspot_x; h16y = e.hotspot_y; }
            if (e.width == 24) { h24x = e.hotspot_x; h24y = e.hotspot_y; }
        }
        CHECK(h16x == 4 && h16y == 4);   // scale_hotspot((8,8),32,16)
        CHECK(h24x == 6 && h24y == 6);   // scale_hotspot((8,8),32,24)
    });

    // ---- 端到端负：坏 .meta 必须挡在写注册表前，且原因落到该产物上 ----
    runner.add("sidecar_bad_key_blocks_before_registry_e2e", [](Runner& runner) {
        const std::wstring dir = L"build\\m11_bad";
        mkdir_p(dir);
        std::vector<uint8_t> png;
        if (!cs::test::read_whole_file(L"tests\\fixtures\\semi_grey_32x32.png", png)) {
            CHECK(false);
            return;
        }
        CHECK(cs::test::write_whole_file(dir + L"\\arrow.png", png));
        CHECK(put_text(dir + L"\\arrow.meta", "frames = a.png,b.png\n"));   // D21b 未落地 → 未知 key

        const PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
        const PipelineResult r = cs::apply::build_only(opt);
        CHECK(r.aborted_before_registry);
        CHECK(r.produced.size() == 1u);
        if (!r.produced.empty()) {
            CHECK(!r.produced.front().l1_ok);
            CHECK(r.produced.front().l1_detail.find("meta") != std::string::npos);
            CHECK(r.produced.front().l1_detail.find("unknown key") != std::string::npos);
        }
    });
}

}  // namespace cs::test
