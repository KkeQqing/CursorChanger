// src/tests/tests_tables.cpp — M1a：数据表守卫、认领、hotspot 规则、IR 不变量
//
// 三条纪律在本文件的落点：
//  1) 遍历型用例自带「访问了几条」的自证断言（防夹具安静地少测）；
//  2) 每条负例断言的是**具体状态码**，不是「返回了错误」这种弱断言；
//  3) display_name 非空这条，防守的是本项目 M1a 真实发生过的一次手抖
//     （槽表显示名被我写成空串，编译与旧测试都不会响）。
#include "cases.h"

#include "core/claim.h"
#include "core/ir.h"
#include "core/slots.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace cs::core;
using cs::test::Runner;

namespace {

// 期望数量取自 DESIGN：§5.2 槽表 17 行、V1.0 判据 16 个 v1 槽位、别名表 43 条。
// 数字变了必须同时改 DESIGN，否则这里变红。
constexpr std::size_t kExpectedSlots = 17;
constexpr std::size_t kExpectedV1Slots = 16;
constexpr std::size_t kExpectedAliases = 43;

bool ascii_only(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (static_cast<unsigned char>(c) >= 0x80) return false;
    }
    return true;
}

// 独立期望表：**43 条逐字手抄自 DESIGN §5.2**，不从 alias_table() 派生。
// 存在理由：M1a 的两次守卫制造实验证明
//   a) 用同一张表当输入又当期望的自检，在被破坏时完全不动；
//   b) 只「每槽抄一条」也不够 —— 破坏落在没被抄到的那条别名上时照样全绿。
// 所以这里全表对照：表内容被改坏（ASCII 拼写、同形字符、增删行）都必须在这里变红。
struct ExpectedAlias {
    const char* alias;
    SlotId slot;
};
constexpr ExpectedAlias kExpectedAliasRows[] = {
    {"arrow", SlotId::Arrow},
    {"normal", SlotId::Arrow},
    {"ibeam", SlotId::IBeam},
    {"text", SlotId::IBeam},
    {"beam", SlotId::IBeam},
    {"wait", SlotId::Wait},
    {"busy", SlotId::Wait},
    {"loading", SlotId::Wait},
    {"cross", SlotId::Cross},
    {"precision", SlotId::Cross},
    {"up", SlotId::UpArrow},
    {"alternate", SlotId::UpArrow},
    {"uparrow", SlotId::UpArrow},
    {"help", SlotId::Help},
    {"question", SlotId::Help},
    {"question_arrow", SlotId::Help},
    {"hand", SlotId::Hand},
    {"link", SlotId::Hand},
    {"pointer", SlotId::Hand},
    {"appstarting", SlotId::AppStarting},
    {"working_busy", SlotId::AppStarting},
    {"no", SlotId::Unavailable},
    {"not_allowed", SlotId::Unavailable},
    {"denied", SlotId::Unavailable},
    {"unavailable", SlotId::Unavailable},
    {"move", SlotId::SizeAll},
    {"sizeall", SlotId::SizeAll},
    {"ew_resize", SlotId::SizeWE},
    {"sizewe", SlotId::SizeWE},
    {"ns_resize", SlotId::SizeNS},
    {"sizens", SlotId::SizeNS},
    {"nw_resize", SlotId::SizeNW},
    {"sizenw", SlotId::SizeNW},
    {"top_left_corner", SlotId::SizeNW},
    {"se_resize", SlotId::SizeSE},
    {"sizese", SlotId::SizeSE},
    {"bottom_right_corner", SlotId::SizeSE},
    {"ne_resize", SlotId::SizeNE},
    {"sizene", SlotId::SizeNE},
    {"top_right_corner", SlotId::SizeNE},
    {"sw_resize", SlotId::SizeSW},
    {"sizesw", SlotId::SizeSW},
    {"bottom_left_corner", SlotId::SizeSW},
};
constexpr std::size_t kExpectedAliasCount = sizeof(kExpectedAliasRows) / sizeof(kExpectedAliasRows[0]);

// 预乘 BGRA：通道值不超过 alpha，因此 alpha=0 时必为 (0,0,0,0)。
std::vector<uint8_t> solid(uint32_t w, uint32_t h, uint8_t level) {
    std::vector<uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = level;
        px[i + 1] = level;
        px[i + 2] = level;
        px[i + 3] = level;
    }
    return px;
}

CursorSpec one_frame_spec(uint32_t w, uint32_t h, uint8_t level, Hotspot hs) {
    CursorSpec s;
    s.slot = SlotId::Arrow;
    s.source_kind = SourceKind::Png;
    s.hotspot = hs;
    s.frames.push_back(make_frame(w, h, solid(w, h, level)));
    return s;
}

}  // namespace

