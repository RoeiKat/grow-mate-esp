#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <WiFi.h>
#include <math.h>
#include "backend_client.h"
#include "config.h"

namespace {

Preferences prefs;

const char* PREFS_NAMESPACE = "growmate";
const char* PREF_KEY_SECRET = "auth_secret";
const char* PREF_KEY_VERSION = "auth_ver";
const char* PREF_KEY_RESET_PENDING = "reset_pending";

struct TelemetryData {
  float temperatureC;
  float humidityPercent;
  int soilMoisturePercent;
  int waterLevelPercent;
  bool pumpActive;
  bool online;
};

struct DeviceRuntimeState {
  bool isPaired = false;
  bool factoryResetPending = false;
  unsigned long lastFactoryResetAttemptAt = 0;
  String lastPairingCode;
  String lastPairingExpiresAt;
  unsigned long lastTelemetryAt = 0;
  unsigned long lastCommandPollAt = 0;
  unsigned long lastPairingCodeRequestAt = 0;
  unsigned long lastHeartbeatAt = 0;
};

DeviceRuntimeState runtimeState;

void loadStoredAuth() {
  prefs.begin(PREFS_NAMESPACE, true);

  if (prefs.isKey(PREF_KEY_SECRET)) {
    ACTIVE_DEVICE_SECRET = prefs.getString(PREF_KEY_SECRET, ACTIVE_DEVICE_SECRET);
  }

  if (prefs.isKey(PREF_KEY_VERSION)) {
    ACTIVE_DEVICE_AUTH_VERSION = prefs.getString(PREF_KEY_VERSION, ACTIVE_DEVICE_AUTH_VERSION);
  }

  prefs.end();
}

bool persistActiveAuth() {
  prefs.begin(PREFS_NAMESPACE, false);
  bool ok1 = prefs.putString(PREF_KEY_SECRET, ACTIVE_DEVICE_SECRET) > 0;
  bool ok2 = prefs.putString(PREF_KEY_VERSION, ACTIVE_DEVICE_AUTH_VERSION) > 0;
  prefs.end();
  return ok1 && ok2;
}

bool persistFactoryResetPendingFlag(bool pending) {
  prefs.begin(PREFS_NAMESPACE, false);
  bool ok = prefs.putBool(PREF_KEY_RESET_PENDING, pending);
  prefs.end();
  return ok;
}

void loadFactoryResetPendingFlag() {
  prefs.begin(PREFS_NAMESPACE, true);
  runtimeState.factoryResetPending = prefs.getBool(PREF_KEY_RESET_PENDING, false);
  prefs.end();
}

void clearPairingState() {
  runtimeState.isPaired = false;
  runtimeState.lastPairingCode = "";
  runtimeState.lastPairingExpiresAt = "";
  runtimeState.lastPairingCodeRequestAt = 0;
  runtimeState.lastHeartbeatAt = 0;
  runtimeState.lastTelemetryAt = 0;
  runtimeState.lastCommandPollAt = 0;
}

void addDefaultHeaders(HTTPClient& http) {
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-device-serial", DEVICE_SERIAL_NUMBER);
  http.addHeader("x-device-secret", ACTIVE_DEVICE_SECRET);
  http.addHeader("x-device-auth-version", ACTIVE_DEVICE_AUTH_VERSION);
  http.addHeader("x-device-model", DEVICE_MODEL);
  http.addHeader("x-device-firmware", DEVICE_FIRMWARE_VERSION);
}

bool beginJsonRequest(HTTPClient& http, const String& url) {
  if (!http.begin(url)) {
    Serial.print("Failed to begin HTTP request: ");
    Serial.println(url);
    return false;
  }

  addDefaultHeaders(http);
  return true;
}

bool syncFactoryResetWithBackend() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = buildApiUrl(DEVICE_RESET_PATH);
  if (!beginJsonRequest(http, url)) return false;

  int statusCode = http.POST("{}");
  String response = http.getString();
  http.end();

  Serial.print("[RESET_SYNC] HTTP status: ");
  Serial.println(statusCode);

  if (statusCode < 200 || statusCode >= 300) {
    Serial.print("[RESET_SYNC] Response: ");
    Serial.println(response);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok) {
    Serial.println("[RESET_SYNC] JSON parse failed.");
  }

  runtimeState.factoryResetPending = false;
  persistFactoryResetPendingFlag(false);
  clearPairingState();

