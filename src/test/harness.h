// src/test/harness.h — 自写极简测试 harness（DESIGN D17：不引 gtest）
//
// 设计约束：
//  * 无全局可变单例：所有状态在 Runner 实例内（AGENTS §6）。
//  * 进程退出码 = 失败断言数（build/tests.bat 依赖它，因此 §9.1 的负对照能成立）。
//  * CHECK(cond) 依赖当前用例函数的形参名为 `runner` —— 见宏定义。
#pragma once

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace cs::test {

class Runner {
public:
    using Fn = void (*)(Runner&);

    void add(std::string_view name, Fn fn) { cases_.push_back(Case{name, fn}); }

    std::size_t case_count() const { return cases_.size(); }
    int checks_passed() const { return checks_passed_; }
    int checks_failed() const { return checks_failed_; }
    int cases_failed() const { return cases_failed_; }

    // 运行全部用例。返回失败断言数（调用方直接用它当 exit code）。
    int run(std::FILE* out) {
        if (out == nullptr) out = stdout;
        out_ = out;
        for (const Case& c : cases_) {
            current_ = c.name;
            const int before = checks_failed_;
            c.fn(*this);
            if (checks_failed_ == before) {
                ++cases_passed_;
                std::fprintf(out_, "PASS   %s\n", str(c.name).c_str());
            } else {
                ++cases_failed_;
                std::fprintf(out_, "FAILED %s\n", str(c.name).c_str());
            }
        }
        std::fprintf(out_, "CASES passed=%d failed=%d  CHECKS passed=%d failed=%d\n",
                     cases_passed_, cases_failed_, checks_passed_, checks_failed_);
        std::fflush(out_);
        return checks_failed_;
    }

    void check(bool ok, std::string_view expr, const char* file, int line) {
        if (ok) {
            ++checks_passed_;
            return;
        }
        ++checks_failed_;
        std::fprintf(out_, "  FAIL  %s : %s  (at %s:%d)\n", str(current_).c_str(),
                     str(expr).c_str(), file, line);
    }

private:
    struct Case {
        std::string_view name;
        Fn fn;
    };

    // string_view 用 %s 打印需要先物化；仅测试路径，分配无所谓。
    static std::string str(std::string_view s) { return std::string(s); }

    std::vector<Case> cases_;
    std::FILE* out_{stdout};
    std::string_view current_{};
    int checks_passed_{0};
    int checks_failed_{0};
    int cases_passed_{0};
    int cases_failed_{0};
};

}  // namespace cs::test

// 约定：用例函数签名必须是 void f(cs::test::Runner& runner)
#define CHECK(cond) runner.check(!!(cond), #cond, __FILE__, __LINE__)
