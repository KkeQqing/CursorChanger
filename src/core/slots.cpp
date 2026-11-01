#include "core/slots.h"
#include "core/ir.h"

namespace cs::core {
namespace {

// 行序 == SlotId 声明序，且不含 Invalid。由 tests_tables.cpp 的守卫强制。
// registry_name 参与注册表写入与匹配，必须纯 ASCII（R10 同形字符风险，已被测试守卫）。
// display_name 只给 UI 看，允许中文，不参与任何匹配。
constexpr SlotSpec kSlots[] = {
    {SlotId::Arrow, {"Arrow"}, {"正常选择"}, HotspotRule::TipTopLeft, true},
    {SlotId::IBeam, {"IBeam"}, {"文本选择"}, HotspotRule::IBeamTop, true},
    {SlotId::Wait, {"Wait"}, {"忙"}, HotspotRule::TipTopLeft, true},
    {SlotId::Cross, {"Cross"}, {"精确选择"}, HotspotRule::Center, true},
    {SlotId::UpArrow, {"UpArrow"}, {"替代选择"}, HotspotRule::TipTopCenter, true},
    {SlotId::Help, {"Help"}, {"帮助选择"}, HotspotRule::TipTopLeft, true},
    {SlotId::Hand, {"Hand"}, {"链接选择"}, HotspotRule::HandTip, true},
    {SlotId::AppStarting, {"AppStarting"}, {"后台运行"}, HotspotRule::TipTopLeft, true},
    {SlotId::Unavailable, {"Unavailable"}, {"不可用"}, HotspotRule::Center, true},
    {SlotId::Size, {"Size"}, {"调整（遗留）"}, HotspotRule::Center, false},
    {SlotId::SizeNW, {"SizeNW"}, {"左上调整"}, HotspotRule::CornerNorthWest, true},
    {SlotId::SizeSE, {"SizeSE"}, {"右下调整"}, HotspotRule::CornerSouthEast, true},
    {SlotId::SizeNE, {"SizeNE"}, {"右上调整"}, HotspotRule::CornerNorthEast, true},
    {SlotId::SizeSW, {"SizeSW"}, {"左下调整"}, HotspotRule::CornerSouthWest, true},
    {SlotId::SizeWE, {"SizeWE"}, {"水平调整"}, HotspotRule::Center, true},
    {SlotId::SizeNS, {"SizeNS"}, {"垂直调整"}, HotspotRule::Center, true},
    {SlotId::SizeAll, {"SizeAll"}, {"移动"}, HotspotRule::Center, true},
};

}  // namespace

const SlotSpec* slot_table(std::size_t& count) {
    count = sizeof(kSlots) / sizeof(kSlots[0]);
    return kSlots;
}

const SlotSpec* find_slot(SlotId id) {
    std::size_t n = 0;
    const SlotSpec* t = slot_table(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (t[i].id == id) return &t[i];
    }
    return nullptr;
}

SlotId slot_from_registry_name(std::string_view name) {
    std::size_t n = 0;
    const SlotSpec* t = slot_table(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (t[i].registry_name == name) return t[i].id;
    }
    return SlotId::Invalid;
}

Hotspot default_hotspot(HotspotRule rule, uint32_t width, uint32_t height) {
    // 尺寸非法时退化到 (0,0)。调用方不得据此交付：validate_spec 会先把它拒掉。
    if (width == 0 || height == 0) return Hotspot{0, 0};

    const uint32_t cx = width / 2;
    const uint32_t cy = height / 2;
    switch (rule) {
    case HotspotRule::TipTopLeft:
        return Hotspot{0, 0};
    case HotspotRule::TipTopCenter:
        return Hotspot{static_cast<uint16_t>(cx), 0};
    case HotspotRule::IBeamTop:
        return Hotspot{static_cast<uint16_t>(cx), static_cast<uint16_t>(height / 5)};
    case HotspotRule::HandTip:
        return Hotspot{static_cast<uint16_t>(width * 3 / 10), static_cast<uint16_t>(height / 5)};
    case HotspotRule::Center:
        return Hotspot{static_cast<uint16_t>(cx), static_cast<uint16_t>(cy)};
    case HotspotRule::CornerNorthWest:
        return Hotspot{0, 0};
    case HotspotRule::CornerSouthEast:
        return Hotspot{static_cast<uint16_t>(width - 1), static_cast<uint16_t>(height - 1)};
    case HotspotRule::CornerNorthEast:
        return Hotspot{static_cast<uint16_t>(width - 1), 0};
    case HotspotRule::CornerSouthWest:
        return Hotspot{0, static_cast<uint16_t>(height - 1)};
    case HotspotRule::Invalid:
        return Hotspot{0, 0};
    }
    return Hotspot{0, 0};
}

}  // namespace cs::core
