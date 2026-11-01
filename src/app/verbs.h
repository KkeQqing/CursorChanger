// src/app/verbs.h — 命令行动词的实现（CLI 与将来的 GUI 共用同一条链，DESIGN §9.2）
#pragma once

#include <string_view>

namespace cs::app {

// 把 skin 目录应用到真机方案键（HKCU\Control Panel\Cursors）并生效。
// 顺序受 §7.1 约束：L1 不过就不写注册表。
int run_apply(std::string_view skin_dir);

// 还原到首次 apply 前的基线快照。
int run_restore();

// 复位到系统默认方案（不依赖快照的兜底出口）。
int run_reset();

// 一次性的真机闭环：before → apply → L2/L3 → **必定还原** → after 比对。
// 用 RAII 保证「中途任何一步失败也还原」，不依赖人记得再敲一次 restore。
int run_live_check(std::string_view skin_dir);

}  // namespace cs::app
