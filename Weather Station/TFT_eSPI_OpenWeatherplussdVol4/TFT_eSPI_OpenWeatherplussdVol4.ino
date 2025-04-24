//Very Important - check data in All_settings.h and inside folder data file t1, t2,t3
//copy the contents of the data folder directly to the sd card
/***************************************************************************************
**                          Load the libraries and settings
***************************************************************************************/
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>  // https://github.com/Bodmer/TFT_eSPI
// Additional functions
#include "GfxUi.h"           // Attached to this sketch
#include "SPIFFS_Support.h"  // Attached to this sketch
#include <WiFi.h>
#include <HTTPClient.h>
// check All_Settings.h for adapting to your needs
#include "All_Settings.h"
#include <JSON_Decoder.h>  // https://github.com/Bodmer/JSON_Decoder
#include <ArduinoJson.h>
#include <SD.h>
#include "NTP_Time.h"  // Attached to this sketch, see that tab for library needs
#include <XPT2046_Bitbang.h> //https://github.com/ddxfish/XPT2046_Bitbang_Arduino_Library/

/***************************************************************************************
**                          Define the globals and class instances
***************************************************************************************/

TFT_eSPI tft = TFT_eSPI();  // Invoke custom library
DynamicJsonDocument current(1024);
DynamicJsonDocument daily(4096);
bool parsed = false;
boolean booted = true;
GfxUi ui = GfxUi(&tft);  // Jpeg and bmpDraw functions TODO: pull outside of a class
long lastDownloadUpdate = millis();
int MAX_DAYS = 5;

const char* wifi_id;
const char* wifi_pwd;

/***************************************************************************************
**                          Select WiFi globals
***************************************************************************************/

#define MOSI_PIN 32
#define MISO_PIN 39
#define CLK_PIN  25
#define CS_PIN   33
#define SD_CS 5 //sd

XPT2046_Bitbang touchscreen(MOSI_PIN, MISO_PIN, CLK_PIN, CS_PIN);
bool firstScreen = true;
DynamicJsonDocument doc(512);
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FONT_SIZE 2

void pokazPlik(const char* filename);
void printTouchToDisplay(int touchX, int touchY);
void testTouch();
void drawButtons();

/***************************************************************************************
**                          Declare prototypes
***************************************************************************************/
void updateData();
void drawProgress(uint8_t percentage, String text);
void drawTime();
void drawCurrentWeather();
void drawForecast();
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex);
const char *getMeteoconIcon(uint16_t id, bool today);
void drawAstronomy();
void drawSeparator(uint16_t y);
void fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int colour);


String strDate(time_t unixTime);
String strTime(time_t unixTime);
int leftOffset(String text, String sub);
int rightOffset(String text, String sub);
int splitIndex(String text);
#define SD_CS 5 //sd

#define AA_FONT_SMALL "fonts/NotoSerif-Regular-12"  // 15 point sans serif bold
#define AA_FONT_MIDIUM "fonts/NotoSerif-Regular-20"
#define AA_FONT_LARGE "fonts/NotoSerif-Regular-28"  // 36 point sans serif bold





