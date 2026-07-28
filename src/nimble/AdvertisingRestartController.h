#pragma once

#include <cstdint>

namespace meshtastic::bluetooth
{

/** Main-task timing state for a deferred BLE advertising restart. */
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

    uint32_t remainingMs(uint32_t nowMs) const
    {
        if (!armed) {
            return 0;
        }
        // Unsigned subtraction intentionally preserves elapsed time across the millis() wraparound.
        const uint32_t elapsedMs = nowMs - startedAtMs;
        return elapsedMs >= waitMs ? 0 : waitMs - elapsedMs;
    }

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
