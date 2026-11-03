// src/tests/tests_m13.cpp — D23 全局 config/settings.ini：解析 + 端到端生效。
//
// 独立 oracle = cur_reader（帧集/尺寸）。用 PipelineOptions.settings_path 注入临时 ini，
// 不碰默认 <exe>\config\settings.ini，避免污染其它测试的档位期望（隔离）。
#include "cases.h"
#include "testfs.h"

#include "apply/pipeline.h"
#include "core/cur_reader.h"
#include "core/settings.h"

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
    return cs::test::write_whole_file(path, std::vector<uint8_t>(s.begin(), s.end()));
}
std::vector<uint32_t> frame_widths(const CursorInfo& ci) {
    std::vector<uint32_t> w;
    for (const CursorEntryInfo& e : ci.entries) w.push_back(e.width);
    std::sort(w.begin(), w.end());
    return w;
}

}  // namespace

namespace cs::test {

void register_settings_cases(Runner& runner) {
    runner.add("settings_parse_ok_and_reject", [](Runner& runner) {
        Settings s;
        CHECK(parse_settings("frames.sizes = 16,32,48\n", s).status == SettingsStatus::Ok);
        CHECK(s.has_frames_sizes && s.frames_sizes.size() == 3);
        CHECK(parse_settings("frames.bogus = 1\n", s).status == SettingsStatus::UnknownKey);
        // allow_upscale 不开放（通道只做下采样）→ 按未知 key 拒，不给接了却没用的旋钮
        CHECK(parse_settings("frames.allow_upscale = true\n", s).status == SettingsStatus::UnknownKey);
        CHECK(parse_settings("frames.sizes = 0\n", s).status == SettingsStatus::BadValue);      // 0 非法
        CHECK(parse_settings("frames.sizes = 512\n", s).status == SettingsStatus::BadValue);    // >256
        CHECK(parse_settings("no_equals_here\n", s).status == SettingsStatus::BadSyntax);
        CHECK(parse_settings("  ; only comment\n", s).status == SettingsStatus::Empty);
    });

    // 端到端：settings 收窄档位 → 产出帧集随之变（默认应 {16,32}，settings=16 应 {16}）
    runner.add("settings_sizes_narrow_output_e2e", [](Runner& runner) {
        const std::wstring dir = L"build\\m13";
        mkdir_p(dir);
        std::vector<uint8_t> png;
        if (!cs::test::read_whole_file(L"tests\\fixtures\\semi_grey_32x32.png", png)) { CHECK(false); return; }
        CHECK(cs::test::write_whole_file(dir + L"\\arrow.png", png));   // 32 原生
        CHECK(put_text(dir + L"\\cfg.ini", "frames.sizes = 16\n"));

        PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
        opt.settings_path = dir + L"\\cfg.ini";
        const PipelineResult r = cs::apply::build_only(opt);
        CHECK(!r.aborted_before_registry);
        CHECK(r.produced.size() == 1u);
        if (r.produced.size() != 1u) return;
        CHECK(r.produced.front().l1_ok);
        std::vector<uint8_t> cur;
        CHECK(cs::test::read_whole_file(r.produced.front().path, cur));
        CursorInfo ci;
        CHECK(read_cursor_info(cur.data(), cur.size(), ci) == ReadStatus::Ok);
        const std::vector<uint32_t> expect{16};   // 不是默认 {16,32}
        CHECK(frame_widths(ci) == expect);
    });

    // 端到端负：坏 settings.ini ⇒ 整体中止、绝不写注册表
    runner.add("settings_bad_file_aborts_before_registry", [](Runner& runner) {
        const std::wstring dir = L"build\\m13_bad";
        mkdir_p(dir);
        std::vector<uint8_t> png;
        if (!cs::test::read_whole_file(L"tests\\fixtures\\semi_grey_32x32.png", png)) { CHECK(false); return; }
        CHECK(cs::test::write_whole_file(dir + L"\\arrow.png", png));
        CHECK(put_text(dir + L"\\cfg.ini", "frames.sizzy = 16\n"));   // 未知 key

        PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
        opt.settings_path = dir + L"\\cfg.ini";
        const PipelineResult r = cs::apply::build_only(opt);
        CHECK(r.aborted_before_registry);
        CHECK(r.abort_reason.find("settings") != std::string::npos);
        CHECK(r.abort_reason.find("unknown key") != std::string::npos);
        CHECK(r.produced.empty());   // 中止在扫描之后、任何产出/写册之前
    });
}

}  // namespace cs::test
