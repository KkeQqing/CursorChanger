// src/core/debounce.cpp — 见头文件。纯计算，无副作用。
#include "core/debounce.h"

namespace cs::core {

std::vector<uint64_t> compute_replay_times(const std::vector<uint64_t>& event_ms,
                                           uint64_t debounce_ms, uint64_t max_wait_ms) {
    std::vector<uint64_t> replays;
    if (event_ms.empty()) return replays;

    // max_wait 小于 debounce 视为「不封顶」：用哨兵极值，clamp 永远取不到它。
    const bool cap = max_wait_ms >= debounce_ms;
    const uint64_t NO_CAP = 0xFFFFFFFFFFFFFFFFull;

    bool pending = false;
    uint64_t first = 0;     // 本突发首事件时刻
    uint64_t deadline = 0;  // 本突发计划触发时刻（含封顶）

    for (uint64_t t : event_ms) {
        if (!pending) {
            pending = true;
            first = t;
            deadline = t + debounce_ms;
            continue;
        }
        if (t >= deadline) {
            // 上一突发的计划时刻已过 → 先结算它，再把本事件当作新突发起点
            replays.push_back(deadline);
            first = t;
            deadline = t + debounce_ms;
        } else {
            // 仍在合并窗口内：右移到 t+debounce，但不超过 first+max_wait（封顶）
            uint64_t candidate = t + debounce_ms;
            uint64_t ceiling = cap ? (first + max_wait_ms) : NO_CAP;
            deadline = candidate < ceiling ? candidate : ceiling;
        }
    }
    if (pending) replays.push_back(deadline);
    return replays;
}

}  // namespace cs::core
