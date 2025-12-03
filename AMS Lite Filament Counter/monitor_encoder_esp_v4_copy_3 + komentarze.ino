// =====================================================================================================
//  Projekt: Filament AMS - Licznik (CYD / ESP32 + TFT_eSPI + XPT2046 + SD + UDP)
//  Opis: Kompletny interfejs dotykowy do monitorowania zużycia filamentu (4 sloty)
//        z zapisem danych na karcie SD (JSON) oraz odbiorem danych telemetrycznych przez UDP
// =====================================================================================================

// ---------------------------------------
// Sekcja: Import bibliotek
// ---------------------------------------

#include <TFT_eSPI.h>               // Biblioteka do obsługi wyświetlacza TFT (sterownik ILI9341/ST7789 itp.)
#include <XPT2046_Touchscreen.h>    // Biblioteka do obsługi panelu dotykowego XPT2046
#include <SPI.h>                    // Standardowa biblioteka obsługująca interfejs SPI (komunikacja z SD i dotykiem)
#include <SD.h>                     // Biblioteka do obsługi kart SD
#include <ArduinoJson.h>            // Biblioteka ArduinoJson do pracy z plikami JSON (zapis/odczyt danych)
#include <WiFi.h>                   // Biblioteka WiFi (tryb Access Point ESP32)
#include <WiFiUdp.h>                // Biblioteka obsługująca protokół UDP (komunikacja zdalna)


// ---------------------------------------
// Sekcja: Tworzenie obiektów globalnych
// ---------------------------------------

TFT_eSPI tft = TFT_eSPI();          // Obiekt sterujący wyświetlaczem TFT_eSPI (używa ustawień z pliku User_Setup)
WiFiUDP udp;                        // Obiekt do obsługi komunikacji UDP (odbiór i wysyłanie pakietów)

// ---------------------------------------
// Sekcja: Definicja pinów zgodnych z płytką CYD (ESP32 z TFT i dotykiem)
// ---------------------------------------

// Piny dla karty SD
#define SD_CS   5                   // Pin CS (chip select) dla karty SD
#define SD_SCK  18                  // Pin SCK (zegar SPI)
#define SD_MISO 19                  // Pin MISO (Master In Slave Out)
#define SD_MOSI 23                  // Pin MOSI (Master Out Slave In)

// Piny dla dotyku XPT2046
#define TOUCH_CS   33               // Pin CS (chip select) dla układu dotyku
#define TOUCH_IRQ  36               // Pin IRQ (sygnalizacja dotyku)
#define TOUCH_SCK  25               // Pin SCK (zegar)
#define TOUCH_MISO 39               // Pin MISO
#define TOUCH_MOSI 32               // Pin MOSI

// Tworzenie instancji obiektu dotyku z przypisanymi pinami
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);


// ---------------------------------------
// Sekcja: Ustalona ścieżka pliku JSON na karcie SD
// ---------------------------------------

const char *SD_FILENAME = "/filament.json";   // Nazwa pliku, w którym zapisywane będą dane o filamentach i T/H


// ---------------------------------------
// Sekcja: Struktura pojedynczego slotu filamentu
// ---------------------------------------
// Każdy slot reprezentuje szpulę filamentu z nazwą, masą, pozycją w UI oraz współrzędnymi przycisków

struct Slot {
  const char *name;   // Nazwa slotu (np. "A1", "A2", ...)
  int grams;          // Ilość pozostałego filamentu (w gramach)
  int x, y, w, h;     // Pozycja i rozmiar prostokąta na ekranie
  int btnUseX, btnUseY, btnUseW, btnUseH;     // Współrzędne przycisku „Used”
  int btnResetX, btnResetY, btnResetW, btnResetH; // Współrzędne przycisku „Reset”
};

// Utworzenie tablicy 4 slotów – reprezentującej 4 szpule
Slot slots[4];


// ---------------------------------------
// Sekcja: Definicje kolorów UI (zgodne z biblioteką TFT_eSPI)
// ---------------------------------------

#define BG_COLOR   TFT_BLACK         // Kolor tła ekranu
#define HEAD_COLOR TFT_WHITE         // Kolor tekstu nagłówka
#define SLOT_BG    TFT_DARKGREY      // Kolor tła dla slotu
#define SLOT_HL    TFT_LIGHTGREY     // Kolor tła po zaznaczeniu (highlight)
#define BTN_COLOR  0x001F            // Kolor przycisków „Used” (ciemny niebieski)
#define BTN_TEXT   TFT_WHITE         // Kolor tekstu przycisków


// ---------------------------------------
// Sekcja: Konfiguracja sieci i zmienne UDP
// ---------------------------------------

const char *AP_SSID = "FILAMENT-MONITOR";  // Nazwa sieci Wi-Fi utworzonej przez ESP32 (tryb Access Point)
const char *AP_PASS = "12345678";          // Hasło do sieci Wi-Fi
const int UDP_PORT = 4210;                 // Port UDP, na którym ESP32 nasłuchuje pakietów

