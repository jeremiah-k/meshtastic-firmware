#pragma once
#include <cstddef>
#include <cstdint>

class Throttle
{
  public:
    static bool execute(uint32_t *lastExecutionMs, uint32_t minumumIntervalMs, void (*func)(void), void (*onDefer)(void) = NULL);
    static bool isWithinTimespanMs(uint32_t lastExecutionMs, uint32_t intervalMs);
    static constexpr uint32_t remainingTimespanMs(uint32_t lastExecutionMs, uint32_t intervalMs, uint32_t nowMs)
    {
        const uint32_t elapsedMs = nowMs - lastExecutionMs;
        return elapsedMs < intervalMs ? intervalMs - elapsedMs : 0;
    }
};