/***************************************************************************************
**                         detup
***************************************************************************************/
void setup() {
  Serial.begin(115200);

  // Inicjalizacja karty SD
  if (!SD.begin(SD_CS)) {
    Serial.println("Błąd inicjalizacji karty SD!");
    return;
  }

  tft.begin();
  tft.fillScreen(TFT_BLACK);
  tft.invertDisplay(1);
  SPIFFS.begin();
  listFiles();

// Enable if you want to erase SPIFFS, this takes some time!
// then disable and reload sketch to avoid reformatting on every boot!
#ifdef FORMAT_SPIFFS
  tft.setTextDatum(BC_DATUM);  // Bottom Centre datum
  tft.drawString("Formatting SPIFFS, so wait!", 120, 195);
  SPIFFS.format();
#endif

  // Draw splash screen
  if (SPIFFS.exists("/splash/OpenWeather.jpg") == true) ui.drawJpeg("/splash/OpenWeather.jpg", 0, 40);

  delay(2000);
  touchscreen.setCalibration(0,0,320,240);
  touchscreen.begin();

  drawButtons();


  // Clear bottom section of screen
  tft.fillRect(0, 206, 240, 320 - 206, TFT_BLACK);

  tft.loadFont(AA_FONT_SMALL,SD);
  tft.setTextDatum(BC_DATUM);  // Bottom Centre datum
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

  tft.drawString("Original by: blog.squix.org", 120, 260);
  tft.drawString("Adapted by: Bodmer", 120, 280);
  tft.drawString("Upgrade by: Rav4", 120, 300);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);

  delay(2000);

  //wifi_id = WIFI_SSID;
  //wifi_pwd = WIFI_PASSWORD;

  tft.fillRect(0, 206, 240, 320 - 206, TFT_BLACK);

  tft.drawString("Connecting to WiFi", 120, 240);
  tft.setTextPadding(240);  // Pad next drawString() text to full width to over-write old text

  WiFi.begin(wifi_id, wifi_pwd);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print("no.");
  }
  Serial.println("Polaczono");
  Serial.println();

  tft.setTextDatum(BC_DATUM);
  tft.setTextPadding(240);        // Pad next drawString() text to full width to over-write old text
  tft.drawString(" ", 120, 220);  // Clear line above using set padding width
  tft.drawString("Fetching weather data...", 120, 240);

  // Fetch the time
  udp.begin(localPort);
  syncTime();

  tft.unloadFont();

}

/***************************************************************************************
**                          Loop
***************************************************************************************/
void loop() {

  // Check if we should update weather information
  if (booted || (millis() - lastDownloadUpdate > 1000UL * UPDATE_INTERVAL_SECS)) {
    updateData();
    get_weather_data();
    lastDownloadUpdate = millis();
  }

  // If minute has changed then request new time from NTP server
  if (booted || minute() != lastMinute) {
    // Update displayed time first as we may have to wait for a response
    drawTime();
    lastMinute = minute();

    // Request and synchronise the local clock
    syncTime();

#ifdef SCREEN_SERVER
    screenServer();
#endif
  }

  booted = false;
}

/***************************************************************************************
**                          Fetch the weather data  and update screen
***************************************************************************************/
// Update the Internet based information and update screen
void updateData() {


  if (booted) drawProgress(20, "Aktualizowanie czasu...");
  else fillSegment(22, 22, 0, (int)(20 * 3.6), 16, TFT_NAVY);

  if (booted) drawProgress(50, "Aktualizowanie warunkow...");
  else fillSegment(22, 22, 0, (int)(50 * 3.6), 16, TFT_NAVY);


#ifdef RANDOM_LOCATION  // Randomly choose a place on Earth to test icons etc
  String latitude = "";
  latitude = (random(180) - 90);
  String longitude = "";
  longitude = (random(360) - 180);
  Serial.print("Lat = ");
  Serial.print(latitude);
  Serial.print(", Lon = ");
  Serial.println(longitude);
#endif

  //Tutaj pobiera pogodę zgodnie z current i daily //
  get_weather_data();

  if (parsed) Serial.println("Otrzymano punkty danych");
  else Serial.println("Blad pobierania danych");

  Serial.print("Free heap = ");
  Serial.println(ESP.getFreeHeap(), DEC);

  if (booted) {
    drawProgress(100, "Gotowe...");
    delay(2000);
    tft.fillScreen(TFT_BLACK);
  } else {
    fillSegment(22, 22, 0, 360, 16, TFT_NAVY);
    fillSegment(22, 22, 0, 360, 22, TFT_BLACK);
  }

  if (parsed) {
    tft.loadFont(AA_FONT_SMALL, SD);
    drawCurrentWeather();
    drawForecast();
    drawAstronomy();
    tft.unloadFont();

    // Update the temperature here so we don't need to keep
    // loading and unloading font which takes time
    tft.loadFont(AA_FONT_MIDIUM, SD);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);

    // Font ASCII code 0xB0 is a degree symbol, but o used instead in small font
    tft.setTextPadding(tft.textWidth(" -88"));  // Max width of values

    String weatherText = "";
    weatherText = current["main"]["temp"].as<float>();  // Make it integer temperature
    tft.drawString(weatherText, 215, 95);    //  + "°" symbol is big... use o in small font
    tft.unloadFont();
  } else {
    Serial.println("Blad pobierania pogody");
  }

  // Delete to free up space
}

