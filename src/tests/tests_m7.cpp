// src/tests/tests_m7.cpp — 真素材皮肤离线自检：对仓库里真实的 skin/ 跑 build_only。
//
// 为什么直接测 skin/ 而不是再造一份合成夹具（AGENTS §4「禁止同义反复」的反面）：
//   被测对象就是**交付给用户的那 16 个文件**本身。若皮肤里有某槽 PNG/SVG 解不出、或某帧
//   被加载器拒，这条会当场变红 —— 而不是像实心块那样「永远没问题」。
// 边界（本步判据 = 用户选的「仅离线自检」）：只 build_only（扫描→认领→落地 build\applied→L1），
//   do_spi=false 且绝不进 apply_skin，因此**不写注册表、不触发 SPI、不动用户正在用的指针**。
//   皮肤像素是否「好看/热点对位」仍是 U8/U17 的真机目测项，这里不假装已验。
#include "cases.h"
#include "testfs.h"

#include "apply/pipeline.h"
#include "core/cur_reader.h"
#include "selfcheck/l1.h"

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

// 遍历型夹具的「应当访问到 N 个」自证数：改槽位表/别名表会在这里显形，而不是安静少测。
constexpr std::uint32_t kExpectedScanned = 16;   // 15 槽 + arrow 的 png/svg 冲突对
constexpr std::size_t kExpectedProduced = 15;    // 同槽双认领去重后
constexpr std::size_t kExpectedConflicts = 1;    // 且恰好是 Arrow 一处

bool mkdir_p(const std::wstring& d) {
    return CreateDirectoryW(d.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::vector<std::uint32_t> frame_widths(const CursorInfo& ci) {
    std::vector<std::uint32_t> w;
    for (const CursorEntryInfo& e : ci.entries) w.push_back(e.width);
    std::sort(w.begin(), w.end());
    return w;
}

bool ends_with(const std::wstring& s, const std::wstring& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// 每个产物必须是**精确三档 {16,32,48}**（PNG 48 原生 + SVG work_res=1024 都覆盖三档），
// 且被真实加载器接受。断言相等而非「非空」——H9：容错加载器会让坏文件看着没问题。
void check_produced_is_real_cur(Runner& runner, const std::wstring& path) {
    std::vector<std::uint8_t> cur;
    CHECK(cs::test::read_whole_file(path, cur));
    CursorInfo ci;
    CHECK(read_cursor_info(cur.data(), cur.size(), ci) == ReadStatus::Ok);
    CHECK(ci.type == 2);                       // .cur，不是被当 .ico
    // 期望帧集合放进局部变量：CHECK 宏按逗号切参数，花括号里的逗号会把它撑成多参。
    const std::vector<std::uint32_t> expect{16, 32, 48};
    CHECK(frame_widths(ci) == expect);
    CHECK(cs::selfcheck::l1_load_and_probe(path.c_str()).loaded);
}

}  // namespace

namespace cs::test {

void register_real_skin_cases(Runner& runner) {
    // 正例（全链）：真实 skin/ 覆盖 15 槽、1 处同槽冲突、PNG 择胜、逐槽三帧 + L1 通过、无未认领。
    runner.add("real_skin_offline_e2e", [](Runner& runner) {
        const PipelineOptions opt = cs::apply::make_live_opts(L"skin", /*do_spi=*/false);
        const PipelineResult r = cs::apply::build_only(opt);

        // 前提方向断言：真素材链**不该**在写册前中止（坏素材才该）。
        CHECK(!r.aborted_before_registry);
        CHECK(r.scanned == kExpectedScanned);
        CHECK(r.produced.size() == kExpectedProduced);
        CHECK(r.conflicts.size() == kExpectedConflicts);
        CHECK(r.unclaimed.empty());
        if (r.produced.size() != kExpectedProduced) return;   // 数量不对就别逐槽空跑

        for (const auto& p : r.produced) {
            CHECK(p.l1_ok);
            if (p.l1_ok) check_produced_is_real_cur(runner, p.path);
        }

        // 冲突必须是 Arrow，且被采用的源是 .png（排序择一：skin\arrow.png < skin\arrow.svg）。
        if (r.conflicts.size() == 1) {
            CHECK(r.conflicts.front().slot_name == L"Arrow");
            CHECK(r.conflicts.front().files.size() == 2);
            CHECK(ends_with(r.conflicts.front().files.front(), L".png"));
        }
    });

    // 正例（部分认领）：单槽 skin 也是合法结果，缺槽不是 bug（§5.2 推论）。
    runner.add("real_skin_partial_cover_is_valid", [](Runner& runner) {
        const std::wstring dir = L"build\\m7_partial";
        mkdir_p(dir);
        std::vector<std::uint8_t> png;
        if (!cs::test::read_whole_file(L"skin\\arrow.png", png)) { CHECK(false); return; }
        cs::test::write_whole_file(dir + L"\\arrow.png", png);

        const PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
        const PipelineResult r = cs::apply::build_only(opt);
        CHECK(!r.aborted_before_registry);
        CHECK(r.scanned == 1u);
        CHECK(r.produced.size() == 1u);
        CHECK(r.conflicts.empty());
        if (!r.produced.empty()) {
            CHECK(r.produced.front().l1_ok);
            check_produced_is_real_cur(runner, r.produced.front().path);
        }
    });

    // 负对照：全透明素材必须在**写注册表之前**就被挡下（H10），且记在产物上、绝不静默产出。
    // 取反方向：这条断言正文与上面「正例不中止」相反，让「透明」成为唯一变量。
    runner.add("real_skin_fully_transparent_rejected_before_registry", [](Runner& runner) {
        const std::wstring dir = L"build\\m7_trans";
        mkdir_p(dir);
        std::vector<std::uint8_t> png;
        if (!cs::test::read_whole_file(L"tests\\fixtures\\fully_transparent_4x4.png", png)) {
            CHECK(false);
            return;
        }
        cs::test::write_whole_file(dir + L"\\arrow.png", png);

        const PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
        const PipelineResult r = cs::apply::build_only(opt);
        CHECK(r.aborted_before_registry);
        CHECK(r.produced.size() == 1u);
        if (!r.produced.empty()) {
            CHECK(!r.produced.front().l1_ok);
            // 失败原因要落在「cur writer 拒绝（全透明）」这一类，而不是含糊的读不出。
            CHECK(r.produced.front().l1_detail.find("cur writer rejected") != std::string::npos);
        }
    });
}

}  // namespace cs::test
