/*
 * Raspberry Pi Guitarix pedal controller
 * Arduino Leonardo + ST7796S SPI TFT (320x480, landscape 480x320)
 * + FT6336U touch (I2C) + four 3PDT ON-ON latching switches.
 *
 * Pi -> Leonardo, ASCII lines:
 *   K|index|count|bank name             display currently selected bank
 *   M|switch(0..3)|slot(0..1)|preset   mapping names in bank
 *   A|switch(0..3 or 255)|slot(0..1 or 255)|current preset name
 * Leonardo -> Pi:
 *   B|-1 or B|1                     prev/next Guitarix bank
 *   P|switch(0..3)|slot(0..1)       request preset by mapping
 *   R                                request state / map refresh
 *   L|page                           request actual Guitarix preset list page
 *   S|switch|slot|preset_index       assign preset in current bank (-1 = clear)
 * Pi -> Leonardo (additional):
 *   T|page|page_count                preset picker page information
 *   E|row|preset_name                preset picker list entry (5 per page)
 *   U|OK  or  U|ERR                  assignment save acknowledgment
 *   H|STARTED  or H|ERR               Pi shutdown request status
 *   N|note_name|velocity              tuner feedback from Guitarix via Pi
 * Leonardo -> Pi (additional):
 *   H|POWEROFF                        request safe operating system poweroff
 *
 * No MIDI Program Change for preset switching here: Guitarix is controlled by
 * Python through the verified JSON-RPC setpreset method. MIDIUSB can be added
 * independently for other CCs if desired.
 */
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <string.h>

// -------------------- PINOUT --------------------
#define TFT_CS      10
#define TFT_DC       9  // LCD_RS
#define TFT_RST      8
#define SD_CS        7
#define TOUCH_INT    6
#define TOUCH_RST    5
#define FT6336_ADDR  0x38
const uint8_t FS_PIN[4] = {A0, A1, A2, A3};
// LCD SPI: Leonardo ICSP MOSI/SCK/MISO; touch SDA=D2, SCL=D3.

// -------------------- DISPLAY --------------------
#define LCD_ROTATION    3  // 180 degrees compared with landscape rotation 1
#define TOUCH_ROTATION  3  // Match display rotation
#define SCREEN_W       480
#define SCREEN_H       320

Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
Arduino_GFX *gfx = new Arduino_ST7796(bus, TFT_RST, LCD_ROTATION, false);

#define C_BG         0x0841
#define C_TOP        0x1082
#define C_CARD       0x2104
#define C_GRAY       0x7BEF
#define C_LIGHT      0xBDF7
#define C_ACCENT     0x05FF
#define C_ACCENT_DIM 0x02F7
#define C_WHITE      0xFFFF
#define C_BLACK      0x0000

#define SLOT_Y_A 154
#define SLOT_Y_B 231
#define SLOT_H    64
#define SIDE_PAD   8
#define COL_GAP    6
#define NAME_LEN  19  // 18 visible ASCII chars + NUL

struct Rect { int16_t x, y, w, h; };
Rect slotRects[4][2];
char slotNames[4][2][NAME_LEN];
char currentName[NAME_LEN] = "WAITING";
#define BANK_NAME_LEN 25
char bankName[BANK_NAME_LEN] = "CONNECTING";
uint8_t bankIndex = 0;
uint8_t bankCount = 0;
const Rect BANK_PREV = {8, 34, 53, 34};
const Rect BANK_NEXT = {419, 34, 53, 34};

// Touchscreen assignment UI: 8 slot buttons, 5-item Guitarix preset browser.
const Rect SETUP_BUTTON = {385, 2, 87, 26};
const Rect TUNER_BUTTON = {286, 2, 91, 26};
const Rect POWER_BUTTON = {348, 51, 124, 36};
const Rect POWER_CANCEL = {24, 236, 195, 60};
const Rect POWER_CONFIRM = {251, 236, 205, 60};
const Rect BACK_BUTTON = {389, 2, 83, 26};
const Rect PICK_PREV = {8, 278, 72, 36};
const Rect PICK_CLEAR = {93, 278, 93, 36};
const Rect PICK_NEXT = {400, 278, 72, 36};
#define PICK_ROWS 5
#define PICK_PAGE_SIZE 5
#define PICK_FIRST_Y 65
#define PICK_ROW_STEP 42
#define PICK_ROW_HEIGHT 38

