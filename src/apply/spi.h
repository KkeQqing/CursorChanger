// src/apply/spi.h — 让系统重新读取光标方案（DESIGN §7.1 第 6 步）
//
// U2 的诚实状态：`SPI_SETCURSORS` 的调用形式在 M2b 真机验证之前属于**待实测**。
// 本文件把 flags 做成参数而不是在代码里写死，正是为了让「传 0」与「传 SPIF_SENDCHANGE」
// 能被同一个探针二选一测出来 —— 测出结果后写回 DESIGN §13，而不是继续保留两个猜测。
#pragma once

#include <cstdint>

namespace cs::apply {

struct SpiResult {
    bool performed{false};   // 是否真的调用了（false = 被 do_spi 跳过，不等于成功）
    bool called_ok{false};   // API 返回 TRUE
    uint32_t last_error{0};  // 仅作附加信息；见 selfcheck/l1.h 同类教训：返回值不等于原因
};

// flags=0 与 flags=SPIF_SENDCHANGE 的差别由调用方（探针）决定，不在这里做假设。
SpiResult set_cursors_from_registry(uint32_t flags);

}  // namespace cs::apply
