#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "config.h"

namespace WiFiManagerApp {

Preferences preferences;
WebServer server(80);

String savedSsid;
String savedPassword;

bool setupMode = false;

// BOOT button reset tracking
bool buttonPressActive = false;
unsigned long buttonPressStartMs = 0;

// ---------------------------
// Internal helpers
// ---------------------------

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
  String options = "";

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
    label += " (";
    label += String(rssi);
    label += " dBm";
    if (open) label += ", open";
    label += ")";

    options += "<option value='" + htmlEscape(ssid) + "'>" + label + "</option>";
  }

  return options;
}

String getSetupPage() {
  String wifiOptions = buildWifiOptions();

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Grow Mate Setup</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      max-width: 430px;
      margin: 30px auto;
      padding: 16px;
      background: #f6f6f6;
      color: #111;
    }
    .card {
      background: #fff;
      padding: 20px;
      border-radius: 14px;
      box-shadow: 0 2px 12px rgba(0,0,0,0.08);
    }
    h2 {
      margin-top: 0;
      margin-bottom: 8px;
    }
    p {
      color: #555;
      margin-top: 0;
      margin-bottom: 18px;
      line-height: 1.4;
    }
    label {
      display: block;
      font-weight: bold;
      margin-bottom: 6px;
    }
    select, input, button {
      width: 100%;
      box-sizing: border-box;
      padding: 12px;
      margin-bottom: 16px;
      border-radius: 10px;
      border: 1px solid #ddd;
      font-size: 15px;
    }
    button {
      border: none;
      background: #111;
      color: white;
      cursor: pointer;
    }
    .secondary {
      background: #eee;
      color: #111;
    }
    .hint {
      font-size: 13px;
      color: #777;
      margin-top: -8px;
      margin-bottom: 14px;
    }
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

  html += wifiOptions;

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

    <div class="hint">
      If the page does not open automatically, go to <b>http://192.168.4.1</b>
    </div>
  </div>

  <script>
    function copySelectedSsid() {
      const select = document.getElementById('ssidSelect');
      const input = document.getElementById('ssid');
      if (select.value) {
        input.value = select.value;
      }
    }
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

// ---------------------------
// Web handlers
// ---------------------------

void handleRoot() {
  server.send(200, "text/html", getSetupPage());
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

  Serial.println("Saved Wi-Fi credentials:");
  Serial.print("SSID: ");
  Serial.println(newSsid);

  server.send(200, "text/html",
              "<html><body><h3>Saved. Restarting device...</h3></body></html>");

  delay(1500);
  ESP.restart();
}

void handleRescan() {
  server.send(200, "text/html", getSetupPage());
}

void handleResetApi() {
  server.send(200, "text/html",
              "<html><body><h3>Resetting device...</h3></body></html>");
  delay(500);
  factoryResetAndRestart("API /reset");
}

void setupPortalRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/rescan", HTTP_GET, handleRescan);
  server.on("/reset", HTTP_POST, handleResetApi);
  server.on("/reset", HTTP_GET, handleResetApi);

  server.onNotFound(handleRoot);
}

} // namespace WiFiManagerApp

// ---------------------------
// Public API
// ---------------------------

namespace WiFiManagerApp {

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

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    setupMode = false;
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

  IPAddress apIP = WiFi.softAPIP();

  Serial.println();
  Serial.println("=================================");
  Serial.println("Starting setup portal");
  Serial.print("AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Portal IP: http://");
  Serial.println(apIP);
  Serial.println("=================================");

  setupPortalRoutes();
  server.begin();
}

void handle() {
  checkBootButtonLongPress();

  if (setupMode) {
    server.handleClient();
    delay(2);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Lost Wi-Fi connection. Reopening setup portal...");
    startSetupPortal();
    return;
  }

  delay(2);
}

bool isInSetupMode() {
  return setupMode;
}

bool isConnected() {
  return !setupMode && WiFi.status() == WL_CONNECTED;
}

}