#include <windows.h>
#include <shellapi.h>
#include <dbt.h>
#include <hidsdi.h>
#include <strsafe.h>
#include "rapoo_protocol.h"
#include "device_manager.h"
#include "osd_window.h"
#include "tray_menu.h"

static const UINT WM_APP_TRAYMSG = WM_APP + 1;
static const UINT WM_APP_STATE_UPDATE = WM_APP + 2;

static HWND g_hMainWnd = NULL;
static NOTIFYICONDATAW g_nid = {0};
static UINT g_uTaskbarRestartMsg = 0;
static HICON g_hCurrentTrayIcon = NULL;

static void RefreshTrayUI(const Device::State& state) {
    if (!g_hMainWnd) return;

    bool isDark = true;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 1, size = sizeof(DWORD), type = 0;
        if (RegQueryValueExW(hKey, L"SystemUsesLightTheme", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            isDark = (val == 0);
        }
        RegCloseKey(hKey);
    }

    int iconSize = GetSystemMetrics(SM_CXSMICON);
    if (iconSize <= 0) iconSize = 16;

    HICON hNewIcon = Tray::CreateBatteryIcon(state.battery, state.isCharging, state.isConnected, iconSize, isDark);
    if (hNewIcon) {
        g_nid.hIcon = hNewIcon;
        Tray::UpdateTooltip(g_nid, state);
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);

        if (g_hCurrentTrayIcon) {
            DestroyIcon(g_hCurrentTrayIcon);
        }
        g_hCurrentTrayIcon = hNewIcon;
    }
}

static void OnDeviceStateChanged(const Device::State& state, DWORD changeMask) {
    if (!g_hMainWnd) return;

    // Show dynamic OSD on DPI changes
    if (changeMask & Device::CHANGE_DPI) {
        Osd::ShowDpiUpdate(
            state.dpiLevel,
            state.dpiX,
            state.dpiY,
            state.battery,
            state.pollingHz,
            state.isCharging,
            state.isWired,
            state.modelName
        );
    }

    PostMessageW(g_hMainWnd, WM_APP_STATE_UPDATE, 0, 0);
}

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_uTaskbarRestartMsg) {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        RefreshTrayUI(Device::GetCurrentState());
        return 0;
    }

    switch (msg) {
        case WM_APP_TRAYMSG: {
            if (lParam == WM_LBUTTONUP) {
                Device::State st = Device::GetCurrentState();
                if (st.isConnected) {
                    Osd::ShowDpiUpdate(
                        st.dpiLevel,
                        st.dpiX,
                        st.dpiY,
                        st.battery,
                        st.pollingHz,
                        st.isCharging,
                        st.isWired,
                        st.modelName
                    );
                } else {
                    const WCHAR* mName = st.modelName[0] ? st.modelName : L"雷柏游戏鼠标";
                    Osd::Show(mName, L"设备休眠 / 未连接", L"请移动鼠标唤醒或插上 USB 线");
                }
            } else if (lParam == WM_RBUTTONUP) {
                SetForegroundWindow(hWnd);
                Tray::ShowMenu(hWnd);
            } else if (lParam == WM_LBUTTONDBLCLK) {
                int s = Tray::CycleBatteryStyle();
                Osd::ShowStyleOsd(s);
                RefreshTrayUI(Device::GetCurrentState());
            }
            return 0;
        }

        case WM_APP_STATE_UPDATE: {
            RefreshTrayUI(Device::GetCurrentState());
            return 0;
        }

        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED: {
            RefreshTrayUI(Device::GetCurrentState());
            return 0;
        }

        case WM_DEVICECHANGE: {
            if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE || wParam == DBT_DEVNODES_CHANGED) {
                Device::NotifyDeviceChange();
            }
            return 0;
        }

        case WM_DESTROY: {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_hCurrentTrayIcon) {
                DestroyIcon(g_hCurrentTrayIcon);
                g_hCurrentTrayIcon = NULL;
            }
            PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // Single instance protection
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Local\\rapoo-traySingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    Tray::InitTheme();

    // Enable modern Per-Monitor V2 DPI awareness
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef BOOL (WINAPI *pfnSetDpiAwareV2)(DPI_AWARENESS_CONTEXT);
        pfnSetDpiAwareV2 fnSetDpiAwareV2 = (pfnSetDpiAwareV2)GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
        if (fnSetDpiAwareV2) {
            fnSetDpiAwareV2(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"RapooTrayMessageWnd";
    RegisterClassExW(&wc);

    g_hMainWnd = CreateWindowExW(
        0, wc.lpszClassName, L"RapooTray",
        WS_POPUP, 0, 0, 0, 0,
        NULL, NULL, hInstance, NULL
    );

    g_uTaskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // Initialize OSD Window
    Osd::Init(hInstance);

    // Register PnP Device notifications
    DEV_BROADCAST_DEVICEINTERFACE_W dbFilter = {0};
    dbFilter.dbcc_size = sizeof(dbFilter);
    dbFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    HidD_GetHidGuid(&dbFilter.dbcc_classguid);
    HDEVNOTIFY hDevNotify = RegisterDeviceNotificationW(g_hMainWnd, &dbFilter, DEVICE_NOTIFY_WINDOW_HANDLE | 0x00000004);

    // Register Notify Icon
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hMainWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAYMSG;
    g_nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"雷柏游戏鼠标 (正在连接...)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // Start background HID Device Manager
    Device::Start(g_hMainWnd, OnDeviceStateChanged);

    // Run message pump
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Clean exit
    if (hDevNotify) {
        UnregisterDeviceNotification(hDevNotify);
        hDevNotify = NULL;
    }
    Device::Stop();
    Osd::Cleanup();

    CloseHandle(hMutex);
    return 0;
}
