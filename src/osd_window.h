#pragma once
#include <windows.h>

namespace Osd {

bool Init(HINSTANCE hInstance);
void Cleanup();

// Shows custom 2-line or 3-line notification with auto-sizing geometry
void Show(const WCHAR* line1, const WCHAR* line2, const WCHAR* line3 = nullptr);

// Formatted notification when DPI changes
void ShowDpiUpdate(int level, int dpix, int dpiy, int battery, int pollingHz, bool isCharging, bool isWired, const WCHAR* modelName);

HWND GetWindowHandle();

} // namespace Osd
