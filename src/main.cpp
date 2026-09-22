#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include <strsafe.h>
#include <dbt.h>
#include <initguid.h>
#include <devguid.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

#define WM_TRAY_ICON         (WM_USER + 101)
#define WM_APP_DPI_UPDATE    (WM_USER + 102)
#define WM_APP_BAT_UPDATE    (WM_USER + 103)

#define IDM_HEADER          2001
#define IDM_BATTERY         2002
#define IDM_DPI             2003
#define IDM_POLL_INFO       2004
#define IDM_AUTORUN         2005
#define IDM_RECONNECT       2006
#define IDM_EXIT            2007

// Polling Rate Menu IDs
#define IDM_POLL_125        2101
#define IDM_POLL_250        2102
#define IDM_POLL_500        2103
#define IDM_POLL_1000       2104
#define IDM_POLL_2000       2105
#define IDM_POLL_4000       2106
#define IDM_POLL_8000       2107

// Sleep Timeout Menu IDs
#define IDM_SLEEP_2M        2301
#define IDM_SLEEP_5M        2302
#define IDM_SLEEP_10M       2303
#define IDM_SLEEP_30M       2304
#define IDM_SLEEP_60M       2305

#define TIMER_OSD_HIDE      3001
#define TIMER_OSD_FADE      3002

#define OSD_KEY_COLOR       RGB(255, 0, 255) // Magenta transparent key

// Global State
static HINSTANCE g_hInstance = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hOsdWnd = NULL;
static NOTIFYICONDATAW g_nid = {0};
static HANDLE g_hHidThread = NULL;
static HANDLE g_hStopEvent = NULL;

static CRITICAL_SECTION g_csDevIO;
static HANDLE g_hControlDev = INVALID_HANDLE_VALUE;
static HANDLE g_hFeatureDev = INVALID_HANDLE_VALUE;

static volatile LONG g_battery = 100;
static volatile LONG g_dpiLevel = 1;
static volatile LONG g_dpiX = 1200;
static volatile LONG g_dpiY = 1200;

static volatile LONG g_currentPollingHz = 1000;
static volatile LONG g_currentSleepMin = 10;

static WCHAR g_osdTextLine1[64] = L"第 1 档  DPI 1200";
static WCHAR g_osdTextLine2[64] = L"雷柏 VT7  |  电量 100%";
static BYTE g_osdAlpha = 0;

static UINT g_uTaskbarRestartMsg = 0;
static const WCHAR* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR* APP_NAME = L"rapoo-tray";
static WCHAR g_detectedModel[64] = L"雷柏无线鼠标";
static volatile bool g_deviceConnected = false;

static void UpdateTrayTooltip();
static void UpdateTrayIcon(int battery);
static void ShowCustomOsd(const WCHAR* line1, const WCHAR* line2);
static void ShowOsdNotification(int dpiLevel, int dpiX, int battery);
static bool IsAutoRunEnabled();
static void SetAutoRun(bool enable);

// 3x5 font for 16x16: 3 bits per row, 5 rows
static const uint8_t FONT_3X5[10][5] = {
    { 0x07, 0x05, 0x05, 0x05, 0x07 }, // '0'
    { 0x02, 0x06, 0x02, 0x02, 0x07 }, // '1'
    { 0x07, 0x01, 0x07, 0x04, 0x07 }, // '2'
    { 0x07, 0x01, 0x07, 0x01, 0x07 }, // '3'
    { 0x05, 0x05, 0x07, 0x01, 0x01 }, // '4'
    { 0x07, 0x04, 0x07, 0x01, 0x07 }, // '5'
    { 0x07, 0x04, 0x07, 0x05, 0x07 }, // '6'
    { 0x07, 0x01, 0x02, 0x02, 0x02 }, // '7'
    { 0x07, 0x05, 0x07, 0x05, 0x07 }, // '8'
    { 0x07, 0x05, 0x07, 0x01, 0x07 }  // '9'
};

// 5x9 font for 20x20 and 24x24: 5 bits per row, 9 rows
static const uint16_t FONT_5X9[10][9] = {
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // '0'
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, // '1'
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10, 0x1F }, // '2'
    { 0x1E, 0x01, 0x01, 0x06, 0x01, 0x01, 0x01, 0x01, 0x1E }, // '3'
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x02, 0x02 }, // '4'
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x01, 0x01, 0x11, 0x0E }, // '5'
    { 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x0E }, // '6'
    { 0x1F, 0x01, 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x08 }, // '7'
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x11, 0x11, 0x0E }, // '8'
    { 0x0E, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x01, 0x0E }  // '9'
};

// 4x9 font for '0' in '100' at 20x20 / 24x24
static const uint8_t FONT_4X9_0[9] = {
    0x06, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x06
};

static inline void PutPixelARGB(uint32_t* pPixels, int size, int x, int y, uint32_t color) {
    if (x >= 0 && x < size && y >= 0 && y < size) {
        pPixels[y * size + x] = color;
    }
}

