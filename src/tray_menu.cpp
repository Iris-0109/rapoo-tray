#include "tray_menu.h"
#include "osd_window.h"
#include <dwmapi.h>
#include <strsafe.h>
#include <cstring>
#include <algorithm>
#include <vector>

namespace Tray {

static const WCHAR* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR* APP_NAME = L"rapoo-tray";

static HWND g_hParentAppWnd = NULL;
static HWND g_hAcrylicMenu = NULL;
static HWND g_hAcrylicSubMenu = NULL;
static HHOOK g_hMenuMouseHook = NULL;
static bool g_bModalLoop = false;

static int g_curDpi = 96;
static bool g_curDark = false;
static int g_activeSubId = 0; // 0: None, 1: Polling Hz, 2: Sleep Slider
static int g_mainHover = -1;
static int g_subHover = -1;

// Sleep slider state
static bool g_isDraggingSlider = false;
static int g_sliderVal = 10;
static const int PRESET_SLEEP[] = { 5, 10, 15, 30, 60 };

static inline int S(int val) {
    return MulDiv(val, g_curDpi, 96);
}

// Windows 11 / 10 Acrylic & Theme Hooks
typedef enum _ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_INVALID_STATE = 5
} ACCENT_STATE;

typedef struct _ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
} ACCENT_POLICY;

typedef struct _WINDOWCOMPOSITIONATTRIBDATA {
    DWORD Attrib;
    PVOID pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef BOOL (WINAPI *pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);
typedef BOOL (WINAPI *pfnShouldAppsUseDarkMode)();
typedef BOOL (WINAPI *pfnAllowDarkModeForWindow)(HWND, BOOL);
typedef void (WINAPI *pfnFlushMenuThemes)();

static pfnSetWindowCompositionAttribute fnSetWindowCompositionAttribute = NULL;
static pfnShouldAppsUseDarkMode fnShouldAppsUseDarkMode = NULL;
static pfnAllowDarkModeForWindow fnAllowDarkModeForWindow = NULL;
static pfnFlushMenuThemes fnFlushMenuThemes = NULL;

void InitTheme() {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        fnSetWindowCompositionAttribute = (pfnSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
    }
    HMODULE hUx = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUx) {
        fnShouldAppsUseDarkMode = (pfnShouldAppsUseDarkMode)GetProcAddress(hUx, MAKEINTRESOURCEA(132));
        fnAllowDarkModeForWindow = (pfnAllowDarkModeForWindow)GetProcAddress(hUx, MAKEINTRESOURCEA(133));
        fnFlushMenuThemes = (pfnFlushMenuThemes)GetProcAddress(hUx, MAKEINTRESOURCEA(136));
    }
}

static bool IsSystemDarkMode() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 1, size = sizeof(DWORD), type = 0;
        // Check SystemUsesLightTheme first since tray popup menus belong to the taskbar/system area
        if (RegQueryValueExW(hKey, L"SystemUsesLightTheme", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (val == 0);
        }
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (val == 0);
        }
        RegCloseKey(hKey);
    }
    if (fnShouldAppsUseDarkMode) return fnShouldAppsUseDarkMode();
    return false;
}

static void ApplyModernWindowStyle(HWND hWnd, bool isDark, int w = 0, int h = 0) {
    if (fnAllowDarkModeForWindow) {
        fnAllowDarkModeForWindow(hWnd, isDark ? TRUE : FALSE);
    }
    BOOL darkVal = isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(hWnd, 20, &darkVal, sizeof(darkVal)); // DWMWA_USE_IMMERSIVE_DARK_MODE

    // Request Windows 11 rounded corners (DWMWA_WINDOW_CORNER_PREFERENCE = 33)
    DWORD corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hWnd, 33, &corner, sizeof(corner));

    // Also clip window region with rounded corners for compatibility
    RECT rc = {0};
    GetWindowRect(hWnd, &rc);
    int width = (w > 0) ? w : (rc.right - rc.left);
    int height = (h > 0) ? h : (rc.bottom - rc.top);
    if (width > 0 && height > 0) {
        HRGN hRgn = CreateRoundRectRgn(0, 0, width + 1, height + 1, S(12), S(12));
        SetWindowRgn(hWnd, hRgn, TRUE);
    }

    if (fnSetWindowCompositionAttribute) {
        ACCENT_POLICY policy = {};
        if (isDark) {
            policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
            policy.AccentFlags = 0; // No DWM system rectangular border (eliminated white streaks)
            policy.GradientColor = 0xCC1A1B20; // AABBGGRR
        } else {
            // In light mode, disable acrylic blur-behind composition.
            // Standard GDI rendering leaves Alpha = 0, causing DWM acrylic composition to render as an opaque white wash.
            // Disabling it restores crisp, solid Win32 GDI rendering with full contrast.
            policy.AccentState = ACCENT_DISABLED;
            policy.AccentFlags = 0;
            policy.GradientColor = 0;
        }
        WINDOWCOMPOSITIONATTRIBDATA data = { 19, &policy, sizeof(policy) };
        fnSetWindowCompositionAttribute(hWnd, &data);
    }
}

static int g_batteryStyle = -1;

int GetBatteryStyle() {
    if (g_batteryStyle == -1) {
        HKEY hKey;
        DWORD val = 0, size = sizeof(DWORD), type = 0;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\rapoo-tray", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            if (RegQueryValueExW(hKey, L"BatteryStyle", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                g_batteryStyle = (int)val % 3;
            }
            RegCloseKey(hKey);
        }
        if (g_batteryStyle == -1) g_batteryStyle = 0;
    }
    return g_batteryStyle;
}

int CycleBatteryStyle() {
    g_batteryStyle = (GetBatteryStyle() + 1) % 3;
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\rapoo-tray", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD val = (DWORD)g_batteryStyle;
        RegSetValueExW(hKey, L"BatteryStyle", 0, REG_DWORD, (const BYTE*)&val, sizeof(DWORD));
        RegCloseKey(hKey);
    }
    return g_batteryStyle;
}

bool IsAutoRunEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR path[MAX_PATH];
        DWORD len = sizeof(path), type = 0;
        LSTATUS st = RegQueryValueExW(hKey, APP_NAME, NULL, &type, (LPBYTE)path, &len);
        RegCloseKey(hKey);
        return (st == ERROR_SUCCESS);
    }
    return false;
}

void ToggleAutoRun() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (IsAutoRunEnabled()) {
            RegDeleteValueW(hKey, APP_NAME);
        } else {
            WCHAR path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (const BYTE*)path, (DWORD)((wcslen(path) + 1) * sizeof(WCHAR)));
        }
        RegCloseKey(hKey);
    }
}

// --------------------------------------------------------------------------
// Submenu Window (Polling Rate & Sleep Slider)
// --------------------------------------------------------------------------

static void DismissSubMenu() {
    if (g_hAcrylicSubMenu) {
        HWND h = g_hAcrylicSubMenu;
        g_hAcrylicSubMenu = NULL;
        DestroyWindow(h);
    }
    g_activeSubId = 0;
    g_subHover = -1;
    g_isDraggingSlider = false;
}

