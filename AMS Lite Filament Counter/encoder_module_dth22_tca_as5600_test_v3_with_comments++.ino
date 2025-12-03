#include <Wire.h>         // biblioteka I2C do komunikacji po magistrali I2C
#include <WiFi.h>         // biblioteka WiFi dla ESP32
#include <WiFiUdp.h>      // biblioteka do obsługi protokołu UDP
#include "DHT.h"          // biblioteka do obsługi czujników DHT
#include <math.h>         // biblioteka matematyczna (floor, ceil, abs)

// --- konfiguracja WiFi i UDP ---
WiFiUDP udp;                      // obiekt UDP do wysyłania i odbierania pakietów
const char* ssid = "FILAMENT-MONITOR";   // nazwa sieci WiFi, do której ESP32 ma się połączyć
const char* password = "12345678";       // hasło do sieci WiFi
IPAddress receiverIP(192,168,4,1);       // IP urządzenia odbierającego pakiety
const int receiverPort = 4210;           // port odbiorcy
const int localPort = 4211;              // port lokalny ESP32
bool testMode = false;                   // tryb testowy – symulacja danych zamiast rzeczywistych odczytów

// --- czujnik temperatury/wilgotności ---
#define DHTPIN 23       // pin ESP32, do którego podłączony jest DHT22
#define DHTTYPE DHT22   // typ czujnika DHT
DHT dht(DHTPIN, DHTTYPE);  // utworzenie obiektu DHT

// --- ekspander I2C TCA9548A ---
#define TCA_ADDR 0x70    // adres I2C ekspandera TCA9548A
#define AS5600_ADDR 0x36 // adres I2C czujnika AS5600 (magnetyczny enkoder kąta)

// --- stałe dla przeliczeń filamentu ---
#define STEPS_PER_REV 4096.0                     // liczba impulsów na obrót koła pomiarowego
#define WHEEL_DIAMETER_MM 50.0                   // średnica koła pomiarowego w mm
#define MM_PER_STEP ((PI * WHEEL_DIAMETER_MM) / STEPS_PER_REV) // milimetry przesunięcia filamentu na jeden impuls
#define GRAMS_PER_MM 0.00305                     // gramy filamentu na 1 mm
#define GRAMS_PER_STEP (MM_PER_STEP * GRAMS_PER_MM)           // gramy na impuls

// --- zmienne globalne ---
int8_t encoderDirection[4] = { +1, -1, +1, -1 }; // kierunek obrotu enkoderów dla korekty

long E1 = 0, E2 = 0, E3 = 0, E4 = 0;              // liczniki impulsów dla 4 enkoderów
long tmpE1 = 0, tmpE2 = 0, tmpE3 = 0, tmpE4 = 0; // tymczasowe bufory impulsów (jeśli ACK nie dotarł)
int tmpG1 = 0, tmpG2 = 0, tmpG3 = 0, tmpG4 = 0;  // tymczasowe bufory wysyłanych gramów

float temp = 0.0, hum = 0.0;  // bieżąca temperatura i wilgotność

unsigned long lastSend = 0;       // czas ostatniej wysyłki pakietu (ms)
int packetID = 0;                 // numer pakietu UDP
bool awaitingAck = false;         // flaga oczekiwania na potwierdzenie odbioru (ACK)
unsigned long ackWaitStart = 0;   // czas rozpoczęcia oczekiwania na ACK (ms)

uint16_t lastAngle[4] = {0, 0, 0, 0}; // ostatnie odczytane kąty z czujników AS5600

// --- wykryte czujniki AS5600 ---
uint8_t activeSensors[8];  // tablica przechowująca aktywne kanały ekspandera TCA9548A (maks. 8)
uint8_t sensorCount = 0;   // liczba wykrytych czujników

// --- zmienne do kontroli masy ---
float g1_total = 0, g2_total = 0, g3_total = 0, g4_total = 0;       // całkowita masa od początku
float g1_lastSent = 0, g2_lastSent = 0, g3_lastSent = 0, g4_lastSent = 0; // ostatnio wysłane wartości
float g1_residual = 0, g2_residual = 0, g3_residual = 0, g4_residual = 0; // część dziesiętna gramu nie wysłana

// --- funkcje pomocnicze ---