  Serial.println("[RESET_SYNC] Backend reset synced successfully.");
  return true;
}

struct TelemetryData readDummyTelemetry() {
  const unsigned long seconds = millis() / 1000;

  TelemetryData data;
  data.temperatureC = TEMPERATURE_BASE_C + sin(seconds / 13.0f) * TEMPERATURE_VARIATION_C;
  data.humidityPercent = HUMIDITY_BASE_PERCENT + cos(seconds / 17.0f) * HUMIDITY_VARIATION_PERCENT;
  data.soilMoisturePercent = constrain(
    SOIL_MOISTURE_BASE_PERCENT + (int)(sin(seconds / 11.0f) * SOIL_MOISTURE_VARIATION_PERCENT), 0, 100
  );
  data.waterLevelPercent = constrain(
    WATER_LEVEL_BASE_PERCENT + (int)(cos(seconds / 19.0f) * WATER_LEVEL_VARIATION_PERCENT), 0, 100
  );
  data.pumpActive = false;
  data.online = true;
  return data;
}

bool updateCommandStatusWithResult(const String& commandId, const char* status, const JsonDocument& resultDoc) {
  HTTPClient http;
  String url = buildApiUrl(COMMAND_STATUS_BASE_PATH) + "/" + commandId + "/status";
  if (!beginJsonRequest(http, url)) return false;

  JsonDocument body;
  body["status"] = status;
  body["result"] = resultDoc.as<JsonObjectConst>();

  String payload;
  serializeJson(body, payload);

  int statusCode = http.PATCH(payload);
  String response = http.getString();
  http.end();

  Serial.print("[COMMAND] Status update for ");
  Serial.print(commandId);
  Serial.print(" -> ");
  Serial.print(status);
  Serial.print(" | HTTP ");
  Serial.println(statusCode);

  if (statusCode >= 200 && statusCode < 300) {
    return true;
  }

  Serial.print("[COMMAND] Status response: ");
  Serial.println(response);
  return false;
}

bool updateCommandStatusMessage(const String& commandId, const char* status, const char* message) {
  JsonDocument result;
  result["message"] = message;
  return updateCommandStatusWithResult(commandId, status, result);
}

bool requestPairingCode() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = buildApiUrl(PAIRING_REQUEST_PATH);
  if (!beginJsonRequest(http, url)) return false;

  JsonDocument payload;
  payload["model"] = DEVICE_MODEL;
  payload["firmwareVersion"] = DEVICE_FIRMWARE_VERSION;

  String body;
  serializeJson(payload, body);

  Serial.println();
  Serial.println("[PAIRING] Requesting pairing status/code...");
  Serial.print("[PAIRING] URL: ");
  Serial.println(url);
  Serial.print("[PAIRING] Serial: ");
  Serial.println(DEVICE_SERIAL_NUMBER);
  Serial.print("[PAIRING] Auth version: ");
  Serial.println(ACTIVE_DEVICE_AUTH_VERSION);

  int statusCode = http.POST(body);
  String response = http.getString();
  http.end();

  Serial.print("[PAIRING] HTTP status: ");
  Serial.println(statusCode);
  Serial.print("[PAIRING] Response: ");
  Serial.println(response);

  if (statusCode == 401) {
    Serial.println("[PAIRING] Unauthorized device. Backend did not accept current derived secret.");
    return false;
  }

  if (statusCode < 200 || statusCode >= 300) {
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.print("[PAIRING] JSON parse failed: ");
    Serial.println(error.c_str());
    return false;
  }

  runtimeState.isPaired = doc["device"]["isPaired"] | doc["isPaired"] | false;
  runtimeState.lastPairingCode = doc["pairingCode"]["code"] | doc["code"] | "";
  runtimeState.lastPairingExpiresAt = doc["pairingCode"]["expiresAt"] | doc["expiresAt"] | "";

  Serial.print("[PAIRING] Paired: ");
  Serial.println(runtimeState.isPaired ? "yes" : "no");

  if (!runtimeState.lastPairingCode.isEmpty()) {
    Serial.print("[PAIRING] Current code: ");
    Serial.println(runtimeState.lastPairingCode);

    if (!runtimeState.lastPairingExpiresAt.isEmpty()) {
      Serial.print("[PAIRING] Expires at: ");
      Serial.println(runtimeState.lastPairingExpiresAt);
    }
  }

  return true;
}

