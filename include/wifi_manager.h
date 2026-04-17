#pragma once

namespace WiFiManagerApp {
  void begin();
  bool connectToSavedWiFi();
  void startSetupPortal();
  void handle();
  bool isInSetupMode();
  bool isConnected();
}
