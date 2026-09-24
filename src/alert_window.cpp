#include "alert_window.h"
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <strsafe.h>
#include <algorithm>

namespace Alert {

static HWND g_hAlertWnd = NULL;
static bool s_isShown = false;
static bool s_isCritical = false;
static int  s_battery = 0;
static bool s_isWired = false;
static WCHAR s_modelName[64] = {0};

static bool s_btnHover = false;
static bool s_closeHover = false;
static bool s_trackingMouse = false;

static bool s_alerted20 = false;
static bool s_alerted10 = false;

static inline int ScaleDpi(int val, int dpi) {
    return MulDiv(val, dpi, 96);
}

static UINT GetCurrentWindowDpi(HWND hWnd) {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef UINT (WINAPI *pfnGetDpiForWindow)(HWND);
        pfnGetDpiForWindow fnGetDpi = (pfnGetDpiForWindow)GetProcAddress(hUser, "GetDpiForWindow");
        if (fnGetDpi && hWnd) {
            UINT d = fnGetDpi(hWnd);
            if (d > 0) return d;
        }
    }
    return 96;
}

static bool IsSystemDark() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 1, size = sizeof(DWORD), type = 0;
        if (RegQueryValueExW(hKey, L"SystemUsesLightTheme", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (val == 0);
        }
        RegCloseKey(hKey);
    }
    return true;
}

static void CalculateAlertPosition(int targetW, int targetH, int dpi, int* outX, int* outY) {
    auto S = [dpi](int v) { return ScaleDpi(v, dpi); };

    // 1. Dynamically locate the Windows taskbar
    HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    APPBARDATA abd = { sizeof(APPBARDATA) };
    abd.hWnd = hTaskbar;
    BOOL hasAppBar = (hTaskbar && SHAppBarMessage(ABM_GETTASKBARPOS, &abd));

    // 2. Identify the monitor where the taskbar is located
    HMONITOR hMon = NULL;
    if (hTaskbar && IsWindow(hTaskbar)) {
        hMon = MonitorFromWindow(hTaskbar, MONITOR_DEFAULTTOPRIMARY);
    } else {
        POINT pt = { 0, 0 };
        GetCursorPos(&pt);
        hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    }

    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (!GetMonitorInfoW(hMon, &mi)) {
        RECT rcWork = { 0 };
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
        *outX = rcWork.right - targetW - S(10);
        *outY = rcWork.bottom - targetH - S(8);
        return;
    }

    RECT rcWork = mi.rcWork;
    RECT rcMon  = mi.rcMonitor;

    int marginSide = S(10); // Gap from screen right edge
    int marginTask = S(8);  // Snug gap hugging the taskbar edge

    if (hasAppBar) {
        switch (abd.uEdge) {
            case ABE_BOTTOM: {
                // Taskbar is at bottom: abd.rc.top is the dynamic top edge
                *outX = rcWork.right - targetW - marginSide;
                *outY = abd.rc.top - targetH - marginTask;
                break;
            }
            case ABE_TOP: {
                // Taskbar is at top: abd.rc.bottom is the dynamic bottom edge
                *outX = rcWork.right - targetW - marginSide;
                *outY = abd.rc.bottom + marginTask;
                break;
            }
            case ABE_LEFT: {
                // Taskbar is at left: abd.rc.right is the dynamic right edge
                *outX = abd.rc.right + marginTask;
                *outY = rcWork.bottom - targetH - marginSide;
                break;
            }
            case ABE_RIGHT: {
                // Taskbar is at right: abd.rc.left is the dynamic left edge
                *outX = abd.rc.left - targetW - marginTask;
                *outY = rcWork.bottom - targetH - marginSide;
                break;
            }
            default: {
                *outX = rcWork.right - targetW - marginSide;
                *outY = rcWork.bottom - targetH - marginTask;
                break;
            }
        }
    } else {
        *outX = rcWork.right - targetW - marginSide;
        *outY = rcWork.bottom - targetH - marginTask;
    }

    // Safety boundary clamping
    if (*outX + targetW > rcMon.right)  *outX = rcMon.right - targetW;
    if (*outX < rcMon.left)             *outX = rcMon.left;
    if (*outY + targetH > rcMon.bottom) *outY = rcMon.bottom - targetH;
    if (*outY < rcMon.top)              *outY = rcMon.top;
}

