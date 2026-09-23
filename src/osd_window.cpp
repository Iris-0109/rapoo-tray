#include "osd_window.h"
#include <strsafe.h>
#include <algorithm>

namespace Osd {

static HWND g_hOsdWnd = NULL;
static WCHAR g_textLine1[64] = {0};
static WCHAR g_textLine2[64] = {0};
static WCHAR g_textLine3[64] = {0};
static BYTE g_osdAlpha = 0;

static const COLORREF OSD_KEY_COLOR = RGB(10, 12, 16);
static const UINT_PTR TIMER_OSD_HIDE = 101;
static const UINT_PTR TIMER_OSD_FADE = 102;

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

static LRESULT CALLBACK OsdWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

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

            // Fill with Key Color for transparency
            HBRUSH hKey = CreateSolidBrush(OSD_KEY_COLOR);
            FillRect(memDC, &rc, hKey);
            DeleteObject(hKey);

            RECT rcBox = rc;
            InflateRect(&rcBox, -S(2), -S(2));

            // Modern frosted card background & border
            HBRUSH hBg = CreateSolidBrush(RGB(22, 25, 34));
            HPEN hBorder = CreatePen(PS_SOLID, 1, RGB(70, 78, 96));
            HGDIOBJ oldBr = SelectObject(memDC, hBg);
            HGDIOBJ oldPen = SelectObject(memDC, hBorder);

            RoundRect(memDC, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, S(16), S(16));

            SetBkMode(memDC, TRANSPARENT);

            bool hasLine3 = (g_textLine3[0] != 0);

