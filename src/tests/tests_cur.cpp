// src/tests/tests_cur.cpp — M1b：CUR 容器（writer 独立实现对照 golden + reader 反向核验）
//
// 本文件的立足点：golden **不是**由被测的 C++ writer 生成的，而是 tools/gen_golden.py
// 按 cur_format.h 的布局独立编码、再由一份分开写的解码规则验过才落盘。
// 因此「writer 输出 == golden」是两个独立实现的一致性对照，而不是实现自比。
#include "cases.h"

#include "core/cur_format.h"
#include "core/cur_reader.h"
#include "core/cur_writer.h"
#include "core/ir.h"
#include "core/slots.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

using namespace cs::core;
using cs::test::Runner;

namespace {

struct Px {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
};

// ---------- 夹具：与 tools/gen_golden.py 的 SPEC 同构，但独立转写 ----------
constexpr uint32_t kW0 = 4;
constexpr uint32_t kH0 = 4;
// top-down：行 0 是最上一行。非对称图案（上三行不透明、底行半透明、col0 全透明、(2,1) 半透明）
// 才有资格做 H11（底向上）的判据 —— 对称图案上行序颠倒不会有任何可观察差异。
constexpr Px kFrame0[kH0 * kW0] = {
    {0, 0, 0, 0},         {255, 0, 0, 255},   {128, 128, 128, 128}, {0, 0, 255, 255},
    {0, 0, 0, 0},         {0, 255, 0, 255},   {255, 255, 255, 255}, {128, 0, 0, 255},
    {0, 0, 0, 0},         {64, 32, 16, 128},  {0, 255, 255, 255},   {255, 0, 255, 255},
    {0, 0, 0, 0},         {0, 0, 0, 128},     {0, 0, 0, 64},        {0, 0, 0, 32},
};
constexpr uint32_t kW1 = 8;
constexpr uint32_t kH1 = 8;
constexpr Px kFrame1Pixel = {128, 128, 128, 128};  // 半透明纯灰，预乘下 BGR == A

CursorSpec fixture_spec() {
    CursorSpec s;
    s.slot = SlotId::Arrow;
    s.source_kind = SourceKind::Png;

    Frame f0;
    f0.width = kW0;
    f0.height = kH0;
    f0.hotspot = Hotspot{1, 1};   // 帧级热点（U15）
    f0.pixels.resize(static_cast<std::size_t>(kW0) * kH0 * 4u);
    for (std::size_t i = 0; i < kW0 * kH0; ++i) {
        f0.pixels[i * 4u + 0] = kFrame0[i].b;
        f0.pixels[i * 4u + 1] = kFrame0[i].g;
        f0.pixels[i * 4u + 2] = kFrame0[i].r;
        f0.pixels[i * 4u + 3] = kFrame0[i].a;
    }

    Frame f1;
    f1.width = kW1;
    f1.height = kH1;
    f1.hotspot = Hotspot{1, 1};
    f1.pixels.assign(static_cast<std::size_t>(kW1) * kH1 * 4u, 0);
    for (std::size_t i = 0; i < kW1 * kH1; ++i) {
        f1.pixels[i * 4u + 0] = kFrame1Pixel.b;
        f1.pixels[i * 4u + 1] = kFrame1Pixel.g;
        f1.pixels[i * 4u + 2] = kFrame1Pixel.r;
        f1.pixels[i * 4u + 3] = kFrame1Pixel.a;
    }

    s.frames.push_back(f0);  // 面积升序（H8 的 IR 约定）
    s.frames.push_back(f1);
    return s;
}

// ---------- golden 读取与自证 ----------
constexpr const wchar_t* kGoldenPath = L"tests/golden/cursor_4x4_8x8_premult.cur";
constexpr std::size_t kGoldenBytes = 1974;
constexpr uint64_t kGoldenFnv = 0xE54CE2E80CCF5B9Aull;  // FNV-1a 64，由生成侧算出

bool read_whole_file(const wchar_t* path, std::vector<uint8_t>& out) {
    std::FILE* f = nullptr;
    if (::_wfopen_s(&f, path, L"rb") != 0 || f == nullptr) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.clear();
    if (n > 0) out.resize(static_cast<std::size_t>(n));
    const std::size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size() && got > 0;
}

// 从零构造 .cur 字节用的小端写入器（与 writer 内部实现无关，测试侧独立一份）
void push_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFFu));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFFu));
}

