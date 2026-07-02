#include <TFT_eSPI.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include "arduino_secrets.h"

TFT_eSPI tft = TFT_eSPI();

// --- WiFi + NTP ---
// SECRET_SSID / SECRET_PASSWORD come from arduino_secrets.h (gitignored — copy
// arduino_secrets.h.example to arduino_secrets.h and fill in your own WiFi credentials)
const char* ssid     = SECRET_SSID;
const char* password = SECRET_PASSWORD;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 28800, 60000);

// --- Pins ---
#define CLK_PIN  34
#define DT_PIN   35
#define SW_PIN   32
#define BUZZ_PIN 25

// --- Colors ---
#define PINK      0xF81F
#define DARK_PINK 0xC00F
#define BG        TFT_BLACK

// --- Screen ---
#define W  320
#define H  240
#define CX 160
#define CY 120

// --- App Modes ---
#define MODE_FACE      0
#define MODE_POMODORO  1
#define MODE_EDIT_POM  2
#define MODE_EDIT_TIME 3
#define MODE_TIMER     4
int appMode = MODE_FACE;

// --- Pomodoro States ---
#define STATE_IDLE    0
#define STATE_RUNNING 1
#define STATE_PAUSED  2
#define STATE_BREAK   3
#define STATE_DONE    4
int pomState = STATE_IDLE;

// --- Pomodoro Settings ---
int workMinutes = 25;
int shortBreak  = 5;
int longBreak   = 15;
#define SESSIONS_PER_CYCLE 4

// --- Edit Pomodoro ---
#define EDIT_WORK  0
#define EDIT_SHORT 1
#define EDIT_LONG  2
int editStep = EDIT_WORK;

// --- Manual Time Override ---
bool useManualTime = false;
int manualHour   = 0;
int manualMinute = 0;
int manualSecond = 0;
unsigned long manualTimeSetAt = 0; // millis() when manual time was set

// --- Time Edit Steps ---
#define EDIT_TIME_HOUR  0
#define EDIT_TIME_MIN   1
#define EDIT_TIME_AMPM  2
int timeEditStep = EDIT_TIME_HOUR;
int tempHour   = 12; // 1-12
int tempMinute = 0;
bool tempIsPM  = false;

// --- Session tracking ---
int currentSession = 1;
int remainingSeconds = 0;
unsigned long lastTick = 0;

// --- Pomodoro idle auto-return ---
#define POM_IDLE_TIMEOUT 15000UL
unsigned long pomIdleStart = 0;

// --- Simple Timer (2x click from home) ---
#define TSTATE_SET     0
#define TSTATE_RUNNING 1
#define TSTATE_PAUSED  2
#define TSTATE_DONE    3
int timerState = TSTATE_SET;

#define TIMER_EDIT_MIN 0
#define TIMER_EDIT_SEC 1
int timerEditStep = TIMER_EDIT_MIN;

int tempTimerMin = 5; // 0-99
int tempTimerSec = 0; // 0-59
int countdownRemaining = 0;
unsigned long lastCountdownTick = 0;

// --- Encoder ---
int lastCLK;
bool lastButton = HIGH;
unsigned long buttonPressTime = 0;
bool buttonHeld = false;

// --- Button debounce ---
#define DEBOUNCE_MS 30
bool lastRawButton = HIGH;
unsigned long lastDebounceTime = 0;

// --- Triple click ---
int clickCount = 0;
unsigned long lastClickTime = 0;
#define TRIPLE_CLICK_WINDOW 600

// --- Face refresh ---
unsigned long lastFaceUpdate = 0;
unsigned long lastBlink = 0;

// ==================== TIME HELPER ====================
// Returns current h, m, s — either NTP or manual
void getCurrentTime(int &h, int &m, int &s) {
  if (useManualTime) {
    // Calculate elapsed seconds since manual time was set
    unsigned long elapsed = (millis() - manualTimeSetAt) / 1000;
    int totalSeconds = manualHour * 3600 + manualMinute * 60 + manualSecond + elapsed;
    totalSeconds = totalSeconds % 86400; // wrap at 24hrs
    h = totalSeconds / 3600;
    m = (totalSeconds % 3600) / 60;
    s = totalSeconds % 60;
  } else {
    timeClient.update();
    h = timeClient.getHours();
    m = timeClient.getMinutes();
    s = timeClient.getSeconds();
  }
}

