#include <SD.h>

// Deklaracja funkcji
void listDir(fs::FS &fs, const char *dirname, uint8_t levels);

// -------------------------------------------------------------------------
// List SD card files in a neat format for ESP32 or ESP8266
// -------------------------------------------------------------------------
void listFiles(void) {
  Serial.println();
  Serial.println("SD card files found:");

  listDir(SD, "/", 0);

  Serial.println();
  delay(1000);
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}