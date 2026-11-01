// src/tests/cases.h — 跨测试文件注册函数（保持 tests_main 是唯一 main）
#pragma once

#include <cstdio>

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

}  // namespace cs::test
