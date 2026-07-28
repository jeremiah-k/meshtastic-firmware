#pragma once

#include "mesh/Throttle.h"

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
};

/** Serializes ESP32 GAP updates while accepting peer-selected parameters that already satisfy the desired profile. */
class ConnectionParamsUpdateController
{
  public:
    static constexpr uint16_t NO_CONNECTION = UINT16_MAX;
    static constexpr int32_t NO_WAKE_DELAY_MS = INT32_MAX;
    static constexpr int32_t SUBMISSION_RETRY_MS = 250;
    // Coalesce back-to-back config phases before entering the optional steady-state profile.
    static constexpr uint32_t LOW_POWER_SETTLE_MS = 1000;

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

    /** Start a fresh controller session for [connHandle] and return its admission token. */
    uint32_t onConnected(uint16_t connHandle)
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        resetState(connHandle);
        return generation;
    }

    /** Queue or replace the desired mode only if the caller still belongs to the active session. */
    bool request(uint32_t expectedGeneration, ConnectionParamsMode mode, uint32_t nowMs)
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (mode == ConnectionParamsMode::NONE || connectionHandle == NO_CONNECTION || generation != expectedGeneration) {
            return false;
        }

        if (!updateInProgress && isSatisfied(mode, currentInterval)) {
            // The newest request wins even when it needs no controller procedure.
            desiredMode = ConnectionParamsMode::NONE;
            requestedMode = ConnectionParamsMode::NONE;
            stateStartedAtMs = 0;
            return false;
        }

        desiredMode = mode;
        if (!updateInProgress && mode == ConnectionParamsMode::LOW_POWER) {
            // A new LOW_POWER request gets its full quiet period, which also supersedes a shorter submission backoff.
            requestedMode = ConnectionParamsMode::NONE;
            stateStartedAtMs = nowMs;
        }
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
            const uint32_t remainingMs = Throttle::remainingTimespanMs(stateStartedAtMs, UPDATE_LANE_TIMEOUT_MS, nowMs);
            if (remainingMs != 0) {
                return static_cast<int32_t>(remainingMs);
            }
            const ConnectionParamsMode expiredMode = requestedMode;
            requestedMode = ConnectionParamsMode::NONE;
            updateInProgress = false;
            if (desiredMode == ConnectionParamsMode::LOW_POWER) {
                stateStartedAtMs = expiredMode == ConnectionParamsMode::LOW_POWER ? nowMs - LOW_POWER_SETTLE_MS : nowMs;
            } else {
                stateStartedAtMs = 0;
            }
        }

        if (desiredMode == ConnectionParamsMode::NONE) {
            return NO_WAKE_DELAY_MS;
        }
        if (isSatisfied(desiredMode, currentInterval)) {
            desiredMode = ConnectionParamsMode::NONE;
            requestedMode = ConnectionParamsMode::NONE;
            stateStartedAtMs = 0;
            return NO_WAKE_DELAY_MS;
        }

        // A non-active requestedMode marks submission backoff; otherwise LOW_POWER observes its config quiet period.
        uint32_t waitMs = 0;
        if (requestedMode != ConnectionParamsMode::NONE) {
            waitMs = SUBMISSION_RETRY_MS;
        } else if (desiredMode == ConnectionParamsMode::LOW_POWER) {
            waitMs = LOW_POWER_SETTLE_MS;
        }
        if (waitMs != 0) {
            const uint32_t remainingMs = Throttle::remainingTimespanMs(stateStartedAtMs, waitMs, nowMs);
            if (remainingMs != 0) {
                return static_cast<int32_t>(remainingMs);
            }
        }

        requestedMode = desiredMode;
        updateInProgress = true;
        stateStartedAtMs = nowMs;
        const ConnectionParamsRequest request{connectionHandle, requestedMode};

        // NimBLE submission is nonblocking; retain the lock so reset/reconnect cannot retarget this handle mid-call.
        if (submitter(request)) {
            return static_cast<int32_t>(UPDATE_LANE_TIMEOUT_MS);
        }

        updateInProgress = false;
        stateStartedAtMs = nowMs;
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
        stateStartedAtMs = 0;
        updateInProgress = false;
    }

    /** Retire the active session and discard its queued and in-flight state. */
    void reset()
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        resetState();
    }

#ifdef PIO_UNIT_TESTING
    /** Invoke [beforeLock] immediately before acquiring the controller lock. */
    template <typename BeforeLock> void resetForTest(BeforeLock &&beforeLock)
    {
        beforeLock();
        std::lock_guard<std::mutex> guard(stateMutex);
        resetState();
    }
#endif

  private:
    void resetState(uint16_t newConnectionHandle = NO_CONNECTION)
    {
        generation++;
        connectionHandle = newConnectionHandle;
        currentInterval = 0;
        desiredMode = ConnectionParamsMode::NONE;
        requestedMode = ConnectionParamsMode::NONE;
        stateStartedAtMs = 0;
        updateInProgress = false;
    }

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
    // Times the active GAP lane, or the deferred LOW_POWER request when no update is active.
    uint32_t stateStartedAtMs = 0;
    uint16_t connectionHandle = NO_CONNECTION;
    uint16_t currentInterval = 0;
    ConnectionParamsMode desiredMode = ConnectionParamsMode::NONE;
    ConnectionParamsMode requestedMode = ConnectionParamsMode::NONE;
    bool updateInProgress = false;
};

} // namespace bluetooth
} // namespace meshtastic