namespace cs::test {

void register_table_cases(Runner& runner) {
    runner.add("slot_table_count_matches_design", [](Runner& runner) {
        std::size_t n = 0;
        const SlotSpec* t = slot_table(n);
        CHECK(n == kExpectedSlots);
        CHECK(t != nullptr);
    });

    runner.add("slot_table_rows_are_ordered_by_id", [](Runner& runner) {
        // 表行序必须等于 SlotId 声明序：一旦有人重排表或改枚举，槽位会整体错位
        // （「层号当节点号」同族缺陷，编译与逐条功能测试都发现不了）。
        std::size_t n = 0;
        const SlotSpec* t = slot_table(n);
        std::size_t checked = 0;
        for (std::size_t i = 0; i < n; ++i) {
            CHECK(t[i].id == static_cast<SlotId>(i));
            ++checked;
        }
        CHECK(checked == kExpectedSlots);
    });

    runner.add("slot_table_registry_names_wellformed", [](Runner& runner) {
        std::size_t n = 0;
        const SlotSpec* t = slot_table(n);
        for (std::size_t i = 0; i < n; ++i) {
            // registry_name 参与注册表写入与查表：ASCII、非空、无空格、无路径分隔符
            CHECK(ascii_only(t[i].registry_name));
            CHECK(t[i].registry_name.find(' ') == std::string_view::npos);
            CHECK(t[i].registry_name.find('\\') == std::string_view::npos);
        }
    });

    runner.add("slot_table_registry_names_unique", [](Runner& runner) {
        std::size_t n = 0;
        const SlotSpec* t = slot_table(n);
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                CHECK(!(t[i].registry_name == t[j].registry_name));
            }
        }
    });

    runner.add("slot_table_display_names_not_blank", [](Runner& runner) {
        // 允许中文（只作 UI 展示），但不得为空 —— 实测踩过。
        std::size_t n = 0;
        const SlotSpec* t = slot_table(n);
        std::size_t blank = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (t[i].display_name.empty()) ++blank;
        }
        CHECK(blank == 0);
    });

    runner.add("slot_table_v1_usage_count_matches_design", [](Runner& runner) {
        std::size_t n = 0;
        const SlotSpec* t = slot_table(n);
        std::size_t used = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (t[i].used_in_v1) ++used;
        }
        CHECK(used == kExpectedV1Slots);  // 只有遗留 Size 为 false
    });

    runner.add("slot_registry_name_roundtrip", [](Runner& runner) {
        std::size_t n = 0;
        const SlotSpec* t = slot_table(n);
        std::size_t ok = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (slot_from_registry_name(t[i].registry_name) == t[i].id) ++ok;
            CHECK(find_slot(t[i].id) != nullptr);
        }
        CHECK(ok == n);  // 反查必须整表可逆，否则注册表回读会认错槽位
    });

    runner.add("alias_table_wellformed", [](Runner& runner) {
        std::size_t n = 0;
        const AliasSpec* a = alias_table(n);
        CHECK(n == kExpectedAliases);
        char nb[64];
        for (std::size_t i = 0; i < n; ++i) {
            CHECK(ascii_only(a[i].alias));  // R10：同形字符防线
            // 表里存的必须已是归一形态，否则永远匹配不上（静默少认领）
            std::string_view norm;
            CHECK(normalize_alias(a[i].alias, nb, sizeof(nb), norm) == TokenStatus::Ok);
            CHECK(a[i].alias == norm);
            // 每个别名指向的槽位都得真的存在于槽表
            CHECK(find_slot(a[i].slot) != nullptr);
            for (std::size_t j = i + 1; j < n; ++j) {
                CHECK(!(a[i].alias == a[j].alias));
            }
        }
    });
}

