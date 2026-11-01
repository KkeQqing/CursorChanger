#include "core/cur_reader.h"

#include "core/cur_format.h"
#include "core/ir.h"  // kMaxCursorDimension（0 尺寸字节 == 256 的还原值，H6）

namespace cs::core {
namespace {

inline uint16_t get_u16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8));
}

inline uint32_t get_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t get_i32(const uint8_t* p) { return static_cast<int32_t>(get_u32(p)); }

// 目录项尺寸字节 0 == 256（H6）；DIB 头里则是真实值，两者必须一致。
inline uint32_t real_size(uint8_t byte) {
    return byte == 0 ? kMaxCursorDimension : static_cast<uint32_t>(byte);
}

inline bool bit_count_ok(uint16_t bc) {
    return bc == 1 || bc == 4 || bc == 8 || bc == 24 || bc == kDibBitCount32;
}

inline std::size_t xor_row_bytes(uint32_t w, uint16_t bit_count) {
    // DIB 行距按位深算，并向上取整到 32 位（H3 同一套对齐规则的镜像面）。
    // 上一版无条件用 w*4，对 8/24bpp 的第三方 .cur 会算错 XOR 长度，
    // 于是把 AND 区起点整段搬错 —— M2 的 passthrough 校验一定会踩。
    if (bit_count == 0) return 0;
    const std::size_t bits_per_row = static_cast<std::size_t>(w) * bit_count;
    return (bits_per_row + 31u) / 32u * 4u;
}

}  // namespace

ReadStatus read_cursor_info(const uint8_t* data, std::size_t size, CursorInfo& out,
                            std::size_t max_entries) {
    out = CursorInfo{};
    if (data == nullptr || size < kDirHeaderBytes) return ReadStatus::TooShort;

    const uint16_t reserved = get_u16(data);
    const uint16_t type = get_u16(data + 2);
    const uint16_t count = get_u16(data + 4);

    if (reserved != 0) return ReadStatus::BadReserved;
    if (type != kIconTypeCursor) return ReadStatus::WrongType;  // H1
    if (count == 0) return ReadStatus::ZeroCount;
    if (static_cast<std::size_t>(count) > max_entries) return ReadStatus::CountOverflow;

    const std::size_t dir_end = kDirHeaderBytes + static_cast<std::size_t>(count) * kDirEntryBytes;
    if (size < dir_end) return ReadStatus::Truncated;  // 无意义 length 字段最常见的暴露点

    out.type = type;
    out.count = count;
    out.entries.reserve(count);

    std::size_t expect_offset = dir_end;
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* e = data + kDirHeaderBytes + static_cast<std::size_t>(i) * kDirEntryBytes;

        CursorEntryInfo info;
        info.width = real_size(e[0]);
        info.height = real_size(e[1]);
        info.hotspot_x = get_u16(e + 4);
        info.hotspot_y = get_u16(e + 6);
        info.bytes_in_res = get_u32(e + 8);
        info.image_offset = get_u32(e + 12);

        if (info.image_offset < dir_end) return ReadStatus::OverlapsDir;  // H8
        if (info.image_offset != expect_offset) return ReadStatus::OffsetNotContiguous;
        if (static_cast<std::size_t>(info.image_offset) + info.bytes_in_res > size) {
            return ReadStatus::Truncated;
        }
        // H5：目录项热点必须落在该帧尺寸内
        if (info.hotspot_x >= info.width || info.hotspot_y >= info.height) {
            return ReadStatus::BadHotspot;
        }

        const uint8_t* h = data + info.image_offset;
        if (info.bytes_in_res < kDibHeaderSize) return ReadStatus::BadHeaderSize;
        // 先确认这确实是一个 40 字节的 BITMAPINFOHEADER，再从中取字段：
        // 上一版是「先读四个字段、再验 biSize」，边界虽然安全但顺序是坏味道，
        // 一旦有人改动上面的 bytes_in_res 校验就会变成真的越界读。
        if (get_u32(h) != kDibHeaderSize) return ReadStatus::BadHeaderSize;

        info.bi_width = static_cast<uint32_t>(get_i32(h + 4));
        info.bi_height = static_cast<uint32_t>(get_i32(h + 8));
        info.planes = get_u16(h + 12);
        info.bit_count = get_u16(h + 14);

        if (static_cast<uint32_t>(info.bi_height) != info.height * 2u) {
            return ReadStatus::BadDoubleHeight;  // H2
        }
        if (info.bi_width != info.width) return ReadStatus::BadWidth;
        if (info.planes != kDibPlanes) return ReadStatus::BadPlanes;  // H7
        if (!bit_count_ok(info.bit_count)) return ReadStatus::BadBitCount;

        if (info.bit_count == kDibBitCount32) {
            // H3：32bpp 走 alpha，AND 掩码必须全 0，否则透明区会变成实心块
            const std::size_t xbytes = xor_row_bytes(info.width, info.bit_count) * info.height;
            if (info.bytes_in_res < kDibHeaderSize + xbytes) return ReadStatus::Truncated;
            const std::size_t and_start = static_cast<std::size_t>(info.image_offset) + kDibHeaderSize + xbytes;
            const std::size_t and_len = static_cast<std::size_t>(info.bytes_in_res) - kDibHeaderSize - xbytes;
            for (std::size_t k = 0; k < and_len; ++k) {
                if (data[and_start + k] != 0) return ReadStatus::BadAndMask;
            }
        }

        expect_offset = static_cast<std::size_t>(info.image_offset) + info.bytes_in_res;
        out.entries.push_back(info);
    }

    out.trailing_bytes = size - expect_offset;
    return ReadStatus::Ok;
}

