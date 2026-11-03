// src/core/sidecar.h — `<stem>.meta` sidecar 的**纯解析**（DESIGN §5.3）。
//
// 只做字符串 → 结构化字段，不碰文件、不碰 Win32（§2 层规则；读文件在 apply/ 层）。
// 契约：未知 key → 报错（不静默忽略）；值类型/范围不对 → 报错。§5.3「只允许承载参数，
// 不允许承载身份」——身份仍只由文件名 stem 决定，这里绝不读槽位名。
//
// 支持 hotspot / size / trim（静态通道）与 frames / rate（D21b 动画合成）。
// frames= 出现即令调用方走 .ani 合成；rate= 为每帧 jiffies（1/60 秒），非毫秒。
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

    // 动画合成（D21b）：出现非空 frames= 即改走 .ani 合成路径，主文件仅用于定槽位。
    bool has_frames{false};
    std::vector<std::string> frames;   // 帧文件（相对 sidecar 同目录），按顺序 = 动画帧序
    bool has_rate{false};
    uint32_t rate{0};                  // 每帧 jiffies（1/60s）；0 视为未给，由调用方用默认
};

struct SidecarParse {
    SidecarStatus status{SidecarStatus::Ok};
    std::string detail;                // 失败时可读原因（含 offending token）
};

// 解析 sidecar 文本。`;` 之后为行内注释；空行跳过。out 仅在返回 Ok 时有意义。
SidecarParse parse_sidecar(const std::string& text, Sidecar& out);

}  // namespace cs::core
