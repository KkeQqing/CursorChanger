// src/core/cli.h — 命令行解析（纯逻辑，不含 Win32；DESIGN §2 层规则）
//
// 动词表是唯一表述处：加一个动词 = 只在 cli.cpp 的表里加一行，不得新增 if/switch。
// M0 只登记真正存在的动词；`apply` / `restore` 等在 M1-M4 落地时才进表
// （不预告未实现的能力 —— 未知动词会被拒绝，这本身就是可验证的行为）。
#pragma once

#include <cstddef>
#include <string_view>

namespace cs::core {

enum class Verb { None, Help, Version };

struct VerbSpec {
    std::string_view name;
    Verb verb;
    std::string_view summary;
};

// 返回表首地址与元素数（静态存储期，调用方不得释放）。
const VerbSpec* verb_table(std::size_t& count);

enum class Status { Ok,
                    Unknown };

struct Parsed {
    Status status{Status::Ok};
    Verb verb{Verb::None};
    std::string_view unknown{};  // status == Unknown 时有值
};

// 解析 argv[1]（可带 "--" / "-" 前缀，大小写不敏感）。不持有 argv。
// argc < 2，或 argv[1] 去掉前导 '-' 后为空串 => {Ok, None}，表示「没有动词」，
// 由调用方决定默认行为。
// 已知限制（写在此以免被误读成完备实现）：ASCII-only 大小写折叠，
// 非 ASCII 字节按原样比较；动词表本身由测试守卫为纯 ASCII。
Parsed parse_args(int argc, const char* const* argv);

}  // namespace cs::core
