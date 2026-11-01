#include "core/claim.h"

#include <cctype>

namespace cs::core {
namespace {

// 行内容必须与 DESIGN §5.2 的别名表逐条一致（守卫测 ASCII、测重复、测非空，
// 但「一致」本身是人工核对项 —— 见 M1 总结的「一致靠约定」小节，不作反射比对）。
constexpr AliasSpec kAliases[] = {
    {{"arrow"}, SlotId::Arrow},
    {{"normal"}, SlotId::Arrow},
    {{"ibeam"}, SlotId::IBeam},
    {{"text"}, SlotId::IBeam},
    {{"beam"}, SlotId::IBeam},
    {{"wait"}, SlotId::Wait},
    {{"busy"}, SlotId::Wait},
    {{"loading"}, SlotId::Wait},
    {{"cross"}, SlotId::Cross},
    {{"precision"}, SlotId::Cross},
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
    {{"no"}, SlotId::Unavailable},
    {{"not_allowed"}, SlotId::Unavailable},
    {{"denied"}, SlotId::Unavailable},
    {{"unavailable"}, SlotId::Unavailable},
    {{"move"}, SlotId::SizeAll},
    {{"sizeall"}, SlotId::SizeAll},
    {{"ew_resize"}, SlotId::SizeWE},
    {{"sizewe"}, SlotId::SizeWE},
    {{"ns_resize"}, SlotId::SizeNS},
    {{"sizens"}, SlotId::SizeNS},
    {{"nw_resize"}, SlotId::SizeNW},
    {{"sizenw"}, SlotId::SizeNW},
    {{"top_left_corner"}, SlotId::SizeNW},
    {{"se_resize"}, SlotId::SizeSE},
    {{"sizese"}, SlotId::SizeSE},
    {{"bottom_right_corner"}, SlotId::SizeSE},
    {{"ne_resize"}, SlotId::SizeNE},
    {{"sizene"}, SlotId::SizeNE},
    {{"top_right_corner"}, SlotId::SizeNE},
    {{"sw_resize"}, SlotId::SizeSW},
    {{"sizesw"}, SlotId::SizeSW},
    {{"bottom_left_corner"}, SlotId::SizeSW},
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