/***************************************************************************************
**                          Update progress bar
***************************************************************************************/
void drawProgress(uint8_t percentage, String text) {
  tft.loadFont(AA_FONT_SMALL, SD);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(240);
  tft.drawString(text, 120, 260);

  ui.drawProgressBar(10, 269, 240 - 20, 15, percentage, TFT_WHITE, TFT_BLUE);

  tft.setTextPadding(0);
  tft.unloadFont();
}

/***************************************************************************************
**                          Draw the clock digits
***************************************************************************************/
void drawTime() {
  tft.loadFont(AA_FONT_LARGE, SD);

  // Convert UTC to local time, returns zone code in tz1_Code, e.g "GMT"
  time_t local_time = TIMEZONE.toLocal(now(), &tz1_Code);

  String timeNow = "";

  if (hour(local_time) < 10) timeNow += "0";
  timeNow += hour(local_time);
  timeNow += ":";
  if (minute(local_time) < 10) timeNow += "0";
  timeNow += minute(local_time);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 44:44 "));  // String width + margin

  tft.drawString(timeNow, 120, 53);

  drawSeparator(51);

  tft.setTextPadding(0);

  tft.unloadFont();
}

/***************************************************************************************
**                          Draw the current weather
***************************************************************************************/
void drawCurrentWeather() {
  String date = "Aktualizacja: "+ current["name"].as<String>() +" "+ strDate(current["dt"]);
  String weatherText = "None";

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" Updated: Mmm 44 44:44 "));  // String width + margin
  tft.drawString(date, 120, 16);

  String weatherIcon = "";

  String currentSummary = current["main"];
  currentSummary.toLowerCase();

  weatherIcon = getMeteoconIcon(current["weather"][0]["id"], true);

  //uint32_t dt = millis();
  ui.drawBmp("/icon/" + weatherIcon + ".bmp", 0, 53);
  //Serial.print("Icon draw time = "); Serial.println(millis()-dt);

  // Weather Text
  if (language == "en")
    weatherText = current["main"].as<String>();
  else
    weatherText = current["weather"][0]["description"].as<String>();

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);

  int splitPoint = 0;
  int xpos = 235;
  splitPoint = splitIndex(weatherText);

  tft.setTextPadding(xpos - 100);  // xpos - icon width
  if (splitPoint) tft.drawString(weatherText.substring(0, splitPoint), xpos, 69);
  else tft.drawString(" ", xpos, 69);
  tft.drawString(weatherText.substring(splitPoint), xpos, 86);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(0);
  if (units == "metric") tft.drawString("oC", 237, 95);
  else tft.drawString("oF", 237, 95);

  //Temperature large digits added in updateData() to save swapping font here

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  float windSpeed = current["wind"]["speed"].as<float>();
  weatherText = String(windSpeed, 0);
  //podmiana current
  if (units == "metric") weatherText += " m/s";
  else weatherText += " mph";

  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(tft.textWidth("888 m/s"));  // Max string length?
  tft.drawString(weatherText, 124, 136);

  if (units == "imperial") {
    weatherText = current["main"]["pressure"].as<float>() * 0.02953;
    weatherText += " in";
  } else {
    weatherText = current["wind"]["speed"].as<float>();
    weatherText += " hPa";
  }

  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(tft.textWidth(" 8888hPa"));  // Max string length?
  tft.drawString(weatherText, 230, 136);

  int windAngle = (current["wind"]["deg"].as<int>() + 22.5) / 45;
  if (windAngle > 7) windAngle = 0;
  String wind[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
  ui.drawBmp("/wind/" + wind[windAngle] + ".bmp", 101, 86);

  drawSeparator(153);

  tft.setTextDatum(TL_DATUM);  // Reset datum to normal
  tft.setTextPadding(0);       // Reset padding width to none
}

