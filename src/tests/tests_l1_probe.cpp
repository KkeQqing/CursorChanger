// src/tests/tests_l1_probe.cpp — M2a 第一步：实测 Windows 光标加载器的行为
//
// 纪律：这批观测的目的是**核销 §13 里三个关于加载器的假设**（U12 hotspot 字段位置、
// U14 biSizeImage 取值、U16 AND 行过填充），并顺便回答一个 M2 设计问题：
// 「指名帧能不能取到」。看完真值才写断言 —— 没有观测值支持的断言等于把猜测当事实。
//
// 安全性：只做进程内加载（LoadCursorFromFileW / LoadImageW + DestroyIcon），
// 不写注册表、不广播、不调用 SetSystemCursor，不会改变用户正在使用的指针。
#include "cases.h"
#include "testfs.h"

#include "core/cur_reader.h"
#include "core/cur_writer.h"
#include "core/ir.h"
#include "core/slots.h"
#include "selfcheck/l1.h"

#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <vector>

using namespace cs::core;

namespace {

CursorSpec solid_spec(uint32_t dim, Hotspot hs) {
    CursorSpec s;
    s.slot = SlotId::Arrow;
    s.source_kind = SourceKind::Png;
    Frame f;
    f.width = dim;
    f.height = dim;
    f.hotspot = hs;  // 帧级热点（U15）
    f.pixels.assign(static_cast<std::size_t>(dim) * dim * 4u, 255);  // 全不透明白：BGR<=A 成立
    s.frames.push_back(f);
    return s;
}

void write_probe(const wchar_t* name, const CursorSpec& spec, std::vector<uint8_t>& bytes_out) {
    bytes_out.clear();
    if (write_cursor_bytes(spec, bytes_out).status != WriteStatus::Ok) {
        std::fprintf(stdout, "PROBE %-34s WRITER REFUSED\n", "prep");
        return;
    }
    wchar_t path[128];
    if (swprintf_s(path, 128, L"build\\probe_%ls", name) == -1) return;
    if (!cs::test::write_whole_file(path, bytes_out)) {
        std::fprintf(stdout, "PROBE %-34s cannot write %ls\n", "prep", path);
        return;
    }
    std::fprintf(stdout, "PROBE %-34s file=%ls bytes=%zu\n", "prep", path, bytes_out.size());
}

void probe_one(const char* label, const wchar_t* path) {
    const cs::selfcheck::L1Result a = cs::selfcheck::l1_load_and_probe(path);
    std::printf("PROBE %-34s via=LoadCursorFromFileW loaded=%d %ux%u hot=(%u,%u) color=%d mask=%d err=%u\n",
                label, a.loaded ? 1 : 0, a.width, a.height, a.hotspot_x, a.hotspot_y,
                a.has_color_bitmap ? 1 : 0, a.has_mask_bitmap ? 1 : 0, a.win_error);
}

void probe_exact(const char* label, const wchar_t* path, uint32_t w, uint32_t h) {
    const cs::selfcheck::L1Result b = cs::selfcheck::l1_load_frame(path, w, h);
    std::printf("PROBE %-34s via=LoadImage(exact %ux%u) loaded=%d %ux%u hot=(%u,%u) color=%d err=%u\n",
                label, w, h, b.loaded ? 1 : 0, b.width, b.height, b.hotspot_x, b.hotspot_y,
                b.has_color_bitmap ? 1 : 0, b.win_error);
}

}  // namespace

