## 🚀 rapoo-tray v1.2.2 发行说明 (Release Notes)

rapoo-tray v1.2.2 重点修复了 Windows 系统浅色模式下的托盘菜单视觉对比度问题，精简了通用未打标机型的显示布局以优化 OSD 浮窗尺寸，并新增了便捷的硬件 PID 提取辅助工具。

### 一、浅色主题渲染修复与高对比度适配 (Light Mode Rendering Fix)
1. **任务栏主题精准对齐**：优先读取注册表 `SystemUsesLightTheme`，解决“系统任务栏设为浅色但全局应用为深色”时的模式误判问题。
2. **根除 DWM 亚克力白雾冲淡**：在浅色模式下停用 DWM 亚克力像素合成（`ACCENT_DISABLED`），规避 GDI Alpha 通道与 DWM 混叠引起的文字泛白及透明漂白现象。
3. **重构浅色色彩系统**：
   - 菜单背景：纯白 (`#FFFFFF`)
   - 边框描边：浅灰 (`#CDD4DE`)
   - 主菜单文本：高对比度墨黑 (`#141820`)
   - 交互悬停态：淡雅蓝灰 (`#E8EEF8`)
   - 激活与选中指示：Win11 强调蓝 (`#006ED7`)

### 二、未打标机型标识精简 (Generic Fallback Display Streamlining)
1. **紧凑化型号显示**：二代 Nordic 架构未打标/未识别机型的兜底显示名称由 `雷柏游戏鼠标 (通用)` 精简为 **`通用`**。
2. **OSD 浮窗尺寸优化**：彻底消除因冗长机型名称导致 DPI 切换 OSD 悬浮卡片被横向拉宽的问题，恢复最紧凑精致的视觉布局。
3. **全端同步**：托盘右键菜单、托盘气泡提示、屏幕 OSD 及 TrafficMonitor 监控插件同步更新。

### 三、硬件 PID 提取辅助工具 (Hardware PID Tool)
1. **一键提取与复制**：项目根目录新增 `获取鼠标PID.bat` 与 `tools/get_mouse_pid.ps1`。用户插入鼠标后双击脚本，即可自动探测雷柏设备（VID `0x24AE`）的硬件 PID，生成 PR 代码片段并自动复制到剪贴板。
2. **降低社区贡献门槛**：方便非技术用户快速反馈设备信息，加速雷柏全系列机型收录。

### 四、组件版本同步
- 主程序 `rapoo-tray.exe`、安装程序 `rapoo-tray-setup.exe` 及 TrafficMonitor 扩展插件 `rapoo-plugin.dll` 同步升级至 v1.2.2。
