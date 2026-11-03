// src/tests/tests_m14.cpp — watcher 去抖/合并纯函数（M7 判据①）。
//
// 判据要求：喂时间戳事件序列 → 断言重放次数/时刻。期望值全是**手算**写死，不是拿实现再算一遍。
// 覆盖：单次、突发合并、隔足够远→两次、封顶防饿死（风暴仍重放且间隔≤max_wait）、
//       空/单点、debounce=0 退化为每事件一次、输出严格单调（自证）。
#include "cases.h"

#include "core/debounce.h"

#include <cstdint>
#include <vector>

using cs::core::compute_replay_times;
using cs::test::Runner;

namespace {

std::vector<uint64_t> v(std::initializer_list<uint64_t> xs) { return std::vector<uint64_t>(xs); }

}  // namespace

namespace cs::test {

void register_debounce_cases(Runner& runner) {
    runner.add("debounce_single_and_merge", [](Runner& runner) {
        // 单事件 t=100, d=50 → 150
        CHECK(compute_replay_times(v({100}), 50, 1000) == v({150}));
        // 突发 100,110,120 都在窗口内合并 → 末+debounce = 170，一次
        CHECK(compute_replay_times(v({100, 110, 120}), 50, 1000) == v({170}));
        // 两个相隔足够远（gap>debounce）→ 两次独立重放
        CHECK(compute_replay_times(v({100, 200}), 50, 1000) == v({150, 250}));
    });

    runner.add("debounce_edge_cases", [](Runner& runner) {
        CHECK(compute_replay_times(std::vector<uint64_t>{}, 50, 1000) == std::vector<uint64_t>{});  // 空
        CHECK(compute_replay_times(v({7}), 0, 0) == v({7}));               // debounce=0 → 事件时刻即触发
        // debounce=0 多事件：每事件一次（不合并）
        CHECK(compute_replay_times(v({7, 20, 35}), 0, 0) == v({7, 20, 35}));
        // max_wait<debounce 视为不封顶：长突发仍合并成一次
        CHECK(compute_replay_times(v({0, 10, 20, 30}), 50, 10 /*<d*/) == v({80}));
    });

    runner.add("debounce_maxwait_prevents_starvation", [](Runner& runner) {
        // 持续风暴：每 10ms 一次，共 0..300。d=50、max_wait=100。
        // 无封顶本会一路合并成「永不触发」；封顶后必须被切成多次重放，且相邻重放间隔≤max_wait。
        std::vector<uint64_t> ev;
        for (uint64_t t = 0; t <= 300; t += 10) ev.push_back(t);
        const auto rp = compute_replay_times(ev, 50, 100);
        CHECK(rp.size() >= 3);   // 关键：风暴没被吞成一次（自证「≥3」，否则安静少测看不出来）
        // 严格单调 + 首次重放不超过 first+max_wait
        CHECK(!rp.empty() && rp[0] <= 100);
        bool monotonic = true, capped = true;
        for (std::size_t i = 1; i < rp.size(); ++i) {
            if (!(rp[i] > rp[i - 1])) monotonic = false;
            if (rp[i] - rp[i - 1] > 100) capped = false;   // 相邻重放间隔不超过 max_wait
        }
        CHECK(monotonic);
        CHECK(capped);
    });

    runner.add("debounce_output_strictly_increasing", [](Runner& runner) {
        // 混合序列：验证任何输入下输出都严格递增（去抖不会产出乱序/相等触发点）。
        const auto rp = compute_replay_times(v({0, 5, 30, 31, 200, 201, 202}), 50, 1000);
        for (std::size_t i = 1; i < rp.size(); ++i) CHECK(rp[i] > rp[i - 1]);
        // 手算：0..31 合并(first0,deadline随事件推到 31+50=81)；200>=81→先结算81，再 200..202→252
        CHECK(rp == v({81, 252}));
    });
}

}  // namespace cs::test
