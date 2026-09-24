#include "device_manager.h"
#include <hidsdi.h>
#include <setupapi.h>
#include <strsafe.h>
#include <algorithm>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace Device {

static const WCHAR* REG_SUBKEY = L"Software\\rapoo-tray";

static State g_currentState;
static StateCallback g_callback = nullptr;
static HWND g_hNotifyWnd = NULL;

static HANDLE g_hWorkerThread = NULL;
static HANDLE g_hStopEvent = NULL;
static HANDLE g_hDevChangeEvent = NULL;

static CRITICAL_SECTION g_csDevIO;
static CRITICAL_SECTION g_csState;
static HANDLE g_hControlDev = INVALID_HANDLE_VALUE;
static HANDLE g_hFeatureDev = INVALID_HANDLE_VALUE;

static int g_cachedBattery = 100;

// Registry helper for persistent settings
static DWORD LoadRegistryDword(const WCHAR* valName, DWORD defVal) {
    HKEY hKey;
    DWORD val = defVal;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_SUBKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0, size = sizeof(DWORD);
        RegQueryValueExW(hKey, valName, NULL, &type, (LPBYTE)&val, &size);
        RegCloseKey(hKey);
    }
    return val;
}

static void SaveRegistryDword(const WCHAR* valName, DWORD val) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_SUBKEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, valName, 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

static bool SendRawCommand(BYTE bank, BYTE addr, const BYTE* pData, BYTE len) {
    EnterCriticalSection(&g_csDevIO);
    if (g_hControlDev == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }

    BYTE buf[33] = {0};
    DWORD pktSize = Rapoo::BuildWriteCommand(bank, addr, pData, len, buf, sizeof(buf));

    DWORD written = 0;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    BOOL ok = WriteFile(g_hControlDev, buf, pktSize, &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 800) == WAIT_OBJECT_0) {
            GetOverlappedResult(g_hControlDev, &ov, &written, FALSE);
            ok = (written == pktSize);
        } else {
            CancelIo(g_hControlDev);
            ok = FALSE;
        }
    }
    CloseHandle(ov.hEvent);
    LeaveCriticalSection(&g_csDevIO);
    return (ok && written == pktSize);
}

static bool PingDevice(int& outHz) {
    EnterCriticalSection(&g_csDevIO);
    if (g_hControlDev == INVALID_HANDLE_VALUE || g_hFeatureDev == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }

    BYTE outBuf[33] = {0};
    DWORD pktSize = Rapoo::BuildReadCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_POLLING_HZ, 1, outBuf, sizeof(outBuf));

    DWORD written = 0;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    BOOL wOk = WriteFile(g_hControlDev, outBuf, pktSize, &written, &ov);
    if (!wOk && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 300) == WAIT_OBJECT_0) {
            GetOverlappedResult(g_hControlDev, &ov, &written, FALSE);
            wOk = (written == pktSize);
        } else {
            CancelIo(g_hControlDev);
            wOk = FALSE;
        }
    }
    CloseHandle(ov.hEvent);

    if (!wOk) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }

    Sleep(20);

    BYTE featBuf[33] = {0};
    featBuf[0] = Rapoo::REPORT_ID_FEATURE;
    BOOL fOk = HidD_GetFeature(g_hFeatureDev, featBuf, sizeof(featBuf));

    BYTE readVal = 0;
    if (!fOk || !Rapoo::ParseFeatureRead(featBuf, sizeof(featBuf), 1, &readVal)) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }

    outHz = Rapoo::CodeToPollingHz(readVal);
    LeaveCriticalSection(&g_csDevIO);
    return true;
}


struct RapooModelEntry {
    const WCHAR* pidKeyword;
    const WCHAR* modelName;
};

// 仅收录经由实体硬件测试验证的数据，严禁未经证实的推测
static const RapooModelEntry VERIFIED_MODELS[] = {
    // 雷柏 VT7 系列 (实测已验证)
    { L"1460", L"雷柏 VT7" },
    { L"4660", L"雷柏 VT7" },

    // 雷柏 VT3S 系列 (实测已验证)
    { L"1406", L"雷柏 VT3S" },
    { L"1410", L"雷柏 VT3S" },
    { L"4606", L"雷柏 VT3S" },
    { L"1411", L"雷柏 VT3S" },

    // 雷柏 VT3 MAX 系列 (实测已验证 - PR #3 by @sAchNMN)
    { L"1417", L"雷柏 VT3 MAX" },
};