// Funkcja wykrywająca aktywne czujniki AS5600 na kanałach TCA9548A
void detectAS5600Sensors() {
    Serial.println("🔍 Skanuję kanały TCA9548A...");
    sensorCount = 0; // reset liczby wykrytych czujników

    for (uint8_t i = 0; i < 8; i++) {
        TCASelect(i); // wybór kanału ekspandera
        delay(5);

        Wire.beginTransmission(AS5600_ADDR); 
        if (Wire.endTransmission() == 0) { // jeśli czujnik odpowiada
            Serial.printf("✅ Znaleziono AS5600 na kanale %d\n", i);
            activeSensors[sensorCount++] = i; // zapis kanału jako aktywny
        } else {
            Serial.printf("— Brak czujnika na kanale %d\n", i);
        }
    }

    if (sensorCount == 0) {
        Serial.println("⚠️ Nie wykryto żadnych czujników AS5600!");
    } else {
        Serial.printf("📈 Wykryto %d czujnik(i) AS5600.\n", sensorCount);
    }
}

// Wybór kanału TCA9548A
void TCASelect(uint8_t channel) {
    if (channel > 7) return;   // tylko kanały 0–7
    Wire.beginTransmission(TCA_ADDR);
    Wire.write(1 << channel);  // ustawienie bitu odpowiadającego kanałowi
    Wire.endTransmission();
}

// Odczyt kąta z AS5600
uint16_t readAS5600Angle() {
    Wire.beginTransmission(AS5600_ADDR);
    Wire.write(0x0C);  // rejestr RAW_ANGLE
    Wire.endTransmission();
    Wire.requestFrom(AS5600_ADDR, 2);
    if (Wire.available() == 2) { // jeśli dostępne 2 bajty
        uint16_t high = Wire.read();
        uint16_t low = Wire.read();
        return (high << 8) | low; // połączenie bajtów w 16-bitowy kąt
    }
    return 0xFFFF; // zwracamy 0xFFFF przy błędzie odczytu
}

// Aktualizacja liczników impulsów z czujników AS5600
void updateEncodersFromSensors() {
    for (uint8_t j = 0; j < sensorCount; j++) {
        uint8_t i = activeSensors[j];  // numer aktywnego kanału
        TCASelect(i);
        delay(2);

        uint16_t angle = readAS5600Angle(); // odczyt kąta
        if (angle == 0xFFFF) continue;      // pomijamy jeśli odczyt nieudany

        int diff = (int)angle - (int)lastAngle[j]; // różnica kątów od poprzedniego pomiaru
        if (diff > 2048) diff -= 4096;   // korekta overflow (zakładamy 12-bitowy enkoder)
        if (diff < -2048) diff += 4096;

        diff *= encoderDirection[j];      // uwzględnienie kierunku obrotu

        switch (j) {  // przypisanie różnicy do odpowiedniego licznika impulsów
            case 0: E1 += diff; break;
            case 1: E2 += diff; break;
            case 2: E3 += diff; break;
            case 3: E4 += diff; break;
        }

        lastAngle[j] = angle; // zapamiętanie ostatniego kąta
    }
}

// --- przygotowanie i wysyłka pakietu ---
void prepareAndSendPacket() {
    char msg[128];

    // odczyt temperatury i wilgotności
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan(t)) { // sprawdzenie poprawności odczytu
        Serial.println("Błąd odczytu z DHT22!");
        return;
    } else {
        temp = t;
        hum = h;
    }

    // budowa pakietu UDP (ID pakietu, gramy z każdego enkodera, temp, wilgotność)
    sprintf(msg, "ID:%d;E1:%d;E2:%d;E3:%d;E4:%d;temp:%f;hum:%f",
            packetID, tmpG1, tmpG2, tmpG3, tmpG4, temp, hum);

    udp.beginPacket(receiverIP, receiverPort);
    udp.print(msg);     // wysłanie wiadomości
    udp.endPacket();    // zakończenie pakietu

    Serial.printf("📤 Wysłano pakiet #%d -> %s\n", packetID, msg);
    Serial.printf("Residuals: %.3f, %.3f, %.3f, %.3f\n",
                  g1_residual, g2_residual, g3_residual, g4_residual);

    lastSend = millis(); // zapis czasu wysyłki
}

// --- odbiór ACK ---
void checkForAck() {
    int packetSize = udp.parsePacket(); // sprawdzenie, czy przyszły dane UDP
    if (packetSize) {
        char buffer[32];
        udp.read(buffer, sizeof(buffer)); // odczyt danych
        int ackID = atoi(buffer);          // konwersja na numer pakietu

        if (ackID == packetID) {           // jeśli ACK pasuje do ostatniego pakietu
            Serial.printf("✅ ACK #%d odebrany – potwierdzono pakiet\n", ackID);
            awaitingAck = false;           // koniec oczekiwania
            packetID++;                    // zwiększenie numeru pakietu

            tmpG1 = tmpG2 = tmpG3 = tmpG4 = 0; // wyzerowanie tymczasowych gramów
        }
    }
}