/***************************************************************************************
**                          Draw the 4 forecast columns
***************************************************************************************/
// draws the three forecast columns
void drawForecast() {
  int8_t dayIndex = 0;
    JsonArray list = daily["list"];

  // Usuwamy wpisy z listy inne niż 12:00:00
  for (int i = list.size() - 1; i >= 0; i--) {
    const char* dt_txt = list[i]["dt_txt"];
    
    if (!strstr(dt_txt, "12:00:00")) {
      list.remove(i); // Usuń wszystko, co nie jest z 12:00
    }
  }

  if (weekday(TIMEZONE.toLocal(current["dt"].as<int>(), &tz1_Code)) == weekday(TIMEZONE.toLocal(daily["list"][dayIndex]["dt"].as<int>(), &tz1_Code))){
    dayIndex +=1;
  }

  drawForecastDetail(8, 171, dayIndex++);
  drawForecastDetail(66, 171, dayIndex++);   // was 95
  drawForecastDetail(124, 171, dayIndex++);  // was 180
  drawForecastDetail(182, 171, dayIndex);    // was 180
  drawSeparator(171 + 69);
}

/***************************************************************************************
**                          Draw 1 forecast column at x, y
***************************************************************************************/
// helper for the forecast columns


void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex) {

  if (dayIndex >= MAX_DAYS) return;

  String day = shortDOW[weekday(TIMEZONE.toLocal(daily["list"][dayIndex]["dt"].as<int>(), &tz1_Code))];

  day.toUpperCase();

  tft.setTextDatum(BC_DATUM);

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("WWW"));
  tft.drawString(day, x + 25, y);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("-88   -88"));
  String highTemp = String(daily["list"][dayIndex]["main"]["temp_max"].as<float>(), 0);
  String lowTemp = String(daily["list"][dayIndex]["main"]["temp_min"].as<float>(), 0);
  tft.drawString(highTemp + " " + lowTemp, x + 25, y + 17);
  
  String weatherIcon = getMeteoconIcon(daily["list"][dayIndex]["weather"][0]["id"].as<int>(), false);

  ui.drawBmp("/icon50/" + weatherIcon + ".bmp", x, y + 18);

  tft.setTextPadding(0);  // Reset padding width to none
}

/***************************************************************************************
**                          Draw Sun rise/set, Moon, cloud cover and humidity
***************************************************************************************/
void drawAstronomy() {

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" Last qtr "));
  
  time_t local_time = TIMEZONE.toLocal(current["dt"].as<int>(), &tz1_Code);
  uint16_t y = year(local_time);
  uint8_t m = month(local_time);
  uint8_t d = day(local_time);
  uint8_t h = hour(local_time);
  int ip;
  uint8_t icon = moon_phase(y, m, d, h, &ip);

  tft.drawString(moonPhase[ip], 120, 319);
  ui.drawBmp("/moon/moonphase_L" + String(icon) + ".bmp", 120 - 30, 318 - 16 - 60);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(0);  // Reset padding width to none
  tft.drawString(sunStr, 40, 270);

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 88:88 "));

  String rising = strTime(current["sys"]["sunrise"]) + " ";
  int dt = rightOffset(rising, ":");  // Draw relative to colon to them aligned
  tft.drawString(rising, 40 + dt, 290);

  String setting = strTime(current["sys"]["sunset"]) + " ";
  dt = rightOffset(setting, ":");
  tft.drawString(setting, 40 + dt, 305);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(cloudStr, 195, 260);

  String cloudCover = "";
  cloudCover += current["clouds"]["all"].as<int>();
  cloudCover += "%";

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 100%"));
  tft.drawString(cloudCover, 210, 277);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(humidityStr, 195, 300 - 2);

  String humidity = "";
  humidity += current["main"]["humidity"].as<int>();
  humidity += "%";

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("100%"));
  tft.drawString(humidity, 210, 315);

  tft.setTextPadding(0);  // Reset padding width to none
}