namespace cs::test {

void probe_l1_load(std::FILE* out) {
    (void)out;  // report 一律走 stdout，调用方负责重定向到日志文件
    std::printf("== L1 loader probe (in-process only, no system state touched) ==\n");

    // 1) 三档单帧：系统默认尺寸会不会把我们的帧换掉？
    const uint32_t sizes[] = {16, 32, 48};
    for (uint32_t dim : sizes) {
        wchar_t name[32];
        swprintf_s(name, 32, L"single_%u.cur", dim);
        std::vector<uint8_t> bytes;
        write_probe(name, solid_spec(dim, Hotspot{static_cast<uint16_t>(dim / 4),
                                                  static_cast<uint16_t>(dim / 4)}),
                    bytes);
        wchar_t path[128];
        swprintf_s(path, 128, L"build\\probe_single_%u.cur", dim);
        char label[64];
        sprintf_s(label, 64, "single%u", dim);
        probe_one(label, path);
        probe_exact(label, path, dim, dim);
    }

    // 2) 多帧 16/32/48：加载器自选 + 指名取帧（H8 的真实侧）
    {
        CursorSpec s = solid_spec(16, Hotspot{1, 1});
        for (uint32_t dim : {32u, 48u}) {
            Frame f;
            f.width = dim;
            f.height = dim;
            f.hotspot = scale_hotspot(Hotspot{1, 1}, 16, dim);  // 逐帧换算，不是一值抄三遍
            f.pixels.assign(static_cast<std::size_t>(dim) * dim * 4u, 255);
            s.frames.push_back(f);
        }
        std::vector<uint8_t> bytes;
        write_probe(L"multi.cur", s, bytes);
        probe_one("multi-16-32-48", L"build\\probe_multi.cur");
        probe_exact("multi-16-32-48", L"build\\probe_multi.cur", 16, 16);
        probe_exact("multi-16-32-48", L"build\\probe_multi.cur", 48, 48);
    }

    // 3) golden 夹具（4x4/8x8，非系统尺寸）：U14 / U16 的直接对象
    probe_one("golden-4x4-8x8", L"tests\\golden\\cursor_4x4_8x8_premult.cur");
    probe_exact("golden-4x4-8x8", L"tests\\golden\\cursor_4x4_8x8_premult.cur", 4, 4);
    probe_exact("golden-4x4-8x8", L"tests\\golden\\cursor_4x4_8x8_premult.cur", 8, 8);

    std::fflush(stdout);
}

uint32_t scaled(uint32_t value, uint32_t from_dim, uint32_t to_dim) {
    // 观测到的换算形式：写 (12,12)@48 回读 (8,8)@32；写 (4,4)@16 回读 (8,8)@32。
    // 两种都精确等于 round(value * to / from)，所以用四舍五入。取不到整数时测试会告诉我们。
    if (from_dim == 0) return 0;
    return (value * to_dim + from_dim / 2) / from_dim;
}

}  // namespace