// --- setup ---
void setup() {
    Serial.begin(115200);   // uruchomienie portu szeregowego
    Wire.begin();           // uruchomienie I2C
    dht.begin();            // inicjalizacja DHT22

    Serial.printf("Łączenie z siecią: %s\n", ssid);
    WiFi.begin(ssid, password);  // połączenie z WiFi
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nPołączono z CYD!");
    udp.begin(localPort);         // uruchomienie UDP na porcie lokalnym

    detectAS5600Sensors();        // wykrycie aktywnych enkoderów

    // inicjalizacja startowych kątów dla aktywnych czujników
    for (uint8_t j = 0; j < sensorCount; j++) {
        uint8_t ch = activeSensors[j];
        TCASelect(ch);
        delay(10);
        lastAngle[j] = readAS5600Angle();
    }

    Serial.println("System gotowy – rozpoczęto monitorowanie.");
}

// --- pętla główna ---
void loop() {
    unsigned long now = millis(); // aktualny czas w ms

    // tryb testowy – symulacja losowych impulsów
    if (testMode && now % 1000 < 10) {
        int chosen = random(1,5);
        int delta = random(0,10);
        switch (chosen) {
            case 1: E1 += delta; break;
            case 2: E2 += delta; break;
            case 3: E3 += delta; break;
            case 4: E4 += delta; break;
        }
    }

    // aktualizacja impulsów z rzeczywistych czujników
    if (!testMode) updateEncodersFromSensors();

    // przeliczenie impulsów na gramy i dodanie residuali
    float g1_total = E1*MM_PER_STEP*GRAMS_PER_MM + g1_residual;
    float g2_total = E2*MM_PER_STEP*GRAMS_PER_MM + g2_residual;
    float g3_total = E3*MM_PER_STEP*GRAMS_PER_MM + g3_residual;
    float g4_total = E4*MM_PER_STEP*GRAMS_PER_MM + g4_residual;

    // wyliczenie pełnych gramów do wysłania (floor dla dodatnich, ceil dla ujemnych)
    int sendG1 = (g1_total >= 0) ? floor(g1_total) : ceil(g1_total);
    int sendG2 = (g2_total >= 0) ? floor(g2_total) : ceil(g2_total);
    int sendG3 = (g3_total >= 0) ? floor(g3_total) : ceil(g3_total);
    int sendG4 = (g4_total >= 0) ? floor(g4_total) : ceil(g4_total);

    // sprawdzenie, czy zmiana masy >= 1 g
    bool massChanged = (abs(sendG1) >= 1 || abs(sendG2) >= 1 || abs(sendG3) >= 1 || abs(sendG4) >= 1);

    if (!awaitingAck && now - lastSend >= 10000) { // wysyłka co 10s, jeśli nie czekamy na ACK
        if (massChanged) {
            // zapisanie pełnych gramów do wysłania
            tmpG1 = sendG1;
            tmpG2 = sendG2;
            tmpG3 = sendG3;
            tmpG4 = sendG4;

            // aktualizacja residuali (część dziesiętna)
            g1_residual = g1_total - tmpG1;
            g2_residual = g2_total - tmpG2;
            g3_residual = g3_total - tmpG3;
            g4_residual = g4_total - tmpG4;

            prepareAndSendPacket(); // wysłanie pakietu
            awaitingAck = true;
            ackWaitStart = now;

            E1 = E2 = E3 = E4 = 0; // zerowanie liczników impulsów po wysyłce
        } else {
            Serial.println("Brak zmiany >= 1g – pomijam wysyłkę.");
            Serial.printf("Aktualnie: E1=%.2fg E2=%.2fg E3=%.2fg E4=%.2fg\n",
                          g1_total, g2_total, g3_total, g4_total);
        }
        lastSend = now;
    }

    checkForAck(); // sprawdzenie, czy przyszło ACK

    // brak ACK >10s → doliczenie wysłanych gramów do residuali i liczników
    if (awaitingAck && now - ackWaitStart > 10000) {
        Serial.println("Brak ACK – doliczam wysłane gramy do residuali i liczników, ponawiam wysyłkę...");
        g1_residual += tmpG1;
        g2_residual += tmpG2;
        g3_residual += tmpG3;
        g4_residual += tmpG4;

        E1 += tmpG1;
        E2 += tmpG2;
        E3 += tmpG3;
        E4 += tmpG4;

        awaitingAck = false;
    }
}
