// src/selfcheck/l1.h — L1 文件级自检：让 Windows 自己的加载器读我们的产物
//
// 这是本项目第一个允许依赖 Win32 的模块（DESIGN §2：只有 apply/ 与 selfcheck/ 可碰 Win32）。
// 它是**纯读、进程内**的：只创建并销毁本进程的光标对象，不写注册表、不广播、
// 不改变用户正在使用的指针，也绝不调用 SetSystemCursor（注入类，v1 非目标）。
// 因此 L1 可以在任何真机 apply 之前跑，并就地核销三个对加载器的假设（DESIGN §13）：
//   U12  hotspot 是否真在目录项的两个 WORD 里
//   U14  biSizeImage 写 XOR 实际长度是否被接受
//   U16  AND 行过填充（max(对齐值,128)）是否被接受
#pragma once

#include <cstdint>

namespace cs::selfcheck {

struct L1Result {
    bool loaded{false};
    uint32_t width{0};        // 加载器实际给出的帧尺寸（自选帧时是观测值，不是断言值）
    uint32_t height{0};
    uint32_t hotspot_x{0};
    uint32_t hotspot_y{0};
    bool has_color_bitmap{false};  // 32bpp 应有 hbmColor
    bool has_mask_bitmap{false};   // 应有 hbmMask（AND 面）
    uint32_t win_error{0};         // 失败时的 GetLastError；成功时为 0
};

// 实测（M2a）：`LoadCursorFromFileW` 拒绝垃圾输入时返回 NULL 但**不设 last error**，
// 因此 win_error 只能作为附加信息，**不能作为失败判据**。失败判据唯一是 `loaded == false`。
// M2 的错误报告文案要按这条写，不要写「原因：GetLastError」那种看起来有依据其实是空的解释。

// 用 LoadCursorFromFileW 加载，帧由系统自行挑选。用于「文件是否被接受」+ U12。
L1Result l1_load_and_probe(const wchar_t* path);

// 用 LoadImageW 指名请求 w x h 的帧（不带 LR_DEFAULTSIZE，避免被换成系统默认尺寸）。
// 用于证明多帧目录可被加载器按尺寸导航（H8 的真实侧）。
L1Result l1_load_frame(const wchar_t* path, uint32_t w, uint32_t h);

// 当前机器的系统光标默认边长（SM_CXCURSOR）。
// 实测依据：`LoadCursorFromFileW` 无视文件自带尺寸，一律按这个值重采样，并**按比例缩放 hotspot**
// （single16 的 (4,4) 回读成 (8,8)；single48 的 (12,12) 回读成 (8,8)）。
// 所以任何「加载后的尺寸/热点」断言都必须由它推算，不能把 32 写死。
uint32_t system_cursor_default_size();

}  // namespace cs::selfcheck
