// src/apply/pipeline.h — 端到端：扫描 skin/ -> 认领 -> 落地 applied\ -> 基线 -> HKCU -> SPI -> 自检
//
// 顺序即判据（DESIGN §7.1）：L1 不过就**不写注册表**，否则坏文件会先进系统、
// 而基线又是在写之后才采的，用户就同时失去了「正确的光标」和「回到原状的路径」。
#pragma once

#include "apply/baseline.h"
#include "apply/registry.h"
#include "apply/spi.h"
#include "selfcheck/verify_live.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cs::apply {

struct PipelineOptions {
    std::wstring skin_dir;      // 素材目录（含 .cur）
    std::wstring applied_dir;   // 产物目录；不存在则创建
    Root root{Root::CurrentUser};
    std::wstring sub_key;       // 真机 L"Control Panel\\Cursors"；测试用沙箱键
    std::wstring baseline_path; // 首次 apply 创建，之后不覆盖
    uint32_t spi_flags{0};      // U2：由调用方（探针）决定传 0 还是 SPIF_SENDCHANGE
    // SPI_SETCURSORS 会**当场替换用户正在使用的指针**。它必须显式打开：
    // 沙箱测试用 false，把「写注册表的逻辑」和「影响真人的那一下」分开验证。
    bool do_spi{true};
};

struct Unclaimed {
    std::wstring file;
    std::string reason;  // NotInTable / EmptyStem / TooLong / NotCursorExtension
};

struct Conflict {
    std::wstring slot_name;
    std::vector<std::wstring> files;  // 至少两个；第一个被采用，其余报告
};

struct ProducedFile {
    std::wstring registry_name;
    std::wstring path;  // 绝对路径
    bool l1_ok{false};
    std::string l1_detail;
};

struct PipelineResult {
    uint32_t scanned{0};
    std::vector<Unclaimed> unclaimed;
    std::vector<Conflict> conflicts;
    std::vector<ProducedFile> produced;

    // 阶段结果
    bool aborted_before_registry{false};
    std::string abort_reason;

    bool baseline_saved{false};
    bool baseline_preexisted{false};
    RestoreResult restore{};

    uint32_t registry_written{0};
    uint32_t registry_failed{0};
    selfcheck::L2Result l2{};
    SpiResult spi{};
    bool scheme_label_written{false};
    // ok() 需要知道「本轮是否本来就该真调 SPI」：
    // do_spi=false 时 spi.performed==false 是正常的，不能被当成失败，也不能被当成成功。
    bool spi_expected{true};

    uint32_t slots_applied() const { return registry_written; }
    bool ok() const {
        const bool spi_part = spi_expected ? (spi.performed && spi.called_ok) : !spi.performed;
        return !aborted_before_registry && registry_failed == 0 && registry_written > 0 && spi_part &&
               l2.ok();
    }
};

// 只做「扫描 + 认领 + 落地 + L1」，**不碰注册表**。用于在没有授权时也能验证全链前半段。
PipelineResult build_only(const PipelineOptions& opt);

// 完整 apply：build_only 之后写注册表并触发 SPI。需要真机授权（沙箱键除外）。
PipelineResult apply_skin(const PipelineOptions& opt);

// 还原与复位（D13 两个动作）。都要跟着一次 SPI 重新加载，否则系统仍用内存里的旧帧。
RestoreResult restore_from_file(const PipelineOptions& opt, SpiResult& spi_out, bool& loaded);
RestoreResult reset_default(const PipelineOptions& opt, SpiResult& spi_out);

// 供报告用：目录是否存在、有多少 .cur。不改变任何东西。
uint32_t count_cur_files(const std::wstring& dir, std::vector<std::wstring>& names_out);

}  // namespace cs::apply