char incomingPacket[255];                  // Bufor do odbierania danych UDP (max 255 znaków)
int e1val = 0, e2val = 0, e3val = 0, e4val = 0; // Zmienne do przechowywania odebranych wartości czujników wagowych
float tempval = 0, humval = 0;             // Zmienne do przechowywania temperatury i wilgotności z pakietu UDP


// ---------------------------------------
// Sekcja: Telemetria (do wyświetlenia w nagłówku UI)
// ---------------------------------------

float headerTemp = 0.0;                    // Temperatura (°C)
float headerHum  = 0.0;                    // Wilgotność (%) — obie aktualizowane po odebraniu pakietu UDP


// ---------------------------------------
// Sekcja: Kalibracja mapowania dotyku
// ---------------------------------------
// Wartości te odpowiadają zakresowi surowych odczytów z kontrolera XPT2046
// (można je dopasować, jeśli dotyk nie pokrywa się z ekranem)

const int TOUCH_MIN_X_RAW = 200;           // Minimalna surowa wartość X
const int TOUCH_MAX_X_RAW = 3900;          // Maksymalna surowa wartość X
const int TOUCH_MIN_Y_RAW = 200;           // Minimalna surowa wartość Y
const int TOUCH_MAX_Y_RAW = 3900;          // Maksymalna surowa wartość Y

// =====================================================================================================
//  Sekcja 2: Funkcje pomocnicze dla slotów (układ ekranu) oraz zapis/odczyt danych SD/JSON
// =====================================================================================================

// -------------------------------------------------------------------------------------------
// Funkcja: setupSlotsDefault()
// Cel:     Inicjalizuje tablicę slots[] z domyślnymi wartościami i ustawia pozycje elementów UI
// -------------------------------------------------------------------------------------------

void setupSlotsDefault() {
  int margin = 8;                                   // margines od krawędzi ekranu
  int headerH = 48;                                 // wysokość nagłówka (stała, 48px)
  int slotSpacing = 8;                              // odstęp między slotami
  int totalH = tft.height();                        // pobranie wysokości ekranu z obiektu TFT
  int slotH = (totalH - margin * 2 - headerH - (3 * slotSpacing)) / 4;
                                                    // obliczenie wysokości pojedynczego slotu (równy podział ekranu)
  int y = margin + headerH;                         // współrzędna Y pierwszego slotu (tuż pod nagłówkiem)

  static const char *names[4] = { "A1", "A2", "A3", "A4" };  // domyślne etykiety czterech slotów

  // --- pętla ustawiająca wszystkie cztery sloty ---
  for (int i = 0; i < 4; i++) {
    slots[i].name = names[i];                       // przypisanie nazwy (A1, A2, A3, A4)
    slots[i].grams = 1000;                          // wartość domyślna (1000g filamentu)
    slots[i].x = margin;                            // pozycja X startu slotu (8px od lewej krawędzi)
    slots[i].y = y;                                 // pozycja Y bieżącego slotu
    slots[i].w = tft.width() - margin * 2;          // szerokość slotu (pełna szerokość minus marginesy)
    slots[i].h = slotH;                             // wysokość obliczona powyżej

    // --- Definicja przycisków wewnątrz slotu ---
    int btnW = 70;                                  // szerokość przycisku
    int btnH = 30;                                  // wysokość przycisku
    int spacing = 8;                                // odstęp między przyciskami
    int btnY = slots[i].y + (slots[i].h - btnH) / 2; // centrowanie przycisków w pionie względem slotu

    // przypisanie parametrów przycisku RESET
    slots[i].btnResetW = btnW;
    slots[i].btnResetH = btnH;

    // przypisanie parametrów przycisku USED
    slots[i].btnUseW = btnW;
    slots[i].btnUseH = btnH;

    // --- Pozycjonowanie przycisków po prawej stronie slotu ---
    slots[i].btnResetX = slots[i].x + slots[i].w - btnW - 8; // Reset po prawej krawędzi
    slots[i].btnResetY = btnY;

    slots[i].btnUseX = slots[i].btnResetX - btnW - spacing;  // Used po lewej od Reset
    slots[i].btnUseY = btnY;

    y += slotH + slotSpacing;                      // przesunięcie Y dla kolejnego slotu
  }
}



// =====================================================================================================
//  Sekcja: Zapis danych do pliku SD w formacie JSON
// =====================================================================================================

void saveToSD() {
  StaticJsonDocument<256> doc;                     // Tworzymy bufor dokumentu JSON (256 bajtów)
  
  // Zapis wartości wszystkich czterech slotów do struktury JSON
  for (int i = 0; i < 4; i++)
    doc[String(slots[i].name)] = slots[i].grams;   // np. {"A1":1000, "A2":900, "A3":500, "A4":1000}

  // Dodanie również aktualnych wartości temperatury i wilgotności
  doc["T"] = headerTemp;
  doc["H"] = headerHum;

  // Jeśli plik istnieje, usuwamy go, aby uniknąć błędów przy nadpisywaniu
  if (SD.exists(SD_FILENAME))
    SD.remove(SD_FILENAME);

  // Otwieramy plik w trybie zapisu
  File f = SD.open(SD_FILENAME, FILE_WRITE);
  if (!f) {                                        // jeśli nie udało się otworzyć pliku
    Serial.println("Error opening file for write!"); 
    return;                                        // przerywamy funkcję
  }

  // Serializacja dokumentu JSON do pliku (zapis w formacie tekstowym)
  serializeJson(doc, f);
  f.close();                                       // zamykamy plik po zapisie
  Serial.println("Saved to SD");                   // komunikat debugowy
}