// Format to 12hr string
void formatTime12(int h, int m, int s, char* buf) {
  const char* period = (h >= 12) ? "PM" : "AM";
  if (h > 12) h -= 12;
  if (h == 0) h = 12;
  sprintf(buf, "%02d:%02d:%02d %s", h, m, s, period);
}

// --- Date (always from NTP epoch — manual time only overrides the clock, not the date) ---
const char* dayNames[7]   = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* monthNames[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void formatDate(char* buf) {
  time_t rawtime = timeClient.getEpochTime();
  struct tm* ti = gmtime(&rawtime);
  sprintf(buf, "%s, %s %d", dayNames[ti->tm_wday], monthNames[ti->tm_mon], ti->tm_mday);
}

// ==================== SETUP ====================
void setup() {
  pinMode(CLK_PIN, INPUT);
  pinMode(DT_PIN, INPUT);
  pinMode(SW_PIN, INPUT_PULLUP);
  ledcAttach(BUZZ_PIN, 2000, 8);
  lastCLK = digitalRead(CLK_PIN);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(BG);

  tft.setTextColor(PINK, BG);
  tft.setTextSize(2);
  tft.setCursor(30, CY - 10);
  tft.println("Connecting to WiFi...");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  tft.fillScreen(BG);
  tft.setTextColor(PINK, BG);
  tft.setTextSize(2);
  tft.setCursor(70, CY - 10);
  tft.println("WiFi Connected!");
  delay(1000);

  timeClient.begin();
  timeClient.update();

  drawFaceScreen(false);
}

// ==================== LOOP ====================
void loop() {
  handleEncoder();
  handleButton();

  // Reset click counter if window expired — a stray count of exactly 2 means
  // a double click (not followed by a 3rd click) → open the simple timer
  if (clickCount > 0 && millis() - lastClickTime > TRIPLE_CLICK_WINDOW) {
    if (clickCount == 2 && appMode == MODE_FACE) {
      appMode = MODE_TIMER;
      timerState = TSTATE_SET;
      timerEditStep = TIMER_EDIT_MIN;
      drawTimerSetScreen();
    }
    clickCount = 0;
  }

  if (appMode == MODE_FACE) {
    if (millis() - lastFaceUpdate >= 1000) {
      lastFaceUpdate = millis();

      if (millis() - lastBlink > 4000) {
        lastBlink = millis();
        drawFaceScreen(true);
        delay(600);
        drawFaceScreen(false);
      } else {
        updateFaceTime();
      }
    }
  }

  if (appMode == MODE_POMODORO && pomState == STATE_IDLE) {
    if (millis() - pomIdleStart >= POM_IDLE_TIMEOUT) {
      appMode = MODE_FACE;
      drawFaceScreen(false);
    }
  }

  if (appMode == MODE_POMODORO &&
     (pomState == STATE_RUNNING || pomState == STATE_BREAK)) {
    if (millis() - lastTick >= 1000) {
      lastTick = millis();
      remainingSeconds--;

      if (remainingSeconds <= 0) {
        buzzAlert();

        if (pomState == STATE_RUNNING) {
          pomState = STATE_BREAK;
          remainingSeconds = (currentSession >= SESSIONS_PER_CYCLE)
                             ? longBreak * 60 : shortBreak * 60;
          lastTick = millis();
          drawBreakScreen(currentSession >= SESSIONS_PER_CYCLE);

        } else if (pomState == STATE_BREAK) {
          if (currentSession >= SESSIONS_PER_CYCLE) {
            currentSession = 1;
            pomState = STATE_DONE;
            drawDoneScreen();
          } else {
            currentSession++;
            pomState = STATE_RUNNING;
            remainingSeconds = workMinutes * 60;
            lastTick = millis();
            drawTimerScreen();
          }
        }

      } else {
        if (pomState == STATE_RUNNING) drawTimerScreen();
        else drawBreakScreen(currentSession >= SESSIONS_PER_CYCLE);
      }
    }
  }

  if (appMode == MODE_TIMER && timerState == TSTATE_RUNNING) {
    if (millis() - lastCountdownTick >= 1000) {
      lastCountdownTick = millis();
      countdownRemaining--;

      if (countdownRemaining <= 0) {
        countdownRemaining = 0;
        buzzAlert();
        timerState = TSTATE_DONE;
        drawCountdownDoneScreen();
      } else {
        drawCountdownScreen();
      }
    }
  }
}

// ==================== ENCODER ====================
void handleEncoder() {
  int currentCLK = digitalRead(CLK_PIN);
  if (currentCLK != lastCLK && currentCLK == LOW) {
    int delta = (digitalRead(DT_PIN) != currentCLK) ? 1 : -1;

    if (appMode == MODE_EDIT_POM) {
      if (editStep == EDIT_WORK)  workMinutes = constrain(workMinutes + delta, 1, 60);
      if (editStep == EDIT_SHORT) shortBreak  = constrain(shortBreak  + delta, 1, 30);
      if (editStep == EDIT_LONG)  longBreak   = constrain(longBreak   + delta, 1, 60);
      drawEditPomScreen();
    }

    if (appMode == MODE_EDIT_TIME) {
      if (timeEditStep == EDIT_TIME_HOUR) {
        tempHour += delta;
        if (tempHour > 12) tempHour = 1;
        if (tempHour < 1)  tempHour = 12;
      } else if (timeEditStep == EDIT_TIME_MIN) {
        tempMinute = (tempMinute + delta + 60) % 60;
      } else {
        tempIsPM = !tempIsPM;
      }
      drawEditTimeScreen();
    }

    if (appMode == MODE_TIMER && timerState == TSTATE_SET) {
      if (timerEditStep == TIMER_EDIT_MIN) {
        tempTimerMin = constrain(tempTimerMin + delta, 0, 99);
      } else {
        tempTimerSec = (tempTimerSec + delta + 60) % 60;
      }
      drawTimerSetScreen();
    }
  }
  lastCLK = currentCLK;
}

// ==================== BUTTON ====================
void handleButton() {
  bool raw = digitalRead(SW_PIN);
  if (raw != lastRawButton) {
    lastDebounceTime = millis();
    lastRawButton = raw;
  }
  if (millis() - lastDebounceTime < DEBOUNCE_MS) return;

  bool currentButton = raw;

  if (currentButton == LOW && lastButton == HIGH) {
    buttonPressTime = millis();
    buttonHeld = false;
  }

  if (currentButton == LOW && !buttonHeld) {
    if (millis() - buttonPressTime >= 700) {
      buttonHeld = true;
      handleLongPress();
    }
  }

  if (currentButton == HIGH && lastButton == LOW) {
    if (!buttonHeld) handleShortPress();
  }

  lastButton = currentButton;
}

void handleShortPress() {
  unsigned long now = millis();

  // --- FACE MODE: triple click detection ---
  if (appMode == MODE_FACE) {
    if (now - lastClickTime <= TRIPLE_CLICK_WINDOW) {
      clickCount++;
    } else {
      clickCount = 1;
    }
    lastClickTime = now;

    if (clickCount >= 3) {
      clickCount = 0;
      appMode = MODE_POMODORO;
      pomState = STATE_IDLE;
      currentSession = 1;
      remainingSeconds = 0;
      pomIdleStart = millis();
      drawIdleScreen();
    }
    return;
  }

  // --- TIME EDIT MODE ---
  if (appMode == MODE_EDIT_TIME) {
    if (timeEditStep == EDIT_TIME_HOUR) {
      timeEditStep = EDIT_TIME_MIN;
      drawEditTimeScreen();
    } else if (timeEditStep == EDIT_TIME_MIN) {
      timeEditStep = EDIT_TIME_AMPM;
      drawEditTimeScreen();
    } else {
      // Save manual time, converting 12hr + AM/PM back to 24hr.
      // Seconds keep running from whatever they currently are — only hour/minute get set.
      int h24 = tempHour % 12;
      if (tempIsPM) h24 += 12;

      int curH, curM, curS;
      getCurrentTime(curH, curM, curS);

      manualHour   = h24;
      manualMinute = tempMinute;
      manualSecond = curS;
      manualTimeSetAt = millis();
      useManualTime = true;
      appMode = MODE_FACE;
      drawFaceScreen(false);
    }
    return;
  }

  // --- POMODORO EDIT MODE ---
  if (appMode == MODE_EDIT_POM) {
    editStep++;
    if (editStep > EDIT_LONG) {
      editStep = EDIT_WORK;
      appMode = MODE_POMODORO;
      pomState = STATE_IDLE;
      pomIdleStart = millis();
      drawIdleScreen();
    } else {
      drawEditPomScreen();
    }
    return;
  }

  // --- POMODORO MODE ---
  if (appMode == MODE_POMODORO) {
    // Triple click anywhere in pomodoro mode → back to face
    if (now - lastClickTime <= TRIPLE_CLICK_WINDOW) {
      clickCount++;
    } else {
      clickCount = 1;
    }
    lastClickTime = now;

    if (clickCount >= 3) {
      clickCount = 0;
      appMode = MODE_FACE;
      pomState = STATE_IDLE;
      currentSession = 1;
      remainingSeconds = 0;
      drawFaceScreen(false);
      return;
    }

    if (pomState == STATE_IDLE) {
      currentSession = 1;
      remainingSeconds = workMinutes * 60;
      pomState = STATE_RUNNING;
      lastTick = millis();
      drawTimerScreen();

    } else if (pomState == STATE_RUNNING || pomState == STATE_BREAK) {
      pomState = STATE_PAUSED;
      drawPausedScreen();

    } else if (pomState == STATE_PAUSED) {
      pomState = STATE_RUNNING;
      lastTick = millis();
      drawTimerScreen();

    } else if (pomState == STATE_DONE) {
      currentSession = 1;
      pomState = STATE_IDLE;
      pomIdleStart = millis();
      drawIdleScreen();
    }
    return;
  }

  // --- SIMPLE TIMER MODE ---
  if (appMode == MODE_TIMER) {
    // Triple click anywhere in timer mode → back to face
    if (now - lastClickTime <= TRIPLE_CLICK_WINDOW) {
      clickCount++;
    } else {
      clickCount = 1;
    }
    lastClickTime = now;

    if (clickCount >= 3) {
      clickCount = 0;
      appMode = MODE_FACE;
      timerState = TSTATE_SET;
      drawFaceScreen(false);
      return;
    }

    if (timerState == TSTATE_SET) {
      if (timerEditStep == TIMER_EDIT_MIN) {
        timerEditStep = TIMER_EDIT_SEC;
        drawTimerSetScreen();
      } else {
        int total = tempTimerMin * 60 + tempTimerSec;
        if (total <= 0) total = 1;
        countdownRemaining = total;
        timerState = TSTATE_RUNNING;
        lastCountdownTick = millis();
        drawCountdownScreen();
      }

    } else if (timerState == TSTATE_RUNNING) {
      timerState = TSTATE_PAUSED;
      drawCountdownPausedScreen();

    } else if (timerState == TSTATE_PAUSED) {
      timerState = TSTATE_RUNNING;
      lastCountdownTick = millis();
      drawCountdownScreen();

    } else if (timerState == TSTATE_DONE) {
      timerEditStep = TIMER_EDIT_MIN;
      timerState = TSTATE_SET;
      drawTimerSetScreen();
    }
  }
}

void handleLongPress() {
  // Face → enter time edit mode
  if (appMode == MODE_FACE) {
    int h, m, s;
    getCurrentTime(h, m, s);
    tempIsPM = (h >= 12);
    tempHour = h % 12;
    if (tempHour == 0) tempHour = 12;
    tempMinute = m;
    timeEditStep = EDIT_TIME_HOUR;
    appMode = MODE_EDIT_TIME;
    drawEditTimeScreen();
    return;
  }

  // Pomodoro idle → enter pomodoro edit mode
  if (appMode == MODE_POMODORO && pomState == STATE_IDLE) {
    editStep = EDIT_WORK;
    appMode = MODE_EDIT_POM;
    drawEditPomScreen();
    return;
  }
}

// ==================== BUZZER ====================
void buzzAlert() {
  for (int i = 0; i < 3; i++) {
    ledcWriteTone(BUZZ_PIN, 3000);
    delay(400);
    ledcWriteTone(BUZZ_PIN, 0);
    delay(200);
  }
}

// ==================== HELPERS ====================
void printCentered(const char* text, int y, int size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color, BG);
  int textW = strlen(text) * 6 * size;
  tft.setCursor(CX - textW / 2, y);
  tft.print(text);
}