            // Font 1: Line 1 (Large bold title)
            HFONT hFontBig = CreateFontW(
                -S(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI"
            );
            HGDIOBJ oldFont = SelectObject(memDC, hFontBig);
            SetTextColor(memDC, RGB(255, 255, 255));

            RECT rcTop = rcBox;
            rcTop.top = rcBox.top + S(8);
            rcTop.bottom = rcTop.top + S(26);
            DrawTextW(memDC, g_textLine1, -1, &rcTop, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Font 2: Line 2 (Sub text)
            HFONT hFontMid = CreateFontW(
                -S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI"
            );
            SelectObject(memDC, hFontMid);
            SetTextColor(memDC, RGB(210, 222, 238));

            RECT rcMid = rcBox;
            rcMid.top = rcTop.bottom + S(2);
            rcMid.bottom = rcMid.top + S(22);
            DrawTextW(memDC, g_textLine2, -1, &rcMid, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Font 3: Line 3 (Status bar / Cyan highlight)
            HFONT hFontSub = NULL;
            if (hasLine3) {
                hFontSub = CreateFontW(
                    -S(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI"
                );
                SelectObject(memDC, hFontSub);
                SetTextColor(memDC, RGB(120, 210, 255));

                RECT rcBot = rcBox;
                rcBot.top = rcMid.bottom + S(2);
                rcBot.bottom = rcBot.top + S(22);
                DrawTextW(memDC, g_textLine3, -1, &rcBot, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldFont);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBr);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            DeleteObject(hFontBig);
            DeleteObject(hFontMid);
            if (hFontSub) DeleteObject(hFontSub);
            DeleteObject(hBorder);
            DeleteObject(hBg);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_TIMER: {
            if (wParam == TIMER_OSD_HIDE) {
                KillTimer(hWnd, TIMER_OSD_HIDE);
                SetTimer(hWnd, TIMER_OSD_FADE, 16, NULL);
            } else if (wParam == TIMER_OSD_FADE) {
                if (g_osdAlpha > 15) {
                    g_osdAlpha -= 15;
                    SetLayeredWindowAttributes(hWnd, OSD_KEY_COLOR, g_osdAlpha, LWA_COLORKEY | LWA_ALPHA);
                } else {
                    KillTimer(hWnd, TIMER_OSD_FADE);
                    ShowWindow(hWnd, SW_HIDE);
                    g_osdAlpha = 0;
                }
            }
            return 0;
        }

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

bool Init(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = OsdWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"RapooOsdPopupWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    g_hOsdWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName,
        L"RapooOSD",
        WS_POPUP,
        0, 0, 300, 96,
        NULL, NULL, hInstance, NULL
    );

    return (g_hOsdWnd != NULL);
}

void Cleanup() {
    if (g_hOsdWnd && IsWindow(g_hOsdWnd)) {
        DestroyWindow(g_hOsdWnd);
        g_hOsdWnd = NULL;
    }
}

HWND GetWindowHandle() {
    return g_hOsdWnd;
}

void Show(const WCHAR* line1, const WCHAR* line2, const WCHAR* line3) {
    if (!g_hOsdWnd) return;

    StringCchCopyW(g_textLine1, ARRAYSIZE(g_textLine1), line1 ? line1 : L"");
    StringCchCopyW(g_textLine2, ARRAYSIZE(g_textLine2), line2 ? line2 : L"");
    if (line3 && line3[0]) {
        StringCchCopyW(g_textLine3, ARRAYSIZE(g_textLine3), line3);
    } else {
        g_textLine3[0] = 0;
    }

    // Determine target monitor based on cursor position
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);
    RECT rcWork = mi.rcWork;

    UINT dpi = GetCurrentWindowDpi(g_hOsdWnd);
    auto S = [dpi](int v) { return ScaleDpi(v, dpi); };

    // Dynamic width & height calculation via DT_CALCRECT
    HDC screenDC = GetDC(NULL);
    HFONT hFontBig = CreateFontW(
        -S(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI"
    );
    HFONT hFontMid = CreateFontW(
        -S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI"
    );
    HFONT hFontSub = CreateFontW(
        -S(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI"
    );

    RECT r1 = {0, 0, 0, 0};
    RECT r2 = {0, 0, 0, 0};
    RECT r3 = {0, 0, 0, 0};

    HGDIOBJ oldF = SelectObject(screenDC, hFontBig);
    DrawTextW(screenDC, g_textLine1, -1, &r1, DT_CALCRECT | DT_SINGLELINE);

    SelectObject(screenDC, hFontMid);
    DrawTextW(screenDC, g_textLine2, -1, &r2, DT_CALCRECT | DT_SINGLELINE);

    int maxTextW = std::max(r1.right - r1.left, r2.right - r2.left);

    bool hasLine3 = (g_textLine3[0] != 0);
    if (hasLine3) {
        SelectObject(screenDC, hFontSub);
        DrawTextW(screenDC, g_textLine3, -1, &r3, DT_CALCRECT | DT_SINGLELINE);
        maxTextW = std::max(maxTextW, (int)(r3.right - r3.left));
    }

    SelectObject(screenDC, oldF);
    DeleteObject(hFontBig);
    DeleteObject(hFontMid);
    DeleteObject(hFontSub);
    ReleaseDC(NULL, screenDC);

    // Add safe horizontal padding (18px each side)
    int targetW = maxTextW + S(36);
    targetW = std::max(targetW, S(210));
    targetW = std::min(targetW, S(420));

    int targetH = hasLine3 ? S(88) : S(66);

    int x = rcWork.left + ((rcWork.right - rcWork.left) - targetW) / 2;
    int y = rcWork.bottom - targetH - S(70);

    KillTimer(g_hOsdWnd, TIMER_OSD_HIDE);
    KillTimer(g_hOsdWnd, TIMER_OSD_FADE);

    g_osdAlpha = 240;
    SetLayeredWindowAttributes(g_hOsdWnd, OSD_KEY_COLOR, g_osdAlpha, LWA_COLORKEY | LWA_ALPHA);

    SetWindowPos(g_hOsdWnd, HWND_TOPMOST, x, y, targetW, targetH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hOsdWnd, NULL, TRUE);

    SetTimer(g_hOsdWnd, TIMER_OSD_HIDE, 1600, NULL);
}

void ShowDpiUpdate(int level, int dpix, int dpiy, int battery, int pollingHz, bool isCharging, bool isWired, const WCHAR* modelName) {
    WCHAR l1[64], l2[64], l3[64];
    const WCHAR* mName = (modelName && modelName[0]) ? modelName : L"雷柏游戏鼠标";
    StringCchPrintfW(l1, ARRAYSIZE(l1), L"%s  DPI %d", mName, dpix);
    StringCchPrintfW(l2, ARRAYSIZE(l2), L"X 轴: %d    Y 轴: %d", dpix, dpiy);

    const WCHAR* modeStr = isWired ? L"USB" : L"2.4G";
    const WCHAR* batIcon = isCharging ? L"⚡" : L"🔋";
    StringCchPrintfW(l3, ARRAYSIZE(l3), L"%s  |  第 %d 档  |  %s %d%%  |  %d Hz",
        modeStr, level, batIcon, battery, pollingHz);

    Show(l1, l2, l3);
}

void ShowStyleOsd(int style) {
    const WCHAR* names[] = { L"样式一：经典电池", L"样式二：状态圆点", L"样式三：大号数字" };
    int idx = std::clamp(style, 0, 2);
    Show(L"电池图标样式", names[idx], L"双击托盘图标可循环切换");
}

} // namespace Osd
