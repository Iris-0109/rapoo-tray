#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}
#include <strsafe.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <atomic>
#include <string>

#include "PluginInterface.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "user32.lib")

#define IDI_PLUGIN_ICON 101

// Supported devices configuration table (easily extensible for future mice)
struct RapooDeviceDef {
    const wchar_t* pid_sub;
    const wchar_t* model_name;
    const wchar_t* mode_name;
};

static const RapooDeviceDef SUPPORTED_DEVICES[] = {
    // 雷柏 VT7 系列 (实测已验证)
    { L"pid_1460", L"雷柏 VT7", L"2.4G无线模式" },
    { L"pid_4660", L"雷柏 VT7", L"USB有线模式" },

    // 雷柏 VT3S 系列 (实测已验证)
    { L"pid_1406", L"雷柏 VT3S", L"2.4G无线模式" },
    { L"pid_4606", L"雷柏 VT3S", L"USB有线模式" },
    { L"pid_1410", L"雷柏 VT3S", L"2.4G无线模式" },
    { L"pid_1411", L"雷柏 VT3S", L"USB有线模式" },

    // 雷柏 VT3 MAX 系列 (实测已验证)
    { L"pid_1417", L"雷柏 VT3 MAX", L"2.4G无线模式" },
    { L"pid_4617", L"雷柏 VT3 MAX", L"USB有线模式" },
};

// Global shared state between worker thread and TrafficMonitor UI callbacks
static HMODULE g_hModule = NULL;
static HICON g_hPluginIcon = NULL;

static HANDLE g_hHidThread = NULL;
static HANDLE g_hStopEvent = NULL;

static std::atomic<int>  g_battery{ -1 };
static std::atomic<bool> g_isCharging{ false };
static std::atomic<int>  g_dpi{ 0 };
static std::atomic<int>  g_dpiLevel{ 0 };
static std::atomic<bool> g_connected{ false };

static WCHAR g_curModelName[64] = L"通用";
static WCHAR g_curModeName[32] = L"未连接";

// Dynamic device path scanner with generic fallback (single-pass enumeration)
static bool FindRapooDevicePath(WCHAR* outPath, DWORD maxLen, WCHAR* outModel, DWORD maxModelLen, WCHAR* outMode, DWORD maxModeLen) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA devData = { 0 };
    devData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    bool found = false;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &hidGuid, i, &devData); ++i) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devData, NULL, 0, &reqSize, NULL);
        if (reqSize == 0) continue;

        PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(reqSize);
        if (!pDetail) continue;

        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devData, pDetail, reqSize, NULL, NULL)) {
            WCHAR lowerPath[MAX_PATH];
            StringCchCopyW(lowerPath, MAX_PATH, pDetail->DevicePath);
            _wcslwr_s(lowerPath, MAX_PATH);

            if (wcsstr(lowerPath, L"vid_24ae")) {
                HANDLE hProbe = CreateFileW(
                    pDetail->DevicePath,
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
                if (hProbe == INVALID_HANDLE_VALUE) {
                    hProbe = CreateFileW(
                        pDetail->DevicePath,
                        0,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL
                    );
                }

                if (hProbe != INVALID_HANDLE_VALUE) {
                    PHIDP_PREPARSED_DATA pData = NULL;
                    if (HidD_GetPreparsedData(hProbe, &pData)) {
                        HIDP_CAPS caps;
                        if (HidP_GetCaps(pData, &caps) == HIDP_STATUS_SUCCESS) {
                            if (caps.UsagePage == 0xFF00) {
                                if (caps.Usage == 0x0002 || (caps.InputReportByteLength >= 19 && wcsstr(lowerPath, L"col09")) || (caps.InputReportByteLength == 19 && caps.Usage != 0x000E)) {
                                    StringCchCopyW(outPath, maxLen, pDetail->DevicePath);
                                    bool matched = false;
                                    for (const auto& dev : SUPPORTED_DEVICES) {
                                        if (wcsstr(lowerPath, dev.pid_sub)) {
                                            if (outModel && maxModelLen > 0) StringCchCopyW(outModel, maxModelLen, dev.model_name);
                                            if (outMode && maxModeLen > 0) StringCchCopyW(outMode, maxModeLen, dev.mode_name);
                                            matched = true;
                                            break;
                                        }
                                    }
                                    if (!matched) {
                                        bool isWired = (wcsstr(lowerPath, L"pid_46") || wcsstr(lowerPath, L"pid_1411"));
                                        if (outModel && maxModelLen > 0) StringCchCopyW(outModel, maxModelLen, L"通用");
                                        if (outMode && maxModeLen > 0) StringCchCopyW(outMode, maxModeLen, isWired ? L"USB有线模式" : L"2.4G无线模式");
                                    }
                                    found = true;
                                }
                            }
                        }
                        HidD_FreePreparsedData(pData);
                    }
                    CloseHandle(hProbe);
                }

                if (found) {
                    free(pDetail);
                    break;
                }
            }
        }
        free(pDetail);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return found;
}