void drawHeart(int cx, int cy, int size, uint16_t color) {
  int r = size / 2;
  tft.fillCircle(cx - r, cy, r, color);
  tft.fillCircle(cx + r, cy, r, color);
  tft.fillTriangle(
    cx - size, cy,
    cx + size, cy,
    cx,        cy + size,
    color
  );
}

void drawHeartOutline(int cx, int cy, int size, uint16_t color) {
  int r = size / 2;
  tft.drawCircle(cx - r, cy, r, color);
  tft.drawCircle(cx + r, cy, r, color);
  tft.drawTriangle(
    cx - size, cy,
    cx + size, cy,
    cx,        cy + size,
    color
  );
}

void drawSessionDots(int session) {
  int spacing = 26;
  int totalW  = SESSIONS_PER_CYCLE * spacing;
  int startX  = CX - totalW / 2 + 4;
  int y = 214;
  int heartSize = 8;
  for (int i = 1; i <= SESSIONS_PER_CYCLE; i++) {
    int x = startX + (i - 1) * spacing;
    if (i < session)       drawHeart(x, y, heartSize, PINK);
    else if (i == session) drawHeart(x, y, heartSize, TFT_WHITE);
    else                   drawHeartOutline(x, y, heartSize, DARK_PINK);
  }
}


