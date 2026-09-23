## 🚀 rapoo-tray v1.2.1 发行说明 (Release Notes)

rapoo-tray v1.2.1 正式发布官方 TrafficMonitor 扩展插件（`rapoo-plugin.dll`），并将项目版本号迭代至 1.2.1。本版本核心更新内容如下：

### 一、新增 TrafficMonitor 扩展插件 (`rapoo-plugin.dll`)
1. **监控集成**：基于 TrafficMonitor 插件接口规范（`ITMPlugin API v8`）原生开发，支持在 TrafficMonitor 任务栏窗口与主悬浮窗中直接显示雷柏鼠标的实时运行状态。
2. **状态项目**：
   - **鼠标电量** (`rapoo_mouse_battery`)：实时显示电量百分比（例如：`85%`），充电时自动附带 `⚡` 符号（例如：`⚡85%`）。
   - **鼠标 DPI** (`rapoo_mouse_dpi`)：实时显示当前档位对应 DPI 数值（例如：`800`）。
3. **详细信息提示 (Tooltip)**：鼠标指针悬停于监控项时，弹出包含设备具体型号、当前连接模式（2.4G 无线 / USB 有线）、详细电量（含充电状态）与当前 DPI 档位索引的提示信息。
4. **双模热插拔**：采用非阻塞重叠 I/O（Overlapped I/O）后台轮询监听，支持 2.4G 无线接收器与 USB 有线直连双模自动识别与无感切换。
5. **设备支持范围**：与主程序一致，原生支持雷柏 VT7 系列、VT3S 系列、VT3 MAX 系列以及二代 Nordic 架构通用机型。
6. **原生低耗**：纯 Win32 C++ 静态链接构建，DLL 体积仅约 28 KB，常驻内存占用小于 1 MB，CPU 占用趋近 0.0%。

### 二、工程架构与持续集成升级
1. **自动化流水线升级**：更新 GitHub Actions CI/CD 流水线，在发布新版本时自动编译并随 Release 附件分发 `rapoo-plugin.dll`。
2. **插件独立编译脚本**：提供 `plugin/build_plugin.bat`，支持本地 MinGW 工具链一键构建插件 DLL。
3. **版本号同步对齐**：主程序（`rapoo-tray.exe`）与安装向导（`rapoo-tray-setup.exe`）元数据版本号同步升级至 1.2.1.0。
