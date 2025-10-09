#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>

TFT_eSPI tft = TFT_eSPI();

// --- Piny CYD ---
#define SD_CS   5
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23

#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_SCK  25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32

XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// --- Plik JSON ---
const char *SD_FILENAME = "/filament.json";

// --- Struktura slotu ---
struct Slot {
  const char *name;
  int grams;
  int x, y, w, h;
  int btnUseX, btnUseY, btnUseW, btnUseH;
  int btnResetX, btnResetY, btnResetW, btnResetH;
};

Slot slots[4];

// --- Kolory ---
#define BG_COLOR    TFT_BLACK
#define HEAD_COLOR  TFT_WHITE
#define SLOT_BG     TFT_DARKGREY
#define SLOT_HL     TFT_LIGHTGREY
#define BTN_COLOR   TFT_BLUE
#define BTN_TEXT    TFT_WHITE

// --- Ustawienia slotów ---
void setupSlotsDefault() {
  int margin = 8;
  int headerH = 40;
  int slotH = (tft.height() - margin * 2 - headerH - (3 * 8)) / 4;
  int y = margin + headerH;
  const char* names[4] = {"A1", "A2", "A3", "A4"};
  for (int i = 0; i < 4; i++) {
    slots[i].name = names[i];
    slots[i].grams = 1000;
    slots[i].x = margin;
    slots[i].y = y;
    slots[i].w = tft.width() - margin * 2;
    slots[i].h = slotH;

    // dwa przyciski obok siebie
    int btnW = 60;
    int btnH = 30;
    int spacing = 6;
    slots[i].btnUseW = btnW;
    slots[i].btnUseH = btnH;
    slots[i].btnResetW = btnW;
    slots[i].btnResetH = btnH;

    int btnY = slots[i].y + (slots[i].h - btnH) / 2;
    slots[i].btnUseX = slots[i].x + slots[i].w - (btnW * 2 + spacing + 8);
    slots[i].btnUseY = btnY;
    slots[i].btnResetX = slots[i].btnUseX + btnW + spacing;
    slots[i].btnResetY = btnY;

    y += slotH + 8;
  }
}

// --- SD JSON ---
void saveToSD() {
  StaticJsonDocument<256> doc;
  for (int i = 0; i < 4; i++) doc[String(slots[i].name)] = slots[i].grams;

  // usuń stary plik, by nie dopisywać do końca
  if (SD.exists(SD_FILENAME)) SD.remove(SD_FILENAME);

  File f = SD.open(SD_FILENAME, FILE_WRITE);
  if (!f) {
    Serial.println("Błąd otwarcia pliku do zapisu!");
    return;
  }

  serializeJson(doc, f);
  f.close();
  Serial.println("Zapisano dane na SD");
}

void loadFromSD() {
  if (!SD.exists(SD_FILENAME)) {
    Serial.println("Brak pliku – domyślne wartości");
    return;
  }
  File f = SD.open(SD_FILENAME);
  if (!f) { Serial.println("Błąd otwarcia pliku"); return; }
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, f)) {
    Serial.println("Błąd JSON – domyślne wartości");
  } else {
    for (int i = 0; i < 4; i++) {
      const char *n = slots[i].name;
      if (doc.containsKey(n)) slots[i].grams = doc[n];
    }
    Serial.println("Wczytano dane z SD");
  }
  f.close();
}

