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
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <WiFiMulti.h>
#include <Wire.h>
#include <esp_wifi.h>
#include <time.h>

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
WiFiMulti wifiMulti;
Preferences wifiPrefs;
GFXcanvas1 marqueeLine(60, 12);

struct State {
  String mode = "eyes";
  String mood = "neutral";
  String text;
  String title;
  String artist;
  String art;
  String anim = "none";
  String caption;
  int pos = -1;  // seconds into the track
  int dur = 0;   // track length in seconds
  bool play = false;
  uint32_t posAt = 0;  // millis() when pos was received
};

// Text styles from biggest to smallest. A line uses the first one it fits in. The classic 5x7
// font (font = nullptr) is the clearest on this screen; Picopixel is only the last resort.
struct Face {
  const GFXfont* font;
  uint8_t size;
};
const Face FACES[] = {{nullptr, 2}, {nullptr, 1}, {&Picopixel, 1}};
constexpr int FACE_COUNT = 3;

void useFace(Adafruit_GFX& g, const Face& f) {
  g.setFont(f.font);
  g.setTextSize(f.size);
}

// Cursor y for a baseline: the classic font draws from its top, GFX fonts from the baseline.
int faceY(const Face& f, int baseline) { return f.font ? baseline : baseline - 7 * f.size; }

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
      s.anim = doc["anim"] | "none";
      s.caption = ascii(doc["caption"] | "");
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
      wifiMulti.run(8000);  // rejoin whichever known network (home or hotspot) is in range
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

// The home network is remembered in our own storage, so adding the phone hotspot as a second
// network can never overwrite it. WiFiMulti joins whichever of the two is in range.
void connectWifi() {
  WiFi.mode(WIFI_STA);
  show("Connecting...");
  wifiPrefs.begin("wifi", false);

  String ssid = wifiPrefs.getString("ssid", "");
  String pass = wifiPrefs.getString("pass", "");
  if (ssid.length() == 0) {  // first boot of this version: adopt the network the radio has saved
    wifi_config_t conf;
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && conf.sta.ssid[0] &&
        strcmp((const char*)conf.sta.ssid, HOTSPOT_SSID) != 0) {
      ssid = (const char*)conf.sta.ssid;
      pass = (const char*)conf.sta.password;
      wifiPrefs.putString("ssid", ssid);
      wifiPrefs.putString("pass", pass);
    }
  }
  if (ssid.length()) wifiMulti.addAP(ssid.c_str(), pass.c_str());
  if (strlen(HOTSPOT_SSID)) wifiMulti.addAP(HOTSPOT_SSID, HOTSPOT_PASS);
  if (wifiMulti.run(15000) == WL_CONNECTED) return;

  // Nothing known is in range: open the "Keychain-Setup" hotspot so a network can be picked from a phone.
  show("Wi-Fi setup:", "join Keychain-Setup", "then open 192.168.4.1");
  WiFiManager wm;
  wm.setConnectTimeout(15);
  wm.setConfigPortalTimeout(120);
  if (wm.autoConnect("Keychain-Setup") && WiFi.SSID() != HOTSPOT_SSID) {
    wifiPrefs.putString("ssid", WiFi.SSID());
    wifiPrefs.putString("pass", WiFi.psk());
    wifiMulti.addAP(WiFi.SSID().c_str(), WiFi.psk().c_str());
  }
}