void push_u32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFFu));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFFu));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFFu));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFFu));
}

void push_i32(std::vector<uint8_t>& v, int32_t x) { push_u32(v, static_cast<uint32_t>(x)); }

uint64_t fnv1a64(const std::vector<uint8_t>& v) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t b : v) {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    return h;
}

// 先把夹具自证清楚再比对：夹具读不到 / 被截空 / 被换掉，都必须在这里响，
// 否则后面的「相等」可能是两个空数组相等。
bool load_golden_self_proved(Runner& runner, std::vector<uint8_t>& golden) {
    const bool ok = read_whole_file(kGoldenPath, golden);
    CHECK(ok);
    if (!ok) return false;
    CHECK(golden.size() == kGoldenBytes);
    CHECK(fnv1a64(golden) == kGoldenFnv);
    if (golden.size() != kGoldenBytes || fnv1a64(golden) != kGoldenFnv) return false;
    return true;
}

// ---------- 字节级改写工具（给 reader 的负例用） ----------
void put_u16_at(std::vector<uint8_t>& v, std::size_t at, uint16_t x) {
    v[at] = static_cast<uint8_t>(x & 0xFFu);
    v[at + 1] = static_cast<uint8_t>((x >> 8) & 0xFFu);
}

void put_u32_at(std::vector<uint8_t>& v, std::size_t at, uint32_t x) {
    v[at] = static_cast<uint8_t>(x & 0xFFu);
    v[at + 1] = static_cast<uint8_t>((x >> 8) & 0xFFu);
    v[at + 2] = static_cast<uint8_t>((x >> 16) & 0xFFu);
    v[at + 3] = static_cast<uint8_t>((x >> 24) & 0xFFu);
}

ReadStatus status_of(const std::vector<uint8_t>& bytes) {
    CursorInfo info;
    return read_cursor_info(bytes.data(), bytes.size(), info);
}

Frame single_frame(uint32_t w, uint32_t h, uint8_t level, Hotspot hs = Hotspot{0, 0}) {
    Frame f;
    f.width = w;
    f.height = h;
    f.hotspot = hs;
    f.pixels.assign(static_cast<std::size_t>(w) * h * 4u, 0);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
        f.pixels[i * 4u + 0] = level;
        f.pixels[i * 4u + 1] = level;
        f.pixels[i * 4u + 2] = level;
        f.pixels[i * 4u + 3] = level;
    }
    return f;
}

// hs 落到帧上（帧级热点），不再存在 spec 级热点
CursorSpec spec_of(Frame f, Hotspot hs) {
    CursorSpec s;
    s.slot = SlotId::Arrow;
    s.source_kind = SourceKind::Png;
    f.hotspot = hs;
    s.frames.push_back(f);
    return s;
}

}  // namespace

