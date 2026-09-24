#pragma once
#include <windows.h>
#include "device_manager.h"

namespace Alert {

void Init(HINSTANCE hInstance);
void Cleanup();
void CheckBattery(const Device::State& state);
void Show(const WCHAR* modelName, int battery, bool isCritical, bool isWired);
void Hide();

} // namespace Alert