static int GetTrayIconSize(HWND hWnd) {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    typedef UINT (WINAPI *pfnGetDpiForWindow)(HWND);
    typedef int (WINAPI *pfnGetSystemMetricsForDpi)(int, UINT);
    pfnGetDpiForWindow fnGetDpiForWindow = (pfnGetDpiForWindow)GetProcAddress(hUser, "GetDpiForWindow");
    pfnGetSystemMetricsForDpi fnGetSystemMetricsForDpi = (pfnGetSystemMetricsForDpi)GetProcAddress(hUser, "GetSystemMetricsForDpi");

    int sz = 0;
    if (fnGetDpiForWindow && fnGetSystemMetricsForDpi && hWnd) {
        UINT dpi = fnGetDpiForWindow(hWnd);
        if (dpi > 0) {
            sz = fnGetSystemMetricsForDpi(SM_CXSMICON, dpi);
        }
    }
    if (sz <= 0) {
        sz = GetSystemMetrics(SM_CXSMICON);
    }
    if (sz <= 0) sz = 16;
    return sz;
}

static bool IsSystemDarkTheme() {
    DWORD val = 0, size = sizeof(val);
    if (RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, NULL, &val, &size) == ERROR_SUCCESS) {
        return (val == 0);
    }
    return true; // Default to dark theme
}

