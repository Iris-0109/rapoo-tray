#pragma once
#include <windows.h>
#include <cstdint>

namespace Rapoo {

// Standard Rapoo HID Report IDs (aligned with ClickSync)
constexpr BYTE REPORT_ID_CONTROL = 0x06; // Output report for sending commands
constexpr BYTE REPORT_ID_STATUS  = 0x07; // Input report for passive status broadcasts
constexpr BYTE REPORT_ID_FEATURE = 0x08; // Feature report for reading register responses

// Hardware Memory Banks
constexpr BYTE BANK_COMM   = 0x00; // Communication Protocol
constexpr BYTE BANK_BUTTON = 0x06; // Button mappings
constexpr BYTE BANK_SYSTEM = 0x08; // System settings (DPI, Polling, Sleep, LOD)

// System Register Addresses
constexpr BYTE ADDR_POLLING_HZ    = 0x80;
constexpr BYTE ADDR_KEY_SCAN_RATE = 0x81;
constexpr BYTE ADDR_LOD_HEIGHT    = 0x84;
constexpr BYTE ADDR_MOTION_SYNC   = 0x85;
constexpr BYTE ADDR_SLEEP_TIMEOUT = 0xC2; // 2..120 minutes
constexpr BYTE ADDR_LINEAR_RIPPLE = 0xC3;
constexpr BYTE ADDR_SENSOR_ANGLE  = 0xC4;
constexpr BYTE ADDR_GLASS_MODE    = 0xC5;

struct DeviceStatus {
    int dpiLevel = 1;
    int dpiX = 800;
    int dpiY = 800;
    int battery = 100;
    bool isCharging = false;
    bool isWired = false; // device marker 0x10=wired, 0x20=2.4G dongle
};

// Packet Builders
DWORD BuildUnlockPacket(BYTE* outBuf, DWORD bufSize);
DWORD BuildWriteCommand(BYTE bank, BYTE addr, const BYTE* pData, BYTE dataLen, BYTE* outBuf, DWORD bufSize);
DWORD BuildReadCommand(BYTE bank, BYTE addr, BYTE readLen, BYTE* outBuf, DWORD bufSize);

// Parsers
bool ParseStatusReport(const BYTE* buf, DWORD bytesRead, DeviceStatus& outStatus, int cachedBattery);
bool ParseFeatureRead(const BYTE* buf, DWORD bytesRead, BYTE expectedLen, BYTE* outData);

// Polling Rate Converters
int CodeToPollingHz(BYTE code);
BYTE PollingHzToCode(int hz);

// Sleep Timeout Clamping (2 to 120 minutes)
inline int ClampSleepMinutes(int minutes) {
    if (minutes < 2) return 2;
    if (minutes > 120) return 120;
    return minutes;
}

} // namespace Rapoo
