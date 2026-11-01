// src/core/cur_writer.h — CursorSpec -> .cur 字节（只产 DIB 条目，不产 PNG 压缩条目，D7）
//
// 纯函数：输入内存、输出内存，不碰文件、不碰 Win32（§2 层规则）。
// 任何不合格输入都返回具体错误码，**不做修正**（§4 共同契约：禁止静默夹带）。
#pragma once

#include "core/ir.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cs::core {

enum class WriteStatus : uint8_t {
    Ok,
    InvalidSpec,            // 由 validate_spec 给出细节
    TooManyFrames,          // 超过 writer.max_dir_entries，或超过目录头 uint16 的硬上限
    FramesUnordered,        // 违反 H8 的 IR 约定：不在 writer 里代排序，静默重排会误导调用方
    InternalLengthMismatch, // 实写字节与登记长度不符（H9）。清空输出并报错，绝不交半坏文件
};

struct WriteError {
    WriteStatus status{WriteStatus::Ok};
    SpecError spec_error{};  // status == InvalidSpec 时有意义
    std::size_t frame_index{0};
};

struct WriteOptions {
    // 默认值即 DESIGN §10 参数总表的「默认」列。M1 阶段没有配置文件读取点，
    // 这里就是唯一来源；settings.ini 落地后由加载器覆盖，本结构只保留兜底默认。
    std::size_t max_dir_entries{12};
    std::size_t min_frame_bytes{128};   // AND 掩码最小行填充（H3）
    std::size_t min_dir_gap_bytes{64};  // 帧数据偏移最小间隔（H8）
    bool reject_fully_transparent{true};  // H10
};

WriteError write_cursor_bytes(const CursorSpec& spec, std::vector<uint8_t>& out,
                              const WriteOptions& opts = {});

}  // namespace cs::core
