// src/core/slots.h — 槽位表与 hotspot 规则组（DESIGN §5.2 / §5.3 的代码对应物）
//
// 契约：槽位、别名、默认规则一律是**数据**。加一条别名或加一个槽位 = 只加一行，
// 若需要动 if/switch 即为契约违背（AGENTS §6）。
//
// 值名来源：M2b 在本机（Win11 22631）`HKCU\Control Panel\Cursors` 实测枚举。
// 更早那版凭文档记忆写的表有 4 处错名（Cross / Unavailable / SizeNW+SizeSE / SizeNE+SizeSW），
// 并漏掉一个真实存在的槽 NWPen —— 写错值名是静默无效的，所以这一列必须来自实测而非记忆。
// 仍未证明的事（U1 的剩余部分）：注册表里存在某名字 ≠ 系统接受我们写的任意名字。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cs::core {

enum class SlotId : uint8_t {
    Arrow,
    IBeam,
    Wait,
    UpArrow,
    Help,
    Hand,
    AppStarting,
    Crosshair,
    No,
    SizeNWSE,   // 一个槽同时服务西北与右下两端（镜像行为见 U19）
    SizeNESW,   // 同上，东北与西南
    SizeWE,
    SizeNS,
    SizeAll,
    NWPen,      // 手写笔：M2b 实测新增，原计划没有
    Invalid,    // 必须是最后一项：表守卫用「除 Invalid 外与索引同序」做不变量
};

// 默认规则用「组」而不是每槽一个函数指针：仍然是一张表，且新增槽位只需复用已有组。
enum class HotspotRule : uint8_t {
    TipTopLeft,       // (0, 0)
    TipTopCenter,     // (w/2, 0)
    IBeamTop,         // (w/2, h/5)
    HandTip,          // (w*3/10, h/5)
    Center,           // (w/2, h/2)
    CornerNorthWest,  // (0, 0)      —— SizeNWSE 以西北端为基准
    CornerNorthEast,  // (w-1, 0)    —— SizeNESW 以东北端为基准
    Invalid,
};

struct SlotSpec {
    SlotId id;
    std::string_view registry_name;  // 参与注册表写入与查表，必须纯 ASCII（R10 守卫）
    std::string_view display_name;   // 仅 UI 展示，允许中文，不参与任何匹配
    HotspotRule rule;
    bool used_in_v1;
    bool observed_on_this_machine;   // true = 值名来自本机注册表实测；false = 仅凭文档，尚未证实
};

// 表首地址与行数（静态存储期）。行序必须与 SlotId 声明序一致 —— 由测试守卫强制。
const SlotSpec* slot_table(std::size_t& count);

// 按 id 查行；未找到返回 nullptr。
const SlotSpec* find_slot(SlotId id);

// 按注册表值名查 id；未找到返回 SlotId::Invalid。
SlotId slot_from_registry_name(std::string_view registry_name);

// v1 使用且已实测确认的槽位数（判据与报告都用这个数，避免各处再各写一个常量）。
std::size_t slot_table_v1_count();

// 按规则组求默认热点（帧尺寸已知）。初值，U8/U17 目测校正前不作权威值。
struct Hotspot;  // 定义在 core/ir.h，这里前置声明以避免循环包含
Hotspot default_hotspot(HotspotRule rule, uint32_t width, uint32_t height);

}  // namespace cs::core