void DismissAllMenus() {
    if (g_hMenuMouseHook) {
        UnhookWindowsHookEx(g_hMenuMouseHook);
        g_hMenuMouseHook = NULL;
    }
    DismissSubMenu();
    if (g_hAcrylicMenu) {
        HWND h = g_hAcrylicMenu;
        g_hAcrylicMenu = NULL;
        DestroyWindow(h);
    }
    g_bModalLoop = false;
    if (g_hParentAppWnd) {
        PostMessageW(g_hParentAppWnd, WM_NULL, 0, 0);
    }
}

static LRESULT CALLBACK MenuMouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || 
            wParam == WM_NCLBUTTONDOWN || wParam == WM_NCRBUTTONDOWN ||
            wParam == WM_MBUTTONDOWN) {
            MSLLHOOKSTRUCT* p = (MSLLHOOKSTRUCT*)lParam;
            POINT pt = p->pt;
            RECT rM = {0}, rS = {0};
            if (g_hAcrylicMenu && IsWindow(g_hAcrylicMenu)) GetWindowRect(g_hAcrylicMenu, &rM);
            if (g_hAcrylicSubMenu && IsWindow(g_hAcrylicSubMenu)) GetWindowRect(g_hAcrylicSubMenu, &rS);
            bool inM = (g_hAcrylicMenu && IsWindow(g_hAcrylicMenu)) && PtInRect(&rM, pt);
            bool inS = (g_hAcrylicSubMenu && IsWindow(g_hAcrylicSubMenu)) && PtInRect(&rS, pt);
            if (!inM && !inS) {
                DismissAllMenus();
            }
        }
    }
    return CallNextHookEx(g_hMenuMouseHook, nCode, wParam, lParam);
}