static void GetButtonRects(HWND hWnd, RECT* pClose, RECT* pConfirm) {
    UINT dpi = GetCurrentWindowDpi(hWnd);
    auto S = [dpi](int v) { return ScaleDpi(v, dpi); };

    RECT rc;
    GetClientRect(hWnd, &rc);

    if (pClose) {
        pClose->left   = rc.right - S(34);
        pClose->top    = S(10);
        pClose->right  = rc.right - S(10);
        pClose->bottom = S(34);
    }

    if (pConfirm) {
        pConfirm->left   = rc.right - S(108);
        pConfirm->top    = rc.bottom - S(38);
        pConfirm->right  = rc.right - S(16);
        pConfirm->bottom = rc.bottom - S(12);
    }
}

static LRESULT CALLBACK AlertWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_SETCURSOR: {
            RECT rClose, rConfirm;
            GetButtonRects(hWnd, &rClose, &rConfirm);
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            if (PtInRect(&rClose, pt) || PtInRect(&rConfirm, pt)) {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }

        case WM_MOUSEMOVE: {
            if (!s_trackingMouse) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
                TrackMouseEvent(&tme);
                s_trackingMouse = true;
            }

            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rClose, rConfirm;
            GetButtonRects(hWnd, &rClose, &rConfirm);

            bool newClose = PtInRect(&rClose, pt);
            bool newBtn   = PtInRect(&rConfirm, pt);

            if (newClose != s_closeHover || newBtn != s_btnHover) {
                s_closeHover = newClose;
                s_btnHover   = newBtn;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            s_trackingMouse = false;
            s_closeHover    = false;
            s_btnHover      = false;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONUP: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rClose, rConfirm;
            GetButtonRects(hWnd, &rClose, &rConfirm);

            if (PtInRect(&rClose, pt) || PtInRect(&rConfirm, pt)) {
                Hide();
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

            UINT dpi = GetCurrentWindowDpi(hWnd);
            auto S = [dpi](int v) { return ScaleDpi(v, dpi); };

            bool isDark = IsSystemDark();

            COLORREF bgCol = isDark ? RGB(24, 27, 36) : RGB(255, 255, 255);
            COLORREF borderCol = s_isCritical ? RGB(239, 68, 68) : RGB(245, 158, 11);
            COLORREF titleCol  = s_isCritical ? RGB(239, 68, 68) : RGB(245, 158, 11);
            COLORREF textMain  = isDark ? RGB(241, 245, 249) : RGB(15, 23, 42);
            COLORREF textSub   = isDark ? RGB(148, 163, 184) : RGB(100, 116, 139);

            // 1. Pre-fill background to ensure 100% clean solid surface
            HBRUSH hBg = CreateSolidBrush(bgCol);
            FillRect(memDC, &rc, hBg);
            DeleteObject(hBg);

            // 2. Draw 1px perimeter border without leaking background outside
            HPEN hBorder = CreatePen(PS_SOLID, 1, borderCol);
            HGDIOBJ oB = SelectObject(memDC, GetStockObject(NULL_BRUSH));
            HGDIOBJ oP = SelectObject(memDC, hBorder);
            RoundRect(memDC, 0, 0, rc.right, rc.bottom, S(16), S(16));
            SelectObject(memDC, oB);
            SelectObject(memDC, oP);
            DeleteObject(hBorder);

            SetBkMode(memDC, TRANSPARENT);

            // Fonts
            HFONT hFontTitle = CreateFontW(-S(15), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
            HFONT hFontBody = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
            HFONT hFontSmall = CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");

            // 1. Title Row: Icon + Title
            HGDIOBJ oF = SelectObject(memDC, hFontTitle);
            SetTextColor(memDC, titleCol);

            WCHAR szTitle[64];
            if (s_isCritical) {
                StringCchPrintfW(szTitle, ARRAYSIZE(szTitle), L"⚠️  严重低电量警告 (%d%%)", s_battery);
            } else {
                StringCchPrintfW(szTitle, ARRAYSIZE(szTitle), L"🪫  电量不足 (%d%%)", s_battery);
            }
            RECT rTitle = { S(18), S(12), rc.right - S(40), S(34) };
            DrawTextW(memDC, szTitle, -1, &rTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Close button [✕]
            RECT rClose, rConfirm;
            GetButtonRects(hWnd, &rClose, &rConfirm);

            if (s_closeHover) {
                HBRUSH hCloseBg = CreateSolidBrush(isDark ? RGB(50, 56, 70) : RGB(232, 238, 248));
                HPEN hNoPen = CreatePen(PS_NULL, 0, 0);
                SelectObject(memDC, hCloseBg);
                SelectObject(memDC, hNoPen);
                RoundRect(memDC, rClose.left, rClose.top, rClose.right, rClose.bottom, S(6), S(6));
                DeleteObject(hCloseBg);
                DeleteObject(hNoPen);
            }

            SelectObject(memDC, hFontBody);
            SetTextColor(memDC, s_closeHover ? (isDark ? RGB(255, 255, 255) : RGB(0, 0, 0)) : RGB(148, 163, 184));
            DrawTextW(memDC, L"✕", -1, &rClose, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // 2. Middle Info Rows
            SelectObject(memDC, hFontBody);
            SetTextColor(memDC, textMain);

            const WCHAR* mName = s_modelName[0] ? s_modelName : L"通用";
            const WCHAR* modeStr = s_isWired ? L"USB 有线模式" : L"2.4G 无线模式";
            WCHAR szDev[96];
            StringCchPrintfW(szDev, ARRAYSIZE(szDev), L"设备：%s  ·  %s", mName, modeStr);
            RECT rDev = { S(18), S(38), rc.right - S(18), S(58) };
            DrawTextW(memDC, szDev, -1, &rDev, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(memDC, hFontSmall);
            SetTextColor(memDC, s_isCritical ? (isDark ? RGB(252, 165, 165) : RGB(220, 38, 38)) : textSub);

            const WCHAR* szAdvice = s_isCritical
                ? L"鼠标即将耗尽电量自动关机，请立即连接充电器！"
                : L"剩余电量较低，建议适时连接 USB 线缆充电";
            RECT rAdvice = { S(18), S(58), rc.right - S(18), S(78) };
            DrawTextW(memDC, szAdvice, -1, &rAdvice, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // 3. Confirm Button [ 我知道了 ]
            HBRUSH hBtnBg;
            HPEN hBtnPen;
            COLORREF cBtnText;

            if (s_btnHover) {
                hBtnBg = CreateSolidBrush(borderCol);
                hBtnPen = CreatePen(PS_SOLID, 1, borderCol);
                cBtnText = RGB(255, 255, 255);
            } else {
                hBtnBg = CreateSolidBrush(isDark ? RGB(36, 41, 54) : RGB(241, 245, 249));
                hBtnPen = CreatePen(PS_SOLID, 1, isDark ? RGB(60, 68, 86) : RGB(203, 213, 225));
                cBtnText = isDark ? RGB(226, 232, 240) : RGB(30, 41, 59);
            }

            SelectObject(memDC, hBtnBg);
            SelectObject(memDC, hBtnPen);
            RoundRect(memDC, rConfirm.left, rConfirm.top, rConfirm.right, rConfirm.bottom, S(6), S(6));
            DeleteObject(hBtnBg);
            DeleteObject(hBtnPen);

            SelectObject(memDC, hFontBody);
            SetTextColor(memDC, cBtnText);
            DrawTextW(memDC, L"我知道了", -1, &rConfirm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Clean fonts
            SelectObject(memDC, oF);
            DeleteObject(hFontTitle);
            DeleteObject(hFontBody);
            DeleteObject(hFontSmall);

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            g_hAlertWnd = NULL;
            s_isShown = false;
            return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void Init(HINSTANCE hInstance) {
    WNDCLASSEXW wc = { 0 };
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = AlertWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"RapooAlertCardWnd";

    RegisterClassExW(&wc);

    g_hAlertWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"RapooAlertCardWnd",
        L"RapooBatteryAlert",
        WS_POPUP,
        0, 0, 10, 10,
        NULL, NULL, hInstance, NULL
    );

    if (g_hAlertWnd) {
        // Windows 11 hardware-antialiased rounded corners (DWMWA_WINDOW_CORNER_PREFERENCE = 33)
        DWORD corner = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(g_hAlertWnd, 33, &corner, sizeof(corner));

        BOOL isDark = IsSystemDark();
        DwmSetWindowAttribute(g_hAlertWnd, 20, &isDark, sizeof(isDark));
    }
}

void Cleanup() {
    if (g_hAlertWnd && IsWindow(g_hAlertWnd)) {
        DestroyWindow(g_hAlertWnd);
        g_hAlertWnd = NULL;
    }
    s_isShown = false;
}

void Show(const WCHAR* modelName, int battery, bool isCritical, bool isWired) {
    s_isCritical = isCritical;
    s_battery    = battery;
    s_isWired    = isWired;
    s_btnHover   = false;
    s_closeHover = false;
    if (modelName) {
        StringCchCopyW(s_modelName, ARRAYSIZE(s_modelName), modelName);
    } else {
        StringCchCopyW(s_modelName, ARRAYSIZE(s_modelName), L"通用");
    }

    if (!g_hAlertWnd) return;

    // Dynamically update DWM border color (Warning: Amber / Critical: Red)
    COLORREF borderCol = s_isCritical ? RGB(239, 68, 68) : RGB(245, 158, 11);
    DwmSetWindowAttribute(g_hAlertWnd, 34, &borderCol, sizeof(borderCol));

    BOOL isDark = IsSystemDark();
    DwmSetWindowAttribute(g_hAlertWnd, 20, &isDark, sizeof(isDark));

    UINT dpi = GetCurrentWindowDpi(g_hAlertWnd);
    auto S = [dpi](int v) { return ScaleDpi(v, dpi); };

    int targetW = S(310);
    int targetH = S(116);

    // Calculate dynamic position hugging the taskbar
    int x = 0, y = 0;
    CalculateAlertPosition(targetW, targetH, dpi, &x, &y);

    // NOTE: SetWindowRgn is removed!
    // SetWindowRgn disables DWM anti-aliased corner rounding and drop shadow,
    // causing 1-bit jagged staircase pixels (毛边).
    // DWM with DWMWCP_ROUND automatically provides GPU subpixel antialiasing and drop shadow.

    SetWindowPos(g_hAlertWnd, HWND_TOPMOST, x, y, targetW, targetH, SWP_NOACTIVATE);
    InvalidateRect(g_hAlertWnd, NULL, TRUE);
    UpdateWindow(g_hAlertWnd);
    ShowWindow(g_hAlertWnd, SW_SHOWNOACTIVATE);

    s_isShown = true;
}

void Hide() {
    if (g_hAlertWnd) {
        ShowWindow(g_hAlertWnd, SW_HIDE);
        s_isShown = false;
    }
}

void CheckBattery(const Device::State& state) {
    if (!state.isConnected) {
        return;
    }

    // Auto-dismiss if user starts charging
    if (state.isCharging) {
        if (s_isShown) {
            Hide();
        }
        // Reset alert triggers if charged back up
        if (state.battery >= 25) {
            s_alerted20 = false;
            s_alerted10 = false;
        }
        return;
    }

    // Reset when battery is restored above 25%
    if (state.battery >= 25) {
        s_alerted20 = false;
        s_alerted10 = false;
        return;
    }

    // Level 2: Critical alert (<= 10%)
    if (state.battery <= 10 && !s_alerted10) {
        s_alerted10 = true;
        s_alerted20 = true;
        Show(state.modelName, state.battery, true, state.isWired);
    }
    // Level 1: Warning alert (<= 20%)
    else if (state.battery <= 20 && !s_alerted20) {
        s_alerted20 = true;
        Show(state.modelName, state.battery, false, state.isWired);
    }
}

} // namespace Alert
