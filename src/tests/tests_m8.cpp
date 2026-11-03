// src/tests/tests_m8.cpp — 构建产物「子系统」守卫（D18 V0.6.2 改判的可执行钉子）。
//
// 为什么用「读 PE 头」而不是「跑起来看有没有黑框」：后者无法在无头 harness 里判定，
// 而子系统位是 IMAGE_OPTIONAL_HEADER 里一个确定的 WORD —— 字节级判据优先（§4 / H9）。
// 守卫的是**非对称**：app 必须是 WINDOWS GUI(2)（双击不分配控制台 = 用户要的静默），
// tests 必须是 WINDOWS CUI(3)（harness 退出码=失败数、--probe-* 依赖控制台 stdout）。
// 两边若被统一成同一个值，这里当场变红；这正是 D18 改判「只动 app、不动 tests」的边界。
//
// 边界诚实声明：本测读的是 build\ 里的兄弟产物（build.bat 先产 app 再产 tests 才跑到这），
// 它证明「镜像的子系统位」，不证明「运行时 AttachConsole 一定接得到终端输出」——
// 后者由重定向继承句柄这条链保证，见 D18 与本轮真机 --version 实测。
#include "cases.h"
#include "testfs.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using cs::test::Runner;

namespace {

constexpr int kGui = 2;   // IMAGE_SUBSYSTEM_WINDOWS_GUI
constexpr int kCui = 3;   // IMAGE_SUBSYSTEM_WINDOWS_CUI

// 解析 PE32+(x64) 的 OptionalHeader.Subsystem。判不出来返回 -1，并把观测原因写进 out。
int read_pe_subsystem(const std::wstring& path, std::string& out_reason) {
    std::vector<std::uint8_t> b;
    if (!cs::test::read_whole_file(path, b) || b.size() < 0x40) {
        out_reason = "cannot read image / too small";
        return -1;
    }
    if (b[0] != 'M' || b[1] != 'Z') { out_reason = "missing MZ header"; return -1; }
    auto u32 = [&](std::size_t o) -> std::uint32_t {
        return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1]) << 8) |
               (static_cast<std::uint32_t>(b[o + 2]) << 16) |
               (static_cast<std::uint32_t>(b[o + 3]) << 24);
    };
    auto u16 = [&](std::size_t o) -> std::uint32_t {
        return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1]) << 8);
    };
    const std::uint32_t e_lfanew = u32(0x3C);
    // OptionalHeader 起点 = e_lfanew + 4(签名) + 20(文件头)。Subsystem 在 +68；读 +70 内以保边界。
    if (static_cast<std::size_t>(e_lfanew) + 24 + 70 > b.size()) {
        out_reason = "e_lfanew points past EOF";
        return -1;
    }
    const std::size_t sig = e_lfanew;
    if (!(b[sig] == 'P' && b[sig + 1] == 'E' && b[sig + 2] == 0 && b[sig + 3] == 0)) {
        out_reason = "missing PE signature";
        return -1;
    }
    const std::size_t opt = sig + 24;
    const std::uint32_t magic = u16(opt);
    // 前提断言取反方向：只有 PE32+ 才适用「+68 即 Subsystem」的偏移。不是就先判不出，
    // 免得用错偏移得到「看着像子系统」的噪声值。
    if (magic != 0x20b) { out_reason = "not PE32+ (x64), offset model invalid"; return -1; }
    return static_cast<int>(u16(opt + 68));
}

}  // namespace

namespace cs::test {

void register_build_image_cases(Runner& runner) {
    runner.add("build_products_subsystem_split", [](Runner& runner) {
        std::string app_reason, tst_reason;
        const int app = read_pe_subsystem(L"build\\cursorswitch.exe", app_reason);
        const int tst = read_pe_subsystem(L"build\\cursorswitch_tests.exe", tst_reason);

        // app：WINDOWS 子系统（GUI）。若解析失败(-1)也直接红 —— 读不出产物不能算过。
        CHECK(app == kGui);
        // tests：仍是 CONSOLE 子系统，harness 的可验证性依赖它。
        CHECK(tst == kCui);

        if (app != kGui || tst != kCui) {
            std::printf("  subsystem app=%d(%s) tests=%d(%s) | want app=%d tests=%d\n", app,
                        app_reason.c_str(), tst, tst_reason.c_str(), kGui, kCui);
        }
    });
}

}  // namespace cs::test
