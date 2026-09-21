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
#define IDM_AUTORUN         2004
#define IDM_RECONNECT       2005
#define IDM_EXIT            2006

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

static volatile LONG g_battery = 100;
static volatile LONG g_dpiLevel = 1;
static volatile LONG g_dpiX = 1200;
static volatile LONG g_dpiY = 1200;

static WCHAR g_osdTextLine1[64] = L"DPI 1200";
static WCHAR g_osdTextLine2[64] = L"第 2 档  |  电量 100%";
static BYTE g_osdAlpha = 0;

static UINT g_uTaskbarRestartMsg = 0;
static const WCHAR* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR* APP_NAME = L"VT3S";

static void UpdateTrayTooltip();
static void UpdateTrayIcon(int battery);
static void ShowOsdNotification(int dpiLevel, int dpiX, int battery);
static bool IsAutoRunEnabled();
static void SetAutoRun(bool enable);

// 3x5 font for 16x16: 3 bits per row, 5 rows
static const uint8_t FONT_3X5[10][5] = {
    { 0x07, 0x05, 0x05, 0x05, 0x07 }, // '0' (111, 101, 101, 101, 111)
    { 0x02, 0x06, 0x02, 0x02, 0x07 }, // '1' (010, 110, 010, 010, 111)
    { 0x07, 0x01, 0x07, 0x04, 0x07 }, // '2' (111, 001, 111, 100, 111)
    { 0x07, 0x01, 0x07, 0x01, 0x07 }, // '3' (111, 001, 111, 001, 111)
    { 0x05, 0x05, 0x07, 0x01, 0x01 }, // '4' (101, 101, 111, 001, 001)
    { 0x07, 0x04, 0x07, 0x01, 0x07 }, // '5' (111, 100, 111, 001, 111)
    { 0x07, 0x04, 0x07, 0x05, 0x07 }, // '6' (111, 100, 111, 101, 111)
    { 0x07, 0x01, 0x02, 0x02, 0x02 }, // '7' (111, 001, 010, 010, 010)
    { 0x07, 0x05, 0x07, 0x05, 0x07 }, // '8' (111, 101, 111, 101, 111)
    { 0x07, 0x05, 0x07, 0x01, 0x07 }  // '9' (111, 101, 111, 001, 111)
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

// Generate Win11-style slim battery icon.
// Rendered at 4x supersampling then box-downsampled, so edges and digits
// come out anti-aliased instead of fat/pixelated in the tray.
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

    uint32_t c_fill, c_frame, c_empty, c_text_fill, c_text_empty;
    // Empty track is FULLY TRANSPARENT (taskbar shows through); digits use
    // ONE soft dark-gray color everywhere.
    const uint32_t c_digit = 0xFF2B2B2B; // soft black
    if (battery > 20) {
        c_fill        = 0xFF2DD773; // Emerald green
        c_frame       = 0xFFB4DCC8;
        c_empty       = 0x00000000; // transparent track
        c_text_fill   = c_digit;
        c_text_empty  = c_digit;
    } else if (battery > 10) {
        c_fill        = 0xFFFFB923; // Warm amber (11% ~ 20%)
        c_frame       = 0xFFDC9E28;
        c_empty       = 0x00000000; // transparent track
        c_text_fill   = c_digit;
        c_text_empty  = c_digit;
    } else {
        c_fill        = 0xFFFF4646; // Critical red (<= 10%)
        c_frame       = 0xFFDC4646;
        c_empty       = 0x00000000; // transparent track
        c_text_fill   = c_digit;
        c_text_empty  = c_digit;
    }

    // --- hi-res drawing helpers ---
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

    // --- slim battery geometry (hi-res units) ---
    int pad_y  = ((size < 20) ? 1 : 2) * SS;
    int tip_w  = 2 * SS;                  // thin tip -> longer, slimmer body
    int tip_h  = W * 34 / 100;
    int tip_y0 = (W - tip_h) / 2;
    int tip_y1 = tip_y0 + tip_h - 1;

    int body_x0 = 0;
    int body_y0 = pad_y;
    int body_x1 = W - 1 - tip_w;
    int body_y1 = W - 1 - pad_y;
    int radius  = 3 * SS;
    int frame_t = SS + SS / 2;            // ~1.5px frame at final size

    FillRoundRect(body_x0, body_y0, body_x1, body_y1, c_frame, radius);
    FillRoundRect(body_x0 + frame_t, body_y0 + frame_t,
                  body_x1 - frame_t, body_y1 - frame_t, c_empty, radius - frame_t);
    FillRoundRect(body_x1 + 1, tip_y0, W - 1, tip_y1, c_frame, SS);

    int in_x0 = body_x0 + frame_t;
    int in_x1 = body_x1 - frame_t;
    int in_y0 = body_y0 + frame_t;
    int in_w  = in_x1 - in_x0 + 1;
    int in_h  = (body_y1 - frame_t) - in_y0 + 1;

    int fill_w = (in_w * battery + 50) / 100;
    if (fill_w < SS && battery > 0) fill_w = SS;
    if (fill_w > in_w) fill_w = in_w;
    int fill_end_x = in_x0 + fill_w; // hi-res gauge boundary (for text contrast)

    if (fill_w > 0) {
        FillRoundRect(in_x0, in_y0, in_x0 + fill_w - 1, body_y1 - frame_t,
                      c_fill, radius - frame_t);
    }

    // --- digits: scale font to fill the battery interior so numbers stay readable ---
    auto DrawDigitScaled = [&](const uint16_t* rows, int fw, int fh,
                               int x0, int y0, int scale) {
        for (int r = 0; r < fh; ++r)
            for (int c = 0; c < fw; ++c) {
                if (!((rows[r] >> (fw - 1 - c)) & 1)) continue;
                for (int dy = 0; dy < scale; ++dy)
                    for (int dx = 0; dx < scale; ++dx) {
                        int px = x0 + c * scale + dx;
                        int py = y0 + r * scale + dy;
                        HiPixel(px, py, (px < fill_end_x) ? c_text_fill : c_text_empty);
                    }
            }
    };

    char s[8];
    snprintf(s, sizeof(s), "%d", battery);
    int len = (int)strlen(s);

    {
        // font metrics (font pixels)
        const int fw = (size < 20) ? 3 : 5;
        const int fh = (size < 20) ? 5 : 9;

        int gap_fp = (len == 1) ? 0 : 1;                 // 1 font-pixel gap between digits
        int text_w_fp = len * fw + (len - 1) * gap_fp;

        // scale so digit height fills inner height; shrink if width overflows
        int scale = in_h / fh;
        if (scale < 1) scale = 1;
        while (scale > 1 && text_w_fp * scale > in_w) --scale;

        int text_h = fh * scale;
        int text_w = text_w_fp * scale;
        int sx = in_x0 + (in_w - text_w) / 2;
        int sy = in_y0 + (in_h - text_h) / 2;

        if (battery == 100 && size < 20) {
            // "00" with a leading 1 bar, 3x5 font
            int cur = sx;
            for (int t = 0; t < scale; ++t)
                for (int r = 0; r < text_h; ++r)
                    HiPixel(cur + t, sy + r,
                            (cur + t < fill_end_x) ? c_text_fill : c_text_empty);
            cur += scale + gap_fp * scale;
            uint16_t rows5[5];
            for (int r = 0; r < 5; ++r) rows5[r] = FONT_3X5[0][r];
            DrawDigitScaled(rows5, 3, 5, cur, sy, scale);
            cur += 3 * scale + gap_fp * scale;
            DrawDigitScaled(rows5, 3, 5, cur, sy, scale);
        } else if (battery == 100) {
            // "00" with a leading 1 bar, 4x9 font
            int cur = sx;
            for (int t = 0; t < 2 * scale; ++t)
                for (int r = 0; r < text_h; ++r)
                    HiPixel(cur + t, sy + r,
                            (cur + t < fill_end_x) ? c_text_fill : c_text_empty);
            cur += 2 * scale + gap_fp * scale;
            uint16_t rows9[9];
            for (int r = 0; r < 9; ++r) rows9[r] = FONT_4X9_0[r];
            DrawDigitScaled(rows9, 4, 9, cur, sy, scale);
            cur += 4 * scale + gap_fp * scale;
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
                cur += fw * scale + gap_fp * scale;
            }
        }
    }

    // --- box downsample SSx -> 1x (alpha-weighted average = anti-aliasing) ---
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
                    sumA += a;
                    sumR += ((c >> 16) & 0xFF) * a;
                    sumG += ((c >> 8) & 0xFF) * a;
                    sumB += (c & 0xFF) * a;
                }
            }
            uint32_t a = sumA / SS2;
            uint32_t r = sumA ? sumR / sumA : 0;
            uint32_t g = sumA ? sumG / sumA : 0;
            uint32_t b = sumA ? sumB / sumA : 0;
            out[y * size + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, NULL);

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
    StringCchPrintfW(
        g_nid.szTip,
        ARRAYSIZE(g_nid.szTip),
        L"VT3S\n电量: %d%%\nDPI: %d (第 %d 档)",
        g_battery,
        g_dpiX,
        g_dpiLevel
    );
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

            // DPI Value (Prominent Bold Font)
            HFONT hFontBig = CreateFontW(
                -26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI"
            );
            HGDIOBJ oldFont = SelectObject(memDC, hFontBig);
            SetTextColor(memDC, RGB(255, 255, 255));
            RECT rcTop = rcBox;
            rcTop.bottom = rcBox.top + 42;
            DrawTextW(memDC, g_osdTextLine1, -1, &rcTop, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Subtitle (Level + Battery in Cyan)
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

static void ShowOsdNotification(int dpiLevel, int dpiX, int battery) {
    if (!g_hOsdWnd) return;

    StringCchPrintfW(g_osdTextLine1, ARRAYSIZE(g_osdTextLine1), L"DPI %d", dpiX);
    StringCchPrintfW(g_osdTextLine2, ARRAYSIZE(g_osdTextLine2), L"第 %d 档  |  电量 %d%%", dpiLevel, battery);

    RECT rcWork;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0)) {
        rcWork.left = 0;
        rcWork.top = 0;
        rcWork.right = GetSystemMetrics(SM_CXSCREEN);
        rcWork.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int w = 240;
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

// Find Rapoo col09 device path dynamically (supports wireless dongle pid_1460, wired USB pid_4660 and VT3s pid_1411)
static bool FindRapooReportPath(WCHAR* outPath, DWORD maxLen) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA devData = {0};
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

            if (wcsstr(lowerPath, L"vid_24ae") && 
               (wcsstr(lowerPath, L"pid_1460") || wcsstr(lowerPath, L"pid_4660") || wcsstr(lowerPath, L"pid_1411")) && 
                wcsstr(lowerPath, L"col09")) {
                StringCchCopyW(outPath, maxLen, pDetail->DevicePath);
                found = true;
                free(pDetail);
                break;
            }
        }
        free(pDetail);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return found;
}

// Low-overhead background reader thread
static DWORD WINAPI HidWorkerThread(LPVOID lpParam) {
    WCHAR devPath[MAX_PATH] = {0};

    while (WaitForSingleObject(g_hStopEvent, 200) == WAIT_TIMEOUT) {
        if (!FindRapooReportPath(devPath, MAX_PATH)) {
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
            Sleep(1500);
            continue;
        }

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

        CloseHandle(hReadEvent);
        CloseHandle(hDev);
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

static void ShowContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    WCHAR bufHeader[64];
    WCHAR bufBat[64];
    WCHAR bufDpi[64];

    StringCchPrintfW(bufHeader, 64, L"VT3S");
    StringCchPrintfW(bufBat, 64, L"电池电量: %d%%", g_battery);
    StringCchPrintfW(bufDpi, 64, L"当前 DPI: %d (第 %d 档)", g_dpiX, g_dpiLevel);

    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_HEADER, bufHeader);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_BATTERY, bufBat);
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_DPI, bufDpi);
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
        L"VT3S",
        WS_POPUP,
        0, 0, 240, 76,
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

    CloseHandle(hMutex);
    return (int)msg.wParam;
}