static void ResolveRapooModelName(const WCHAR* targetPid, WCHAR* outModel, DWORD maxModelLen) {
    if (!outModel || maxModelLen == 0) return;

    for (const auto& entry : VERIFIED_MODELS) {
        if (wcsstr(targetPid, entry.pidKeyword)) {
            StringCchCopyW(outModel, maxModelLen, entry.modelName);
            return;
        }
    }

    // 通用类兜底：兼容所有雷柏二代 Nordic 架构 (54L15/3950) 未打标机型
    StringCchCopyW(outModel, maxModelLen, L"通用");
}

static bool FindRapooEndpoints(WCHAR* pathStatus, WCHAR* pathControl, WCHAR* pathFeature, WCHAR* outModel, DWORD maxModelLen, bool* outIsWired) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA devData = {0};
    devData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    pathStatus[0] = 0;
    pathControl[0] = 0;
    pathFeature[0] = 0;

    WCHAR targetPid[32] = {0};
    bool isWired = false;

    // Phase 1: Determine the target device.
    // First scan: look specifically for wired Rapoo mice (pid_46xx or pid_1411).
    // If a wired device is found, prioritize it over wireless dongles!
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &hidGuid, i, &devData); ++i) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devData, NULL, 0, &reqSize, NULL);
        if (reqSize == 0) continue;

        PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(reqSize);
        if (!pDetail) continue;

        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devData, pDetail, reqSize, NULL, NULL)) {
            WCHAR lower[MAX_PATH];
            StringCchCopyW(lower, MAX_PATH, pDetail->DevicePath);
            _wcslwr_s(lower, MAX_PATH);

            if (wcsstr(lower, L"vid_24ae")) {
                const WCHAR* pPid = wcsstr(lower, L"pid_");
                if (pPid) {
                    if (wcsstr(pPid, L"pid_46") || wcsstr(pPid, L"pid_1411")) {
                        StringCchCopyNW(targetPid, 32, pPid, 8);
                        isWired = true;
                        free(pDetail);
                        break;
                    }
                }
            }
        }
        free(pDetail);
    }

    // Second scan: if no wired device found, look for wireless dongles
    if (targetPid[0] == 0) {
        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &hidGuid, i, &devData); ++i) {
            DWORD reqSize = 0;
            SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devData, NULL, 0, &reqSize, NULL);
            if (reqSize == 0) continue;

            PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(reqSize);
            if (!pDetail) continue;

            pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devData, pDetail, reqSize, NULL, NULL)) {
                WCHAR lower[MAX_PATH];
                StringCchCopyW(lower, MAX_PATH, pDetail->DevicePath);
                _wcslwr_s(lower, MAX_PATH);

                if (wcsstr(lower, L"vid_24ae")) {
                    const WCHAR* pPid = wcsstr(lower, L"pid_");
                    if (pPid) {
                        StringCchCopyNW(targetPid, 32, pPid, 8);
                        isWired = false;
                        free(pDetail);
                        break;
                    }
                }
            }
            free(pDetail);
        }
    }

    if (targetPid[0] == 0) {
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return false;
    }

    // Phase 2: Find the 3 required endpoints for the target PID
    bool foundStatus = false;
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

            if (wcsstr(lowerPath, L"vid_24ae") && wcsstr(lowerPath, targetPid)) {
                HANDLE hProbe = CreateFileW(
                    pDetail->DevicePath,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
                if (hProbe == INVALID_HANDLE_VALUE) {
                    hProbe = CreateFileW(
                        pDetail->DevicePath,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL
                    );
                }
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
                                if (caps.Usage == 0x000E || caps.OutputReportByteLength == 33) {
                                    StringCchCopyW(pathControl, MAX_PATH, pDetail->DevicePath);
                                } else if (caps.Usage == 0x000F || (caps.FeatureReportByteLength == 33 && caps.Usage != 0x0010)) {
                                    StringCchCopyW(pathFeature, MAX_PATH, pDetail->DevicePath);
                                } else if (caps.Usage == 0x0002 || (caps.InputReportByteLength >= 19 && wcsstr(lowerPath, L"col09")) || (caps.InputReportByteLength == 19 && caps.Usage != 0x000E)) {
                                    StringCchCopyW(pathStatus, MAX_PATH, pDetail->DevicePath);
                                    foundStatus = true;
                                }
                            }
                        }
                        HidD_FreePreparsedData(pData);
                    }
                    CloseHandle(hProbe);
                }
            }
        }
        free(pDetail);
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);

    if (foundStatus) {
        if (outModel && maxModelLen > 0) {
            ResolveRapooModelName(targetPid, outModel, maxModelLen);
        }
        if (outIsWired) {
            *outIsWired = isWired;
        }
    }

    return foundStatus;
}

