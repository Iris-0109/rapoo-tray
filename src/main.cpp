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
#include <uxtheme.h>
#include <dwmapi.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

typedef enum _WINDOWCOMPOSITIONATTRIB {
    WCA_ACCENT_POLICY = 19
} WINDOWCOMPOSITIONATTRIB;

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
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef BOOL (WINAPI *pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);
static pfnSetWindowCompositionAttribute fnSetWindowCompositionAttribute = NULL;

static bool IsSystemDarkMode() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 1;
        DWORD size = sizeof(val);
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (val == 0);
        }
        RegCloseKey(hKey);
    }
    return true;
}

static void ApplyAcrylic(HWND hWnd, bool dark) {
    BOOL bDark = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hWnd, (DWORD)DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
    int corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hWnd, (DWORD)DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    int backdrop = 3; // DWMSBT_TRANSIENTWINDOW (Acrylic)
    DwmSetWindowAttribute(hWnd, (DWORD)DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    if (!fnSetWindowCompositionAttribute) {
        HMODULE hUser = GetModuleHandleW(L"user32.dll");
        if (hUser) {
            fnSetWindowCompositionAttribute = (pfnSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
        }
    }
    if (fnSetWindowCompositionAttribute) {
        ACCENT_POLICY policy;
        memset(&policy, 0, sizeof(policy));
        policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
        policy.AccentFlags = 2;
        policy.GradientColor = dark ? 0xCC201E1C : 0xCCFAF8F5;
        WINDOWCOMPOSITIONATTRIBDATA data;
        data.Attrib = WCA_ACCENT_POLICY;
        data.pvData = &policy;
        data.cbData = sizeof(policy);
        fnSetWindowCompositionAttribute(hWnd, &data);
    }
    SetLayeredWindowAttributes(hWnd, 0, 230, LWA_ALPHA);
}

enum PreferredAppMode {
    Default,
    AllowDark,
    ForceDark,
    ForceLight,
    Max
};

typedef PreferredAppMode (WINAPI *pfnSetPreferredAppMode)(PreferredAppMode);
typedef BOOL (WINAPI *pfnAllowDarkModeForWindow)(HWND, BOOL);
typedef void (WINAPI *pfnFlushMenuThemes)();

static pfnSetPreferredAppMode fnSetPreferredAppMode = NULL;
static pfnAllowDarkModeForWindow fnAllowDarkModeForWindow = NULL;
static pfnFlushMenuThemes fnFlushMenuThemes = NULL;

static void InitThemeSupport() {
    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxtheme) {
        fnSetPreferredAppMode = (pfnSetPreferredAppMode)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        fnAllowDarkModeForWindow = (pfnAllowDarkModeForWindow)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133));
        fnFlushMenuThemes = (pfnFlushMenuThemes)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136));

        if (fnSetPreferredAppMode) {
            fnSetPreferredAppMode(AllowDark);
        }
        if (fnFlushMenuThemes) {
            fnFlushMenuThemes();
        }
    }
}

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
static HANDLE g_hDevChangeEvent = NULL;

static CRITICAL_SECTION g_csDevIO;
static HANDLE g_hControlDev = INVALID_HANDLE_VALUE;
static HANDLE g_hFeatureDev = INVALID_HANDLE_VALUE;

static volatile LONG g_battery = 100;
static volatile LONG g_dpiLevel = 1;
static volatile LONG g_dpiX = 1200;
static volatile LONG g_dpiY = 1200;

static volatile LONG g_currentPollingHz = 1000;
static volatile LONG g_currentSleepMin = 10;
static volatile LONG g_isCharging = 0;
static volatile bool g_isWiredMode = false;

static WCHAR g_osdTextLine1[64] = L"第 1 档  DPI 1200";
static WCHAR g_osdTextLine2[64] = L"X 轴: 1200    Y 轴: 1200";
static WCHAR g_osdTextLine3[64] = L"雷柏 VT7  |  电量 100%  |  1000 Hz";
static BYTE g_osdAlpha = 0;

static UINT g_uTaskbarRestartMsg = 0;
static const WCHAR* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR* APP_NAME = L"rapoo-tray";
static WCHAR g_detectedModel[64] = L"雷柏无线鼠标";
static volatile bool g_deviceConnected = false;

static DWORD LoadRegistryDword(const WCHAR* valueName, DWORD defaultValue) {
    HKEY hKey;
    DWORD value = defaultValue;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\rapoo-tray", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD data = 0;
        DWORD size = sizeof(data);
        if (RegQueryValueExW(hKey, valueName, NULL, &type, (LPBYTE)&data, &size) == ERROR_SUCCESS) {
            if (type == REG_DWORD) {
                value = data;
            }
        }
        RegCloseKey(hKey);
    }
    return value;
}

static void SaveRegistryDword(const WCHAR* valueName, DWORD value) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\rapoo-tray", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, valueName, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
        RegCloseKey(hKey);
    }
}

