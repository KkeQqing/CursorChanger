// src/core/svg_guard.h — SVG 特性守卫：nanosvg 不支持的构造必须**拒绝渲染并报因**
//
// 为什么这是 M4 的核心而不是附属品：nanosvg 对不认识的元素/属性是**静默忽略**——
// 它会渲染出一张"看起来正常但少画了东西"的图，而且尺寸、色彩都对，只有内容和用户
// 想的不一样。对一个天天盯着的鼠标指针来说，这比直接报错危险得多（DESIGN §4.3.2 红线）。
//
// 策略是**白名单**：只放行确认支持的结构，其余一律违规。
// 白名单之外的元素（包括我们没听过的）默认拒绝 —— 宁可拒一个其实能画的图，
// 也不要放过一个被静默裁掉内容的图。漏放行的代价远大于误拒。
#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace cs::core {

struct GuardViolation {
    std::string token;   // 命中的元素名或属性名
    std::string reason;  // 为什么拒
};

struct GuardResult {
    bool ok{true};
    std::vector<GuardViolation> violations;  // 全部列出，不只第一个
    std::string detail;                      // 人读的一句话摘要
};

// 只读扫描 SVG 文本（不改内容、不解析几何）。空输入返回 ok=false。
GuardResult svg_guard_scan(const std::string& svg_text);

// 表的可观察性：两个集合直接暴露给测试，用于断言一致性。
// 动机是一条真实的违规制造结果：从黑名单里摘掉 text 后测试**没有变红**，
// 因为 allow-list 的「未知即拒」补上了这一层 —— 也就是「被拒」不等于「被哪一层拒」。
// 若同一元素同时出现在两表，行为就取决于检查顺序，那必须当场变红。
const std::set<std::string>& svg_guard_allow_elements();
const std::set<std::string>& svg_guard_block_elements();

}  // namespace cs::core
