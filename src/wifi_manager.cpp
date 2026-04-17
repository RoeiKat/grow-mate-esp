#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"
#include "backend_client.h"

namespace WiFiManagerApp {

void setupPortalRoutes();
void startSetupPortal();
void stopSetupPortal();
void handleRoot();
void handleWifiPage();
void handlePairPage();
void handleNoContent();
void handleNotFound();
void handleSave();
void handleRescan();
void handleResetApi();
void handleStatus();
void processPendingWifiConnect();
void refreshWifiOptionsCache();

Preferences preferences;
WebServer server(80);

String savedSsid;
String savedPassword;

// cached Wi-Fi list so pages load fast
String cachedWifiOptions = "<option value=''>Scanning Wi-Fi...</option>";

bool setupMode = false;
bool portalStarted = false;
bool buttonPressActive = false;
unsigned long buttonPressStartMs = 0;

bool connectInProgress = false;
bool connectAttemptFinished = false;
bool connectSucceeded = false;
String lastConnectMessage = "Waiting for Wi-Fi setup...";

// deferred connect so /save response can finish cleanly
bool pendingWifiConnect = false;
String pendingSsid;
String pendingPassword;

String htmlEscape(const String& input) {
  String out = input;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  out.replace("'", "&#39;");
  return out;
}

String getPortalBaseUrl() {
  return String("http://") + WiFi.softAPIP().toString();
}

String buildWifiOptions() {
  String options;
  int networkCount = WiFi.scanNetworks();

  if (networkCount <= 0) {
    options += "<option value=''>No networks found</option>";
    return options;
  }

  for (int i = 0; i < networkCount; i++) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

    String label = htmlEscape(ssid);
    label += " (" + String(rssi) + " dBm";
    if (open) label += ", open";
    label += ")";

    options += "<option value='" + htmlEscape(ssid) + "'>" + label + "</option>";
  }

  return options;
}

void refreshWifiOptionsCache() {
  Serial.println("Refreshing Wi-Fi scan cache...");
  cachedWifiOptions = buildWifiOptions();
}

bool ensureFileSystemMounted() {
  static bool mounted = false;
  static bool attempted = false;

  if (mounted) return true;
  if (attempted) return false;

  attempted = true;
  mounted = LittleFS.begin(true);

  Serial.print("LittleFS mount: ");
  Serial.println(mounted ? "OK" : "FAILED");
  return mounted;
}

String readRequiredFile(const char* path) {
  if (!ensureFileSystemMounted()) {
    return String("<html><body><h1>LittleFS mount failed</h1><p>Could not load ") + path + "</p></body></html>";
  }

  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.print("Failed to open file: ");
    Serial.println(path);
    return String("<html><body><h1>Missing file</h1><p>Could not load ") + path + "</p></body></html>";
  }

  String content = file.readString();
  file.close();
  return content;
}

String getSetupPage(bool showBackToPairingButton) {
  String html = readRequiredFile("/setup.html");
  html.replace("{{WIFI_OPTIONS}}", cachedWifiOptions);
  html.replace("{{PORTAL_URL}}", htmlEscape(getPortalBaseUrl()));
  html.replace("{{DEVICE_SERIAL}}", htmlEscape(DEVICE_SERIAL_NUMBER));

  if (showBackToPairingButton) {
    html.replace("{{PAIR_BACK_BUTTON}}",
      "<button class=\"secondary\" onclick=\"window.location.href='/pair'\">Back to Pairing</button>");
  } else {
    html.replace("{{PAIR_BACK_BUTTON}}", "");
  }

  return html;
}

String getPairPage() {
  String html = readRequiredFile("/pair.html");
  html.replace("{{PORTAL_URL}}", htmlEscape(getPortalBaseUrl()));
  html.replace("{{DEVICE_SERIAL}}", htmlEscape(DEVICE_SERIAL_NUMBER));
  return html;
}

void loadWiFiCredentials() {
  preferences.begin("wifi", true);

  savedSsid = preferences.isKey("ssid")
    ? preferences.getString("ssid", "")
    : "";

  savedPassword = preferences.isKey("password")
    ? preferences.getString("password", "")
    : "";

  preferences.end();
}

void saveWiFiCredentials(const String& ssid, const String& password) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
}

void clearWiFiCredentials() {
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();
}

void factoryResetAndRestart(const char* reason) {
  Serial.println();
  Serial.println("=================================");
  Serial.println("Factory reset triggered");
  Serial.print("Reason: ");
  Serial.println(reason);
  Serial.println("Clearing saved Wi-Fi credentials...");
  Serial.println("=================================");
  clearWiFiCredentials();
  delay(500);
  ESP.restart();
}

