// src/tests/testfs.h — 测试侧的最小文件读写工具（tests_cur / tests_l1 共用）
// 说明：这些是**测试夹具**用的 IO，允许用 CRT；core 层仍然一律不碰文件路径。
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace cs::test {

inline bool read_whole_file(const std::wstring& path, std::vector<uint8_t>& out) {
    out.clear();
    std::FILE* f = nullptr;
    if (::_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) out.resize(static_cast<std::size_t>(n));
    const std::size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size() && got > 0;
}

inline bool write_whole_file(const std::wstring& path, const std::vector<uint8_t>& bytes) {
    std::FILE* f = nullptr;
    if (::_wfopen_s(&f, path.c_str(), L"wb") != 0 || f == nullptr) return false;
    const std::size_t put = bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    return put == bytes.size();
}

}  // namespace cs::test
