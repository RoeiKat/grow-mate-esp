#include <Arduino.h>
#include "config.h"
#include "wifi_manager.h"
#include "backend_client.h"

unsigned long lastPostTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFiManagerApp::begin();

  bool connected = WiFiManagerApp::connectToSavedWiFi();
  if (!connected) {
    WiFiManagerApp::startSetupPortal();
  }
}

void loop() {
  WiFiManagerApp::handle();

  if (WiFiManagerApp::isConnected()) {
    unsigned long now = millis();
    if (now - lastPostTime >= POST_INTERVAL_MS) {
      lastPostTime = now;
      BackendClient::sendTelemetry();
    }
  }
}