// Generate Win11-style slim battery icon with 4x supersampling anti-aliasing.
// When connected: vibrant solid emerald green with exact percentage digits.
// When disconnected/asleep: elegant dimmed outline with centered dashed line "--".
static HICON CreateBatteryIcon(int battery) {
    int size = GetTrayIconSize(g_hMainWnd);
    if (size <= 0) size = 16;
    const int SS = 4;
    const int W = size * SS;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -W; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = NULL;
    HBITMAP hbmColor = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    if (!hbmColor || !pvBits) {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    uint32_t* hi = (uint32_t*)pvBits;
    memset(hi, 0, W * W * sizeof(uint32_t));

    bool isDark = IsSystemDarkTheme();

    const uint32_t c_fill  = 0xFF2DD773; // Emerald green
    const uint32_t c_frame = isDark ? 0xFFFFFFFF : 0xFF1E1E1E;
    const uint32_t c_digit = 0xFF000000;

    auto HiPixel = [&](int x, int y, uint32_t col) {
        if (x >= 0 && x < W && y >= 0 && y < W) hi[y * W + x] = col;
    };
    auto FillRoundRect = [&](int x0, int y0, int x1, int y1, uint32_t col, int rad) {
        if (rad > (x1 - x0) / 2) rad = (x1 - x0) / 2;
        if (rad > (y1 - y0) / 2) rad = (y1 - y0) / 2;
        if (rad < 0) rad = 0;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                int dx = (x < x0 + rad) ? (x0 + rad - x) : (x > x1 - rad) ? (x - (x1 - rad)) : 0;
                int dy = (y < y0 + rad) ? (y0 + rad - y) : (y > y1 - rad) ? (y - (y1 - rad)) : 0;
                if (dx * dx + dy * dy <= rad * rad) HiPixel(x, y, col);
            }
    };

    int pad_y = (size < 20) ? 1 : ((size <= 24) ? 2 : 3);
    int tip_w = (size < 20) ? 2 : ((size <= 24) ? 3 : 4);
    int tip_h = (size < 20) ? 6 : (size / 3);
    int tip_y = (size - tip_h) / 2;

    int body_x0 = 0;
    int body_x1 = (size - 1 - tip_w) * SS + (SS - 1);
    int body_y0 = pad_y * SS;
    int body_y1 = (size - 1 - pad_y) * SS + (SS - 1);

    int frame_t = 1 * SS; // 1.0 screen pixel border
    int radius  = 2 * SS;

    FillRoundRect(body_x0, body_y0, body_x1, body_y1, c_frame, radius);

    if (g_deviceConnected) {
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
    FillRoundRect(tip_x0, tip_y0, tip_x1, tip_y1, c_frame, SS);

    int in_x0 = body_x0 + frame_t;
    int in_x1 = body_x1 - frame_t;
    int in_y0 = body_y0 + frame_t;
    int in_w  = in_x1 - in_x0 + 1;
    int in_h  = (body_y1 - frame_t) - in_y0 + 1;

    if (!g_deviceConnected) {
        // Draw centered dashed line "--" for offline / sleep state
        uint32_t c_dash = isDark ? 0xFFBBBBBB : 0xFF666666;
        int dash_w = (size < 20) ? 3 * SS : 4 * SS;
        int dash_h = (size < 20) ? 1 * SS : 2 * SS;
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

        const int fw = (size < 20) ? 3 : 5;
        const int fh = (size < 20) ? 5 : 9;
        int gap_fp = (len == 1) ? 0 : 1;

        int target_h = in_h * 70 / 100;
        int scale = target_h / fh;
        if (scale < 1) scale = 1;
        int bold_w = 1;

        auto CalcTextWidth = [&](int sc, int bw) -> int {
            if (battery == 100 && size < 20) {
                return (sc + bw) + (gap_fp * sc) + 2 * (3 * sc + bw) + (gap_fp * sc);
            } else if (battery == 100) {
                return (2 * sc + bw) + (gap_fp * sc) + 2 * (4 * sc + bw) + (gap_fp * sc);
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

        auto DrawDigitScaled = [&](const uint16_t* rows, int fw, int fh,
                                   int x0, int y0, int scale) {
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

        if (battery == 100 && size < 20) {
            int cur = sx;
            for (int t = 0; t < scale + bold_w; ++t)
                for (int r = 0; r < text_h; ++r)
                    HiPixel(cur + t, sy + r, c_digit);
            cur += scale + bold_w + gap_fp * scale;
            uint16_t rows5[5];
            for (int r = 0; r < 5; ++r) rows5[r] = FONT_3X5[0][r];
            DrawDigitScaled(rows5, 3, 5, cur, sy, scale);
            cur += 3 * scale + bold_w + gap_fp * scale;
            DrawDigitScaled(rows5, 3, 5, cur, sy, scale);
        } else if (battery == 100) {
            int cur = sx;
            for (int t = 0; t < 2 * scale + bold_w; ++t)
                for (int r = 0; r < text_h; ++r)
                    HiPixel(cur + t, sy + r, c_digit);
            cur += 2 * scale + bold_w + gap_fp * scale;
            uint16_t rows9[9];
            for (int r = 0; r < 9; ++r) rows9[r] = FONT_4X9_0[r];
            DrawDigitScaled(rows9, 4, 9, cur, sy, scale);
            cur += 4 * scale + bold_w + gap_fp * scale;
            DrawDigitScaled(rows9, 4, 9, cur, sy, scale);
        } else {
            int cur = sx;
            for (int i = 0; i < len; ++i) {
                int d = s[i] - '0';
                if (size < 20) {
                    uint16_t rows5[5];
                    for (int r = 0; r < 5; ++r) rows5[r] = FONT_3X5[d][r];
                    DrawDigitScaled(rows5, 3, 5, cur, sy, scale);
                } else {
                    DrawDigitScaled(FONT_5X9[d], 5, 9, cur, sy, scale);
                }
                cur += fw * scale + bold_w + gap_fp * scale;
            }
        }
    }

    BITMAPINFO bomi = {0};
    bomi.bmiHeader = bmi.bmiHeader;
    bomi.bmiHeader.biWidth = size;
    bomi.bmiHeader.biHeight = -size;
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

static void UpdateTrayTooltip() {
    if (g_deviceConnected) {
        StringCchPrintfW(
            g_nid.szTip,
            ARRAYSIZE(g_nid.szTip),
            L"%s\n电量: %d%%\nDPI: %d (第 %d 档)\n回报率: %d Hz",
            g_detectedModel,
            g_battery,
            g_dpiX,
            g_dpiLevel,
            g_currentPollingHz
        );
    } else {
        StringCchPrintfW(
            g_nid.szTip,
            ARRAYSIZE(g_nid.szTip),
            L"%s\n(鼠标已休眠或未连接)",
            g_detectedModel
        );
    }
}

static void UpdateTrayIcon(int battery) {
    HICON hNewIcon = CreateBatteryIcon(battery);
    if (hNewIcon) {
        if (g_nid.hIcon) {
            DestroyIcon(g_nid.hIcon);
        }
        g_nid.hIcon = hNewIcon;
    }
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    UpdateTrayTooltip();
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// OSD Window Procedure
// OSD Floating Window Procedure with Double-Buffering and Transparency
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
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            // Fill transparent color key
            HBRUSH hBrKey = CreateSolidBrush(OSD_KEY_COLOR);
            FillRect(memDC, &rc, hBrKey);
            DeleteObject(hBrKey);

            // Modern Dark Floating Pill
            RECT rcBox = rc;
            InflateRect(&rcBox, -2, -2);
            HBRUSH hBg = CreateSolidBrush(RGB(24, 26, 32));
            HPEN hBorder = CreatePen(PS_SOLID, 1, RGB(70, 75, 90));
            HGDIOBJ oldBr = SelectObject(memDC, hBg);
            HGDIOBJ oldPen = SelectObject(memDC, hBorder);
            RoundRect(memDC, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 22, 22);

            SetBkMode(memDC, TRANSPARENT);

            // Level + DPI Value (Prominent Bold Font)
            HFONT hFontBig = CreateFontW(
                -24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI"
            );
            HGDIOBJ oldFont = SelectObject(memDC, hFontBig);
            SetTextColor(memDC, RGB(255, 255, 255));
            RECT rcTop = rcBox;
            rcTop.bottom = rcBox.top + 42;
            DrawTextW(memDC, g_osdTextLine1, -1, &rcTop, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Subtitle (Model + Status in Cyan)
            HFONT hFontSub = CreateFontW(
                -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI"
            );
            SelectObject(memDC, hFontSub);
            SetTextColor(memDC, RGB(130, 215, 255));
            RECT rcBot = rcBox;
            rcBot.top = rcBox.top + 40;
            DrawTextW(memDC, g_osdTextLine2, -1, &rcBot, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldFont);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBr);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            DeleteObject(hFontBig);
            DeleteObject(hFontSub);
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

static void ShowCustomOsd(const WCHAR* line1, const WCHAR* line2) {
    if (!g_hOsdWnd) return;

    StringCchCopyW(g_osdTextLine1, ARRAYSIZE(g_osdTextLine1), line1);
    StringCchCopyW(g_osdTextLine2, ARRAYSIZE(g_osdTextLine2), line2);

    RECT rcWork;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0)) {
        rcWork.left = 0;
        rcWork.top = 0;
        rcWork.right = GetSystemMetrics(SM_CXSCREEN);
        rcWork.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int w = 260;
    int h = 76;
    int x = rcWork.left + ((rcWork.right - rcWork.left) - w) / 2;
    int y = rcWork.bottom - h - 80;

    SetWindowPos(g_hOsdWnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_osdAlpha = 240;
    SetLayeredWindowAttributes(g_hOsdWnd, OSD_KEY_COLOR, g_osdAlpha, LWA_COLORKEY | LWA_ALPHA);
    InvalidateRect(g_hOsdWnd, NULL, FALSE);
    UpdateWindow(g_hOsdWnd);

    KillTimer(g_hOsdWnd, TIMER_OSD_FADE);
    SetTimer(g_hOsdWnd, TIMER_OSD_HIDE, 1400, NULL);
}

static void ShowOsdNotification(int dpiLevel, int dpiX, int battery) {
    WCHAR l1[64], l2[64];
    StringCchPrintfW(l1, ARRAYSIZE(l1), L"第 %d 档  DPI %d", dpiLevel, dpiX);
    StringCchPrintfW(l2, ARRAYSIZE(l2), L"%s  |  电量 %d%%", g_detectedModel, battery);
    ShowCustomOsd(l1, l2);
}

// --------------------------------------------------------------------------
// Rapoo Hardware Protocol Implementation (A5A5 Write, A5A4 Read)
// --------------------------------------------------------------------------

// Sends 33-byte output report (Report ID = 6) to Usage 14 endpoint
static bool SendRapooCommand(BYTE bank, BYTE addr, const BYTE* pData, int dataLen) {
    EnterCriticalSection(&g_csDevIO);
    if (g_hControlDev == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }

    BYTE buf[33] = {0};
    buf[0] = 0x06; // Output Report ID = 6
    buf[1] = 0xA5;
    buf[2] = 0xA5;
    buf[3] = (BYTE)(dataLen & 0xFF);
    buf[4] = addr;
    buf[5] = bank;
    buf[6] = 0x00;
    buf[7] = 0x00;
    if (pData && dataLen > 0) {
        memcpy(&buf[8], pData, (dataLen > 25) ? 25 : dataLen);
    }

    DWORD written = 0;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    BOOL ok = WriteFile(g_hControlDev, buf, 33, &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 1000) == WAIT_OBJECT_0) {
            GetOverlappedResult(g_hControlDev, &ov, &written, FALSE);
            ok = (written == 33);
        } else {
            CancelIo(g_hControlDev);
            ok = FALSE;
        }
    }
    CloseHandle(ov.hEvent);
    LeaveCriticalSection(&g_csDevIO);
    return (ok && written == 33);
}

// Active Ping to check if mouse is awake and get current settings
static bool PingRapooDevice() {
    EnterCriticalSection(&g_csDevIO);
    if (g_hControlDev == INVALID_HANDLE_VALUE || g_hFeatureDev == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }

    // Query Polling Rate register: A5 A4 01 80 08 00 00 00
    BYTE outBuf[33] = {0};
    outBuf[0] = 0x06; // Report ID
    outBuf[1] = 0xA5;
    outBuf[2] = 0xA4;
    outBuf[3] = 0x01; // Len = 1
    outBuf[4] = 0x80; // Addr = 0x80 (pollingHz)
    outBuf[5] = 0x08; // Bank = 0x08 (SYSTEM)

    DWORD written = 0;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    BOOL wOk = WriteFile(g_hControlDev, outBuf, 33, &written, &ov);
    if (!wOk && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 300) == WAIT_OBJECT_0) {
            GetOverlappedResult(g_hControlDev, &ov, &written, FALSE);
            wOk = (written == 33);
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

    Sleep(25);

    // Read response via Feature Report ID = 8
    BYTE featBuf[33] = {0};
    featBuf[0] = 0x08;
    BOOL fOk = HidD_GetFeature(g_hFeatureDev, featBuf, 33);

    // If fOk failed or featBuf[0] != 0x01 (ACK), mouse is offline (turned off or deep sleeping)
    if (!fOk || featBuf[0] != 0x01) {
        LeaveCriticalSection(&g_csDevIO);
        return false;
    }

    // When featBuf[0] == 0x01, mouse is online and awake
    // The register value is at featBuf[4]
    BYTE pollCode = featBuf[4];
    int hz = 1000;
    switch (pollCode) {
        case 0x08: hz = 125; break;
        case 0x04: hz = 250; break;
        case 0x02: hz = 500; break;
        case 0x01: hz = 1000; break;
        case 0x84: hz = 2000; break;
        case 0x82: hz = 4000; break;
        case 0x81: hz = 8000; break;
        default:   hz = 1000; break;
    }
    InterlockedExchange(&g_currentPollingHz, hz);

    LeaveCriticalSection(&g_csDevIO);
    return true;
}

static void SetPollingRate(int hz) {
    BYTE code = 0x01;
    switch (hz) {
        case 125:  code = 0x08; break;
        case 250:  code = 0x04; break;
        case 500:  code = 0x02; break;
        case 1000: code = 0x01; break;
        case 2000: code = 0x84; break;
        case 4000: code = 0x82; break;
        case 8000: code = 0x81; break;
        default:   code = 0x01; hz = 1000; break;
    }

    if (SendRapooCommand(0x08, 0x80, &code, 1)) {
        InterlockedExchange(&g_currentPollingHz, hz);
        UpdateTrayTooltip();
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);

        WCHAR l1[64], l2[64];
        StringCchPrintfW(l1, ARRAYSIZE(l1), L"回报率: %d Hz", hz);
        StringCchPrintfW(l2, ARRAYSIZE(l2), L"%s  |  设置已生效", g_detectedModel);
        ShowCustomOsd(l1, l2);
    }
}

static void SetSleepTimeout(int minutes) {
    if (minutes < 2) minutes = 2;
    if (minutes > 120) minutes = 120;
    BYTE code = (BYTE)minutes;

    if (SendRapooCommand(0x08, 0xC2, &code, 1)) {
        InterlockedExchange(&g_currentSleepMin, minutes);

        WCHAR l1[64], l2[64];
        StringCchPrintfW(l1, ARRAYSIZE(l1), L"休眠时间: %d 分钟", minutes);
        StringCchPrintfW(l2, ARRAYSIZE(l2), L"%s  |  设置已生效", g_detectedModel);
        ShowCustomOsd(l1, l2);
    }
}

// --------------------------------------------------------------------------
// Multi-Endpoint Device Enumeration via HID Caps
// --------------------------------------------------------------------------

static bool FindRapooEndpoints(WCHAR* pathStatus, WCHAR* pathControl, WCHAR* pathFeature, WCHAR* outModel, DWORD maxModelLen) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA devData = {0};
    devData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    pathStatus[0] = 0;
    pathControl[0] = 0;
    pathFeature[0] = 0;

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

                if (hProbe != INVALID_HANDLE_VALUE) {
                    PHIDP_PREPARSED_DATA pData = NULL;
                    if (HidD_GetPreparsedData(hProbe, &pData)) {
                        HIDP_CAPS caps;
                        if (HidP_GetCaps(pData, &caps) == HIDP_STATUS_SUCCESS) {
                            if (caps.UsagePage == 0xFF00) {
                                if (caps.Usage == 0x0002 || (caps.InputReportByteLength == 19 && wcsstr(lowerPath, L"col09"))) {
                                    StringCchCopyW(pathStatus, MAX_PATH, pDetail->DevicePath);
                                    found = true;
                                } else if (caps.Usage == 0x000E && caps.OutputReportByteLength == 33) {
                                    StringCchCopyW(pathControl, MAX_PATH, pDetail->DevicePath);
                                } else if (caps.Usage == 0x000F && caps.FeatureReportByteLength == 33) {
                                    StringCchCopyW(pathFeature, MAX_PATH, pDetail->DevicePath);
                                }
                            }
                        }
                        HidD_FreePreparsedData(pData);
                    }
                    CloseHandle(hProbe);
                }

                // Detect hardware model name from PID
                if (outModel && maxModelLen > 0) {
                    if (wcsstr(lowerPath, L"pid_1460")) {
                        StringCchCopyW(outModel, maxModelLen, L"雷柏 VT7");
                    } else if (wcsstr(lowerPath, L"pid_4660")) {
                        StringCchCopyW(outModel, maxModelLen, L"雷柏 VT7 (有线)");
                    } else if (wcsstr(lowerPath, L"pid_1406") || wcsstr(lowerPath, L"pid_1410")) {
                        StringCchCopyW(outModel, maxModelLen, L"雷柏 VT3S");
                    } else if (wcsstr(lowerPath, L"pid_4606") || wcsstr(lowerPath, L"pid_1411")) {
                        StringCchCopyW(outModel, maxModelLen, L"雷柏 VT3S (有线)");
                    } else if (wcsstr(lowerPath, L"pid_1412") || wcsstr(lowerPath, L"pid_1413") || wcsstr(lowerPath, L"pid_1440")) {
                        StringCchCopyW(outModel, maxModelLen, L"雷柏 VT9 系列");
                    } else {
                        StringCchCopyW(outModel, maxModelLen, L"雷柏无线鼠标");
                    }
                }
            }
        }
        free(pDetail);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return found;
}