// =====================================================================================================
//  Sekcja: Odczyt danych z pliku SD i aktualizacja zmiennych
// =====================================================================================================

void loadFromSD() {
  // Sprawdzenie czy plik w ogóle istnieje
  if (!SD.exists(SD_FILENAME)) {
    Serial.println("SD file not found - using defaults"); // brak pliku – zostają wartości domyślne
    return;
  }

  // Próba otwarcia pliku w trybie odczytu
  File f = SD.open(SD_FILENAME);
  if (!f) {
    Serial.println("Cannot open SD file");
    return;
  }

  StaticJsonDocument<256> doc;                    // Bufor na dane JSON
  DeserializationError err = deserializeJson(doc, f);  // Próba parsowania zawartości pliku JSON
  f.close();                                      // Zamknięcie pliku po odczycie

  if (err) {                                     // Sprawdzenie błędu parsowania
    Serial.println("JSON parse error");
    return;
  }

  // --- Odczyt wartości gramów dla każdego slotu z pliku ---
  for (int i = 0; i < 4; i++) {
    const char *n = slots[i].name;               // pobranie nazwy slotu (np. "A1")
    if (doc.containsKey(n))                      // sprawdzenie czy klucz istnieje w JSON
      slots[i].grams = doc[n];                   // przypisanie wartości gramów
  }

  // --- Odczyt temperatury i wilgotności (jeśli są zapisane) ---
  headerTemp = doc["T"] | headerTemp;            // operator | = wartość z JSON lub poprzednia
  headerHum  = doc["H"] | headerHum;

  Serial.println("Loaded values from SD");        // komunikat debugowy
}


// =====================================================================================================
//  Sekcja 3: Rysowanie interfejsu użytkownika na wyświetlaczu TFT
// =====================================================================================================

// -------------------------------------------------------------------------------------------
// Funkcja: drawHeader()
// Cel:     Rysuje górny pasek nagłówka z nazwą projektu i danymi T/H (temperatura/wilgotność)
// -------------------------------------------------------------------------------------------

void drawHeader() {
  // Wypełnienie prostokąta (nagłówka) kolorem granatowym (TFT_NAVY)
  tft.fillRect(0, 0, tft.width(), 48, TFT_NAVY);

  // Ustawienie koloru tekstu: biały na tle granatowym
  tft.setTextColor(TFT_WHITE, TFT_NAVY);

  // Ustawienie trybu pozycjonowania tekstu: środek względem punktu odniesienia (MC = middle center)
  tft.setTextDatum(MC_DATUM);

  // Ustawienie czcionki o rozmiarze 4 (duży tytuł)
  tft.setTextFont(4);

  // Rysowanie głównego tytułu aplikacji na środku górnego paska
  tft.drawString("Filament AMS - Licznik", tft.width() / 2, 18);

  // Ustawienie mniejszej czcionki dla wiersza telemetrycznego
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);  // wyrównanie nadal centralne

  // Utworzenie tekstu zawierającego temperaturę i wilgotność, np. "T: 23.4C  H: 45.2%"
  String th = String("T: ") + String(headerTemp, 1) + "C  " + String("H: ") + String(headerHum, 1) + "%";

  // Rysowanie tekstu w dolnej części nagłówka (linia 36px od góry)
  tft.drawString(th, tft.width() / 2, 36);
}



// -------------------------------------------------------------------------------------------
// Funkcja: drawSlot()
// Cel:     Rysuje pojedynczy slot z nazwą, ilością gramów i dwoma przyciskami ("Used" / "Reset")
// Parametry: i - numer slotu (0–3)
//             highlight - flaga (true = podświetlony slot, false = normalny wygląd)
// -------------------------------------------------------------------------------------------