// Background worker thread for low-overhead asynchronous HID polling
static DWORD WINAPI HidWorkerThread(LPVOID lpParam) {
    WCHAR devPath[MAX_PATH] = { 0 };
    WCHAR modelBuf[64] = { 0 };
    WCHAR modeBuf[32] = { 0 };

    while (WaitForSingleObject(g_hStopEvent, 200) == WAIT_TIMEOUT) {
        if (!FindRapooDevicePath(devPath, MAX_PATH, modelBuf, 64, modeBuf, 32)) {
            g_connected.store(false);
            Sleep(1500);
            continue;
        }

        HANDLE hDev = CreateFileW(
            devPath,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL
        );

        if (hDev == INVALID_HANDLE_VALUE) {
            g_connected.store(false);
            Sleep(1500);
            continue;
        }

        StringCchCopyW(g_curModelName, ARRAYSIZE(g_curModelName), modelBuf);
        StringCchCopyW(g_curModeName, ARRAYSIZE(g_curModeName), modeBuf);
        g_connected.store(true);

        HANDLE hReadEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        OVERLAPPED ov = { 0 };
        ov.hEvent = hReadEvent;

        BYTE buf[65] = { 0 };
        DWORD bytesRead = 0;

        while (WaitForSingleObject(g_hStopEvent, 0) == WAIT_TIMEOUT) {
            ResetEvent(hReadEvent);
            BOOL ok = ReadFile(hDev, buf, 65, &bytesRead, &ov);
            if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    HANDLE waitHandles[2] = { g_hStopEvent, hReadEvent };
                    DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                    if (waitRes == WAIT_OBJECT_0) {
                        CancelIo(hDev);
                        break;
                    } else if (waitRes == WAIT_OBJECT_0 + 1) {
                        if (!GetOverlappedResult(hDev, &ov, &bytesRead, FALSE)) {
                            break;
                        }
                    } else {
                        CancelIo(hDev);
                        break;
                    }
                } else {
                    break;
                }
            }

            if (bytesRead >= 9 && buf[0] == 0x07 && buf[1] == 0x20) {
                int level = (int)buf[2] + 1;
                int dpi   = (int)buf[3] | ((int)buf[4] << 8);
                BYTE statusByte = buf[7];
                BYTE rawBat     = buf[8];
                BYTE extraByte  = (bytesRead > 9) ? buf[9] : 0;

                // 充电状态检测 (与 rapoo-tray 硬件状态机严格一致)
                bool charging = ((statusByte & 0x02) != 0 || statusByte == 0x02 || statusByte == 0x03 ||
                                 extraByte == 0x01 || extraByte == 0x02);

                // 电量滤波：防止 USB 握手瞬态 0/1% 导致跳闪
                int bat = rawBat;
                if (rawBat <= 1) {
                    int cached = g_battery.load();
                    if (cached >= 2 && cached <= 100) {
                        bat = cached;
                    } else {
                        bat = (rawBat == 0) ? 100 : rawBat;
                    }
                } else if (rawBat > 100) {
                    bat = 100;
                }

                g_battery.store(bat);
                g_isCharging.store(charging);
                g_dpi.store(dpi);
                g_dpiLevel.store(level);
                g_connected.store(true);
            }
        }

        g_connected.store(false);
        g_isCharging.store(false);
        CloseHandle(hReadEvent);
        CloseHandle(hDev);
        Sleep(1000);
    }

    return 0;
}

static void StartHidWorker() {
    if (!g_hStopEvent) {
        g_hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    }
    if (!g_hHidThread) {
        g_hHidThread = CreateThread(NULL, 0, HidWorkerThread, NULL, 0, NULL);
    }
}

static void StopHidWorker() {
    if (g_hStopEvent) {
        SetEvent(g_hStopEvent);
    }
    if (g_hHidThread) {
        WaitForSingleObject(g_hHidThread, 2000);
        CloseHandle(g_hHidThread);
        g_hHidThread = NULL;
    }
    if (g_hStopEvent) {
        CloseHandle(g_hStopEvent);
        g_hStopEvent = NULL;
    }
}

// -------------------------------------------------------------
// TrafficMonitor Display Items (Text Mode)
// -------------------------------------------------------------

// Item 1: Mouse Battery Item
class CMiniBatteryItem : public IPluginItem {
public:
    const wchar_t* GetItemName() const override { return L"鼠标电量"; }
    const wchar_t* GetItemId() const override { return L"rapoo_mouse_battery"; }
    const wchar_t* GetItemLableText() const override { return L"M:"; }
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override { return L"⚡100%"; }
    bool IsCustomDraw() const override { return false; }
};

