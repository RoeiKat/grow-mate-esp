#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "config.h"
#include "backend_client.h"

namespace BackendClient {

void sendTelemetry() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi not connected. Skipping POST.");
    return;
  }

  HTTPClient http;
  http.begin(POST_URL);
  http.addHeader("Content-Type", "application/json");

  String payload = R"({
    "device": "grow-mate-esp32",
    "status": "online",
    "message": "hello from esp32"
  })";

  Serial.println("Sending POST...");
  int code = http.POST(payload);

  Serial.print("HTTP code: ");
  Serial.println(code);

  if (code > 0) {
    Serial.print("Response: ");
    Serial.println(http.getString());
  } else {
    Serial.print("POST failed: ");
    Serial.println(http.errorToString(code));
  }

  http.end();
}

}