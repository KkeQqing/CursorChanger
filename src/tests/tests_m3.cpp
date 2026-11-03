// src/tests/tests_m3.cpp — PNG 通道：解码、预乘、缩放取整方向、拒绝路径
//
// 期望值来源：**手抄自 tools/gen_png_fixtures.py 生成的 fixtures.json**，并在注释里给出
// 手算过程。不让测试去读 JSON —— 读同一份产物当输入又当期望就是同义反复（本项目已因此
// 抓出过两次假绿）。两侧不一致时这里直接变红，正是我们要的信号。
#include "cases.h"
#include "testfs.h"

#include "core/cur_writer.h"
#include "core/ir.h"
#include "core/png_source.h"
#include "core/slots.h"
#include "selfcheck/l1.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace cs::core;
using cs::test::Runner;

namespace {

constexpr const wchar_t* kFixDir = L"tests\\fixtures\\";

bool load_fix(const wchar_t* file, std::vector<uint8_t>& out) {
    return cs::test::read_whole_file(std::wstring(kFixDir) + file, out);
}

PngOptions sizes163248() {
    PngOptions o;
    o.sizes = {16, 32, 48};
    return o;
}

}  // namespace

namespace cs::test {

void register_png_cases(Runner& runner) {
    runner.add("png_fixture_self_proof", [](Runner& runner) {
        // 夹具缺失/被换掉必须在这里响，否则后面的「解码失败」分不清是谁的锅
        std::vector<uint8_t> a, b, c;
        CHECK(load_fix(L"semi_grey_4x4.png", a));
        CHECK(load_fix(L"checker_4x4.png", b));
        CHECK(load_fix(L"rgb_no_alpha_4x4.png", c));
        CHECK(a.size() == 75);   // 与 tools/gen_png_fixtures.py 本次输出一致
        CHECK(b.size() == 83);
        CHECK(c.size() == 73);
        // PNG 签名：独立于 stb 的一条结构性自检
        CHECK(a.size() > 8 && a[0] == 0x89 && a[1] == 'P' && a[2] == 'N' && a[3] == 'G');
        CHECK(c.size() > 8 && c[0] == 0x89);
    });

    runner.add("png_premultiply_matches_hand_computed_values", [](Runner& runner) {
        // 源 (R,G,B,A) = (200,150,100,128)，公式 (v*a+127)/255：
        //   R: (200*128+127)/255 = 25727/255 = 100
        //   G: (150*128+127)/255 = 19327/255 = 75
        //   B: (100*128+127)/255 = 12927/255 = 50
        // BGRA 存序 -> (50, 75, 100, 128)
        std::vector<uint8_t> png;
        if (!load_fix(L"semi_grey_4x4.png", png)) return;
        CursorSpec s;
        std::string detail;
        PngOptions o = sizes163248();
        o.sizes = {4};  // 只要原生档，隔离「预乘」与「缩放」两件事
        CHECK(load_png_to_spec(png.data(), png.size(), SlotId::Arrow, o, s, detail) == PngStatus::Ok);
        CHECK(s.frames.size() == 1);
        if (s.frames.empty()) return;
        const Frame& f = s.frames[0];
        CHECK(f.width == 4 && f.height == 4);
        CHECK(f.pixels.size() == 4u * 4u * 4u);
        for (std::size_t i = 0; i < 16; ++i) {
            CHECK(f.pixels[i * 4u + 0] == 50);    // B
            CHECK(f.pixels[i * 4u + 1] == 75);    // G
            CHECK(f.pixels[i * 4u + 2] == 100);   // R
            CHECK(f.pixels[i * 4u + 3] == 128);   // A 原样
        }
        // 预乘成立 -> IR 的每通道 <= alpha 必然通过（两条判据在这里汇合）
        CHECK(premultiply_ok(f));
    });

    runner.add("png_downscale_uses_floor_not_round_half", [](Runner& runner) {
        // 棋盘 4x4 缩到 2x2：每个 2x2 块含两白两黑。
        //   R: (255+0+0+255)/4 = 510/4 = 127.5 -> floor 127
        //   G: (0+255+255+0)/4 = 127.5 -> floor 127
        //   B: 128*4/4 = 128（整除，两种取整都一样，故不构成区分）
        //   A: 255
        // 若哪天有人把缩放改成四舍五入，这里会变成 (128,128,128) 并立刻变红。
        std::vector<uint8_t> png;
        if (!load_fix(L"checker_4x4.png", png)) return;
        CursorSpec s;
        std::string detail;
        PngOptions o;
        o.sizes = {2};
        CHECK(load_png_to_spec(png.data(), png.size(), SlotId::Arrow, o, s, detail) == PngStatus::Ok);
        CHECK(s.frames.size() == 1);
        if (s.frames.empty()) return;
        const Frame& f = s.frames[0];
        CHECK(f.width == 2 && f.height == 2);
        for (std::size_t i = 0; i < 4; ++i) {
            CHECK(f.pixels[i * 4u + 0] == 128);  // B
            CHECK(f.pixels[i * 4u + 1] == 127);  // G  <- floor 的证据
            CHECK(f.pixels[i * 4u + 2] == 127);  // R  <- floor 的证据
            CHECK(f.pixels[i * 4u + 3] == 255);
        }
    });

    runner.add("png_without_alpha_becomes_opaque_not_knocked_out", [](Runner& runner) {
        // §4.2：无 alpha 的 RGB 输入 -> alpha 全 255，不做亮度抠图
        std::vector<uint8_t> png;
        if (!load_fix(L"rgb_no_alpha_4x4.png", png)) return;
        uint32_t w = 0, h = 0;
        const int comp = png_probe_channels(png.data(), png.size(), w, h);
        CHECK(comp == 3);            // 独立确认这张确实没有 alpha 通道
        CHECK(w == 4 && h == 4);

        CursorSpec s;
        std::string detail;
        PngOptions o;
        o.sizes = {4};
        CHECK(load_png_to_spec(png.data(), png.size(), SlotId::Arrow, o, s, detail) == PngStatus::Ok);
        if (s.frames.empty()) return;
        const Frame& f = s.frames[0];
        for (std::size_t i = 0; i < 16; ++i) {
            CHECK(f.pixels[i * 4u + 0] == 0);     // B
            CHECK(f.pixels[i * 4u + 1] == 140);   // G
            CHECK(f.pixels[i * 4u + 2] == 255);   // R
            CHECK(f.pixels[i * 4u + 3] == 255);   // A：一律不透明
        }
    });

    runner.add("png_fully_transparent_is_rejected_by_ir_not_by_channel", [](Runner& runner) {
        // H10 只在 IR/writer 一处判（通道层不重复规则）。所以这里必须：
        // 通道解码成功 -> writer 拒收。两半都是结论。
        std::vector<uint8_t> png;
        if (!load_fix(L"fully_transparent_4x4.png", png)) return;
        CursorSpec s;
        std::string detail;
        PngOptions o;
        o.sizes = {4};
        CHECK(load_png_to_spec(png.data(), png.size(), SlotId::Arrow, o, s, detail) == PngStatus::Ok);
        CHECK(s.frames.size() == 1);

        std::vector<uint8_t> bytes;
        const WriteError e = write_cursor_bytes(s, bytes);
        CHECK(e.status == WriteStatus::InvalidSpec);
        CHECK(e.spec_error.status == SpecStatus::FullyTransparent);
        CHECK(bytes.empty());   // 拒收不留半成品
    });

    runner.add("png_oversized_source_is_not_upscaled", [](Runner& runner) {
        // 源 4x4，请求 16/32/48 全部大于源 -> 不放大，改用原生 4 这一档
        std::vector<uint8_t> png;
        if (!load_fix(L"semi_grey_4x4.png", png)) return;
        CursorSpec s;
        std::string detail;
        CHECK(load_png_to_spec(png.data(), png.size(), SlotId::Arrow, sizes163248(), s, detail) ==
              PngStatus::Ok);
        CHECK(s.frames.size() == 1);
        if (s.frames.empty()) return;
        CHECK(s.frames[0].width == 4);
        for (const Frame& f : s.frames) CHECK(f.width == 4);
    });

    runner.add("png_hotspot_follows_slot_rule_per_frame", [](Runner& runner) {
        // U15：不同档的热点应随尺寸变化；Arrow 的规则是 (0,0) 所以这里同时验「规则查表正确」
        std::vector<uint8_t> png;
        if (!load_fix(L"opaque_white_8x8.png", png)) return;
        CursorSpec arrow;
        std::string detail;
        PngOptions o;
        o.sizes = {8, 4, 2};
        CHECK(load_png_to_spec(png.data(), png.size(), SlotId::Arrow, o, arrow, detail) == PngStatus::Ok);
        CHECK(arrow.frames.size() == 3);
        for (const Frame& f : arrow.frames) {
            CHECK(f.hotspot.x == 0 && f.hotspot.y == 0);  // TipTopLeft
        }
        // 中心类规则：SizeAll 在 8/4/2 上热点分别是 (4,4)/(2,2)/(1,1)
        CursorSpec move;
        CHECK(load_png_to_spec(png.data(), png.size(), SlotId::SizeAll, o, move, detail) ==
              PngStatus::Ok);
        CHECK(move.frames.size() == 3);
        if (move.frames.size() != 3) return;
        CHECK(move.frames[0].width == 2 && move.frames[0].hotspot.x == 1);
        CHECK(move.frames[1].width == 4 && move.frames[1].hotspot.x == 2);
        CHECK(move.frames[2].width == 8 && move.frames[2].hotspot.x == 4);
        // 面积升序（H8 的 IR 约定）：乱序会被 writer 拒，所以能写成功就说明顺序成立
        std::vector<uint8_t> bytes;
        CHECK(write_cursor_bytes(move, bytes).status == WriteStatus::Ok);
        CHECK(!bytes.empty());
    });

    runner.add("png_rejects_non_png_and_empty", [](Runner& runner) {
        CursorSpec s;
        std::string detail;
        const std::vector<uint8_t> junk(64, 0xAB);
        CHECK(load_png_to_spec(junk.data(), junk.size(), SlotId::Arrow, sizes163248(), s, detail) ==
              PngStatus::DecodeFailed);
        CHECK(load_png_to_spec(nullptr, 0, SlotId::Arrow, sizes163248(), s, detail) ==
              PngStatus::EmptyInput);
        const std::vector<uint8_t> none;
        CHECK(load_png_to_spec(none.data(), none.size(), SlotId::Arrow, sizes163248(), s, detail) ==
              PngStatus::EmptyInput);
        CHECK(s.frames.empty());
    });

    runner.add("png_end_to_end_survives_writer_and_real_loader", [](Runner& runner) {
        // M3 判据的最后一环：PNG -> IR -> CurWriter -> 落盘 -> 真实加载器接受，
        // 且回读的尺寸/热点与 spec 一致。缺了它，「解码正确」和「能被系统用」是两件事。
        std::vector<uint8_t> png;
        if (!load_fix(L"semi_grey_4x4.png", png)) return;
        CursorSpec s;
        std::string detail;
        PngOptions o;
        o.sizes = {4};
        CHECK(load_png_to_spec(png.data(), png.size(), SlotId::Hand, o, s, detail) == PngStatus::Ok);
        CHECK(s.frames.size() == 1);

        std::vector<uint8_t> bytes;
        // writer 会验 H8 帧序与各帧热点界内，所以「写成功」本身就是一次校验
        CHECK(write_cursor_bytes(s, bytes).status == WriteStatus::Ok);
        if (bytes.empty()) return;
        CHECK(cs::test::write_whole_file(L"build\\m3_hand.cur", bytes));

        const cs::selfcheck::L1Result l1 = cs::selfcheck::l1_load_and_probe(L"build\\m3_hand.cur");
        CHECK(l1.loaded);
        CHECK(l1.has_color_bitmap && l1.has_mask_bitmap);

        // 本条最初写成「回读尺寸/热点 == 文件里那一帧」，被真值否掉了：
        // 文件只有 4x4 帧时，加载器会重采样到系统默认尺寸（本机 32），
        // 并把热点一起换算 —— (1,0)@4 变成 (8,0)@32。
        // 于是断言改成更有力的形式：**我们的 scale_hotspot 必须与 OS 的换算吻合**。
        // 这条比原写法强，因为它是跨实现的一致性检查，而不是自己对自己。
        const uint32_t sys = cs::selfcheck::system_cursor_default_size();
        CHECK(sys > 0);
        CHECK(l1.width == sys && l1.height == sys);
        const Hotspot expect = scale_hotspot(s.frames[0].hotspot, s.frames[0].width, sys);
        std::printf("E2E png->cur: file frame 4x4 hot=(%u,%u) -> loader %ux%u hot=(%u,%u), "
                    "our scale_hotspot says (%u,%u)\n",
                    s.frames[0].hotspot.x, s.frames[0].hotspot.y, l1.width, l1.height, l1.hotspot_x,
                    l1.hotspot_y, expect.x, expect.y);
        CHECK(l1.hotspot_x == expect.x);
        CHECK(l1.hotspot_y == expect.y);
        // Hand 的规则是 HandTip：4x4 -> (4*3/10, 4/5) = (1, 0)
        CHECK(s.frames[0].hotspot.x == 1 && s.frames[0].hotspot.y == 0);
    });

    runner.add("png_names_the_actual_format_when_mislabeled", [](Runner& runner) {
        // 本轮的动因：用户把 JPEG 改名成 .png，旧实现只回笼统 "stb decode failed"，GUI 只能显示
        // 看不懂的内部码。现在必须先嗅魔数、点名真实格式。输入是**手写的各格式魔数字节**（独立于
        // stb / 独立于被测），期望是 detail 里出现该格式名 —— 不是拿输出当期望。
        CursorSpec s;
        std::string detail;

        std::vector<uint8_t> jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00};
        CHECK(load_png_to_spec(jpeg.data(), jpeg.size(), SlotId::Arrow, sizes163248(), s, detail) ==
              PngStatus::DecodeFailed);
        CHECK(detail.find("JPEG") != std::string::npos);
        CHECK(s.frames.empty());

        std::vector<uint8_t> gif = {'G', 'I', 'F', '8', '9', 'a', 0x01, 0x00};
        CHECK(load_png_to_spec(gif.data(), gif.size(), SlotId::Arrow, sizes163248(), s, detail) ==
              PngStatus::DecodeFailed);
        CHECK(detail.find("GIF") != std::string::npos);

        std::vector<uint8_t> bmp = {'B', 'M', 0x36, 0x00, 0x00, 0x00};
        CHECK(load_png_to_spec(bmp.data(), bmp.size(), SlotId::Arrow, sizes163248(), s, detail) ==
              PngStatus::DecodeFailed);
        CHECK(detail.find("BMP") != std::string::npos);

        // ICO/CUR 容器冒充 png：也应点名（type 字节在偏移 2..3 = 01 或 02）
        std::vector<uint8_t> ico = {0x00, 0x00, 0x01, 0x00, 0x01, 0x00};
        CHECK(load_png_to_spec(ico.data(), ico.size(), SlotId::Arrow, sizes163248(), s, detail) ==
              PngStatus::DecodeFailed);
        CHECK(detail.find("ICO/CUR") != std::string::npos);

        // 反向对照（防嗅探误杀真图）：真正的 PNG 夹具必须走成功分支、detail 不该被格式名污染。
        std::vector<uint8_t> png;
        if (load_fix(L"semi_grey_4x4.png", png)) {
            CursorSpec ok_spec;
            std::string ok_detail;
            PngOptions o;
            o.sizes = {4};
            CHECK(load_png_to_spec(png.data(), png.size(), SlotId::Arrow, o, ok_spec, ok_detail) ==
                  PngStatus::Ok);
            CHECK(ok_detail.find("JPEG") == std::string::npos);
            CHECK(ok_detail.find("not a PNG") == std::string::npos);
        } else {
            CHECK(false);   // 夹具没了，误杀分支就没被覆盖 —— 显式响，别静默跳过
        }
    });
}

}  // namespace cs::test
