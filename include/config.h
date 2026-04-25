#pragma once

#include <Arduino.h>

// ---------------------------
// Device identity
// ---------------------------
extern String DEVICE_SERIAL_NUMBER;
extern String ACTIVE_DEVICE_SECRET;
extern String ACTIVE_DEVICE_AUTH_VERSION;

// Burned initial per-device secret/version.
// These are NOT backend master secrets.
extern const char* DEFAULT_DEVICE_SECRET;
extern const char* DEFAULT_DEVICE_AUTH_VERSION;
extern const char* DEVICE_MODEL;
extern const char* DEVICE_FIRMWARE_VERSION;

// ---------------------------
// Network / setup portal
// ---------------------------
extern const char* AP_SSID;
extern const char* PORTAL_HOSTNAME;
extern const int BOOT_BUTTON_PIN;
extern const unsigned long RESET_HOLD_TIME_MS;
extern const unsigned long WIFI_CONNECT_TIMEOUT_MS;
extern const unsigned long WIFI_RETRY_DELAY_MS;

// ---------------------------
// Backend
// ---------------------------
extern const char* API_BASE_URL;
extern const char* PAIRING_REQUEST_PATH;
extern const char* HEARTBEAT_PATH;
extern const char* TELEMETRY_PATH;
extern const char* PENDING_COMMANDS_PATH;
extern const char* COMMAND_STATUS_BASE_PATH;
extern const char* DEVICE_RESET_PATH;

// ---------------------------
// Timing
// ---------------------------
extern const unsigned long TELEMETRY_INTERVAL_MS;
extern const unsigned long COMMAND_POLL_INTERVAL_MS;
extern const unsigned long PAIRING_CODE_REFRESH_INTERVAL_MS;
extern const unsigned long HEARTBEAT_INTERVAL_MS;
extern const unsigned long LOOP_IDLE_DELAY_MS;

// ---------------------------
// Dummy telemetry tuning
// ---------------------------
extern const float TEMPERATURE_BASE_C;
extern const float TEMPERATURE_VARIATION_C;
extern const float HUMIDITY_BASE_PERCENT;
extern const float HUMIDITY_VARIATION_PERCENT;
extern const int SOIL_MOISTURE_BASE_PERCENT;
extern const int SOIL_MOISTURE_VARIATION_PERCENT;
extern const int WATER_LEVEL_BASE_PERCENT;
extern const int WATER_LEVEL_VARIATION_PERCENT;

String buildApiUrl(const char* path);
void initConfig();
String generateDeviceSerial();
String getPortalBaseUrl();