// src/tests/tests_m2.cpp — M2：注册表写入 / 三态基线 / 还原 / 端到端流水线（全部走沙箱键）
//
// 边界：本文件**只**操作用于自测的 HKCU\Software\CursorSwitch-selftest，跑完自己清干净；
// 不碰 Control Panel\Cursors，也不调用 SPI_SETCURSORS（那会当场换掉用户正在用的指针，
// 需要单独授权）。因此 do_spi 一律 false —— 而且它 false 时结果里记的是
// 「没执行」，不是「执行成功」（SpiResult::performed）。
//
// 还原正确性是 M2 的安全底线：apply 能生效但不安全，比不生效更糟。
#include "cases.h"
#include "testfs.h"

#include "apply/baseline.h"
#include "apply/pipeline.h"
#include "apply/registry.h"
#include "core/cur_writer.h"
#include "core/ir.h"
#include "core/slots.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace cs::core;
using namespace cs::apply;
using cs::test::Runner;

namespace {

const wchar_t* kSandbox = L"Software\\CursorSwitch-selftest";
const wchar_t* kSandboxMissing = L"Software\\CursorSwitch-selftest-does-not-exist";

std::wstring wname(std::string_view sv) { return std::wstring(sv.begin(), sv.end()); }

bool read_absent(Root root, const wchar_t* sub, const wchar_t* name) {
    std::wstring v;
    bool found = false;
    const QueryResult r = read_sz(root, sub, name, v, found);
    return r.status == QueryStatus::Ok && !found;
}

// 用真 writer 产出一个合法 .cur 落到指定目录（不引入第二套编码实现，也不写进仓库）
bool write_fixture_cur(const std::wstring& dir, const std::wstring& fname, uint32_t dim) {
    CursorSpec s;
    s.slot = SlotId::Arrow;
    s.source_kind = SourceKind::CurPassthrough;
    Frame f;
    f.width = dim;
    f.height = dim;
    f.hotspot = Hotspot{static_cast<uint16_t>(dim / 4), static_cast<uint16_t>(dim / 4)};
    f.pixels.assign(static_cast<std::size_t>(dim) * dim * 4u, 255);
    s.frames.push_back(f);

    std::vector<uint8_t> bytes;
    if (write_cursor_bytes(s, bytes).status != cs::core::WriteStatus::Ok) return false;
    CreateDirectoryW(dir.c_str(), nullptr);
    return cs::test::write_whole_file(dir + L"\\" + fname, bytes);
}

bool write_garbage_cur(const std::wstring& dir, const std::wstring& fname) {
    CreateDirectoryW(dir.c_str(), nullptr);
    std::vector<uint8_t> junk(64, 0xAB);
    return cs::test::write_whole_file(dir + L"\\" + fname, junk);
}

// 素材目录：4 个可认领 + 1 个不该被认领 + 1 对同槽冲突
bool prepare_m2_skin_dir() {
    const std::wstring dir = L"build\\m2skin";
    bool ok = true;
    ok = write_fixture_cur(dir, L"arrow.cur", 32) && ok;
    ok = write_fixture_cur(dir, L"hand.cur", 32) && ok;
    ok = write_fixture_cur(dir, L"nw_resize.cur", 32) && ok;
    ok = write_fixture_cur(dir, L"se_resize.cur", 32) && ok;
    ok = write_fixture_cur(dir, L"not_a_slot.cur", 32) && ok;
    return ok;
}

// 同样的目录，但 arrow.cur 是垃圾字节：用来验「坏文件必须挡在写注册表之前」
bool prepare_m2_bad_skin_dir() {
    const std::wstring dir = L"build\\m2skin_bad";
    bool ok = true;
    ok = write_garbage_cur(dir, L"arrow.cur") && ok;
    ok = write_fixture_cur(dir, L"hand.cur", 32) && ok;
    return ok;
}

// 把沙箱清成一个已知起点：建键 + 删掉我们关心的所有值与额外值
void reset_sandbox() {
    ensure_key(Root::CurrentUser, kSandbox);
    std::size_t n = 0;
    const SlotSpec* t = slot_table(n);
    for (std::size_t i = 0; i < n; ++i) {
        delete_value(Root::CurrentUser, kSandbox, wname(t[i].registry_name).c_str());
    }
    delete_value(Root::CurrentUser, kSandbox, L"Scheme");
    delete_value(Root::CurrentUser, kSandbox, L"");
}

void tear_down_sandbox() {
    // 先清空再删键；delete_empty_key 对非空键是拒绝的，这里显式清干净
    std::size_t n = 0;
    const SlotSpec* t = slot_table(n);
    for (std::size_t i = 0; i < n; ++i) delete_value(Root::CurrentUser, kSandbox, wname(t[i].registry_name).c_str());
    delete_value(Root::CurrentUser, kSandbox, L"Scheme");
    delete_value(Root::CurrentUser, kSandbox, L"");
    delete_empty_key(Root::CurrentUser, kSandbox);
}

}  // namespace