enum ScreenMode : uint8_t {
  SCREEN_LIVE, SCREEN_TUNER, SCREEN_ASSIGN, SCREEN_PICK,
  SCREEN_POWER_CONFIRM, SCREEN_POWER_PENDING, SCREEN_POWER_DONE, SCREEN_POWER_ERROR
};
ScreenMode screenMode = SCREEN_LIVE;
uint8_t editSwitch = 0, editSlot = 0;
uint16_t pickPage = 0, pickPageCount = 0;
bool pickLoading = false;
bool savePending = false;
bool saveError = false;
unsigned long saveStartedMs = 0;
unsigned long powerRequestMs = 0;
char tunerNote[5] = "--";
uint8_t tunerVelocity = 63;
bool tunerHasSignal = false;
unsigned long tunerLastMs = 0;
char pickNames[PICK_ROWS][NAME_LEN];


// Actual Guitarix selection; -1 means not assigned to any of the 8 slots.
int8_t activeSwitch = -1;
uint8_t activeSlot = 0;
bool synced = false;

// -------------------- 3PDT INPUT --------------------
// Boot electrical level is logically A for EACH switch.
// The mapping can flip when that slot is selected via touch/GUI.
bool aElectricalState[4];
bool rawState[4];
bool stableState[4];
unsigned long lastChangeMs[4];
const uint16_t DEBOUNCE_MS = 40;

uint8_t physicalSlot(uint8_t sw) {
  return stableState[sw] == aElectricalState[sw] ? 0 : 1;
}

void alignPhysicalMapping(uint8_t sw, uint8_t desiredSlot) {
  // The CURRENT physical position now logically points to desiredSlot.
  aElectricalState[sw] = desiredSlot == 0 ? stableState[sw] : !stableState[sw];
}

void initializeSwitches() {
  for (uint8_t i = 0; i < 4; ++i) pinMode(FS_PIN[i], INPUT_PULLUP);
  delay(30);
  for (uint8_t i = 0; i < 4; ++i) {
    const bool level = digitalRead(FS_PIN[i]);
    rawState[i] = stableState[i] = aElectricalState[i] = level;
    lastChangeMs[i] = millis();
  }
}

// -------------------- FT6336U --------------------
struct Touch { bool down; int16_t x, y; };

bool ftRead(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)FT6336_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}

Touch readTouch() {
  Touch t = {false, 0, 0};
  uint8_t data[5];
  if (!ftRead(0x02, data, sizeof(data))) return t;
  uint8_t n = data[0] & 0x0F;
  if (n == 0 || n > 2) return t;
  uint8_t event = (data[1] >> 6) & 3;
  if (event == 1 || event == 3) return t;  // release / no event
  uint16_t rawX = ((uint16_t)(data[1] & 15) << 8) | data[2];
  uint16_t rawY = ((uint16_t)(data[3] & 15) << 8) | data[4];
#if TOUCH_ROTATION == 0
  t.x = rawX; t.y = rawY;
#elif TOUCH_ROTATION == 1
  t.x = rawY; t.y = 319 - rawX;
#elif TOUCH_ROTATION == 2
  t.x = 319 - rawX; t.y = 479 - rawY;
#elif TOUCH_ROTATION == 3
  t.x = 479 - rawY; t.y = rawX;
#endif
  t.down = t.x >= 0 && t.x < SCREEN_W && t.y >= 0 && t.y < SCREEN_H;
  return t;
}

void initTouch() {
  pinMode(TOUCH_RST, OUTPUT);
  pinMode(TOUCH_INT, INPUT_PULLUP); // Polling for now
  digitalWrite(TOUCH_RST, LOW);
  delay(20);
  digitalWrite(TOUCH_RST, HIGH);
  delay(160);
  Wire.begin();
  Wire.setClock(100000);
}

// -------------------- UI GEOMETRY --------------------
void initRects() {
  const int16_t cw = (SCREEN_W - 2 * SIDE_PAD - 3 * COL_GAP) / 4;
  for (uint8_t i = 0; i < 4; ++i) {
    int16_t x = SIDE_PAD + i * (cw + COL_GAP);
    slotRects[i][0] = {x, SLOT_Y_A, cw, SLOT_H};
    slotRects[i][1] = {x, SLOT_Y_B, cw, SLOT_H};
  }
}

bool hit(const Rect &r, int16_t x, int16_t y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void drawCentered(const char *s, int16_t cx, int16_t y,
                  uint8_t size, uint16_t color) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setTextWrap(false);
  int16_t w = (int16_t)strlen(s) * (6 * size);
  gfx->setCursor(cx - w / 2, y);
  gfx->print(s);
}

