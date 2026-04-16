#pragma once
#include <Arduino.h>

namespace BackendClient {
  void begin();
  void handle();
  void resetRuntimeState();
  void printIdentity();

  bool isPaired();
  bool hasPairingCode();
  String getPairingCode();
  String getPairingExpiresAt();
  unsigned long getLastPairingRefreshAtMs();
}