static DWORD WINAPI HidWorkerThread(LPVOID lpParam) {
    WCHAR pathStatus[MAX_PATH] = {0};
    WCHAR pathControl[MAX_PATH] = {0};
    WCHAR pathFeature[MAX_PATH] = {0};
    WCHAR modelBuf[64] = {0};
    bool isWired = false;

    // Load user's saved sleep timeout from registry (default 10 min)
    EnterCriticalSection(&g_csState);
    g_currentState.sleepMinutes = (int)LoadRegistryDword(L"SleepTimeout", 10);
    g_currentState.sleepMinutes = Rapoo::ClampSleepMinutes(g_currentState.sleepMinutes);
    LeaveCriticalSection(&g_csState);

    while (WaitForSingleObject(g_hStopEvent, 200) == WAIT_TIMEOUT) {
        if (!FindRapooEndpoints(pathStatus, pathControl, pathFeature, modelBuf, 64, &isWired)) {
            EnterCriticalSection(&g_csState);
            bool wasConn = g_currentState.isConnected;
            g_currentState.isConnected = false;
            g_currentState.isCharging = false;
            State copySt = g_currentState;
            LeaveCriticalSection(&g_csState);
            if (wasConn && g_callback) g_callback(copySt, CHANGE_CONNECTED | CHANGE_BATTERY);
            Sleep(1000);
            continue;
        }

        HANDLE hStatus = CreateFileW(
            pathStatus,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL
        );

        if (hStatus == INVALID_HANDLE_VALUE) {
            EnterCriticalSection(&g_csState);
            bool wasConn = g_currentState.isConnected;
            g_currentState.isConnected = false;
            g_currentState.isCharging = false;
            State copySt = g_currentState;
            LeaveCriticalSection(&g_csState);
            if (wasConn && g_callback) g_callback(copySt, CHANGE_CONNECTED | CHANGE_BATTERY);
            Sleep(1000);
            continue;
        }

        EnterCriticalSection(&g_csDevIO);
        if (pathControl[0]) {
            g_hControlDev = CreateFileW(
                pathControl,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                NULL
            );
            if (g_hControlDev == INVALID_HANDLE_VALUE) {
                g_hControlDev = CreateFileW(
                    pathControl,
                    GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED,
                    NULL
                );
            }
        }
        if (pathFeature[0]) {
            g_hFeatureDev = CreateFileW(
                pathFeature,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            if (g_hFeatureDev == INVALID_HANDLE_VALUE) {
                g_hFeatureDev = CreateFileW(
                    pathFeature,
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
            }
            if (g_hFeatureDev == INVALID_HANDLE_VALUE) {
                g_hFeatureDev = CreateFileW(
                    pathFeature,
                    0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
            }
        }
        LeaveCriticalSection(&g_csDevIO);

        EnterCriticalSection(&g_csState);
        StringCchCopyW(g_currentState.modelName, ARRAYSIZE(g_currentState.modelName), modelBuf);
        g_currentState.isWired = isWired;
        g_currentState.isConnected = true;
        if (isWired) {
            g_currentState.isCharging = true;
        }
        State connSt = g_currentState;
        LeaveCriticalSection(&g_csState);
        if (g_callback) g_callback(connSt, CHANGE_CONNECTED);

        // ClickSync A5 A3 unlock handshake
        if (g_hControlDev != INVALID_HANDLE_VALUE) {
            BYTE unlockBuf[33] = {0};
            DWORD uSize = Rapoo::BuildUnlockPacket(unlockBuf, sizeof(unlockBuf));
            DWORD written = 0;
            OVERLAPPED uOv = {0};
            uOv.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
            WriteFile(g_hControlDev, unlockBuf, uSize, &written, &uOv);
            WaitForSingleObject(uOv.hEvent, 200);
            CloseHandle(uOv.hEvent);
        }

        // Query current hardware polling rate
        int curHz = 1000;
        if (PingDevice(curHz)) {
            EnterCriticalSection(&g_csState);
            g_currentState.pollingHz = curHz;
            State pollSt = g_currentState;
            LeaveCriticalSection(&g_csState);
            if (g_callback) g_callback(pollSt, CHANGE_POLLING);
        }

        // Push saved sleep timeout to device hardware
        int curSleepMin = 10;
        EnterCriticalSection(&g_csState);
        curSleepMin = g_currentState.sleepMinutes;
        LeaveCriticalSection(&g_csState);
        BYTE sleepCode = (BYTE)curSleepMin;
        SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_SLEEP_TIMEOUT, &sleepCode, 1);

        HANDLE hReadEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        OVERLAPPED ov = {0};
        ov.hEvent = hReadEvent;

        BYTE buf[65] = {0};
        DWORD bytesRead = 0;

        while (WaitForSingleObject(g_hStopEvent, 0) == WAIT_TIMEOUT) {
            ResetEvent(hReadEvent);
            BOOL ok = ReadFile(hStatus, buf, 19, &bytesRead, &ov);
            if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    HANDLE waitHandles[3] = { g_hStopEvent, hReadEvent, g_hDevChangeEvent };
                    DWORD waitMs = 3000;
                    DWORD waitRes = WaitForMultipleObjects(3, waitHandles, FALSE, waitMs);

                    if (waitRes == WAIT_OBJECT_0) {
                        CancelIo(hStatus);
                        break;
                    } else if (waitRes == WAIT_OBJECT_0 + 2) {
                        // PnP Debounce: Windows composite device sends multiple arrival messages.
                        // Drain any consecutive events within 300ms before handling reconnect.
                        Sleep(300);
                        while (WaitForSingleObject(g_hDevChangeEvent, 0) == WAIT_OBJECT_0) {
                            ResetEvent(g_hDevChangeEvent);
                            Sleep(50);
                        }
                        CancelIo(hStatus);
                        break;
                    } else if (waitRes == WAIT_TIMEOUT) {
                        CancelIo(hStatus);
                        GetOverlappedResult(hStatus, &ov, &bytesRead, FALSE);

                        if (!isWired) {
                            int hz = 0;
                            bool alive = PingDevice(hz);
                            if (!alive) {
                                EnterCriticalSection(&g_csState);
                                bool wasConn = g_currentState.isConnected;
                                g_currentState.isConnected = false;
                                g_currentState.isCharging = false;
                                State copySt = g_currentState;
                                LeaveCriticalSection(&g_csState);
                                if (wasConn && g_callback) g_callback(copySt, CHANGE_CONNECTED | CHANGE_BATTERY);
                            } else {
                                EnterCriticalSection(&g_csState);
                                bool wasConn = g_currentState.isConnected;
                                g_currentState.isConnected = true;
                                State copySt = g_currentState;
                                LeaveCriticalSection(&g_csState);
                                if (!wasConn && g_callback) g_callback(copySt, CHANGE_CONNECTED);
                            }
                        }
                        continue;
                    } else if (waitRes == WAIT_OBJECT_0 + 1) {
                        if (!GetOverlappedResult(hStatus, &ov, &bytesRead, FALSE)) {
                            break;
                        }
                    } else {
                        CancelIo(hStatus);
                        break;
                    }
                } else {
                    break;
                }
            }

            // Parse status broadcast using ClickSync standard parser
            Rapoo::DeviceStatus devStatus;
            if (Rapoo::ParseStatusReport(buf, bytesRead, devStatus, g_cachedBattery)) {
                if (isWired) {
                    devStatus.isCharging = true;
                }

                DWORD mask = 0;
                State copySt;

                EnterCriticalSection(&g_csState);
                if (!g_currentState.isConnected) {
                    g_currentState.isConnected = true;
                    mask |= CHANGE_CONNECTED;
                }

                if (devStatus.dpiLevel != g_currentState.dpiLevel || devStatus.dpiX != g_currentState.dpiX) {
                    g_currentState.dpiLevel = devStatus.dpiLevel;
                    g_currentState.dpiX = devStatus.dpiX;
                    g_currentState.dpiY = devStatus.dpiY;
                    mask |= CHANGE_DPI;
                }

                if (devStatus.battery != g_currentState.battery || devStatus.isCharging != g_currentState.isCharging) {
                    g_currentState.battery = devStatus.battery;
                    g_currentState.isCharging = devStatus.isCharging;
                    g_cachedBattery = devStatus.battery;
                    mask |= CHANGE_BATTERY;
                }
                copySt = g_currentState;
                LeaveCriticalSection(&g_csState);

                if (mask != 0 && g_callback) {
                    g_callback(copySt, mask);
                }
            }
        }

        EnterCriticalSection(&g_csState);
        bool wasConn = g_currentState.isConnected;
        g_currentState.isConnected = false;
        g_currentState.isCharging = false;
        State copySt = g_currentState;
        LeaveCriticalSection(&g_csState);
        if (wasConn && g_callback) g_callback(copySt, CHANGE_CONNECTED | CHANGE_BATTERY);

        CloseHandle(hReadEvent);
        CloseHandle(hStatus);

        EnterCriticalSection(&g_csDevIO);
        if (g_hControlDev != INVALID_HANDLE_VALUE) {
            CloseHandle(g_hControlDev);
            g_hControlDev = INVALID_HANDLE_VALUE;
        }
        if (g_hFeatureDev != INVALID_HANDLE_VALUE) {
            CloseHandle(g_hFeatureDev);
            g_hFeatureDev = INVALID_HANDLE_VALUE;
        }
        LeaveCriticalSection(&g_csDevIO);

        Sleep(300);
    }

    return 0;
}

