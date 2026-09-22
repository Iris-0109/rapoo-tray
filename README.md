<div align="center">
  <img src="res/logo.png" width="128" height="128" alt="rapoo-tray logo" />
  <h1>rapoo-tray</h1>
  <p><b>轻量级雷柏鼠标系统托盘电量与 DPI 监控助手</b></p>
  <p>纯 Win32 API 原生实现 · 告别臃肿官方驱动</p>

  <p>
    <a href="https://github.com/Iris-0109/rapoo-tray/releases"><img src="https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue?style=flat-square&logo=windows" alt="Platform" /></a>
    <img src="https://img.shields.io/badge/Language-C%2B%2B17%20%2F%20Win32-00599C?style=flat-square&logo=c%2B%2B" alt="Language" />
    <img src="https://img.shields.io/badge/License-MIT-green?style=flat-square" alt="License" />
  </p>
</div>

---

## 💡 项目简介

**rapoo-tray** 是一个纯原生 C++ / Win32 实现的轻量辅助工具。无需安装官方大型驱动，在 Windows 系统任务栏托盘即可实时查看电量数值与状态，并在切换 DPI 时提供精美的屏幕悬浮提示（OSD）。

---

## ✨ 主要功能

- ⚡ **极简轻量**：纯 Win32 原生 API 编写，无第三方臃肿框架与运行时依赖，运行时内存占用仅约 **2~3 MB**。
- 🚀 **开机自启动支持**：
  - 右键托盘图标可一键切换“开机自启动”（基于用户注册表 `HKCU`，绿色无管理员提权弹窗干扰）。
- 🔋 **托盘数字电量图标**：
  - 任务栏托盘图标直接内嵌显示当前剩余电量百分比。
  - 电池图标内部电量条随百分比动态填充。
  - 具备 3 级智能颜色预警：正常状态为绿色（>20%），中低电量为黄色（11%~20%），严重低电量为红色（≤10%）。
- 🎯 **DPI 悬浮提示 (OSD)**：
  - 按下鼠标 DPI 按键时，屏幕右下角自动弹出精致现代的半透明悬浮窗。
  - 显示当前识别到的设备型号、DPI 档位、X/Y 轴分辨率及实时电量，展示 1.4 秒后平滑淡出。
  - 单击托盘图标亦可手动唤出该提示面板。

---

## 🖱️ 支持设备列表 (Supported Devices)

| 设备型号 | 连接模式 | VID | PID | 支持状态 |
| :--- | :--- | :--- | :--- | :---: |
| **雷柏 VT7 系列** | 2.4G 无线接收器 | `0x24AE` | `0x1460` | ✅ 已实测支持 |
| **雷柏 VT7 系列** | USB 有线直连 | `0x24AE` | `0x4660` | ✅ 已实测支持 |
| **雷柏 VT3S 系列** | 2.4G 无线接收器 | `0x24AE` | `0x1406` | ✅ 已实测支持 |
| **雷柏 VT3S 系列** | USB 有线直连 | `0x24AE` | `0x4606` | ✅ 已实测支持 |
| **雷柏游戏鼠标（PID 1417）** | USB HID | `0x24AE` | `0x1417` | ✅ 已实测支持 |

> 📌 **欢迎贡献**：雷柏采用同系方案（如VT系列）的无线游戏鼠标协议大多互相兼容。欢迎拥有其他型号的小伙伴测试并提交 PR 扩充型号支持列表！

---

## 📥 下载与使用

请前往 [GitHub Releases](https://github.com/Iris-0109/rapoo-tray/releases) 下载最新版本：

1. **绿色免安装版** (`rapoo-tray.exe`)：
   - 下载后放置在任意你喜欢的文件夹，双击即可运行。
2. **轻量安装包版** (`rapoo-tray-setup.exe`)：
   - 仅约 200KB，一键将程序安装至本地应用程序目录，自动生成快捷方式并支持控制面板卸载。

---

## 🔬 技术协议解析 (Protocol Reverse Engineering)

程序通过 Windows HID API 与鼠标的专有通信接口交互（`UsagePage: 0xFF00`, `Usage: 0x0002`），以中断/轮询方式接收 Report ID 为 `0x07` 的 19 字节状态数据包：

| 字节偏移 (Offset) | 字段含义 | 说明与数据格式 |
| :--- | :--- | :--- |
| `Byte [0]` | Report ID | 固定为 `0x07` |
| `Byte [1]` | 设备标识 | 接收器模式为 `0x20` |
| `Byte [2]` | DPI 档位 | 从 `0` 开始（0 代表第 1 档，1 代表第 2 档…） |
| `Byte [3..4]` | X 轴 DPI | 16 位小端序整数（如 `0x04B0` = 1200 DPI） |
| `Byte [5..6]` | Y 轴 DPI | 16 位小端序整数 |
| `Byte [7]` | 活跃状态 | `0x01` 活跃 / 通信中，`0x00` 待机 / 休眠 |
| `Byte [8]` | 物理电量 | 百分比数值 `0x00` ~ `0x64`（0% ~ 100%） |

---

## 🛠️ 源码编译 (Build from Source)

本项目采用纯 Win32 C++17 编写，可在 Windows 下使用 MinGW (GCC) 或 MSVC 快速编译：

### 1. 使用一键脚本 (推荐)
直接运行根目录下的 `build.bat`：
```cmd
build.bat
```
脚本会自动探测系统中的 `g++` (MinGW) 或 `cl.exe` (MSVC)，构建完成后的产物将输出在 `bin/rapoo-tray.exe`。

### 2. 使用 CMake 构建
```cmd
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### 3. 构建原生安装向导
```cmd
cd installer
build_installer.bat
```
产物将输出至 `dist/rapoo-tray-setup.exe`。

---

## ⚠️ 免责声明 (Disclaimer)

1. 本项目为个人业余开源作品，旨在为雷柏无线鼠标用户提供轻量、免臃肿驱动的电量与 DPI 托盘监控体验，**非雷柏官方出品**。
2. “雷柏”与“RAPOO”及其商标与产品名称的所有权归深圳市雷柏科技股份有限公司所有。
3. 软件按“原样”（AS IS）提供，作者不对使用过程中可能出现的任何异常承担法律责任。

---

## 📄 开源许可证 (License)

本项目基于 [MIT License](LICENSE) 协议开源。
