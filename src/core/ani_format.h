// src/core/ani_format.h — D21b：RIFF/ACON 动画光标(.ani) 的合成 writer + 反解 reader（DESIGN §4.4.2）
//
// 纯字节进出，不碰 Win32 / 文件（§2 层规则）；读文件、拼帧由 apply/ 层负责。
// 结构与字段顺序来自权威规范 + 本机 aero_busy.ani 逐字节实测（见 §4.4）：
//   anih = 9×DWORD: cbSizeOf(36), nFrames, nSteps, iWidth, iHeight, iBitCount, nPlanes, iDispRate, bfAttributes
//   - iWidth/iHeight = 0  => 用每帧自带尺寸
//   - iDispRate 单位 = jiffies（1/60 秒），**不是毫秒**
//   - bfAttributes bit0=1 => 每个 'fram' 子块是「一个完整 CUR/ICO 文件」；本实现按此产出
//   - 不用 seq / rate 逐帧块（统一 iDispRate、顺序 0..n-1）
// reader 只为离线**反解核对**（测试用），不当作播放保证 —— 「会不会按这速率动」是 U22，靠真机。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cs::core {

struct AniSpec {
    std::vector<std::vector<uint8_t>> frames;  // 每帧 = 一个完整的 .cur 字节（write_cursor_bytes 的产物）
    uint32_t rate_jiffies{6};                  // 全局每帧停留（jiffies）；等待类常用 6~8
    uint32_t bit_count{32};
    uint32_t planes{1};
    uint32_t max_frames{1000};                 // 荒谬上限（不是 U7 的 OS 真上限），只为防溢出式失控；越界报错不截断
};

enum class AniWriteStatus : uint8_t {
    Ok,
    NoFrames,        // 帧列表为空
    TooManyFrames,   // 超 max_frames（宁可失败，不静默截断，H9）
    EmptyFrame,      // 某帧字节为空
};
struct AniWriteError {
    AniWriteStatus status{AniWriteStatus::Ok};
    std::string detail;
};

// 合成。out 仅在 Ok 时被写；非 Ok 时 out 被清空（绝不交半坏文件）。
AniWriteError write_ani_bytes(const AniSpec& spec, std::vector<uint8_t>& out);

struct AniInfo {
    uint32_t n_frames{0};
    uint32_t n_steps{0};
    uint32_t i_width{0};
    uint32_t i_height{0};
    uint32_t bit_count{0};
    uint32_t planes{0};
    uint32_t rate_jiffies{0};
    uint32_t bf_attributes{0};
    std::vector<std::vector<uint8_t>> frames;  // 反解出的每帧内嵌 .cur 字节
};

enum class AniReadStatus : uint8_t {
    Ok,
    NotRiffAcon,
    Truncated,
    MissingAnih,
    MissingFram,
};

// 反解。用于测试核对（结构自洽）；不代表 OS 会按此播放。
AniReadStatus read_ani_info(const uint8_t* data, std::size_t size, AniInfo& out);

}  // namespace cs::core