bool Start(HWND hNotifyWnd, StateCallback callback) {
    g_hNotifyWnd = hNotifyWnd;
    g_callback = callback;

    InitializeCriticalSection(&g_csDevIO);
    InitializeCriticalSection(&g_csState);
    g_hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_hDevChangeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);

    g_hWorkerThread = CreateThread(NULL, 0, HidWorkerThread, NULL, 0, NULL);
    return (g_hWorkerThread != NULL);
}

void Stop() {
    if (g_hStopEvent) SetEvent(g_hStopEvent);
    if (g_hWorkerThread) {
        WaitForSingleObject(g_hWorkerThread, 2000);
        CloseHandle(g_hWorkerThread);
        g_hWorkerThread = NULL;
    }
    if (g_hStopEvent) {
        CloseHandle(g_hStopEvent);
        g_hStopEvent = NULL;
    }
    if (g_hDevChangeEvent) {
        CloseHandle(g_hDevChangeEvent);
        g_hDevChangeEvent = NULL;
    }
    DeleteCriticalSection(&g_csDevIO);
    DeleteCriticalSection(&g_csState);
}

void NotifyDeviceChange() {
    if (g_hDevChangeEvent) {
        SetEvent(g_hDevChangeEvent);
    }
}

bool SetPollingRate(int hz) {
    BYTE code = Rapoo::PollingHzToCode(hz);
    if (SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_POLLING_HZ, &code, 1)) {
        EnterCriticalSection(&g_csState);
        g_currentState.pollingHz = hz;
        State copySt = g_currentState;
        LeaveCriticalSection(&g_csState);
        if (g_callback) g_callback(copySt, CHANGE_POLLING);
        return true;
    }
    return false;
}