static void UpdateTrayTooltip();
static void UpdateTrayIcon(int battery);
static void ShowCustomOsd(const WCHAR* line1, const WCHAR* line2, const WCHAR* line3);
static void ShowOsdNotification(int dpiLevel, int dpiX, int dpiY, int battery, int pollingHz);
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

    const uint32_t c_fill  = (g_isCharging != 0) ? 0xFF00D2FF : 0xFF2DD773; // Electric cyan when charging, Emerald green when normal
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
        if (g_isCharging) {
            StringCchPrintfW(
                g_nid.szTip,
                ARRAYSIZE(g_nid.szTip),
                L"%s\n电量: %d%% (充电中 ⚡)\nDPI: %d (第 %d 档)\n回报率: %d Hz",
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
                L"%s\n电量: %d%%\nDPI: %d (第 %d 档)\n回报率: %d Hz",
                g_detectedModel,
                g_battery,
                g_dpiX,
                g_dpiLevel,
                g_currentPollingHz
            );
        }
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
            RoundRect(memDC, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 24, 24);

            SetBkMode(memDC, TRANSPARENT);

            // Line 1: Level + DPI Value (Bold Font -22)
            HFONT hFontBig = CreateFontW(
                -22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI"
            );
            HGDIOBJ oldFont = SelectObject(memDC, hFontBig);
            SetTextColor(memDC, RGB(255, 255, 255));
            RECT rcTop = rcBox;
            rcTop.top = rcBox.top + 7;
            rcTop.bottom = rcBox.top + 34;
            DrawTextW(memDC, g_osdTextLine1, -1, &rcTop, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Line 2: X & Y Axis DPI (Font -15, slightly larger than bottom line -13, smaller than top line -22)
            HFONT hFontMid = CreateFontW(
                -15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI"
            );
            SelectObject(memDC, hFontMid);
            SetTextColor(memDC, RGB(215, 230, 248));
            RECT rcMid = rcBox;
            rcMid.top = rcBox.top + 34;
            rcMid.bottom = rcBox.top + 57;
            DrawTextW(memDC, g_osdTextLine2, -1, &rcMid, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Line 3: Model + Battery + Polling Rate (Signature Cyan Font -13)
            HFONT hFontSub = CreateFontW(
                -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI"
            );
            SelectObject(memDC, hFontSub);
            SetTextColor(memDC, RGB(130, 215, 255));
            RECT rcBot = rcBox;
            rcBot.top = rcBox.top + 57;
            rcBot.bottom = rcBox.top + 82;
            DrawTextW(memDC, g_osdTextLine3, -1, &rcBot, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldFont);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBr);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            DeleteObject(hFontBig);
            DeleteObject(hFontMid);
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

static void ShowCustomOsd(const WCHAR* line1, const WCHAR* line2, const WCHAR* line3) {
    if (!g_hOsdWnd) return;

    StringCchCopyW(g_osdTextLine1, ARRAYSIZE(g_osdTextLine1), line1);
    StringCchCopyW(g_osdTextLine2, ARRAYSIZE(g_osdTextLine2), line2);
    if (line3) {
        StringCchCopyW(g_osdTextLine3, ARRAYSIZE(g_osdTextLine3), line3);
    } else {
        g_osdTextLine3[0] = 0;
    }

    RECT rcWork;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0)) {
        rcWork.left = 0;
        rcWork.top = 0;
        rcWork.right = GetSystemMetrics(SM_CXSCREEN);
        rcWork.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int w = 288;
    int h = 92;
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

static void ShowOsdNotification(int dpiLevel, int dpiX, int dpiY, int battery, int pollingHz) {
    WCHAR l1[64], l2[64], l3[64];
    StringCchPrintfW(l1, ARRAYSIZE(l1), L"第 %d 档  DPI %d", dpiLevel, dpiX);
    StringCchPrintfW(l2, ARRAYSIZE(l2), L"X 轴: %d    Y 轴: %d", dpiX, dpiY);
    if (g_isCharging) {
        StringCchPrintfW(l3, ARRAYSIZE(l3), L"%s  |  电量 %d%% (充电中 ⚡)  |  %d Hz", g_detectedModel, battery, pollingHz);
    } else {
        StringCchPrintfW(l3, ARRAYSIZE(l3), L"%s  |  电量 %d%%  |  %d Hz", g_detectedModel, battery, pollingHz);
    }
    ShowCustomOsd(l1, l2, l3);
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

        WCHAR l1[64], l2[64], l3[64];
        StringCchPrintfW(l1, ARRAYSIZE(l1), L"回报率: %d Hz", hz);
        StringCchPrintfW(l2, ARRAYSIZE(l2), L"设置已即时生效");
        StringCchPrintfW(l3, ARRAYSIZE(l3), L"%s  |  电量 %d%%  |  %d Hz", g_detectedModel, g_battery, hz);
        ShowCustomOsd(l1, l2, l3);
    }
}