void checkBootButtonLongPress() {
  int state = digitalRead(BOOT_BUTTON_PIN);

  if (state == LOW) {
    if (!buttonPressActive) {
      buttonPressActive = true;
      buttonPressStartMs = millis();
    } else if (millis() - buttonPressStartMs >= RESET_HOLD_TIME_MS) {
      factoryResetAndRestart("BOOT button long press");
    }
  } else {
    buttonPressActive = false;
    buttonPressStartMs = 0;
  }
}

void handleWifiPage() {
  server.send(200, "text/html", getSetupPage(true));
}

void handleRoot() {
  const bool wifiConnected = (WiFi.status() == WL_CONNECTED);

  if (wifiConnected || connectInProgress || connectSucceeded) {
    server.sendHeader("Location", "/pair", true);
    server.send(302, "text/plain", "");
    return;
  }

  server.send(200, "text/html", getSetupPage(false));
}

void handlePairPage() {
  const bool wifiConnected = (WiFi.status() == WL_CONNECTED);

  // Pair page only makes sense when Wi-Fi is actually connected
  if (!wifiConnected) {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
    return;
  }

  server.send(200, "text/html", getPairPage());
}

void handleNoContent() {
  server.send(204);
}

void handleNotFound() {
  handleRoot();
}

void handleSave() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "Missing SSID");
    return;
  }

  String newSsid = server.arg("ssid");
  String newPassword = server.hasArg("password") ? server.arg("password") : "";

  if (newSsid.isEmpty()) {
    server.send(400, "text/plain", "SSID cannot be empty");
    return;
  }

  saveWiFiCredentials(newSsid, newPassword);
  savedSsid = newSsid;
  savedPassword = newPassword;

  pendingSsid = newSsid;
  pendingPassword = newPassword;
  pendingWifiConnect = true;

  connectInProgress = true;
  connectAttemptFinished = false;
  connectSucceeded = false;
  lastConnectMessage = "Saving credentials. Starting Wi-Fi connection...";

  Serial.println("Saved Wi-Fi credentials:");
  Serial.print("SSID: ");
  Serial.println(newSsid);

  JsonDocument doc;
  doc["ok"] = true;
  doc["redirectTo"] = "/pair";
  doc["portalUrl"] = getPortalBaseUrl() + "/pair";

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void processPendingWifiConnect() {
  if (!pendingWifiConnect) return;

  pendingWifiConnect = false;

  Serial.println("Starting deferred Wi-Fi connection...");

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID);
  portalStarted = true;
  setupMode = true;

  WiFi.disconnect(true, true);
  delay(300);

  WiFi.begin(pendingSsid.c_str(), pendingPassword.c_str());
  lastConnectMessage = "Connecting to Wi-Fi...";
}

void handleRescan() {
  refreshWifiOptionsCache();
  server.send(200, "text/html", getSetupPage(true));
}

void handleResetApi() {
  server.send(200, "text/html", "<html><body><h3>Resetting device...</h3></body></html>");
  delay(500);
  factoryResetAndRestart("API /reset");
}

