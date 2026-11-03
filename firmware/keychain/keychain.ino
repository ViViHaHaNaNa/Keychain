// Keychain display: animated eyes, clock, custom text, Spotify now-playing with album art.
// Libraries (Library Manager): Adafruit SSD1306, Adafruit GFX, WiFiManager (tzapu), ArduinoJson (v7).
// Board: "ESP32 Dev Module". OLED: SSD1306 128x64, I2C 0x3C, SDA=21, SCL=22.
//
// Networking runs in its own task so the animation never freezes while a request is in flight.
// Hold the BOOT button for 3 seconds to forget the saved Wi-Fi and reopen the setup hotspot.

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <Fonts/Picopixel.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <time.h>

#include "Silkscreen8pt7b.h"
#include "secrets.h"

// Optional in secrets.h: a second network (your phone hotspot) tried when the saved one fails.
#ifndef HOTSPOT_SSID
#define HOTSPOT_SSID ""
#define HOTSPOT_PASS ""
#endif
#ifndef TZ_OFFSET_SEC
#define TZ_OFFSET_SEC 19800  // IST (UTC+5:30); change in secrets.h for other zones
#endif

constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint32_t POLL_MS = 4000;
constexpr uint32_t RECONNECT_MS = 10000;
constexpr uint32_t FRAME_MS = 33;
constexpr uint8_t FAIL_LIMIT = 3;  // failed polls before falling back to eyes
constexpr size_t ART_BYTES = 512;  // 64x64, 1 bit per pixel

Adafruit_SSD1306 display(128, 64, &Wire, -1);
GFXcanvas1 marqueeLine(60, 8);

struct State {
  String mode = "eyes";
  String mood = "neutral";
  String text;
  String title;
  String artist;
  String art;
  int pos = -1;  // seconds into the track
  int dur = 0;   // track length in seconds
  bool play = false;
  uint32_t posAt = 0;  // millis() when pos was received
};

// Written by the network task, copied into the view* variables by the render loop.
SemaphoreHandle_t mu;
State shared;
bool sharedOnline = false;
uint32_t sharedVersion = 0;
uint8_t sharedArt[ART_BYTES];
bool sharedArtValid = false;
uint32_t sharedArtVersion = 0;

State view;
bool viewOnline = false;
uint32_t viewVersion = 0;
uint8_t viewArt[ART_BYTES];
bool viewArtValid = false;
uint32_t viewArtVersion = 0;

// ---------- small helpers ----------

void show(const String& l1, const String& l2 = "", const String& l3 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(l1);
  display.setCursor(0, 16);
  display.println(l2);
  display.setCursor(0, 32);
  display.println(l3);
  display.display();
}

// The built-in font only has ASCII/CP437; drop UTF-8 bytes so you don't get garbage glyphs.
String ascii(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    if ((uint8_t)s[i] < 0x7F) out += s[i];
  }
  return out;
}

// ---------- networking ----------

String url(const char* path) { return String(SITE_URL) + path + "?key=" + DEVICE_KEY; }

bool fetchState(State& s) {
  WiFiClientSecure client;
  client.setInsecure();  // hobby project: skips certificate checking
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url("/api/state"))) return false;

  bool ok = false;
  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) {
      s.mode = doc["mode"] | "eyes";
      s.mood = doc["mood"] | "neutral";
      s.text = ascii(doc["text"] | "");
      s.title = ascii(doc["title"] | "");
      s.artist = ascii(doc["artist"] | "");
      s.art = doc["art"] | "";
      s.pos = doc["pos"] | -1;
      s.dur = doc["dur"] | 0;
      s.play = (doc["play"] | 0) != 0;
      s.posAt = millis();
      ok = true;
    }
  }
  http.end();
  return ok;
}

bool fetchArt(uint8_t* out) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url("/api/art"))) return false;

  bool ok = false;
  if (http.GET() == 200) {
    String body = http.getString();  // handles chunked responses; binary-safe by length
    if (body.length() == ART_BYTES) {
      memcpy(out, body.c_str(), ART_BYTES);
      ok = true;
    }
  }
  http.end();
  return ok;
}

void netTask(void*) {
  String loadedArt;
  uint8_t fails = 0;
  uint8_t artBuf[ART_BYTES];

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      xSemaphoreTake(mu, portMAX_DELAY);
      sharedOnline = false;
      sharedVersion++;
      xSemaphoreGive(mu);
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(RECONNECT_MS));
      continue;
    }

    State s;
    bool ok = fetchState(s);
    bool gotArt = false, clearArt = false;
    if (ok && s.mode == "spotify") {
      if (s.art.length() == 0) {
        if (loadedArt.length()) {
          loadedArt = "";
          clearArt = true;
        }
      } else if (s.art != loadedArt && fetchArt(artBuf)) {
        loadedArt = s.art;
        gotArt = true;
      }
    }

    xSemaphoreTake(mu, portMAX_DELAY);
    if (ok) {
      shared = s;
      sharedOnline = true;
      fails = 0;
    } else if (fails < 255 && ++fails >= FAIL_LIMIT) {
      sharedOnline = false;
    }
    if (gotArt) {
      memcpy(sharedArt, artBuf, ART_BYTES);
      sharedArtValid = true;
      sharedArtVersion++;
    } else if (clearArt) {
      sharedArtValid = false;
      sharedArtVersion++;
    }
    sharedVersion++;
    xSemaphoreGive(mu);

    Serial.printf("poll ok=%d mode=%s mood=%s title=%s art=%s\n", ok, s.mode.c_str(), s.mood.c_str(),
                  s.title.c_str(), s.art.c_str());
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
}

