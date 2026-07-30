#pragma once

#include <atomic>
#include <cstdint>

namespace meshtastic::bluetooth
{

/** Prevents new cross-task BLE resource access while teardown drains existing users. */
class BleResourceGate
{
  public:
    bool tryAcquire()
    {
        if (draining.load()) {
            return false;
        }

        activeUsers.fetch_add(1);
        if (!draining.load()) {
            return true;
        }

        activeUsers.fetch_sub(1);
        return false;
    }

    void release() { activeUsers.fetch_sub(1); }

    void beginDrain() { draining = true; }
    void reopen() { draining = false; }

    bool isDraining() const { return draining.load(); }
    bool hasActiveUsers() const { return activeUsers.load() != 0; }

  private:
    std::atomic<uint32_t> activeUsers{0};
    std::atomic<bool> draining{false};
};

/** Scoped admission to BLE resources guarded by a BleResourceGate. */
class BleResourceLease
{
  public:
    explicit BleResourceLease(BleResourceGate &gate) : gate(gate), acquired(gate.tryAcquire()) {}
    ~BleResourceLease()
    {
        if (acquired) {
            gate.release();
        }
    }

    BleResourceLease(const BleResourceLease &) = delete;
    BleResourceLease &operator=(const BleResourceLease &) = delete;
    BleResourceLease(BleResourceLease &&) = delete;
    BleResourceLease &operator=(BleResourceLease &&) = delete;

    explicit operator bool() const { return acquired; }

  private:
    BleResourceGate &gate;
    bool acquired;
};

} // namespace meshtastic::bluetooth
