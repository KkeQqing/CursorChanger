// src/core/cur_reader.h — CUR 容器解析与校验（读元信息，不解码像素）
//
// 用途两条：
//  1) M1：作为与 CurWriter **独立的第二套规则实现**，反向核对 writer 的产物
//     （尺寸还原、双高、掩码、偏移、热点、底向上行序）。
//  2) M2：.cur passthrough 通道的合法性检查（DESIGN §4.1）。
// 像素解码不在这里做：passthrough 不重编码（D5），所以本文件永不需要把 BGRA 还原成 Frame。
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cs::core {

enum class ReadStatus : uint8_t {
    Ok,
    TooShort,            // 连头都读不到
    BadReserved,         // 头 reserved != 0
    WrongType,           // type != 2（H1：例如把 .ico 当 .cur）
    ZeroCount,           // 条目数 0
    CountOverflow,       // 条目数超上限（无意义 length 字段的常见来源）
    Truncated,           // 目录或任一帧数据超出文件长度（H9）
    OffsetNotContiguous, // 帧偏移有空洞或重叠（H8）
    OverlapsDir,         // 第一帧压在目录上
    BadHeaderSize,       // biSize != 40
    BadDoubleHeight,     // biHeight != 2 * 图像高（H2）
    BadWidth,            // biWidth 与目录项不符
    BadPlanes,           // planes != 1（H7）
    BadBitCount,         // bit_count 不在 {1,4,8,24,32}（H7）
    BadHotspot,          // 目录项热点不在该帧尺寸内（H5）
    BadAndMask,          // 32bpp 帧的 AND 掩码含非零字节（H3）
};

struct CursorEntryInfo {
    uint32_t width{0};   // 真实尺寸（目录项的 0 已还原成 256）
    uint32_t height{0};
    uint16_t hotspot_x{0};
    uint16_t hotspot_y{0};
    uint16_t planes{0};
    uint16_t bit_count{0};
    uint32_t bytes_in_res{0};
    uint32_t image_offset{0};
    uint32_t bi_height{0};
    uint32_t bi_width{0};
};

struct CursorInfo {
    uint16_t type{0};
    uint16_t count{0};
    std::vector<CursorEntryInfo> entries;
    std::size_t trailing_bytes{0};  // 文件末尾多余字节：允许但可报告（H9 只禁空洞/重叠）
};

// 帧内部的区段布局（AND 掩码行由 bytes_in_res 反推，兼容不同工具的填充宽度）。
struct EntryLayout {
    std::size_t xor_offset{0};
    std::size_t xor_bytes{0};
    std::size_t and_offset{0};
    std::size_t and_bytes{0};
    std::size_t and_row_bytes{0};
};

// 解析并校验。data 为空或 size==0 返回 TooShort。max_entries 是合理性上限，不是格式要求。
ReadStatus read_cursor_info(const uint8_t* data, std::size_t size, CursorInfo& out,
                            std::size_t max_entries = 64);

bool entry_layout(const CursorInfo& info, std::size_t frame, const uint8_t* data,
                  std::size_t size, EntryLayout& out);

// 32bpp 帧的取点：把容器层的底向上行序换算回 top-down（H11 的核对就靠它）。
bool frame_pixel_at(const CursorInfo& info, const uint8_t* data, std::size_t size,
                   std::size_t frame, uint32_t x, uint32_t y, uint8_t out_bgra[4]);

// 32bpp 帧的 AND 掩码必须全 0（H3）——read_cursor_info 已把它作为接受条件，
// 本函数保留给报告用途（指出「哪一帧的掩码坏了」）。非 32bpp 返回 false（不适用）。
bool and_mask_all_zero(const CursorInfo& info, const uint8_t* data, std::size_t size,
                       std::size_t frame);

const char* read_status_name(ReadStatus s);

}  // namespace cs::core
