// src/tests/tests_main.cpp — M0 测试链
//
// 本文件的关键不是「测了多少」，而是证明这套 harness 能变红：
//  * 用例 7 用嵌套 Runner 断言「故意失败的用例确实被计入失败」；
//  * 进程退出码 = 失败断言数；
//  * `--force-fail` 追加一条必败用例（tests.bat 断言此时 exit != 0）。
// 没有这两条，任何「一片绿」都不算证据（AGENTS §4）。
#include "cases.h"
#include "core/cli.h"
#include "test/harness.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>

using cs::core::Parsed;
using cs::core::Arity;
using cs::core::Status;
using cs::core::Verb;
using cs::core::VerbSpec;
using cs::test::Runner;

namespace {

// Windows 空设备：吞掉嵌套 probe 的输出，只保留断言计数。
// 上一轮把 fopen 换成 _wfopen 只是把 C4996 换了个函数名（警告照旧），
// 真正无声的是 secure 家族 _wfopen_s。
std::FILE* sink_open() {
    std::FILE* f = nullptr;
    if (::_wfopen_s(&f, L"nul", L"w") != 0) return nullptr;
    return f;
}

std::FILE* probe_out(std::FILE*& sink) {
    sink = sink_open();
    return sink ? sink : stderr;
}

// ---------- CLI 解析：正例 ----------

void cli_no_args(Runner& runner) {
    const char* argv[] = {"cursorswitch"};
    const Parsed p = cs::core::parse_args(1, argv);
    CHECK(p.status == Status::Ok);
    CHECK(p.verb == Verb::None);
}

void cli_help_and_version(Runner& runner) {
    const char* a1[] = {"x", "--help"};
    const char* a2[] = {"x", "version"};   // 不带前导横线也认
    const char* a3[] = {"x", "--VERSION"}; // 大小写不敏感
    CHECK(cs::core::parse_args(2, a1).verb == Verb::Help);
    CHECK(cs::core::parse_args(2, a2).verb == Verb::Version);
    CHECK(cs::core::parse_args(2, a3).verb == Verb::Version);
}

// ---------- CLI 解析：负对照 ----------

void cli_unknown_verb_is_rejected(Runner& runner) {
    const char* a[] = {"x", "--convert"};  // convert 尚未实现，也不在表里
    const Parsed p = cs::core::parse_args(2, a);
    CHECK(p.status == Status::Unknown);
    CHECK(p.unknown == std::string_view("convert"));
    CHECK(p.verb == Verb::None);
}

// 参数个数由表声明（Arity），所以「漏了目录」和「动词不存在」是两种不同错误
void cli_missing_operand(Runner& runner) {
    const char* a[] = {"x", "--apply"};
    const Parsed p = cs::core::parse_args(2, a);
    CHECK(p.status == Status::MissingOperand);
    CHECK(p.verb == Verb::Apply);   // 认得动词，只是缺参数
    CHECK(p.operand.empty());
}

void cli_operand_captured(Runner& runner) {
    const char* a[] = {"x", "--apply", "build\\liveskin"};
    const Parsed p = cs::core::parse_args(3, a);
    CHECK(p.status == Status::Ok);
    CHECK(p.verb == Verb::Apply);
    CHECK(p.operand == std::string_view("build\\liveskin"));
    // 无参数的动词不该吞掉后面的 token（--restore 后跟目录不算错，只是忽略）
    const char* b[] = {"x", "restore"};
    CHECK(cs::core::parse_args(2, b).status == Status::Ok);
    CHECK(cs::core::verb_arity(Verb::Apply) == Arity::NeedDir);
    CHECK(cs::core::verb_arity(Verb::Restore) == Arity::None);
    CHECK(cs::core::verb_arity(Verb::LiveCheck) == Arity::NeedDir);
}

void cli_bare_dashes_mean_no_verb(Runner& runner) {
    const char* a[] = {"x", "--"};
    CHECK(cs::core::parse_args(2, a).status == Status::Ok);
    CHECK(cs::core::parse_args(2, a).verb == Verb::None);
}

void cli_prefix_of_verb_is_not_a_match(Runner& runner) {
    const char* a[] = {"x", "--hel"};  // 前缀不得误命中 help
    CHECK(cs::core::parse_args(2, a).status == Status::Unknown);
}

// ---------- 数据表守卫（R10：同形字符 / 重复名） ----------

void table_wellformed(Runner& runner) {
    std::size_t n = 0;
    const VerbSpec* t = cs::core::verb_table(n);
    CHECK(n >= 2);  // 自证：表空过 = 这条测试静默少测
    if (n == 0) return;

    for (std::size_t i = 0; i < n; ++i) {
        CHECK(!t[i].name.empty());
        for (std::size_t j = i + 1; j < n; ++j) {
            CHECK(!(t[i].name == t[j].name));  // 无重复动词
        }
        bool ascii_only = true;
        for (char c : t[i].name) {
            if (static_cast<unsigned char>(c) >= 0x80) ascii_only = false;
        }
        // 曾真实发生过：别名表里混进西里尔 е(U+0435)，匹配静默失配。
        CHECK(ascii_only);
    }
}

// ---------- harness 自证：正例 ----------

void noop_ok(Runner& runner) { CHECK(1 == 1); }

void harness_counts_passes(Runner& runner) {
    Runner probe;
    probe.add("noop_ok", &noop_ok);
    std::FILE* sink = nullptr;
    const int rc = probe.run(probe_out(sink));
    if (sink) std::fclose(sink);
    CHECK(rc == 0);
    CHECK(probe.case_count() == 1);
    CHECK(probe.checks_passed() == 1);
    CHECK(probe.checks_failed() == 0);
}

// ---------- harness 自证：核心负对照 ----------

void deliberate_fail(Runner& runner) { CHECK(1 == 2); }

void harness_detects_failure(Runner& runner) {
    Runner probe;
    probe.add("SELF_PROOF_inner_deliberate_fail", &deliberate_fail);
    std::FILE* sink = nullptr;
    const int rc = probe.run(probe_out(sink));
    if (sink) std::fclose(sink);

    // 前提断言取反方向：故意失败的用例若没让 rc>0，说明 CHECK 根本没计入，
    // 那么本文件所有「绿」都无意义 —— 这条断言是整个 harness 的立足点。
    CHECK(rc > 0);
    CHECK(probe.checks_failed() == 1);
    CHECK(probe.cases_failed() == 1);
}

}  // namespace