bool sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = buildApiUrl(HEARTBEAT_PATH);
  if (!beginJsonRequest(http, url)) return false;

  int statusCode = http.GET();
  String response = http.getString();
  http.end();

  Serial.print("[HEARTBEAT] HTTP status: ");
  Serial.println(statusCode);

  if (statusCode < 200 || statusCode >= 300) {
    Serial.print("[HEARTBEAT] Response: ");
    Serial.println(response);
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.print("[HEARTBEAT] JSON parse failed: ");
    Serial.println(error.c_str());
    return false;
  }

  runtimeState.isPaired = doc["device"]["isPaired"] | doc["paired"] | false;
  return true;
}

bool sendTelemetry() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!runtimeState.isPaired) return false;

  TelemetryData telemetry = readDummyTelemetry();

  JsonDocument body;
  body["temperatureC"] = telemetry.temperatureC;
  body["humidityPercent"] = telemetry.humidityPercent;
  body["soilMoisturePercent"] = telemetry.soilMoisturePercent;
  body["waterLevelPercent"] = telemetry.waterLevelPercent;
  body["pumpActive"] = telemetry.pumpActive;
  body["online"] = telemetry.online;

  String payload;
  serializeJson(body, payload);

  HTTPClient http;
  String url = buildApiUrl(TELEMETRY_PATH);
  if (!beginJsonRequest(http, url)) return false;

  int statusCode = http.POST(payload);
  String response = http.getString();
  http.end();

  Serial.print("[TELEMETRY] HTTP status: ");
  Serial.println(statusCode);

  if (statusCode >= 200 && statusCode < 300) {
    return true;
  }

  Serial.print("[TELEMETRY] Response: ");
  Serial.println(response);
  return false;
}

bool handleRefreshTelemetry(const JsonObjectConst& command) {
  String commandId = command["_id"] | command["id"] | "";

  if (commandId.isEmpty()) {
    Serial.println("[REFRESH] Missing command id.");
    return false;
  }

  updateCommandStatusMessage(commandId, "in_progress", "Refreshing telemetry now");

  bool sent = sendTelemetry();

  JsonDocument result;
  if (sent) {
    result["message"] = "Telemetry refreshed successfully";
    result["telemetrySent"] = true;
    return updateCommandStatusWithResult(commandId, "finished", result);
  }

  result["message"] = "Failed to send telemetry";
  result["telemetrySent"] = false;
  return updateCommandStatusWithResult(commandId, "failed", result);
}

bool handleWaterPlant(const JsonObjectConst& command) {
  String commandId = command["_id"] | command["id"] | "";
  JsonObjectConst payload = command["payload"].as<JsonObjectConst>();

  if (commandId.isEmpty()) {
    Serial.println("[WATER] Missing command id.");
    return false;
  }

  int amountMl = payload["amountMl"] | 0;
  const char* reason = payload["reason"] | "manual";

  updateCommandStatusMessage(commandId, "in_progress", "Watering command received");

  Serial.println();
  Serial.println("[WATER] =================================");
  Serial.println("[WATER] Water plant command received");
  Serial.print("[WATER] amountMl: ");
  Serial.println(amountMl);
  Serial.print("[WATER] reason: ");
  Serial.println(reason);
  Serial.println("[WATER] No pump/servo installed yet.");
  Serial.println("[WATER] TODO: replace this handler with real actuator logic.");
  Serial.println("[WATER] =================================");

  JsonDocument result;
  result["message"] = "Water command simulated on device";
  result["amountMl"] = amountMl;
  result["reason"] = reason;
  result["simulated"] = true;

  return updateCommandStatusWithResult(commandId, "finished", result);
}