/***************************************************************************************
**                          Get the icon file name from the index number
***************************************************************************************/
const char *getMeteoconIcon(uint16_t id, bool today) {
  //if (today && id / 100 == 8 && (current["dt"].as<int>() < current["sys"]["sunrise"].as<int>() || current["dt"].as<int>() > current["sysd"]["sunset"].as<int>())) id += 1000;
  if (today && id / 100 == 8 && (now() < current["sys"]["sunrise"].as<int>() || now() > current["sysd"]["sunset"].as<int>())) id += 1000;
  
  if (id / 100 == 2) return "thunderstorm";
  if (id / 100 == 3) return "drizzle";
  if (id / 100 == 4) return "unknown";
  if (id == 500) return "lightRain";
  else if (id == 511) return "sleet";
  else if (id / 100 == 5) return "rain";
  if (id >= 611 && id <= 616) return "sleet";
  else if (id / 100 == 6) return "snow";
  if (id / 100 == 7) return "fog";
  if (id == 800) return "clear-day";
  if (id == 801) return "partly-cloudy-day";
  if (id == 802) return "cloudy";
  if (id == 803) return "cloudy";
  if (id == 804) return "cloudy";
  if (id == 1800) return "clear-night";
  if (id == 1801) return "partly-cloudy-night";
  if (id == 1802) return "cloudy";
  if (id == 1803) return "cloudy";
  if (id == 1804) return "cloudy";

  return "unknown";
}

/***************************************************************************************
**                          Draw screen section separator line
***************************************************************************************/
// if you don't want separators, comment out the tft-line
void drawSeparator(uint16_t y) {
  tft.drawFastHLine(10, y, 240 - 2 * 10, 0x4228);
}

/***************************************************************************************
**                          Determine place to split a line line
***************************************************************************************/
// determine the "space" split point in a long string
int splitIndex(String text) {
  uint16_t index = 0;
  while ((text.indexOf(' ', index) >= 0) && (index <= text.length() / 2)) {
    index = text.indexOf(' ', index) + 1;
  }
  if (index) index--;
  return index;
}

/***************************************************************************************
**                          Right side offset to a character
***************************************************************************************/
// Calculate coord delta from end of text String to start of sub String contained within that text
// Can be used to vertically right align text so for example a colon ":" in the time value is always
// plotted at same point on the screen irrespective of different proportional character widths,
// could also be used to align decimal points for neat formatting
int rightOffset(String text, String sub) {
  int index = text.indexOf(sub);
  return tft.textWidth(text.substring(index));
}

/***************************************************************************************
**                          Left side offset to a character
***************************************************************************************/
// Calculate coord delta from start of text String to start of sub String contained within that text
// Can be used to vertically left align text so for example a colon ":" in the time value is always
// plotted at same point on the screen irrespective of different proportional character widths,
// could also be used to align decimal points for neat formatting
int leftOffset(String text, String sub) {
  int index = text.indexOf(sub);
  return tft.textWidth(text.substring(0, index));
}

