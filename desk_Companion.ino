#include <WiFi.h>
#include "time.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// ================= CONFIG - EDIT THESE =================
const char* ssid       = "";
const char* password   = "";
const char* ntpServer  = "pool.ntp.org";
const long  gmtOffsetSec     = 19800; // IST = UTC + 5:30
const int   daylightOffsetSec = 0;

// ================= PIN DIAGRAM =================
// OLED (I2C, SSD1306 0.96")   -> ESP32
//   VCC  -> 3V3
//   GND  -> GND
//   SDA  -> GPIO 21
//   SCL  -> GPIO 22
//
// Push button                 -> ESP32
//   One leg -> GPIO 4
//   Other leg -> GND
//   (uses internal pullup, no external resistor needed)
//
// LED Left (ear/antenna)      -> ESP32
//   Anode(+) -> GPIO 25 (through ~220ohm resistor)
//   Cathode(-) -> GND
//
// LED Right (ear/antenna)     -> ESP32
//   Anode(+) -> GPIO 26 (through ~220ohm resistor)
//   Cathode(-) -> GND
//
// Battery -> ESP32 VIN/5V or 3V3 pin depending on your battery/board (check your board's regulator specs)

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define BUTTON_PIN 4
#define LED_LEFT_PIN  13
#define LED_RIGHT_PIN 12

// LEDC (PWM) channels for brightness control
#define LED_LEFT_CH  0
#define LED_RIGHT_CH 1
#define PWM_FREQ     5000
#define PWM_RES_BITS 8

Preferences prefs;

// ================= STATE =================
enum ViewState { REAL_TIME, STOPWATCH_NUMERIC, STOPWATCH_ANIMATION };
ViewState currentView = REAL_TIME;

bool swRunning = false;
unsigned long swElapsedMs = 0;     // total accumulated elapsed ms (frozen while paused)
unsigned long swStartMillis = 0;   // millis() timestamp of last resume

unsigned long lastInteractionMs = 0;
const unsigned long ANIMATION_TIMEOUT_MS = 10000;

unsigned long lastNvsSaveMs = 0;
const unsigned long NVS_SAVE_INTERVAL_MS = 10000;

int animLedBrightness = 0; // computed by drawTwoRabbits() each frame, consumed by updateLEDs()

// ================= BUTTON DETECTION =================
int lastRawState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_MS = 40;
int stableState = HIGH;

bool isPressed = false;
unsigned long pressStartMs = 0;
bool longPressFired = false;
const unsigned long LONG_PRESS_MS = 600;

int pendingClicks = 0;
unsigned long lastReleaseMs = 0;
const unsigned long DOUBLE_PRESS_WINDOW_MS = 300;

// ================= FORWARD DECLARATIONS =================
void connectWiFiAndSyncTime();
void handleButton();
void onShortPress();
void onLongPress();
void onDoublePress();
unsigned long getElapsedMs();
void saveStopwatchToNVS();
void loadStopwatchFromNVS();
void renderRealTime();
void renderStopwatchNumeric();
void renderStopwatchAnimation();
void updateLEDs();
void printCentered(const char* text, int textSize, int y);
struct RabbitPose { float x; float hopHeight; bool ascending; };
struct EarTips { int x1, y1, x2, y2; };
RabbitPose hopTravel(float tInPhase, float phaseDuration, int numHops, float startX, float endX);
EarTips drawRabbitShape(float xf, int groundY, float hopHeight, int dir, bool ascending, int blinkOverride, int earLeanOverride);
void drawTwoRabbits(unsigned long tMillis);

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  ledcSetup(LED_LEFT_CH, PWM_FREQ, PWM_RES_BITS);
  ledcAttachPin(LED_LEFT_PIN, LED_LEFT_CH);
  ledcSetup(LED_RIGHT_CH, PWM_FREQ, PWM_RES_BITS);
  ledcAttachPin(LED_RIGHT_PIN, LED_RIGHT_CH);
  ledcWrite(LED_LEFT_CH, 0);
  ledcWrite(LED_RIGHT_CH, 0);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed - check wiring/address");
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.display();

  prefs.begin("stopwatch", false);
  loadStopwatchFromNVS();

  connectWiFiAndSyncTime();
  lastInteractionMs = millis();
}