int main(int argc, char** argv) {
    // 运行模式必须**封闭**：未知参数一律拒绝，绝不回落到默认套件。
    // 实测踩因：一次编译失败后跑的是旧 exe，它不认识 --probe-registry，
    // 于是把拼错的 probe 名当成普通测试跑完并返回 0 —— 一个假的成功退出码。
    bool force_fail = false;
    if (argc >= 2) {
        const std::string_view mode(argv[1] ? argv[1] : "");
        if (mode == "--force-fail") {
            force_fail = true;
        } else if (mode == "--emit-skin") {
            // 脚手架：造一套真机测试皮肤给 app 的 --live-check 用（不跑断言）
            if (argc < 3) {
                std::fprintf(stderr, "error: --emit-skin needs a directory\n");
                return 2;
            }
            std::uint32_t files = 0;
            if (!cs::test::emit_live_skin_narrow(argv[2], files)) {
                std::fprintf(stderr, "error: emit-skin failed\n");
                return 1;
            }
            std::printf("EMIT files=%u dir=%s\n", files, argv[2]);
            return 0;
        } else if (mode == "--probe-l1") {
            cs::test::probe_l1_load(stdout);   // 打印加载器真值；不改任何系统状态
            return 0;
        } else if (mode == "--probe-registry") {
            cs::test::probe_registry_names(stdout);  // 只读：KEY_READ，不写不删不广播
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown test mode '%s'\n", std::string(mode).c_str());
            std::fprintf(stderr,
                         "available modes: --force-fail | --probe-l1 | --probe-registry | --emit-skin <dir>\n");
            return 2;
        }
    }

    Runner runner;
    runner.add("cli_no_args", &cli_no_args);
    runner.add("cli_help_and_version", &cli_help_and_version);
    runner.add("cli_unknown_verb_is_rejected", &cli_unknown_verb_is_rejected);
    runner.add("cli_missing_operand", &cli_missing_operand);
    runner.add("cli_operand_captured", &cli_operand_captured);
    runner.add("cli_bare_dashes_mean_no_verb", &cli_bare_dashes_mean_no_verb);
    runner.add("cli_prefix_of_verb_is_not_a_match", &cli_prefix_of_verb_is_not_a_match);
    runner.add("table_wellformed", &table_wellformed);
    runner.add("harness_counts_passes", &harness_counts_passes);
    runner.add("harness_detects_failure", &harness_detects_failure);

    // M1a：数据表守卫 / 认领 / hotspot 规则 / IR 不变量（实现在 tests_tables.cpp）
    cs::test::register_table_cases(runner);
    cs::test::register_claim_cases(runner);
    cs::test::register_hotspot_cases(runner);
    cs::test::register_ir_negative_cases(runner);
    cs::test::register_container_cases(runner);
    cs::test::register_l1_cases(runner);
    cs::test::register_registry_cases(runner);
    cs::test::register_apply_cases(runner);
    cs::test::register_png_cases(runner);
    cs::test::register_svg_cases(runner);
    cs::test::register_ui_cases(runner);
    cs::test::register_channel_e2e_cases(runner);

    if (force_fail) {
        std::printf("[MODE] force-fail: injecting a case that must fail\n");
        runner.add("SELF_PROOF_process_level_fail", &deliberate_fail);
    }

    std::printf("[RUN] cases=%d\n", static_cast<int>(runner.case_count()));
    return runner.run(stdout);
}