// ==================== FACE SCREEN ====================
void drawFaceScreen(bool blinking) {
  tft.fillScreen(BG);

  printCentered("*:. pomi .:*", 12, 2, PINK);

  char dateBuf[16];
  formatDate(dateBuf);
  printCentered(dateBuf, 30, 1, DARK_PINK);

  if (blinking) {
    tft.fillRect(78,  97, 22, 4, PINK);
    tft.fillRect(218, 97, 22, 4, PINK);
  } else {
    tft.fillRect(78,  88, 22, 22, PINK);
    tft.fillRect(218, 88, 22, 22, PINK);
  }

  // Retro smile
  tft.fillRect(130, 158, 60, 8, PINK);
  tft.fillRect(118, 148, 12, 10, PINK);
  tft.fillRect(190, 148, 12, 10, PINK);

  // Time
  int h, m, s;
  getCurrentTime(h, m, s);
  char timeBuf[15];
  formatTime12(h, m, s, timeBuf);
  tft.setTextSize(2);
  tft.setTextColor(PINK, BG);
  int textW = strlen(timeBuf) * 6 * 2;
  tft.setCursor(CX - textW / 2, 195);
  tft.print(timeBuf);

  // Mode indicator
  tft.setTextSize(1);
  tft.setTextColor(DARK_PINK, BG);
  tft.setCursor(CX - 45, 218);
  tft.print(useManualTime ? "manual time" : "NTP synced");

  printCentered("x2: timer | x3: pomodoro | hold: set time", 228, 1, DARK_PINK);
}

