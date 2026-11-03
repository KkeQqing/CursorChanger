// src/tests/tests_m12.cpp — D21b 合成 writer/reader（RIFF/ACON .ani）。
//
// 独立 oracle 两条：① cur_reader（另一套实现）解析每帧内嵌 .cur；② LoadCursorFromFileW 接受
// 我们**自造**的 .ani —— 这是"结构对不对"最强证据（不是拿 reader 读 writer 自己，那是同义反复）。
// 诚实边界：加载接受 ≠「会按 iDispRate 转」，动画播放真伪 = U22，须真机。
#include "cases.h"
#include "testfs.h"

#include "apply/pipeline.h"
#include "core/ani_format.h"
#include "core/cur_reader.h"
#include "core/cur_writer.h"
#include "core/ir.h"
#include "core/slots.h"
#include "selfcheck/l1.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace cs::core;
using cs::test::Runner;

namespace {

// 一个 dim×dim 全不透明、非全透明的单帧 CursorSpec（预乘条件：255<=255 成立）。
CursorSpec solid_spec(SlotId slot, uint32_t dim) {
    CursorSpec s;
    s.slot = slot;
    s.source_kind = SourceKind::Png;
    Frame f;
    f.width = dim;
    f.height = dim;
    f.hotspot = Hotspot{0, 0};
    f.pixels.assign(static_cast<std::size_t>(dim) * dim * 4u, 255);
    s.frames.push_back(f);
    return s;
}

std::vector<uint8_t> to_cur(const CursorSpec& s) {
    std::vector<uint8_t> b;
    if (write_cursor_bytes(s, b).status != WriteStatus::Ok) b.clear();
    return b;
}

bool mkdir_p(const std::wstring& d) {
    return CreateDirectoryW(d.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

}  // namespace

namespace cs::test {

void register_ani_writer_cases(Runner& runner) {
    runner.add("ani_writer_roundtrip_and_embedded_cursors_valid", [](Runner& runner) {
        std::vector<uint8_t> cur32 = to_cur(solid_spec(SlotId::Arrow, 32));
        std::vector<uint8_t> cur48 = to_cur(solid_spec(SlotId::Arrow, 48));
        CHECK(!cur32.empty() && !cur48.empty());

        AniSpec spec;
        spec.frames = {cur32, cur48};
        spec.rate_jiffies = 7;
        std::vector<uint8_t> ani;
        CHECK(write_ani_bytes(spec, ani).status == AniWriteStatus::Ok);
        CHECK(!ani.empty());
        // RIFF/ACON 头
        CHECK(ani.size() > 12 && ani[0] == 'R' && ani[1] == 'I' && ani[2] == 'F' && ani[3] == 'F');
        CHECK(ani[8] == 'A' && ani[9] == 'C' && ani[10] == 'O' && ani[11] == 'N');

        // 反解核对字段与帧
        AniInfo info;
        CHECK(read_ani_info(ani.data(), ani.size(), info) == AniReadStatus::Ok);
        CHECK(info.n_frames == 2);
        CHECK(info.rate_jiffies == 7);
        CHECK(info.bit_count == 32);
        CHECK(info.bf_attributes == 1);
        CHECK(info.frames.size() == 2);
        CHECK(info.frames[0] == cur32);   // 帧字节原样进出，未改写
        CHECK(info.frames[1] == cur48);

        // 独立 oracle：每帧内嵌 .cur 能被另一套 reader 当合法光标解析
        for (const auto& f : info.frames) {
            CursorInfo ci;
            CHECK(read_cursor_info(f.data(), f.size(), ci) == ReadStatus::Ok);
            CHECK(ci.type == 2);
        }
    });

    runner.add("ani_writer_output_accepted_by_os_loader", [](Runner& runner) {
        // 最强离线证据：OS 的 LoadCursorFromFileW 接受我们自造的 .ani（仍不等于"会按速率转"=U22）。
        std::vector<uint8_t> a = to_cur(solid_spec(SlotId::Wait, 32));
        std::vector<uint8_t> b = to_cur(solid_spec(SlotId::Wait, 48));
        AniSpec spec;
        spec.frames = {a, b};
        spec.rate_jiffies = 6;
        std::vector<uint8_t> ani;
        if (write_ani_bytes(spec, ani).status != AniWriteStatus::Ok) { CHECK(false); return; }
        CHECK(cs::test::write_whole_file(L"build\\m12_spin.ani", ani));
        CHECK(cs::selfcheck::l1_load_and_probe(L"build\\m12_spin.ani").loaded);
    });

    runner.add("ani_writer_rejects_empty_and_over_max", [](Runner& runner) {
        std::vector<uint8_t> out;
        AniSpec empty;
        CHECK(write_ani_bytes(empty, out).status == AniWriteStatus::NoFrames);
        CHECK(out.empty());

        AniSpec over;
        over.max_frames = 2;
        over.frames = {{1, 2}, {3, 4}, {5, 6}};   // 3 > 2
        CHECK(write_ani_bytes(over, out).status == AniWriteStatus::TooManyFrames);
        CHECK(out.empty());                         // 越界报错、不截断、不留半坏文件（H9）
    });

    // 端到端：sidecar frames= 经 build_only 真的合成出 .ani、被 OS 加载、反解 nFrames=2。
    runner.add("ani_frames_sidecar_synthesizes_via_pipeline", [](Runner& runner) {
        const std::wstring dir = L"build\\m12_synth";
        mkdir_p(dir);
        std::vector<uint8_t> f0, f1, prim;
        if (!cs::test::read_whole_file(L"tests\\fixtures\\semi_grey_32x32.png", f0) ||
            !cs::test::read_whole_file(L"tests\\fixtures\\opaque_white_8x8.png", f1)) {
            CHECK(false);
            return;
        }
        prim = f0;
        CHECK(cs::test::write_whole_file(dir + L"\\wait.png", prim));    // 仅用于定槽位（内容走 frames）
        CHECK(cs::test::write_whole_file(dir + L"\\w0.png", f0));
        CHECK(cs::test::write_whole_file(dir + L"\\w1.png", f1));
        const std::string meta = "frames = w0.png,w1.png\nrate = 5\n";
        CHECK(cs::test::write_whole_file(dir + L"\\wait.meta",
                                         std::vector<uint8_t>(meta.begin(), meta.end())));

        const cs::apply::PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
        const cs::apply::PipelineResult r = cs::apply::build_only(opt);
        CHECK(!r.aborted_before_registry);
        CHECK(r.produced.size() == 1u);
        if (r.produced.size() != 1u) return;
        CHECK(r.produced.front().l1_ok);                                   // OS 加载接受合成 .ani
        CHECK(r.produced.front().path.size() >= 4 &&
              r.produced.front().path.compare(r.produced.front().path.size() - 4, 4, L".ani") == 0);
        std::vector<uint8_t> ani;
        CHECK(cs::test::read_whole_file(r.produced.front().path, ani));
        AniInfo info;
        CHECK(read_ani_info(ani.data(), ani.size(), info) == AniReadStatus::Ok);
        CHECK(info.n_frames == 2);
        CHECK(info.rate_jiffies == 5);                                     // sidecar rate 生效
    });

    // 端到端负：frames= 引用不存在的帧文件 => 该槽失败、挡在写注册表前。
    runner.add("ani_frames_missing_file_blocks_before_registry", [](Runner& runner) {
        const std::wstring dir = L"build\\m12_synth_bad";
        mkdir_p(dir);
        std::vector<uint8_t> prim;
        if (!cs::test::read_whole_file(L"tests\\fixtures\\semi_grey_32x32.png", prim)) {
            CHECK(false);
            return;
        }
        CHECK(cs::test::write_whole_file(dir + L"\\wait.png", prim));
        const std::string meta = "frames = nope.png\n";
        CHECK(cs::test::write_whole_file(dir + L"\\wait.meta",
                                         std::vector<uint8_t>(meta.begin(), meta.end())));
        const cs::apply::PipelineOptions opt = cs::apply::make_live_opts(dir, /*do_spi=*/false);
        const cs::apply::PipelineResult r = cs::apply::build_only(opt);
        CHECK(r.aborted_before_registry);
        CHECK(!r.produced.empty());
        if (!r.produced.empty()) {
            CHECK(!r.produced.front().l1_ok);
            CHECK(r.produced.front().l1_detail.find("cannot read frame file") != std::string::npos);
        }
    });
}

}  // namespace cs::test
