#pragma once

#include <cstdint>
#include <mutex>

namespace meshtastic
{
namespace bluetooth
{

/** Desired BLE connection profile, ordered only for diagnostics; newer requests replace older ones. */
enum class ConnectionParamsMode : uint8_t { NONE, INITIAL_HIGH_THROUGHPUT, HIGH_THROUGHPUT, LOW_POWER };

/** One controller request admitted to the single ESP32 GAP connection-parameter procedure lane. */
struct ConnectionParamsRequest {
    uint16_t connHandle;
    ConnectionParamsMode mode;
    uint32_t generation;
};

/** Serializes ESP32 GAP updates and retains the newest mode until the active procedure completes. */
class ConnectionParamsUpdateController
{
  public:
    static constexpr uint16_t NO_CONNECTION = UINT16_MAX;

    uint32_t currentGeneration() const
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        return generation;
    }

    void onConnected(uint16_t connHandle)
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        generation++;
        connectionHandle = connHandle;
        requestedMode = ConnectionParamsMode::NONE;
        updateInProgress = false;
        desiredMode = ConnectionParamsMode::INITIAL_HIGH_THROUGHPUT;
    }

    /** Queue or replace the desired mode only if the caller still belongs to the active session. */
    bool request(uint32_t expectedGeneration, ConnectionParamsMode mode)
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (mode == ConnectionParamsMode::NONE || connectionHandle == NO_CONNECTION || generation != expectedGeneration) {
            return false;
        }
        desiredMode = mode;
        return true;
    }

    /** Admit the next request for submission by the main task. */
    bool beginNext(ConnectionParamsRequest &request)
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (updateInProgress || connectionHandle == NO_CONNECTION || desiredMode == ConnectionParamsMode::NONE) {
            return false;
        }

        updateInProgress = true;
        requestedMode = desiredMode;
        request = {connectionHandle, requestedMode, generation};
        return true;
    }

    /** Keep a synchronously rejected request pending for a later retry, unless its session has already retired. */
    bool onSubmissionRejected(const ConnectionParamsRequest &request)
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (request.generation != generation || request.connHandle != connectionHandle || request.mode != requestedMode) {
            return false;
        }
        updateInProgress = false;
        return desiredMode != ConnectionParamsMode::NONE;
    }

    /** Complete the active procedure and report whether a newer desired mode remains pending. */
    bool onUpdateComplete(uint16_t connHandle)
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (connHandle != connectionHandle || !updateInProgress) {
            return false;
        }

        const ConnectionParamsMode completed = requestedMode;
        if (desiredMode == completed) {
            desiredMode = ConnectionParamsMode::NONE;
        }
        updateInProgress = false;
        return desiredMode != ConnectionParamsMode::NONE;
    }

    void reset()
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        generation++;
        connectionHandle = NO_CONNECTION;
        desiredMode = ConnectionParamsMode::NONE;
        requestedMode = ConnectionParamsMode::NONE;
        updateInProgress = false;
    }

  private:
    mutable std::mutex stateMutex;
    uint32_t generation = 0;
    uint16_t connectionHandle = NO_CONNECTION;
    ConnectionParamsMode desiredMode = ConnectionParamsMode::NONE;
    ConnectionParamsMode requestedMode = ConnectionParamsMode::NONE;
    bool updateInProgress = false;
};

} // namespace bluetooth
} // namespace meshtastic
