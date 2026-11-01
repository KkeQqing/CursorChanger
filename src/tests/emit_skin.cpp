// src/tests/emit_skin.cpp — 用被测 writer 生成一套真机测试皮肤（15 槽 x 16/32/48 帧）
//
// 为什么放在测试侧：造素材是**验证脚手架**，不是产品能力 —— 产品只消费 skin 目录。
// 帧内容与热点都取自实现本身，因此这里同时是「writer 能产出合法多帧文件」的活体证明。
#include "cases.h"
#include "testfs.h"

#include "core/claim.h"
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

namespace cs::test {

bool emit_live_skin(const std::wstring& dir, std::uint32_t& files_out) {
    files_out = 0;
    if (dir.empty()) return false;
    CreateDirectoryW(dir.c_str(), nullptr);

    std::size_t n = 0;
    const SlotSpec* slots = slot_table(n);
    std::size_t na = 0;
    const AliasSpec* aliases = alias_table(na);
    const std::uint32_t dims[] = {16, 32, 48};

    for (std::size_t i = 0; i < n; ++i) {
        // 取该槽的第一条别名当文件名：顺带验证「别名 -> 槽位」这条链在真机目录上成立
        std::string fname;
        for (std::size_t k = 0; k < na; ++k) {
            if (aliases[k].slot != slots[i].id) continue;
            fname.assign(aliases[k].alias.begin(), aliases[k].alias.end());
            break;
        }
        if (fname.empty()) {
            std::printf("EMIT no alias for slot %zu -> abort\n", i);
            return false;
        }

        CursorSpec spec;
        spec.slot = slots[i].id;
        spec.source_kind = SourceKind::Png;
        bool bad = false;
        for (std::uint32_t dim : dims) {
            // 帧级热点：直接问默认规则，而不是抄同一个数（U15）
            Frame f;
            f.width = dim;
            f.height = dim;
            f.hotspot = default_hotspot(slots[i].rule, dim, dim);
            f.pixels.assign(static_cast<std::size_t>(dim) * dim * 4u, 0);
            for (std::size_t p = 0; p < static_cast<std::size_t>(dim) * dim; ++p) {
                f.pixels[p * 4u + 0] = 255;
                f.pixels[p * 4u + 1] = 255;
                f.pixels[p * 4u + 2] = 255;
                f.pixels[p * 4u + 3] = 255;
            }
            if (f.hotspot.x >= dim || f.hotspot.y >= dim) bad = true;
            spec.frames.push_back(f);
        }
        if (bad) {
            std::printf("EMIT hotspot out of bounds for %s\n", fname.c_str());
            return false;
        }

        std::vector<std::uint8_t> bytes;
        if (write_cursor_bytes(spec, bytes).status != WriteStatus::Ok) {
            std::printf("EMIT writer refused %s\n", fname.c_str());
            return false;
        }
        std::wstring wname;
        for (char c : fname) wname.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        if (!write_whole_file(dir + L"\\" + wname + L".cur", bytes)) {
            std::printf("EMIT cannot write %s\n", fname.c_str());
            return false;
        }
        ++files_out;
    }
    return files_out == n;
}

bool emit_live_skin_narrow(const char* dir, std::uint32_t& files_out) {
    if (dir == nullptr || dir[0] == '\0') return false;
    std::wstring w;
    const int n = MultiByteToWideChar(CP_ACP, 0, dir, -1, nullptr, 0);
    if (n <= 1) return false;
    w.resize(static_cast<std::size_t>(n - 1));
    MultiByteToWideChar(CP_ACP, 0, dir, -1, w.data(), n);
    return emit_live_skin(w, files_out);
}

}  // namespace cs::test