namespace cs::test {

void register_container_cases(Runner& runner) {

    runner.add("golden_fixture_self_proof", [](Runner& runner) {
        std::vector<uint8_t> golden;
        // load_golden_self_proved 内部已经 CHECK 过；这里再断一次结果，防止「静默跳过」
        const bool loaded = load_golden_self_proved(runner, golden);
        CHECK(loaded);
        CHECK(golden.size() == kGoldenBytes);
    });

    runner.add("writer_output_equals_independent_golden", [](Runner& runner) {
        std::vector<uint8_t> golden;
        if (!load_golden_self_proved(runner, golden)) return;

        std::vector<uint8_t> built;
        const WriteError e = write_cursor_bytes(fixture_spec(), built);
        CHECK(e.status == WriteStatus::Ok);
        CHECK(built.size() == golden.size());
        // 逐字节：H2/H3/H6/H8/H11 任何一处写错，这里就红
        CHECK(built == golden);
    });

    runner.add("reader_accepts_writer_output_and_agrees", [](Runner& runner) {
        std::vector<uint8_t> built;
        CHECK(write_cursor_bytes(fixture_spec(), built).status == WriteStatus::Ok);

        CursorInfo info;
        const ReadStatus st = read_cursor_info(built.data(), built.size(), info);
        CHECK(st == ReadStatus::Ok);
        if (st != ReadStatus::Ok) return;

        CHECK(info.count == 2);
        CHECK(info.entries.size() == 2);
        CHECK(info.entries[0].width == kW0 && info.entries[0].height == kH0);
        CHECK(info.entries[1].width == kW1 && info.entries[1].height == kH1);
        CHECK(info.entries[0].bit_count == 32);
        CHECK(info.entries[0].planes == 1);
        CHECK(info.entries[0].hotspot_x == 1 && info.entries[0].hotspot_y == 1);
        CHECK(info.entries[0].image_offset == 38);   // 6 + 16*2
        CHECK(info.entries[1].image_offset == 654);  // 38 + 616
        CHECK(info.entries[0].bytes_in_res == 616);
        CHECK(info.entries[1].bytes_in_res == 1320);
        CHECK(info.trailing_bytes == 0);
        CHECK(and_mask_all_zero(info, built.data(), built.size(), 0));
        CHECK(and_mask_all_zero(info, built.data(), built.size(), 1));
    });

    runner.add("row_order_bottom_up_decodes_back_to_top_down", [](Runner& runner) {
        // H11：容器底向上、IR top-down。逐点取回并对照原图，走满 16 点才算数。
        std::vector<uint8_t> built;
        CHECK(write_cursor_bytes(fixture_spec(), built).status == WriteStatus::Ok);
        CursorInfo info;
        CHECK(read_cursor_info(built.data(), built.size(), info) == ReadStatus::Ok);

        std::size_t visited = 0;
        for (uint32_t y = 0; y < kW0; ++y) {
            for (uint32_t x = 0; x < kW0; ++x) {
                uint8_t got[4] = {0, 0, 0, 0};
                CHECK(frame_pixel_at(info, built.data(), built.size(), 0, x, y, got));
                const Px& want = kFrame0[y * kW0 + x];
                CHECK(got[0] == want.b);
                CHECK(got[1] == want.g);
                CHECK(got[2] == want.r);
                CHECK(got[3] == want.a);
                ++visited;
            }
        }
        CHECK(visited == static_cast<std::size_t>(kW0) * kH0);

        // 反向对照：行序真的颠倒时，(0,0) 与 (3,0) 的取值会互换 —— 先确认这两点本来就不同，
        // 否则上面的逐点比对可能因为图案对称而空过。
        uint8_t a[4] = {0, 0, 0, 0};
        uint8_t b[4] = {0, 0, 0, 0};
        frame_pixel_at(info, built.data(), built.size(), 0, 1, 0, a);
        frame_pixel_at(info, built.data(), built.size(), 0, 1, 3, b);
        CHECK(a[3] != b[3]);  // 行 0 col1 是全不透明，行 3 col1 是 alpha=128
    });

    runner.add("dir_size_byte_zero_means_256", [](Runner& runner) {
        // H6：目录项 0 == 256，而 DIB 头里必须写真实值
        std::vector<uint8_t> built;
        const WriteError e = write_cursor_bytes(spec_of(single_frame(256, 256, 255), Hotspot{255, 255}), built);
        CHECK(e.status == WriteStatus::Ok);
        if (e.status != WriteStatus::Ok) return;
        CHECK(built[6] == 0);   // entry0 width 字节
        CHECK(built[7] == 0);   // entry0 height 字节
        CursorInfo info;
        CHECK(read_cursor_info(built.data(), built.size(), info) == ReadStatus::Ok);
        CHECK(info.entries[0].width == 256);
        CHECK(info.entries[0].height == 256);
        CHECK(info.entries[0].bi_height == 512);  // 2h，且用的是真实尺寸不是 0
        CHECK(info.entries[0].bi_width == 256);
    });

    runner.add("writer_rejects_unordered_frames", [](Runner& runner) {
        // H8 的 IR 约定：不在 writer 里代排序，静默重排会让调用方误以为自己的顺序有效
        CursorSpec s = fixture_spec();
        std::swap(s.frames[0], s.frames[1]);
        std::vector<uint8_t> built;
        const WriteError e = write_cursor_bytes(s, built);
        CHECK(e.status == WriteStatus::FramesUnordered);
        CHECK(built.empty());  // 拒收时必须什么都没写
    });

    runner.add("writer_delegates_spec_invariants", [](Runner& runner) {
        std::vector<uint8_t> built;

        CursorSpec empty;
        CHECK(write_cursor_bytes(empty, built).status == WriteStatus::InvalidSpec);

        std::vector<uint8_t> dummy;
        const WriteError oob = write_cursor_bytes(spec_of(single_frame(4, 4, 255), Hotspot{4, 0}), dummy);
        CHECK(oob.status == WriteStatus::InvalidSpec);
        CHECK(oob.spec_error.status == SpecStatus::HotspotOutOfBounds);

        const WriteError clear = write_cursor_bytes(spec_of(single_frame(4, 4, 0), Hotspot{0, 0}), dummy);
        CHECK(clear.spec_error.status == SpecStatus::FullyTransparent);  // H10
    });

    runner.add("small_frame_gets_padded_and_region", [](Runner& runner) {
        // 1x1：AND 行取 max(4, writer.min_frame_bytes=128) => 条目 = 40+4+128 = 172
        std::vector<uint8_t> built;
        CHECK(write_cursor_bytes(spec_of(single_frame(1, 1, 255), Hotspot{0, 0}), built).status == WriteStatus::Ok);
        CursorInfo info;
        CHECK(read_cursor_info(built.data(), built.size(), info) == ReadStatus::Ok);
        CHECK(info.entries[0].bytes_in_res == 172);
        EntryLayout l;
        CHECK(entry_layout(info, 0, built.data(), built.size(), l));
        CHECK(l.and_row_bytes == 128);
        CHECK(l.xor_bytes == 4);
    });

    // ---------- 审查补漏（M1b）：writer 的拒绝路径与非 32bpp 行距 ----------

    runner.add("writer_rejects_not_premultiplied_input", [](Runner& runner) {
        // H4：这条以前只在 validate_spec 层测过，**没有经过 write_cursor_bytes**。
        // 「入口函数会自己校验」属于未证明的假设，一旦有人把校验挪走就会静默放行。
        std::vector<uint8_t> px(4 * 4 * 4, 0);
        for (std::size_t i = 0; i < px.size(); i += 4) {
            px[i + 0] = 255;  // B
            px[i + 3] = 128;  // A  -> B > A，非预乘
        }
        CursorSpec s = spec_of(make_frame(4, 4, px), Hotspot{0, 0});
        std::vector<uint8_t> built;
        const WriteError e = write_cursor_bytes(s, built);
        CHECK(e.status == WriteStatus::InvalidSpec);
        CHECK(e.spec_error.status == SpecStatus::NotPremultiplied);
        CHECK(built.empty());  // 拒收不得留下半个产物
    });

    runner.add("writer_rejects_too_many_frames", [](Runner& runner) {
        // 这条分支此前完全没有测试：目录头的 count 是 uint16，超限必须拒而不是截断。
        CursorSpec s = fixture_spec();  // 2 帧
        CHECK(s.frames.size() == 2);
        WriteOptions o;
        o.max_dir_entries = 1;
        std::vector<uint8_t> built;
        CHECK(write_cursor_bytes(s, built, o).status == WriteStatus::TooManyFrames);
        CHECK(built.empty());
    });

    runner.add("writer_pads_entry_to_declared_length", [](Runner& runner) {
        // H9：bytes_in_res 必须等于实际写入字节。这里把 AND 行下限压到最小、
        // 把条目下限抬到 256，制造 raw < min_dir_gap_bytes 的组合 ——
        // 上一版正是这种配置下静默产出「登记 256、实写 48」的坏文件。
        WriteOptions o;
        o.min_frame_bytes = 4;    // 1x1 的 AND 行按精确对齐就是 4
        o.min_dir_gap_bytes = 256;

        std::vector<uint8_t> built;
        const WriteError e = write_cursor_bytes(spec_of(single_frame(1, 1, 255), Hotspot{0, 0}), built, o);
        CHECK(e.status == WriteStatus::Ok);
        if (e.status != WriteStatus::Ok) return;

        const std::size_t expected_total = kDirHeaderBytes + kDirEntryBytes + 256u;
        CHECK(built.size() == expected_total);  // 头 + 1 个目录项 + 补齐后的条目

        CursorInfo info;
        CHECK(read_cursor_info(built.data(), built.size(), info) == ReadStatus::Ok);
        CHECK(info.entries[0].bytes_in_res == 256);
        CHECK(info.trailing_bytes == 0);
        EntryLayout l;
        CHECK(entry_layout(info, 0, built.data(), built.size(), l));
        CHECK(l.xor_bytes == 4);
        CHECK(l.and_bytes == 256 - 40u - 4u);  // 补齐被算进掩码区，且必须全 0
        CHECK(and_mask_all_zero(info, built.data(), built.size(), 0));
    });

    runner.add("reader_uses_bitdepth_row_stride_not_4x_width", [](Runner& runner) {
        // 非 32bpp 的第三方光标：XOR 行距必须按位深算。
        // 8x8 / 8bpp：行距 = ((8*8+31)/32)*4 = 8 字节，整段 XOR = 64；
        // 而无条件写 w*4 的旧实现会算成 256 —— 两个数差得远，这条断言不是空壳。
        std::vector<uint8_t> f;
        const uint32_t xor_bytes = 64u;   // 行距 ((8*8+31)/32)*4 = 8，8 行
        const uint32_t and_bytes = 32u;   // 1bpp 行距 ((8+31)/32)*4 = 4，8 行
        const uint32_t entry_len = 40u + xor_bytes + and_bytes;
        const uint32_t off = 6u + 16u;    // 单条目：头 6 + 目录项 16

        push_u16(f, 0);                 // ICONDIR.reserved
        push_u16(f, 2);                 // ICONDIR.type = cursor
        push_u16(f, 1);                 // ICONDIR.count
        // 目录项**必须正好 16 字节**：4 + 2 + 2 + 4 + 4。上一版这里多塞了一个 u32，
        // 条目变 20 字节，于是整段偏移错位 —— 是测试自己错了，不是 reader 错了。
        f.push_back(8);                 // bWidth
        f.push_back(8);                 // bHeight
        f.push_back(0);                 // bColorCount
        f.push_back(0);                 // bReserved
        push_u16(f, 1);                 // hotspot x
        push_u16(f, 1);                 // hotspot y
        push_u32(f, entry_len);         // dwBytesInRes
        push_u32(f, off);               // dwImageOffset

        push_u32(f, 40);                // biSize
        push_u32(f, 8);                 // biWidth
        push_i32(f, 16);                // biHeight = 2h
        push_u16(f, 1);                 // biPlanes
        push_u16(f, 8);                 // biBitCount = 8
        push_u32(f, 0);                 // BI_RGB
        push_u32(f, xor_bytes);         // biSizeImage
        push_u32(f, 0); push_u32(f, 0); push_u32(f, 0); push_u32(f, 0);
        for (uint32_t i = 0; i < xor_bytes + and_bytes; ++i) f.push_back(0);

        CHECK(f.size() == off + entry_len);  // 自证夹具长度，避免又拿错位的数据去测

        CursorInfo info;
        CHECK(read_cursor_info(f.data(), f.size(), info) == ReadStatus::Ok);
        CHECK(info.entries[0].bit_count == 8);
        EntryLayout l;
        CHECK(entry_layout(info, 0, f.data(), f.size(), l));
        CHECK(l.xor_bytes == 64);
        CHECK(l.and_bytes == 32);
        CHECK(l.and_offset == off + 40u + 64u);
        // 32bpp 专属的取点/掩码判定对 8bpp 必须明确「不适用」，不能给假结果
        uint8_t px[4] = {0, 0, 0, 0};
        CHECK(!frame_pixel_at(info, f.data(), f.size(), 0, 0, 0, px));
        CHECK(!and_mask_all_zero(info, f.data(), f.size(), 0));
    });

    // ---------- reader 负例：每类坏字节都要被具体状态码抓住 ----------
    runner.add("reader_rejects_bad_bytes", [](Runner& runner) {
        std::vector<uint8_t> golden;
        if (!load_golden_self_proved(runner, golden)) return;

        // 前提对照：原样必须能过（否则下面每一条「红」都可能只是因为夹具本身坏了）
        CHECK(status_of(golden) == ReadStatus::Ok);

        {  // H1：type 不是 2
            auto v = golden;
            put_u16_at(v, 2, 1);
            CHECK(status_of(v) == ReadStatus::WrongType);
        }
        {  // 头 reserved 非 0
            auto v = golden;
            put_u16_at(v, 0, 0x1234);
            CHECK(status_of(v) == ReadStatus::BadReserved);
        }
        {  // 条目数 0
            auto v = golden;
            put_u16_at(v, 4, 0);
            CHECK(status_of(v) == ReadStatus::ZeroCount);
        }
        {  // 条目数被写坏成大值（无意义 length 字段的典型形态）
            auto v = golden;
            put_u16_at(v, 4, 0xFFFF);
            CHECK(status_of(v) == ReadStatus::CountOverflow);
        }
        {  // H2：biHeight 不再是 2h
            auto v = golden;
            put_u32_at(v, 38 + 8, 4);  // frame0 的 biHeight 改成 h
            CHECK(status_of(v) == ReadStatus::BadDoubleHeight);
        }
        {  // biWidth 与目录项不符
            auto v = golden;
            put_u32_at(v, 38 + 4, 5);
            CHECK(status_of(v) == ReadStatus::BadWidth);
        }
        {  // H7：planes
            auto v = golden;
            put_u16_at(v, 38 + 12, 2);
            CHECK(status_of(v) == ReadStatus::BadPlanes);
        }
        {  // H7：bit_count 非法
            auto v = golden;
            put_u16_at(v, 38 + 14, 16);
            CHECK(status_of(v) == ReadStatus::BadBitCount);
        }
        {  // H3：AND 掩码出现非零
            auto v = golden;
            v[38 + 40 + 64] = 0xFF;  // frame0 的 AND 区首字节
            CHECK(status_of(v) == ReadStatus::BadAndMask);
        }
        {  // H5：热点越界
            auto v = golden;
            put_u16_at(v, 6 + 4, 200);  // entry0 hotspot_x
            CHECK(status_of(v) == ReadStatus::BadHotspot);
        }
        {  // H9：文件被截短
            auto v = golden;
            v.resize(1000);
            CHECK(status_of(v) == ReadStatus::Truncated);
        }
        {  // H8：帧偏移出现空洞
            auto v = golden;
            const uint32_t off1 = 654;
            put_u32_at(v, 6 + 16 + 12, off1 + 1);
            CHECK(status_of(v) == ReadStatus::OffsetNotContiguous);
        }
        {  // H8：首帧压在目录上
            auto v = golden;
            put_u32_at(v, 6 + 12, 10);
            CHECK(status_of(v) == ReadStatus::OverlapsDir);
        }
    });
}

}  // namespace cs::test