// Background worker thread for low-overhead HID monitoring & active heartbeats
static DWORD WINAPI HidWorkerThread(LPVOID lpParam) {
    WCHAR pathStatus[MAX_PATH] = {0};
    WCHAR pathControl[MAX_PATH] = {0};
    WCHAR pathFeature[MAX_PATH] = {0};
    WCHAR modelBuf[64] = {0};

    while (WaitForSingleObject(g_hStopEvent, 200) == WAIT_TIMEOUT) {
        if (!FindRapooEndpoints(pathStatus, pathControl, pathFeature, modelBuf, 64)) {
            if (g_deviceConnected) {
                g_deviceConnected = false;
                PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, 0, 0);
            }
            Sleep(1500);
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
            if (g_deviceConnected) {
                g_deviceConnected = false;
                PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, 0, 0);
            }
            Sleep(1500);
            continue;
        }

        // Open bidirectional control and feature endpoints
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
        }
        LeaveCriticalSection(&g_csDevIO);

        StringCchCopyW(g_detectedModel, ARRAYSIZE(g_detectedModel), modelBuf);
        g_deviceConnected = true;
        PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, (WPARAM)g_battery, 0);

        // Ping device on startup to sync current hardware polling rate
        PingRapooDevice();

        HANDLE hReadEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        OVERLAPPED ov = {0};
        ov.hEvent = hReadEvent;

        BYTE buf[65] = {0};
        DWORD bytesRead = 0;
        int lastDpiLevel = -1;
        int lastDpiVal = -1;
        int lastBat = -1;

        while (WaitForSingleObject(g_hStopEvent, 0) == WAIT_TIMEOUT) {
            ResetEvent(hReadEvent);
            BOOL ok = ReadFile(hStatus, buf, 19, &bytesRead, &ov);
            if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    HANDLE waitHandles[2] = { g_hStopEvent, hReadEvent };
                    // 3500ms timeout for active heartbeat probe
                    DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, 3500);
                    if (waitRes == WAIT_OBJECT_0) {
                        CancelIo(hStatus);
                        break;
                    } else if (waitRes == WAIT_TIMEOUT) {
                        CancelIo(hStatus);
                        GetOverlappedResult(hStatus, &ov, &bytesRead, FALSE);

                        // Heartbeat check: actively query device status
                        bool alive = PingRapooDevice();
                        if (!alive) {
                            if (g_deviceConnected) {
                                g_deviceConnected = false;
                                PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, 0, 0);
                            }
                        } else {
                            if (!g_deviceConnected) {
                                g_deviceConnected = true;
                                PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, (WPARAM)g_battery, 0);
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

            if (bytesRead >= 9 && buf[0] == 0x07) {
                int level = (int)buf[2] + 1;
                int dpix = (int)buf[3] | ((int)buf[4] << 8);
                int dpiy = (int)buf[5] | ((int)buf[6] << 8);
                int status = (int)buf[7];
                int bat = (int)buf[8];

                if (bat > 100) bat = 100;

                bool dpiChanged = (lastDpiLevel != -1 && (level != lastDpiLevel || dpix != lastDpiVal));
                bool batChanged = (lastBat != -1 && bat != lastBat);

                InterlockedExchange(&g_dpiLevel, level);
                InterlockedExchange(&g_dpiX, dpix);
                InterlockedExchange(&g_dpiY, dpiy);
                InterlockedExchange(&g_battery, bat);

                if (!g_deviceConnected) {
                    g_deviceConnected = true;
                }

                if (dpiChanged) {
                    PostMessageW(g_hMainWnd, WM_APP_DPI_UPDATE, (WPARAM)level, (LPARAM)dpix);
                }
                if (batChanged || lastBat == -1) {
                    PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, (WPARAM)bat, 0);
                }

                lastDpiLevel = level;
                lastDpiVal = dpix;
                lastBat = bat;
            }
        }

        g_deviceConnected = false;
        PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, 0, 0);

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

        Sleep(1000);
    }

    return 0;
}

