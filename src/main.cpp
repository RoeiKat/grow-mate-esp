#include <Arduino.h>
#include "backend_client.h"
#include "wifi_manager.h"
#include "config.h"

void setup() {
  Serial.begin(115200);
  delay(1000);

  initConfig();
  Serial.print("DEVICE SERIAL: ");
  Serial.println(DEVICE_SERIAL_NUMBER);

  WiFiManagerApp::begin();
  BackendClient::begin();

  bool connected = WiFiManagerApp::connectToSavedWiFi();
  if (!connected) {
    WiFiManagerApp::startSetupPortal();
  }
}

void loop() {
  WiFiManagerApp::handle();

  if (WiFiManagerApp::isConnected()) {
    BackendClient::handle();
  }
}