// src/apply/baseline.h — 应用前的三态基线快照与两路还原（DESIGN §7.2 / §7.3）
//
// 三态必须区分，否则「还原」会造出第三种状态：
//   key_absent   整个键不存在        -> 还原 = 什么都不写（不能创建键）
//   value_absent 键在、该值不存在     -> 还原 = 删除该值（写空串是错的）
//   value_present 值存在             -> 还原 = 写回原字符串
// 快照文件只在**首次 apply** 创建、之后不覆盖，并把旧文件留成 .prev —— 否则
// 「上一次我自己写进去的路径」会被当成原始默认，用户就再也回不到系统光标了（R5）。
#pragma once

#include "apply/registry.h"
#include "core/slots.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cs::apply {

enum class SlotState : uint8_t { KeyAbsent, ValueAbsent, ValuePresent };

struct SlotSnapshot {
    core::SlotId slot{core::SlotId::Invalid};
    SlotState state{SlotState::KeyAbsent};
    std::wstring value;  // 仅 ValuePresent 有意义
};

struct NamedSnapshot {
    std::wstring name;  // 空串 = 该键的默认值（方案显示名）
    SlotState state{SlotState::ValueAbsent};
    std::wstring value;
};

struct Baseline {
    Root root{Root::CurrentUser};
    std::wstring sub_key;                  // 真机是 L"Control Panel\\Cursors"；测试用沙箱键
    bool key_absent_before{false};
    std::vector<SlotSnapshot> slots;       // 每个 v1 槽位一条，顺序同槽表
    // 同一键下还有两个不属于槽位、但会被我们改动的值：默认值（方案显示名）与 Scheme。
    // 不快照它们的话，「还原」就不是真还原（R5）。
    std::vector<NamedSnapshot> extras;
};

// 只读采集。不会对注册表做任何修改。
Baseline capture_baseline(Root root, const wchar_t* sub_key);

// 文本格式：制表符分隔，第一行是键状态。路径含冒号与反斜杠，故不用 '=' 做分隔。
bool save_baseline(const Baseline& b, const wchar_t* path, std::wstring& error);
bool load_baseline(Baseline& out, const wchar_t* path, std::wstring& error);

// 还原到快照（D13 的动作一）。返回失败槽位数；出错时也会把已成功的部分留下，
// 因为「半还原」比「全不还原」更接近用户期望，但必须报告计数。
struct RestoreResult {
    uint32_t restored{0};
    uint32_t failed{0};
    uint32_t skipped{0};  // 快照里本来就没有 -> 不写
};
RestoreResult restore_baseline(const Baseline& b);

// 复位系统默认方案（D13 的动作二）：删掉全部 v1 槽位的自定义值与方案名，
// 不依赖快照，是兜底出口。
RestoreResult reset_to_default_scheme(Root root, const wchar_t* sub_key);

}  // namespace cs::apply