static void SetSleepTimeout(int minutes) {
    if (minutes < 2) minutes = 2;
    if (minutes > 120) minutes = 120;
    BYTE code = (BYTE)minutes;

    if (SendRapooCommand(0x08, 0xC2, &code, 1)) {
        InterlockedExchange(&g_currentSleepMin, minutes);
        SaveRegistryDword(L"SleepTimeout", (DWORD)minutes);

        WCHAR l1[64], l2[64], l3[64];
        StringCchPrintfW(l1, ARRAYSIZE(l1), L"休眠时间: %d 分钟", minutes);
        StringCchPrintfW(l2, ARRAYSIZE(l2), L"设置已即时生效并已保存");
        if (g_isCharging) {
            StringCchPrintfW(l3, ARRAYSIZE(l3), L"%s  |  电量 %d%% (充电中 ⚡)  |  %d Hz", g_detectedModel, g_battery, g_currentPollingHz);
        } else {
            StringCchPrintfW(l3, ARRAYSIZE(l3), L"%s  |  电量 %d%%  |  %d Hz", g_detectedModel, g_battery, g_currentPollingHz);
        }
        ShowCustomOsd(l1, l2, l3);
    }
}

// --------------------------------------------------------------------------
// Multi-Endpoint Device Enumeration via HID Caps
// --------------------------------------------------------------------------

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

    // Phase 1: Determine the target device.
    // Check if any WIRED Rapoo device (pid_46xx or pid_1411) is currently present.
    // If a wired device is found, prioritize it over wireless dongles!
    WCHAR targetPid[32] = {0};
    bool isWired = false;

    // First scan: look specifically for wired Rapoo mice
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
                        // Found wired device! Extract 8 chars e.g. "pid_4660"
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

    // If no wired device found, scan for wireless dongles
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
                        StringCchCopyNW(targetPid, 32, pPid, 8); // e.g. "pid_1460"
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

    // Phase 2: Collect endpoints strictly belonging to the chosen targetPid!
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

                if (hProbe != INVALID_HANDLE_VALUE) {
                    PHIDP_PREPARSED_DATA pData = NULL;
                    if (HidD_GetPreparsedData(hProbe, &pData)) {
                        HIDP_CAPS caps;
                        if (HidP_GetCaps(pData, &caps) == HIDP_STATUS_SUCCESS) {
                            if (caps.UsagePage == 0xFF00) {
                                if ((caps.Usage == 0x0002 || (caps.InputReportByteLength >= 19 && wcsstr(lowerPath, L"col09"))) && caps.InputReportByteLength >= 19) {
                                    StringCchCopyW(pathStatus, MAX_PATH, pDetail->DevicePath);
                                    found = true;
                                } else if (caps.Usage == 0x000E || caps.OutputReportByteLength == 33) {
                                    StringCchCopyW(pathControl, MAX_PATH, pDetail->DevicePath);
                                } else if (caps.Usage == 0x000F || caps.FeatureReportByteLength == 33) {
                                    StringCchCopyW(pathFeature, MAX_PATH, pDetail->DevicePath);
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

    if (found) {
        if (outModel && maxModelLen > 0) {
            if (wcsstr(targetPid, L"1460") || wcsstr(targetPid, L"4660")) {
                StringCchCopyW(outModel, maxModelLen, isWired ? L"雷柏 VT7 (有线模式)" : L"雷柏 VT7");
            } else if (wcsstr(targetPid, L"1406") || wcsstr(targetPid, L"1410") || wcsstr(targetPid, L"4606") || wcsstr(targetPid, L"1411")) {
                StringCchCopyW(outModel, maxModelLen, isWired ? L"雷柏 VT3S (有线模式)" : L"雷柏 VT3S");
            } else if (wcsstr(targetPid, L"1412") || wcsstr(targetPid, L"1413") || wcsstr(targetPid, L"1440") || wcsstr(targetPid, L"4612") || wcsstr(targetPid, L"4613") || wcsstr(targetPid, L"4640")) {
                StringCchCopyW(outModel, maxModelLen, isWired ? L"雷柏 VT9 系列 (有线模式)" : L"雷柏 VT9 系列");
            } else {
                StringCchCopyW(outModel, maxModelLen, isWired ? L"雷柏游戏鼠标 (有线模式)" : L"雷柏无线鼠标");
            }
        }
        if (outIsWired) {
            *outIsWired = isWired;
        }
    }

    return found;
}

// Background worker thread for low-overhead HID monitoring & active heartbeats
static DWORD WINAPI HidWorkerThread(LPVOID lpParam) {
    WCHAR pathStatus[MAX_PATH] = {0};
    WCHAR pathControl[MAX_PATH] = {0};
    WCHAR pathFeature[MAX_PATH] = {0};
    WCHAR modelBuf[64] = {0};
    bool isWired = false;

    while (WaitForSingleObject(g_hStopEvent, 200) == WAIT_TIMEOUT) {
        if (!FindRapooEndpoints(pathStatus, pathControl, pathFeature, modelBuf, 64, &isWired)) {
            if (g_deviceConnected) {
                g_deviceConnected = false;
                InterlockedExchange(&g_isCharging, 0);
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
                InterlockedExchange(&g_isCharging, 0);
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
        g_isWiredMode = isWired;
        g_deviceConnected = true;
        PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, (WPARAM)g_battery, 0);

        // Ping device on startup to sync current hardware polling rate
        PingRapooDevice();

        // Push user's saved sleep timeout to ensure mouse hardware is synced!
        BYTE sleepCode = (BYTE)g_currentSleepMin;
        SendRapooCommand(0x08, 0xC2, &sleepCode, 1);

        HANDLE hReadEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        OVERLAPPED ov = {0};
        ov.hEvent = hReadEvent;

        BYTE buf[65] = {0};
        DWORD bytesRead = 0;
        int lastDpiLevel = -1;
        int lastDpiVal = -1;
        int lastBat = -1;
        int lastCharging = -1;

        while (WaitForSingleObject(g_hStopEvent, 0) == WAIT_TIMEOUT) {
            ResetEvent(hReadEvent);
            BOOL ok = ReadFile(hStatus, buf, 19, &bytesRead, &ov);
            if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    HANDLE waitHandles[3] = { g_hStopEvent, hReadEvent, g_hDevChangeEvent };
                    // 3500ms timeout for active heartbeat probe
                    DWORD waitRes = WaitForMultipleObjects(3, waitHandles, FALSE, 3500);
                    if (waitRes == WAIT_OBJECT_0) {
                        CancelIo(hStatus);
                        break;
                    } else if (waitRes == WAIT_OBJECT_0 + 2) {
                        // Device added or removed (e.g. wired plugged/unplugged)
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
                                InterlockedExchange(&g_isCharging, 0);
                                PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, 0, 0);
                            }
                        } else {
                            if (!g_deviceConnected) {
                                g_deviceConnected = true;
                                PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, (WPARAM)g_battery, (LPARAM)g_isCharging);
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
                int rawBat = (int)buf[8];

                bool charging = false;
                int bat = rawBat;
                if ((rawBat & 0x80) != 0) {
                    charging = true;
                    bat = rawBat & 0x7F;
                }
                if ((status & 0x02) != 0 || status == 0x02 || status == 0x03) {
                    charging = true;
                }
                if (bytesRead > 9 && (buf[9] == 0x01 || buf[9] == 0x02)) {
                    charging = true;
                }
                if (bat > 100) bat = 100;

                bool dpiChanged = (lastDpiLevel != -1 && (level != lastDpiLevel || dpix != lastDpiVal));
                bool batChanged = (lastBat != -1 && bat != lastBat);
                bool chgChanged = (lastCharging != -1 && charging != (lastCharging != 0));

                InterlockedExchange(&g_dpiLevel, level);
                InterlockedExchange(&g_dpiX, dpix);
                InterlockedExchange(&g_dpiY, dpiy);
                InterlockedExchange(&g_battery, bat);
                InterlockedExchange(&g_isCharging, charging ? 1 : 0);

                if (!g_deviceConnected) {
                    g_deviceConnected = true;
                }

                if (dpiChanged) {
                    PostMessageW(g_hMainWnd, WM_APP_DPI_UPDATE, (WPARAM)level, (LPARAM)dpix);
                }
                if (batChanged || chgChanged || lastBat == -1) {
                    PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, (WPARAM)bat, (LPARAM)(charging ? 1 : 0));
                }

                lastDpiLevel = level;
                lastDpiVal = dpix;
                lastBat = bat;
                lastCharging = charging ? 1 : 0;
            }
        }

        g_deviceConnected = false;
        InterlockedExchange(&g_isCharging, 0);
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

        Sleep(500);
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

// ============================================================================
// Modern Native Win32 Acrylic Context Menu Implementation (Option B)
// ============================================================================
struct MainMenuItem {
    int id;
    const wchar_t* label;
    const wchar_t* value;
    bool isSeparator;
    bool isHeader;
    int submenuType; // 0=none, 1=poll, 2=sleep
    bool isDisabled;
    bool isInteractive;
};

struct SubMenuItem {
    int id;
    const wchar_t* label;
    bool isChecked;
};

static HWND g_hAcrylicMenu = NULL;
static HWND g_hAcrylicSubMenu = NULL;
static bool g_bModalLoop = false;
static HHOOK g_hMenuMouseHook = NULL;
static int g_mainHover = -1;
static int g_subHover = -1;
static int g_activeSubId = 0; // 0=none, 1=poll, 2=sleep
static UINT g_curDpi = 96;
static bool g_curDark = true;
static HWND g_hParentAppWnd = NULL;

static inline int S(int v) {
    return MulDiv(v, g_curDpi, 96);
}

static SubMenuItem g_subPoll[7];
static SubMenuItem g_subSleep[5];

static void InitSubData(int pollHz, int sleepMin) {
    g_subPoll[0] = { IDM_POLL_125,  L"125 Hz", pollHz == 125 };
    g_subPoll[1] = { IDM_POLL_250,  L"250 Hz", pollHz == 250 };
    g_subPoll[2] = { IDM_POLL_500,  L"500 Hz", pollHz == 500 };
    g_subPoll[3] = { IDM_POLL_1000, L"1000 Hz (默认标准)", pollHz == 1000 };
    g_subPoll[4] = { IDM_POLL_2000, L"2000 Hz", pollHz == 2000 };
    g_subPoll[5] = { IDM_POLL_4000, L"4000 Hz", pollHz == 4000 };
    g_subPoll[6] = { IDM_POLL_8000, L"8000 Hz (电竞高刷)", pollHz == 8000 };

    g_subSleep[0] = { IDM_SLEEP_2M,  L"2 分钟", sleepMin == 2 };
    g_subSleep[1] = { IDM_SLEEP_5M,  L"5 分钟", sleepMin == 5 };
    g_subSleep[2] = { IDM_SLEEP_10M, L"10 分钟 (推荐)", sleepMin == 10 };
    g_subSleep[3] = { IDM_SLEEP_30M, L"30 分钟", sleepMin == 30 };
    g_subSleep[4] = { IDM_SLEEP_60M, L"60 分钟", sleepMin == 60 };
}

static void DismissSubMenu() {
    if (g_hAcrylicSubMenu) {
        DestroyWindow(g_hAcrylicSubMenu);
        g_hAcrylicSubMenu = NULL;
    }
    g_activeSubId = 0;
    g_subHover = -1;
}

static void DismissAllMenus() {
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

// SubMenu WndProc
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
        case WM_MOUSEMOVE: {
            int my = HIWORD(lParam);
            int count = (g_activeSubId == 1) ? 7 : 5;
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
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE: {
            if (g_subHover != -1) {
                g_subHover = -1;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            int count = (g_activeSubId == 1) ? 7 : 5;
            if (g_subHover >= 0 && g_subHover < count) {
                int cmd = (g_activeSubId == 1) ? g_subPoll[g_subHover].id : g_subSleep[g_subHover].id;
                HWND hOwner = g_hParentAppWnd;
                DismissAllMenus();
                if (hOwner) {
                    PostMessageW(hOwner, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
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

            COLORREF bgCol = g_curDark ? RGB(24, 26, 32) : RGB(248, 248, 252);
            COLORREF borderCol = g_curDark ? RGB(65, 70, 85) : RGB(210, 215, 225);
            COLORREF hoverCol = g_curDark ? RGB(52, 58, 72) : RGB(228, 232, 242);
            COLORREF textCol = g_curDark ? RGB(235, 240, 248) : RGB(30, 35, 45);
            COLORREF checkCol = g_curDark ? RGB(96, 205, 255) : RGB(0, 120, 215);

            HBRUSH bgBrush = CreateSolidBrush(bgCol);
            FillRect(memDC, &rc, bgBrush);
            DeleteObject(bgBrush);

            HPEN borderPen = CreatePen(PS_SOLID, 1, borderCol);
            HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
            HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, nullBrush);
            RoundRect(memDC, 0, 0, rc.right, rc.bottom, S(12), S(12));
            SelectObject(memDC, oldPen);
            DeleteObject(borderPen);

            HFONT hFont = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            HFONT oldFont = (HFONT)SelectObject(memDC, hFont);
            SetBkMode(memDC, TRANSPARENT);

            int count = (g_activeSubId == 1) ? 7 : 5;
            int itemH = S(28);
            int y = S(8);
            for (int i = 0; i < count; i++) {
                const SubMenuItem& it = (g_activeSubId == 1) ? g_subPoll[i] : g_subSleep[i];
                RECT rItem = { S(6), y, rc.right - S(6), y + itemH };

                if (i == g_subHover) {
                    HBRUSH hH = CreateSolidBrush(hoverCol);
                    HPEN nPen = (HPEN)GetStockObject(NULL_PEN);
                    HPEN oP = (HPEN)SelectObject(memDC, nPen);
                    HBRUSH oB = (HBRUSH)SelectObject(memDC, hH);
                    RoundRect(memDC, rItem.left, rItem.top + 1, rItem.right, rItem.bottom - 1, S(6), S(6));
                    SelectObject(memDC, oP);
                    SelectObject(memDC, oB);
                    DeleteObject(hH);
                }

                SetTextColor(memDC, textCol);
                RECT rText = { S(14), y, rc.right - S(32), y + itemH };
                DrawTextW(memDC, it.label, -1, &rText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                if (it.isChecked) {
                    SetTextColor(memDC, checkCol);
                    RECT rCheck = { rc.right - S(28), y, rc.right - S(10), y + itemH };
                    DrawTextW(memDC, L"✓", -1, &rCheck, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                y += itemH;
            }

            SelectObject(memDC, oldFont);
            DeleteObject(hFont);

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
        wcSub.lpszClassName = L"RapooAcrylicSubClass";
        wcSub.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcSub.style = CS_DROPSHADOW;
        RegisterClassExW(&wcSub);
        s_subRegistered = true;
    }

    int subW = (subType == 1) ? S(170) : S(140);
    int count = (subType == 1) ? 7 : 5;
    int subH = count * S(28) + S(16);

    HMONITOR hMon = MonitorFromRect(&rItemScreen, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);

    int subX = rItemScreen.left - subW - S(4);
    if (subX < mi.rcWork.left) {
        subX = rItemScreen.right + S(4);
    }
    int subY = rItemScreen.top - S(6);
    if (subY + subH > mi.rcWork.bottom) {
        subY = mi.rcWork.bottom - subH;
    }
    if (subY < mi.rcWork.top) {
        subY = mi.rcWork.top;
    }

    g_hAcrylicSubMenu = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"RapooAcrylicSubClass",
        L"AcrylicSub",
        WS_POPUP,
        subX, subY, subW, subH,
        g_hAcrylicMenu, NULL, hInst, NULL
    );

    ApplyAcrylic(g_hAcrylicSubMenu, g_curDark);
    ShowWindow(g_hAcrylicSubMenu, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hAcrylicSubMenu);
}

static MainMenuItem g_mainItems[13];
static const int MAIN_ITEM_COUNT = 13;

static int GetItemY(int idx) {
    int y = S(8);
    for (int i = 0; i < idx; i++) {
        if (g_mainItems[i].isHeader) y += S(44);
        else if (g_mainItems[i].isSeparator) y += S(9);
        else y += S(28);
    }
    return y;
}

static int GetItemH(int idx) {
    if (g_mainItems[idx].isHeader) return S(44);
    if (g_mainItems[idx].isSeparator) return S(9);
    return S(28);
}

static int HitTestMain(int my) {
    int y = S(8);
    for (int i = 0; i < MAIN_ITEM_COUNT; i++) {
        int h = GetItemH(i);
        if (my >= y && my < y + h) {
            if (g_mainItems[i].isHeader || g_mainItems[i].isSeparator || g_mainItems[i].isDisabled) return -1;
            return i;
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
                if (PtInRect(&rSub, pt)) {
                    return 0;
                }
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
                HWND hOwner = g_hParentAppWnd;
                DismissAllMenus();
                if (hOwner) {
                    PostMessageW(hOwner, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
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

            COLORREF bgCol = g_curDark ? RGB(24, 26, 32) : RGB(248, 248, 252);
            COLORREF borderCol = g_curDark ? RGB(65, 70, 85) : RGB(210, 215, 225);
            COLORREF hoverCol = g_curDark ? RGB(52, 58, 72) : RGB(228, 232, 242);
            COLORREF textCol = g_curDark ? RGB(235, 240, 248) : RGB(30, 35, 45);
            COLORREF mutedCol = g_curDark ? RGB(155, 165, 180) : RGB(100, 110, 125);
            COLORREF greenCol = g_curDark ? RGB(34, 197, 94) : RGB(22, 163, 74);
            COLORREF sepCol = g_curDark ? RGB(48, 52, 64) : RGB(220, 225, 235);
            COLORREF checkCol = g_curDark ? RGB(96, 205, 255) : RGB(0, 120, 215);

            HBRUSH bgBrush = CreateSolidBrush(bgCol);
            FillRect(memDC, &rc, bgBrush);
            DeleteObject(bgBrush);

            HPEN borderPen = CreatePen(PS_SOLID, 1, borderCol);
            HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
            HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, nullBrush);
            RoundRect(memDC, 0, 0, rc.right, rc.bottom, S(14), S(14));
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

            HFONT oldFont = (HFONT)SelectObject(memDC, hFontNormal);
            SetBkMode(memDC, TRANSPARENT);

            int y = S(8);
            for (int i = 0; i < MAIN_ITEM_COUNT; i++) {
                int h = GetItemH(i);
                if (g_mainItems[i].isHeader) {
                    SelectObject(memDC, hFontBold);
                    SetTextColor(memDC, textCol);
                    RECT rTitle = { S(14), y + S(2), rc.right - S(14), y + S(22) };
                    DrawTextW(memDC, g_mainItems[i].label, -1, &rTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                    SelectObject(memDC, hFontSub);
                    SetTextColor(memDC, (wcsstr(g_mainItems[i].value, L"已连接") != NULL) ? greenCol : mutedCol);
                    RECT rSub = { S(14), y + S(22), rc.right - S(14), y + S(42) };
                    DrawTextW(memDC, g_mainItems[i].value, -1, &rSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    y += h;
                } else if (g_mainItems[i].isSeparator) {
                    HPEN pSep = CreatePen(PS_SOLID, 1, sepCol);
                    HPEN oP = (HPEN)SelectObject(memDC, pSep);
                    MoveToEx(memDC, S(12), y + S(4), NULL);
                    LineTo(memDC, rc.right - S(12), y + S(4));
                    SelectObject(memDC, oP);
                    DeleteObject(pSep);
                    y += h;
                } else {
                    RECT rItem = { S(6), y, rc.right - S(6), y + h };

                    if (i == g_mainHover && g_mainItems[i].isInteractive && !g_mainItems[i].isDisabled) {
                        HBRUSH hH = CreateSolidBrush(hoverCol);
                        HPEN nPen = (HPEN)GetStockObject(NULL_PEN);
                        HPEN oP = (HPEN)SelectObject(memDC, nPen);
                        HBRUSH oB = (HBRUSH)SelectObject(memDC, hH);
                        RoundRect(memDC, rItem.left, rItem.top + 1, rItem.right, rItem.bottom - 1, S(6), S(6));
                        SelectObject(memDC, oP);
                        SelectObject(memDC, oB);
                        DeleteObject(hH);
                    }

                    SelectObject(memDC, hFontNormal);
                    SetTextColor(memDC, g_mainItems[i].isDisabled ? mutedCol : textCol);
                    RECT rLabel = { S(14), y, rc.right - S(90), y + h };
                    DrawTextW(memDC, g_mainItems[i].label, -1, &rLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                    if (g_mainItems[i].value && g_mainItems[i].value[0]) {
                        bool isCheck = (wcscmp(g_mainItems[i].value, L"✓ 已开启") == 0);
                        SetTextColor(memDC, isCheck ? checkCol : mutedCol);
                        RECT rVal = { rc.right - S(110), y, rc.right - S(14), y + h };
                        DrawTextW(memDC, g_mainItems[i].value, -1, &rVal, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    }
                    y += h;
                }
            }

            SelectObject(memDC, oldFont);
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

// Right-Click Context Menu & Control Panel (Option B: Acrylic Modern Menu)
static void ShowContextMenu(HWND hWnd) {
    if (g_bModalLoop) return;
    g_hParentAppWnd = hWnd;

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
        if (fnGetDpi && hWnd) {
            g_curDpi = fnGetDpi(hWnd);
        }
    }
    if (g_curDpi == 0) g_curDpi = 96;

    static WCHAR szHeaderTitle[64];
    static WCHAR szHeaderSub[64];
    static WCHAR szBatVal[32];
    static WCHAR szDpiVal[32];
    static WCHAR szPollInfoVal[32];
    static WCHAR szPollSetVal[32];
    static WCHAR szSleepSetVal[32];
    static WCHAR szAutoRunVal[32];

    StringCchCopyW(szHeaderTitle, 64, g_detectedModel);
    if (g_deviceConnected) {
        if (g_isWiredMode) {
            StringCchCopyW(szHeaderSub, 64, L"● USB 有线模式  ·  已连接");
        } else {
            StringCchCopyW(szHeaderSub, 64, L"● 2.4G 无线模式  ·  已连接");
        }
        if (g_isCharging) {
            StringCchPrintfW(szBatVal, 32, L"%d%% (充电中 ⚡)", g_battery);
        } else {
            StringCchPrintfW(szBatVal, 32, L"%d%%", g_battery);
        }
        StringCchPrintfW(szDpiVal, 32, L"%d (第 %d 档)", g_dpiX, g_dpiLevel);
        StringCchPrintfW(szPollInfoVal, 32, L"%d Hz", g_currentPollingHz);
        StringCchPrintfW(szPollSetVal, 32, L"%d Hz  ›", g_currentPollingHz);
    } else {
        StringCchCopyW(szHeaderSub, 64, L"● 设备休眠 / 未连接");
        StringCchCopyW(szBatVal, 32, L"--");
        StringCchCopyW(szDpiVal, 32, L"--");
        StringCchCopyW(szPollInfoVal, 32, L"--");
        StringCchCopyW(szPollSetVal, 32, L"--  ›");
    }
    StringCchPrintfW(szSleepSetVal, 32, L"%d 分钟  ›", g_currentSleepMin);
    StringCchCopyW(szAutoRunVal, 32, IsAutoRunEnabled() ? L"✓ 已开启" : L"未开启");

    InitSubData(g_currentPollingHz, g_currentSleepMin);

    g_mainItems[0]  = { IDM_HEADER,    szHeaderTitle,      szHeaderSub,   false, true,  0, false, false };
    g_mainItems[1]  = { 0,             NULL,               NULL,          true,  false, 0, false, false };
    g_mainItems[2]  = { IDM_BATTERY,   L"⚡  电池电量",    szBatVal,      false, false, 0, false, false };
    g_mainItems[3]  = { IDM_DPI,       L"🎯  当前 DPI",    szDpiVal,      false, false, 0, false, false };
    g_mainItems[4]  = { IDM_POLL_INFO, L"📡  当前回报率",  szPollInfoVal, false, false, 0, false, false };
    g_mainItems[5]  = { 0,             NULL,               NULL,          true,  false, 0, false, false };
    g_mainItems[6]  = { 0,             L"⚙️  回报率设置",  szPollSetVal,  false, false, 1, !g_deviceConnected, true };
    g_mainItems[7]  = { 0,             L"⏱️  休眠时间",    szSleepSetVal, false, false, 2, !g_deviceConnected, true };
    g_mainItems[8]  = { 0,             NULL,               NULL,          true,  false, 0, false, false };
    g_mainItems[9]  = { IDM_AUTORUN,   L"🚀  开机自启动",  szAutoRunVal,  false, false, 0, false, true };
    g_mainItems[10] = { IDM_RECONNECT, L"🔄  重新连接外设", L"",           false, false, 0, false, true };
    g_mainItems[11] = { 0,             NULL,               NULL,          true,  false, 0, false, false };
    g_mainItems[12] = { IDM_EXIT,      L"✕  退出程序",     L"",           false, false, 0, false, true };

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);
    static bool s_mainRegistered = false;
    if (!s_mainRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = AcrylicMainWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"RapooAcrylicMainClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.style = CS_DROPSHADOW;
        RegisterClassExW(&wc);
        s_mainRegistered = true;
    }

    int menuW = S(236);
    int menuH = GetItemY(MAIN_ITEM_COUNT) + S(8);

    int posX = pt.x - menuW;
    int posY = pt.y - menuH;
    if (posX < mi.rcWork.left) posX = pt.x + S(4);
    if (posY < mi.rcWork.top) posY = pt.y + S(4);
    if (posX + menuW > mi.rcWork.right) posX = mi.rcWork.right - menuW - S(4);
    if (posY + menuH > mi.rcWork.bottom) posY = mi.rcWork.bottom - menuH - S(4);

    g_hAcrylicMenu = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"RapooAcrylicMainClass",
        L"RapooAcrylicMenu",
        WS_POPUP,
        posX, posY, menuW, menuH,
        hWnd, NULL, hInst, NULL
    );

    ApplyAcrylic(g_hAcrylicMenu, g_curDark);
    ShowWindow(g_hAcrylicMenu, SW_SHOW);
    UpdateWindow(g_hAcrylicMenu);
    SetForegroundWindow(g_hAcrylicMenu);

    g_hMenuMouseHook = SetWindowsHookExW(WH_MOUSE_LL, MenuMouseHookProc, GetModuleHandleW(NULL), 0);

    g_bModalLoop = true;
    MSG msg;
    while (g_bModalLoop && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || 
            msg.message == WM_NCLBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN) {
            POINT curPt = msg.pt;
            RECT rM = {0}, rS = {0};
            if (g_hAcrylicMenu && IsWindow(g_hAcrylicMenu)) GetWindowRect(g_hAcrylicMenu, &rM);
            if (g_hAcrylicSubMenu && IsWindow(g_hAcrylicSubMenu)) GetWindowRect(g_hAcrylicSubMenu, &rS);
            bool inM = (g_hAcrylicMenu && IsWindow(g_hAcrylicMenu)) && PtInRect(&rM, curPt);
            bool inS = (g_hAcrylicSubMenu && IsWindow(g_hAcrylicSubMenu)) && PtInRect(&rS, curPt);
            if (!inM && !inS) {
                DismissAllMenus();
                break;
            }
        } else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            DismissAllMenus();
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hMenuMouseHook) {
        UnhookWindowsHookEx(g_hMenuMouseHook);
        g_hMenuMouseHook = NULL;
    }
    DismissAllMenus();
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
                ShowOsdNotification(g_dpiLevel, g_dpiX, g_dpiY, g_battery, g_currentPollingHz);
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
            ShowOsdNotification(level, dpix, g_dpiY, g_battery, g_currentPollingHz);
            UpdateTrayTooltip();
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            return 0;
        }
        case WM_APP_BAT_UPDATE: {
            int bat = (int)wParam;
            UpdateTrayIcon(bat);
            return 0;
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED: {
            if (fnFlushMenuThemes) {
                fnFlushMenuThemes();
            }
            UpdateTrayIcon(g_battery);
            return 0;
        }
        case WM_DEVICECHANGE: {
            if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
                if (g_hDevChangeEvent) {
                    SetEvent(g_hDevChangeEvent);
                }
            }
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

    InitThemeSupport();

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
    if (fnAllowDarkModeForWindow) {
        fnAllowDarkModeForWindow(g_hMainWnd, TRUE);
    }
    SetWindowTheme(g_hMainWnd, L"DarkMode_Explorer", NULL);

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
        0, 0, 288, 92,
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
    g_hDevChangeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_currentSleepMin = LoadRegistryDword(L"SleepTimeout", 10);
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
    if (g_hDevChangeEvent) {
        CloseHandle(g_hDevChangeEvent);
        g_hDevChangeEvent = NULL;
    }

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
