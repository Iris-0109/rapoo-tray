#include "rapoo_protocol.h"
#include <cstring>
#include <algorithm>

namespace Rapoo {

DWORD BuildUnlockPacket(BYTE* outBuf, DWORD bufSize) {
    if (!outBuf || bufSize < 33) return 0;
    std::memset(outBuf, 0, bufSize);
    outBuf[0] = REPORT_ID_CONTROL; // 0x06
    outBuf[1] = 0xA5;
    outBuf[2] = 0xA3;
    outBuf[3] = 0x00;
    outBuf[4] = 0x00;
    outBuf[5] = 0x00;
    outBuf[6] = 0x00;
    outBuf[7] = 0x00;
    outBuf[8] = 0x00;
    return 33;
}

DWORD BuildWriteCommand(BYTE bank, BYTE addr, const BYTE* pData, BYTE dataLen, BYTE* outBuf, DWORD bufSize) {
    if (!outBuf || bufSize < 33) return 0;
    std::memset(outBuf, 0, bufSize);
    outBuf[0] = REPORT_ID_CONTROL; // 0x06
    outBuf[1] = 0xA5;
    outBuf[2] = 0xA5;
    outBuf[3] = (BYTE)(dataLen & 0xFF);
    outBuf[4] = addr;
    outBuf[5] = bank;
    outBuf[6] = 0x00;
    outBuf[7] = 0x00;
    if (pData && dataLen > 0) {
        BYTE copyLen = (dataLen > 25) ? 25 : dataLen;
        std::memcpy(&outBuf[8], pData, copyLen);
    }
    return 33;
}

DWORD BuildReadCommand(BYTE bank, BYTE addr, BYTE readLen, BYTE* outBuf, DWORD bufSize) {
    if (!outBuf || bufSize < 33) return 0;
    std::memset(outBuf, 0, bufSize);
    outBuf[0] = REPORT_ID_CONTROL; // 0x06
    outBuf[1] = 0xA5;
    outBuf[2] = 0xA4;
    outBuf[3] = readLen;
    outBuf[4] = addr;
    outBuf[5] = bank;
    outBuf[6] = 0x00;
    outBuf[7] = 0x00;
    outBuf[8] = 0x00;
    return 33;
}

bool ParseStatusReport(const BYTE* buf, DWORD bytesRead, DeviceStatus& outStatus, int cachedBattery) {
    if (!buf || bytesRead < 9) return false;
    if (buf[0] != REPORT_ID_STATUS) return false;
    // Device marker: 0x20 (2.4G dongle), 0x10 (wired USB direct)
    if (buf[1] != 0x20 && buf[1] != 0x10) return false;
    outStatus.isWired = (buf[1] == 0x10);

    outStatus.dpiLevel = (int)buf[2] + 1;
    outStatus.dpiX = (int)buf[3] | ((int)buf[4] << 8);
    outStatus.dpiY = (int)buf[5] | ((int)buf[6] << 8);

    BYTE statusByte = buf[7];
    BYTE rawBat = buf[8];
    BYTE extraByte = (bytesRead > 9) ? buf[9] : 0;

    // Charging flag detection based on Rapoo status flags
    bool charging = ((statusByte & 0x02) != 0 || statusByte == 0x02 || statusByte == 0x03 ||
                     extraByte == 0x01 || extraByte == 0x02);
    outStatus.isCharging = charging;

    // Battery percentage filtering:
    // In Rapoo hardware, rawBat is an integer 0..100.
    // When the mouse is first plugged into USB/charging, rawBat may momentarily report 0 or 1
    // (transient handshake / ADC settle time). We protect the battery gauge by retaining
    // the previous valid reading until a real measurement (>= 2) arrives.
    if (rawBat >= 2 && rawBat <= 100) {
        outStatus.battery = rawBat;
    } else if (rawBat <= 1) {
        if (cachedBattery >= 2 && cachedBattery <= 100) {
            outStatus.battery = cachedBattery;
        } else {
            outStatus.battery = (rawBat == 0) ? 100 : rawBat;
        }
    } else {
        outStatus.battery = 100;
    }

    return true;
}

bool ParseFeatureRead(const BYTE* buf, DWORD bytesRead, BYTE expectedLen, BYTE* outData) {
    if (!buf || bytesRead < 5) return false;

    // Case 1: Windows HID driver stripped Report ID -> buf[0] is ACK (0x01)
    if (buf[0] == 0x01 && bytesRead >= (DWORD)(4 + expectedLen)) {
        if (outData) {
            std::memcpy(outData, &buf[4], expectedLen);
        }
        return true;
    }

    // Case 2: buf[0] is Report ID (0x08) and buf[1] is ACK (0x01)
    if (buf[0] == REPORT_ID_FEATURE && buf[1] == 0x01 && bytesRead >= (DWORD)(5 + expectedLen)) {
        if (outData) {
            std::memcpy(outData, &buf[5], expectedLen);
        }
        return true;
    }

    return false;
}

int CodeToPollingHz(BYTE code) {
    switch (code) {
        case 0x08: return 125;
        case 0x04: return 250;
        case 0x02: return 500;
        case 0x01: return 1000;
        case 0x84: return 2000;
        case 0x82: return 4000;
        case 0x81: return 8000;
        default:   return 1000;
    }
}

BYTE PollingHzToCode(int hz) {
    switch (hz) {
        case 125:  return 0x08;
        case 250:  return 0x04;
        case 500:  return 0x02;
        case 1000: return 0x01;
        case 2000: return 0x84;
        case 4000: return 0x82;
        case 8000: return 0x81;
        default:   return 0x01;
    }
}

} // namespace Rapoo