// Submenu Window Procedure
static LRESULT CALLBACK AcrylicSubWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) {
                HWND hOther = (HWND)lParam;
                if (hOther != g_hAcrylicMenu && hOther != g_hAcrylicSubMenu) {
                    DismissAllMenus();
                }
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEWHEEL: {
            if (g_activeSubId == 2) { // Sleep Slider
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                int step = (delta > 0) ? 1 : -1;
                if (GetKeyState(VK_SHIFT) & 0x8000) step *= 5;
                g_sliderVal = std::clamp(g_sliderVal + step, 2, 120);
                Device::SetSleepTimeout(g_sliderVal);

                Device::State st = Device::GetCurrentState();
                WCHAR l1[64], l2[64], l3[64];
                StringCchPrintfW(l1, ARRAYSIZE(l1), L"休眠时间: %d 分钟", g_sliderVal);
                StringCchPrintfW(l2, ARRAYSIZE(l2), L"设置已即时生效并已保存");
                const WCHAR* modeStr = st.isWired ? L"USB" : L"2.4G";
                const WCHAR* batIcon = st.isCharging ? L"⚡" : L"🔋";
                StringCchPrintfW(l3, ARRAYSIZE(l3), L"%s  |  第 %d 档  |  %s %d%%  |  %d Hz",
                    modeStr, st.dpiLevel, batIcon, st.battery, st.pollingHz);
                Osd::Show(l1, l2, l3);

                InvalidateRect(hWnd, NULL, FALSE);
                if (g_hAcrylicMenu) InvalidateRect(g_hAcrylicMenu, NULL, FALSE);
                return 0;
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            RECT rc;
            GetClientRect(hWnd, &rc);

            if (g_activeSubId == 2) {
                // Sleep Slider Track interaction
                int trackX0 = S(20);
                int trackX1 = rc.right - S(20);
                int trackY = S(58);

                if (my >= trackY - S(14) && my <= trackY + S(14) && mx >= trackX0 - S(8) && mx <= trackX1 + S(8)) {
                    g_isDraggingSlider = true;
                    SetCapture(hWnd);
                    double ratio = (double)(mx - trackX0) / (double)(trackX1 - trackX0);
                    ratio = std::clamp(ratio, 0.0, 1.0);
                    g_sliderVal = Rapoo::ClampSleepMinutes(2 + (int)(ratio * (120 - 2) + 0.5));
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }

                // Preset Chips interaction (y = S(98), h = S(26))
                int chipY0 = S(96);
                int chipY1 = chipY0 + S(26);
                if (my >= chipY0 && my <= chipY1) {
                    int count = 5;
                    int chipW = (rc.right - S(40) - (count - 1) * S(6)) / count;
                    int curX = S(20);
                    for (int i = 0; i < count; i++) {
                        if (mx >= curX && mx <= curX + chipW) {
                            g_sliderVal = PRESET_SLEEP[i];
                            Device::SetSleepTimeout(g_sliderVal);

                            Device::State st = Device::GetCurrentState();
                            WCHAR l1[64], l2[64], l3[64];
                            StringCchPrintfW(l1, ARRAYSIZE(l1), L"休眠时间: %d 分钟", g_sliderVal);
                            StringCchPrintfW(l2, ARRAYSIZE(l2), L"设置已即时生效并已保存");
                            const WCHAR* modeStr = st.isWired ? L"USB" : L"2.4G";
                            const WCHAR* batIcon = st.isCharging ? L"⚡" : L"🔋";
                            StringCchPrintfW(l3, ARRAYSIZE(l3), L"%s  |  第 %d 档  |  %s %d%%  |  %d Hz",
                                modeStr, st.dpiLevel, batIcon, st.battery, st.pollingHz);
                            Osd::Show(l1, l2, l3);

                            InvalidateRect(hWnd, NULL, FALSE);
                            if (g_hAcrylicMenu) InvalidateRect(g_hAcrylicMenu, NULL, FALSE);
                            return 0;
                        }
                        curX += chipW + S(6);
                    }
                }
            }
            break;
        }

        case WM_MOUSEMOVE: {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            RECT rc;
            GetClientRect(hWnd, &rc);

            if (g_activeSubId == 2 && g_isDraggingSlider) {
                int trackX0 = S(20);
                int trackX1 = rc.right - S(20);
                double ratio = (double)(mx - trackX0) / (double)(trackX1 - trackX0);
                ratio = std::clamp(ratio, 0.0, 1.0);
                int newVal = Rapoo::ClampSleepMinutes(2 + (int)(ratio * (120 - 2) + 0.5));
                if (newVal != g_sliderVal) {
                    g_sliderVal = newVal;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;
            }

            if (g_activeSubId == 1) { // Polling Rate list
                int count = 7;
                int itemH = S(28);
                int y = S(8);
                int newH = -1;
                for (int i = 0; i < count; i++) {
                    if (my >= y && my < y + itemH) {
                        newH = i;
                        break;
                    }
                    y += itemH;
                }
                if (newH != g_subHover) {
                    g_subHover = newH;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_activeSubId == 2 && g_isDraggingSlider) {
                g_isDraggingSlider = false;
                ReleaseCapture();

                Device::SetSleepTimeout(g_sliderVal);

                Device::State st = Device::GetCurrentState();
                WCHAR l1[64], l2[64], l3[64];
                StringCchPrintfW(l1, ARRAYSIZE(l1), L"休眠时间: %d 分钟", g_sliderVal);
                StringCchPrintfW(l2, ARRAYSIZE(l2), L"设置已即时生效并已保存");
                const WCHAR* modeStr = st.isWired ? L"USB" : L"2.4G";
                const WCHAR* batIcon = st.isCharging ? L"⚡" : L"🔋";
                StringCchPrintfW(l3, ARRAYSIZE(l3), L"%s  |  第 %d 档  |  %s %d%%  |  %d Hz",
                    modeStr, st.dpiLevel, batIcon, st.battery, st.pollingHz);
                Osd::Show(l1, l2, l3);

                if (g_hAcrylicMenu) InvalidateRect(g_hAcrylicMenu, NULL, FALSE);
                return 0;
            }

            if (g_activeSubId == 1 && g_subHover >= 0) {
                const int hzVals[] = { 125, 250, 500, 1000, 2000, 4000, 8000 };
                int hz = hzVals[g_subHover];
                DismissAllMenus();

                Device::SetPollingRate(hz);

                Device::State st = Device::GetCurrentState();
                WCHAR l1[64], l2[64], l3[64];
                StringCchPrintfW(l1, ARRAYSIZE(l1), L"回报率: %d Hz", hz);
                StringCchPrintfW(l2, ARRAYSIZE(l2), L"设置已即时生效");
                const WCHAR* modeStr = st.isWired ? L"USB" : L"2.4G";
                const WCHAR* batIcon = st.isCharging ? L"⚡" : L"🔋";
                StringCchPrintfW(l3, ARRAYSIZE(l3), L"%s  |  第 %d 档  |  %s %d%%  |  %d Hz",
                    modeStr, st.dpiLevel, batIcon, st.battery, hz);
                Osd::Show(l1, l2, l3);
                return 0;
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP hbm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP oldBm = (HBITMAP)SelectObject(memDC, hbm);

            COLORREF bgCol = g_curDark ? RGB(24, 26, 32) : RGB(255, 255, 255);
            COLORREF borderCol = g_curDark ? RGB(50, 54, 65) : RGB(205, 212, 222);
            COLORREF hoverCol = g_curDark ? RGB(52, 58, 72) : RGB(232, 238, 248);
            COLORREF textCol = g_curDark ? RGB(235, 240, 248) : RGB(20, 24, 32);
            COLORREF checkCol = g_curDark ? RGB(96, 205, 255) : RGB(0, 110, 215);
            COLORREF mutedCol = g_curDark ? RGB(140, 150, 165) : RGB(105, 115, 130);
            COLORREF trackBgCol = g_curDark ? RGB(45, 50, 60) : RGB(215, 220, 230);

            HBRUSH bgBrush = CreateSolidBrush(bgCol);
            FillRect(memDC, &rc, bgBrush);
            DeleteObject(bgBrush);

            HPEN borderPen = CreatePen(PS_SOLID, 1, borderCol);
            HGDIOBJ oldPen = SelectObject(memDC, borderPen);
            HGDIOBJ oldBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
            RoundRect(memDC, 0, 0, rc.right, rc.bottom, S(12), S(12));
            SelectObject(memDC, oldPen);
            DeleteObject(borderPen);

            HFONT hFontNormal = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            HFONT hFontBold = CreateFontW(-S(13), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            HFONT hFontSmall = CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

            SetBkMode(memDC, TRANSPARENT);
            HGDIOBJ oldFont = SelectObject(memDC, hFontNormal);

            if (g_activeSubId == 1) { // Polling Rate list
                const WCHAR* labels[] = { L"125 Hz", L"250 Hz", L"500 Hz", L"1000 Hz", L"2000 Hz", L"4000 Hz", L"8000 Hz" };
                const int hzVals[] = { 125, 250, 500, 1000, 2000, 4000, 8000 };
                int currentHz = Device::GetCurrentState().pollingHz;

                int count = 7;
                int itemH = S(28);
                int y = S(8);
                for (int i = 0; i < count; i++) {
                    RECT rItem = { S(6), y, rc.right - S(6), y + itemH };
                    if (i == g_subHover) {
                        HBRUSH hH = CreateSolidBrush(hoverCol);
                        HPEN hP = CreatePen(PS_SOLID, 1, hoverCol);
                        HGDIOBJ oP = SelectObject(memDC, hP);
                        HGDIOBJ oB = SelectObject(memDC, hH);
                        RoundRect(memDC, rItem.left, rItem.top + 1, rItem.right, rItem.bottom - 1, S(6), S(6));
                        SelectObject(memDC, oP);
                        SelectObject(memDC, oB);
                        DeleteObject(hP);
                        DeleteObject(hH);
                    }

                    SetTextColor(memDC, textCol);
                    RECT rText = { S(14), y, rc.right - S(32), y + itemH };
                    DrawTextW(memDC, labels[i], -1, &rText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                    if (hzVals[i] == currentHz) {
                        SetTextColor(memDC, checkCol);
                        RECT rCheck = { rc.right - S(28), y, rc.right - S(10), y + itemH };
                        DrawTextW(memDC, L"✓", -1, &rCheck, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                    y += itemH;
                }
            } else if (g_activeSubId == 2) { // Modern 2~120 min Interactive Sleep Slider
                // Header Row: Label + Value
                SelectObject(memDC, hFontNormal);
                SetTextColor(memDC, textCol);
                RECT rHeader = { S(16), S(14), rc.right - S(80), S(38) };
                DrawTextW(memDC, L"休眠等待时间", -1, &rHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                SelectObject(memDC, hFontBold);
                SetTextColor(memDC, checkCol);
                WCHAR szVal[32];
                StringCchPrintfW(szVal, ARRAYSIZE(szVal), L"%d 分钟", g_sliderVal);
                RECT rVal = { rc.right - S(90), S(14), rc.right - S(16), S(38) };
                DrawTextW(memDC, szVal, -1, &rVal, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                // Slider Track
                int trackX0 = S(20);
                int trackX1 = rc.right - S(20);
                int trackY = S(58);
                int trackH = S(4);

                double ratio = (double)(g_sliderVal - 2) / (double)(120 - 2);
                ratio = std::clamp(ratio, 0.0, 1.0);
                int thumbX = trackX0 + (int)(ratio * (trackX1 - trackX0));

                // Draw background track (inactive portion)
                HBRUSH hTrackBg = CreateSolidBrush(trackBgCol);
                HGDIOBJ oP = SelectObject(memDC, GetStockObject(NULL_PEN));
                HGDIOBJ oB = SelectObject(memDC, hTrackBg);
                RoundRect(memDC, thumbX, trackY - trackH / 2, trackX1, trackY + trackH / 2 + 1, S(4), S(4));

                // Draw filled track (active portion)
                HBRUSH hTrackActive = CreateSolidBrush(checkCol);
                SelectObject(memDC, hTrackActive);
                RoundRect(memDC, trackX0, trackY - trackH / 2, thumbX, trackY + trackH / 2 + 1, S(4), S(4));

                // Draw thumb
                int thumbR = S(8);
                HBRUSH hThumbBg = CreateSolidBrush(g_curDark ? RGB(255, 255, 255) : RGB(255, 255, 255));
                HPEN hThumbBorder = CreatePen(PS_SOLID, 2, checkCol);
                SelectObject(memDC, hThumbBorder);
                SelectObject(memDC, hThumbBg);
                Ellipse(memDC, thumbX - thumbR, trackY - thumbR, thumbX + thumbR, trackY + thumbR);

                DeleteObject(hThumbBg);
                DeleteObject(hThumbBorder);
                DeleteObject(hTrackActive);
                DeleteObject(hTrackBg);

                // Preset Chips Row
                SelectObject(memDC, hFontSmall);
                int count = 5;
                int chipW = (rc.right - S(40) - (count - 1) * S(6)) / count;
                int curX = S(20);
                int chipY0 = S(96);
                int chipY1 = chipY0 + S(26);

                for (int i = 0; i < count; i++) {
                    RECT rChip = { curX, chipY0, curX + chipW, chipY1 };
                    bool isCur = (g_sliderVal == PRESET_SLEEP[i]);

                    HBRUSH hChipBg = CreateSolidBrush(isCur ? checkCol : (g_curDark ? RGB(36, 40, 50) : RGB(232, 236, 244)));
                    HPEN hChipPen = CreatePen(PS_SOLID, 1, isCur ? checkCol : borderCol);
                    SelectObject(memDC, hChipPen);
                    SelectObject(memDC, hChipBg);
                    RoundRect(memDC, rChip.left, rChip.top, rChip.right, rChip.bottom, S(6), S(6));
                    DeleteObject(hChipPen);
                    DeleteObject(hChipBg);

                    SetTextColor(memDC, isCur ? (g_curDark ? RGB(10, 12, 16) : RGB(255, 255, 255)) : textCol);
                    WCHAR szChip[16];
                    StringCchPrintfW(szChip, ARRAYSIZE(szChip), L"%d分", PRESET_SLEEP[i]);
                    DrawTextW(memDC, szChip, -1, &rChip, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                    curX += chipW + S(6);
                }
            }

            SelectObject(memDC, oldFont);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBrush);
            DeleteObject(hFontNormal);
            DeleteObject(hFontBold);
            DeleteObject(hFontSmall);

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBm);
            DeleteObject(hbm);
            DeleteDC(memDC);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void ShowSubMenuWindow(int subType, RECT rItemScreen) {
    if (g_activeSubId == subType && g_hAcrylicSubMenu) return;
    DismissSubMenu();
    g_activeSubId = subType;

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(g_hAcrylicMenu, GWLP_HINSTANCE);
    static bool s_subRegistered = false;
    if (!s_subRegistered) {
        WNDCLASSEXW wcSub = {0};
        wcSub.cbSize = sizeof(wcSub);
        wcSub.lpfnWndProc = AcrylicSubWndProc;
        wcSub.hInstance = hInst;
        wcSub.lpszClassName = L"RapooModernSubMenuWnd";
        wcSub.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wcSub);
        s_subRegistered = true;
    }

    int subW = (subType == 1) ? S(140) : S(260); // 260px wide for sleep slider
    int subH = (subType == 1) ? S(212) : S(138); // 138px height for sleep slider

    HMONITOR hMon = MonitorFromPoint({ rItemScreen.left, rItemScreen.top }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);

    int subX = rItemScreen.right + S(4);
    if (subX + subW > mi.rcWork.right) {
        subX = rItemScreen.left - subW - S(4);
    }
    int subY = rItemScreen.top - S(4);
    if (subY + subH > mi.rcWork.bottom) {
        subY = mi.rcWork.bottom - subH - S(4);
    }
    if (subY < mi.rcWork.top) subY = mi.rcWork.top;

    if (subType == 2) {
        g_sliderVal = Device::GetCurrentState().sleepMinutes;
    }

    g_hAcrylicSubMenu = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"RapooModernSubMenuWnd",
        L"",
        WS_POPUP,
        subX, subY, subW, subH,
        g_hAcrylicMenu, NULL, hInst, NULL
    );

    ApplyModernWindowStyle(g_hAcrylicSubMenu, g_curDark, subW, subH);
    ShowWindow(g_hAcrylicSubMenu, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hAcrylicSubMenu);
}

// --------------------------------------------------------------------------
// Main Acrylic Context Menu
// --------------------------------------------------------------------------

struct MenuItemData {
    int id;
    const WCHAR* label;
    WCHAR value[32];
    bool isHeader;
    bool isSeparator;
    bool isInteractive;
    bool isDisabled;
    int submenuType; // 0: None, 1: Polling, 2: Sleep
};

static std::vector<MenuItemData> g_mainItems;

static int GetItemH(int idx) {
    if (idx < 0 || idx >= (int)g_mainItems.size()) return 0;
    if (g_mainItems[idx].isHeader) return S(52);
    if (g_mainItems[idx].isSeparator) return S(9);
    return S(32);
}

static int GetItemY(int targetIdx) {
    int y = S(8);
    for (int i = 0; i < targetIdx; i++) {
        y += GetItemH(i);
    }
    return y;
}

static int HitTestMain(int my) {
    int y = S(8);
    for (size_t i = 0; i < g_mainItems.size(); i++) {
        int h = GetItemH((int)i);
        if (my >= y && my < y + h) {
            if (g_mainItems[i].isHeader || g_mainItems[i].isSeparator || g_mainItems[i].isDisabled) return -1;
            return (int)i;
        }
        y += h;
    }
    return -1;
}

static LRESULT CALLBACK AcrylicMainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) {
                HWND hOther = (HWND)lParam;
                if (hOther != g_hAcrylicMenu && hOther != g_hAcrylicSubMenu) {
                    DismissAllMenus();
                }
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE: {
            int my = HIWORD(lParam);
            int newH = HitTestMain(my);
            if (newH != g_mainHover) {
                g_mainHover = newH;
                InvalidateRect(hWnd, NULL, FALSE);

                if (newH >= 0 && g_mainItems[newH].submenuType > 0 && !g_mainItems[newH].isDisabled) {
                    RECT rcItem = { 0, GetItemY(newH), S(236), GetItemY(newH) + GetItemH(newH) };
                    POINT ptTopLeft = { rcItem.left, rcItem.top };
                    POINT ptBottomRight = { rcItem.right, rcItem.bottom };
                    ClientToScreen(hWnd, &ptTopLeft);
                    ClientToScreen(hWnd, &ptBottomRight);
                    RECT rScreen = { ptTopLeft.x, ptTopLeft.y, ptBottomRight.x, ptBottomRight.y };
                    ShowSubMenuWindow(g_mainItems[newH].submenuType, rScreen);
                } else if (newH >= 0 && g_mainItems[newH].submenuType == 0) {
                    DismissSubMenu();
                }
            }
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE: {
            POINT pt;
            GetCursorPos(&pt);
            if (g_hAcrylicSubMenu) {
                RECT rSub;
                GetWindowRect(g_hAcrylicSubMenu, &rSub);
                if (PtInRect(&rSub, pt)) return 0;
            }
            if (g_mainHover != -1) {
                g_mainHover = -1;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            int idx = HitTestMain(HIWORD(lParam));
            if (idx >= 0 && g_mainItems[idx].isInteractive && !g_mainItems[idx].isDisabled) {
                int cmd = g_mainItems[idx].id;
                DismissAllMenus();
                if (cmd == 1001) { // Toggle Auto Run
                    ToggleAutoRun();
                } else if (cmd == 1002) { // Exit
                    PostQuitMessage(0);
                }
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP hbm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP oldBm = (HBITMAP)SelectObject(memDC, hbm);

            COLORREF bgCol = g_curDark ? RGB(24, 26, 32) : RGB(255, 255, 255);
            COLORREF borderCol = g_curDark ? RGB(50, 54, 65) : RGB(205, 212, 222);
            COLORREF hoverCol = g_curDark ? RGB(52, 58, 72) : RGB(232, 238, 248);
            COLORREF textCol = g_curDark ? RGB(235, 240, 248) : RGB(20, 24, 32);
            COLORREF mutedCol = g_curDark ? RGB(155, 165, 180) : RGB(105, 115, 130);
            COLORREF greenCol = g_curDark ? RGB(34, 197, 94) : RGB(16, 145, 60);
            COLORREF sepCol = g_curDark ? RGB(40, 44, 54) : RGB(225, 228, 236);
            COLORREF checkCol = g_curDark ? RGB(96, 205, 255) : RGB(0, 110, 215);

            HBRUSH bgBrush = CreateSolidBrush(bgCol);
            FillRect(memDC, &rc, bgBrush);
            DeleteObject(bgBrush);

            HPEN borderPen = CreatePen(PS_SOLID, 1, borderCol);
            HGDIOBJ oldPen = SelectObject(memDC, borderPen);
            HGDIOBJ oldBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
            RoundRect(memDC, 0, 0, rc.right, rc.bottom, S(12), S(12));
            SelectObject(memDC, oldPen);
            DeleteObject(borderPen);

            HFONT hFontNormal = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            HFONT hFontBold = CreateFontW(-S(14), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            HFONT hFontSub = CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

            SetBkMode(memDC, TRANSPARENT);
            HGDIOBJ oldFont = SelectObject(memDC, hFontNormal);

            int y = S(8);
            for (size_t i = 0; i < g_mainItems.size(); i++) {
                int h = GetItemH((int)i);
                if (g_mainItems[i].isHeader) {
                    SelectObject(memDC, hFontBold);
                    SetTextColor(memDC, textCol);
                    RECT rTitle = { S(16), y + S(4), rc.right - S(16), y + S(26) };
                    DrawTextW(memDC, g_mainItems[i].label, -1, &rTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                    SelectObject(memDC, hFontSub);
                    Device::State st = Device::GetCurrentState();
                    SetTextColor(memDC, st.isConnected ? greenCol : mutedCol);
                    RECT rSub = { S(16), y + S(26), rc.right - S(16), y + S(46) };
                    DrawTextW(memDC, g_mainItems[i].value, -1, &rSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    y += h;
                } else if (g_mainItems[i].isSeparator) {
                    HPEN pSep = CreatePen(PS_SOLID, 1, sepCol);
                    HGDIOBJ oP = SelectObject(memDC, pSep);
                    MoveToEx(memDC, S(12), y + S(4), NULL);
                    LineTo(memDC, rc.right - S(12), y + S(4));
                    SelectObject(memDC, oP);
                    DeleteObject(pSep);
                    y += h;
                } else {
                    RECT rItem = { S(6), y, rc.right - S(6), y + h };
                    if ((int)i == g_mainHover && g_mainItems[i].isInteractive && !g_mainItems[i].isDisabled) {
                        HBRUSH hH = CreateSolidBrush(hoverCol);
                        HPEN hP = CreatePen(PS_SOLID, 1, hoverCol);
                        HGDIOBJ oP = SelectObject(memDC, hP);
                        HGDIOBJ oB = SelectObject(memDC, hH);
                        RoundRect(memDC, rItem.left, rItem.top + 1, rItem.right, rItem.bottom - 1, S(6), S(6));
                        SelectObject(memDC, oP);
                        SelectObject(memDC, oB);
                        DeleteObject(hP);
                        DeleteObject(hH);
                    }

                    SelectObject(memDC, hFontNormal);
                    SetTextColor(memDC, g_mainItems[i].isDisabled ? mutedCol : textCol);
                    RECT rLabel = { S(14), y, rc.right - S(90), y + h };
                    DrawTextW(memDC, g_mainItems[i].label, -1, &rLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                    if (g_mainItems[i].value[0]) {
                        bool isCheck = (wcscmp(g_mainItems[i].value, L"✓ 已开启") == 0);
                        SetTextColor(memDC, isCheck ? checkCol : mutedCol);
                        RECT rVal = { rc.right - S(115), y, rc.right - S(14), y + h };
                        DrawTextW(memDC, g_mainItems[i].value, -1, &rVal, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    }
                    y += h;
                }
            }

            SelectObject(memDC, oldFont);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBrush);
            DeleteObject(hFontNormal);
            DeleteObject(hFontBold);
            DeleteObject(hFontSub);

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBm);
            DeleteObject(hbm);
            DeleteDC(memDC);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void ShowMenu(HWND hWndOwner) {
    if (g_bModalLoop) return;
    Device::RefreshPollingRate();
    g_hParentAppWnd = hWndOwner;
    g_curDark = IsSystemDarkMode();

    POINT pt;
    GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);

    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef UINT (WINAPI *pfnGetDpiForWindow)(HWND);
        pfnGetDpiForWindow fnGetDpi = (pfnGetDpiForWindow)GetProcAddress(hUser, "GetDpiForWindow");
        if (fnGetDpi && hWndOwner) g_curDpi = fnGetDpi(hWndOwner);
    }
    if (g_curDpi == 0) g_curDpi = 96;

    Device::State st = Device::GetCurrentState();

    // Populate Menu Items
    g_mainItems.clear();

    MenuItemData itemHeader = { 0 };
    itemHeader.isHeader = true;
    itemHeader.label = st.modelName[0] ? st.modelName : L"通用";
    if (st.isConnected) {
        StringCchCopyW(itemHeader.value, 32, st.isWired ? L"● USB 有线连接 · 已连接" : L"● 2.4G 无线连接 · 已连接");
    } else {
        StringCchCopyW(itemHeader.value, 32, L"● 设备休眠 / 未连接");
    }
    g_mainItems.push_back(itemHeader);

    MenuItemData itemBat = { 0 };
    itemBat.label = L"电池电量";
    itemBat.isDisabled = !st.isConnected;
    if (st.isConnected) {
        if (st.isCharging) {
            StringCchPrintfW(itemBat.value, 32, L"%d%% 充电中", st.battery);
        } else {
            StringCchPrintfW(itemBat.value, 32, L"%d%%", st.battery);
        }
    } else {
        StringCchCopyW(itemBat.value, 32, L"--");
    }
    g_mainItems.push_back(itemBat);

    MenuItemData itemDpi = { 0 };
    itemDpi.label = L"DPI 档位";
    itemDpi.isDisabled = !st.isConnected;
    if (st.isConnected) {
        StringCchPrintfW(itemDpi.value, 32, L"%d (第 %d 档)", st.dpiX, st.dpiLevel);
    } else {
        StringCchCopyW(itemDpi.value, 32, L"--");
    }
    g_mainItems.push_back(itemDpi);

    MenuItemData itemPoll = { 0 };
    itemPoll.label = L"回报率设置";
    itemPoll.isInteractive = true;
    itemPoll.isDisabled = !st.isConnected;
    itemPoll.submenuType = 1;
    if (st.isConnected) {
        StringCchPrintfW(itemPoll.value, 32, L"%d Hz  ›", st.pollingHz);
    } else {
        StringCchCopyW(itemPoll.value, 32, L"--");
    }
    g_mainItems.push_back(itemPoll);

    MenuItemData itemSleep = { 0 };
    itemSleep.label = L"休眠时间设置";
    itemSleep.isInteractive = true;
    itemSleep.isDisabled = !st.isConnected;
    itemSleep.submenuType = 2; // Slider submenu
    if (st.isConnected) {
        StringCchPrintfW(itemSleep.value, 32, L"%d 分钟  ›", st.sleepMinutes);
    } else {
        StringCchCopyW(itemSleep.value, 32, L"--");
    }
    g_mainItems.push_back(itemSleep);

    MenuItemData sep1 = { 0 };
    sep1.isSeparator = true;
    g_mainItems.push_back(sep1);

    MenuItemData itemAuto = { 1001 };
    itemAuto.label = L"开机自启动";
    itemAuto.isInteractive = true;
    StringCchCopyW(itemAuto.value, 32, IsAutoRunEnabled() ? L"✓ 已开启" : L"未开启");
    g_mainItems.push_back(itemAuto);

    MenuItemData sep2 = { 0 };
    sep2.isSeparator = true;
    g_mainItems.push_back(sep2);

    MenuItemData itemExit = { 1002 };
    itemExit.label = L"退出程序";
    itemExit.isInteractive = true;
    g_mainItems.push_back(itemExit);

    int totalH = S(16);
    for (size_t i = 0; i < g_mainItems.size(); i++) {
        totalH += GetItemH((int)i);
    }
    int menuW = S(236);

    int posX = pt.x - S(10);
    int posY = pt.y - totalH - S(10);
    if (posX + menuW > mi.rcWork.right) posX = mi.rcWork.right - menuW - S(8);
    if (posX < mi.rcWork.left) posX = mi.rcWork.left + S(8);
    if (posY < mi.rcWork.top) posY = pt.y + S(10);
    if (posY + totalH > mi.rcWork.bottom) posY = mi.rcWork.bottom - totalH - S(8);

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hWndOwner, GWLP_HINSTANCE);
    static bool s_mainRegistered = false;
    if (!s_mainRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = AcrylicMainWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"RapooModernMainMenuWnd";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        s_mainRegistered = true;
    }

    g_mainHover = -1;
    g_activeSubId = 0;

    g_hAcrylicMenu = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"RapooModernMainMenuWnd",
        L"",
        WS_POPUP,
        posX, posY, menuW, totalH,
        hWndOwner, NULL, hInst, NULL
    );

    ApplyModernWindowStyle(g_hAcrylicMenu, g_curDark, menuW, totalH);
    SetForegroundWindow(g_hAcrylicMenu);
    ShowWindow(g_hAcrylicMenu, SW_SHOW);
    UpdateWindow(g_hAcrylicMenu);

    g_hMenuMouseHook = SetWindowsHookExW(WH_MOUSE_LL, MenuMouseHookProc, hInst, 0);

    g_bModalLoop = true;
    MSG msg;
    while (g_bModalLoop && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            DismissAllMenus();
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (msg.message == WM_QUIT) {
        PostQuitMessage((int)msg.wParam);
    }
}

// --------------------------------------------------------------------------
// Taskbar Battery Icon & Tooltip
// --------------------------------------------------------------------------

static const uint16_t FONT_5X9[10][9] = {
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x11, 0x11, 0x0E },
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x0E, 0x11, 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
    { 0x1E, 0x01, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x01, 0x1E },
    { 0x02, 0x06, 0x0A, 0x12, 0x12, 0x1F, 0x02, 0x02, 0x02 },
    { 0x1F, 0x10, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
    { 0x06, 0x08, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x0E },
    { 0x1F, 0x01, 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x08 },
    { 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x11, 0x0E },
    { 0x0E, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x02, 0x0C },
};

static const uint16_t FONT_4X9_0[9] = {
    0x06, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x06
};

static const uint16_t FONT_3X5[10][5] = {
    { 0x07, 0x05, 0x05, 0x05, 0x07 },
    { 0x02, 0x06, 0x02, 0x02, 0x07 },
    { 0x07, 0x01, 0x07, 0x04, 0x07 },
    { 0x07, 0x01, 0x07, 0x01, 0x07 },
    { 0x05, 0x05, 0x07, 0x01, 0x01 },
    { 0x07, 0x04, 0x07, 0x01, 0x07 },
    { 0x07, 0x04, 0x07, 0x05, 0x07 },
    { 0x07, 0x01, 0x02, 0x02, 0x02 },
    { 0x07, 0x05, 0x07, 0x05, 0x07 },
    { 0x07, 0x05, 0x07, 0x01, 0x07 },
};

HICON CreateBatteryIcon(int battery, bool isCharging, bool isConnected, int size, bool isDark) {
    if (size <= 0) size = 16;
    const int SS = 4;
    int W = size * SS;
    int H = size * SS;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvPixels = NULL;
    HBITMAP hbmColor = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvPixels, NULL, 0);
    if (!hbmColor || !pvPixels) {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    uint32_t* hi = (uint32_t*)pvPixels;
    std::memset(hi, 0, W * H * 4);

    auto HiPixel = [&](int x, int y, uint32_t c) {
        if (x >= 0 && x < W && y >= 0 && y < H) {
            hi[y * W + x] = c;
        }
    };

    auto FillRoundRect = [&](int x0, int y0, int x1, int y1, uint32_t col, int rad) {
        if (rad > (x1 - x0) / 2) rad = (x1 - x0) / 2;
        if (rad > (y1 - y0) / 2) rad = (y1 - y0) / 2;
        if (rad < 0) rad = 0;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                int dx = (x < x0 + rad) ? (x0 + rad - x) : (x > x1 - rad) ? (x - (x1 - rad)) : 0;
                int dy = (y < y0 + rad) ? (y0 + rad - y) : (y > y1 - rad) ? (y - (y1 - rad)) : 0;
                if (dx * dx + dy * dy <= rad * rad) HiPixel(x, y, col);
            }
        }
    };

    auto DrawDigitScaled = [&](const uint16_t* rows, int fw, int fh,
                               int x0, int y0, int scale, int bold_w, uint32_t c_digit) {
        for (int r = 0; r < fh; ++r) {
            for (int c = 0; c < fw; ++c) {
                if (!((rows[r] >> (fw - 1 - c)) & 1)) continue;
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale + bold_w; ++dx) {
                        int px = x0 + c * scale + dx;
                        int py = y0 + r * scale + dy;
                        HiPixel(px, py, c_digit);
                    }
                }
            }
        }
    };

    int style = GetBatteryStyle();

    // Style 0: 经典电池图标 (左右拉长横向电池 + 极耳 + 三色变色)
    if (style == 0) {
        uint32_t c_frame = isDark ? 0xFFFFFFFF : 0xFF1E1E1E;
        uint32_t c_fill = isCharging ? 0xFF22C55E : ((battery <= 30) ? 0xFFEF4444 : 0xFF3B82F6);
        uint32_t c_digit = 0xFF000000;

        // Horizontally elongated: enlarged battery icon and digits (做大两号)
        int tip_w = (size < 20) ? 1 : 2;
        int tip_h = (size < 20) ? 6 : (size * 4 / 10);
        int tip_y = (size - tip_h) / 2;

        int pad_y = (size < 20) ? 1 : ((size <= 24) ? 2 : (size * 10 / 100));

        int body_x0 = 0;
        int body_x1 = (size - 1 - tip_w) * SS + (SS - 1);
        int body_y0 = pad_y * SS;
        int body_y1 = (size - 1 - pad_y) * SS + (SS - 1);

        int frame_t = 1 * SS; // 1.0 screen pixel border
        int radius  = (size < 20) ? (SS + SS / 2) : (2 * SS);

        FillRoundRect(body_x0, body_y0, body_x1, body_y1, c_frame, radius);

        if (isConnected) {
            FillRoundRect(body_x0 + frame_t, body_y0 + frame_t,
                          body_x1 - frame_t, body_y1 - frame_t, c_fill, (radius > frame_t ? radius - frame_t : 0));
        } else {
            uint32_t c_dim = isDark ? 0x30606060 : 0x30B0B0B0;
            FillRoundRect(body_x0 + frame_t, body_y0 + frame_t,
                          body_x1 - frame_t, body_y1 - frame_t, c_dim, (radius > frame_t ? radius - frame_t : 0));
        }

        int tip_x0 = (size - tip_w) * SS;
        int tip_x1 = size * SS - 1;
        int tip_y0 = tip_y * SS;
        int tip_y1 = (tip_y + tip_h) * SS - 1;
        FillRoundRect(tip_x0, tip_y0, tip_x1, tip_y1, c_frame, SS / 2);

        int in_x0 = body_x0 + frame_t;
        int in_x1 = body_x1 - frame_t;
        int in_y0 = body_y0 + frame_t;
        int in_w  = in_x1 - in_x0 + 1;
        int in_h  = (body_y1 - frame_t) - in_y0 + 1;

        if (!isConnected) {
            uint32_t c_dash = isDark ? 0xFFBBBBBB : 0xFF666666;
            int dash_w = (size < 20) ? 4 * SS : 5 * SS;
            int dash_h = 2 * SS;
            int gap = 2 * SS;
            int total_w = 2 * dash_w + gap;
            int sx = in_x0 + (in_w - total_w) / 2;
            int sy = in_y0 + (in_h - dash_h) / 2;
            for (int y = 0; y < dash_h; ++y) {
                for (int x = 0; x < dash_w; ++x) {
                    HiPixel(sx + x, sy + y, c_dash);
                    HiPixel(sx + dash_w + gap + x, sy + y, c_dash);
                }
            }
        } else {
            char s[8];
            snprintf(s, sizeof(s), "%d", battery);
            int len = (int)strlen(s);

            bool useLargeFont = (in_h >= 9 * SS && in_w >= 12 * SS);
            const int fw = useLargeFont ? 5 : 3;
            const int fh = useLargeFont ? 9 : 5;
            int gap_fp = (len == 1) ? 0 : 1;

            int target_h = in_h * 85 / 100;
            int scale = target_h / fh;
            if (scale < 1) scale = 1;
            int bold_w = (scale >= 2 * SS) ? 1 : 0;

            auto CalcTextWidth = [&](int sc, int bw) -> int {
                if (battery == 100 && useLargeFont) {
                    return (2 * sc + bw) + (gap_fp * sc) + 2 * (4 * sc + bw) + (gap_fp * sc);
                } else if (battery == 100) {
                    return (sc + bw) + (gap_fp * sc) + 2 * (3 * sc + bw) + (gap_fp * sc);
                } else {
                    return len * (fw * sc + bw) + (len - 1) * (gap_fp * sc);
                }
            };

            while (scale > 1 && CalcTextWidth(scale, bold_w) > in_w) {
                --scale;
            }

            int text_h = fh * scale;
            int text_w = CalcTextWidth(scale, bold_w);
            int sx = in_x0 + (in_w - text_w) / 2;
            int sy = in_y0 + (in_h - text_h) / 2;

            if (battery == 100 && useLargeFont) {
                int cur = sx;
                for (int t = 0; t < 2 * scale + bold_w; ++t)
                    for (int r = 0; r < text_h; ++r)
                        HiPixel(cur + t, sy + r, c_digit);
                cur += 2 * scale + bold_w + gap_fp * scale;
                uint16_t rows9[9];
                for (int r = 0; r < 9; ++r) rows9[r] = FONT_4X9_0[r];
                DrawDigitScaled(rows9, 4, 9, cur, sy, scale, bold_w, c_digit);
                cur += 4 * scale + bold_w + gap_fp * scale;
                DrawDigitScaled(rows9, 4, 9, cur, sy, scale, bold_w, c_digit);
            } else if (battery == 100) {
                int cur = sx;
                for (int t = 0; t < scale + bold_w; ++t)
                    for (int r = 0; r < text_h; ++r)
                        HiPixel(cur + t, sy + r, c_digit);
                cur += scale + bold_w + gap_fp * scale;
                uint16_t rows5[5];
                for (int r = 0; r < 5; ++r) rows5[r] = FONT_3X5[0][r];
                DrawDigitScaled(rows5, 3, 5, cur, sy, scale, bold_w, c_digit);
                cur += 3 * scale + bold_w + gap_fp * scale;
                DrawDigitScaled(rows5, 3, 5, cur, sy, scale, bold_w, c_digit);
            } else {
                int cur = sx;
                for (int i = 0; i < len; ++i) {
                    int d = s[i] - '0';
                    if (useLargeFont) {
                        DrawDigitScaled(FONT_5X9[d], 5, 9, cur, sy, scale, bold_w, c_digit);
                    } else {
                        uint16_t rows5[5];
                        for (int r = 0; r < 5; ++r) rows5[r] = FONT_3X5[d][r];
                        DrawDigitScaled(rows5, 3, 5, cur, sy, scale, bold_w, c_digit);
                    }
                    cur += fw * scale + bold_w + gap_fp * scale;
                }
            }
        }
    }
    // Style 1: 配置二 (状态大圆点，不带数字)
    else if (style == 1) {
        uint32_t c_dot;
        if (!isConnected) {
            c_dot = isDark ? 0xFF6B7280 : 0xFF9CA3AF;
        } else if (isCharging) {
            c_dot = 0xFF22C55E; // Emerald Green
        } else if (battery <= 30) {
            c_dot = 0xFFEF4444; // Warning Red
        } else {
            c_dot = 0xFF3B82F6; // Tech Blue
        }

        int cx = W / 2;
        int cy = H / 2;
        // Big dot radius: ~34% of icon size, occupying around 68% diameter
        int dot_r = (size * 34 / 100) * SS;
        int r2 = dot_r * dot_r;

        for (int y = cy - dot_r - 1; y <= cy + dot_r + 1; ++y) {
            for (int x = cx - dot_r - 1; x <= cx + dot_r + 1; ++x) {
                int dx = x - cx;
                int dy = y - cy;
                if (dx * dx + dy * dy <= r2) {
                    HiPixel(x, y, c_dot);
                }
            }
        }
    }
    // Style 2: 配置三 (大号纯数字，输入法中英风格，矢量抗锯齿)
    else {
        COLORREF targetCol;
        if (!isConnected) {
            targetCol = isDark ? RGB(160, 160, 160) : RGB(100, 100, 100);
        } else if (isCharging) {
            targetCol = RGB(34, 197, 94); // Green
        } else if (battery <= 30) {
            targetCol = RGB(239, 68, 68); // Red
        } else {
            targetCol = isDark ? RGB(255, 255, 255) : RGB(20, 20, 20); // White on dark, black on light
        }

        WCHAR sWide[16] = {0};
        if (!isConnected) {
            StringCchCopyW(sWide, ARRAYSIZE(sWide), L"-");
        } else {
            StringCchPrintfW(sWide, ARRAYSIZE(sWide), L"%d", battery);
        }

        int fontH = (battery == 100) ? (-H * 55 / 100) : (-H * 74 / 100);
        HFONT hFont = CreateFontW(
            fontH, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, VARIABLE_PITCH, L"Segoe UI"
        );

        HGDIOBJ oldBm = SelectObject(hdcMem, hbmColor);
        HFONT oldFont = (HFONT)SelectObject(hdcMem, hFont);
        SetBkMode(hdcMem, TRANSPARENT);
        SetTextColor(hdcMem, RGB(255, 255, 255));

        RECT rcText = { 0, -SS, W, H - SS }; // optical baseline adjustment
        DrawTextW(hdcMem, sWide, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        GdiFlush();
        SelectObject(hdcMem, oldFont);
        DeleteObject(hFont);
        SelectObject(hdcMem, oldBm);

        uint8_t tR = GetRValue(targetCol);
        uint8_t tG = GetGValue(targetCol);
        uint8_t tB = GetBValue(targetCol);

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                uint32_t raw = hi[y * W + x];
                uint8_t gray = (raw & 0xFF); // R byte of white text
                if (gray > 0) {
                    hi[y * W + x] = ((uint32_t)gray << 24) | ((uint32_t)tR << 16) | ((uint32_t)tG << 8) | tB;
                } else {
                    hi[y * W + x] = 0;
                }
            }
        }
    }

    // Downsample SSxSS to size x size with smooth box filtering
    BITMAPINFO bomi = {0};
    bomi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bomi.bmiHeader.biWidth = size;
    bomi.bmiHeader.biHeight = -size;
    bomi.bmiHeader.biPlanes = 1;
    bomi.bmiHeader.biBitCount = 32;
    bomi.bmiHeader.biCompression = BI_RGB;

    void* pvOut = NULL;
    HBITMAP hbmOut = CreateDIBSection(hdcMem, &bomi, DIB_RGB_COLORS, &pvOut, NULL, 0);
    if (!hbmOut || !pvOut) {
        DeleteObject(hbmColor);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    uint32_t* out = (uint32_t*)pvOut;
    const int SS2 = SS * SS;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint32_t sumA = 0, sumR = 0, sumG = 0, sumB = 0;
            for (int sy = 0; sy < SS; ++sy) {
                const uint32_t* row = hi + (y * SS + sy) * W + x * SS;
                for (int sx = 0; sx < SS; ++sx) {
                    uint32_t c = row[sx];
                    uint32_t a = (c >> 24) & 0xFF;
                    if (a > 0) {
                        sumA += a;
                        sumR += (c >> 16) & 0xFF;
                        sumG += (c >>  8) & 0xFF;
                        sumB += (c      ) & 0xFF;
                    }
                }
            }
            uint8_t avgA = (uint8_t)(sumA / SS2);
            if (avgA == 0) {
                out[y * size + x] = 0;
            } else {
                uint8_t avgR = (uint8_t)(sumR / SS2);
                uint8_t avgG = (uint8_t)(sumG / SS2);
                uint8_t avgB = (uint8_t)(sumB / SS2);
                uint8_t prR = (uint8_t)((avgR * avgA) / 255);
                uint8_t prG = (uint8_t)((avgG * avgA) / 255);
                uint8_t prB = (uint8_t)((avgB * avgA) / 255);
                out[y * size + x] = ((uint32_t)avgA << 24) | ((uint32_t)prR << 16) | ((uint32_t)prG << 8) | (uint32_t)prB;
            }
        }
    }

    int maskPitch = ((size + 15) / 16) * 2;
    BYTE maskBits[256] = {0};
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint32_t px = out[y * size + x];
            uint8_t a = (px >> 24) & 0xFF;
            if (a < 32) {
                maskBits[y * maskPitch + (x / 8)] |= (1 << (7 - (x % 8)));
            }
        }
    }
    HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, maskBits);

    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmColor = hbmOut;
    ii.hbmMask = hbmMask;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    DeleteObject(hbmOut);
    DeleteObject(hbmMask);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return hIcon;
}

void UpdateTooltip(NOTIFYICONDATAW& nid, const Device::State& state) {
    if (state.isConnected) {
        const WCHAR* modeStr = state.isWired ? L" (USB)" : L" (2.4G)";
        if (state.isCharging) {
            StringCchPrintfW(
                nid.szTip,
                ARRAYSIZE(nid.szTip),
                L"%s%s\n电量: %d%% (充电中)\nDPI: %d (第 %d 档)\n回报率: %d Hz",
                state.modelName,
                modeStr,
                state.battery,
                state.dpiX,
                state.dpiLevel,
                state.pollingHz
            );
        } else {
            StringCchPrintfW(
                nid.szTip,
                ARRAYSIZE(nid.szTip),
                L"%s%s\n电量: %d%%\nDPI: %d (第 %d 档)\n回报率: %d Hz",
                state.modelName,
                modeStr,
                state.battery,
                state.dpiX,
                state.dpiLevel,
                state.pollingHz
            );
        }
    } else {
        StringCchPrintfW(
            nid.szTip,
            ARRAYSIZE(nid.szTip),
            L"%s\n设备休眠 / 未连接",
            state.modelName[0] ? state.modelName : L"通用"
        );
    }
}

} // namespace Tray