void drawBank() {
  // Two large touch buttons: previous bank / next bank.
  gfx->fillRect(0, 31, SCREEN_W, 40, C_BG);
  gfx->fillRoundRect(BANK_PREV.x, BANK_PREV.y, BANK_PREV.w, BANK_PREV.h, 5, C_CARD);
  gfx->fillRoundRect(BANK_NEXT.x, BANK_NEXT.y, BANK_NEXT.w, BANK_NEXT.h, 5, C_CARD);
  drawCentered("<", BANK_PREV.x + BANK_PREV.w / 2, 40, 3, C_ACCENT);
  drawCentered(">", BANK_NEXT.x + BANK_NEXT.w / 2, 40, 3, C_ACCENT);
  gfx->setTextSize(1);
  gfx->setTextColor(C_GRAY);
  gfx->setCursor(76, 35);
  gfx->print(F("BANK"));
  if (bankCount > 0) {
    // Print directly; no snprintf or formatting buffer.
    gfx->setCursor(374, 35);
    gfx->print((unsigned int)bankIndex + 1);
    gfx->print('/');
    gfx->print(bankCount);
  }
  drawCentered(bankName, SCREEN_W / 2, 48, 2, C_WHITE);
}

void drawCurrent() {
  // The bank selector uses y=34..68, leaving current preset y=74..123.
  gfx->fillRect(0, 72, SCREEN_W, 51, C_BG);
  size_t len = strlen(currentName);
  uint8_t size = len <= 10 ? 3 : 2;
  drawCentered(currentName, SCREEN_W / 2, 77, size, C_WHITE);

  if (!synced) {
    drawCentered("CONNECTING", SCREEN_W / 2, 109, 1, C_ACCENT);
  } else if (activeSwitch < 0) {
    drawCentered("NOT ASSIGNED", SCREEN_W / 2, 109, 1, C_ACCENT);
  } else {
    char label[] = "FS1 - A";
    label[2] = '1' + activeSwitch;
    label[6] = activeSlot ? 'B' : 'A';
    drawCentered(label, SCREEN_W / 2, 109, 1, C_ACCENT);
  }
}

void drawSlot(uint8_t sw, uint8_t slot) {
  const Rect &r = slotRects[sw][slot];
  bool active = activeSwitch == sw && activeSlot == slot;
  bool physicallyPointingHere = physicalSlot(sw) == slot;
  uint16_t bg = active ? C_ACCENT : C_CARD;
  uint16_t fg = active ? C_BLACK : C_WHITE;
  uint16_t border = active ? C_WHITE : (physicallyPointingHere ? C_LIGHT : C_GRAY);

  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 7, bg);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 7, border);
  gfx->setTextSize(1);
  gfx->setTextColor(active ? C_ACCENT_DIM : C_GRAY);
  gfx->setCursor(r.x + 7, r.y + 7);
  gfx->print(slot ? 'B' : 'A');

  const char *name = slotNames[sw][slot];
  size_t len = strlen(name);
  uint8_t size = len <= 8 ? 2 : 1;
  drawCentered(name, r.x + r.w / 2, r.y + (r.h - size * 8) / 2,
               size, fg);
}

void drawColumn(uint8_t sw) {
  const Rect &r = slotRects[sw][0];
  gfx->fillRect(r.x, 133, r.w, 17, C_BG);
  char label[] = "FS1";
  label[2] = '1' + sw;
  drawCentered(label, r.x + r.w / 2, 135, 1,
               activeSwitch == sw ? C_ACCENT : C_GRAY);
  drawSlot(sw, 0);
  drawSlot(sw, 1);
}

void drawButton(const Rect &r, const char *label, uint16_t color = C_ACCENT) {
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 5, C_CARD);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 5, C_GRAY);
  drawCentered(label, r.x + r.w / 2, r.y + (r.h - 16) / 2, 2, color);
}

void drawHeader(const char *title) {
  gfx->fillRect(0, 0, SCREEN_W, 30, C_TOP);
  gfx->setTextSize(2);
  gfx->setTextColor(C_ACCENT);
  gfx->setCursor(10, 8);
  gfx->print(title);
  drawButton(BACK_BUTTON, "BACK");
}

void drawAssignCard(uint8_t sw, uint8_t slot) {
  Rect r = {slotRects[sw][slot].x, (int16_t)(slot == 0 ? 94 : 197),
            slotRects[sw][slot].w, 88};
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 6, C_CARD);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 6, C_GRAY);
  char label[] = "FS1-A";
  label[2] = '1' + sw;
  label[4] = slot ? 'B' : 'A';
  drawCentered(label, r.x + r.w / 2, r.y + 11, 2, C_ACCENT);
  const char *name = slotNames[sw][slot];
  drawCentered(name, r.x + r.w / 2, r.y + 50,
               strlen(name) <= 8 ? 2 : 1, C_WHITE);
}