void register_claim_cases(Runner& runner) {
    runner.add("claim_matches_independent_expected_table", [](Runner& runner) {
        char scratch[128];
        std::size_t hits = 0;
        for (const ExpectedAlias& e : kExpectedAliasRows) {
            const ClaimResult r = claim_by_stem(e.alias, scratch, sizeof(scratch));
            if (r.status == ClaimStatus::Claimed && r.slot == e.slot) ++hits;
        }
        CHECK(hits == kExpectedAliasCount);
        CHECK(kExpectedAliasCount == kExpectedAliases);  // 手抄表与实现表同为 43 条（DESIGN §5.2）

        // 双向包含：实现表不得多出/少掉手抄表里的行，且**逐字**相同（含指向的槽位）。
        // 这一条才真正把「别名串被改坏」变成可发现的错误，无论改坏成 ASCII 还是同形字符。
        std::size_t na = 0;
        const AliasSpec* at = alias_table(na);
        CHECK(na == kExpectedAliasCount);
        for (std::size_t i = 0; i < na; ++i) {
            bool documented = false;
            for (const ExpectedAlias& e : kExpectedAliasRows) {
                if (std::string_view(e.alias) == at[i].alias && e.slot == at[i].slot) documented = true;
            }
            CHECK(documented);
        }

        // 覆盖面：期望表命中的槽位集合必须恰好等于「v1 使用的槽位」集合
        std::size_t ns = 0;
        const SlotSpec* t = slot_table(ns);
        std::size_t covered = 0;
        for (std::size_t i = 0; i < ns; ++i) {
            bool found = false;
            for (const ExpectedAlias& e : kExpectedAliasRows) {
                if (e.slot == t[i].id) found = true;
            }
            CHECK(found == t[i].used_in_v1);  // 覆盖 ⟺ v1 使用
            if (found) ++covered;
        }
        CHECK(covered == kExpectedV1Slots);

        // 反向对照：Size 有槽位、无别名 —— 它不该被任何文件名认领上
        CHECK(find_slot(SlotId::Size) != nullptr);
        CHECK(claim_by_stem("size", scratch, sizeof(scratch)).status == ClaimStatus::NotInTable);
    });

    // 这条只验证「表内每行的映射自洽」，对**别名字符串本身被改坏无效**（见上方注释），
    // 保留它是为了抓映射错位，不能替代上一条。
    runner.add("claim_every_alias_hits_its_own_slot", [](Runner& runner) {
        std::size_t n = 0;
        const AliasSpec* a = alias_table(n);
        char scratch[128];
        std::size_t hits = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const ClaimResult r = claim_by_stem(a[i].alias, scratch, sizeof(scratch));
            if (r.status == ClaimStatus::Claimed && r.slot == a[i].slot) ++hits;
        }
        CHECK(hits == n);      // 正向：全表可命中
        CHECK(n == kExpectedAliases);
    });

    runner.add("claim_accepts_real_file_shapes", [](Runner& runner) {
        char scratch[128];
        CHECK(claim_by_stem("Arrow.png", scratch, sizeof(scratch)).slot == SlotId::Arrow);
        CHECK(claim_by_stem("sub/dir/nw-resize.svg", scratch, sizeof(scratch)).slot == SlotId::SizeNW);
        CHECK(claim_by_stem("top left corner.cur", scratch, sizeof(scratch)).slot == SlotId::SizeNW);
        CHECK(claim_by_stem("se_resize.CUR", scratch, sizeof(scratch)).slot == SlotId::SizeSE);
        // 边界外行为不写测试：含空格的 stem（"  IBeam  "）归一后成 "__ibeam__"，
        // 属未定义清洗规则，留给 M2 的扫描层决定，此处不假装已知。
        CHECK(claim_by_stem("hand.cur", scratch, sizeof(scratch)).slot == SlotId::Hand);
    });

    runner.add("claim_rejects_instead_of_guessing", [](Runner& runner) {
        char scratch[128];
        CHECK(claim_by_stem("not_a_slot.png", scratch, sizeof(scratch)).status == ClaimStatus::NotInTable);
        CHECK(claim_by_stem("arrowx", scratch, sizeof(scratch)).status == ClaimStatus::NotInTable);   // 前缀/后缀不误命中
        // ".png" 这类点开头名字：索引 0 的点不当后缀分隔，结果是 stem=".png" 归一为 "_png"
        // —— 判「不在表里」并报告，比判「空 stem」更贴近实现，也更可诊断。
        CHECK(claim_by_stem(".png", scratch, sizeof(scratch)).status == ClaimStatus::NotInTable);
        CHECK(claim_by_stem("", scratch, sizeof(scratch)).status == ClaimStatus::EmptyStem);
    });

    runner.add("claim_too_long_is_refused_not_truncated", [](Runner& runner) {
        char scratch[16];
        const std::string long_name(60, 'a');
        CHECK(claim_by_stem(long_name, scratch, sizeof(scratch)).status == ClaimStatus::TooLong);
        // 反向对照：同样的内容放进够大的缓冲就能正常判「不在表里」，
        // 说明 TooLong 是容量决定，不是匹配逻辑失败。
        char big[128];
        CHECK(claim_by_stem(long_name, big, sizeof(big)).status == ClaimStatus::NotInTable);
    });
}

