// src/selfcheck/verify_live.h — L2 配置级 与 L3 生效级自检
//
// 分工（DESIGN §8）：L2 证明「注册表写对了」，L3 证明「系统指针真的换成这一帧」。
// L2 全绿而 L3 不符 = 静默失败，必须报失败 —— 这是本项目最重要的一条判据分层。
#pragma once

#include "apply/registry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cs::selfcheck {

// ---------- L2 ----------
struct ExpectedSlot {
    std::wstring registry_name;  // 空串 = 键的默认值
    std::wstring expected_path;  // 期望写进去的绝对路径
};

struct L2Mismatch {
    std::wstring registry_name;
    std::wstring expected;
    std::wstring actual;
    bool actual_missing{false};  // 值根本不存在
};

struct L2Result {
    uint32_t checked{0};
    uint32_t matched{0};
    std::vector<L2Mismatch> mismatches;
    bool ok() const { return mismatches.empty() && checked > 0 && matched == checked; }
};

L2Result l2_verify_registry(apply::Root root, const wchar_t* sub_key,
                            const std::vector<ExpectedSlot>& expected);

// ---------- L3 ----------
// 做法：把物理光标移到指定点 -> GetCursorInfo 取当前生效句柄 -> DrawIconEx 画进 32bpp
// 内存 DIB -> 交回像素，由调用方判定。U3 的全部不确定性都在这条路径上，因此这里
// 只**报告观测**，把「算不算通过」留给判据层，避免把猜测实现成断言。
struct L3Capture {
    bool got_cursor{false};
    uint32_t x{0};
    uint32_t y{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t opaque_pixels{0};    // alpha>0 的像素数：用来区分「箭头」与「实心块」
    uint32_t distinct_colors{0};  // 颜色多样性（我们的测试皮肤是纯白实心）
    uint64_t fingerprint{0};      // 图像指纹；用「apply 前后是否变化」作最强判据
    std::vector<uint8_t> pixels;  // 持有采到的 top-down BGRA，使 l3_evaluate 能做真差值
    std::string detail;           // 失败原因（可诊断，不靠 GetLastError 猜）
};

// 期望帧：BGRA32 预乘、top-down。用于算「与期望的最大/平均通道差」。
struct L3Verdict {
    L3Capture capture;
    bool size_matches_expected{false};
    uint32_t max_channel_diff{0};
    uint32_t mean_diff{0};
    bool changed_vs(const L3Capture& before) const {
        // 成员在 capture 里，不在 verdict 自身：直接写 fingerprint 会编译不过，
        // 更糟的是若哪天两边同名就会静默比错对象。显式走 capture。
        return capture.fingerprint != before.fingerprint || capture.width != before.width ||
               capture.height != before.height;
    }
};

L3Capture l3_capture_cursor_at(uint32_t x, uint32_t y, uint32_t settle_ms);

// 与期望帧比较。expected 为空时只回报观测值（size_matches_expected=false，不当判据）。
L3Verdict l3_evaluate(const L3Capture& cap, const std::vector<uint8_t>& expected_bgra,
                     uint32_t expected_w, uint32_t expected_h);

}  // namespace cs::selfcheck