namespace cs::test {

// 由观测值写死的断言。路径与 probe 输出一致，全部落在 build\（已被 gitignore）。
const wchar_t* const kProbeSingle16 = L"build\\probe_single_16.cur";
const wchar_t* const kProbeMulti = L"build\\probe_multi.cur";
const wchar_t* const kGolden = L"tests\\golden\\cursor_4x4_8x8_premult.cur";

void register_l1_cases(Runner& runner) {
    // 前置条件：这些产物由同一次运行的 probe_l1_load 写出。
    // 没有前置自证，后面的「加载失败」就分不清是实现坏了还是夹具根本不存在。
    runner.add("l1_requires_probe_artifacts", [](Runner& runner) {
        std::vector<uint8_t> junk;
        CHECK(cs::test::read_whole_file(kProbeSingle16, junk));
        CHECK(cs::test::read_whole_file(kProbeMulti, junk));
        CHECK(cs::test::read_whole_file(kGolden, junk));
    });

    runner.add("l1_loader_accepts_our_container", [](Runner& runner) {
        // 核销 U14（biSizeImage 写 XOR 实际长度）与 U16（AND 行过填充 128）：
        // 真实加载器接受我们两个字段取值，否则 loaded=0。
        const cs::selfcheck::L1Result a = cs::selfcheck::l1_load_and_probe(kProbeSingle16);
        CHECK(a.loaded);
        CHECK(a.win_error == 0);
        CHECK(a.has_color_bitmap);
        CHECK(a.has_mask_bitmap);

        const cs::selfcheck::L1Result g = cs::selfcheck::l1_load_and_probe(kGolden);
        CHECK(g.loaded);
        CHECK(g.has_color_bitmap);
        CHECK(g.has_mask_bitmap);
    });

    runner.add("l1_hotspot_is_read_from_directory_words", [](Runner& runner) {
        // U12：指名取帧时不重采样，回读的 hotspot 必须**逐字等于**我们写进目录项那两个 WORD 的值。
        // 这是 U12 的直接判据：写的位置对了就相等，写错位置就会读到别的数。
        const cs::selfcheck::L1Result s16 = cs::selfcheck::l1_load_frame(kProbeSingle16, 16, 16);
        CHECK(s16.loaded);
        CHECK(s16.width == 16);
        CHECK(s16.height == 16);
        CHECK(s16.hotspot_x == 4 && s16.hotspot_y == 4);   // 写入值

        const cs::selfcheck::L1Result g4 = cs::selfcheck::l1_load_frame(kGolden, 4, 4);
        CHECK(g4.loaded);
        CHECK(g4.width == 4 && g4.height == 4);
        CHECK(g4.hotspot_x == 1 && g4.hotspot_y == 1);     // 写入值
    });

    runner.add("l1_system_loader_resamples_and_scales_hotspot", [](Runner& runner) {
        // 实测发现：LoadCursorFromFileW 一律给系统默认边长，并把 hotspot 按比例换算。
        // 断言按 SM_CXCURSOR 推算，不把 32 写死（换台机器默认值不同也应成立）。
        const uint32_t sys = cs::selfcheck::system_cursor_default_size();
        CHECK(sys > 0);

        const cs::selfcheck::L1Result a = cs::selfcheck::l1_load_and_probe(kProbeSingle16);
        CHECK(a.loaded);
        CHECK(a.width == sys);
        CHECK(a.height == sys);
        CHECK(a.hotspot_x == scaled(4, 16, sys));
        CHECK(a.hotspot_y == scaled(4, 16, sys));

        // 反向对照：同一份多帧文件，指名取 48 时**不得**被换算成系统尺寸；
        // 且热点必须是自己那一帧的换算值（1@16 -> 3@48），不是抄来的同一个数。
        // 这条断言在 U15 修复前是 "== 1"，它当时"过"就是因为各帧热点相同 —— 正是测不出问题的形态。
        const cs::selfcheck::L1Result m48 = cs::selfcheck::l1_load_frame(kProbeMulti, 48, 48);
        CHECK(m48.loaded);
        CHECK(m48.width == 48 && m48.height == 48);
        CHECK(m48.hotspot_x == 3 && m48.hotspot_y == 3);

        const cs::selfcheck::L1Result m16 = cs::selfcheck::l1_load_frame(kProbeMulti, 16, 16);
        CHECK(m16.loaded);
        CHECK(m16.hotspot_x == 1 && m16.hotspot_y == 1);  // 参考帧原值
        CHECK(m16.hotspot_x != m48.hotspot_x);            // 帧间确实不同 = 逐帧换算而非一值抄三遍
    });

    runner.add("l1_frames_are_size_navigable", [](Runner& runner) {
        // H8 的真实侧：多帧目录能被按尺寸导航，且每帧回读到自己那一帧。
        const cs::selfcheck::L1Result m16 = cs::selfcheck::l1_load_frame(kProbeMulti, 16, 16);
        const cs::selfcheck::L1Result m32 = cs::selfcheck::l1_load_frame(kProbeMulti, 32, 32);
        const cs::selfcheck::L1Result m48 = cs::selfcheck::l1_load_frame(kProbeMulti, 48, 48);
        CHECK(m16.loaded && m16.width == 16);
        CHECK(m32.loaded && m32.width == 32);
        CHECK(m48.loaded && m48.width == 48);

        // 自证：三次取到的必须是三个不同尺寸，而不是同一个对象被返回了三遍
        CHECK(!(m16.width == m32.width && m32.width == m48.width));
    });

    runner.add("l1_rejects_non_cursor_file", [](Runner& runner) {
        // 负对照：L1 必须能拒绝。用一段与容器无关的字节当作 .cur 去加载。
        const wchar_t* path = L"build\\probe_garbage.cur";
        std::vector<uint8_t> junk(64, 0xAB);
        CHECK(cs::test::write_whole_file(path, junk));

        const cs::selfcheck::L1Result good = cs::selfcheck::l1_load_and_probe(kProbeSingle16);
        CHECK(good.loaded);                       // 正向对照，排除「加载器整体不可用」
        const cs::selfcheck::L1Result r = cs::selfcheck::l1_load_and_probe(path);
        CHECK(!r.loaded);                         // 判据只有这一个
        // 实测（本条最初写成断言 win_error != 0，被真值证伪）：
        // LoadCursorFromFileW 拒绝垃圾时返回 NULL 但**不设 last error**，win_error 仍是 0。
        // 结论写进 l1.h：M2 的错误报告不能靠 GetLastError 解释失败，只能报「加载器拒绝」。
        CHECK(r.win_error == 0);
    });

    runner.add("l1_probe_accepts_icon_typed_container", [](Runner& runner) {
        // 真探测：把 golden 的 ICONDIR.type 从 2 改成 1（冒充图标），问加载器认不认。
        // 这一条不预设结论 —— 认或不认都是 M2 需要知道的平台事实。
        std::vector<uint8_t> v;
        CHECK(cs::test::read_whole_file(kGolden, v));
        if (v.size() < 4) return;
        v[2] = 1;
        v[3] = 0;
        const wchar_t* path = L"build\\probe_as_icon.cur";
        CHECK(cs::test::write_whole_file(path, v));

        const cs::selfcheck::L1Result r = cs::selfcheck::l1_load_and_probe(path);
        std::printf("OBSERVE type=1 container: loaded=%d err=%u (H1 requires us to reject it anyway)\n",
                    r.loaded ? 1 : 0, r.win_error);
        // 无论加载器认不认，**我们自己的 reader 必须拒**（H1）。这条把责任放对位置：
        // 判据在 selfcheck 侧，不在 OS 的宽容度上。
        cs::core::CursorInfo info;
        CHECK(cs::core::read_cursor_info(v.data(), v.size(), info) == cs::core::ReadStatus::WrongType);
    });
}

}  // namespace cs::test