void drawAssignScreen() {
  gfx->fillScreen(C_BG);
  drawHeader("ASSIGN");
  gfx->setTextColor(C_GRAY);
  gfx->setTextSize(1);
  gfx->setCursor(12, 38);
  gfx->print(F("BANK:"));
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(53, 38);
  gfx->print(bankName);
  gfx->setTextColor(C_GRAY);
  gfx->setCursor(12, 58);
  gfx->print(F("Tap slot to assign"));
  drawButton(POWER_BUTTON, "POWER OFF", C_LIGHT);
  for (uint8_t i = 0; i < 4; ++i) {
    drawAssignCard(i, 0);
    drawAssignCard(i, 1);
  }
}

// -------------------- SAFE POWER OFF UI --------------------
// Two taps required: SETUP -> POWER OFF -> SHUTDOWN.
// "STARTED" is NOT confirmation that shutdown has finished.
void drawPowerConfirm() {
  gfx->fillScreen(C_BG);
  drawHeader("POWER");
  drawCentered("POWER OFF?", SCREEN_W / 2, 76, 3, C_WHITE);
  drawCentered("Confirm safe shutdown", SCREEN_W / 2, 134, 1, C_LIGHT);
  drawCentered("Audio will stop", SCREEN_W / 2, 154, 1, C_LIGHT);
  drawButton(POWER_CANCEL, "CANCEL", C_WHITE);
  drawButton(POWER_CONFIRM, "SHUTDOWN", C_ACCENT);
}

void drawPowerStatus() {
  gfx->fillScreen(C_BG);
  drawCentered("POWER", 240, 8, 2, C_ACCENT);
  if (screenMode == SCREEN_POWER_PENDING) {
    drawCentered("CONTACTING PI...", 240, 90, 2, C_WHITE);
  } else if (screenMode == SCREEN_POWER_DONE) {
    drawCentered("SHUTDOWN STARTED", 240, 90, 2, C_ACCENT);
    drawCentered("Wait until Pi halts", 240, 145, 1, C_WHITE);
    drawCentered("THEN disconnect power", 240, 166, 1, C_WHITE);
  } else {
    drawCentered("POWER OFF FAILED", 240, 90, 2, C_WHITE);
    drawCentered("Check Pi / permissions", 240, 145, 1, C_LIGHT);
    drawButton(POWER_CANCEL, "BACK");
    drawButton(POWER_CONFIRM, "RETRY");
  }
}

void requestSafePoweroff() {
  screenMode = SCREEN_POWER_PENDING;
  powerRequestMs = millis();
  drawPowerStatus();
  if (Serial) {
    Serial.println(F("H|POWEROFF"));
    Serial.flush();
  }
}

void drawPickRow(uint8_t row) {
  int16_t y = PICK_FIRST_Y + row * PICK_ROW_STEP;
  gfx->fillRoundRect(12, y, 456, PICK_ROW_HEIGHT, 5, C_CARD);
  gfx->drawRoundRect(12, y, 456, PICK_ROW_HEIGHT, 5, C_GRAY);
  gfx->setCursor(23, y + 11);
  gfx->setTextSize(2);
  gfx->setTextColor(C_WHITE);
  gfx->print(pickNames[row][0] ? pickNames[row] : "...");
}

void drawPickFooter() {
  gfx->fillRect(0, 277, SCREEN_W, 43, C_BG);
  drawButton(PICK_PREV, "<", pickPage > 0 ? C_ACCENT : C_GRAY);
  drawButton(PICK_CLEAR, "EMPTY", C_LIGHT);
  drawButton(PICK_NEXT, ">", pickPage + 1 < pickPageCount ? C_ACCENT : C_GRAY);
  if (savePending) {
    drawCentered("SAVING...", 294, 291, 2, C_ACCENT);
  } else if (saveError) {
    drawCentered("SAVE FAILED", 294, 291, 2, C_WHITE);
  } else if (pickLoading) {
    drawCentered("LOADING...", 294, 291, 2, C_ACCENT);
  } else {
    // Print page counter directly, avoiding a second number formatter.
    gfx->setTextSize(2);
    gfx->setTextColor(C_ACCENT);
    gfx->setCursor(256, 291);
    gfx->print((unsigned int)(pickPage + 1));
    gfx->print('/');
    gfx->print((unsigned int)(pickPageCount ? pickPageCount : 1));
  }
}

