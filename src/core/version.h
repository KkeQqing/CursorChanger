// src/core/version.h — 版本与构建元数据的唯一出处（见 build.bat 注 5）
// 里程碑版本号与 DESIGN §11 / docs/summaries 的 Vx.y 对应。
#pragma once

namespace cs::core {
inline constexpr const char* kVersion = "0.3.0";
inline constexpr const char* kBuildFlags = "c++17 /MT static-crt / SUBSYSTEM:CONSOLE";
}  // namespace cs::core