/***************************************************************************************
**                          Draw circle segment
***************************************************************************************/
// Draw a segment of a circle, centred on x,y with defined start_angle and subtended sub_angle
// Angles are defined in a clockwise direction with 0 at top
// Segment has radius r and it is plotted in defined colour
// Can be used for pie charts etc, in this sketch it is used for wind direction
#define DEG2RAD 0.0174532925  // Degrees to Radians conversion factor
#define INC 2                 // Minimum segment subtended angle and plotting angle increment (in degrees)
void fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int colour) {
  // Calculate first pair of coordinates for segment start
  float sx = cos((start_angle - 90) * DEG2RAD);
  float sy = sin((start_angle - 90) * DEG2RAD);
  uint16_t x1 = sx * r + x;
  uint16_t y1 = sy * r + y;

  // Draw colour blocks every INC degrees
  for (int i = start_angle; i < start_angle + sub_angle; i += INC) {
    // Calculate pair of coordinates for segment end
    int x2 = cos((i + 1 - 90) * DEG2RAD) * r + x;
    int y2 = sin((i + 1 - 90) * DEG2RAD) * r + y;
    tft.fillTriangle(x1, y1, x2, y2, x, y, colour);
    // Copy segment end to segment start for next segment
    x1 = x2;
    y1 = y2;
  }
}


/***************************************************************************************
**             Convert Unix time to a "local time" time string "12:34"
***************************************************************************************/
String strTime(time_t unixTime) {
  time_t local_time = TIMEZONE.toLocal(unixTime, &tz1_Code);

  String localTime = "";

  if (hour(local_time) < 10) localTime += "0";
  localTime += hour(local_time);
  localTime += ":";
  if (minute(local_time) < 10) localTime += "0";
  localTime += minute(local_time);

  return localTime;
}

/***************************************************************************************
**  Convert Unix time to a local date + time string "Oct 16 17:18", ends with newline
***************************************************************************************/
String strDate(time_t unixTime) {
  time_t local_time = TIMEZONE.toLocal(unixTime, &tz1_Code);
  String localDate = "";
  localDate += monthShortStr(month(local_time));
  localDate += " ";
  localDate += day(local_time);
  localDate += " " + strTime(unixTime);

  return localDate;
}