void loop() {
  handleButton();

  // 30s auto-timeout: only from NUMERIC view while RUNNING
  if (currentView == STOPWATCH_NUMERIC && swRunning) {
    if (millis() - lastInteractionMs > ANIMATION_TIMEOUT_MS) {
      currentView = STOPWATCH_ANIMATION;
    }
  }

  // periodic NVS save, only while running
  if (swRunning && millis() - lastNvsSaveMs > NVS_SAVE_INTERVAL_MS) {
    saveStopwatchToNVS();
    lastNvsSaveMs = millis();
  }

  switch (currentView) {
    case REAL_TIME:           renderRealTime(); break;
    case STOPWATCH_NUMERIC:   renderStopwatchNumeric(); break;
    case STOPWATCH_ANIMATION: renderStopwatchAnimation(); break;
  }

  updateLEDs();
  delay(20);
}

// ================= WIFI + NTP =================
void connectWiFiAndSyncTime() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);
    display.println("Time synced!");
  } else {
    display.println("WiFi failed.");
    display.println("Using boot time.");
  }
  display.display();
  delay(800);
}

// ================= BUTTON STATE MACHINE =================
void handleButton() {
  int raw = digitalRead(BUTTON_PIN); // LOW = pressed (INPUT_PULLUP)

  if (raw != lastRawState) {
    lastDebounceTime = millis();
  }

  if (millis() - lastDebounceTime > DEBOUNCE_MS) {
    if (raw != stableState) {
      stableState = raw;

      if (stableState == LOW) {
        // button just pressed
        isPressed = true;
        pressStartMs = millis();
        longPressFired = false;
      } else {
        // button just released
        isPressed = false;
        if (!longPressFired) {
          // count as a click, wait to see if it becomes a double click
          pendingClicks++;
          lastReleaseMs = millis();
        }
      }
    }
  }
  lastRawState = raw;

  // long press detection while held
  if (isPressed && !longPressFired) {
    if (millis() - pressStartMs >= LONG_PRESS_MS) {
      longPressFired = true;
      onLongPress();
    }
  }

  // resolve pending single/double click after the window closes
  if (pendingClicks > 0 && millis() - lastReleaseMs > DOUBLE_PRESS_WINDOW_MS) {
    if (pendingClicks == 1) {
      onShortPress();
    } else {
      onDoublePress();
    }
    pendingClicks = 0;
  }
}

void onShortPress() {
  lastInteractionMs = millis();
  if (currentView == REAL_TIME) {
    currentView = STOPWATCH_NUMERIC;
  } else if (currentView == STOPWATCH_NUMERIC) {
    currentView = REAL_TIME;
  } else if (currentView == STOPWATCH_ANIMATION) {
    currentView = STOPWATCH_NUMERIC;
  }
}

void onLongPress() {
  lastInteractionMs = millis();
  if (swRunning) {
    // pause
    swElapsedMs = getElapsedMs();
    swRunning = false;
    saveStopwatchToNVS();
  } else {
    // resume/start
    swStartMillis = millis();
    swRunning = true;
  }
}

void onDoublePress() {
  lastInteractionMs = millis();
  if (currentView == STOPWATCH_NUMERIC || currentView == STOPWATCH_ANIMATION) {
    swElapsedMs = 0;
    swRunning = false;
    saveStopwatchToNVS();
  }
}

// ================= STOPWATCH HELPERS =================
unsigned long getElapsedMs() {
  if (swRunning) {
    return swElapsedMs + (millis() - swStartMillis);
  }
  return swElapsedMs;
}

void saveStopwatchToNVS() {
  prefs.putULong("elapsed", getElapsedMs());
}

void loadStopwatchFromNVS() {
  swElapsedMs = prefs.getULong("elapsed", 0);
  swRunning = false; // always boot paused; long-press to resume
}

