#pragma once

#include <climits>
#include <cstdint>
#include <mutex>

namespace meshtastic
{
namespace bluetooth
{

/** Desired BLE connection profile; newer requests replace older pending requests. */
enum class ConnectionParamsMode : uint8_t { NONE, HIGH_THROUGHPUT, LOW_POWER };

/** One controller request admitted to the single ESP32 GAP connection-parameter procedure lane. */
struct ConnectionParamsRequest {
    uint16_t connHandle;
    ConnectionParamsMode mode;
    uint32_t generation;
};

/** Serializes ESP32 GAP updates while accepting peer-selected parameters that already satisfy the desired profile. */
class ConnectionParamsUpdateController
{
  public:
    static constexpr uint16_t NO_CONNECTION = UINT16_MAX;
    static constexpr int32_t NO_WAKE_DELAY_MS = INT32_MAX;
    static constexpr int32_t SUBMISSION_RETRY_MS = 250;

    static constexpr uint16_t HIGH_THROUGHPUT_MIN_INTERVAL = 6;
    static constexpr uint16_t HIGH_THROUGHPUT_MAX_INTERVAL = 12;
    static constexpr uint16_t HIGH_THROUGHPUT_LATENCY = 0;
    static constexpr uint16_t LOW_POWER_MIN_INTERVAL = 24;
    static constexpr uint16_t LOW_POWER_MAX_INTERVAL = 40;
    static constexpr uint16_t LOW_POWER_LATENCY = 2;
    static constexpr uint16_t SUPERVISION_TIMEOUT = 600;

    static constexpr uint32_t NIMBLE_UPDATE_TIMEOUT_MS = 40000;
    // Recover after NimBLE's own deadline, plus margin for its timeout callback and disconnect cleanup.
    static constexpr uint32_t UPDATE_LANE_TIMEOUT_MS = NIMBLE_UPDATE_TIMEOUT_MS + 1000;
    static_assert(UPDATE_LANE_TIMEOUT_MS < INT32_MAX);

    /** Return the token callers use to reject work from a retired BLE session. */
    uint32_t currentGeneration() const
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        return generation;
    }

    /** Start a fresh controller session for [connHandle]. */
    void onConnected(uint16_t connHandle)
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        generation++;
        connectionHandle = connHandle;
        currentInterval = 0;
        requestedMode = ConnectionParamsMode::NONE;
        desiredMode = ConnectionParamsMode::NONE;
        updateStartedAtMs = 0;
        updateInProgress = false;
    }

    /** Queue or replace the desired mode only if the caller still belongs to the active session. */
    bool request(uint32_t expectedGeneration, ConnectionParamsMode mode)
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (mode == ConnectionParamsMode::NONE || connectionHandle == NO_CONNECTION || generation != expectedGeneration) {
            return false;
        }

        if (!updateInProgress && isSatisfied(mode, currentInterval)) {
            if (desiredMode == mode) {
                desiredMode = ConnectionParamsMode::NONE;
            }
            return false;
        }

        desiredMode = mode;
        return true;
    }

    /** Admit and submit one request while session retirement is excluded by the controller lock. */
    template <typename Submitter> int32_t servicePending(uint32_t nowMs, Submitter &&submitter)
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (connectionHandle == NO_CONNECTION) {
            return NO_WAKE_DELAY_MS;
        }

        if (updateInProgress) {
            const uint32_t elapsedMs = nowMs - updateStartedAtMs;
            if (elapsedMs < UPDATE_LANE_TIMEOUT_MS) {
                return static_cast<int32_t>(UPDATE_LANE_TIMEOUT_MS - elapsedMs);
            }
            requestedMode = ConnectionParamsMode::NONE;
            updateInProgress = false;
        }

        if (desiredMode == ConnectionParamsMode::NONE) {
            return NO_WAKE_DELAY_MS;
        }
        if (isSatisfied(desiredMode, currentInterval)) {
            desiredMode = ConnectionParamsMode::NONE;
            return NO_WAKE_DELAY_MS;
        }

        requestedMode = desiredMode;
        updateInProgress = true;
        updateStartedAtMs = nowMs;
        const ConnectionParamsRequest request{connectionHandle, requestedMode, generation};

        // NimBLE submission is nonblocking; retain the lock so reset/reconnect cannot retarget this handle mid-call.
        if (submitter(request)) {
            return static_cast<int32_t>(UPDATE_LANE_TIMEOUT_MS);
        }

        requestedMode = ConnectionParamsMode::NONE;
        updateInProgress = false;
        return SUBMISSION_RETRY_MS;
    }

    /** Record successful link parameters; ambiguous failures cannot prove that our request completed. */
    void recordUpdate(uint16_t connHandle, uint16_t interval, uint8_t status)
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (connHandle != connectionHandle || status != 0) {
            return;
        }

        currentInterval = interval;
        if (!updateInProgress || !isSatisfied(requestedMode, currentInterval)) {
            return;
        }

        // Releasing is safe only when it cannot immediately submit a different procedure from an uncorrelated callback.
        if (desiredMode != requestedMode && !isSatisfied(desiredMode, currentInterval)) {
            return;
        }

        desiredMode = ConnectionParamsMode::NONE;
        requestedMode = ConnectionParamsMode::NONE;
        updateInProgress = false;
    }

    /** Retire the active session and discard its queued and in-flight state. */
    void reset()
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        generation++;
        connectionHandle = NO_CONNECTION;
        currentInterval = 0;
        desiredMode = ConnectionParamsMode::NONE;
        requestedMode = ConnectionParamsMode::NONE;
        updateStartedAtMs = 0;
        updateInProgress = false;
    }

  private:
    /** Return whether [interval] already satisfies [mode]. */
    static bool isSatisfied(ConnectionParamsMode mode, uint16_t interval)
    {
        if (interval == 0) {
            return false;
        }
        switch (mode) {
        case ConnectionParamsMode::HIGH_THROUGHPUT:
            return interval <= HIGH_THROUGHPUT_MAX_INTERVAL;
        case ConnectionParamsMode::LOW_POWER:
            return interval >= LOW_POWER_MIN_INTERVAL;
        case ConnectionParamsMode::NONE:
            return true;
        }
        return false;
    }

    mutable std::mutex stateMutex;
    uint32_t generation = 0;
    uint32_t updateStartedAtMs = 0;
    uint16_t connectionHandle = NO_CONNECTION;
    uint16_t currentInterval = 0;
    ConnectionParamsMode desiredMode = ConnectionParamsMode::NONE;
    ConnectionParamsMode requestedMode = ConnectionParamsMode::NONE;
    bool updateInProgress = false;
};

} // namespace bluetooth
} // namespace meshtastic
