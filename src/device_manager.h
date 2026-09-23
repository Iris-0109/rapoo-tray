#pragma once
#include <windows.h>
#include "rapoo_protocol.h"

namespace Device {

struct State {
    bool isConnected = false;
    bool isWired = false;
    bool isCharging = false;
    int battery = 100;
    int dpiLevel = 1;
    int dpiX = 800;
    int dpiY = 800;
    int pollingHz = 1000;
    int sleepMinutes = 10;
    WCHAR modelName[64] = {0};
};

typedef void (*StateCallback)(const State& state, DWORD changeMask);

constexpr DWORD CHANGE_CONNECTED = 0x01;
constexpr DWORD CHANGE_BATTERY   = 0x02;
constexpr DWORD CHANGE_DPI       = 0x04;
constexpr DWORD CHANGE_POLLING   = 0x08;
constexpr DWORD CHANGE_SLEEP     = 0x10;

bool Start(HWND hNotifyWnd, StateCallback callback);
void Stop();

void NotifyDeviceChange();

bool SetPollingRate(int hz);
bool SetSleepTimeout(int minutes);
bool RefreshPollingRate();

State GetCurrentState();

} // namespace Device
