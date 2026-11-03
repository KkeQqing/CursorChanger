# BACKLOG

> 只进不出。这里的条目不得被顺手实施；要做必须先经批准写入 DESIGN.md 决策表。

- .ico 输入通道（无 hotspot 字段，需先定认领规则）
- .ani 动画光标（另有系统尺寸策略上限，未验）
- 目录 watcher 常驻自动生效（D12 当前排除）
- 逐 DPI 素材集与多帧策略优化
- 光标包导入/导出（方案打包）
- hotspot 可视化编辑器
- comctl32 v6 主题化（manifest + rc 接入）：M5b/M5c 用核心 Win32 控件即可跑通并可截图，D2 的「v6 控件」外观推迟——属可见性偏离，见 V0.6 总结 §2。**已落地（commit `4da864d`）**：app.manifest(Common-Controls 6.0 + asInvoker) + app.rc(RT_MANIFEST id1) + build.bat 接 rc + InitCommonControlsEx；实测 IsThemeActive=1、OpenThemeData 有效、与无 manifest 版截图 diff 15249px（主题化确证）。此条按「只进不出」保留仅作溯源。
- 提权写入 HKU\.DEFAULT（登录面/UAC 光标，D14 排除，需另行立项）
- 多语言 UI
- 用户可配置的输出尺寸档位：现固定下采样到 16/32/48（`allow_upscale=false` 写死），`settings.ini` 读取**未实现**（cur_writer 注释里「落地后由加载器覆盖」尚未发生）。要做「选尺寸/开关放大」属新增
- 实现 §5.3 设计的 `<stem>.meta` 热点 sidecar：代码目前**从不读取 .meta**（全仓无消费点），只在 png/svg 通道留了 `has_hotspot_override` 结构字段。落地需解析器 + 接入 build + 校验越界（§6 H5）。V0.6.3 曾把这条误写进用户文档、已纠正，故记此避免再当"已存在"
