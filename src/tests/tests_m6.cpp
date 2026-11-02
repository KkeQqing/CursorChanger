// src/tests/tests_m6.cpp — 三通道端到端集成：build_only 按扩展名把 png/svg/.cur 归一到 .cur 产物。
//
// 存在理由：M3/M4 交付的是**通道模块**及其单元级测试，M2 的端到端判据当时只覆盖 `.cur`
// 透传 —— 于是「PNG/SVG 能解码/能光栅化」与「PNG/SVG 真能被 apply 成系统指针」之间断了线。
// 本文件把这条线钉死：每条通道都有
//   ① 正例（产出被加载器接受的 .cur，帧集合精确匹配），
//   ② 与之配对的负对照（坏源必须挡在写注册表之前，绝不静默产出半坏文件），
//   ③ .cur 透传的回归（改一条通道不得动另一条）。
#include "cases.h"
#include "testfs.h"

#include "apply/pipeline.h"
#include "apply/registry.h"
#include "core/cur_reader.h"
#include "core/cur_writer.h"
#include "core/ir.h"
#include "selfcheck/l1.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace cs::core;
using cs::apply::PipelineOptions;
using cs::apply::PipelineResult;
using cs::test::Runner;

namespace {

const char* kTriSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\">"
    "<path d=\"M2 2 L30 2 L30 30 Z\" fill=\"#ffffff\"/></svg>";

// 含 <text>：白名单守卫必须拒；这正是「静默忽略会让坏图看起来没问题」的那一类（R8）。
const char* kTextSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\">"
    "<text x=\"4\" y=\"20\">hi</text></svg>";

bool mkdir_p(const std::wstring& d) {
    return CreateDirectoryW(d.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool put_text_as_binary(const std::wstring& path, const char* text) {
    std::vector<uint8_t> v(text, text + std::string(text).size());
    return cs::test::write_whole_file(path, v);
}

// 从夹具复制一张 PNG，改名成可认领的 arrow.png（文件名 stem 决定槽位，与扩展名无关）。
bool copy_png_fixture(const std::wstring& dir) {
    std::vector<uint8_t> png;
    if (!cs::test::read_whole_file(L"tests\\fixtures\\semi_grey_32x32.png", png)) return false;
    mkdir_p(dir);
    return cs::test::write_whole_file(dir + L"\\arrow.png", png);
}

std::vector<uint32_t> frame_widths(const CursorInfo& ci) {
    std::vector<uint32_t> w;
    for (const CursorEntryInfo& e : ci.entries) w.push_back(e.width);
    std::sort(w.begin(), w.end());
    return w;
}

// 正例：目录里只有一张 arrow.<ext>，build_only 应产出 1 个被加载器接受的 .cur。
// expect_widths 是**该通道契约要求的精确帧集合**，不是「非空」这种弱断言。
void expect_positive_channel(Runner& runner, const std::wstring& dir,
                             const std::vector<uint32_t>& expect_widths) {
    const PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
    const PipelineResult r = cs::apply::build_only(opt);

    CHECK(!r.aborted_before_registry);
    CHECK(r.produced.size() == 1);   // 遍历型自证：安静少产 0 个也要响
    if (r.produced.empty()) return;
    const auto& p = r.produced.front();
    CHECK(p.l1_ok);

    std::vector<uint8_t> cur;
    CHECK(cs::test::read_whole_file(p.path, cur));
    CursorInfo ci;
    CHECK(read_cursor_info(cur.data(), cur.size(), ci) == ReadStatus::Ok);
    CHECK(ci.type == 2);   // .cur 的类型字段
    CHECK(frame_widths(ci) == expect_widths);

    // 最终判据仍是加载器接受，而不是「我们自己写得出来」（H9：容错加载器会让坏文件看着没问题）。
    CHECK(cs::selfcheck::l1_load_and_probe(p.path.c_str()).loaded);
}

void expect_negative_channel(Runner& runner, const std::wstring& dir, const std::string& needle) {
    const PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
    const PipelineResult r = cs::apply::build_only(opt);

    CHECK(r.aborted_before_registry);
    // 失败必须「记在产物上且 l1 不过」，而不是悄悄没有产物 —— 两者都通向「用户以为应用了」。
    CHECK(r.produced.size() == 1);
    if (!r.produced.empty()) {
        CHECK(!r.produced.front().l1_ok);
        CHECK(r.produced.front().l1_detail.find(needle) != std::string::npos);
    }
}

}  // namespace

namespace cs::test {

void register_channel_e2e_cases(Runner& runner) {
    runner.add("png_channel_e2e_to_cur_loader", [](Runner& runner) {
        const std::wstring dir = L"build\\m6_png";
        if (!copy_png_fixture(dir)) { CHECK(false); return; }
        // 32 原生 + allow_upscale=false + sizes{16,32,48}：48 超过原生被跳过，应恰好 {16,32}。
        expect_positive_channel(runner, dir, {16, 32});
    });

    runner.add("svg_channel_e2e_to_cur_loader", [](Runner& runner) {
        const std::wstring dir = L"build\\m6_svg";
        mkdir_p(dir);
        if (!put_text_as_binary(dir + L"\\arrow.svg", kTriSvg)) { CHECK(false); return; }
        // SVG 按 work_resolution=1024 光栅化，16/32/48 全都在原生之内：应恰好 {16,32,48}。
        expect_positive_channel(runner, dir, {16, 32, 48});
    });

    // 负对照：非 PNG 字节却叫 arrow.png —— 解码必须拒，且记在产物上。
    runner.add("png_channel_e2e_rejects_non_png", [](Runner& runner) {
        const std::wstring dir = L"build\\m6_png_bad";
        mkdir_p(dir);
        std::vector<uint8_t> junk(64, 0xAB);
        cs::test::write_whole_file(dir + L"\\arrow.png", junk);
        expect_negative_channel(runner, dir, "png decode refused");
    });

    // 负对照：含 <text> 的 SVG —— 白名单守卫拒，绝不静默产出一张缺字的图。
    runner.add("svg_channel_e2e_rejects_guarded_feature", [](Runner& runner) {
        const std::wstring dir = L"build\\m6_svg_bad";
        mkdir_p(dir);
        put_text_as_binary(dir + L"\\arrow.svg", kTextSvg);
        expect_negative_channel(runner, dir, "svg raster refused");
    });

    // 负对照（写侧）：守卫拒的源即便走完整 apply_skin，也绝不能写到注册表（§7.1 / §11-M2 判据①）。
    runner.add("bad_channel_source_never_reaches_registry", [](Runner& runner) {
        const wchar_t* kSandbox = L"Software\\CursorSwitch-m6-selftest";
        cs::apply::ensure_key(cs::apply::Root::CurrentUser, kSandbox);
        // 清理起点
        cs::apply::delete_value(cs::apply::Root::CurrentUser, kSandbox, L"Arrow");

        const std::wstring dir = L"build\\m6_svg_bad";   // 上一个用例已写入 <text> svg
        PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
        opt.root = cs::apply::Root::CurrentUser;
        opt.sub_key = kSandbox;

        const PipelineResult r = cs::apply::apply_skin(opt);
        CHECK(r.aborted_before_registry);
        std::wstring v;
        bool found = false;
        cs::apply::read_sz(cs::apply::Root::CurrentUser, kSandbox, L"Arrow", v, found);
        CHECK(!found);   // 关键：没写进去

        cs::apply::delete_value(cs::apply::Root::CurrentUser, kSandbox, L"Arrow");
        cs::apply::delete_empty_key(cs::apply::Root::CurrentUser, kSandbox);
    });

    // 回归：三条通道里 .cur 透传仍是原样拷入、不重编码；加 png/svg 没动它。
    runner.add("cur_passthrough_still_works_after_channel_wiring", [](Runner& runner) {
        const std::wstring dir = L"build\\m6_cur";
        mkdir_p(dir);
        CursorSpec s;
        s.slot = SlotId::Arrow;
        s.source_kind = SourceKind::CurPassthrough;
        Frame f;
        f.width = 32;
        f.height = 32;
        f.hotspot = Hotspot{8, 8};
        f.pixels.assign(32u * 32u * 4u, 255);   // 全不透明
        s.frames.push_back(f);
        std::vector<uint8_t> bytes;
        if (write_cursor_bytes(s, bytes).status != WriteStatus::Ok) { CHECK(false); return; }
        cs::test::write_whole_file(dir + L"\\arrow.cur", bytes);

        // 透传：产物帧集合就是源的单帧 32（不会因 png/svg 的默认尺寸表被硬凑成 16/48）。
        expect_positive_channel(runner, dir, {32});
    });
}

}  // namespace cs::test
