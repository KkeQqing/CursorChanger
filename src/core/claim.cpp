#include "core/claim.h"

#include <cctype>

namespace cs::core {
namespace {

// 行内容与 DESIGN §5.2 的别名表逐条一致（守卫测 ASCII / 重复 / 已归一 / 槽位真实存在，
// 但「与文档一致」本身靠人工核对，见 M1 总结的「一致靠约定」小节，不作反射比对）。
//
// 对角槽各有 6 条别名，是因为实测确认 Windows 只有 SizeNWSE / SizeNESW 两个值
// （原先以为是四个方向各一个）。后果：素材包里 `nw_resize.png` 与 `se_resize.png`
// 会**同时认领同一个槽**。认领函数如实返回该槽，重复冲突由 M2b 的扫描器负责报告
// （规则见 §5.2「重复认领必须报告，不得任选其一」）—— 这里不静默挑选。
constexpr AliasSpec kAliases[] = {
    {{"arrow"}, SlotId::Arrow},
    {{"normal"}, SlotId::Arrow},
    {{"ibeam"}, SlotId::IBeam},
    {{"text"}, SlotId::IBeam},
    {{"beam"}, SlotId::IBeam},
    {{"wait"}, SlotId::Wait},
    {{"busy"}, SlotId::Wait},
    {{"loading"}, SlotId::Wait},
    {{"cross"}, SlotId::Crosshair},
    {{"crosshair"}, SlotId::Crosshair},
    {{"precision"}, SlotId::Crosshair},
    {{"up"}, SlotId::UpArrow},
    {{"alternate"}, SlotId::UpArrow},
    {{"uparrow"}, SlotId::UpArrow},
    {{"help"}, SlotId::Help},
    {{"question"}, SlotId::Help},
    {{"question_arrow"}, SlotId::Help},
    {{"hand"}, SlotId::Hand},
    {{"link"}, SlotId::Hand},
    {{"pointer"}, SlotId::Hand},
    {{"appstarting"}, SlotId::AppStarting},
    {{"working_busy"}, SlotId::AppStarting},
    {{"no"}, SlotId::No},
    {{"not_allowed"}, SlotId::No},
    {{"denied"}, SlotId::No},
    {{"unavailable"}, SlotId::No},
    {{"move"}, SlotId::SizeAll},
    {{"sizeall"}, SlotId::SizeAll},
    {{"ew_resize"}, SlotId::SizeWE},
    {{"sizewe"}, SlotId::SizeWE},
    {{"ns_resize"}, SlotId::SizeNS},
    {{"sizens"}, SlotId::SizeNS},
    {{"nw_resize"}, SlotId::SizeNWSE},
    {{"sizenw"}, SlotId::SizeNWSE},
    {{"se_resize"}, SlotId::SizeNWSE},
    {{"sizese"}, SlotId::SizeNWSE},
    {{"top_left_corner"}, SlotId::SizeNWSE},
    {{"bottom_right_corner"}, SlotId::SizeNWSE},
    {{"ne_resize"}, SlotId::SizeNESW},
    {{"sizene"}, SlotId::SizeNESW},
    {{"sw_resize"}, SlotId::SizeNESW},
    {{"sizesw"}, SlotId::SizeNESW},
    {{"top_right_corner"}, SlotId::SizeNESW},
    {{"bottom_left_corner"}, SlotId::SizeNESW},
    {{"nwpen"}, SlotId::NWPen},
};

}  // namespace

const AliasSpec* alias_table(std::size_t& count) {
    count = sizeof(kAliases) / sizeof(kAliases[0]);
    return kAliases;
}

TokenStatus normalize_alias(std::string_view in, char* buf, std::size_t cap, std::string_view& out) {
    out = {};
    if (in.empty()) return TokenStatus::Empty;
    if (buf == nullptr || cap == 0 || in.size() > cap - 1) return TokenStatus::TooLong;

    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '-' || c == '.' || c == ' ') c = '_';
        buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    buf[in.size()] = '\0';
    out = std::string_view(buf, in.size());  // 与 in 同长度同位置，可原地读写
    return TokenStatus::Ok;
}

TokenStatus stem_of(std::string_view filename, char* buf, std::size_t cap, std::string_view& out) {
    out = {};

    std::size_t base = 0;
    for (std::size_t i = 0; i < filename.size(); ++i) {
        if (filename[i] == '/' || filename[i] == '\\') base = i + 1;
    }
    const std::string_view name = filename.substr(base);
    if (name.empty()) return TokenStatus::Empty;

    // 最后一个 '.' 之后是后缀；位于索引 0 的 '.'（隐藏文件式命名）不当作后缀分隔。
    std::size_t dot = std::string_view::npos;
    for (std::size_t i = name.size(); i > 1; --i) {
        if (name[i - 1] == '.') {
            dot = i - 1;
            break;
        }
    }
    const std::string_view stem = (dot == std::string_view::npos) ? name : name.substr(0, dot);
    if (stem.empty()) return TokenStatus::Empty;
    if (buf == nullptr || cap == 0 || stem.size() > cap - 1) return TokenStatus::TooLong;

    for (std::size_t i = 0; i < stem.size(); ++i) buf[i] = stem[i];
    buf[stem.size()] = '\0';
    out = std::string_view(buf, stem.size());
    return TokenStatus::Ok;
}

ClaimResult claim_by_stem(std::string_view filename_or_stem, char* scratch, std::size_t cap) {
    ClaimResult r;
    std::string_view stem;
    const TokenStatus s = stem_of(filename_or_stem, scratch, cap, stem);
    if (s == TokenStatus::Empty) {
        r.status = ClaimStatus::EmptyStem;
        return r;
    }
    if (s == TokenStatus::TooLong) {
        r.status = ClaimStatus::TooLong;
        return r;
    }

    std::string_view norm;
    const TokenStatus n = normalize_alias(stem, scratch, cap, norm);
    if (n == TokenStatus::TooLong) {
        r.status = ClaimStatus::TooLong;
        return r;
    }

    std::size_t count = 0;
    const AliasSpec* table = alias_table(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (table[i].alias == norm) {
            r.status = ClaimStatus::Claimed;
            r.slot = table[i].slot;
            r.alias_index = i;
            return r;
        }
    }
    r.status = ClaimStatus::NotInTable;
    return r;
}

}  // namespace cs::core
