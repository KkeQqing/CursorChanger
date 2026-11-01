// src/core/cli.h — 命令行解析（纯逻辑，不含 Win32；DESIGN §2 层规则）
//
// 动词表是唯一表述处：加一个动词 = 只在 cli.cpp 的表里加一行，不得新增 if/switch。
// 表里只登记**真的实现了**的动词（`convert` 之类等实现落地再进表），
// 未知动词被拒绝 —— 这本身就是可验证的行为，不预告未实现的能力。
#pragma once

#include <cstddef>
#include <string_view>

namespace cs::core {

enum class Verb { None, Help, Version, Apply, Restore, Reset, LiveCheck };

// 动词是否需要一个目录参数（apply / live-check 需要皮肤目录）。
// 仍然是表数据：加动词时顺带声明 arity，不在 main 里写 if(verb==...)。
enum class Arity : uint8_t { None, NeedDir };

struct VerbSpec {
    std::string_view name;
    Verb verb;
    std::string_view summary;
    Arity arity{Arity::None};
};

// 返回表首地址与元素数（静态存储期，调用方不得释放）。
const VerbSpec* verb_table(std::size_t& count);
// 查某个动词需要的参数个数；未知动词返回 None。
Arity verb_arity(Verb v);

enum class Status { Ok,
                    Unknown,
                    MissingOperand };  // 动词要求目录参数但没给

struct Parsed {
    Status status{Status::Ok};
    Verb verb{Verb::None};
    std::string_view operand{};    // 目录等参数（原样，未做路径处理）
    std::string_view unknown{};    // status == Unknown 时有值
};

// 解析 argv[1]（可带 "--" / "-" 前缀，大小写不敏感），可选 argv[2] 作 operand。不持有 argv。
// argc < 2，或 argv[1] 去掉前导 '-' 后为空串 => {Ok, None}，表示「没有动词」，
// 由调用方决定默认行为。
// 已知限制（写在此以免被误读成完备实现）：ASCII-only 大小写折叠，
// 非 ASCII 字节按原样比较；动词表本身由测试守卫为纯 ASCII。
// operand 可以是裸路径（不要求前导横线），因为 Windows 路径常含冒号/反斜杠。
Parsed parse_args(int argc, const char* const* argv);

}  // namespace cs::core
