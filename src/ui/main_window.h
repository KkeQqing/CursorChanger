// src/ui/main_window.h — Win32 窗口入口（DESIGN D2：原生 Win32，只做输入输出与状态显示）
//
// 本层不含业务逻辑：所有状态来自 apply::build_only/apply_skin + apply::make_report；
// 按钮动作走 apply::make_live_opts 构造的同一份 options，因此与 CLI 结果必然一致（§9.2）。
#pragma once

namespace cs {
namespace ui {

// 启动主窗口并进入消息循环，返回进程退出码。
// 若环境变量 CS_GUI_CAPTURE 指向一个 .bmp 路径：窗口首帧渲染后用 PrintWindow 存档该截图
// 并立即退出（供无人值守的「窗口截图存档」自检用；此路径只跑 build_only，绝不碰注册表/SPI）。
int run_window();

}  // namespace ui
}  // namespace cs
