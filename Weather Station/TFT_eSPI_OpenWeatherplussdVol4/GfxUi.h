#ifndef _GFX_UI_H
#define _GFX_UI_H

#include <TFT_eSPI.h> // Hardware-specific library

#define FS_NO_GLOBALS // Avoid conflict with SD library File type definition
#include <SD.h>

// JPEG decoder library
#include <JPEGDecoder.h>

// Maximum of 85 for BUFFPIXEL as 3 x this value is stored in an 8-bit variable!
// 32 is an efficient size for SPIFFS due to SPI hardware pipeline buffer size
// A larger value of 80 is better for SD cards
#define BUFFPIXEL 32

class GfxUi {
  public:
    GfxUi(TFT_eSPI *tft);

    // Deklaracje funkcji
    void drawBmp(String filename, uint16_t x, uint16_t y);
    void drawProgressBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t percentage, uint16_t frameColor, uint16_t barColor);
    void jpegInfo();
    void drawJpeg(String filename, int xpos, int ypos);
    void jpegRender(int xpos, int ypos);

  private:
    TFT_eSPI *_tft;

    // Funkcje pomocnicze do odczytu danych z pliku
    uint16_t read16(File &f);
    uint32_t read32(File &f);
};

#endif