// Redraws only the clock text so the rest of the home screen doesn't flicker every second
void updateFaceTime() {
  int h, m, s;
  getCurrentTime(h, m, s);
  char timeBuf[15];
  formatTime12(h, m, s, timeBuf);

  int textW = strlen(timeBuf) * 6 * 2;
  int x = CX - textW / 2;

  tft.setTextSize(2);
  tft.setCursor(x, 195);
  tft.setTextColor(PINK, BG);
  tft.print(timeBuf);
}

// ==================== TIME EDIT SCREEN ====================
void drawEditTimeScreen() {
  tft.fillScreen(BG);

  printCentered("SET TIME", 20, 3, PINK);

  // Hour / colon / minute, sized so none of the cells overlap
  const int charW  = 6 * 6; // font cell width at text size 6
  const int hourX   = CX - (2 * charW + charW + 2 * charW) / 2;
  const int colonX  = hourX + 2 * charW;
  const int minuteX = colonX + charW;

  char hourBuf[10];
  sprintf(hourBuf, "%02d", tempHour);
  tft.setTextSize(6);
  tft.setTextColor(timeEditStep == EDIT_TIME_HOUR ? TFT_WHITE : DARK_PINK, BG);
  tft.setCursor(hourX, 90);
  tft.print(hourBuf);

  // Separator
  tft.setTextColor(PINK, BG);
  tft.setCursor(colonX, 90);
  tft.print(":");

  // Minute
  char minBuf[10];
  sprintf(minBuf, "%02d", tempMinute);
  tft.setTextColor(timeEditStep == EDIT_TIME_MIN ? TFT_WHITE : DARK_PINK, BG);
  tft.setCursor(minuteX, 90);
  tft.print(minBuf);

  // AM/PM
  const int ampmX = minuteX + 2 * charW + 8;
  tft.setTextSize(3);
  tft.setTextColor(timeEditStep == EDIT_TIME_AMPM ? TFT_WHITE : DARK_PINK, BG);
  tft.setCursor(ampmX, 102);
  tft.print(tempIsPM ? "PM" : "AM");

  // Labels
  if (timeEditStep == EDIT_TIME_HOUR) {
    printCentered("Rotate: change hour", 170, 2, PINK);
    printCentered("Press: confirm hour", 195, 2, PINK);
  } else if (timeEditStep == EDIT_TIME_MIN) {
    printCentered("Rotate: change minute", 170, 2, PINK);
    printCentered("Press: confirm minute", 195, 2, PINK);
  } else {
    printCentered("Rotate: toggle AM/PM", 170, 2, PINK);
    printCentered("Press: save & exit", 195, 2, PINK);
  }

  // Arrow indicator
  tft.setTextColor(PINK, BG);
  tft.setTextSize(2);
  int arrowX = (timeEditStep == EDIT_TIME_HOUR) ? hourX
             : (timeEditStep == EDIT_TIME_MIN)  ? minuteX
             : ampmX;
  tft.setCursor(arrowX, 148);
  tft.print("^");
}