bool waitConnected(uint32_t ms) {
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < ms) delay(250);
  return WiFi.status() == WL_CONNECTED;
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  show("Connecting...");

  WiFi.begin();  // last saved network
  if (waitConnected(10000)) return;

  if (strlen(HOTSPOT_SSID)) {
    show("Connecting...", HOTSPOT_SSID);
    WiFi.persistent(false);  // don't overwrite the saved home network
    WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);
    const bool ok = waitConnected(10000);
    WiFi.persistent(true);
    if (ok) return;
  }

  // Nothing worked: open the "Keychain-Setup" hotspot so a network can be picked from a phone.
  show("Wi-Fi setup:", "join Keychain-Setup", "then open 192.168.4.1");
  WiFiManager wm;
  wm.setConnectTimeout(15);
  wm.setConfigPortalTimeout(120);
  wm.autoConnect("Keychain-Setup");
}

void checkResetButton(uint32_t now) {
  static uint32_t downSince = 0;
  if (digitalRead(0) == LOW) {  // BOOT button
    if (!downSince) downSince = now;
    if (now - downSince > 3000) {
      show("Forgetting Wi-Fi...");
      WiFiManager wm;
      wm.resetSettings();
      delay(500);
      ESP.restart();
    }
  } else {
    downSince = 0;
  }
}

// ---------- drawing ----------

struct {
  float lookX = 0, lookY = 0, targetX = 0, targetY = 0;
  uint32_t nextLook = 0, nextBlink = 2000, blinkStart = 0;
  bool blinking = false;
} eyes;

void drawEyes(const String& mood, uint32_t now) {
  const bool surprised = mood == "surprised";
  const bool sleepy = mood == "sleepy";

  if (now >= eyes.nextLook) {
    eyes.targetX = random(-8, 9);
    eyes.targetY = random(-4, 5);
    if (surprised) {
      eyes.targetX /= 3;
      eyes.targetY /= 3;
    }
    eyes.nextLook = now + random(1200, 3500);
  }
  eyes.lookX += (eyes.targetX - eyes.lookX) * 0.25f;
  eyes.lookY += (eyes.targetY - eyes.lookY) * 0.25f;

  float open = 1.0f;
  if (!eyes.blinking && now >= eyes.nextBlink) {
    eyes.blinking = true;
    eyes.blinkStart = now;
  }
  if (eyes.blinking) {
    const float t = (now - eyes.blinkStart) / (sleepy ? 400.0f : 180.0f);
    if (t >= 1.0f) {
      eyes.blinking = false;
      eyes.nextBlink = now + random(2000, 6000);
    } else {
      open = fabsf(1.0f - 2.0f * t);
    }
  }

  const int w = surprised ? 40 : 36;
  const int h = surprised ? 48 : 40;
  const int he = max(4, (int)(h * open));
  const int r = min(10, he / 2);

  for (int e = 0; e < 2; e++) {
    const bool left = e == 0;
    const int cx = (left ? 34 : 94) + (int)eyes.lookX;
    const int x = cx - w / 2;
    const int y = 32 + (int)eyes.lookY - he / 2;
    display.fillRoundRect(x, y, w, he, r, SSD1306_WHITE);
    if (open < 0.6f) continue;  // lids only matter while the eye is open

    if (mood == "angry") {  // inner corners lower
      display.fillTriangle(x - 1, y - 1, x + w + 1, y - 1, left ? x + w + 1 : x - 1, y + he / 2, SSD1306_BLACK);
    } else if (mood == "sad") {  // outer corners lower
      display.fillTriangle(x - 1, y - 1, x + w + 1, y - 1, left ? x - 1 : x + w + 1, y + he / 2, SSD1306_BLACK);
    } else if (sleepy) {
      display.fillRect(x - 1, y - 1, w + 2, he / 2 + 1, SSD1306_BLACK);
    } else if (mood == "happy") {  // cheek pushes up from below: ^ ^ shape
      const int cr = (int)(w * 0.62f);
      display.fillCircle(cx, y + he - (int)(he * 0.38f) + cr, cr, SSD1306_BLACK);
    }
  }
}

void drawClock() {
  struct tm t;
  display.setTextColor(SSD1306_WHITE);
  if (!getLocalTime(&t, 0)) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Syncing time...");
    return;
  }
  char buf[6];
  snprintf(buf, sizeof buf, "%02d%c%02d", t.tm_hour, (t.tm_sec % 2) ? ' ' : ':', t.tm_min);
  display.setTextSize(4);
  display.setCursor(4, 6);
  display.print(buf);

  char date[16];
  strftime(date, sizeof date, "%a %d %b", &t);
  display.setTextSize(2);
  display.setCursor(4, 46);
  display.print(date);
}