// Item 2: Mouse DPI Item
class CMiniDpiItem : public IPluginItem {
public:
    const wchar_t* GetItemName() const override { return L"鼠标DPI"; }
    const wchar_t* GetItemId() const override { return L"rapoo_mouse_dpi"; }
    const wchar_t* GetItemLableText() const override { return L"DPI:"; }
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override { return L"16000"; }
    bool IsCustomDraw() const override { return false; }
};

// -------------------------------------------------------------
// TrafficMonitor Plugin Core (ITMPlugin)
// -------------------------------------------------------------

class CRapooPlugin : public ITMPlugin {
public:
    CRapooPlugin() {
        m_batteryStr[0] = L'\0';
        m_dpiStr[0] = L'\0';
        m_tooltipStr[0] = L'\0';
    }

    int GetAPIVersion() const override { return 8; }

    IPluginItem* GetItem(int index) override {
        if (index == 0) return &m_batteryItem;
        if (index == 1) return &m_dpiItem;
        return nullptr;
    }

    void DataRequired() override {
        bool connected = g_connected.load();
        int bat = g_battery.load();
        bool charging = g_isCharging.load();
        int dpi = g_dpi.load();
        int level = g_dpiLevel.load();

        if (connected) {
            if (bat >= 0) {
                if (charging) {
                    swprintf_s(m_batteryStr, L"⚡%d%%", bat);
                } else {
                    swprintf_s(m_batteryStr, L"%d%%", bat);
                }
            } else {
                wcscpy_s(m_batteryStr, charging ? L"⚡--" : L"--");
            }

            if (dpi > 0) {
                swprintf_s(m_dpiStr, L"%d", dpi);
            } else {
                wcscpy_s(m_dpiStr, L"--");
            }

            WCHAR batDetail[64];
            if (bat >= 0) {
                if (charging) {
                    swprintf_s(batDetail, L"%d%% (⚡ 正在充电)", bat);
                } else {
                    swprintf_s(batDetail, L"%d%%", bat);
                }
            } else {
                wcscpy_s(batDetail, charging ? L"-- (⚡ 正在充电)" : L"--");
            }

            swprintf_s(m_tooltipStr, L"%s (%s)\n剩余电量: %s\n当前档位: %s (第 %d 档)",
                       g_curModelName, g_curModeName, batDetail, m_dpiStr, level);
        } else {
            // Disconnected or sleeping
            wcscpy_s(m_batteryStr, L"--");
            wcscpy_s(m_dpiStr, L"--");
            wcscpy_s(m_tooltipStr, L"雷柏鼠标: 未连接或休眠中");
        }
    }

    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
        case TMI_NAME:        return L"雷柏鼠标监控插件";
        case TMI_DESCRIPTION: return L"在任务栏与悬浮窗实时显示雷柏鼠标电量与DPI";
        case TMI_AUTHOR:      return L"Iris";
        case TMI_COPYRIGHT:   return L"Copyright (C) 2026 Iris";
        case TMI_VERSION:     return L"1.0.0";
        case TMI_URL:         return L"https://github.com/Iris-0109/rapoo-tray";
        default:              return L"";
        }
    }

    const wchar_t* GetTooltipInfo() override {
        return m_tooltipStr;
    }

    void* GetPluginIcon() override {
        if (!g_hPluginIcon && g_hModule) {
            g_hPluginIcon = (HICON)LoadImageW(g_hModule, MAKEINTRESOURCEW(IDI_PLUGIN_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        }
        return (void*)g_hPluginIcon;
    }

    const wchar_t* GetBatteryText() const { return m_batteryStr; }
    const wchar_t* GetDpiText() const { return m_dpiStr; }

private:
    CMiniBatteryItem m_batteryItem;
    CMiniDpiItem     m_dpiItem;

    WCHAR m_batteryStr[32];
    WCHAR m_dpiStr[32];
    WCHAR m_tooltipStr[256];
};

static CRapooPlugin g_plugin;

const wchar_t* CMiniBatteryItem::GetItemValueText() const {
    return g_plugin.GetBatteryText();
}

const wchar_t* CMiniDpiItem::GetItemValueText() const {
    return g_plugin.GetDpiText();
}

// -------------------------------------------------------------
// DLL Lifecycle & Export Entry Point
// -------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        StartHidWorker();
        break;
    case DLL_PROCESS_DETACH:
        StopHidWorker();
        if (g_hPluginIcon) {
            DestroyIcon(g_hPluginIcon);
            g_hPluginIcon = NULL;
        }
        break;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance() {
    return &g_plugin;
}