// ==================== POMODORO EDIT SCREEN ====================
void drawEditPomScreen() {
  tft.fillScreen(BG);

  printCentered("EDIT TIMERS", 15, 2, PINK);

  char buf[30];
  sprintf(buf, "Work:  %2d min", workMinutes);
  printCentered(buf, 65, 2, editStep == EDIT_WORK ? TFT_WHITE : DARK_PINK);

  sprintf(buf, "Short: %2d min", shortBreak);
  printCentered(buf, 105, 2, editStep == EDIT_SHORT ? TFT_WHITE : DARK_PINK);

  sprintf(buf, "Long:  %2d min", longBreak);
  printCentered(buf, 145, 2, editStep == EDIT_LONG ? TFT_WHITE : DARK_PINK);

  int arrowY = 65 + (editStep * 40);
  tft.setTextColor(PINK, BG);
  tft.setTextSize(2);
  tft.setCursor(30, arrowY);
  tft.print(">");

  printCentered("Rotate to change", 185, 2, PINK);
  printCentered("Press to confirm & next", 210, 1, DARK_PINK);
}

// ==================== POMODORO SCREENS ====================
void drawIdleScreen() {
  tft.fillScreen(BG);

  printCentered("*:. pomi .:*", 15, 2, PINK);

  char dateBuf[16];
  formatDate(dateBuf);
  printCentered(dateBuf, 33, 1, DARK_PINK);

  drawHeart(CX, 90, 35, DARK_PINK);
  drawHeart(CX - 72, 100, 20, PINK);
  drawHeart(CX + 72, 100, 20, PINK);

  char buf[30];
  sprintf(buf, "%dm | %dm | %dm", workMinutes, shortBreak, longBreak);
  printCentered(buf, 145, 2, TFT_WHITE);
  printCentered("Press: start", 172, 2, PINK);
  printCentered("Hold: edit timers", 197, 1, DARK_PINK);
}

