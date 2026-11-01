// src/core/cur_format.h — CUR 容器的字节级布局（DESIGN §6 H1-H11）
//
// 这里的结构体是**布局文件与尺寸证明**：writer 与 reader 都逐字段显式读写小端整数，
// 不把缓冲区强转成这些类型（避免依赖对齐与填充行为）。
#pragma once

#include <cstddef>
#include <cstdint>

namespace cs::core {

// ICONDIR 头：6 字节
struct IconDirHeader {
    uint16_t reserved;  // 必须为 0
    uint16_t type;      // 2 = cursor，1 = icon（H1）
    uint16_t count;     // 目录项数
};

// 目录项：16 字节。.cur 下两个 WORD 是热点（H5）；
// 与 ICO 的 wPlanes/wBitCount 同位 —— 该语义为本项目的关键假设，见 DESIGN U12（M2 实测）。
struct CursorDirEntry {
    uint8_t width;       // 0 表示 256（H6）
    uint8_t height;      // 0 表示 256（H6）
    uint8_t color_count; // 32bpp 下为 0
    uint8_t reserved;    // 0
    uint16_t hotspot_x;  // U12
    uint16_t hotspot_y;  // U12
    uint32_t bytes_in_res;
    uint32_t image_offset;
};

// BITMAPINFOHEADER：40 字节
struct DibHeader {
    uint32_t size;
    int32_t width;
    int32_t height;     // = 2 * 图像高（H2：XOR + AND 合为一张）
    uint16_t planes;    // 1（H7）
    uint16_t bit_count; // v1 只产 32（H7）
    uint32_t compression;
    uint32_t image_size;  // 写 XOR 字节数（U14）
    int32_t x_pixels_per_meter;
    int32_t y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
};

inline constexpr uint16_t kIconTypeIcon = 1;
inline constexpr uint16_t kIconTypeCursor = 2;
inline constexpr uint32_t kDibHeaderSize = 40;
inline constexpr uint32_t kBiRgb = 0;
inline constexpr uint16_t kDibPlanes = 1;
inline constexpr uint16_t kDibBitCount32 = 32;

// 容器骨架尺寸（writer 与 reader 共用，避免两边各写一份而漂移）
inline constexpr std::size_t kDirHeaderBytes = 6;
inline constexpr std::size_t kDirEntryBytes = 16;

static_assert(sizeof(IconDirHeader) == 6, "ICONDIR must pack to 6 bytes");
static_assert(sizeof(CursorDirEntry) == 16, "ICONDIRENTRY/CURSORDIRENTRY must pack to 16 bytes");
static_assert(sizeof(DibHeader) == 40, "BITMAPINFOHEADER must pack to 40 bytes");

}  // namespace cs::core
