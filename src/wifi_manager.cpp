#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "config.h"
#include "backend_client.h"

namespace WiFiManagerApp {

Preferences preferences;
WebServer server(80);

String savedSsid;
String savedPassword;

bool setupMode = false;
bool buttonPressActive = false;
unsigned long buttonPressStartMs = 0;

bool connectInProgress = false;
bool connectAttemptFinished = false;
bool connectSucceeded = false;
String lastConnectMessage = "Waiting for Wi-Fi setup...";

String htmlEscape(const String& input) {
  String out = input;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  out.replace("'", "&#39;");
  return out;
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

String getSetupPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Grow Mate Setup</title>
  <style>
    body { font-family: Arial, sans-serif; max-width: 430px; margin: 30px auto; padding: 16px; background: #f6f6f6; color: #111; }
    .card { background: #fff; padding: 20px; border-radius: 14px; box-shadow: 0 2px 12px rgba(0,0,0,0.08); }
    h2 { margin-top: 0; margin-bottom: 8px; }
    p { color: #555; margin-top: 0; margin-bottom: 18px; line-height: 1.4; }
    label { display: block; font-weight: bold; margin-bottom: 6px; }
    select, input, button { width: 100%; box-sizing: border-box; padding: 12px; margin-bottom: 16px; border-radius: 10px; border: 1px solid #ddd; font-size: 15px; }
    button { border: none; background: #111; color: white; cursor: pointer; }
    .secondary { background: #eee; color: #111; }
    .hint { font-size: 13px; color: #777; margin-top: -8px; margin-bottom: 14px; }
    .status { margin-top: 16px; padding: 14px; border-radius: 10px; background: #f3f3f3; }
    .code { font-size: 28px; font-weight: bold; letter-spacing: 4px; }
    .mono { font-family: monospace; word-break: break-all; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Grow Mate Wi-Fi Setup</h2>
    <p>Connect this device to your home Wi-Fi.</p>

    <form action="/save" method="POST">
      <label for="ssidSelect">Detected Wi-Fi networks</label>
      <select id="ssidSelect" onchange="copySelectedSsid()">
        <option value="">-- Select a network --</option>
)rawliteral";

  html += buildWifiOptions();

  html += R"rawliteral(
      </select>

      <label for="ssid">Wi-Fi name (SSID)</label>
      <input id="ssid" name="ssid" placeholder="Enter SSID" required>

      <label for="password">Wi-Fi password</label>
      <input id="password" name="password" type="password" placeholder="Enter password">

      <button type="submit">Save and Connect</button>
    </form>

    <form action="/rescan" method="GET">
      <button class="secondary" type="submit">Rescan Wi-Fi</button>
    </form>

    <form action="/reset" method="POST">
      <button class="secondary" type="submit">Factory Reset Device</button>
    </form>

    <div class="status">
      <div><b>Device serial:</b> <span id="serial" class="mono">-</span></div>
      <div><b>Wi-Fi status:</b> <span id="wifiStatus">Waiting...</span></div>
      <div><b>Local IP:</b> <span id="localIp">-</span></div>
      <div><b>Paired:</b> <span id="paired">No</span></div>
      <div><b>Pairing code:</b> <span id="pairingCode" class="code">------</span></div>
      <div><b>Refresh countdown:</b> <span id="countdown">--</span></div>
      <div><b>Expires at:</b> <span id="expiresAt">-</span></div>
      <div><b>Message:</b> <span id="message">Waiting for Wi-Fi setup...</span></div>
    </div>

    <div class="hint">Stay on this page after saving. The device will connect in the background and show the live pairing code here.</div>
    <div class="hint">If the page does not open automatically, go to <b>http://192.168.4.1</b></div>
  </div>

  <script>
    function copySelectedSsid() {
      const select = document.getElementById('ssidSelect');
      const input = document.getElementById('ssid');
      if (select.value) input.value = select.value;
    }

    function updateCountdown(remainingSeconds) {
      const countdownEl = document.getElementById('countdown');
      countdownEl.textContent =
        (remainingSeconds === null || remainingSeconds === undefined || remainingSeconds < 0)
          ? '--'
          : (remainingSeconds + ' sec');
    }

    async function pollStatus() {
      try {
        const res = await fetch('/status');
        const data = await res.json();

        document.getElementById('serial').textContent = data.serialNumber || '-';
        document.getElementById('wifiStatus').textContent =
          data.wifiConnected ? 'Connected' : (data.connectInProgress ? 'Connecting...' : 'Not connected');
        document.getElementById('localIp').textContent = data.localIp || '-';
        document.getElementById('paired').textContent = data.isPaired ? 'Yes' : 'No';
        document.getElementById('pairingCode').textContent = data.pairingCode || '------';
        document.getElementById('expiresAt').textContent = data.pairingExpiresAt || '-';
        document.getElementById('message').textContent = data.message || '-';

        updateCountdown(data.nextRefreshInSeconds);
      } catch (err) {
        document.getElementById('message').textContent = 'Failed to fetch live status';
      }
    }

    setInterval(pollStatus, 2000);
    pollStatus();
  </script>
</body>
</html>
)rawliteral";

  return html;
}

void loadWiFiCredentials() {
  preferences.begin("wifi", true);
  savedSsid = preferences.getString("ssid", "");
  savedPassword = preferences.getString("password", "");
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

void handleRoot() {
  server.send(200, "text/html", getSetupPage());
}

void startStationConnect(const String& ssid, const String& password) {
  connectInProgress = true;
  connectAttemptFinished = false;
  connectSucceeded = false;
  lastConnectMessage = "Connecting to Wi-Fi...";

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.println();
  Serial.println("=================================");
  Serial.println("Attempting Wi-Fi connect without restart");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.println("=================================");
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

  Serial.println("Saved Wi-Fi credentials:");
  Serial.print("SSID: ");
  Serial.println(newSsid);

  startStationConnect(newSsid, newPassword);

  server.send(200, "text/html",
    "<html><body><h3>Saved. Connecting in background...</h3>"
    "<p>Stay on this page. The live pairing code will appear here once Wi-Fi connects.</p>"
    "<p><a href='/'>Back to setup page</a></p>"
    "</body></html>");
}

void handleRescan() {
  server.send(200, "text/html", getSetupPage());
}

void handleResetApi() {
  server.send(200, "text/html", "<html><body><h3>Resetting device...</h3></body></html>");
  delay(500);
  factoryResetAndRestart("API /reset");
}

void handleGenerate204() { handleRoot(); }
void handleHotspotDetect() { handleRoot(); }
void handleConnectTest() { handleRoot(); }
void handleNcsi() { handleRoot(); }

void handleStatus() {
  JsonDocument doc;

  const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  const unsigned long lastRefreshMs = BackendClient::getLastPairingRefreshAtMs();

  int nextRefreshInSeconds = -1;
  if (!BackendClient::isPaired() && lastRefreshMs > 0) {
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
  doc["isPaired"] = BackendClient::isPaired();
  doc["pairingCode"] = BackendClient::getPairingCode();
  doc["pairingExpiresAt"] = BackendClient::getPairingExpiresAt();
  doc["message"] = lastConnectMessage;
  doc["nextRefreshInSeconds"] = nextRefreshInSeconds;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void setupPortalRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/rescan", HTTP_GET, handleRescan);
  server.on("/reset", HTTP_POST, handleResetApi);
  server.on("/reset", HTTP_GET, handleResetApi);
  server.on("/status", HTTP_GET, handleStatus);

  server.on("/generate_204", HTTP_GET, handleGenerate204);
  server.on("/hotspot-detect.html", HTTP_GET, handleHotspotDetect);
  server.on("/connecttest.txt", HTTP_GET, handleConnectTest);
  server.on("/ncsi.txt", HTTP_GET, handleNcsi);

  server.onNotFound(handleRoot);
}

void begin() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
}

bool connectToSavedWiFi() {
  loadWiFiCredentials();

  if (savedSsid.isEmpty()) {
    Serial.println("No saved Wi-Fi credentials found.");
    return false;
  }

  Serial.print("Connecting to saved Wi-Fi: ");
  Serial.println(savedSsid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid.c_str(), savedPassword.c_str());

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(WIFI_RETRY_DELAY_MS);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    setupMode = false;
    connectInProgress = false;
    connectAttemptFinished = true;
    connectSucceeded = true;
    lastConnectMessage = "Connected to Wi-Fi.";

    Serial.println("Connected to home Wi-Fi.");
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("Failed to connect to saved Wi-Fi.");
  return false;
}

void startSetupPortal() {
  setupMode = true;
  server.stop();
  WiFi.disconnect(true, true);
  delay(500);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);

  connectInProgress = false;
  connectAttemptFinished = false;
  connectSucceeded = false;
  lastConnectMessage = "Waiting for Wi-Fi setup...";

  Serial.println();
  Serial.println("=================================");
  Serial.println("Starting setup portal");
  Serial.print("AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Portal IP: http://");
  Serial.println(WiFi.softAPIP());
  Serial.println("=================================");

  setupPortalRoutes();
  server.begin();
}

void handle() {
  checkBootButtonLongPress();

  if (setupMode) {
    server.handleClient();

    if (connectInProgress && WiFi.status() == WL_CONNECTED) {
      connectInProgress = false;
      connectAttemptFinished = true;
      connectSucceeded = true;
      lastConnectMessage = "Connected to Wi-Fi. Waiting for pairing code...";
      Serial.println("Connected to Wi-Fi while keeping setup portal alive.");
      Serial.print("Local IP: ");
      Serial.println(WiFi.localIP());
    }

    delay(LOOP_IDLE_DELAY_MS);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Lost Wi-Fi connection. Reopening setup portal...");
    startSetupPortal();
    return;
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