void drawSlot(int i, bool highlight = false) {
  Slot &s = slots[i];                    // skrótowy alias do aktualnego slotu (dla wygody)
  
  uint16_t bg = SLOT_BG;                 // domyślny kolor tła (ciemnoszary)
  if (highlight) bg = SLOT_HL;           // jeśli flaga highlight = true, zmień kolor na jaśniejszy
  if (s.grams < 100) bg = TFT_RED;       // jeśli ilość filamentu < 100g, kolor tła czerwony (alarm)

  // Rysowanie tła prostokąta slotu z zaokrąglonymi rogami (promień 8px)
  tft.fillRoundRect(s.x, s.y, s.w, s.h, 8, bg);

  // Ustawienie koloru tekstu: biały na tle koloru slotu
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(ML_DATUM);            // wyrównanie tekstu do lewej z centrowaniem pionowym
  tft.setTextFont(2);                    // mniejsza czcionka (do etykiety)
  tft.drawString(String(s.name), s.x + 8, s.y + 8);  // wypisanie nazwy slotu w lewym górnym rogu

  // --- Rysowanie wartości (ilości gramów) ---
  tft.setTextDatum(MR_DATUM);            // wyrównanie do prawej
  tft.setTextFont(4);                    // większa czcionka dla wartości liczbowej
  tft.setTextColor((s.grams < 100) ? TFT_BLACK : TFT_WHITE, bg);
                                         // jeśli mniej niż 100g, tekst czarny (kontrast na czerwonym)
  tft.drawString(String(s.grams) + " g", 
                 s.x + s.w - slots[i].btnResetW - 90,  // pozycja x (z lewej strony przycisków)
                 s.y + s.h / 2 - 2);                   // pozycja y (środek slotu)

  // --- Rysowanie przycisku „Used” (odejmowanie zużycia) ---
  tft.fillRoundRect(s.btnUseX, s.btnUseY, s.btnUseW, s.btnUseH, 6, BTN_COLOR); // niebieski przycisk
  tft.setTextColor(BTN_TEXT, BTN_COLOR);             // biały tekst
  tft.setTextDatum(MC_DATUM);                        // tekst centrowany
  tft.setTextFont(2);                                // czcionka średnia
  tft.drawString("Used", s.btnUseX + s.btnUseW / 2, s.btnUseY + s.btnUseH / 2); // napis "Used"

  // --- Rysowanie przycisku „Reset” (ustawienie ponownie na 1000g) ---
  tft.fillRoundRect(s.btnResetX, s.btnResetY, s.btnResetW, s.btnResetH, 6, TFT_RED); // czerwony przycisk
  tft.setTextColor(BTN_TEXT, TFT_RED);
  tft.drawString("Reset", s.btnResetX + s.btnResetW / 2, s.btnResetY + s.btnResetH / 2);
}



// -------------------------------------------------------------------------------------------
// Funkcja: drawUI()
// Cel:     Odświeża cały interfejs — czyści ekran, rysuje nagłówek i wszystkie 4 sloty
// -------------------------------------------------------------------------------------------

void drawUI() {
  tft.fillScreen(BG_COLOR);             // Wypełnienie całego ekranu kolorem tła (czarnym)
  drawHeader();                         // Rysowanie nagłówka z tytułem i T/H
  for (int i = 0; i < 4; i++)           // Iteracja po wszystkich czterech slotach
    drawSlot(i);                        // Rysowanie każdego z nich
}


// =====================================================================================================
//  Sekcja 4: Obsługa dotyku (XPT2046) i interaktywne elementy UI
// =====================================================================================================

// -------------------------------------------------------------------------------------------
// Funkcja: getPoint()
// Cel:     Odczytuje surowe dane z kontrolera dotyku i przelicza je na współrzędne ekranu TFT
// -------------------------------------------------------------------------------------------

TS_Point getPoint() {
  TS_Point p = ts.getPoint();                      // Pobranie surowych danych (X, Y, Z) z dotyku
  // map() – funkcja Arduino przelicza wartość z jednego zakresu do drugiego
  int sx = map(p.x, TOUCH_MIN_X_RAW, TOUCH_MAX_X_RAW, 0, tft.width());
                                                   // Skalowanie wartości X do szerokości ekranu
  int sy = map(p.y, TOUCH_MIN_Y_RAW, TOUCH_MAX_Y_RAW, 0, tft.height());
                                                   // Skalowanie wartości Y do wysokości ekranu
  return TS_Point(sx, sy, p.z);                    // Zwraca przeliczone współrzędne dotyku
}



// -------------------------------------------------------------------------------------------
// Funkcja: getTouchXY()
// Cel:     Sprawdza, czy użytkownik dotknął ekran, odczytuje współrzędne, stosuje „debounce”
// Zwraca:  true – jeśli dotyk wykryty;  false – jeśli nie
// -------------------------------------------------------------------------------------------

bool getTouchXY(int &x, int &y) {
  if (!ts.touched()) return false;                 // Brak dotyku – natychmiastowy powrót
  TS_Point p = getPoint();                         // Pobranie skalibrowanych współrzędnych
  if (p.z < 50) return false;                      // Jeżeli siła nacisku (Z) za mała – ignoruj
  x = p.x;                                         // Przekazanie współrzędnych X/Y przez referencję
  y = p.y;
  delay(120);                                      // Krótkie opóźnienie – eliminacja wielokrotnego odczytu (debounce)
  while (ts.touched()) delay(10);                  // Czekaj, aż użytkownik puści ekran (uniknięcie powtórek)
  return true;                                     // Zwróć true – dotyk poprawny
}



// =====================================================================================================
//  Sekcja: Klawiatura ekranowa (numeryczna)
// =====================================================================================================
// Funkcja ta tworzy okno z przyciskami cyfrowymi 0–9, OK i C (Clear)
// Pozwala użytkownikowi wprowadzić wartość liczbową, np. zużycie filamentu
// ------------------------------------------------------------------------------------------------------