static bool IsAutoRunEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR path[MAX_PATH];
        DWORD len = sizeof(path);
        DWORD type = 0;
        LSTATUS st = RegQueryValueExW(hKey, APP_NAME, NULL, &type, (LPBYTE)path, &len);
        RegCloseKey(hKey);
        return (st == ERROR_SUCCESS);
    }
    return false;
}

static void SetAutoRun(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            WCHAR selfPath[MAX_PATH];
            GetModuleFileNameW(NULL, selfPath, MAX_PATH);
            RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (const BYTE*)selfPath, (DWORD)((wcslen(selfPath) + 1) * sizeof(WCHAR)));
        } else {
            RegDeleteValueW(hKey, APP_NAME);
        }
        RegCloseKey(hKey);
    }
}

// Right-Click Context Menu & Control Panel
static void ShowContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    WCHAR bufHeader[64];
    WCHAR bufBat[64];
    WCHAR bufDpi[64];
    WCHAR bufPoll[64];

    if (g_deviceConnected) {
        StringCchPrintfW(bufHeader, 64, L"%s (已连接)", g_detectedModel);
        StringCchPrintfW(bufBat, 64, L"电池电量: %d%%", g_battery);
        StringCchPrintfW(bufDpi, 64, L"当前 DPI: %d (第 %d 档)", g_dpiX, g_dpiLevel);
        StringCchPrintfW(bufPoll, 64, L"当前回报率: %d Hz", g_currentPollingHz);
    } else {
        StringCchPrintfW(bufHeader, 64, L"%s (休眠 / 未连接)", g_detectedModel);
        StringCchPrintfW(bufBat, 64, L"电池电量: --");
        StringCchPrintfW(bufDpi, 64, L"当前 DPI: --");
        StringCchPrintfW(bufPoll, 64, L"当前回报率: --");
    }

    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_HEADER, bufHeader);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_BATTERY, bufBat);
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_DPI, bufDpi);
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_POLL_INFO, bufPoll);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    // Submenu 1: Polling Rate
    HMENU hSubPoll = CreatePopupMenu();
    AppendMenuW(hSubPoll, MF_STRING | (g_currentPollingHz == 125 ? MF_CHECKED : 0), IDM_POLL_125, L"125 Hz");
    AppendMenuW(hSubPoll, MF_STRING | (g_currentPollingHz == 250 ? MF_CHECKED : 0), IDM_POLL_250, L"250 Hz");
    AppendMenuW(hSubPoll, MF_STRING | (g_currentPollingHz == 500 ? MF_CHECKED : 0), IDM_POLL_500, L"500 Hz");
    AppendMenuW(hSubPoll, MF_STRING | (g_currentPollingHz == 1000 ? MF_CHECKED : 0), IDM_POLL_1000, L"1000 Hz (默认标准)");
    AppendMenuW(hSubPoll, MF_STRING | (g_currentPollingHz == 2000 ? MF_CHECKED : 0), IDM_POLL_2000, L"2000 Hz");
    AppendMenuW(hSubPoll, MF_STRING | (g_currentPollingHz == 4000 ? MF_CHECKED : 0), IDM_POLL_4000, L"4000 Hz");
    AppendMenuW(hSubPoll, MF_STRING | (g_currentPollingHz == 8000 ? MF_CHECKED : 0), IDM_POLL_8000, L"8000 Hz (电竞高刷)");
    AppendMenuW(hMenu, MF_POPUP | (g_deviceConnected ? 0 : MF_GRAYED), (UINT_PTR)hSubPoll, L"回报率设置");

    // Submenu 2: Sleep Timeout
    HMENU hSubSleep = CreatePopupMenu();
    AppendMenuW(hSubSleep, MF_STRING | (g_currentSleepMin == 2 ? MF_CHECKED : 0), IDM_SLEEP_2M, L"2 分钟");
    AppendMenuW(hSubSleep, MF_STRING | (g_currentSleepMin == 5 ? MF_CHECKED : 0), IDM_SLEEP_5M, L"5 分钟");
    AppendMenuW(hSubSleep, MF_STRING | (g_currentSleepMin == 10 ? MF_CHECKED : 0), IDM_SLEEP_10M, L"10 分钟 (推荐)");
    AppendMenuW(hSubSleep, MF_STRING | (g_currentSleepMin == 30 ? MF_CHECKED : 0), IDM_SLEEP_30M, L"30 分钟");
    AppendMenuW(hSubSleep, MF_STRING | (g_currentSleepMin == 60 ? MF_CHECKED : 0), IDM_SLEEP_60M, L"60 分钟");
    AppendMenuW(hMenu, MF_POPUP | (g_deviceConnected ? 0 : MF_GRAYED), (UINT_PTR)hSubSleep, L"休眠时间");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    UINT autoRunFlags = MF_STRING | (IsAutoRunEnabled() ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, autoRunFlags, IDM_AUTORUN, L"开机自启动");
    AppendMenuW(hMenu, MF_STRING, IDM_RECONNECT, L"重新连接外设");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"退出");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_uTaskbarRestartMsg && g_uTaskbarRestartMsg != 0) {
        UpdateTrayIcon(g_battery);
        return 0;
    }

    switch (msg) {
        case WM_TRAY_ICON: {
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                ShowContextMenu(hWnd);
            } else if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                ShowOsdNotification(g_dpiLevel, g_dpiX, g_battery);
            }
            return 0;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDM_AUTORUN: {
                    bool cur = IsAutoRunEnabled();
                    SetAutoRun(!cur);
                    break;
                }
                case IDM_RECONNECT: {
                    SetEvent(g_hStopEvent);
                    WaitForSingleObject(g_hHidThread, 1000);
                    CloseHandle(g_hHidThread);
                    ResetEvent(g_hStopEvent);
                    g_hHidThread = CreateThread(NULL, 0, HidWorkerThread, NULL, 0, NULL);
                    break;
                }
                case IDM_POLL_125:  SetPollingRate(125);  break;
                case IDM_POLL_250:  SetPollingRate(250);  break;
                case IDM_POLL_500:  SetPollingRate(500);  break;
                case IDM_POLL_1000: SetPollingRate(1000); break;
                case IDM_POLL_2000: SetPollingRate(2000); break;
                case IDM_POLL_4000: SetPollingRate(4000); break;
                case IDM_POLL_8000: SetPollingRate(8000); break;

                case IDM_SLEEP_2M:  SetSleepTimeout(2);  break;
                case IDM_SLEEP_5M:  SetSleepTimeout(5);  break;
                case IDM_SLEEP_10M: SetSleepTimeout(10); break;
                case IDM_SLEEP_30M: SetSleepTimeout(30); break;
                case IDM_SLEEP_60M: SetSleepTimeout(60); break;

                case IDM_EXIT: {
                    DestroyWindow(hWnd);
                    break;
                }
            }
            return 0;
        }
        case WM_APP_DPI_UPDATE: {
            int level = (int)wParam;
            int dpix = (int)lParam;
            ShowOsdNotification(level, dpix, g_battery);
            UpdateTrayTooltip();
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            return 0;
        }
        case WM_APP_BAT_UPDATE: {
            int bat = (int)wParam;
            UpdateTrayIcon(bat);
            return 0;
        }
        case WM_SETTINGCHANGE: {
            UpdateTrayIcon(g_battery);
            return 0;
        }
        case WM_DEVICECHANGE: {
            return 0;
        }
        case WM_DESTROY: {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Local\\rapoo-traySingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    InitializeCriticalSection(&g_csDevIO);

    // Enable modern Per-Monitor V2 DPI awareness
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef BOOL (WINAPI *pfnSetDpiAwareV2)(DPI_AWARENESS_CONTEXT);
        pfnSetDpiAwareV2 fnSetDpiAwareV2 = (pfnSetDpiAwareV2)GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
        if (fnSetDpiAwareV2) {
            fnSetDpiAwareV2(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    g_hInstance = hInstance;

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"RapooTrayMessageWnd";
    RegisterClassExW(&wc);

    g_hMainWnd = CreateWindowExW(0, wc.lpszClassName, L"RapooTray", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    g_uTaskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wcOsd = {0};
    wcOsd.cbSize = sizeof(WNDCLASSEXW);
    wcOsd.lpfnWndProc = OsdWndProc;
    wcOsd.hInstance = hInstance;
    wcOsd.lpszClassName = L"RapooOsdPopupWnd";
    wcOsd.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wcOsd);

    g_hOsdWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wcOsd.lpszClassName,
        L"RapooOSD",
        WS_POPUP,
        0, 0, 260, 76,
        NULL, NULL, hInstance, NULL
    );

    DEV_BROADCAST_DEVICEINTERFACE_W dbFilter = {0};
    dbFilter.dbcc_size = sizeof(dbFilter);
    dbFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    HidD_GetHidGuid(&dbFilter.dbcc_classguid);
    RegisterDeviceNotificationW(g_hMainWnd, &dbFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hMainWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    g_nid.hIcon = CreateBatteryIcon(100);
    UpdateTrayTooltip();
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    g_hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_hHidThread = CreateThread(NULL, 0, HidWorkerThread, NULL, 0, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    SetEvent(g_hStopEvent);
    WaitForSingleObject(g_hHidThread, 2000);
    CloseHandle(g_hHidThread);
    CloseHandle(g_hStopEvent);

    if (g_nid.hIcon) {
        DestroyIcon(g_nid.hIcon);
    }
    if (g_hOsdWnd) {
        DestroyWindow(g_hOsdWnd);
    }

    DeleteCriticalSection(&g_csDevIO);
    CloseHandle(hMutex);
    return (int)msg.wParam;
}