bool SetSleepTimeout(int minutes) {
    minutes = Rapoo::ClampSleepMinutes(minutes);
    BYTE code = (BYTE)minutes;
    if (SendRawCommand(Rapoo::BANK_SYSTEM, Rapoo::ADDR_SLEEP_TIMEOUT, &code, 1)) {
        EnterCriticalSection(&g_csState);
        g_currentState.sleepMinutes = minutes;
        State copySt = g_currentState;
        LeaveCriticalSection(&g_csState);
        SaveRegistryDword(L"SleepTimeout", (DWORD)minutes);
        if (g_callback) g_callback(copySt, CHANGE_SLEEP);
        return true;
    }
    return false;
}

bool RefreshPollingRate() {
    int curHz = 1000;
    if (PingDevice(curHz)) {
        EnterCriticalSection(&g_csState);
        bool changed = (g_currentState.pollingHz != curHz);
        if (changed) g_currentState.pollingHz = curHz;
        State copySt = g_currentState;
        LeaveCriticalSection(&g_csState);
        if (changed && g_callback) g_callback(copySt, CHANGE_POLLING);
        return true;
    }
    return false;
}

State GetCurrentState() {
    EnterCriticalSection(&g_csState);
    State st = g_currentState;
    LeaveCriticalSection(&g_csState);
    return st;
}

} // namespace Device
