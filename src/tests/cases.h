// src/tests/cases.h — 跨测试文件注册函数（保持 tests_main 是唯一 main）
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "test/harness.h"

namespace cs::test {

// tests_tables.cpp：数据表守卫 + 认领 + IR 不变量
void register_table_cases(Runner& runner);
void register_claim_cases(Runner& runner);
void register_hotspot_cases(Runner& runner);
void register_ir_negative_cases(Runner& runner);

// tests_cur.cpp：CUR 容器（writer 对照独立实现的 golden + reader 反向核验）
void register_container_cases(Runner& runner);

// tests_l1_probe.cpp：M2a 的加载器实测。probe 模式打印观测值，断言等看过真值再写。
void probe_l1_load(std::FILE* out);
void register_l1_cases(Runner& runner);

// tests_registry_probe.cpp：只读探查注册表光标方案键，为 U1 取佐证（不核销 U1）
void probe_registry_names(std::FILE* out);
void register_registry_cases(Runner& runner);

// tests_m2.cpp：注册表写侧 / 三态基线 / 还原 / 端到端流水线，全部走沙箱键
void register_apply_cases(Runner& runner);

// emit_skin.cpp：造一套真机测试皮肤（15 槽 x 16/32/48），供 --live-check 使用。
// 返回写出的文件数；失败返回 false。目录参数用 wstring（Windows 路径）。
bool emit_live_skin(const std::wstring& dir, std::uint32_t& files_out);
// 命令行入口用：把本机 ANSI 码页的参数转成宽字符路径（ASCII 路径无损；非 ASCII 属已知限制）
bool emit_live_skin_narrow(const char* dir, std::uint32_t& files_out);

// tests_m3.cpp：PNG 通道（解码 / 预乘 / 缩放取整方向 / 拒绝路径）
void register_png_cases(Runner& runner);

// tests_m4.cpp：SVG 通道（原始输出探针 / 特性守卫 / 贴边 / 端到端）
void register_svg_cases(Runner& runner);

// tests_m5.cpp：M5a GUI/CLI 共用表现模型 + 动作意图（真解析器 / 真槽表为 oracle）
void register_ui_cases(Runner& runner);

// tests_m6.cpp：三通道端到端集成（png/svg 正例 + 配对负对照 + .cur 透传回归 + 坏源不写注册表）
void register_channel_e2e_cases(Runner& runner);

// tests_m7.cpp：对真实 skin/ 的离线自检（build_only：全链覆盖/冲突/逐帧/L1 + 部分认领 + 全透明负对照）
void register_real_skin_cases(Runner& runner);

// tests_m8.cpp：构建产物子系统守卫（D18 V0.6.2：app=WINDOWS GUI / tests=CONSOLE CUI 的非对称）
void register_build_image_cases(Runner& runner);

// tests_m10.cpp：D21a `.ani` 透传通道（以 OS 加载器为独立 oracle；坏 .ani 挡在写册前）
void register_ani_cases(Runner& runner);

// tests_m11.cpp：§5.3 `.meta` sidecar 纯解析 + 端到端接通（size/hotspot 改变产物，坏 key 挡写册前）
void register_sidecar_cases(Runner& runner);

// tests_m12.cpp：D21b `.ani` 合成 writer/reader（cur_reader + LoadCursorFromFileW 双独立 oracle）
void register_ani_writer_cases(Runner& runner);

// tests_m13.cpp：D23 全局 config/settings.ini 解析 + 端到端生效（含 allow_upscale / 坏文件中止）
void register_settings_cases(Runner& runner);

// tests_m14.cpp：watcher 去抖/合并纯函数（M7 判据①，含封顶防饿死、单调性自证）
void register_debounce_cases(Runner& runner);

}  // namespace cs::test
