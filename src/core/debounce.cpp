// src/core/debounce.cpp — 见头文件。Debouncer 是单一真身；compute_replay_times 是其批式折叠。
//
// 二者共用同一状态机 → M14 对 compute_replay_times 的用例即同时覆盖 UI 实时用的 Debouncer，
// 杜绝「离线测一套、线上跑另一套」的漂移。
#include "core/debounce.h"

namespace cs::core {
namespace {
constexpr uint64_t NO_CAP = 0xFFFFFFFFFFFFFFFFull;
}

Debouncer::Debouncer(uint64_t debounce_ms, uint64_t max_wait_ms)
    : debounce_(debounce_ms), max_wait_(max_wait_ms), cap_(max_wait_ms >= debounce_ms) {}

void Debouncer::on_event(uint64_t now_ms) {
    if (!pending_) {
        pending_ = true;
        first_ = now_ms;
        deadline_ = now_ms + debounce_;
        return;
    }
    if (now_ms >= deadline_) {
        // 已过计划点却没 fire（实时由定时器兜住，批式由折叠先 fire）→ 视作新突发，
        // 不把过期事件并进旧窗口。
        first_ = now_ms;
        deadline_ = now_ms + debounce_;
        return;
    }
    // 合并窗口内：右移到 now+debounce，但不超过 first+max_wait（封顶，防饿死）。
    const uint64_t candidate = now_ms + debounce_;
    const uint64_t ceiling = cap_ ? (first_ + max_wait_) : NO_CAP;
    deadline_ = candidate < ceiling ? candidate : ceiling;
}

bool Debouncer::due(uint64_t now_ms) const { return pending_ && now_ms >= deadline_; }

void Debouncer::fire(uint64_t at_ms) {
    // 实时语义：本次突发结算完毕，清 pending；下一条事件将另起新突发。
    (void)at_ms;
    pending_ = false;
}

std::vector<uint64_t> compute_replay_times(const std::vector<uint64_t>& event_ms,
                                           uint64_t debounce_ms, uint64_t max_wait_ms) {
    std::vector<uint64_t> replays;
    Debouncer d(debounce_ms, max_wait_ms);
    for (uint64_t t : event_ms) {
        if (d.due(t)) {          // 事件到达前，先结算已到期的一次重放
            replays.push_back(d.next_deadline());
            d.fire(d.next_deadline());
        }
        d.on_event(t);
    }
    if (d.pending()) replays.push_back(d.next_deadline());  // 末次突发在序列结束后触发
    return replays;
}

}  // namespace cs::core
