#include "config.h"
#include <WiFi.h>

// ---------------------------
// Device identity
// ---------------------------
String CURRENT_PREFIX = "00000";
String DEVICE_SERIAL_NUMBER = "";
String ACTIVE_DEVICE_SECRET = "";
String ACTIVE_DEVICE_AUTH_VERSION = "";

// Burn this per-device secret into each board.
// It should come from:
// GET /api/devices/factory/generate-secret/<SERIAL>?version=v1
const char* DEFAULT_DEVICE_SECRET = "02cfceeffc4903001e7d2d69d080f01d2c1a11b1745b6af172d677bbdca3b022";
const char* DEFAULT_DEVICE_AUTH_VERSION = "v1";
const char* DEVICE_MODEL = "GrowMate ESP32";
const char* DEVICE_FIRMWARE_VERSION = "1.0.0";

// ---------------------------
// Network / setup portal
// ---------------------------
const char* AP_SSID = "ARO - G1";
const char* PORTAL_HOSTNAME = "grow-mate.local";
const int BOOT_BUTTON_PIN = 0;
const unsigned long RESET_HOLD_TIME_MS = 5000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
const unsigned long WIFI_RETRY_DELAY_MS = 500;

// ---------------------------
// Backend
// ---------------------------
const char* API_BASE_URL = "http://192.168.1.244:3000";
const char* PAIRING_REQUEST_PATH = "/api/device-client/pairing/request";
const char* HEARTBEAT_PATH = "/api/device-client/heartbeat";
const char* TELEMETRY_PATH = "/api/device-client/data";
const char* PENDING_COMMANDS_PATH = "/api/device-client/commands/pending";
const char* COMMAND_STATUS_BASE_PATH = "/api/device-client/commands";
const char* DEVICE_RESET_PATH = "/api/device-client/reset";

// ---------------------------
// Timing
// ---------------------------
const unsigned long TELEMETRY_INTERVAL_MS = 30000;
const unsigned long COMMAND_POLL_INTERVAL_MS = 15000;
const unsigned long PAIRING_CODE_REFRESH_INTERVAL_MS = 60000;
const unsigned long HEARTBEAT_INTERVAL_MS = 60000;
const unsigned long LOOP_IDLE_DELAY_MS = 2;

// ---------------------------
// Dummy telemetry tuning
// ---------------------------
const float TEMPERATURE_BASE_C = 24.5f;
const float TEMPERATURE_VARIATION_C = 2.0f;
const float HUMIDITY_BASE_PERCENT = 58.0f;
const float HUMIDITY_VARIATION_PERCENT = 8.0f;
const int SOIL_MOISTURE_BASE_PERCENT = 43;
const int SOIL_MOISTURE_VARIATION_PERCENT = 10;
const int WATER_LEVEL_BASE_PERCENT = 72;
const int WATER_LEVEL_VARIATION_PERCENT = 12;

String buildApiUrl(const char* path) {
  return String(API_BASE_URL) + String(path);
}

String generateDeviceSerial() {
  uint64_t chipid = ESP.getEfuseMac();

  char macStr[13];
  sprintf(macStr, "%04X%08X",
          (uint16_t)(chipid >> 32),
          (uint32_t)chipid);

  return CURRENT_PREFIX + String(macStr);
}

void initConfig() {
  DEVICE_SERIAL_NUMBER = generateDeviceSerial();
  ACTIVE_DEVICE_SECRET = String(DEFAULT_DEVICE_SECRET);
  ACTIVE_DEVICE_AUTH_VERSION = String(DEFAULT_DEVICE_AUTH_VERSION);
}

String getPortalBaseUrl() {
  return String("http://") + PORTAL_HOSTNAME;
}