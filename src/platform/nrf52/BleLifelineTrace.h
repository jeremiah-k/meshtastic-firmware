#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(ARCH_NRF52) && defined(USERPREFS_NRF52_BLE_LIFELINE) && USERPREFS_NRF52_BLE_LIFELINE
#include "configuration.h"
#include <Arduino.h>
#include <atomic>
#endif

namespace nrf52::ble_lifeline
{

enum class Event : uint8_t {
    PowerOnEnter = 1,
    PowerOnExit,
    PowerDarkEnter,
    PowerDarkExit,
    PowerEnter,
    PowerExit,
    PowerSerialEnter,
    PowerSerialExit,
    PowerShutdownEnter,
    BleEnableRequested,
    BleEnableAlreadyActive,
    BleDisableRequested,
    BleDisableAlreadyInactive,
    BleSetupCalled,
    BleShutdownCalled,
    BleResumeAdvertisingCalled,
    BleAdvertisingStart,
    BleAdvertisingStop,
    BleConnect,
    BleConnectionSecured,
    BleDisconnect,
    BlePairingPasskey,
    BleUnwantedPairing,
    BleToRadioWrite,
    MeshPacketForPhone,
};

struct LifelineEntry {
    uint32_t uptime_ms;
    uint8_t event;
    uint8_t arg1;
    uint16_t arg2;
    uint8_t reserved[8];
};

static_assert(sizeof(LifelineEntry) == 16, "BLE lifeline entries must stay compact");

#if defined(ARCH_NRF52) && defined(USERPREFS_NRF52_BLE_LIFELINE) && USERPREFS_NRF52_BLE_LIFELINE
inline constexpr size_t kEntryCount = 64;
inline std::atomic<uint32_t> head{0};
inline LifelineEntry entries[kEntryCount];

inline void trace(Event event, uint8_t arg1 = 0, uint16_t arg2 = 0)
{
    const uint32_t index = head.fetch_add(1, std::memory_order_relaxed) % kEntryCount;
    entries[index] = LifelineEntry{millis(), static_cast<uint8_t>(event), arg1, arg2, {}};
}

inline void dumpToLog()
{
    const uint32_t total = head.load(std::memory_order_relaxed);
    const uint32_t count = total < kEntryCount ? total : kEntryCount;
    const uint32_t start = total > kEntryCount ? total - kEntryCount : 0;

    for (uint32_t i = 0; i < count; ++i) {
        const LifelineEntry &entry = entries[(start + i) % kEntryCount];
        LOG_INFO("NRF52 BLE lifeline: t=%u event=%u arg1=%u arg2=%u", entry.uptime_ms, entry.event, entry.arg1, entry.arg2);
    }
}
#else
inline void trace(Event, uint8_t = 0, uint16_t = 0) {}
inline void dumpToLog() {}
#endif

} // namespace nrf52::ble_lifeline
