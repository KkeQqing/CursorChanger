// src/core/claim.h — 文件名到槽位的认领（DESIGN §5.2 别名表的代码对应物）
//
// 契约：
//  * 文件名只回答「我是哪个槽位」，不承载 hotspot/尺寸等参数（D10）。
//  * 未命中不是「跳过」，而是一个可报告的状态（§4 共同契约：禁止静默缩水）。
//  * 别名表是数据；加一条别名只加一行。
#pragma once

#include "core/slots.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cs::core {

enum class ClaimStatus : uint8_t {
    Claimed,     // 命中别名表
    NotInTable,  // 归一后不在表里：必须报告，不得静默忽略
    EmptyStem,   // 文件名去后缀后为空
    TooLong,     // 超出 scratch 容量：拒绝，不做截断匹配（防前缀误命中）
};

struct AliasSpec {
    std::string_view alias;  // 归一形态：小写、连接符为 '_'，必须纯 ASCII
    SlotId slot;
};

struct ClaimResult {
    ClaimStatus status{ClaimStatus::NotInTable};
    SlotId slot{SlotId::Invalid};
    std::size_t alias_index{0};  // 命中表内第几行（可观察，便于报告「匹配了什么」）
};

const AliasSpec* alias_table(std::size_t& count);

enum class TokenStatus : uint8_t {
    Ok,
    Empty,    // 输入为空，或去后缀后为空
    TooLong,  // 超出 cap：拒绝，绝不截断后继续匹配
};

// 归一：转小写，并把 '-'、'.'、' ' 一律当作 '_'。
// out 指向 buf（同长度、同位置的原地变换，可安全传同一块缓冲）。
TokenStatus normalize_alias(std::string_view in, char* buf, std::size_t cap, std::string_view& out);

// 取文件名（可含目录）去后缀后的 stem。目录之外的清洗不在此层。
TokenStatus stem_of(std::string_view filename, char* buf, std::size_t cap, std::string_view& out);

// 认领：stem -> 归一 -> 查别名表。scratch 由调用方持有（无全局态）。
ClaimResult claim_by_stem(std::string_view filename_or_stem, char* scratch, std::size_t cap);

}  // namespace cs::core