// --- UI ---
void drawHeader() {
  tft.fillRect(0, 0, tft.width(), 40, TFT_NAVY);
  tft.setTextColor(HEAD_COLOR, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Filament AMS - Licznik", tft.width()/2, 20);
}

void drawSlot(int i, bool highlight=false) {
  Slot &s = slots[i];

  // 🔴 kolor alarmu
  uint16_t bg = SLOT_BG;
  if (highlight) bg = SLOT_HL;
  if (s.grams < 100) bg = TFT_RED;

  tft.fillRoundRect(s.x, s.y, s.w, s.h, 8, bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(String(s.name), s.x + 8, s.y + 8);

  // wartość
  tft.setTextDatum(MR_DATUM);
  tft.setTextFont(4);
  tft.setTextColor((s.grams < 100) ? TFT_BLACK : TFT_WHITE, bg);
  tft.drawString(String(s.grams) + " g", s.x + s.w - 160, s.y + s.h / 2);
  tft.setTextFont(2);

  // przyciski
  // Użyto
  tft.fillRoundRect(s.btnUseX, s.btnUseY, s.btnUseW, s.btnUseH, 6, BTN_COLOR);
  tft.setTextColor(BTN_TEXT, BTN_COLOR);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Użyto", s.btnUseX + s.btnUseW / 2, s.btnUseY + s.btnUseH / 2);

  // Reset
  tft.fillRoundRect(s.btnResetX, s.btnResetY, s.btnResetW, s.btnResetH, 6, TFT_RED);
  tft.setTextColor(BTN_TEXT, TFT_RED);
  tft.drawString("Reset", s.btnResetX + s.btnResetW / 2, s.btnResetY + s.btnResetH / 2);
}

void drawUI() {
  tft.fillScreen(BG_COLOR);
  drawHeader();
  for (int i = 0; i < 4; i++) drawSlot(i);
}

// --- Dotyk ---
TS_Point getPoint() {
  TS_Point p = ts.getPoint();
  int rawX = p.x, rawY = p.y;
  int sx = map(rawX, 200, 3900, 0, tft.width());
  int sy = map(rawY, 200, 3900, 0, tft.height());
  return TS_Point(sx, sy, p.z);
}

bool getTouchXY(int &x, int &y) {
  if (!ts.touched()) return false;
  TS_Point p = getPoint();
  if (p.z < 100) return false;
  x = p.x; y = p.y;
  delay(120);
  while (ts.touched()) delay(10);
  return true;
}

// --- Klawiatura 2-rzędowa ---
struct KBButton { int x, y, w, h; const char *label; };

String showKeypadAndGet(const char *title) {
  int W = tft.width(), H = tft.height();
  int boxW = W - 30, boxH = H - 60;
  int boxX = 15, boxY = 50;

  tft.fillRoundRect(boxX, boxY, boxW, boxH, 8, TFT_WHITE);
  tft.drawRoundRect(boxX, boxY, boxW, boxH, 8, TFT_BLACK);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(title, boxX + boxW / 2, boxY + 12);

  String input = "";
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString("0", boxX + boxW / 2, boxY + 40);

  // dwa rzędy
  const char* labels[12] = {"1","2","3","4","5","OK","6","7","8","9","0","C"};
  KBButton keys[12];
  int kbX = boxX + 10;
  int kbY = boxY + 80;
  int cellW = (boxW - 20 - (5 * 5)) / 6; // 6 kolumn
  int cellH = 48;
  int spacing = 5;

  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 6; c++) {
      int idx = r * 6 + c;
      int x = kbX + c * (cellW + spacing);
      int y = kbY + r * (cellH + spacing);
      keys[idx] = {x, y, cellW, cellH, labels[idx]};
      tft.fillRoundRect(x, y, cellW, cellH, 4, 0x7BEF);
      tft.drawRoundRect(x, y, cellW, cellH, 4, TFT_BLACK);
      tft.setTextColor(TFT_BLACK, 0x7BEF);
      tft.setTextDatum(MC_DATUM);
      tft.setTextFont(2);
      tft.drawString(labels[idx], x + cellW / 2, y + cellH / 2);
    }
  }

  while (1) {
  int tx, ty;
  if (getTouchXY(tx, ty)) {
    for (int k = 0; k < 12; k++) {
      KBButton &b = keys[k];
      if (tx >= b.x && tx <= b.x + b.w && ty >= b.y && ty <= b.y + b.h) {
        const char *lbl = b.label;

        if (strcmp(lbl, "C") == 0) input = "";
        else if (strcmp(lbl, "OK") == 0) {
          tft.setTextFont(2);  // 🔹 PRZYWRÓĆ NORMALNY ROZMIAR
          return input.length() ? input : "0";
        }
        else if (input.length() < 5) input += lbl;

        tft.fillRect(boxX + 8, boxY + 30, boxW - 16, 44, TFT_WHITE);
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.setTextFont(4);
        tft.drawString(input.length() ? input : "0", boxX + boxW / 2, boxY + 40);
      }
    }
  }
  delay(10);
}

}