void drawText(const String& text) {
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  if (text.length() == 0) {
    display.setTextSize(1);
    display.print("(no text set)");
    return;
  }
  display.setTextSize(text.length() <= 40 ? 2 : 1);
  display.print(text);
}

// Draws one line in the 60px column right of the art (x=68). Text shrinks to fit: Silkscreen
// first, then the smaller Picopixel. Only if it still doesn't fit does it scroll.
void fitLine(const String& s, int y, uint32_t now) {
  constexpr int W = 60;
  int16_t bx, by;
  uint16_t tw, th;

  marqueeLine.fillScreen(0);
  marqueeLine.setTextColor(1);
  marqueeLine.setTextSize(1);
  marqueeLine.setTextWrap(false);

  const int baseline = 7;  // GFX fonts draw from the baseline; caps sit on rows 2-6 of the 8px line
  marqueeLine.setFont(&Silkscreen8pt7b);
  marqueeLine.getTextBounds(s, 0, baseline, &bx, &by, &tw, &th);
  if (tw > W) {
    marqueeLine.setFont(&Picopixel);
    marqueeLine.getTextBounds(s, 0, baseline, &bx, &by, &tw, &th);
  }

  if (tw <= W) {
    marqueeLine.setCursor(0, baseline);
    marqueeLine.print(s);
  } else {
    const int span = tw + 16;
    const int off = (now / 60) % span;
    marqueeLine.setCursor(-off, baseline);
    marqueeLine.print(s);
    marqueeLine.setCursor(-off + span, baseline);
    marqueeLine.print(s);
  }
  display.drawBitmap(68, y, marqueeLine.getBuffer(), W, 8, SSD1306_WHITE, SSD1306_BLACK);
}

// 10 blocks across the bottom of the right column fill up as the song plays.
void drawProgress(uint32_t now) {
  float pos = view.pos < 0 ? 0.0f : (float)view.pos;
  if (view.play) pos += (now - view.posAt) / 1000.0f;
  const float frac = constrain(pos / view.dur, 0.0f, 1.0f);
  const int filled = (int)(frac * 10 + 0.5f);
  for (int i = 0; i < 10; i++) {
    if (i < filled) {
      display.fillRect(68 + i * 6, 54, 4, 6, SSD1306_WHITE);
    } else {
      display.drawRect(68 + i * 6, 54, 4, 6, SSD1306_WHITE);
    }
  }
}

void drawSpotify(uint32_t now) {
  display.setFont(nullptr);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (viewArtValid) {
    display.drawBitmap(0, 0, viewArt, 64, 64, SSD1306_WHITE);
  } else {
    display.drawRoundRect(0, 0, 64, 64, 4, SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(2, 16);
    display.print("ALBUM");
    display.setCursor(14, 34);
    display.print("ART");
    display.setTextSize(1);
  }

  display.drawChar(120, 2, 13, SSD1306_WHITE, SSD1306_BLACK, 1);  // note glyph (built-in font)
  display.setFont(&Silkscreen8pt7b);
  display.setCursor(68, 8);
  display.print("LISTENING");
  display.setFont(nullptr);

  fitLine(view.title, 20, now);
  if (view.artist.length()) fitLine(view.artist, 34, now);
  if (view.artist.length() && view.dur > 0) drawProgress(now);
}

// ---------- main ----------

void syncView() {
  if (xSemaphoreTake(mu, 0) != pdTRUE) return;
  if (sharedVersion != viewVersion) {
    view = shared;
    viewOnline = sharedOnline;
    viewVersion = sharedVersion;
  }
  if (sharedArtVersion != viewArtVersion) {
    memcpy(viewArt, sharedArt, ART_BYTES);
    viewArtValid = sharedArtValid;
    viewArtVersion = sharedArtVersion;
  }
  xSemaphoreGive(mu);
}

void setup() {
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found");
    for (;;) delay(1000);
  }
  Wire.setClock(400000);
  randomSeed(esp_random());
  mu = xSemaphoreCreateMutex();

  connectWifi();
  configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.google.com");
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
}

void loop() {
  static uint32_t lastFrame = 0;
  static String lastMood;
  const uint32_t now = millis();

  checkResetButton(now);
  if (now - lastFrame < FRAME_MS) {
    delay(1);
    return;
  }
  lastFrame = now;
  syncView();

  display.clearDisplay();
  if (viewOnline && view.mode == "clock") {
    drawClock();
  } else if (viewOnline && view.mode == "text") {
    drawText(view.text);
  } else if (viewOnline && view.mode == "spotify") {
    drawSpotify(now);
  } else {
    // eyes mode, or the offline/unknown fallback
    const String mood = viewOnline ? view.mood : String("neutral");
    if (mood != lastMood) {
      lastMood = mood;
      eyes.nextBlink = now;  // blink when the mood changes
    }
    drawEyes(mood, now);
  }
  display.display();
}
