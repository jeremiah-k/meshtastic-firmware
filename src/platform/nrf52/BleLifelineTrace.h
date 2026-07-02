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
    PowerStateEnter,
    PowerStateExit,
    PowerSerialEnter,
    PowerSerialExit,
    PowerShutdownEnter,
    BleEnableRequested,
    BleEnableAlreadyActive,
    BleDisableRequested,
    BleDisableAlreadyInactive,
    BleSetupCalled,
    BleStartDisabledCalled,
    BleShutdownCalled,
    BleShutdownAlreadyInactive,
    BleShutdownDeferredRequested,
    BleShutdownDeferredRun,
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

inline const char *eventName(uint8_t event)
{
    switch (static_cast<Event>(event)) {
    case Event::PowerOnEnter:
        return "PowerOnEnter";
    case Event::PowerOnExit:
        return "PowerOnExit";
    case Event::PowerDarkEnter:
        return "PowerDarkEnter";
    case Event::PowerDarkExit:
        return "PowerDarkExit";
    case Event::PowerStateEnter:
        return "PowerStateEnter";
    case Event::PowerStateExit:
        return "PowerStateExit";
    case Event::PowerSerialEnter:
        return "PowerSerialEnter";
    case Event::PowerSerialExit:
        return "PowerSerialExit";
    case Event::PowerShutdownEnter:
        return "PowerShutdownEnter";
    case Event::BleEnableRequested:
        return "BleEnableRequested";
    case Event::BleEnableAlreadyActive:
        return "BleEnableAlreadyActive";
    case Event::BleDisableRequested:
        return "BleDisableRequested";
    case Event::BleDisableAlreadyInactive:
        return "BleDisableAlreadyInactive";
    case Event::BleSetupCalled:
        return "BleSetupCalled";
    case Event::BleStartDisabledCalled:
        return "BleStartDisabledCalled";
    case Event::BleShutdownCalled:
        return "BleShutdownCalled";
    case Event::BleShutdownAlreadyInactive:
        return "BleShutdownAlreadyInactive";
    case Event::BleShutdownDeferredRequested:
        return "BleShutdownDeferredRequested";
    case Event::BleShutdownDeferredRun:
        return "BleShutdownDeferredRun";
    case Event::BleResumeAdvertisingCalled:
        return "BleResumeAdvertisingCalled";
    case Event::BleAdvertisingStart:
        return "BleAdvertisingStart";
    case Event::BleAdvertisingStop:
        return "BleAdvertisingStop";
    case Event::BleConnect:
        return "BleConnect";
    case Event::BleConnectionSecured:
        return "BleConnectionSecured";
    case Event::BleDisconnect:
        return "BleDisconnect";
    case Event::BlePairingPasskey:
        return "BlePairingPasskey";
    case Event::BleUnwantedPairing:
        return "BleUnwantedPairing";
    case Event::BleToRadioWrite:
        return "BleToRadioWrite";
    case Event::MeshPacketForPhone:
        return "MeshPacketForPhone";
    }

    return "Unknown";
}

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
        LOG_INFO("NRF52 BLE lifeline: t=%u event=%s arg1=%u arg2=%u", entry.uptime_ms, eventName(entry.event), entry.arg1,
                 entry.arg2);
    }
}

inline void dumpToLogOnce()
{
    static bool dumped = false;
    if (dumped) {
        return;
    }

    dumped = true;
    dumpToLog();
}
#else
inline void trace(Event, uint8_t = 0, uint16_t = 0) {}
inline void dumpToLog() {}
inline void dumpToLogOnce() {}
#endif

} // namespace nrf52::ble_lifeline