bool handleRotateAuthSecret(const JsonObjectConst& command) {
  String commandId = command["_id"] | command["id"] | "";
  JsonObjectConst payload = command["payload"].as<JsonObjectConst>();

  String targetAuthVersion = payload["targetAuthVersion"] | "";
  String targetAuthSecret = payload["targetAuthSecret"] | "";

  if (commandId.isEmpty()) {
    Serial.println("[ROTATE_AUTH] Missing command id.");
    return false;
  }

  if (targetAuthVersion.isEmpty() || targetAuthSecret.isEmpty()) {
    updateCommandStatusMessage(commandId, "failed", "Missing targetAuthVersion or targetAuthSecret");
    return false;
  }

  updateCommandStatusMessage(commandId, "in_progress", "Starting auth rotation");

  String oldSecret = ACTIVE_DEVICE_SECRET;
  String oldVersion = ACTIVE_DEVICE_AUTH_VERSION;

  ACTIVE_DEVICE_SECRET = targetAuthSecret;
  ACTIVE_DEVICE_AUTH_VERSION = targetAuthVersion;

  JsonDocument result;
  result["message"] = "Auth rotation applied";
  result["authVersion"] = ACTIVE_DEVICE_AUTH_VERSION;

  if (updateCommandStatusWithResult(commandId, "finished", result)) {
    if (!persistActiveAuth()) {
      Serial.println("[ROTATE_AUTH] Warning: auth updated in memory but failed to persist.");
    }

    Serial.print("[ROTATE_AUTH] Rotation complete. New version: ");
    Serial.println(ACTIVE_DEVICE_AUTH_VERSION);
    return true;
  }

  ACTIVE_DEVICE_SECRET = oldSecret;
  ACTIVE_DEVICE_AUTH_VERSION = oldVersion;

  updateCommandStatusMessage(commandId, "failed", "Backend did not accept new auth credentials");
  return false;
}

bool handleFirmwareUpdate(const JsonObjectConst& command) {
  String commandId = command["_id"] | command["id"] | "";
  JsonObjectConst payload = command["payload"].as<JsonObjectConst>();

  String targetVersion = payload["version"] | "";
  String firmwareUrl = payload["url"] | "";

  if (commandId.isEmpty()) {
    Serial.println("[FW] Missing command id.");
    return false;
  }

  if (targetVersion.isEmpty() || firmwareUrl.isEmpty()) {
    updateCommandStatusMessage(commandId, "failed", "Missing firmware version or url");
    return false;
  }

  updateCommandStatusMessage(commandId, "in_progress", "Starting firmware update");

  Serial.println();
  Serial.println("[FW] Starting firmware update...");
  Serial.print("[FW] Target version: ");
  Serial.println(targetVersion);
  Serial.print("[FW] URL: ");
  Serial.println(firmwareUrl);

  WiFiClient client;
  httpUpdate.rebootOnUpdate(false);

  t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl);

  switch (ret) {
    case HTTP_UPDATE_FAILED: {
      JsonDocument result;
      result["message"] = httpUpdate.getLastErrorString();
      updateCommandStatusWithResult(commandId, "failed", result);
      Serial.print("[FW] Update failed: ");
      Serial.println(httpUpdate.getLastErrorString());
      return false;
    }

    case HTTP_UPDATE_NO_UPDATES: {
      updateCommandStatusMessage(commandId, "failed", "No update available at provided URL");
      Serial.println("[FW] No updates available.");
      return false;
    }

    case HTTP_UPDATE_OK: {
      JsonDocument result;
      result["message"] = "Firmware update installed successfully";
      result["firmwareVersion"] = targetVersion;

      bool reported = updateCommandStatusWithResult(commandId, "finished", result);
      Serial.println("[FW] Update installed. Rebooting...");

      delay(reported ? 400 : 1000);
      ESP.restart();
      return true;
    }
  }

  updateCommandStatusMessage(commandId, "failed", "Unexpected OTA result");
  return false;
}

bool executeCommand(const JsonObjectConst& command) {
  String commandId = command["_id"] | command["id"] | "";
  String type = command["type"] | "unknown";

  if (commandId.isEmpty()) {
    Serial.println("[COMMAND] Missing command id, skipping.");
    return false;
  }

  Serial.println();
  Serial.println("[COMMAND] Executing command...");
  Serial.print("[COMMAND] ID: ");
  Serial.println(commandId);
  Serial.print("[COMMAND] Type: ");
  Serial.println(type);

  if (type == "refresh_telemetry") {
    return handleRefreshTelemetry(command);
  }

  if (type == "water_plant") {
    return handleWaterPlant(command);
  }

  if (type == "rotate_auth_secret") {
    return handleRotateAuthSecret(command);
  }

  if (type == "firmware_update") {
    return handleFirmwareUpdate(command);
  }

  updateCommandStatusMessage(commandId, "failed", "Unknown command type");
  return false;
}