bool entry_layout(const CursorInfo& info, std::size_t frame, const uint8_t* data, std::size_t size,
                  EntryLayout& out) {
    out = EntryLayout{};
    if (data == nullptr || frame >= info.entries.size()) return false;

    const CursorEntryInfo& e = info.entries[frame];
    const std::size_t xbytes = xor_row_bytes(e.width, e.bit_count) * e.height;
    if (e.bytes_in_res < kDibHeaderSize) return false;
    const std::size_t payload = e.bytes_in_res - kDibHeaderSize;
    if (payload < xbytes) return false;

    const std::size_t abytes = payload - xbytes;
    if (e.height == 0 || abytes % e.height != 0) return false;

    out.xor_offset = static_cast<std::size_t>(e.image_offset) + kDibHeaderSize;
    out.xor_bytes = xbytes;
    out.and_offset = out.xor_offset + xbytes;
    out.and_bytes = abytes;
    out.and_row_bytes = abytes / e.height;

    if (out.and_offset + out.and_bytes > size) return false;
    return true;
}

bool frame_pixel_at(const CursorInfo& info, const uint8_t* data, std::size_t size, std::size_t frame,
                    uint32_t x, uint32_t y, uint8_t out_bgra[4]) {
    if (out_bgra == nullptr || frame >= info.entries.size()) return false;
    const CursorEntryInfo& e = info.entries[frame];
    if (e.bit_count != kDibBitCount32) return false;  // 其余位深不在 v1 解码范围
    if (x >= e.width || y >= e.height) return false;

    EntryLayout l;
    if (!entry_layout(info, frame, data, size, l)) return false;

    // 容器是底向上：top-down 的第 y 行落在物理行 (height-1-y)（H11）
    const uint32_t phys_row = e.height - 1u - y;
    const std::size_t addr = l.xor_offset + static_cast<std::size_t>(phys_row) * xor_row_bytes(e.width, e.bit_count) +
                            static_cast<std::size_t>(x) * 4u;
    if (addr + 4 > size) return false;

    out_bgra[0] = data[addr + 0];
    out_bgra[1] = data[addr + 1];
    out_bgra[2] = data[addr + 2];
    out_bgra[3] = data[addr + 3];
    return true;
}

bool and_mask_all_zero(const CursorInfo& info, const uint8_t* data, std::size_t size, std::size_t frame) {
    if (frame >= info.entries.size()) return false;
    if (info.entries[frame].bit_count != kDibBitCount32) return false;

    EntryLayout l;
    if (!entry_layout(info, frame, data, size, l)) return false;
    for (std::size_t i = 0; i < l.and_bytes; ++i) {
        if (data[l.and_offset + i] != 0) return false;
    }
    return true;
}

const char* read_status_name(ReadStatus s) {
    switch (s) {
    case ReadStatus::Ok: return "Ok";
    case ReadStatus::TooShort: return "TooShort";
    case ReadStatus::BadReserved: return "BadReserved";
    case ReadStatus::WrongType: return "WrongType";
    case ReadStatus::ZeroCount: return "ZeroCount";
    case ReadStatus::CountOverflow: return "CountOverflow";
    case ReadStatus::Truncated: return "Truncated";
    case ReadStatus::OffsetNotContiguous: return "OffsetNotContiguous";
    case ReadStatus::OverlapsDir: return "OverlapsDir";
    case ReadStatus::BadHeaderSize: return "BadHeaderSize";
    case ReadStatus::BadDoubleHeight: return "BadDoubleHeight";
    case ReadStatus::BadWidth: return "BadWidth";
    case ReadStatus::BadPlanes: return "BadPlanes";
    case ReadStatus::BadBitCount: return "BadBitCount";
    case ReadStatus::BadHotspot: return "BadHotspot";
    case ReadStatus::BadAndMask: return "BadAndMask";
    }
    return "Unknown";
}

}  // namespace cs::core
