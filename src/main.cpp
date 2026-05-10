#include <Arduino.h>
#include <FastLED.h>
#include "backend_client.h"
#include "wifi_manager.h"
#include "config.h"

#define LED_PIN 18
#define NUM_LEDS 1
#define LED_BRIGHTNESS 255
#define PAIRING_BLINK_PERIOD_MS 2000
#define PAIRING_TO_GREEN_MS 600
#define PAIRED_GREEN_DURATION_MS 10000
#define GREEN_FADEOUT_MS 5000

enum LedState {
  LED_STATE_PAIRING_BLINK,
  LED_STATE_TRANSITION_TO_GREEN,
  LED_STATE_GREEN_HOLD,
  LED_STATE_GREEN_FADEOUT,
  LED_STATE_OFF
};

CRGB leds[NUM_LEDS];
static LedState ledState = LED_STATE_PAIRING_BLINK;
static unsigned long ledStateStart = 0;

static float easeInOut(float t) {
  return 0.5f - 0.5f * cosf(PI * t);
}

void setupLed() {
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  leds[0] = CRGB::Black;
  FastLED.show();
}

void updateLedState() {
  const unsigned long now = millis();
  const bool isPaired = BackendClient::isPaired();

  if (!isPaired) {
    if (ledState != LED_STATE_PAIRING_BLINK) {
      ledState = LED_STATE_PAIRING_BLINK;
      ledStateStart = now;
    }

    const unsigned long phase = now % PAIRING_BLINK_PERIOD_MS;
    float normalized = (phase < PAIRING_BLINK_PERIOD_MS / 2)
      ? (float)phase / (PAIRING_BLINK_PERIOD_MS / 2)
      : (float)(phase - PAIRING_BLINK_PERIOD_MS / 2) / (PAIRING_BLINK_PERIOD_MS / 2);
    float eased = easeInOut(normalized);
    uint8_t brightness = (phase < PAIRING_BLINK_PERIOD_MS / 2)
      ? (uint8_t)(eased * LED_BRIGHTNESS)
      : (uint8_t)((1.0f - eased) * LED_BRIGHTNESS);

    leds[0] = CRGB(0, 0, brightness);
    FastLED.show();
    return;
  }

  if (isPaired && ledState == LED_STATE_PAIRING_BLINK) {
    ledState = LED_STATE_TRANSITION_TO_GREEN;
    ledStateStart = now;
  }

  if (ledState == LED_STATE_TRANSITION_TO_GREEN) {
    float progress = (float)(now - ledStateStart) / PAIRING_TO_GREEN_MS;
    if (progress >= 1.0f) {
      progress = 1.0f;
      ledState = LED_STATE_GREEN_HOLD;
      ledStateStart = now;
    }
    float eased = easeInOut(progress);
    uint8_t blue = (uint8_t)((1.0f - eased) * LED_BRIGHTNESS);
    uint8_t green = (uint8_t)(eased * LED_BRIGHTNESS);
    leds[0] = CRGB(0, green, blue);
    FastLED.show();
    return;
  }

  if (ledState == LED_STATE_GREEN_HOLD) {
    if (now - ledStateStart < PAIRED_GREEN_DURATION_MS) {
      leds[0] = CRGB(0, LED_BRIGHTNESS, 0);
      FastLED.show();
      return;
    }
    ledState = LED_STATE_GREEN_FADEOUT;
    ledStateStart = now;
  }

  if (ledState == LED_STATE_GREEN_FADEOUT) {
    float progress = (float)(now - ledStateStart) / GREEN_FADEOUT_MS;
    if (progress >= 1.0f) {
      progress = 1.0f;
      ledState = LED_STATE_OFF;
    }
    float eased = 1.0f - easeInOut(progress);
    uint8_t green = (uint8_t)(eased * LED_BRIGHTNESS);
    leds[0] = CRGB(0, green, 0);
    FastLED.show();
    return;
  }

  leds[0] = CRGB::Black;
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  setupLed();
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

  updateLedState();
}