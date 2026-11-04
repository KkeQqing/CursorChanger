// src/core/version.h — 版本与构建元数据的唯一出处（见 build.bat 注 5）
// 里程碑版本号与 DESIGN §11 / docs/summaries 的 Vx.y 对应；
// 规矩（AGENTS §5）：kVersion 必须与当前 tag 一致 —— tag Vx.y 写成 "x.y.0"，tag Vx.y.z 写成 "x.y.z"。
#pragma once

namespace cs::core {
inline constexpr const char* kVersion = "1.0.0";
inline constexpr const char* kBuildFlags = "c++17 /MT static-crt / SUBSYSTEM:WINDOWS(app)+CONSOLE(tests)";
}  // namespace cs::core
