#pragma once

#include "mesh/Throttle.h"

#include <cstdint>

namespace meshtastic::bluetooth
{

constexpr bool shouldRebootClassicEsp32AfterDisconnect(bool authenticatedSession)
{
    return authenticatedSession;
}

/**
 * Main-task timing state for a deferred BLE advertising restart.
 *
 * This class is not internally synchronized and must remain owned by one task.
 */
class AdvertisingRestartController
{
  public:
    void arm(uint32_t nowMs, uint32_t delayMs)
    {
        startedAtMs = nowMs;
        waitMs = delayMs;
        armed = true;
    }

    void armIfNeeded(uint32_t nowMs, uint32_t delayMs)
    {
        if (!armed) {
            arm(nowMs, delayMs);
        }
    }

    uint32_t remainingMs(uint32_t nowMs) const { return armed ? Throttle::remainingTimespanMs(startedAtMs, waitMs, nowMs) : 0; }

    void reset()
    {
        startedAtMs = 0;
        waitMs = 0;
        armed = false;
    }

  private:
    uint32_t startedAtMs = 0;
    uint32_t waitMs = 0;
    bool armed = false;
};

} // namespace meshtastic::bluetooth
