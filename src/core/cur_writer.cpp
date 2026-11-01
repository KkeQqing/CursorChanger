#include "core/cur_writer.h"

#include "core/cur_format.h"

#include <algorithm>

namespace cs::core {
namespace {

std::size_t xor_row_bytes(uint32_t w) { return static_cast<std::size_t>(w) * 4u; }

// AND 掩码：1bpp，每行按 32 位对齐（H3）。
std::size_t and_row_bytes_exact(uint32_t w) {
    return (static_cast<std::size_t>(w) + 31u) / 32u * 4u;
}

// 该帧实际要写的字节数（不含 min_dir_gap_bytes 的补齐）。
std::size_t frame_raw_bytes(uint32_t w, uint32_t h, const WriteOptions& o) {
    const std::size_t and_row = std::max(and_row_bytes_exact(w), o.min_frame_bytes);
    return kDibHeaderSize + xor_row_bytes(w) * h + and_row * h;
}

// 目录项登记的长度 = 实际写入长度。二者必须相等（H9），所以这里算出的值
// 必须由写循环**真的写满**，而不是只拿去填目录项 —— 上一版就是这么错的。
std::size_t frame_entry_bytes(uint32_t w, uint32_t h, const WriteOptions& o) {
    return std::max(frame_raw_bytes(w, h, o), o.min_dir_gap_bytes);
}

void put_u16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void put_u32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

void put_i32(std::vector<uint8_t>& b, int32_t v) {
    put_u32(b, static_cast<uint32_t>(v));
}

// 目录项的尺寸字节：256 编码为 0（H6）。其余值必须 <=255。
uint8_t size_byte(uint32_t dimension) {
    return dimension == kMaxCursorDimension ? static_cast<uint8_t>(0)
                                            : static_cast<uint8_t>(dimension);
}

}  // namespace

WriteError write_cursor_bytes(const CursorSpec& spec, std::vector<uint8_t>& out,
                              const WriteOptions& opts) {
    WriteError err;

    ValidateOptions vo;
    vo.reject_fully_transparent = opts.reject_fully_transparent;
    err.spec_error = validate_spec(spec, vo);
    if (err.spec_error.status != SpecStatus::Ok) {
        err.status = WriteStatus::InvalidSpec;
        err.frame_index = err.spec_error.frame_index;
        return err;
    }

    // 目录头的 count 是 uint16：先于 max_dir_entries 兜住硬上限，
    // 否则配置写大会静默截断帧数。
    if (spec.frames.size() > opts.max_dir_entries || spec.frames.size() > 0xFFFFu) {
        err.status = WriteStatus::TooManyFrames;
        return err;
    }

    // IR 约定 frames 已按面积升序（H8）。不在这里代排序：静默重排会让调用方
    // 误以为自己的顺序有效。
    for (std::size_t i = 1; i < spec.frames.size(); ++i) {
        const std::size_t prev = static_cast<std::size_t>(spec.frames[i - 1].height) * spec.frames[i - 1].width;
        const std::size_t cur = static_cast<std::size_t>(spec.frames[i].height) * spec.frames[i].width;
        if (cur < prev) {
            err.status = WriteStatus::FramesUnordered;
            err.frame_index = i;
            return err;
        }
    }

    std::vector<std::size_t> entry_bytes;
    entry_bytes.reserve(spec.frames.size());
    std::size_t total = 0;
    for (const Frame& f : spec.frames) {
        const std::size_t n = frame_entry_bytes(f.width, f.height, opts);
        entry_bytes.push_back(n);
        total += n;
    }

    out.clear();
    out.reserve(kDirHeaderBytes + spec.frames.size() * kDirEntryBytes + total);

    // ---- ICONDIR ----
    put_u16(out, 0);                      // reserved
    put_u16(out, kIconTypeCursor);         // type = 2（H1）
    put_u16(out, static_cast<uint16_t>(spec.frames.size()));

    // ---- 目录项 ----
    uint32_t offset = static_cast<uint32_t>(kDirHeaderBytes + spec.frames.size() * kDirEntryBytes);
    for (std::size_t i = 0; i < spec.frames.size(); ++i) {
        const Frame& f = spec.frames[i];
        out.insert(out.end(), size_byte(f.width));
        out.insert(out.end(), size_byte(f.height));
        out.insert(out.end(), 0);  // color_count
        out.insert(out.end(), 0);  // reserved
        put_u16(out, f.hotspot.x);   // 帧级热点（U15）：每个目录项写自己那一个
        put_u16(out, f.hotspot.y);
        put_u32(out, static_cast<uint32_t>(entry_bytes[i]));
        put_u32(out, offset);
        offset += static_cast<uint32_t>(entry_bytes[i]);
    }

    // ---- 每帧：BITMAPINFOHEADER + XOR(底向上) + AND(底向上，全 0) + 补齐 ----
    for (std::size_t i = 0; i < spec.frames.size(); ++i) {
        const Frame& f = spec.frames[i];
        const std::size_t and_row = std::max(and_row_bytes_exact(f.width), opts.min_frame_bytes);
        const std::size_t xor_total = xor_row_bytes(f.width) * f.height;
        const std::size_t written_before_pad = out.size();

        put_u32(out, kDibHeaderSize);                                     // bmiHeader 大小
        put_i32(out, static_cast<int32_t>(f.width));                      // biWidth（真实值，不用 0 表 256）
        put_i32(out, static_cast<int32_t>(f.height) * 2);                 // H2：biHeight = 2h
        put_u16(out, kDibPlanes);                                         // H7
        put_u16(out, kDibBitCount32);                                     // H7
        put_u32(out, kBiRgb);                                             // D7：不产 BI_BITFIELDS
        put_u32(out, static_cast<uint32_t>(xor_total));                   // U14：XOR 字节数
        put_i32(out, 0);                                                  // 无物理分辨率信息
        put_i32(out, 0);
        put_u32(out, 0);                                                  // colors_used
        put_u32(out, 0);                                                  // colors_important

        // XOR：top-down 的 IR 行序翻转成底向上；每像素 B,G,R,A 已是预乘 BGRA32。
        const std::size_t row = xor_row_bytes(f.width);
        for (uint32_t r = f.height; r > 0; --r) {
            const std::size_t begin = static_cast<std::size_t>(r - 1) * row;
            out.insert(out.end(), f.pixels.begin() + static_cast<std::ptrdiff_t>(begin),
                       f.pixels.begin() + static_cast<std::ptrdiff_t>(begin + row));
        }

        // AND：32bpp 带 alpha 的光标必须全 0（H3）；行按调用方给的下限填充。
        out.insert(out.end(), and_row * f.height, static_cast<uint8_t>(0));

        // H9：把条目补到目录登记的长度。缺了这一步，bytes_in_res 就会大于实际写入字节，
        // 下一帧的偏移随之整体错位，而 writer 仍会返回 Ok（静默产出坏文件）。
        const std::size_t actually_written = out.size() - written_before_pad;
        if (actually_written > entry_bytes[i]) {
            out.clear();  // 不许产出半坏文件；这是内部不变量被破坏，宁可空输出+报错
            err.status = WriteStatus::InternalLengthMismatch;
            err.frame_index = i;
            return err;
        }
        out.insert(out.end(), entry_bytes[i] - actually_written, static_cast<uint8_t>(0));
    }

    // H9：总长必须精确等于 头 + 目录 + Σ条目，不得有多余尾部字节。
    const std::size_t expected =
        kDirHeaderBytes + spec.frames.size() * kDirEntryBytes + total;
    if (out.size() != expected) {
        out.clear();
        err.status = WriteStatus::InternalLengthMismatch;
        return err;
    }

    err.status = WriteStatus::Ok;
    return err;
}

}  // namespace cs::core