void drawPickScreen() {
  gfx->fillScreen(C_BG);
  char title[] = "FS1-A";
  title[2] = '1' + editSwitch;
  title[4] = editSlot ? 'B' : 'A';
  drawHeader(title);
  gfx->setTextSize(1);
  gfx->setTextColor(C_GRAY);
  gfx->setCursor(12, 42);
  gfx->print(F("BANK:"));
  gfx->setTextColor(C_WHITE);
  gfx->print(bankName);
  for (uint8_t i = 0; i < PICK_ROWS; ++i) drawPickRow(i);
  drawPickFooter();
}

// Tuner MIDI is decoded on the Raspberry Pi; no FFT or floating point on AVR.
void drawTunerReading() {
  // Redraw only the dynamic area (avoid flooding the 8 MHz SPI bus).
  gfx->fillRect(28, 68, 424, 106, C_BG);
  drawCentered(tunerHasSignal ? tunerNote : "--", 240, 87, 5, C_WHITE);
  gfx->fillRect(28, 182, 424, 63, C_BG);
  gfx->drawFastHLine(48, 213, 384, C_GRAY);
  gfx->fillRect(239, 187, 2, 51, C_ACCENT);
  if (tunerHasSignal) {
    int16_t pos = 240 + ((int16_t)tunerVelocity - 63) * 3;
    if (pos < 48) pos = 48;
    if (pos > 432) pos = 432;
    gfx->fillRect(pos - 3, 198, 7, 30, C_WHITE);
    const char *state = tunerVelocity >= 61 && tunerVelocity <= 65
                        ? "IN TUNE" : tunerVelocity < 63 ? "FLAT" : "SHARP";
    drawCentered(state, 240, 253, 2, C_ACCENT);
  } else {
    drawCentered("NO SIGNAL", 240, 253, 2, C_GRAY);
  }
}

void drawTunerScreen() {
  gfx->fillScreen(C_BG);
  drawHeader("TUNER");
  drawCentered("MIDI tuner", 240, 42, 1, C_LIGHT);
  drawCentered("LOW", 56, 290, 1, C_GRAY);
  drawCentered("HIGH", 424, 290, 1, C_GRAY);
  drawTunerReading();
}

void drawUI() {
  gfx->fillScreen(C_BG);
  gfx->fillRect(0, 0, SCREEN_W, 30, C_TOP);
  gfx->setTextColor(C_ACCENT);
  gfx->setTextSize(2);
  gfx->setCursor(10, 8);
  gfx->print(F("LIVE"));
  drawButton(SETUP_BUTTON, "SETUP");
  drawButton(TUNER_BUTTON, "TUNER");
  gfx->drawFastHLine(8, 124, SCREEN_W - 16, C_CARD);
  drawBank();
  drawCurrent();
  for (uint8_t i = 0; i < 4; ++i) drawColumn(i);
}

void requestPickPage(uint16_t page) {
  if (!Serial) return;
  pickPage = page;
  pickLoading = true;
  saveError = false;
  for (uint8_t i = 0; i < PICK_ROWS; ++i) pickNames[i][0] = '\0';
  drawPickScreen();
  Serial.print(F("L|"));
  Serial.println(page);
}

void sendSlotAssignment(int16_t presetIndex) {
  if (!Serial || savePending) return;
  savePending = true;
  saveStartedMs = millis();
  saveError = false;
  drawPickFooter();
  Serial.print(F("S|"));
  Serial.print(editSwitch);
  Serial.print('|');
  Serial.print(editSlot);
  Serial.print('|');
  Serial.println(presetIndex);
}

// -------------------- PI SERIAL --------------------
char incoming[96];
uint8_t incomingLen = 0;
uint8_t receivedMapMask = 0;
unsigned long lastRequestSyncMs = 0;

void sendBankRequest(int8_t direction) {
  if (!Serial) return;
  if (bankCount < 2) return;
  Serial.print(F("B|"));
  Serial.println(direction);
}

void sendPresetRequest(uint8_t sw, uint8_t slot) {
  // Wait for Pi confirmation before changing active highlight or physical A/B mapping.
  if (!Serial) return;
  Serial.print(F("P|"));
  Serial.print(sw);
  Serial.print('|');
  Serial.println(slot);
}

void copyName(char *target, size_t size, const char *value) {
  if (!value) return;
  strncpy(target, value, size - 1);
  target[size - 1] = '\0';
}