struct KBButton {
  int x, y, w, h;           // Pozycja i rozmiar przycisku
  const char *label;        // Tekst wyświetlany na przycisku
};

String showKeypadAndGet(const char *title) {
  int W = tft.width(), H = tft.height();           // Wymiary ekranu TFT
  int boxW = W - 30, boxH = H - 60;                // Wymiary ramki dialogowej
  int boxX = 15, boxY = 50;                        // Pozycja ramki (lekko odsunięta od krawędzi)

  // --- Rysowanie okna dialogowego ---
  tft.fillRoundRect(boxX, boxY, boxW, boxH, 8, TFT_WHITE);    // tło białe
  tft.drawRoundRect(boxX, boxY, boxW, boxH, 8, TFT_BLACK);    // obramowanie czarne
  tft.setTextColor(TFT_BLACK, TFT_WHITE);                     // czarny tekst
  tft.setTextDatum(TC_DATUM);                                 // tekst centrowany u góry
  tft.setTextFont(2);                                         // średnia czcionka
  tft.drawString(title, boxX + boxW / 2, boxY + 12);          // tytuł okna (np. "Podaj zużycie [g]")

  // --- Wyświetlanie początkowej wartości (0) ---
  String input = "";                                          // bufor tekstowy wprowadzanego numeru
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString("0", boxX + boxW / 2, boxY + 40);            // domyślnie wyświetla „0”

  // --- Etykiety przycisków ---
  const char *labels[12] = { "1", "2", "3", "4", "5", "OK",
                             "6", "7", "8", "9", "0", "C" };

  KBButton keys[12];                                          // tablica przycisków
  int kbX = boxX + 10;                                        // przesunięcie od lewej
  int kbY = boxY + 80;                                        // początek klawiatury poniżej tytułu
  int cols = 6;                                               // liczba kolumn (6 przycisków w rzędzie)
  int spacing = 5;                                            // odstęp między przyciskami
  int cellW = (boxW - 20 - (cols - 1) * spacing) / cols;      // szerokość przycisku
  int cellH = 48;                                             // wysokość przycisku

  // --- Tworzenie i rysowanie przycisków w dwóch rzędach ---
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < cols; c++) {
      int idx = r * cols + c;                                 // indeks przycisku (0–11)
      int x = kbX + c * (cellW + spacing);                    // obliczenie pozycji X
      int y = kbY + r * (cellH + spacing);                    // obliczenie pozycji Y
      keys[idx] = { x, y, cellW, cellH, labels[idx] };        // zapis parametrów w strukturze

      // Rysowanie przycisku (kolor jasnoszary, zaokrąglony)
      tft.fillRoundRect(x, y, cellW, cellH, 4, 0x7BEF);       // kolor RGB565 ≈ jasny szary
      tft.drawRoundRect(x, y, cellW, cellH, 4, TFT_BLACK);    // obramowanie
      tft.setTextColor(TFT_BLACK, 0x7BEF);                    // czarny tekst
      tft.setTextDatum(MC_DATUM);
      tft.setTextFont(2);
      tft.drawString(labels[idx], x + cellW / 2, y + cellH / 2); // wypisanie etykiety
    }
  }

  // --- Pętla obsługująca dotyk przycisków klawiatury ---
  while (1) {
    int tx, ty;
    if (getTouchXY(tx, ty)) {                                 // jeśli dotknięto ekran
      for (int k = 0; k < 12; k++) {                          // przeszukanie wszystkich przycisków
        KBButton &b = keys[k];
        if (tx >= b.x && tx <= b.x + b.w && ty >= b.y && ty <= b.y + b.h) {
          const char *lbl = b.label;                          // pobranie etykiety klikniętego przycisku
          
          if (strcmp(lbl, "C") == 0) input = "";              // przycisk „C” – wyczyść
          else if (strcmp(lbl, "OK") == 0) {                  // przycisk „OK” – zakończ wprowadzanie
            tft.setTextFont(2);
            return input.length() ? input : "0";              // jeśli brak wpisu → zwróć „0”
          } else {
            if (input.length() < 5) input += lbl;             // dopisanie cyfry do bufora (max 5 znaków)
          }

          // --- Aktualizacja wyświetlanej wartości ---
          tft.fillRect(boxX + 8, boxY + 30, boxW - 16, 44, TFT_WHITE);  // wyczyść pole tekstowe
          tft.setTextColor(TFT_BLACK, TFT_WHITE);
          tft.setTextFont(4);
          tft.drawString(input.length() ? input : "0", boxX + boxW / 2, boxY + 40);
        }
      }
    }
    delay(10); // niewielkie opóźnienie dla stabilności
  }
}



// =====================================================================================================
//  Sekcja: Okno potwierdzenia (Confirm Dialog)
// =====================================================================================================
// Funkcja wyświetla okno z komunikatem i dwoma przyciskami: „Anuluj” (czerwony) i „OK” (zielony)
// Zwraca true, jeśli użytkownik potwierdził akcję, lub false, jeśli anulował
// ------------------------------------------------------------------------------------------------------

