#pragma once
#include <Arduino.h>

namespace BackendClient {
  void begin();
  void handle();
  void resetRuntimeState();
  void printIdentity();

  void markFactoryResetPending();
  bool isFactoryResetPending();

  bool isPaired();
  bool hasPairingCode();
  String getPairingCode();
  String getPairingExpiresAt();
  unsigned long getLastPairingRefreshAtMs();
}