// Compact integer parser for ASCII fields sent by the Pi.  The protocol
// sends simple decimal values, so a general-purpose strtol/atoi isn't needed.
int parseFieldNumber(const char *s) {
  if (*s == '-') return -1;
  int value = 0;
  while (*s >= '0' && *s <= '9') {
    value = value * 10 + (*s++ - '0');
  }
  return value;
}

void handlePiLine(char *line) {
  char *save = NULL;
  char *type = strtok_r(line, "|", &save);
  if (!type) return;

  // Ignore delayed bank/preset traffic while viewing shutdown UI.
  if ((screenMode == SCREEN_POWER_CONFIRM || screenMode == SCREEN_POWER_PENDING ||
       screenMode == SCREEN_POWER_DONE || screenMode == SCREEN_POWER_ERROR) &&
      type[0] != 'H') return;

  if (type[0] == 'K') {
    char *sIndex = strtok_r(NULL, "|", &save);
    char *sCount = strtok_r(NULL, "|", &save);
    char *newBank = strtok_r(NULL, "|", &save);
    if (!sIndex || !sCount || !newBank) return;
    int idx = parseFieldNumber(sIndex), count = parseFieldNumber(sCount);
    if (idx < 0 || idx > 254 || count < 1 || count > 255 || idx >= count) return;
    bool changed = bankCount == 0 || bankIndex != idx || strcmp(bankName, newBank) != 0;
    bankIndex = (uint8_t)idx;
    bankCount = (uint8_t)count;
    copyName(bankName, BANK_NAME_LEN, newBank);
    receivedMapMask = 0;  // Eight fresh slot names will follow K.
    if (changed) {
      activeSwitch = -1;
      copyName(currentName, NAME_LEN, "LOADING");
      if (screenMode == SCREEN_PICK) screenMode = SCREEN_ASSIGN;
      if (screenMode == SCREEN_LIVE) drawUI();
      else if (screenMode == SCREEN_ASSIGN) drawAssignScreen();
    } else if (screenMode == SCREEN_LIVE) {
      drawBank();
    }
  }
  else if (type[0] == 'M') {
    char *sSw = strtok_r(NULL, "|", &save);
    char *sSlot = strtok_r(NULL, "|", &save);
    char *name = strtok_r(NULL, "|", &save);
    if (!sSw || !sSlot || !name) return;
    int sw = parseFieldNumber(sSw), slot = parseFieldNumber(sSlot);
    if (sw < 0 || sw > 3 || slot < 0 || slot > 1) return;
    copyName(slotNames[sw][slot], NAME_LEN, name);
    receivedMapMask |= (1 << (sw * 2 + slot));
    if (screenMode == SCREEN_LIVE) drawSlot(sw, slot);
    else if (screenMode == SCREEN_ASSIGN) drawAssignCard(sw, slot);
  }
  else if (type[0] == 'T') {
    char *sPage = strtok_r(NULL, "|", &save);
    char *sCount = strtok_r(NULL, "|", &save);
    if (!sPage || !sCount || screenMode != SCREEN_PICK) return;
    int p = parseFieldNumber(sPage), n = parseFieldNumber(sCount);
    if (p < 0 || n < 1 || p >= n) return;
    pickPage = (uint16_t)p;
    pickPageCount = (uint16_t)n;
    pickLoading = false;
    for (uint8_t i = 0; i < PICK_ROWS; ++i) pickNames[i][0] = '\0';
    for (uint8_t i = 0; i < PICK_ROWS; ++i) drawPickRow(i);
    drawPickFooter();
  }
  else if (type[0] == 'E') {
    char *sRow = strtok_r(NULL, "|", &save);
    char *name = strtok_r(NULL, "|", &save);
    if (!sRow || !name || screenMode != SCREEN_PICK) return;
    int row = parseFieldNumber(sRow);
    if (row < 0 || row >= PICK_ROWS) return;
    copyName(pickNames[row], NAME_LEN, name);
    drawPickRow(row);
  }
  else if (type[0] == 'U') {
    char *result = strtok_r(NULL, "|", &save);
    if (!result || !savePending) return;
    savePending = false;
    if (strcmp(result, "OK") == 0) {
      screenMode = SCREEN_ASSIGN;
      drawAssignScreen();
    } else {
      saveError = true;
      if (screenMode == SCREEN_PICK) drawPickFooter();
    }
  }
  else if (type[0] == 'N') {
    char *name = strtok_r(NULL, "|", &save);
    char *value = strtok_r(NULL, "|", &save);
    if (!name || !value) return;
    int velocity = parseFieldNumber(value);
    if (velocity < 1 || velocity > 127 || strlen(name) > 4) return;
    copyName(tunerNote, sizeof(tunerNote), name);
    tunerVelocity = (uint8_t)velocity;
    tunerHasSignal = true;
    tunerLastMs = millis();
    if (screenMode == SCREEN_TUNER) drawTunerReading();
  }
  else if (type[0] == 'H') {
    char *result = strtok_r(NULL, "|", &save);
    if (!result) return;
    if (strcmp(result, "STARTED") == 0 && screenMode == SCREEN_POWER_PENDING) {
      screenMode = SCREEN_POWER_DONE;
      drawPowerStatus();
    } else if (strcmp(result, "ERR") == 0 &&
               (screenMode == SCREEN_POWER_PENDING || screenMode == SCREEN_POWER_DONE)) {
      screenMode = SCREEN_POWER_ERROR;
      drawPowerStatus();
    }
  }
  else if (type[0] == 'A') {
    char *sSw = strtok_r(NULL, "|", &save);
    char *sSlot = strtok_r(NULL, "|", &save);
    char *name = strtok_r(NULL, "|", &save);
    if (!sSw || !sSlot || !name) return;
    int sw = parseFieldNumber(sSw), slot = parseFieldNumber(sSlot);
    int8_t previous = activeSwitch;
    copyName(currentName, NAME_LEN, name);
    synced = true;

    if (sw >= 0 && sw <= 3 && slot >= 0 && slot <= 1) {
      activeSwitch = sw;
      activeSlot = slot;
      // Touch or GUI changes also align physical switch to chosen slot.
      alignPhysicalMapping(sw, slot);
    } else {
      activeSwitch = -1;
    }
    if (screenMode == SCREEN_LIVE) {
      drawCurrent();
      if (previous >= 0) drawColumn(previous);
      if (activeSwitch >= 0 && activeSwitch != previous) drawColumn(activeSwitch);
    }
  }
}

void updatePiSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      incoming[incomingLen] = '\0';
      if (incomingLen) handlePiLine(incoming);
      incomingLen = 0;
    } else if (c != '\r') {
      if (incomingLen < sizeof(incoming) - 1) {
        incoming[incomingLen++] = c;
      } else {
        incomingLen = 0; // overlong or corrupt line
      }
    }
  }
  if (screenMode != SCREEN_POWER_PENDING && screenMode != SCREEN_POWER_DONE &&
      receivedMapMask != 0xFF && Serial &&
      millis() - lastRequestSyncMs >= 1500) {
    Serial.println(F("R"));
    lastRequestSyncMs = millis();
  }
}

// -------------------- INPUTS --------------------
void updateFootswitches() {
  if (screenMode == SCREEN_POWER_PENDING || screenMode == SCREEN_POWER_DONE) return;
  unsigned long now = millis();
  for (uint8_t i = 0; i < 4; ++i) {
    bool level = digitalRead(FS_PIN[i]);
    if (level != rawState[i]) {
      rawState[i] = level;
      lastChangeMs[i] = now;
    }
    if (level != stableState[i] && now - lastChangeMs[i] >= DEBOUNCE_MS) {
      stableState[i] = level;

      // A different footswitch ALWAYS begins at its upper preset (A).
      // If this is the already-active footswitch, its changed 3PDT
      // contact selects the opposite A/B slot, using the physical mapping.
      // The Pi's A|sw|slot confirmation realigns this switch's current
      // electrical position to the confirmed slot, so the next press toggles.
      const uint8_t targetSlot =
          (activeSwitch == (int8_t)i) ? physicalSlot(i) : 0;

      drawColumn(i); // Actual sound stays highlighted until Guitarix confirms.
      sendPresetRequest(i, targetSlot);
    }
  }
}