bool confirmDialog(const char *msg) {
  int W = tft.width(), H = tft.height();           // rozmiary ekranu
  int boxW = W - 80, boxH = 120;                   // wymiary okna dialogowego
  int boxX = 40, boxY = (H - boxH) / 2;            // pozycja (centrowanie)

  // --- Rysowanie okna dialogowego ---
  tft.fillRoundRect(boxX, boxY, boxW, boxH, 8, TFT_WHITE);
  tft.drawRoundRect(boxX, boxY, boxW, boxH, 8, TFT_BLACK);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.drawString(msg, boxX + boxW / 2, boxY + 28); // wyświetlenie komunikatu

  // --- Rysowanie przycisków ---
  int bw = 80, bh = 36;                            // rozmiary przycisków
  int bx1 = boxX + 20;                             // pozycja przycisku „Anuluj”
  int bx2 = boxX + boxW - bw - 20;                 // pozycja przycisku „OK”
  int by = boxY + boxH - bh - 15;                  // pozycja w pionie (dolna część okna)

  // Przyciski: czerwony „Anuluj”
  tft.fillRoundRect(bx1, by, bw, bh, 6, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextFont(2);
  tft.drawString("Anuluj", bx1 + bw / 2, by + bh / 2);

  // Zielony „OK”
  tft.fillRoundRect(bx2, by, bw, bh, 6, TFT_GREEN);
  tft.setTextColor(TFT_WHITE, TFT_GREEN);
  tft.drawString("OK", bx2 + bw / 2, by + bh / 2);

  // --- Pętla obsługująca dotyk ---
  while (1) {
    int tx, ty;
    if (getTouchXY(tx, ty)) {                      // jeśli użytkownik dotknął ekran
      if (tx >= bx1 && tx <= bx1 + bw && ty >= by && ty <= by + bh) {
        drawUI();                                  // odświeżenie głównego interfejsu
        return false;                              // kliknięto „Anuluj”
      }
      if (tx >= bx2 && tx <= bx2 + bw && ty >= by && ty <= by + bh) {
        drawUI();
        return true;                               // kliknięto „OK”
      }
    }
    delay(10);                                     // niewielkie opóźnienie dla płynności
  }
}


// =====================================================================================================
//  Sekcja 5: Akcje użytkownika — obsługa przycisków slotów ("Used" i "Reset")
// =====================================================================================================
//  Ta część kodu odpowiada za reagowanie na dotyk przycisków w każdym slocie.
//  - „Used” → wywołuje klawiaturę ekranową do wpisania zużycia (odejmuje od wartości gramów).
//  - „Reset” → pyta użytkownika o potwierdzenie i ustawia slot z powrotem na 1000 g.
// =====================================================================================================



// -------------------------------------------------------------------------------------------
// Funkcja: handleUse(int idx)
// Cel:     Obsługuje kliknięcie przycisku "Used" (zużycie filamentu)
// Argument: idx – indeks slotu (0–3)
// -------------------------------------------------------------------------------------------

void handleUse(int idx) {
  // --- Wyświetlenie klawiatury ekranowej i pobranie wpisu użytkownika ---
  String s = showKeypadAndGet("Podaj zużycie [g]");   // pytanie o ilość w gramach
  int used = s.toInt();                                // konwersja tekstu na liczbę
  if (used < 0) used = 0;                              // zabezpieczenie – brak wartości ujemnych

  // --- Aktualizacja stanu slotu ---
  slots[idx].grams -= used;                            // odejmujemy zużycie od aktualnego stanu
  if (slots[idx].grams < 0) slots[idx].grams = 0;      // nie pozwalamy zejść poniżej 0

  // --- Zapis i odświeżenie ekranu ---
  saveToSD();                                          // aktualizacja danych w pliku JSON na karcie SD
  drawUI();                                            // odświeżenie całego interfejsu (sloty + nagłówek)
}



// -------------------------------------------------------------------------------------------
// Funkcja: handleReset(int idx)
// Cel:     Obsługuje kliknięcie przycisku "Reset" (przywraca slot do wartości 1000 g)
// Argument: idx – indeks slotu (0–3)
// -------------------------------------------------------------------------------------------

void handleReset(int idx) {
  // --- Potwierdzenie akcji przez użytkownika ---
  if (confirmDialog("Resetuj do 1000 g?")) {           // okno potwierdzenia (OK/Anuluj)
    slots[idx].grams = 1000;                           // reset wartości do 1000 gramów
    saveToSD();                                        // zapis zmian do pliku JSON
    drawUI();                                          // odświeżenie ekranu po operacji
  }
}



// -------------------------------------------------------------------------------------------
// Funkcja: checkTouchMain(int tx, int ty)
// Cel:     Sprawdza, który przycisk został naciśnięty na ekranie głównym
// Argumenty: tx, ty – współrzędne dotyku (zmapowane do ekranu TFT)
// -------------------------------------------------------------------------------------------

void checkTouchMain(int tx, int ty) {
  // Przeszukanie wszystkich czterech slotów
  for (int i = 0; i < 4; i++) {
    Slot &s = slots[i];

    // --- Sprawdzenie przycisku "Used" ---
    if (tx >= s.btnUseX && tx <= s.btnUseX + s.btnUseW && 
        ty >= s.btnUseY && ty <= s.btnUseY + s.btnUseH) {
      handleUse(i);                                   // uruchom akcję „użycie filamentu”
      return;                                         // zakończ po znalezieniu trafienia
    }

    // --- Sprawdzenie przycisku "Reset" ---
    if (tx >= s.btnResetX && tx <= s.btnResetX + s.btnResetW && 
        ty >= s.btnResetY && ty <= s.btnResetY + s.btnResetH) {
      handleReset(i);                                 // uruchom akcję „reset slotu”
      return;
    }
  }
}


// =====================================================================================================
//  Sekcja 6: Odbiór i obsługa pakietów UDP
// =====================================================================================================
//  Funkcja handleUdpPacket() jest wywoływana w każdej pętli loop().
//  Jej zadania:
//
//   1. Sprawdza, czy przyszły nowe dane UDP.
//   2. Jeśli tak:
//
//        - Odczytuje cały pakiet (np. ID:12;E1:10;E2:0;E3:0;E4:0;temp:23.5;hum:38)
//        - Wyciąga z niego wartości czujników:
//            • E1–E4 = zużycie filamentu dla slotów A1–A4
//            • temp  = temperatura
//            • hum   = wilgotność
//
//        - Aktualizuje sloty i nagłówek UI
//        - Zapisuje dane na kartę SD
//        - Odrysowuje UI na nowo
//
//   3. Na sam koniec odsyła ACK: numer ID pakietu,
//      żeby Twój nadajnik mógł potwierdzić odbiór i usunąć dane z kolejki.
//
// =====================================================================================================


void handleUdpPacket() {

  // Próbujemy odebrać pakiet UDP
  int packetSize = udp.parsePacket();

  // Jeśli packetSize > 0 → coś przyszło
  if (packetSize) {

    // Odczyt danych do bufora incomingPacket (max 255 znaków)
    int len = udp.read(incomingPacket, 255);
    if (len > 0) incomingPacket[len] = 0;    // dodajemy terminator stringa

    // Konwersja bufora do obiektu String dla łatwiejszego parsowania
    String data = String(incomingPacket);
    Serial.println("Odebrano: " + data);

    // ----------------------------------------------
    //  Wyłuskiwanie indeksów pól w pakiecie
    // ----------------------------------------------
    int idIndex = data.indexOf("ID:");
    int e1i = data.indexOf("E1:");
    int e2i = data.indexOf("E2:");
    int e3i = data.indexOf("E3:");
    int e4i = data.indexOf("E4:");
    int tempi = data.indexOf("temp:");
    int humi = data.indexOf("hum:");

    // ----------------------------------------------
    //  Odczyt ID pakietu
    //  (wszystko do znaku ';')
    // ----------------------------------------------
    int packetID = data.substring(
                      idIndex + 3,                 // po "ID:"
                      data.indexOf(';', idIndex)   // do średnika
                    ).toInt();


    // ----------------------------------------------
    //  Parsowanie zużycia E1–E4
    // ----------------------------------------------

    if (e1i >= 0)
        e1val = data.substring(e1i + 3, data.indexOf(';', e1i)).toInt();

    if (e2i >= 0)
        e2val = data.substring(e2i + 3, data.indexOf(';', e2i)).toInt();

    if (e3i >= 0)
        e3val = data.substring(e3i + 3, data.indexOf(';', e3i)).toInt();

    if (e4i >= 0)
        e4val = data.substring(e4i + 3, data.indexOf(';', e4i)).toInt();


    // ----------------------------------------------
    //  Parsowanie temperatury i wilgotności
    // ----------------------------------------------

    if (tempi >= 0)
        tempval = data.substring(tempi + 5, data.indexOf(';', tempi)).toFloat();

    if (humi >= 0)
        humval = data.substring(humi + 4).toFloat();  // ostatnia wartość bez ';'


    // ----------------------------------------------
    //  Przepisanie danych do zmiennych nagłówka
    // ----------------------------------------------
    headerTemp = tempval;
    headerHum  = humval;


    // ----------------------------------------------
    //  Odświeżenie temperatury / wilgotności w nagłówku
    // ----------------------------------------------
    tft.setTextDatum(TR_DATUM);           // wyrównanie prawe
    tft.setTextColor(TFT_YELLOW, TFT_NAVY);

    tft.drawString(
        "T: " + String(tempval, 1) + "°C",
        tft.width() - 10, 10, 2
    );

    tft.drawString(
        "H: " + String(humval, 0) + "%",
        tft.width() - 10, 26, 2
    );


    // ----------------------------------------------
    //  Aktualizacja wartości slotów na podstawie E1–E4
    //  (jeśli wartość nie jest zerem)
    // ----------------------------------------------

    if (e1val != 0) slots[0].grams -= e1val;
    if (e2val != 0) slots[1].grams -= e2val;
    if (e3val != 0) slots[2].grams -= e3val;
    if (e4val != 0) slots[3].grams -= e4val;


    // ----------------------------------------------
    //  Ograniczenie wartości slotów — nie mogą spaść poniżej 0
    // ----------------------------------------------
    for (int i = 0; i < 4; i++) {
      if (slots[i].grams < 0)
          slots[i].grams = 0;
    }

    // ----------------------------------------------
    //  Zapis nowych wartości do pliku JSON
    // ----------------------------------------------
    saveToSD();

    // ----------------------------------------------
    //  Odświeżenie całego interfejsu (nagłówek + sloty)
    // ----------------------------------------------
    drawUI();


    // ----------------------------------------------
    //  Wysłanie potwierdzenia odbioru (ACK)
    //  Twój nadajnik musi otrzymać ID pakietu z powrotem
    //  aby usunąć go ze swojej kolejki
    // ----------------------------------------------

    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print(packetID);     // wysyłamy tylko numer ID
    udp.endPacket();

    Serial.printf("ACK #%d wysłany\n", packetID);
  }
}


// =====================================================================================================
//  Sekcja 7: setup()
// =====================================================================================================
//  Funkcja setup() uruchamia wszystkie moduły potrzebne do działania:
//     1. Serial (debugowanie)
//     2. TFT (wyświetlacz)
//     3. Touchscreen (XPT2046)
//     4. SD (karta pamięci)
//     5. Wczytanie danych slotów z pliku JSON
//     6. Uruchomienie trybu Access Point
//     7. Uruchomienie serwera UDP
//
//  Wszystko w tej kolejności, dzięki czemu ekran startowy pojawia się natychmiast,
//  a sieć i UDP ruszają po chwili, gdy sprzęt już działa.
// =====================================================================================================

void setup() {
  // ------------------------------------------
  // 1. Port szeregowy do debugowania
  // ------------------------------------------
  Serial.begin(115200);


  // ------------------------------------------
  // 2. Inicjalizacja wyświetlacza TFT
  // ------------------------------------------
  tft.init();             // start modułu TFT_eSPI
  tft.setRotation(1);     // orientacja pozioma (1 = rotated landscape)
  tft.invertDisplay(1);   // odwrócenie kolorów zgodnie z CYD
  tft.fillScreen(BG_COLOR); // wyczyszczenie ekranu kolorem tła


  // ------------------------------------------
  // 3. Start sterownika dotyku XPT2046
  // ------------------------------------------
  static SPIClass touchSPI(VSPI);  // dedykowany obiekt SPI dla dotyku
  touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);

  if (!ts.begin(touchSPI))
    Serial.println("Touch init failed");
  else {
    ts.setRotation(1);           // dopasowanie osi do orientacji TFT
    Serial.println("Touch OK");
  }


  // ------------------------------------------
  // 4. Start karty SD
  // ------------------------------------------
  static SPIClass sdSPI(HSPI);  // karta SD ma inny kanał SPI
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, sdSPI))
    Serial.println("SD init failed!");
  else
    Serial.println("SD init OK");


  // ------------------------------------------
  // 5. Przygotowanie slotów
  // ------------------------------------------
  setupSlotsDefault();   // ustawienie geometrii + początkowe 1000g
  loadFromSD();          // wczytanie poprzednich wartości z pliku JSON
  drawUI();              // natychmiastowe wyświetlenie interface’u


  // ------------------------------------------
  // 6. Tworzenie Access Pointa
  // ------------------------------------------
  WiFi.softAP(AP_SSID, AP_PASS);  // nazwa: FILAMENT-MONITOR, hasło: 12345678

  IPAddress IP = WiFi.softAPIP();  // pobranie adresu AP (np. 192.168.4.1)
  Serial.print("AP IP: ");
  Serial.println(IP);


  // ------------------------------------------
  // 7. Uruchomienie odbiornika UDP
  // ------------------------------------------
  udp.begin(UDP_PORT);            // nasłuch na porcie 4210
  Serial.printf("UDP listening on %d\n", UDP_PORT);
}


// =====================================================================================================
//  Sekcja 8: loop()
// =====================================================================================================
//  Pętla główna systemu. Minimalistyczna i szybka.
//
//  W każdej iteracji:
//     1. Sprawdza odbiór danych UDP (handleUdpPacket())
//     2. Sprawdza dotyk i reaguje na kliknięcia w UI
//
//  Program nie używa delayów blokujących (poza małymi opóźnieniami na debouncing),
//  dzięki czemu UI jest bardzo responsywne.
// =====================================================================================================

void loop() {

  // -----------------------------------------------------
  // 1. Odbiór danych UDP (jeśli coś przyszło — obsłuży)
  // -----------------------------------------------------
  handleUdpPacket();


  // -----------------------------------------------------
  // 2. Obsługa dotyku
  // -----------------------------------------------------
  int tx, ty;

  // getTouchXY() zwraca true jeśli dotknięto ekran
  if (getTouchXY(tx, ty)) {
      checkTouchMain(tx, ty);  // sprawdzamy, który przycisk został kliknięty
  }

  // Minimalne opóźnienie zapobiegające zapętleniu CPU
  delay(20);
}