void register_hotspot_cases(Runner& runner) {
    runner.add("default_hotspot_always_in_bounds", [](Runner& runner) {
        // 对每个槽 x 每档尺寸都验边界：公式类 bug（HandTip 的 w*3/10 等）在这里红。
        const uint32_t sizes[] = {1, 2, 3, 16, 32, 48, 255, 256};
        const std::size_t arity = sizeof(sizes) / sizeof(sizes[0]);
        std::size_t ns = 0;
        const SlotSpec* t = slot_table(ns);
        std::size_t visited = 0;
        for (std::size_t i = 0; i < ns; ++i) {
            for (std::size_t r = 0; r < arity; ++r) {
                for (std::size_t c = 0; c < arity; ++c) {
                    const uint32_t w = sizes[c];
                    const uint32_t h = sizes[r];
                    const Hotspot hs = default_hotspot(t[i].rule, w, h);
                    CHECK(hs.x < w);
                    CHECK(hs.y < h);
                    ++visited;
                }
            }
        }
        CHECK(visited == ns * arity * arity);  // 自证：真的走满了，没有中途少跑
    });

    runner.add("default_hotspot_degenerate_size_is_not_a_valid_cursor", [](Runner& runner) {
        // 0 尺寸时退化到 (0,0)，但这样的 spec 必须被 IR 拒掉，不能交付。
        const Hotspot hs = default_hotspot(HotspotRule::Center, 0, 0);
        CursorSpec s = one_frame_spec(0, 0, 255, hs);
        CHECK(validate_spec(s).status == SpecStatus::ZeroDimension);
    });
}

void register_ir_negative_cases(Runner& runner) {
    runner.add("ir_accepts_a_valid_spec", [](Runner& runner) {
        const CursorSpec s = one_frame_spec(4, 4, 255, Hotspot{0, 0});
        CHECK(validate_spec(s).status == SpecStatus::Ok);
    });

    runner.add("ir_rejects_empty_frames", [](Runner& runner) {
        CursorSpec s;
        s.hotspot = Hotspot{0, 0};
        CHECK(validate_spec(s).status == SpecStatus::EmptyFrames);
    });

    runner.add("ir_rejects_bad_pixel_buffer", [](Runner& runner) {
        CursorSpec s;
        s.hotspot = Hotspot{0, 0};
        s.frames.push_back(make_frame(4, 4, solid(4, 3, 255)));  // 少一行
        const SpecError e = validate_spec(s);
        CHECK(e.status == SpecStatus::PixelBufferSize);
        CHECK(e.frame_index == 0);
    });

    runner.add("ir_rejects_oversize_and_zero", [](Runner& runner) {
        CHECK(validate_spec(one_frame_spec(257, 8, 255, Hotspot{0, 0})).status == SpecStatus::SizeTooLarge);
        CHECK(validate_spec(one_frame_spec(8, 0, 255, Hotspot{0, 0})).status == SpecStatus::ZeroDimension);
    });

    runner.add("ir_rejects_out_of_bounds_hotspot", [](Runner& runner) {
        CHECK(validate_spec(one_frame_spec(4, 4, 255, Hotspot{4, 0})).status == SpecStatus::HotspotOutOfBounds);
        CHECK(validate_spec(one_frame_spec(4, 4, 255, Hotspot{0, 9})).status == SpecStatus::HotspotOutOfBounds);
    });

    runner.add("ir_rejects_not_premultiplied", [](Runner& runner) {
        // straight-alpha 的典型形态：alpha 半透明但颜色仍是满值（B=255 > A=128）。
        std::vector<uint8_t> px(4 * 4 * 4, 0);
        for (std::size_t i = 0; i < px.size(); i += 4) {
            px[i + 0] = 255;  // B
            px[i + 3] = 128;  // A  -> B > A，违预乘
        }
        CursorSpec s;
        s.hotspot = Hotspot{0, 0};
        s.frames.push_back(make_frame(4, 4, px));
        CHECK(validate_spec(s).status == SpecStatus::NotPremultiplied);
    });

    runner.add("ir_rejects_fully_transparent_unless_debug", [](Runner& runner) {
        const CursorSpec s = one_frame_spec(8, 8, 0, Hotspot{4, 4});  // 全 alpha=0
        CHECK(validate_spec(s).status == SpecStatus::FullyTransparent);

        ValidateOptions dbg;
        dbg.reject_fully_transparent = false;
        CHECK(validate_spec(s, dbg).status == SpecStatus::Ok);  // 开关只影响这一条判据
    });

    runner.add("ir_256_frame_is_valid", [](Runner& runner) {
        // 256 是目录项 0 的那一档，IR 层必须接受（编码转换属 writer，见 H6）
        const CursorSpec s = one_frame_spec(256, 256, 255, Hotspot{255, 255});
        CHECK(validate_spec(s).status == SpecStatus::Ok);
        CHECK(any_visible_pixel(s));
    });
}

}  // namespace cs::test
