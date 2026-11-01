// src/tests/cases.h — 跨测试文件注册函数（保持 tests_main 是唯一 main）
#pragma once

#include "test/harness.h"

namespace cs::test {

// tests_tables.cpp：数据表守卫 + 认领 + IR 不变量
void register_table_cases(Runner& runner);
void register_claim_cases(Runner& runner);
void register_hotspot_cases(Runner& runner);
void register_ir_negative_cases(Runner& runner);

// tests_cur.cpp：CUR 容器（writer 对照独立实现的 golden + reader 反向核验）
void register_container_cases(Runner& runner);

}  // namespace cs::test
