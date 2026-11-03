// src/core/debounce.h — watcher 的「事件流 → 重放时刻」纯函数（DESIGN §7.5 / M7 判据①）。
//
// 无状态、无时钟、无 Win32：喂一串「毫秒时间戳事件」（假定非递减），吐一串「应触发重放的毫秒时刻」。
// 规则 = 尾部去抖 + 最大等待封顶：
//   - 同一串间隔都 < debounce_ms 的事件合并成一次重放，触发时刻 = 末事件 + debounce_ms；
//   - 但一个突发从首事件起最长只能被推到 first + max_wait_ms 再触发（防「事件永不停→永不重放」的饿死）；
//   - 到封顶点仍来事件，则就地结算、把该事件当作新一突发的开始。
// 半写文件/风暴的「只认最终名」不在这里（那是 §7.1 的 D11 原子替换 + 第2步接线），本函数只管合并节奏。
#pragma once

#include <cstdint>
#include <vector>

namespace cs::core {

// debounce_ms / max_wait_ms 单位同事件时间戳（毫秒）。约定 max_wait_ms >= debounce_ms；
// 若调用方传 max_wait_ms < debounce_ms，按「不封顶」处理（等于把封顶设成无限）。
std::vector<uint64_t> compute_replay_times(const std::vector<uint64_t>& event_ms,
                                           uint64_t debounce_ms, uint64_t max_wait_ms);

// 有状态版：事件随时间增量到达时用（watcher 的 UI 线程正是这种）。语义与 compute_replay_times
// **完全同一**——后者即「喂一串事件给 Debouncer、在 due 时收集 deadline」的批式封装，二者共用
// 同一实现，杜绝「离线测一套、线上跑另一套」的漂移（本项目老教训）。
class Debouncer {
public:
    Debouncer(uint64_t debounce_ms, uint64_t max_wait_ms);

    void on_event(uint64_t now_ms);   // 记录一次到达；按封顶规则推进 deadline
    bool due(uint64_t now_ms) const;  // now_ms 是否应触发一次重放
    bool pending() const { return pending_; }
    uint64_t next_deadline() const { return deadline_; }
    void fire(uint64_t at_ms);        // 确认触发：结算本次，并以 at_ms 起新突发

private:
    uint64_t debounce_;
    uint64_t max_wait_;
    bool cap_;
    bool pending_ = false;
    uint64_t first_ = 0;
    uint64_t deadline_ = 0;
};

}  // namespace cs::core
