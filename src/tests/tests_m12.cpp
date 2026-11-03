// src/tests/tests_m12.cpp — D21b 合成 writer/reader（RIFF/ACON .ani）。
//
// 独立 oracle 两条：① cur_reader（另一套实现）解析每帧内嵌 .cur；② LoadCursorFromFileW 接受
// 我们**自造**的 .ani —— 这是"结构对不对"最强证据（不是拿 reader 读 writer 自己，那是同义反复）。
// 诚实边界：加载接受 ≠「会按 iDispRate 转」，动画播放真伪 = U22，须真机。
#include "cases.h"
#include "testfs.h"

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
}

}  // namespace cs::test
