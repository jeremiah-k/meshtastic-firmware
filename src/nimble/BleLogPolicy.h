#pragma once

#include <cstddef>
#include <cstdint>

namespace meshtastic::bluetooth
{

/** Pure admission rules for best-effort BLE debug-log notifications. */
class BleLogPolicy
{
  public:
    static constexpr size_t MAX_VALUE_LENGTH = 512;
    static constexpr uint16_t ATT_VALUE_OVERHEAD = 3;

    static constexpr bool fitsPeerMtu(size_t length, uint16_t peerMtu)
    {
        return peerMtu > ATT_VALUE_OVERHEAD && length <= MAX_VALUE_LENGTH &&
               length <= static_cast<size_t>(peerMtu - ATT_VALUE_OVERHEAD);
    }

    static constexpr bool intervalElapsed(bool hasTimestamp, uint32_t timestampMs, uint32_t intervalMs, uint32_t nowMs)
    {
        // Inject nowMs for deterministic tests; unsigned subtraction remains valid across millis() rollover.
        return !hasTimestamp || nowMs - timestampMs >= intervalMs;
    }
};

} // namespace meshtastic::bluetooth