void checkResetButton(uint32_t now) {
  static uint32_t downSince = 0;
  if (digitalRead(0) == LOW) {  // BOOT button
    if (!downSince) downSince = now;
    if (now - downSince > 3000) {
      show("Forgetting Wi-Fi...");
      wifiPrefs.begin("wifi", false);
      wifiPrefs.clear();
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

// Centers one line across the whole screen, dropping to a smaller style if it's too wide.
void drawCentered(const String& s, int baseline) {
  int16_t bx, by;
  uint16_t tw = 0, th;
  int face = 0;
  for (; face < FACE_COUNT; face++) {
    useFace(display, FACES[face]);
    display.getTextBounds(s, 0, faceY(FACES[face], baseline), &bx, &by, &tw, &th);
    if (tw <= 128) break;
  }
  face = min(face, FACE_COUNT - 1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(max(0, (128 - (int)tw) / 2 - bx), faceY(FACES[face], baseline));
  display.print(s);
  display.setFont(nullptr);
  display.setTextSize(1);
}

// ---------- clock ----------

// Chunky 22x29 digits with 45-degree corner cuts, built from seven segments (a=1 ... g=64).
constexpr int DIGIT_W = 22, DIGIT_H = 29, STROKE = 5, CUT = 2;
const uint8_t SEGMENTS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
const char* const DAYS[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};

void cutRect(int x, int y, int w, int h, int cut = CUT) {
  const uint16_t c = SSD1306_WHITE;
  display.fillRect(x + cut, y, w - 2 * cut, h, c);
  display.fillRect(x, y + cut, w, h - 2 * cut, c);
  display.fillTriangle(x, y + cut, x + cut, y, x + cut, y + cut, c);
  display.fillTriangle(x + w - 1, y + cut, x + w - 1 - cut, y, x + w - 1 - cut, y + cut, c);
  display.fillTriangle(x, y + h - 1 - cut, x + cut, y + h - 1, x + cut, y + h - 1 - cut, c);
  display.fillTriangle(x + w - 1, y + h - 1 - cut, x + w - 1 - cut, y + h - 1, x + w - 1 - cut, y + h - 1 - cut, c);
}

void drawBigDigit(int x, int y, int d) {
  if (d == 1) {  // stem with a slanted flag and a base bar
    cutRect(x + 9, y, STROKE, DIGIT_H);
    display.fillTriangle(x + 9, y, x + 14, y, x + 8, y + 10, SSD1306_WHITE);
    display.fillTriangle(x + 8, y + 10, x + 3, y + 10, x + 9, y, SSD1306_WHITE);
    cutRect(x + 2, y + DIGIT_H - STROKE, DIGIT_W - 4, STROKE);
    return;
  }
  const uint8_t s = SEGMENTS[d];
  const int half = (DIGIT_H - STROKE) / 2;
  const int vh = half + STROKE;
  if (s & 1) cutRect(x, y, DIGIT_W, STROKE);                       // a: top
  if (s & 2) cutRect(x + DIGIT_W - STROKE, y, STROKE, vh);         // b: top right
  if (s & 4) cutRect(x + DIGIT_W - STROKE, y + half, STROKE, vh);  // c: bottom right
  if (s & 8) cutRect(x, y + DIGIT_H - STROKE, DIGIT_W, STROKE);    // d: bottom
  if (s & 16) cutRect(x, y + half, STROKE, vh);                    // e: bottom left
  if (s & 32) cutRect(x, y, STROKE, vh);                           // f: top left
  if (s & 64) cutRect(x, y + half, DIGIT_W, STROKE);               // g: middle
}

void drawClock(const String& caption) {
  struct tm t;
  if (!getLocalTime(&t, 0)) {
    drawCentered("SYNCING TIME", 34);
    return;
  }
  const int y = 3;
  drawBigDigit(10, y, t.tm_hour / 10);
  drawBigDigit(35, y, t.tm_hour % 10);
  cutRect(60, y + 6, 7, 7, 1);  // colon: two chunky dots
  cutRect(60, y + 16, 7, 7, 1);
  drawBigDigit(70, y, t.tm_min / 10);
  drawBigDigit(95, y, t.tm_min % 10);

  char date[32];
  snprintf(date, sizeof date, "%s  %d/%d/%02d", DAYS[t.tm_wday], t.tm_mday, t.tm_mon + 1, (t.tm_year + 1900) % 100);
  drawCentered(date, 45);
  if (caption.length()) drawCentered(caption, 61);
}

// ---------- full-screen text with animations ----------

constexpr int MAX_GLYPHS = 64;
struct Glyph {
  char c;
  int16_t x, y;
};
Glyph glyphs[MAX_GLYPHS];
int glyphCount = 0;
int glyphSize = 1;
String glyphsFor;

// Picks the largest font size (1-4) whose word-wrapped text fits the screen, centers it, and
// stores each letter's position so the animations can move letters individually.
void layoutText(const String& text) {
  if (text == glyphsFor) return;
  glyphsFor = text;
  glyphCount = 0;
  const int n = text.length();

  for (int size = 4; size >= 1; size--) {
    const int cw = 6 * size, ch = 8 * size;
    const int cols = 128 / cw, rows = 64 / ch;
    int lineStart[8], lineLen[8], lines = 0;

    for (int i = 0; i < n;) {
      while (i < n && text[i] == ' ') i++;
      if (i >= n) break;
      int len = min(cols, n - i);
      if (i + len < n && text[i + len] != ' ') {  // this would split a word: back up to a space
        int sp = len;
        while (sp > 0 && text[i + sp - 1] != ' ') sp--;
        if (sp > 0) len = sp;
      }
      int end = len;
      while (end > 0 && text[i + end - 1] == ' ') end--;
      if (lines < 8) {
        lineStart[lines] = i;
        lineLen[lines] = end;
      }
      lines++;
      i += len;
    }

    if (lines > rows && size > 1) continue;  // too big: try a smaller size
    const int used = min(min(lines, rows), 8);
    const int y0 = (64 - used * ch) / 2;
    for (int l = 0; l < used; l++) {
      const int x0 = (128 - lineLen[l] * cw) / 2;
      for (int k = 0; k < lineLen[l]; k++) {
        const char c = text[lineStart[l] + k];
        if (c != ' ' && glyphCount < MAX_GLYPHS) {
          glyphs[glyphCount++] = {c, (int16_t)(x0 + k * cw), (int16_t)(y0 + l * ch)};
        }
      }
    }
    glyphSize = size;
    return;
  }
}

void drawAnimText(const String& text, const String& anim, uint32_t now) {
  layoutText(text);
  if (glyphCount == 0) {
    drawCentered("NO TEXT SET", 34);
    return;
  }
  const int sz = glyphSize;

  const bool inverted = anim == "invert" && (now / 700) % 2 == 1;
  const uint16_t fg = inverted ? SSD1306_BLACK : SSD1306_WHITE;
  if (inverted) display.fillScreen(SSD1306_WHITE);

  int shown = glyphCount;
  if (anim == "typewriter") {
    const uint32_t step = 130;
    const uint32_t cycle = glyphCount * step + 1800;  // type it out, hold, start over
    shown = min((int)((now % cycle) / step), glyphCount);
  }

  static uint32_t nextGlitch = 0, glitchUntil = 0;
  bool glitching = false;
  if (anim == "glitch") {
    if (now >= nextGlitch) {
      glitchUntil = now + random(80, 240);
      nextGlitch = now + random(1000, 2800);
    }
    glitching = now < glitchUntil;
  }

  const int hop = sz == 1 ? 4 : (sz == 2 ? 6 : 8);
  for (int i = 0; i < shown; i++) {
    int x = glyphs[i].x, y = glyphs[i].y;
    char c = glyphs[i].c;
    if (anim == "wave") {
      y += (int)(sinf(now / 220.0f + i * 0.7f) * (1 + sz));
    } else if (anim == "bounce") {
      const float s = sinf(now / 200.0f - i * 0.5f);
      if (s > 0) y -= (int)(s * hop);
    } else if (glitching) {
      if (random(3) == 0) x += random(-4, 5);
      if (random(6) == 0) y += random(-2, 3);
      if (random(8) == 0) c = (char)random(33, 127);
    }
    display.drawChar(x, y, c, fg, fg, sz);
  }

  if (anim == "typewriter" && shown < glyphCount && (now / 400) % 2 == 0) {  // blinking cursor
    display.fillRect(glyphs[shown].x, glyphs[shown].y + 7 * sz, 5 * sz, sz, fg);
  }
  if (glitching) {
    for (int k = 0; k < 3; k++) {
      display.fillRect(random(0, 100), random(0, 64), random(12, 60), random(1, 4), SSD1306_INVERSE);
    }
  }
}

// Draws one line in the 60px column right of the art (x=68). Text shrinks to fit: normal size
// first, then the tiny Picopixel. Only if even that is too wide does it scroll. `bold` thickens
// the classic font by drawing it twice, one pixel apart, which reads better on the OLED.
void fitLine(const String& s, int y, uint32_t now, bool bold = false) {
  constexpr int W = 60;
  constexpr int BASELINE = 9;
  int16_t bx, by;
  uint16_t tw = 0, th;
  int face = 1;  // start at normal size: the double-size style is too wide for this column

  marqueeLine.fillScreen(0);
  marqueeLine.setTextColor(1);
  marqueeLine.setTextWrap(false);
  for (; face < FACE_COUNT; face++) {
    useFace(marqueeLine, FACES[face]);
    marqueeLine.getTextBounds(s, 0, faceY(FACES[face], BASELINE), &bx, &by, &tw, &th);
    if (tw <= W) break;
  }
  face = min(face, FACE_COUNT - 1);
  const int cy = faceY(FACES[face], BASELINE);
  const bool thick = bold && FACES[face].font == nullptr;

  auto draw = [&](int x) {
    marqueeLine.setCursor(x, cy);
    marqueeLine.print(s);
    if (thick) {
      marqueeLine.setCursor(x + 1, cy);
      marqueeLine.print(s);
    }
  };
  if ((int)tw + (thick ? 1 : 0) <= W) {
    draw(0);
  } else {
    const int span = tw + 16;
    const int off = (now / 60) % span;
    draw(-off);
    draw(-off + span);
  }
  display.drawBitmap(68, y, marqueeLine.getBuffer(), W, 12, SSD1306_WHITE, SSD1306_BLACK);
}

// Loading bar along the bottom of the right column: a thin track with a thick bar filling it.
void drawProgress(uint32_t now) {
  float pos = view.pos < 0 ? 0.0f : (float)view.pos;
  if (view.play) pos += (now - view.posAt) / 1000.0f;
  const float frac = constrain(pos / view.dur, 0.0f, 1.0f);
  constexpr int X = 68, Y = 56, W = 60, H = 4;
  display.drawFastHLine(X, Y + H / 2, W, SSD1306_WHITE);  // track, no border
  const int fill = (int)(W * frac + 0.5f);
  if (fill > 0) display.fillRect(X, Y, fill, H, SSD1306_WHITE);
}

// Thick eighth note, 6x10.
const uint8_t NOTE_ICON[] PROGMEM = {0x38, 0x3C, 0x34, 0x34, 0x30, 0x30, 0x70, 0xF0, 0xF0, 0x60};

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

  display.setCursor(68, 2);
  display.print("Listening");  // 54px wide, so the note fits right after it
  const int bob = (int)roundf(sinf(now / 260.0f) * 1.5f);  // the note bobs up and down
  display.drawBitmap(122, 1 + bob, NOTE_ICON, 6, 10, SSD1306_WHITE);

  fitLine(view.title, 16, now, true);
  if (view.artist.length()) fitLine(view.artist, 31, now);
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
    drawClock(view.caption);
  } else if (viewOnline && view.mode == "text") {
    drawAnimText(view.text, view.anim, now);
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