namespace cs::test {

void register_apply_cases(Runner& runner) {
    runner.add("m2_registry_write_read_delete_roundtrip", [](Runner& runner) {
        reset_sandbox();
        const std::wstring name = L"Arrow";
        const std::wstring data = L"C:\\Windows\\Cursors\\aero\\arrow_r.cur";
        CHECK(write_sz(Root::CurrentUser, kSandbox, name.c_str(), data.c_str()) == apply::WriteStatus::Ok);

        std::wstring got;
        bool found = false;
        CHECK(read_sz(Root::CurrentUser, kSandbox, name.c_str(), got, found).status == QueryStatus::Ok);
        CHECK(found);
        CHECK(got == data);

        CHECK(delete_value(Root::CurrentUser, kSandbox, name.c_str()) == apply::WriteStatus::Ok);
        CHECK(read_absent(Root::CurrentUser, kSandbox, name.c_str()));  // 读回必须真的没了
        tear_down_sandbox();
    });

    runner.add("m2_write_refuses_to_create_a_missing_key", [](Runner& runner) {
        // 误传键路径时静默建键，比报错危险得多（§7.1 的失败模式偏好）
        std::wstring probe;
        bool found = false;
        read_sz(Root::CurrentUser, kSandboxMissing, L"Arrow", probe, found);  // 先确认基线

        const apply::WriteStatus w = write_sz(Root::CurrentUser, kSandboxMissing, L"Arrow", L"x");
        CHECK(w == apply::WriteStatus::KeyNotFound);

        bool still_absent = false;
        CHECK(key_exists(Root::CurrentUser, kSandboxMissing, still_absent).status == QueryStatus::Ok);
        CHECK(!still_absent);  // 键仍然不存在 = 一个字节都没写进去
    });

    runner.add("m2_delete_empty_key_refuses_nonempty_key", [](Runner& runner) {
        reset_sandbox();
        CHECK(write_sz(Root::CurrentUser, kSandbox, L"Arrow", L"C:\\x.cur") == apply::WriteStatus::Ok);
        // 键里有值时必须拒绝，绝不能递归清空用户的东西
        CHECK(delete_empty_key(Root::CurrentUser, kSandbox) == apply::WriteStatus::Unexpected);
        std::wstring got;
        bool found = false;
        read_sz(Root::CurrentUser, kSandbox, L"Arrow", got, found);
        CHECK(found);  // 值仍在：拒绝是「什么都不做」，不是「删一半」
        tear_down_sandbox();
        // 清空之后才允许删键
        CHECK(delete_empty_key(Root::CurrentUser, kSandbox) == apply::WriteStatus::Ok);
    });

    runner.add("m2_baseline_captures_three_states", [](Runner& runner) {
        // 状态一：整个键不存在
        bool ex = false;
        const bool missing_key = !(key_exists(Root::CurrentUser, kSandboxMissing, ex).status ==
                                       QueryStatus::Ok &&
                                   ex);
        CHECK(missing_key);
        const Baseline k = capture_baseline(Root::CurrentUser, kSandboxMissing);
        CHECK(k.key_absent_before);
        std::size_t n = 0;
        slot_table(n);
        CHECK(k.slots.size() == n);
        for (const SlotSnapshot& s : k.slots) CHECK(s.state == SlotState::KeyAbsent);

        // 状态二：键存在、值不存在
        reset_sandbox();
        const Baseline a = capture_baseline(Root::CurrentUser, kSandbox);
        CHECK(!a.key_absent_before);
        std::size_t absent = 0, present = 0;
        for (const SlotSnapshot& s : a.slots) {
            if (s.state == SlotState::ValueAbsent) ++absent;
            if (s.state == SlotState::ValuePresent) ++present;
        }
        CHECK(absent == n);
        CHECK(present == 0);

        // 状态三：写两个值后采集
        const std::wstring arrow = wname(slot_table(n)[0].registry_name);
        const std::wstring hand = wname(slot_table(n)[5].registry_name);
        CHECK(write_sz(Root::CurrentUser, kSandbox, arrow.c_str(), L"C:\\a.cur") == apply::WriteStatus::Ok);
        CHECK(write_sz(Root::CurrentUser, kSandbox, hand.c_str(), L"C:\\h.cur") == apply::WriteStatus::Ok);
        const Baseline p = capture_baseline(Root::CurrentUser, kSandbox);
        std::size_t p2 = 0;
        for (const SlotSnapshot& s : p.slots) {
            if (s.state == SlotState::ValuePresent) ++p2;
        }
        CHECK(p2 == 2);  // 自证：确实采到两个 present
        tear_down_sandbox();
    });

    runner.add("m2_baseline_file_roundtrip_and_corruption_refused", [](Runner& runner) {
        reset_sandbox();
        CHECK(write_sz(Root::CurrentUser, kSandbox, L"Arrow", L"C:\\a.cur") == apply::WriteStatus::Ok);
        const Baseline b = capture_baseline(Root::CurrentUser, kSandbox);

        const wchar_t* path = L"build\\m2_baseline.ini";
        std::wstring err;
        CHECK(save_baseline(b, path, err));
        CHECK(err.empty());

        Baseline back;
        CHECK(load_baseline(back, path, err));
        CHECK(back.slots.size() == b.slots.size());
        CHECK(back.extras.size() == b.extras.size());
        CHECK(!back.key_absent_before);
        std::size_t present = 0;
        for (std::size_t i = 0; i < back.slots.size(); ++i) {
            CHECK(back.slots[i].state == b.slots[i].state);
            CHECK(back.slots[i].value == b.slots[i].value);
            if (back.slots[i].state == SlotState::ValuePresent) ++present;
        }
        CHECK(present == 1);

        // 负对照：截断/损坏的文件必须被拒，不能被当成「快照是空的」——
        // 后者会让还原只还原一半，还报告成功。
        std::vector<uint8_t> raw;
        CHECK(cs::test::read_whole_file(path, raw));
        raw.resize(raw.size() / 2);
        CHECK(cs::test::write_whole_file(L"build\\m2_baseline_trunc.ini", raw));
        Baseline junk;
        CHECK(!load_baseline(junk, L"build\\m2_baseline_trunc.ini", err));
        CHECK(!err.empty());

        const char* bad = "Z\tbogus\n";
        std::vector<uint8_t> vb(bad, bad + 9);
        CHECK(cs::test::write_whole_file(L"build\\m2_baseline_magic.ini", vb));
        Baseline junk2;
        CHECK(!load_baseline(junk2, L"build\\m2_baseline_magic.ini", err));

        tear_down_sandbox();
    });

    runner.add("m2_restore_returns_sandbox_to_absent_state", [](Runner& runner) {
        // 这是 M2 的安全底线：apply 前采基线（全部 absent）→ 模拟 apply 写值 → 还原 → 回到 absent
        reset_sandbox();
        const Baseline before = capture_baseline(Root::CurrentUser, kSandbox);
        std::wstring err;
        CHECK(save_baseline(before, L"build\\m2_restore_base.ini", err));

        PipelineOptions opt;
        opt.root = Root::CurrentUser;
        opt.sub_key = kSandbox;
        opt.baseline_path = L"build\\m2_restore_base.ini";
        opt.do_spi = false;

        // 模拟一次 apply 的结果（只写注册表这一段）
        std::size_t n = 0;
        const SlotSpec* t = slot_table(n);
        for (std::size_t i = 0; i < n; ++i) {
            const std::wstring nm = wname(t[i].registry_name);
            CHECK(write_sz(Root::CurrentUser, kSandbox, nm.c_str(), L"C:\\applied\\x.cur") ==
                  apply::WriteStatus::Ok);
        }
        CHECK(write_sz(Root::CurrentUser, kSandbox, L"", L"CursorSwitch") == apply::WriteStatus::Ok);

        SpiResult spi;
        bool loaded = false;
        const RestoreResult r = restore_from_file(opt, spi, loaded);
        CHECK(loaded);
        CHECK(r.failed == 0);
        CHECK(spi.performed == false);  // do_spi=false：没执行，而不是执行成功

        for (std::size_t i = 0; i < n; ++i) {
            CHECK(read_absent(Root::CurrentUser, kSandbox, wname(t[i].registry_name).c_str()));
        }
        tear_down_sandbox();
    });

    runner.add("m2_reset_default_is_an_exit_without_baseline", [](Runner& runner) {
        // 兜底出口：不依赖任何快照，也能把我们的自定义值全清掉
        reset_sandbox();
        std::size_t n = 0;
        const SlotSpec* t = slot_table(n);
        for (std::size_t i = 0; i < n; ++i) {
            write_sz(Root::CurrentUser, kSandbox, wname(t[i].registry_name).c_str(), L"C:\\a.cur");
        }
        write_sz(Root::CurrentUser, kSandbox, L"Scheme", L"CursorSwitch");
        write_sz(Root::CurrentUser, kSandbox, L"", L"CursorSwitch");

        PipelineOptions opt;
        opt.root = Root::CurrentUser;
        opt.sub_key = kSandbox;
        opt.do_spi = false;
        SpiResult spi;
        const RestoreResult r = reset_default(opt, spi);
        CHECK(spi.performed == false);  // do_spi=false 时还原/复位也不得碰系统
        CHECK(r.failed == 0);
        CHECK(r.restored == n + 2);  // 15 槽 + Scheme + 默认值
        for (std::size_t i = 0; i < n; ++i) {
            CHECK(read_absent(Root::CurrentUser, kSandbox, wname(t[i].registry_name).c_str()));
        }
        CHECK(read_absent(Root::CurrentUser, kSandbox, L"Scheme"));
        CHECK(read_absent(Root::CurrentUser, kSandbox, L""));
        tear_down_sandbox();
    });

    runner.add("m2_pipeline_claims_and_reports_conflicts", [](Runner& runner) {
        // 用真 writer 产出的合法 .cur 搭一个素材目录
        CHECK(prepare_m2_skin_dir());
        reset_sandbox();

        PipelineOptions opt;
        opt.skin_dir = L"build\\m2skin";
        opt.applied_dir = L"build\\m2applied";
        opt.root = Root::CurrentUser;
        opt.sub_key = kSandbox;
        opt.baseline_path = L"build\\m2_pipeline_base.ini";
        opt.do_spi = false;

        const PipelineResult built = build_only(opt);
        CHECK(!built.aborted_before_registry);
        CHECK(built.scanned >= 5);
        // 未认领必须列出来，不能静默
        bool bogus_reported = false;
        for (const Unclaimed& u : built.unclaimed) {
            if (u.reason == "NotInTable") bogus_reported = true;
        }
        CHECK(bogus_reported);
        // 同槽多文件必须报冲突（nw_resize 与 se_resize 同指 SizeNWSE）
        bool conflict_reported = false;
        for (const Conflict& c : built.conflicts) {
            if (c.slot_name == L"SizeNWSE" && c.files.size() >= 2) conflict_reported = true;
        }
        CHECK(conflict_reported);
        for (const ProducedFile& p : built.produced) CHECK(p.l1_ok);

        // 完整 apply（不含 SPI）+ L2 回读
        delete_value(Root::CurrentUser, kSandbox, L"Arrow");  // 确保没有旧基线干扰
        const std::wstring base_path = L"build\\m2_pipeline_base.ini";
        DeleteFileW(base_path.c_str());
        const PipelineResult r = apply_skin(opt);
        CHECK(!r.aborted_before_registry);
        CHECK(r.registry_failed == 0);
        CHECK(r.registry_written > 0);
        CHECK(r.baseline_saved || r.baseline_preexisted);
        CHECK(r.l2.checked == r.l2.matched);
        CHECK(r.l2.ok());
        CHECK(r.spi.performed == false);
        CHECK(!r.spi_expected);
        CHECK(r.ok());  // ok() 在 do_spi=false 下仍然成立，且不是靠谎报

        // 沙箱里 Arrow 的值应等于我们落地的绝对路径
        std::wstring got;
        bool found = false;
        read_sz(Root::CurrentUser, kSandbox, L"Arrow", got, found);
        CHECK(found);
        CHECK(got.find(L"m2applied") != std::wstring::npos);

        tear_down_sandbox();
    });

    runner.add("m2_pipeline_aborts_before_touching_registry", [](Runner& runner) {
        // M2 判据①：坏文件必须让流水线在写注册表**之前**停下，且注册表一个字节都没被改。
        CHECK(prepare_m2_bad_skin_dir());
        reset_sandbox();

        PipelineOptions opt;
        opt.skin_dir = L"build\\m2skin_bad";
        opt.applied_dir = L"build\\m2applied_bad";
        opt.root = Root::CurrentUser;
        opt.sub_key = kSandbox;
        opt.baseline_path = L"build\\m2_bad_base.ini";
        opt.do_spi = false;

        const PipelineResult r = apply_skin(opt);
        CHECK(r.aborted_before_registry);
        CHECK(r.registry_written == 0);
        CHECK(r.registry_failed == 0);
        CHECK(!r.ok());

        // 关键断言：注册表未被触碰（每个槽位仍然 absent）
        std::size_t n = 0;
        const SlotSpec* t = slot_table(n);
        for (std::size_t i = 0; i < n; ++i) {
            CHECK(read_absent(Root::CurrentUser, kSandbox, wname(t[i].registry_name).c_str()));
        }
        tear_down_sandbox();
    });

    runner.add("m2_apply_refuses_when_baseline_is_unreadable", [](Runner& runner) {
        // 安全文件读不出来就不许 apply：否则换完光标才发现还原不了，用户只剩「复位默认」这条粗粒度退路。
        CHECK(prepare_m2_skin_dir());
        reset_sandbox();

        const std::wstring base = L"build\\m2_corrupt_base.ini";
        std::vector<uint8_t> junk;
        const char* bad = "Z\tbogus\n";
        junk.assign(bad, bad + 9);
        CHECK(cs::test::write_whole_file(base, junk));

        PipelineOptions opt;
        opt.skin_dir = L"build\\m2skin";
        opt.applied_dir = L"build\\m2applied";
        opt.root = Root::CurrentUser;
        opt.sub_key = kSandbox;
        opt.baseline_path = base;
        opt.do_spi = false;

        const PipelineResult r = apply_skin(opt);
        CHECK(r.aborted_before_registry);
        CHECK(r.registry_written == 0);
        CHECK(!r.ok());

        std::size_t n = 0;
        const SlotSpec* t = slot_table(n);
        for (std::size_t i = 0; i < n; ++i) {
            CHECK(read_absent(Root::CurrentUser, kSandbox, wname(t[i].registry_name).c_str()));
        }
        DeleteFileW(base.c_str());
        tear_down_sandbox();
    });

    runner.add("m2_apply_does_not_overwrite_an_existing_baseline", [](Runner& runner) {
        // R5：基线只在首次 apply 创建。第二次 apply 若覆盖它，存的就是「第一次我写的路径」，
        // 于是永远回不到系统默认 —— 这条不变量必须被机器看住。
        CHECK(prepare_m2_skin_dir());
        reset_sandbox();
        const std::wstring base = L"build\\m2_twice_base.ini";
        DeleteFileW(base.c_str());

        PipelineOptions opt;
        opt.skin_dir = L"build\\m2skin";
        opt.applied_dir = L"build\\m2applied";
        opt.root = Root::CurrentUser;
        opt.sub_key = kSandbox;
        opt.baseline_path = base;
        opt.do_spi = false;

        const PipelineResult first = apply_skin(opt);
        CHECK(!first.aborted_before_registry);
        CHECK(first.baseline_saved);
        CHECK(!first.baseline_preexisted);

        std::vector<uint8_t> after_first;
        CHECK(cs::test::read_whole_file(base, after_first));

        const PipelineResult second = apply_skin(opt);
        CHECK(!second.aborted_before_registry);
        CHECK(second.baseline_preexisted);   // 走的是「已存在」分支
        CHECK(!second.baseline_saved);       // 且没有重写

        std::vector<uint8_t> after_second;
        CHECK(cs::test::read_whole_file(base, after_second));
        CHECK(after_first == after_second);  // 逐字节相同 = 真的没被覆盖

        tear_down_sandbox();
        DeleteFileW(base.c_str());
    });
}

}  // namespace cs::test