bool touchHeld = false;
unsigned long touchLastSeenMs = 0;
void updateTouch() {
  Touch t = readTouch();
  if (t.down) {
    touchLastSeenMs = millis();
    if (!touchHeld) {
      touchHeld = true;
      if (screenMode == SCREEN_POWER_PENDING || screenMode == SCREEN_POWER_DONE) return;
      if (screenMode == SCREEN_POWER_ERROR) {
        if (hit(POWER_CANCEL, t.x, t.y)) {
          screenMode = SCREEN_ASSIGN;
          drawAssignScreen();
        } else if (hit(POWER_CONFIRM, t.x, t.y)) {
          requestSafePoweroff();
        }
        return;
      }
      if (screenMode == SCREEN_POWER_CONFIRM) {
        if (hit(BACK_BUTTON, t.x, t.y) || hit(POWER_CANCEL, t.x, t.y)) {
          screenMode = SCREEN_ASSIGN;
          drawAssignScreen();
        } else if (hit(POWER_CONFIRM, t.x, t.y)) {
          requestSafePoweroff();
        }
        return;
      }
      if (screenMode == SCREEN_TUNER) {
        if (hit(BACK_BUTTON, t.x, t.y)) {
          screenMode = SCREEN_LIVE;
          drawUI();
        }
        return;
      }
      if (screenMode == SCREEN_ASSIGN) {
        if (hit(BACK_BUTTON, t.x, t.y)) {
          screenMode = SCREEN_LIVE;
          drawUI();
          return;
        }
        if (hit(POWER_BUTTON, t.x, t.y)) {
          screenMode = SCREEN_POWER_CONFIRM;
          drawPowerConfirm();
          return;
        }
        for (uint8_t sw = 0; sw < 4; ++sw) {
          for (uint8_t slot = 0; slot < 2; ++slot) {
            Rect r = {slotRects[sw][slot].x,
                      (int16_t)(slot == 0 ? 94 : 197),
                      slotRects[sw][slot].w, 88};
            if (hit(r, t.x, t.y)) {
              editSwitch = sw; editSlot = slot;
              pickPageCount = 0;
              screenMode = SCREEN_PICK;
              requestPickPage(0);
              return;
            }
          }
        }
        return;
      }
      if (screenMode == SCREEN_PICK) {
        if (savePending) return;
        if (hit(BACK_BUTTON, t.x, t.y)) {
          screenMode = SCREEN_ASSIGN;
          drawAssignScreen();
          return;
        }
        if (hit(PICK_CLEAR, t.x, t.y)) {
          sendSlotAssignment(-1);
          return;
        }
        if (hit(PICK_PREV, t.x, t.y) && pickPage > 0) {
          requestPickPage(pickPage - 1);
          return;
        }
        if (hit(PICK_NEXT, t.x, t.y) &&
            pickPage + 1 < pickPageCount) {
          requestPickPage(pickPage + 1);
          return;
        }
        if (!pickLoading) {
          for (uint8_t row = 0; row < PICK_ROWS; ++row) {
            Rect r = {12, (int16_t)(PICK_FIRST_Y + row * PICK_ROW_STEP),
                      456, PICK_ROW_HEIGHT};
            if (hit(r, t.x, t.y) && pickNames[row][0] &&
                strcmp(pickNames[row], "----") != 0) {
              sendSlotAssignment((int16_t)(pickPage * PICK_PAGE_SIZE + row));
              return;
            }
          }
        }
        return;
      }
      if (hit(TUNER_BUTTON, t.x, t.y)) {
        screenMode = SCREEN_TUNER;
        drawTunerScreen();
        return;
      }
      if (hit(SETUP_BUTTON, t.x, t.y)) {
        screenMode = SCREEN_ASSIGN;
        drawAssignScreen();
        return;
      }
      if (hit(BANK_PREV, t.x, t.y)) {
        sendBankRequest(-1);
        return;
      }
      if (hit(BANK_NEXT, t.x, t.y)) {
        sendBankRequest(1);
        return;
      }
      for (uint8_t sw = 0; sw < 4; ++sw) {
        for (uint8_t slot = 0; slot < 2; ++slot) {
          if (hit(slotRects[sw][slot], t.x, t.y)) {
            sendPresetRequest(sw, slot);
            return;
          }
        }
      }
    }
  } else if (millis() - touchLastSeenMs > 45) {
    touchHeld = false;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  initializeSwitches();
  initTouch();

  if (!gfx->begin(8000000)) {
    // Cannot draw; retry only after power-cycle or reupload.
    for (;;) delay(1000);
  }
  gfx->setRotation(LCD_ROTATION);
  initRects();
  for (uint8_t sw = 0; sw < 4; ++sw)
    for (uint8_t slot = 0; slot < 2; ++slot)
      strcpy(slotNames[sw][slot], "----");
  drawUI();
}

void loop() {
  updateFootswitches();
  updatePiSerial();
  updateTouch();
  if (savePending && millis() - saveStartedMs > 5000UL) {
    savePending = false;
    saveError = true;
    if (screenMode == SCREEN_PICK) drawPickFooter();
  }
  if (screenMode == SCREEN_POWER_PENDING && millis() - powerRequestMs > 8000UL) {
    screenMode = SCREEN_POWER_ERROR;
    drawPowerStatus();
  }
  if (screenMode == SCREEN_TUNER && tunerHasSignal &&
      millis() - tunerLastMs > 1600UL) {
    tunerHasSignal = false;
    drawTunerReading();
  }
  delay(2);
}
