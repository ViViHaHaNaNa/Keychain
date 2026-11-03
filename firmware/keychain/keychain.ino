// Stage 1: Wi-Fi + fetch + mode switch, with plain text on the OLED.
// Libraries (Library Manager): Adafruit SSD1306, Adafruit GFX, WiFiManager (tzapu), ArduinoJson (v7).
// Board: "ESP32 Dev Module". OLED: SSD1306 128x64, I2C 0x3C, SDA=21, SCL=22.

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Wire.h>

#include "secrets.h"

constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint32_t POLL_MS = 4000;
constexpr uint32_t RECONNECT_MS = 30000;
constexpr uint8_t FAIL_LIMIT = 3;  // failed polls before falling back to eyes

Adafruit_SSD1306 display(128, 64, &Wire, -1);

struct State {
  String mode = "eyes";
  String mood = "neutral";
  String text;
  String title;
  String artist;
  String art;
};

State state;
uint8_t failures = FAIL_LIMIT;  // start in fallback until the first fetch works
uint32_t lastPoll = 0;
uint32_t lastReconnect = 0;

void show(const String& l1, const String& l2 = "", const String& l3 = "", const String& l4 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  const String* lines[] = {&l1, &l2, &l3, &l4};
  for (int i = 0; i < 4; i++) {
    display.setCursor(0, i * 16);
    display.print(*lines[i]);
  }
  display.display();
}

bool fetchState() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();  // hobby project: skips certificate checking
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, String(SITE_URL) + "/api/state?key=" + DEVICE_KEY)) return false;

  bool ok = false;
  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) {
      state.mode = doc["mode"] | "eyes";
      state.mood = doc["mood"] | "neutral";
      state.text = doc["text"] | "";
      state.title = doc["title"] | "";
      state.artist = doc["artist"] | "";
      state.art = doc["art"] | "";
      ok = true;
    }
  }
  http.end();
  return ok;
}

void render() {
  // No Wi-Fi or repeated request failures: fall back to eyes.
  if (failures >= FAIL_LIMIT) {
    show("EYES", "(offline)");
    return;
  }
  if (state.mode == "eyes") {
    show("EYES", state.mood);
  } else if (state.mode == "clock") {
    show("CLOCK", "(coming soon)");
  } else if (state.mode == "text") {
    show(state.text);
  } else if (state.mode == "spotify") {
    show(state.title, state.artist);
  } else {
    show("EYES", "(unknown mode)");
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found");
    for (;;) delay(1000);
  }
  show("Connecting...");

  // Joins the saved network; otherwise opens a "Keychain-Setup" hotspot so you can
  // pick a network (e.g. your phone hotspot) from your phone. Gives up after 2 minutes.
  WiFiManager wm;
  wm.setConnectTimeout(15);
  wm.setConfigPortalTimeout(120);
  show("Wi-Fi setup:", "join Keychain-Setup", "on your phone");
  wm.autoConnect("Keychain-Setup");

  lastReconnect = millis();
}

void loop() {
  const uint32_t now = millis();

  if (WiFi.status() != WL_CONNECTED && now - lastReconnect >= RECONNECT_MS) {
    lastReconnect = now;
    WiFi.reconnect();
  }

  if (now - lastPoll >= POLL_MS || lastPoll == 0) {
    lastPoll = now;
    if (fetchState()) {
      failures = 0;
    } else if (failures < FAIL_LIMIT) {
      failures++;
    }
    Serial.printf("mode=%s mood=%s text=%s title=%s failures=%u\n", state.mode.c_str(),
                  state.mood.c_str(), state.text.c_str(), state.title.c_str(), failures);
    render();
  }
}