void get_weather_data() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    // Construct the API endpoint
  //https://api.openweathermap.org/data/2.5/weather?lat={lat}&lon={lon}&appid={API key}
  String url_current = "https://api.openweathermap.org/data/2.5/weather?lat=" + latitude + "&lon=" + longitude + "&units=" + units + "&lang=" + language + "&appid=" + api_key;
  //api.openweathermap.org/data/2.5/forecast/daily?lat={lat}&lon={lon}&cnt={cnt}&appid={API key}
  String url_daily = "https://api.openweathermap.org/data/2.5/forecast?lat=" + latitude + "&lon=" + longitude +  "&units=" + units + "&lang=" + language + "&appid=" + api_key;

    // pobieranie bierzacej
    http.begin(url_current);
    int httpCode = http.GET(); // Make the GET request
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DeserializationError error = deserializeJson(current, payload);
        //parsed = true;
        if (!error) {
        } else {
          Serial.print("deserializeJson() failed: ");
          Serial.println(error.c_str());
        }
      }
      else {
        Serial.println("Failed");
      }
    } else {
      Serial.printf("GET request failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end(); // Close connection

    // pobieranie dziennej
    http.begin(url_daily);
    int httpCodeD = http.GET(); // Make the GET request
    if (httpCodeD > 0) {
      if (httpCodeD == HTTP_CODE_OK) {
        String payload = http.getString();
        DeserializationError error = deserializeJson(daily, payload);
        parsed = true;
        if (!error) {
        } else {
          Serial.print("deserializeJson() failed: ");
          Serial.println(error.c_str());
        }
      }
      else {
        Serial.println("Failed");
      }
    } else {
      Serial.printf("GET request failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end(); // Close connection
  } else {
    Serial.println("Not connected to Wi-Fi");
  }
}

void drawButtons(){
  wifi_id="";
  wifi_pwd="";
  firstScreen = true;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  
  // Przycisk 1 (t1)
  ui.drawJpeg("/butt/butt1.jpg", 20, 30);
  tft.setCursor(70, 45);

  // Przycisk 2 (t2)
  ui.drawJpeg("/butt/butt2.jpg", 20, 100);
  tft.setCursor(70, 115);
  
  // Przycisk 3 (t3)
  ui.drawJpeg("/butt/butt3.jpg", 20, 170);
  tft.setCursor(70, 185);
  testTouch();
  return;
}

void testTouch() {
    while (true) {
    Point touch = touchscreen.getTouch();
    //if (touch.x != 0 || touch.y != 320) {
    if ((touch.y > 20 && touch.y < 70 && touch.x > 10 && touch.x < 230) || (touch.y > 100 && touch.y < 140 && touch.x > 20 && touch.x < 230)|| (touch.y > 180 && touch.y < 210 && touch.x > 20 && touch.x < 230)) {      
      Serial.println("touch.x");
      Serial.println(touch.x);
      Serial.println("touch.y");
      Serial.println(touch.y);
      if (firstScreen) {
        printTouchToDisplay(touch.x, touch.y);
      }
      break;  // Wyjście z pętli, przejście do loop()
    }
    delay(150);
  }
}
void printTouchToDisplay(int touchX, int touchY) {
  if (touchX != 0 || touchX != 320){  // Tylko jeżeli dotyk jest wystarczająco silny
    // Sprawdzenie, czy naciśnięto pierwszy przycisk
    if (touchY > 20 && touchY < 61 && touchX > 4 && touchX < 238) {
      pokazPlik("/t1.json");
      return;
    }
    // Sprawdzenie, czy naciśnięto drugi przycisk
    if (touchY > 100 && touchY < 140 && touchX > 4 && touchX < 238) {
      pokazPlik("/t2.json");
      return;
    }
    // Sprawdzenie, czy naciśnięto drugi przycisk
    if (touchY > 180 && touchY < 210 && touchX > 4 && touchX < 238) {
      pokazPlik("/t3.json");
      return;
    }
    drawButtons();
    return;
  }
}

void pokazPlik(const char* filename){
  File file = SD.open(filename);
  tft.setCursor(50,50);
  if (!file) {
    tft.println("Nie można otworzyć pliku");
    return;
  }
    // Parsowanie JSON
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
      tft.print("Błąd parsowania JSON: ");
      tft.println(error.c_str());
      return;
    }
  // Wczytaj zawartość
  wifi_id = doc["ssid"];
  wifi_pwd = doc["pwd"];
  firstScreen = false;
}


/**The MIT License (MIT)
  Copyright (c) 2015 by Daniel Eichhorn
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYBR_DATUM HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  See more at http://blog.squix.ch
*/

//  Changes made by Bodmer:

//  Minor changes to text placement and auto-blanking out old text with background colour padding
//  Moon phase text added (not provided by OpenWeather)
//  Forecast text lines are automatically split onto two lines at a central space (some are long!)
//  Time is printed with colons aligned to tidy display
//  Min and max forecast temperatures spaced out
//  New smart splash startup screen and updated progress messages
//  Display does not need to be blanked between updates
//  Icons nudged about slightly to add wind direction + speed
//  Barometric pressure added

//  Adapted to use the OpenWeather library: https://github.com/Bodmer/OpenWeather
//  Moon phase/rise/set (not provided by OpenWeather) replace with  and cloud cover humidity
//  Created and added new 100x100 and 50x50 pixel weather icons, these are in the
//  sketch data folder, press Ctrl+K to view
//  Add moon icons, eliminate all downloads of icons (may lose server!)
//  Adapted to use anti-aliased fonts, tweaked coords
//  Added forecast for 4th day
//  Added cloud cover and humidity in lieu of Moon rise/set
//  Adapted to be compatible with ESP32

// Changes introduced by Rav4 added support using Api with free key, fixes regarding icon support (time parsing),
// additionally the code has been modified to support files from the data folder so that they can be placed on 
// the sd card, additionally original code that displays a menu for selecting different wifi networks (files should
// be in the main folder of the SD card under the names t1.json, t2.json and t3.json contain the record 
//{"ssid":"ssid_your_network", "pwd":"password_to_network"}, buttons are located in the butt folder and are in jpg files

