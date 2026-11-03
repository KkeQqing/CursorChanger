// src/core/ani_format.cpp — 见头文件。RIFF/ACON 合成与反解，纯字节、无 Win32。
#include "core/ani_format.h"

namespace cs::core {
namespace {

inline void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}
inline uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void put_fourcc(std::vector<uint8_t>& v, const char* id) {
    v.push_back(static_cast<uint8_t>(id[0]));
    v.push_back(static_cast<uint8_t>(id[1]));
    v.push_back(static_cast<uint8_t>(id[2]));
    v.push_back(static_cast<uint8_t>(id[3]));
}

// 一个 RIFF chunk：fourcc + u32 size(不含四cc与自身size) + data + 奇数字节补齐(不进 size)。
void put_chunk(std::vector<uint8_t>& v, const char* id, const std::vector<uint8_t>& data) {
    put_fourcc(v, id);
    put32(v, static_cast<uint32_t>(data.size()));
    v.insert(v.end(), data.begin(), data.end());
    if (data.size() & 1u) v.push_back(0);  // 偶对齐填充，不计入 size
}

bool fourcc_is(const uint8_t* p, const char* id) {
    return p[0] == static_cast<uint8_t>(id[0]) && p[1] == static_cast<uint8_t>(id[1]) &&
           p[2] == static_cast<uint8_t>(id[2]) && p[3] == static_cast<uint8_t>(id[3]);
}

}  // namespace

AniWriteError write_ani_bytes(const AniSpec& spec, std::vector<uint8_t>& out) {
    out.clear();
    AniWriteError e;
    if (spec.frames.empty()) {
        e.status = AniWriteStatus::NoFrames;
        e.detail = "no frames";
        return e;
    }
    if (spec.frames.size() > spec.max_frames) {
        e.status = AniWriteStatus::TooManyFrames;
        e.detail = "frame count " + std::to_string(spec.frames.size()) + " exceeds max " +
                   std::to_string(spec.max_frames);
        return e;
    }
    for (std::size_t i = 0; i < spec.frames.size(); ++i) {
        if (spec.frames[i].empty()) {
            e.status = AniWriteStatus::EmptyFrame;
            e.detail = "frame " + std::to_string(i) + " is empty";
            out.clear();
            return e;
        }
    }

    const uint32_t n = static_cast<uint32_t>(spec.frames.size());

    // anih：9×DWORD
    std::vector<uint8_t> anih;
    put32(anih, 36);                 // cbSizeOf（这 9 个 DWORD 的字节数）
    put32(anih, n);                  // nFrames
    put32(anih, n);                  // nSteps（无 seq => 步=帧、顺序即 0..n-1）
    put32(anih, 0);                  // iWidth  = 0（用帧自带尺寸）
    put32(anih, 0);                  // iHeight = 0
    put32(anih, spec.bit_count);     // iBitCount
    put32(anih, spec.planes);        // nPlanes
    put32(anih, spec.rate_jiffies);  // iDispRate（jiffies）
    put32(anih, 0x1);                // bfAttributes bit0=每帧是完整 CUR/ICO 文件

    // LIST 'fram'：'fram' + 每帧一个 icon chunk（帧=完整 .cur）
    std::vector<uint8_t> fram;
    put_fourcc(fram, "fram");
    for (const auto& cur : spec.frames) put_chunk(fram, "icon", cur);

    std::vector<uint8_t> body;
    put_chunk(body, "anih", anih);
    put_chunk(body, "LIST", fram);

    // RIFF 头：size = 4('ACON') + body
    put_fourcc(out, "RIFF");
    put32(out, static_cast<uint32_t>(4 + body.size()));
    put_fourcc(out, "ACON");
    out.insert(out.end(), body.begin(), body.end());

    e.status = AniWriteStatus::Ok;
    return e;
}

AniReadStatus read_ani_info(const uint8_t* data, std::size_t size, AniInfo& out) {
    out = AniInfo{};
    if (size < 12 || !fourcc_is(data, "RIFF") || !fourcc_is(data + 8, "ACON")) {
        return AniReadStatus::NotRiffAcon;
    }
    const std::size_t riff_end = 12;  // 简单以缓冲区实际长度为界，不信 size 字段（H9 精神）
    (void)riff_end;
    bool have_anih = false, have_fram = false;
    std::size_t pos = 12;
    while (pos + 8 <= size) {
        const std::size_t chunk_size = get32(data + pos + 4);
        const std::size_t body = pos + 8;
        if (body + chunk_size > size) return AniReadStatus::Truncated;
        const uint8_t* p = data + body;
        if (fourcc_is(data + pos, "anih") && chunk_size >= 36) {
            // anih 布局（byte offset）：0 cbSizeOf | 4 nFrames | 8 nSteps | 12 iWidth | 16 iHeight
            //                      | 20 iBitCount | 24 nPlanes | 28 iDispRate | 32 bfAttributes
            out.n_frames = get32(p + 4);
            out.n_steps = get32(p + 8);
            out.i_width = get32(p + 12);
            out.i_height = get32(p + 16);
            out.bit_count = get32(p + 20);
            out.planes = get32(p + 24);
            out.rate_jiffies = get32(p + 28);
            out.bf_attributes = get32(p + 32);
            have_anih = true;
        } else if (fourcc_is(data + pos, "LIST") && chunk_size >= 4 && fourcc_is(p, "fram")) {
            std::size_t c = body + 4;
            const std::size_t end = body + chunk_size;
            while (c + 8 <= end) {
                const std::size_t cs = get32(data + c + 4);
                const std::size_t cb = c + 8;
                if (cb + cs > end) break;
                if (fourcc_is(data + c, "icon")) {
                    out.frames.emplace_back(data + cb, data + cb + cs);
                }
                c = cb + cs + (cs & 1u);
            }
            have_fram = true;
        }
        pos = body + chunk_size + (chunk_size & 1u);
    }
    if (!have_anih) return AniReadStatus::MissingAnih;
    if (!have_fram) return AniReadStatus::MissingFram;
    return AniReadStatus::Ok;
}

}  // namespace cs::core