void drawTimerScreen() {
  int mins = remainingSeconds / 60;
  int secs = remainingSeconds % 60;

  tft.fillScreen(BG);

  printCentered("*:. pomi .:*", 8, 2, PINK);

  char dateBuf[16];
  formatDate(dateBuf);
  printCentered(dateBuf, 48, 1, DARK_PINK);

  printCentered("WORK", 64, 2, TFT_WHITE);

  char timeBuf[6];
  sprintf(timeBuf, "%02d:%02d", mins, secs);
  tft.setTextSize(6);
  tft.setTextColor(TFT_WHITE, BG);
  int textW = 5 * 6 * 6;
  tft.setCursor(CX - textW / 2, 88);
  tft.print(timeBuf);

  int h, m, s;
  getCurrentTime(h, m, s);
  char clockBuf[15];
  formatTime12(h, m, s, clockBuf);
  printCentered(clockBuf, 145, 2, PINK);

  printCentered("Press to pause", 172, 2, PINK);
  drawSessionDots(currentSession);
}

void drawBreakScreen(bool isLong) {
  int mins = remainingSeconds / 60;
  int secs = remainingSeconds % 60;

  tft.fillScreen(BG);

  if (isLong) {
    printCentered("LONG BREAK!", 15, 2, PINK);
    printCentered("You earned it!", 42, 2, TFT_WHITE);
  } else {
    char sessionLabel[20];
    sprintf(sessionLabel, "SESSION %d / %d", currentSession, SESSIONS_PER_CYCLE);
    printCentered(sessionLabel, 15, 2, PINK);
    printCentered("SHORT BREAK", 42, 2, TFT_WHITE);
  }

  char timeBuf[6];
  sprintf(timeBuf, "%02d:%02d", mins, secs);
  tft.setTextSize(6);
  tft.setTextColor(PINK, BG);
  int textW = 5 * 6 * 6;
  tft.setCursor(CX - textW / 2, 88);
  tft.print(timeBuf);

  printCentered("Press to pause", 170, 2, PINK);
  drawSessionDots(currentSession);
}

void drawPausedScreen() {
  int mins = remainingSeconds / 60;
  int secs = remainingSeconds % 60;

  tft.fillScreen(BG);

  printCentered("PAUSED", 20, 3, PINK);

  char timeBuf[6];
  sprintf(timeBuf, "%02d:%02d", mins, secs);
  tft.setTextSize(6);
  tft.setTextColor(TFT_WHITE, BG);
  int textW = 5 * 6 * 6;
  tft.setCursor(CX - textW / 2, 88);
  tft.print(timeBuf);

  printCentered("Press to resume", 170, 2, PINK);
  drawSessionDots(currentSession);
}