// --- Dialog potwierdzenia ---
bool confirmDialog(const char *msg) {
  int W = tft.width(), H = tft.height();
  int boxW = W - 80, boxH = 120;
  int boxX = 40, boxY = (H - boxH) / 2;

  tft.fillRoundRect(boxX, boxY, boxW, boxH, 8, TFT_WHITE);
  tft.drawRoundRect(boxX, boxY, boxW, boxH, 8, TFT_BLACK);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.drawString(msg, boxX + boxW / 2, boxY + 28);

  // 🔸 mniejsze przyciski
  int bw = 70, bh = 30;
  int bx1 = boxX + 20, bx2 = boxX + boxW - bw - 20;
  int by = boxY + boxH - bh - 15;

  tft.fillRoundRect(bx1, by, bw, bh, 6, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextFont(2);
  tft.drawString("Anuluj", bx1 + bw / 2, by + bh / 2);

  tft.fillRoundRect(bx2, by, bw, bh, 6, TFT_GREEN);
  tft.setTextColor(TFT_WHITE, TFT_GREEN);
  tft.drawString("OK", bx2 + bw / 2, by + bh / 2);

  while (1) {
    int tx, ty;
    if (getTouchXY(tx, ty)) {
      if (tx >= bx1 && tx <= bx1 + bw && ty >= by && ty <= by + bh) { drawUI(); return false; }
      if (tx >= bx2 && tx <= bx2 + bw && ty >= by && ty <= by + bh) { drawUI(); return true; }
    }
    delay(10);
  }
}


// --- Akcje ---
void handleUse(int idx) {
  String s = showKeypadAndGet("Podaj zużycie [g]");
  int used = s.toInt();
  if (used < 0) used = 0;
  slots[idx].grams -= used;
  if (slots[idx].grams < 0) slots[idx].grams = 0;
  saveToSD();
  drawUI();
}

void handleReset(int idx) {
  if (confirmDialog("Resetuj do 1000 g?")) {
    slots[idx].grams = 1000;
    saveToSD();
    drawUI();
  }
}

void checkTouchMain(int tx, int ty) {
  for (int i = 0; i < 4; i++) {
    Slot &s = slots[i];
    if (tx >= s.btnUseX && tx <= s.btnUseX + s.btnUseW && ty >= s.btnUseY && ty <= s.btnUseY + s.btnUseH) {
      handleUse(i); return;
    }
    if (tx >= s.btnResetX && tx <= s.btnResetX + s.btnResetW && ty >= s.btnResetY && ty <= s.btnResetY + s.btnResetH) {
      handleReset(i); return;
    }
  }
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1); // portrait
  tft.fillScreen(BG_COLOR);

  static SPIClass touchSPI(VSPI);
  touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  if (!ts.begin(touchSPI)) Serial.println("Touch init failed");
  else { ts.setRotation(1); Serial.println("Touch OK"); }

  static SPIClass sdSPI(HSPI);
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) Serial.println("SD init failed!");
  else Serial.println("SD init OK");

  setupSlotsDefault();
  loadFromSD();
  drawUI();
}

// --- Loop ---
void loop() {
  int tx, ty;
  if (getTouchXY(tx, ty)) checkTouchMain(tx, ty);
  delay(20);
}
