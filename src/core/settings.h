// src/core/settings.h — config/settings.ini 的**纯解析**（DESIGN §10 / D23）。
//
// 只做字符串 → 结构，不读文件、不碰 Win32（§2：读文件在 apply/pipeline 层）。
// 契约沿用 §5.3：只认表列 key，**未知 key 报错**（不静默忽略，防拼写错悄悄失效）；值类型不对也报错。
// 这里解析的是「全局默认档位」，会被更具体的 `<stem>.meta size=` 覆盖（优先级 meta > settings > 内置）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cs::core {

// 与光标容器一致：单帧尺寸上限 256（H6：目录项 1 字节，0 表 256）。
inline constexpr uint32_t kSettingsMaxDimension = 256;

struct Settings {
    bool has_frames_sizes{false};
    std::vector<uint32_t> frames_sizes;      // 覆盖内置 {16,32,48}

    bool has_debounce_ms{false};
    uint64_t debounce_ms{0};                 // §10 watcher.debounce_ms
    bool has_max_wait_ms{false};
    uint64_t max_wait_ms{0};                 // §10 watcher.max_wait_ms
};

enum class SettingsStatus : uint8_t { Ok, Empty, BadSyntax, UnknownKey, BadValue };

struct SettingsParse {
    SettingsStatus status{SettingsStatus::Ok};
    std::string detail;
};

// 目前只认 frames.sizes（逗号 1..256）。allow_upscale 不开放：PNG/SVG 通道只做下采样
// （§4.2.2「放大一律禁止」无放大实现），置 true 只会让「档位>原生」变 NoUsableFrame，
// 故按未知 key 拒；等真有放大实现再开（见 §11 / BACKLOG）。
SettingsParse parse_settings(const std::string& text, Settings& out);

}  // namespace cs::core