// ================= RENDERING =================
// draws text centered horizontally at a given y, sizing text first so getTextBounds is accurate
void printCentered(const char* text, int textSize, int y) {
  display.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - (int)w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

void renderRealTime() {
  struct tm timeinfo;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (getLocalTime(&timeinfo, 100)) {
    char buf[9];
    strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
    printCentered(buf, 2, 24);
  } else {
    printCentered("Time unavailable", 1, 28);
  }
  display.display();
}

void renderStopwatchNumeric() {
  unsigned long ms = getElapsedMs();
  unsigned long totalSec = ms / 1000;
  int hh = totalSec / 3600;
  int mm = (totalSec % 3600) / 60;
  int ss = totalSec % 60;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", hh, mm, ss);
  printCentered(buf, 2, 26);
  display.display();
}

// ---------------- SHARED RABBIT SHAPE ----------------
EarTips drawRabbitShape(float xf, int groundY, float hopHeight, int dir, bool ascending, int blinkOverride, int earLeanOverride) {
  int x = (int)roundf(xf);
  int y = groundY - (int)roundf(hopHeight) - 4;
  float s = hopHeight / 12.0f;
  int bodyRx = (int)roundf(7.5f - s * 1.8f);
  int bodyRy = (int)roundf(4.5f + s * 2.2f);
  if (bodyRx < 2) bodyRx = 2;
  if (bodyRy < 2) bodyRy = 2;

  // shadow (shrinks as the rabbit gets higher)
  int shadowW = (int)roundf(12.0f * (1.0f - s * 0.75f));
  if (shadowW < 2) shadowW = 2;
  display.drawFastHLine(x - shadowW / 2, groundY, shadowW, SSD1306_WHITE);

  // landing dust, only right near ground while descending
  if (!ascending && hopHeight < 2) {
    display.drawLine(x - 7, groundY - 2, x - 11, groundY - 5, SSD1306_WHITE);
    display.drawLine(x + 7, groundY - 2, x + 11, groundY - 5, SSD1306_WHITE);
  }

  // ears: trail back on launch, flop forward on landing (or forced lean during "meet")
  int headX = x + 6 * dir;
  int earLean = (earLeanOverride != -999) ? earLeanOverride
                : (ascending ? (-3 * dir) : (hopHeight < 3 ? 4 * dir : 1 * dir));
  int earBaseY = y - 6;
  int earTip1x = headX - 2 * dir + earLean;
  int earTip1y = earBaseY - 9;
  int earTip2x = headX + 1 * dir + earLean;
  int earTip2y = earBaseY - 9;
  display.drawLine(headX - 2 * dir, earBaseY, earTip1x, earTip1y, SSD1306_WHITE);
  display.drawLine(headX + 1 * dir, earBaseY, earTip2x, earTip2y, SSD1306_WHITE);

  // tail
  display.fillCircle(x - bodyRx * dir - dir, y + 1, 2, SSD1306_WHITE);

  // body (ellipse approximated with a rounded rect since GFX has no native ellipse)
  display.fillRoundRect(x - bodyRx, y - bodyRy, 2 * bodyRx, 2 * bodyRy, min(bodyRx, bodyRy), SSD1306_WHITE);

  // head
  display.fillCircle(headX, y - 5, 4, SSD1306_WHITE);

  // whiskers
  int snoutX = headX + 3 * dir;
  display.drawLine(snoutX, y - 5, snoutX + 3 * dir, y - 6, SSD1306_WHITE);
  display.drawLine(snoutX, y - 4, snoutX + 3 * dir, y - 4, SSD1306_WHITE);

  // eye: blinkOverride -1 = auto (time-based), 0 = force open, 1 = force closed
  bool blinking;
  if (blinkOverride == 1) blinking = true;
  else if (blinkOverride == 0) blinking = false;
  else blinking = (millis() % 2000) > 1880;

  if (blinking) {
    display.drawLine(headX + 1 * dir, y - 6, headX + 3 * dir, y - 6, SSD1306_WHITE);
  } else {
    display.fillCircle(headX + 2 * dir, y - 6, 1, SSD1306_WHITE);
  }

  // legs/paws: tucked airborne, extended near ground
  if (hopHeight < 3) {
    display.drawLine(x - 4, y + 3, x - 6, y + 8, SSD1306_WHITE);
    display.drawLine(x + 3, y + 3, x + 5, y + 8, SSD1306_WHITE);
    display.fillCircle(x - 6, y + 9, 1, SSD1306_WHITE);
    display.fillCircle(x + 5, y + 9, 1, SSD1306_WHITE);
  } else {
    display.drawLine(x - 3, y + 3, x - 4, y + 6, SSD1306_WHITE);
    display.drawLine(x + 2, y + 3, x + 3, y + 6, SSD1306_WHITE);
  }

  EarTips et;
  et.x1 = earTip1x; et.y1 = earTip1y;
  et.x2 = earTip2x; et.y2 = earTip2y;
  return et;
}

// advances x across a phase in discrete hops (each hop = one sine arc)
RabbitPose hopTravel(float tInPhase, float phaseDuration, int numHops, float startX, float endX) {
  float hopDur = phaseDuration / numHops;
  int hopIndex = (int)(tInPhase / hopDur);
  if (hopIndex >= numHops) hopIndex = numHops - 1;
  if (hopIndex < 0) hopIndex = 0;
  float localT = tInPhase - hopIndex * hopDur;
  float hopCycle = localT / hopDur;
  float hopHeight = sin(hopCycle * PI) * 12.0f;
  bool ascending = hopCycle < 0.5f;
  float xProgress = (hopIndex + hopCycle) / numHops;
  if (xProgress > 1.0f) xProgress = 1.0f;
  float x = startX + (endX - startX) * xProgress;
  RabbitPose p;
  p.x = x; p.hopHeight = hopHeight; p.ascending = ascending;
  return p;
}

// full scene: two rabbits hop toward each other, meet + ear-touch, hop back, idle at home.
// sets animLedBrightness each frame so updateLEDs() can mirror the on-screen energy.
void drawTwoRabbits(unsigned long tMillis) {
  const float APPROACH = 2200, MEET = 600, RETREAT = 2200, PAUSE = 1000;
  const float CYCLE = APPROACH + MEET + RETREAT + PAUSE;
  float tc = fmod((float)tMillis, CYCLE);
  const int groundY = 52;

  const float homeAx = 18, meetAx = 56;
  const float homeBx = 110, meetBx = 72;

  if (tc < APPROACH) {
    RabbitPose a = hopTravel(tc, APPROACH, 4, homeAx, meetAx);
    RabbitPose b = hopTravel(tc, APPROACH, 4, homeBx, meetBx);
    drawRabbitShape(a.x, groundY, a.hopHeight, 1, a.ascending, -1, -999);
    drawRabbitShape(b.x, groundY, b.hopHeight, -1, b.ascending, -1, -999);
    float mh = a.hopHeight > b.hopHeight ? a.hopHeight : b.hopHeight;
    animLedBrightness = (mh > 5) ? 235 : 60;

  } else if (tc < APPROACH + MEET) {
    float mt = tc - APPROACH;
    float idleCycle = fmod(mt, 300.0f) / 300.0f;
    float idleHop = sin(idleCycle * PI) * 3.0f;
    bool blink = mt > (MEET - 150);
    EarTips ra = drawRabbitShape(meetAx, groundY, idleHop, 1, idleCycle < 0.5f, blink ? 1 : 0, 5);
    EarTips rb = drawRabbitShape(meetBx, groundY, idleHop, -1, idleCycle < 0.5f, blink ? 1 : 0, -5);
    if (mt > 150 && mt < MEET - 150) {
      display.drawLine(ra.x1, ra.y1, rb.x1, rb.y1, SSD1306_WHITE); // ear-tips "touch" spark
    }
    animLedBrightness = 250;

  } else if (tc < APPROACH + MEET + RETREAT) {
    float rt = tc - APPROACH - MEET;
    RabbitPose a = hopTravel(rt, RETREAT, 4, meetAx, homeAx);
    RabbitPose b = hopTravel(rt, RETREAT, 4, meetBx, homeBx);
    drawRabbitShape(a.x, groundY, a.hopHeight, -1, a.ascending, -1, -999);
    drawRabbitShape(b.x, groundY, b.hopHeight, 1, b.ascending, -1, -999);
    float mh = a.hopHeight > b.hopHeight ? a.hopHeight : b.hopHeight;
    animLedBrightness = (mh > 5) ? 235 : 60;

  } else {
    float pt = tc - APPROACH - MEET - RETREAT;
    float idle = sin((pt / PAUSE) * PI) * 2.0f;
    drawRabbitShape(homeAx, groundY, idle, 1, true, -1, -999);
    drawRabbitShape(homeBx, groundY, idle, -1, true, -1, -999);
    animLedBrightness = 50;
  }
}

void renderStopwatchAnimation() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawTwoRabbits(millis());
  display.display();
}

// ================= LED SYNC =================
void updateLEDs() {
  if (currentView != STOPWATCH_ANIMATION) {
    ledcWrite(LED_LEFT_CH, 0);
    ledcWrite(LED_RIGHT_CH, 0);
    return;
  }
  // dim while hopping low, bright at the peak of a hop, brightest during the ear-touch "meet" moment
  ledcWrite(LED_LEFT_CH, animLedBrightness);
  ledcWrite(LED_RIGHT_CH, animLedBrightness);
}