void drawDoneScreen() {
  tft.fillScreen(BG);

  printCentered("*:. ruka .:*", 15, 2, PINK);

  char dateBuf[16];
  formatDate(dateBuf);
  printCentered(dateBuf, 33, 1, DARK_PINK);

  printCentered("CYCLE COMPLETE!", 50, 2, TFT_WHITE);

  drawHeart(CX - 68, 125, 22, PINK);
  drawHeart(CX,      110, 30, PINK);
  drawHeart(CX + 68, 125, 22, PINK);

  printCentered("Amazing work!", 162, 2, TFT_WHITE);
  printCentered("Press to restart", 190, 2, PINK);
}

// ==================== SIMPLE TIMER SCREENS ====================
void drawTimerSetScreen() {
  tft.fillScreen(BG);

  printCentered("SET TIMER", 20, 3, PINK);

  // Minutes / colon / seconds, laid out the same way as the clock's SET TIME screen
  const int charW = 6 * 6; // font cell width at text size 6
  const int minX  = CX - (2 * charW + charW + 2 * charW) / 2;
  const int colonX = minX + 2 * charW;
  const int secX   = colonX + charW;

  char minBuf[10];
  sprintf(minBuf, "%02d", tempTimerMin);
  tft.setTextSize(6);
  tft.setTextColor(timerEditStep == TIMER_EDIT_MIN ? TFT_WHITE : DARK_PINK, BG);
  tft.setCursor(minX, 90);
  tft.print(minBuf);

  tft.setTextColor(PINK, BG);
  tft.setCursor(colonX, 90);
  tft.print(":");

  char secBuf[10];
  sprintf(secBuf, "%02d", tempTimerSec);
  tft.setTextColor(timerEditStep == TIMER_EDIT_SEC ? TFT_WHITE : DARK_PINK, BG);
  tft.setCursor(secX, 90);
  tft.print(secBuf);

  if (timerEditStep == TIMER_EDIT_MIN) {
    printCentered("Rotate: change minutes", 170, 2, PINK);
    printCentered("Press: confirm minutes", 195, 2, PINK);
  } else {
    printCentered("Rotate: change seconds", 170, 2, PINK);
    printCentered("Press: start countdown", 195, 2, PINK);
  }

  tft.setTextColor(PINK, BG);
  tft.setTextSize(2);
  int arrowX = (timerEditStep == TIMER_EDIT_MIN) ? minX : secX;
  tft.setCursor(arrowX, 148);
  tft.print("^");
}

void drawCountdownScreen() {
  int mins = countdownRemaining / 60;
  int secs = countdownRemaining % 60;

  tft.fillScreen(BG);

  printCentered("TIMER", 42, 2, TFT_WHITE);

  char timeBuf[6];
  sprintf(timeBuf, "%02d:%02d", mins, secs);
  tft.setTextSize(6);
  tft.setTextColor(TFT_WHITE, BG);
  int textW = 5 * 6 * 6;
  tft.setCursor(CX - textW / 2, 88);
  tft.print(timeBuf);

  int h, m, s;
  getCurrentTime(h, m, s);
  char clockBuf[15];
  formatTime12(h, m, s, clockBuf);
  printCentered(clockBuf, 145, 2, PINK);

  printCentered("Press to pause", 172, 2, PINK);
}

void drawCountdownPausedScreen() {
  int mins = countdownRemaining / 60;
  int secs = countdownRemaining % 60;

  tft.fillScreen(BG);

  printCentered("PAUSED", 20, 3, PINK);

  char timeBuf[6];
  sprintf(timeBuf, "%02d:%02d", mins, secs);
  tft.setTextSize(6);
  tft.setTextColor(TFT_WHITE, BG);
  int textW = 5 * 6 * 6;
  tft.setCursor(CX - textW / 2, 88);
  tft.print(timeBuf);

  printCentered("Press to resume", 170, 2, PINK);
}

void drawCountdownDoneScreen() {
  tft.fillScreen(BG);

  printCentered("TIME'S UP!", 50, 2, TFT_WHITE);

  drawHeart(CX - 55, 110, 20, PINK);
  drawHeart(CX,      100, 28, DARK_PINK);
  drawHeart(CX + 55, 110, 20, PINK);

  printCentered("Press: set new timer", 172, 2, TFT_WHITE);
}