bool pollPendingCommands() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!runtimeState.isPaired) return false;

  HTTPClient http;
  String url = buildApiUrl(PENDING_COMMANDS_PATH);
  if (!beginJsonRequest(http, url)) return false;

  int statusCode = http.GET();
  String response = http.getString();
  http.end();

  Serial.print("[COMMAND] HTTP status: ");
  Serial.println(statusCode);

  if (statusCode < 200 || statusCode >= 300) {
    Serial.print("[COMMAND] Response: ");
    Serial.println(response);
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.print("[COMMAND] JSON parse failed: ");
    Serial.println(error.c_str());
    return false;
  }

  JsonArray commands;
  if (doc["commands"].is<JsonArray>()) {
    commands = doc["commands"].as<JsonArray>();
  } else if (doc.is<JsonArray>()) {
    commands = doc.as<JsonArray>();
  }

  if (commands.isNull() || commands.size() == 0) {
    Serial.println("[COMMAND] No pending commands.");
    return true;
  }

  Serial.print("[COMMAND] Found ");
  Serial.print(commands.size());
  Serial.println(" pending command(s).");

  for (JsonObject command : commands) {
    const char* status = command["status"] | "pending";
    if (strcmp(status, "canceled") == 0 || strcmp(status, "finished") == 0) {
      continue;
    }
    executeCommand(command);
  }

  return true;
}

} // namespace

namespace BackendClient {

void printIdentity() {
  Serial.println();
  Serial.println("=================================");
  Serial.println("GrowMate device identity");
  Serial.print("Serial: ");
  Serial.println(DEVICE_SERIAL_NUMBER);
  Serial.print("Model: ");
  Serial.println(DEVICE_MODEL);
  Serial.print("Firmware: ");
  Serial.println(DEVICE_FIRMWARE_VERSION);
  Serial.print("Auth version: ");
  Serial.println(ACTIVE_DEVICE_AUTH_VERSION);
  Serial.print("Auth secret: ");
  Serial.println(ACTIVE_DEVICE_SECRET);
  Serial.println("=================================");
}

void begin() {
  resetRuntimeState();
  loadStoredAuth();
  loadFactoryResetPendingFlag();
  printIdentity();

  if (runtimeState.factoryResetPending) {
    Serial.println("[RESET_SYNC] Factory reset is pending and will sync after Wi-Fi connects.");
  }
}

void resetRuntimeState() {
  runtimeState = DeviceRuntimeState{};
  loadFactoryResetPendingFlag();
}

void handle() {
  if (WiFi.status() != WL_CONNECTED) return;

  const unsigned long now = millis();

  if (runtimeState.factoryResetPending) {
    if (runtimeState.lastFactoryResetAttemptAt == 0 ||
        now - runtimeState.lastFactoryResetAttemptAt >= 5000) {
      runtimeState.lastFactoryResetAttemptAt = now;
      syncFactoryResetWithBackend();
    }
    return;
  }

  if (!runtimeState.isPaired) {
    if (runtimeState.lastPairingCodeRequestAt == 0 ||
        now - runtimeState.lastPairingCodeRequestAt >= PAIRING_CODE_REFRESH_INTERVAL_MS) {
      runtimeState.lastPairingCodeRequestAt = now;
      requestPairingCode();
    }
    return;
  }

  if (runtimeState.lastHeartbeatAt == 0 ||
      now - runtimeState.lastHeartbeatAt >= HEARTBEAT_INTERVAL_MS) {
    runtimeState.lastHeartbeatAt = now;
    sendHeartbeat();
  }

  if (runtimeState.lastTelemetryAt == 0 ||
      now - runtimeState.lastTelemetryAt >= TELEMETRY_INTERVAL_MS) {
    runtimeState.lastTelemetryAt = now;
    sendTelemetry();
  }

  if (runtimeState.lastCommandPollAt == 0 ||
      now - runtimeState.lastCommandPollAt >= COMMAND_POLL_INTERVAL_MS) {
    runtimeState.lastCommandPollAt = now;
    pollPendingCommands();
  }
}

void markFactoryResetPending() {
  runtimeState.factoryResetPending = true;
  runtimeState.lastFactoryResetAttemptAt = 0;
  persistFactoryResetPendingFlag(true);
  clearPairingState();
}

bool isFactoryResetPending() {
  return runtimeState.factoryResetPending;
}

bool isPaired() {
  return runtimeState.isPaired;
}

bool hasPairingCode() {
  return !runtimeState.lastPairingCode.isEmpty();
}

String getPairingCode() {
  return runtimeState.lastPairingCode;
}

String getPairingExpiresAt() {
  return runtimeState.lastPairingExpiresAt;
}

unsigned long getLastPairingRefreshAtMs() {
  return runtimeState.lastPairingCodeRequestAt;
}

} // namespace BackendClient