void handleStatus() {
  JsonDocument doc;

  const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  const bool isPaired = BackendClient::isPaired();
  const unsigned long lastRefreshMs = BackendClient::getLastPairingRefreshAtMs();

  int nextRefreshInSeconds = -1;
  if (!isPaired && lastRefreshMs > 0) {
    unsigned long elapsed = millis() - lastRefreshMs;
    unsigned long remaining = elapsed >= PAIRING_CODE_REFRESH_INTERVAL_MS
      ? 0
      : (PAIRING_CODE_REFRESH_INTERVAL_MS - elapsed);
    nextRefreshInSeconds = (int)(remaining / 1000);
  }

  doc["serialNumber"] = DEVICE_SERIAL_NUMBER;
  doc["wifiConnected"] = wifiConnected;
  doc["connectInProgress"] = connectInProgress;
  doc["connectAttemptFinished"] = connectAttemptFinished;
  doc["connectSucceeded"] = connectSucceeded;
  doc["localIp"] = wifiConnected ? WiFi.localIP().toString() : "";
  doc["isPaired"] = isPaired;
  doc["pairingCode"] = BackendClient::getPairingCode();
  doc["pairingExpiresAt"] = BackendClient::getPairingExpiresAt();
  doc["message"] = lastConnectMessage;
  doc["nextRefreshInSeconds"] = nextRefreshInSeconds;
  doc["portalUrl"] = getPortalBaseUrl();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void setupPortalRoutes() {
  server.on("/", HTTP_ANY, handleRoot);
  server.on("/wifi", HTTP_ANY, handleWifiPage);
  server.on("/pair", HTTP_ANY, handlePairPage);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/rescan", HTTP_ANY, handleRescan);
  server.on("/reset", HTTP_ANY, handleResetApi);
  server.on("/status", HTTP_ANY, handleStatus);

  server.on("/favicon.ico", HTTP_ANY, handleNoContent);
  server.on("/apple-touch-icon.png", HTTP_ANY, handleNoContent);
  server.on("/apple-touch-icon-precomposed.png", HTTP_ANY, handleNoContent);

  server.onNotFound(handleNotFound);
}

void stopSetupPortal() {
  if (!portalStarted) return;

  server.stop();
  WiFi.softAPdisconnect(true);
  portalStarted = false;
  setupMode = false;

  Serial.println("Setup portal stopped.");
}

void startSetupPortal() {
  if (portalStarted) {
    setupMode = true;
    return;
  }

  setupMode = true;

  if (WiFi.status() == WL_CONNECTED || connectInProgress) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_AP);
  }

  WiFi.softAP(AP_SSID);

  if (lastConnectMessage.isEmpty()) {
    lastConnectMessage = "Waiting for Wi-Fi setup...";
  }

  // do one scan when portal starts, not on every page load
  refreshWifiOptionsCache();

  setupPortalRoutes();
  server.begin();
  portalStarted = true;

  Serial.println();
  Serial.println("=================================");
  Serial.println("Starting SOFT setup portal");
  Serial.print("AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Open in browser: ");
  Serial.println(getPortalBaseUrl());
  Serial.print("Fallback IP: http://");
  Serial.println(WiFi.softAPIP());
  Serial.println("=================================");
}

void begin() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  ensureFileSystemMounted();
}

bool connectToSavedWiFi() {
  loadWiFiCredentials();

  if (savedSsid.isEmpty()) {
    Serial.println("No saved Wi-Fi credentials found.");
    startSetupPortal();
    return false;
  }

  Serial.print("Connecting to saved Wi-Fi: ");
  Serial.println(savedSsid);

  connectInProgress = true;
  connectAttemptFinished = false;
  connectSucceeded = false;
  lastConnectMessage = "Connecting to saved Wi-Fi...";

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID);
  portalStarted = true;
  setupMode = true;

  // do one scan when portal starts
  refreshWifiOptionsCache();

  setupPortalRoutes();
  server.begin();

  WiFi.begin(savedSsid.c_str(), savedPassword.c_str());

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(WIFI_RETRY_DELAY_MS);
    Serial.print('.');
  }
  Serial.println();

  connectInProgress = false;
  connectAttemptFinished = true;

  if (WiFi.status() == WL_CONNECTED) {
    connectSucceeded = true;
    lastConnectMessage = "Connected to Wi-Fi. Waiting for pairing code...";

    Serial.println("Connected to home Wi-Fi.");
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  connectSucceeded = false;
  lastConnectMessage = "Failed to connect to saved Wi-Fi.";
  Serial.println("Failed to connect to saved Wi-Fi.");

  startSetupPortal();
  return false;
}

void handle() {
  checkBootButtonLongPress();

  if (BackendClient::isPaired()) {
    if (portalStarted) {
      stopSetupPortal();
    }

    if (WiFi.status() != WL_CONNECTED && !savedSsid.isEmpty()) {
      WiFi.mode(WIFI_STA);
      WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
    }

    delay(LOOP_IDLE_DELAY_MS);
    return;
  }

  if (!portalStarted) {
    startSetupPortal();
  }

  server.handleClient();

  processPendingWifiConnect();

  if (connectInProgress && WiFi.status() == WL_CONNECTED) {
    connectInProgress = false;
    connectAttemptFinished = true;
    connectSucceeded = true;
    lastConnectMessage = "Connected to Wi-Fi. Waiting for pairing code...";
    Serial.println("Connected to Wi-Fi while keeping setup portal alive.");
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
  }

  if (!connectInProgress && WiFi.status() != WL_CONNECTED && !savedSsid.isEmpty()) {
    lastConnectMessage = "Wi-Fi disconnected. Reconnect or choose another network.";
  }

  delay(LOOP_IDLE_DELAY_MS);
}

bool isInSetupMode() {
  return setupMode;
}

bool isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

} // namespace WiFiManagerApp