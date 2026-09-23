#pragma once
#include <windows.h>
#include <shellapi.h>
#include "device_manager.h"

namespace Tray {

void InitTheme();
void ShowMenu(HWND hWndOwner);
void DismissAllMenus();

HICON CreateBatteryIcon(int battery, bool isCharging, bool isConnected, int size, bool isDark);
void UpdateTooltip(NOTIFYICONDATAW& nid, const Device::State& state);

bool IsAutoRunEnabled();
void ToggleAutoRun();

} // namespace Tray
