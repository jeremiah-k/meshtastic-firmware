#pragma once
#include "BluetoothCommon.h"

class BluetoothPhoneAPI;

class NimbleBluetooth : BluetoothApi
{
  public:
    void setup();
    void shutdown();
    void deinit();
    void clearBonds();
    bool isActive();
    bool isConnected();
    int getRssi();
    void sendLog(const uint8_t *logMessage, size_t length);
    bool startAdvertising();
    bool isDeInit = false;

  private:
    friend class BluetoothPhoneAPI;
    void setupInternal(bool isRetry);
    void setupService();
};

void setBluetoothEnable(bool enable);