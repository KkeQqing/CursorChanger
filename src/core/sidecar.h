// src/core/sidecar.h — `<stem>.meta` sidecar 的**纯解析**（DESIGN §5.3）。
//
// 只做字符串 → 结构化字段，不碰文件、不碰 Win32（§2 层规则；读文件在 apply/ 层）。
// 契约：未知 key → 报错（不静默忽略）；值类型/范围不对 → 报错。§5.3「只允许承载参数，
// 不允许承载身份」——身份仍只由文件名 stem 决定，这里绝不读槽位名。
//
// 当前支持 hotspot / size / trim。动画键 frames / rate 属 D21b，随 .ani writer 一起加，
// 现在出现会按「未知 key」拒掉，绝不假装已支持。
#pragma once

#include "core/ir.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cs::core {

enum class SidecarStatus : uint8_t {
    Ok,
    Empty,        // 没有有效字段（视为无 sidecar，调用方可忽略）
    BadSyntax,    // 行不是 key = value
    UnknownKey,   // 出现了未定义的键（§5.3：未知 key 必须报错）
    BadValue,     // 键认得，但值解析不了 / 越界
};

struct Sidecar {
    bool has_hotspot{false};
    Hotspot hotspot{};                 // 以「源尺寸」为单位（覆盖 §5.3 默认规则）

    bool has_size{false};
    std::vector<uint32_t> sizes;       // 覆盖 D8 默认档位 {16,32,48}；仅做下采样，不放大

    bool has_trim{false};
    std::string trim;                  // 仅 SVG：none | auto
};

struct SidecarParse {
    SidecarStatus status{SidecarStatus::Ok};
    std::string detail;                // 失败时可读原因（含 offending token）
};

// 解析 sidecar 文本。`;` 之后为行内注释；空行跳过。out 仅在返回 Ok 时有意义。
SidecarParse parse_sidecar(const std::string& text, Sidecar& out);

}  // namespace cs::core
