// src/tests/tests_m10.cpp — D21a：`.ani` 透传通道。
//
// 独立 oracle 是**系统加载器**（LoadCursorFromFileW），不是我们自己的 reader：
//   .ani 容器语义尚未被本项目吃透（见 §4.4），所以不拿「自家 reader 能读回」当正确性依据
//   —— 那是同义反复。真正的离线可验证据是：passthrough 原样搬运 + OS 加载器接受这张 .ani。
// 诚实边界：加载成功 **不等于**「会动画」；动画真伪只能在真机 L3 判（U7），本文件不假装已验。
#include "cases.h"
#include "testfs.h"

#include "apply/pipeline.h"
#include "selfcheck/l1.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

using cs::apply::PipelineOptions;
using cs::apply::PipelineResult;
using cs::test::Runner;

namespace {

bool mkdir_p(const std::wstring& d) {
    return CreateDirectoryW(d.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

}  // namespace

namespace cs::test {

void register_ani_cases(Runner& runner) {
    // 正例：一张真实 .ani（取自本机 Windows 自带动画光标）被透传，产出 .ani 且被加载器接受、字节未被改写。
    runner.add("ani_passthrough_copied_and_loader_accepts", [](Runner& runner) {
        std::vector<uint8_t> src;
        if (!cs::test::read_whole_file(L"C:\\Windows\\cursors\\aero_busy.ani", src)) {
            CHECK(false);   // 本机没有这张系统动画光标 = 环境不符，响亮失败，不静默跳过
            return;
        }
        const std::wstring dir = L"build\\m10";
        mkdir_p(dir);
        CHECK(cs::test::write_whole_file(dir + L"\\busy.ani", src));   // busy -> Wait 别名

        const PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
        const PipelineResult r = cs::apply::build_only(opt);

        CHECK(!r.aborted_before_registry);
        CHECK(r.scanned == 1u);
        CHECK(r.produced.size() == 1u);
        CHECK(r.unclaimed.empty());
        CHECK(r.conflicts.empty());
        if (r.produced.size() != 1u) return;
        const auto& p = r.produced.front();
        CHECK(p.l1_ok);                                     // 加载器接受（独立 oracle）
        CHECK(p.registry_name == std::wstring(L"Wait"));
        // 产出扩展名应为 .ani，不是 .cur
        CHECK(p.path.size() >= 4 && p.path.compare(p.path.size() - 4, 4, L".ani") == 0);
        // passthrough 不重编码：产物字节 == 源字节
        std::vector<uint8_t> out;
        CHECK(cs::test::read_whole_file(p.path, out));
        CHECK(out == src);
    });

    // 负对照：垃圾字节取名 wait.ani —— 透传会照搬，但 L1 加载器必须拒绝，且挡在写注册表之前。
    runner.add("ani_garbage_rejected_before_registry", [](Runner& runner) {
        const std::wstring dir = L"build\\m10_bad";
        mkdir_p(dir);
        std::vector<uint8_t> junk(64, 0xAB);
        CHECK(cs::test::write_whole_file(dir + L"\\wait.ani", junk));

        const PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
        const PipelineResult r = cs::apply::build_only(opt);

        CHECK(r.aborted_before_registry);   // 坏 .ani 绝不进注册表
        CHECK(r.produced.size() == 1u);
        if (!r.produced.empty()) {
            CHECK(!r.produced.front().l1_ok);
            // 失败落在「加载器拒绝」这一类（不是解码/认领），与 .cur passthrough 同一保护
            CHECK(r.produced.front().l1_detail.find("loader rejected") != std::string::npos);
        }
    });
}

}  // namespace cs::test
