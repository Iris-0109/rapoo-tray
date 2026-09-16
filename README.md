# rapoo-tray

雷柏 VT7 V2 鼠标的 Windows 系统托盘辅助工具，纯 C++ (Win32 API) 实现。

主要用于在不需要常驻官方驱动的情况下，在系统托盘直接查看鼠标剩余电量与当前 DPI 档位。

---

## 主要功能

- **托盘电量图标**：
  - 图标内直接显示电量百分比数字，图标随剩余电量动态缩短。
  - 支持 3 级颜色状态：电量 >20% 为绿色，11%~20% 为黄色，<=10% 为红色。
- **DPI 切换提示 (OSD)**：
  - 按下鼠标 DPI 键时弹出悬浮提示窗，显示当前 DPI 数值、档位及电量，1.4 秒后自动淡出消失。
  - 单击托盘图标也可手动查看该提示窗。
- **开机自启动**：
  - 托盘右键菜单可勾选“开机自启动”（写入注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`）。
- **双模支持**：
  - 支持 2.4G 无线接收器模式（`VID_24AE & PID_1460`）与 USB 有线直连模式（`VID_24AE & PID_4660`）。

---

## 协议说明

程序通过 USB HID 接口（`UsagePage: 0xFF00`, `Usage: 0x0002`）接收鼠标状态数据包（Report ID `0x07`，长度 19 字节）：

| 字节偏移 | 含义 | 说明 |
| :--- | :--- | :--- |
| `Byte [0]` | Report ID | 固定为 `0x07` |
| `Byte [1]` | 设备标识 | `0x20`（无线接收器数据） |
| `Byte [2]` | DPI 档位 | 0-indexed（0 代表第 1 档，1 代表第 2 档…） |
| `Byte [3..4]` | X 轴 DPI | 小端序 16 位整数（例如 `0x04B0` = 1200） |
| `Byte [5..6]` | Y 轴 DPI | 小端序 16 位整数 |
| `Byte [7]` | 活跃状态 | `0x01` 活跃，`0x00` 待机/休眠 |
| `Byte [8]` | 物理电量 | `0x00` ~ `0x64`（0% ~ 100%） |

---

## 目录结构

```text
rapoo-tray/
├── CMakeLists.txt              # CMake 构建配置
├── build.bat                   # 一键编译脚本 (MinGW / MSVC)
├── LICENSE                     # MIT 许可证
├── README.md                   # 说明文档
├── src/
│   └── main.cpp                # 核心程序源码
├── res/
│   ├── app.rc                  # 资源定义 (版本信息)
│   └── app.manifest            # DPI 感知与公共控件配置
└── installer/
    ├── installer.cpp           # 安装程序源码
    ├── installer.rc            # 安装程序资源
    └── build_installer.bat     # 安装程序编译脚本
```

---

## 编译方法

### 1. 使用一键脚本
直接运行根目录下的 `build.bat`：
```cmd
build.bat
```
脚本会自动探测系统中的 `g++` (MinGW) 或 `cl.exe` (MSVC)，生成可执行文件到 `bin/rapoo-tray.exe`。

### 2. 使用 CMake
```cmd
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### 3. 构建安装程序
进入 `installer` 目录运行：
```cmd
cd installer
build_installer.bat
```
将在 `dist/` 目录下生成 `rapoo-tray-setup.exe`。
