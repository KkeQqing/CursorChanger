// src/core/slots.h — 槽位表与 hotspot 规则组（DESIGN §5.2 / §5.3 的代码对应物）
//
// 契约：槽位、别名、默认规则一律是**数据**。加一条别名或加一个槽位 = 只加一行，
// 若需要动 if/switch 即为契约违背（AGENTS §6）。
// 注册表值名当前按公开文档列出，**状态 U1 未实测**，M2 用程序枚举核销。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cs::core {

enum class SlotId : uint8_t {
    Arrow,
    IBeam,
    Wait,
    Cross,
    UpArrow,
    Help,
    Hand,
    AppStarting,
    Unavailable,
    Size,  // 遗留槽位：登记但不参与 MVP 验收（DESIGN §11 V1.0 判据「16 个 v1 槽位，不含遗留 Size」）
    SizeNW,
    SizeSE,
    SizeNE,
    SizeSW,
    SizeWE,
    SizeNS,
    SizeAll,
    Invalid,  // 必须是最后一项：表守卫用「除 Invalid 外与索引同序」做不变量
};

// 默认规则用「组」而不是每槽一个函数指针：仍然是一张表，且新增槽位只需复用已有组。
enum class HotspotRule : uint8_t {
    TipTopLeft,       // (0, 0)
    TipTopCenter,     // (w/2, 0)
    IBeamTop,         // (w/2, h/5)
    HandTip,          // (w*3/10, h/5)
    Center,           // (w/2, h/2)
    CornerNorthWest,  // (0, 0)
    CornerSouthEast,  // (w-1, h-1)
    CornerNorthEast,  // (w-1, 0)
    CornerSouthWest,  // (0, h-1)
    Invalid,
};

struct SlotSpec {
    SlotId id;
    std::string_view registry_name;  // HKCU\Control Panel\Cursors 的值名（参与写入，必须纯 ASCII）
    std::string_view display_name;   // 仅 UI 展示，可含非 ASCII，不参与任何匹配
    HotspotRule rule;
    bool used_in_v1;
};

// 表首地址与行数（静态存储期）。行序必须与 SlotId 声明序一致 —— 由测试守卫强制。
const SlotSpec* slot_table(std::size_t& count);

// 按 id 查行；未找到返回 nullptr。
const SlotSpec* find_slot(SlotId id);

// 按注册表值名查 id；未找到返回 SlotId::Invalid。
SlotId slot_from_registry_name(std::string_view registry_name);

// 按规则组求默认热点（帧尺寸已知）。v0 初值，U8 目测校正前不作权威值。
struct Hotspot;  // 定义在 core/ir.h，这里仅前置声明以避免循环包含
Hotspot default_hotspot(HotspotRule rule, uint32_t width, uint32_t height);

}  // namespace cs::core
