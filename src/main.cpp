#include <Arduino.h>
#include <OLEDDisplay.h>
#include <OLEDDisplayFonts.h>
#include <OLEDDisplayUi.h>
#include <SSD1306Wire.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <time.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <sntp.h>
#include <esp_wifi.h>
#include <Preferences.h>

// --- !!! ЗАХИСТ ВІД ПЕРЕЗАВАНТАЖЕНЬ !!! ---
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// --- Configuration ---
const char *ssid = "Redmi Note 11";
const char *password = "123456781";

const int httpPort = 17580;
const char *ntpServer = "pool.ntp.org";
const char *weatherHost = "api.open-meteo.com";
const int weatherPort = 80;

// --- НАЛАШТУВАННЯ ЧАСУ ---
// Правило для України (Київський час): UTC+2 взимку, UTC+3 влітку
const char *tzInfo = "EET-2EEST,M3.5.0/3,M10.5.0/4";

#define I2C_SDA 44
#define I2C_SCL 43
SSD1306Wire display(0x3c, I2C_SDA, I2C_SCL);

#define BUTTON_LEFT_PIN GPIO_NUM_3
#define BUTTON_RIGHT_PIN GPIO_NUM_1
#define BUTTON_PREV_PIN GPIO_NUM_5
#define BUTTON_NEXT_PIN GPIO_NUM_6
#define BATTERY_PIN GPIO_NUM_8

const float R1 = 67000.0;
const float R2 = 67000.0;
const float ADC_MAX_VOLTAGE = 3.3;
const int ADC_RESOLUTION = 4095;
const int INTERACTIVE_TIMEOUT_MS = 6000;

// --- СТРУКТУРА ДАНИХ ---
struct PersistentData
{
  uint32_t magic;
  char lastBG[10];
  char lastDirection[20];
  int lastDelta;
  time_t lastBGSDateTime;
  bool hasLastData;
  bool hasWeatherData;
  time_t lastWeatherFetchTime;
  int weatherCodes[5];
  float tempMax[5];
  float tempMin[5];
  time_t weatherDates[5];
  time_t lastSuccessfulFetchTime;
  float lastBatteryVoltage;
  char fetchStatus[20];
  int failedConnectionCount;
  int savedScreen;
};

RTC_DATA_ATTR PersistentData rtcData;
#define DATA_MAGIC 0xCAFEBABE

Preferences preferences;

volatile bool wifiTaskComplete = false;
volatile bool dataFetchedSuccessfully = false;
volatile bool weatherFetchedSuccessfully = false;
int weatherDayOffset = 0;

// --- Function Prototypes ---
void drawGlucoseScreen(time_t datetimenow, String BG, int age, float batteryVoltage, String bgs0_direction, int delta);
void drawClockScreen(time_t datetimenow, float batteryVoltage);
void drawWeatherScreen(time_t datetimenow);
void updateDisplay();
String getWeatherDescription(int code);
bool syncTimeNTP();
void initDataManagement();
void backupDataToFlash();
int getBatteryPercentage(float voltage);
void adjustTimezone(time_t &timestamp);

// --- Bitmaps ---
const unsigned char ArrowUp[] PROGMEM = {0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0xf8, 0x0f, 0xfc, 0x1f, 0xde, 0x3d, 0xcf, 0x79, 0xc7, 0x71, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01};
const unsigned char ArrowDown[] PROGMEM = {0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc7, 0x71, 0xcf, 0x79, 0xde, 0x3d, 0xfc, 0x1f, 0xf8, 0x0f, 0xf0, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00};
const unsigned char ArrowUpS[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0x00, 0x3f, 0x80, 0x3f, 0xc0, 0x3f, 0xe0, 0x3f, 0xf0, 0x39, 0xf8, 0x38, 0x7c, 0x38, 0x3c, 0x38, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char ArrowDownS[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x3c, 0x38, 0x7c, 0x38, 0xf8, 0x38, 0xf0, 0x39, 0xe0, 0x3f, 0xc0, 0x3f, 0x80, 0x3f, 0x00, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0x00, 0x00, 0x00, 0x00};
const unsigned char ArrowSide[] PROGMEM = {0x80, 0x01, 0x80, 0x03, 0x80, 0x07, 0x00, 0x0f, 0x00, 0x1e, 0x00, 0x3c, 0xff, 0x7f, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x3c, 0x00, 0x1e, 0x00, 0x0f, 0x80, 0x07, 0x80, 0x03, 0x80, 0x01};
const unsigned char ArrowUpD[] PROGMEM = {0x08, 0x10, 0x1c, 0x38, 0x2a, 0x54, 0x49, 0x92, 0x88, 0x11, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10};
const unsigned char ArrowDownD[] PROGMEM = {0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x88, 0x11, 0x49, 0x92, 0x2a, 0x54, 0x1c, 0x38, 0x08, 0x10};

// --- Weather Icons ---
const unsigned char IconSun[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x0f, 0x00, 0x00, 0xfc, 0x3f, 0x00, 0x00, 0xfe, 0x7f, 0x00, 0x80, 0xff, 0xff, 0x01, 0x80, 0xff, 0xff, 0x01, 0xc0, 0xff, 0xff, 0x03, 0xe0, 0xff, 0xff, 0x07, 0xe0, 0xff, 0xff, 0x07, 0xf0, 0xff, 0xff, 0x0f, 0xf0, 0xff, 0xff, 0x0f, 0xf0, 0xff, 0xff, 0x0f, 0xf0, 0xff, 0xff, 0x0f, 0xf0, 0xff, 0xff, 0x0f, 0xe0, 0xff, 0xff, 0x07, 0xe0, 0xff, 0xff, 0x07, 0xc0, 0xff, 0xff, 0x03, 0x80, 0xff, 0xff, 0x01, 0x80, 0xff, 0xff, 0x01, 0x00, 0xfe, 0x7f, 0x00, 0x00, 0xfc, 0x3f, 0x00, 0x00, 0xf0, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char IconCloud[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x03, 0x00, 0x00, 0xFC, 0x0F, 0x00, 0x00, 0x0E, 0x1C, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x03, 0x60, 0x00, 0x80, 0x01, 0x40, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0x60, 0x00, 0x80, 0x01, 0x20, 0x00, 0x00, 0x01, 0x30, 0x00, 0x00, 0x03, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x20, 0x00, 0x00, 0x01, 0x60, 0x00, 0x80, 0x01, 0xC0, 0x00, 0xC0, 0x00, 0x80, 0xFF, 0x7F, 0x00, 0x00, 0xFF, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char IconRain[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x03, 0x00, 0x00, 0xFC, 0x0F, 0x00, 0x00, 0x0E, 0x1C, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x03, 0x60, 0x00, 0x80, 0x01, 0x40, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0x60, 0x00, 0x80, 0x01, 0x20, 0x00, 0x00, 0x01, 0x30, 0x00, 0x00, 0x03, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x20, 0x00, 0x00, 0x01, 0x60, 0x00, 0x80, 0x01, 0xC0, 0x00, 0xC0, 0x00, 0x80, 0xFF, 0x7F, 0x00, 0x00, 0xFF, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x80, 0x01, 0x00, 0x20, 0x40, 0x08, 0x00, 0x20, 0x40, 0x08, 0x00, 0x10, 0x20, 0x04, 0x00, 0x10, 0x20, 0x04, 0x00, 0x08, 0x10, 0x02, 0x00, 0x08, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char IconSnow[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x03, 0x00, 0x00, 0xFC, 0x0F, 0x00, 0x00, 0x0E, 0x1C, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x03, 0x60, 0x00, 0x80, 0x01, 0x40, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0x60, 0x00, 0x80, 0x01, 0x20, 0x00, 0x00, 0x01, 0x30, 0x00, 0x00, 0x03, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x20, 0x00, 0x00, 0x01, 0x60, 0x00, 0x80, 0x01, 0xC0, 0x00, 0xC0, 0x00, 0x80, 0xFF, 0x7F, 0x00, 0x00, 0xFF, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x11, 0x00, 0x00, 0x28, 0x0A, 0x00, 0x00, 0x10, 0x04, 0x00, 0x00, 0x28, 0x0A, 0x00, 0x00, 0x44, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char IconThunder[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x03, 0x00, 0x00, 0xFC, 0x0F, 0x00, 0x00, 0x0E, 0x1C, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x03, 0x60, 0x00, 0x80, 0x01, 0x40, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0x60, 0x00, 0x80, 0x01, 0x20, 0x00, 0x00, 0x01, 0x30, 0x00, 0x00, 0x03, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x20, 0x00, 0x00, 0x01, 0x60, 0x00, 0x80, 0x01, 0xC0, 0x00, 0xC0, 0x00, 0x80, 0xFF, 0x7F, 0x00, 0x00, 0xFF, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// --- Helper Functions ---
float getBatteryVoltage()
{
  float calibration_factor = 1.0752;
  int analogValue = analogRead(BATTERY_PIN);
  float voltageAtADC = (float)analogValue * (ADC_MAX_VOLTAGE / ADC_RESOLUTION);
  float rawVoltage = voltageAtADC * (R1 + R2) / R2;
  return rawVoltage * calibration_factor;
}

// НОВА ФУНКЦІЯ: Перетворює UTC у локальний час за правилом tzInfo
void adjustTimezone(time_t &timestamp)
{
  struct tm timeinfo;
  localtime_r(&timestamp, &timeinfo);
  TimeElements te;
  te.Second = timeinfo.tm_sec;
  te.Minute = timeinfo.tm_min;
  te.Hour = timeinfo.tm_hour;
  te.Wday = timeinfo.tm_wday + 1;
  te.Day = timeinfo.tm_mday;
  te.Month = timeinfo.tm_mon + 1;
  te.Year = timeinfo.tm_year + 1900 - 1970;
  timestamp = makeTime(te);
}

// --- DISPLAY UPDATE MANAGER ---
void updateDisplay()
{
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  time_t current_rtc_time = tv_now.tv_sec; // Системний час (UTC)

  // Час лише для малювання на екрані (Локальний)
  time_t local_display_time = current_rtc_time;
  adjustTimezone(local_display_time);
  setTime(local_display_time);

  if (rtcData.savedScreen == 0)
  {
    if (rtcData.hasLastData)
    {
      // Вік даних рахуємо чисто в UTC, щоб перехід часу не давав збоїв
      int current_data_age = (current_rtc_time - rtcData.lastBGSDateTime) / 60;
      if (current_data_age < 0)
        current_data_age = 0;

      drawGlucoseScreen(local_display_time, String(rtcData.lastBG), current_data_age, rtcData.lastBatteryVoltage, String(rtcData.lastDirection), rtcData.lastDelta);
    }
    else
    {
      display.clear();
      display.displayOn();
      display.setFont(ArialMT_Plain_16);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 25, "Waiting Data...");
      if (strlen(rtcData.fetchStatus) > 0)
      {
        display.setFont(ArialMT_Plain_10);
        display.drawString(64, 53, String(rtcData.fetchStatus));
      }
      display.display();
    }
  }
  else if (rtcData.savedScreen == 1)
  {
    drawClockScreen(local_display_time, rtcData.lastBatteryVoltage);
  }
  else if (rtcData.savedScreen == 2)
  {
    drawWeatherScreen(local_display_time);
  }
}

// --- SCREEN 1: GLUCOSE ---
void drawGlucoseScreen(time_t datetimenow, String BG, int age, float batteryVoltage, String bgs0_direction, int delta)
{
  display.clear();
  display.displayOn();
  int hour24 = hour(datetimenow);
  String timeNow = (hour24 < 10 ? "0" : "") + String(hour24) + ":" + (minute(datetimenow) < 10 ? "0" : "") + String(minute(datetimenow));

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(2, 3, timeNow);
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  String ageStr = (age > 60) ? String(age) + "m !" : String(age) + " min";
  display.drawString(126, 3, ageStr);

  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(45, 19, BG);
  display.drawString(46, 19, BG);

  if (bgs0_direction == "Flat")
    display.drawXbm(101, 26, 16, 16, ArrowSide);
  else if (bgs0_direction == "FortyFiveUp")
    display.drawXbm(101, 26, 16, 16, ArrowUpS);
  else if (bgs0_direction == "FortyFiveDown")
    display.drawXbm(101, 26, 16, 16, ArrowDownS);
  else if (bgs0_direction == "SingleUp")
    display.drawXbm(101, 26, 16, 16, ArrowUp);
  else if (bgs0_direction == "SingleDown")
    display.drawXbm(101, 26, 16, 16, ArrowDown);
  else if (bgs0_direction == "DoubleUp")
    display.drawXbm(101, 26, 16, 16, ArrowUpD);
  else if (bgs0_direction == "DoubleDown")
    display.drawXbm(101, 26, 16, 16, ArrowDownD);

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  int batPercent = getBatteryPercentage(batteryVoltage);
  display.drawString(2, 50, "Bat: " + String(batPercent) + "%");
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  String bgdelta = (delta > 0) ? "+" + String(delta) : String(delta);
  display.drawString(126, 50, bgdelta + " mg/dl ");

  display.drawRect(0, 0, 128, 64);
  display.drawRect(0, 17, 128, 33);
  display.drawVerticalLine(90, 20, 27);
  display.display();
}

// --- SCREEN 2: CLOCK ---
void drawClockScreen(time_t datetimenow, float batteryVoltage)
{
  display.clear();
  display.displayOn();
  String timeStr = (hour(datetimenow) < 10 ? "0" : "") + String(hour(datetimenow)) + ":" + (minute(datetimenow) < 10 ? "0" : "") + String(minute(datetimenow));

  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 15, timeStr);
  display.drawString(65, 15, timeStr);

  display.setFont(ArialMT_Plain_16);
  String dateStr = String(day(datetimenow)) + "." + String(month(datetimenow)) + "." + String(year(datetimenow));
  display.drawString(64, 45, dateStr);

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  int batPercent = getBatteryPercentage(batteryVoltage);
  display.drawString(126, 0, String(batPercent) + "%");
  display.drawRect(0, 0, 128, 64);
  display.display();
}

// --- SCREEN 3: WEATHER ---
void drawWeatherScreen(time_t datetimenow)
{
  display.clear();
  display.displayOn();

  if (!rtcData.hasWeatherData)
  {
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 25, "Waiting Weather...");
    if (strlen(rtcData.fetchStatus) > 0)
      display.drawString(64, 38, String(rtcData.fetchStatus));
    display.display();
    return;
  }

  if (weatherDayOffset < 0)
    weatherDayOffset = 0;
  if (weatherDayOffset > 4)
    weatherDayOffset = 4;

  time_t forecastTime = rtcData.weatherDates[weatherDayOffset];
  adjustTimezone(forecastTime); // Переводимо дату прогнозу в локальний час

  String dateStr = String(day(forecastTime)) + "/" + String(month(forecastTime));
  if (weatherDayOffset == 0)
    dateStr = "Today";
  if (weatherDayOffset == 1)
    dateStr = "Tomorrow";

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, "Lviv: " + dateStr);

  display.setFont(ArialMT_Plain_24);
  String tempStr = String((int)rtcData.tempMax[weatherDayOffset]) + " / " + String((int)rtcData.tempMin[weatherDayOffset]);
  display.drawString(64, 12, tempStr);

  int code = rtcData.weatherCodes[weatherDayOffset];
  const unsigned char *weatherIcon = IconCloud;

  if (code == 0)
    weatherIcon = IconSun;
  else if (code >= 1 && code <= 3)
    weatherIcon = IconCloud;
  else if (code == 45 || code == 48)
    weatherIcon = IconCloud;
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82))
    weatherIcon = IconRain;
  else if (code >= 71 && code <= 77)
    weatherIcon = IconSnow;
  else if (code >= 95)
    weatherIcon = IconThunder;

  display.drawXbm(48, 36, 32, 32, weatherIcon);

  display.setFont(ArialMT_Plain_10);
  if (weatherDayOffset > 0)
    display.drawString(10, 25, "<");
  if (weatherDayOffset < 4)
    display.drawString(118, 25, ">");

  display.drawRect(0, 0, 128, 64);
  display.display();
}

// --- DATA MANAGEMENT FUNCTIONS ---
void backupDataToFlash()
{
  Serial.println("Backing up data to Flash...");
  preferences.begin("glucose_data", false);
  preferences.putBytes("data", &rtcData, sizeof(PersistentData));
  preferences.end();
  Serial.println("Backup complete.");
}

void initDataManagement()
{
  if (rtcData.magic != DATA_MAGIC)
  {
    Serial.println("Data invalid/corrupted. Trying to restore from Flash...");
    preferences.begin("glucose_data", true);
    if (preferences.isKey("data"))
    {
      preferences.getBytes("data", &rtcData, sizeof(PersistentData));
      Serial.println("Data restored from Flash successfully!");
      rtcData.magic = DATA_MAGIC;
    }
    else
    {
      Serial.println("No backup found. Clean start.");
      memset(&rtcData, 0, sizeof(PersistentData));
      rtcData.magic = DATA_MAGIC;
      rtcData.savedScreen = 0;
    }
    preferences.end();
  }
  else
  {
    Serial.println("Warm Boot: RAM Data is valid.");
  }
}

// --- NETWORK CONNECTION ---
bool connectToWiFi()
{
  Serial.print("Connecting to WIFI... ");
  strcpy(rtcData.fetchStatus, "WiFi Init...");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.persistent(false);

  WiFi.begin(ssid, password);

  int connectAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && connectAttempts < 24)
  {
    Serial.print(".");
    delay(500);
    connectAttempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
    strcpy(rtcData.fetchStatus, "WiFi OK");
    rtcData.failedConnectionCount = 0;
    return true;
  }
  else
  {
    Serial.println("\nFailed to connect.");
    strcpy(rtcData.fetchStatus, "WiFi Fail");
    rtcData.failedConnectionCount++;
    if (rtcData.failedConnectionCount < 3)
      ESP.restart();
    else
      rtcData.failedConnectionCount = 0;
    return false;
  }
}

// --- GET READINGS ---
bool getreadings()
{
  WiFiClient client;
  client.setTimeout(8000);

  strcpy(rtcData.fetchStatus, "Conn NS...");
  if (!client.connect(WiFi.gatewayIP(), httpPort))
  {
    strcpy(rtcData.fetchStatus, "NS Conn Err");
    client.stop();
    return false;
  }

  client.print(String("GET /pebble HTTP/1.0\r\nHost: ") + WiFi.gatewayIP().toString() + "\r\nConnection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0)
  {
    if (millis() - timeout > 8000)
    {
      strcpy(rtcData.fetchStatus, "NS Timeout");
      client.stop();
      return false;
    }
    delay(10);
  }

  if (!client.find("\r\n\r\n"))
  {
    strcpy(rtcData.fetchStatus, "Inv Header");
    client.stop();
    return false;
  }

  DynamicJsonDocument doc(3000);
  DeserializationError error = deserializeJson(doc, client);

  if (error || !doc.containsKey("status") || !doc.containsKey("bgs"))
  {
    strcpy(rtcData.fetchStatus, "Data Err");
    client.stop();
    return false;
  }

  String status0_now_str = doc["status"][0]["now"].as<String>();
  if (status0_now_str.length() > 3)
    status0_now_str = status0_now_str.substring(0, status0_now_str.length() - 3);
  time_t status0_now1 = status0_now_str.toInt(); // Отримуємо чистий UTC

  JsonObject bgs0 = doc["bgs"][0];

  String bgs_datetime_str = bgs0["datetime"].as<String>();
  time_t bgs0_datetime2;
  if (bgs_datetime_str.length() >= 10)
    bgs0_datetime2 = bgs_datetime_str.substring(0, 10).toInt();
  else
    bgs0_datetime2 = status0_now1; // Отримуємо чистий UTC

  // Встановлюємо системний час чистим UTC
  struct timeval tv = {.tv_sec = status0_now1};
  settimeofday(&tv, NULL);
  setTime(status0_now1);

  strncpy(rtcData.lastBG, bgs0["sgv"], sizeof(rtcData.lastBG) - 1);
  strncpy(rtcData.lastDirection, bgs0["direction"], sizeof(rtcData.lastDirection) - 1);
  rtcData.lastDelta = bgs0["bgdelta"];
  rtcData.lastBGSDateTime = bgs0_datetime2; // Зберігаємо як UTC
  rtcData.hasLastData = true;

  struct timeval tv_now_check;
  gettimeofday(&tv_now_check, NULL);
  rtcData.lastSuccessfulFetchTime = tv_now_check.tv_sec;
  rtcData.lastBatteryVoltage = getBatteryVoltage();
  strcpy(rtcData.fetchStatus, "");

  Serial.println("Glucose success!");
  backupDataToFlash();

  client.stop();
  return true;
}

// --- SYNC NTP ---
bool syncTimeNTP()
{
  configTzTime(tzInfo, ntpServer);
  struct tm timeinfo;
  return getLocalTime(&timeinfo, 5000);
}

// --- GET WEATHER ---
bool getWeather()
{
  WiFiClient client;
  client.setTimeout(15000);

  if (!client.connect(weatherHost, weatherPort))
  {
    client.stop();
    return false;
  }

  String url = "/v1/forecast?latitude=49.84&longitude=24.03&daily=weathercode,temperature_2m_max,temperature_2m_min&timezone=auto";
  client.print(String("GET ") + url + " HTTP/1.0\r\nHost: " + weatherHost + "\r\nConnection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0)
  {
    if (millis() - timeout > 15000)
    {
      client.stop();
      return false;
    }
  }

  if (!client.find("\r\n\r\n"))
  {
    client.stop();
    return false;
  }

  StaticJsonDocument<200> filter;
  filter["daily"]["weathercode"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;

  DynamicJsonDocument doc(3072);
  DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter));

  if (error || !doc.containsKey("daily"))
  {
    client.stop();
    return false;
  }

  JsonArray daily_code = doc["daily"]["weathercode"];
  JsonArray daily_max = doc["daily"]["temperature_2m_max"];
  JsonArray daily_min = doc["daily"]["temperature_2m_min"];

  if (daily_code.size() == 0)
  {
    client.stop();
    return false;
  }

  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);

  for (int i = 0; i < 5; i++)
  {
    rtcData.weatherCodes[i] = daily_code[i];
    rtcData.tempMax[i] = daily_max[i];
    rtcData.tempMin[i] = daily_min[i];
    rtcData.weatherDates[i] = tv_now.tv_sec + (i * 86400); // Зберігаємо як UTC
  }

  rtcData.hasWeatherData = true;
  rtcData.lastWeatherFetchTime = tv_now.tv_sec;

  backupDataToFlash();

  client.stop();
  return true;
}

// --- Wi-Fi Task ---
void wifiTask(void *parameter)
{
  if (connectToWiFi())
  {
    dataFetchedSuccessfully = getreadings();
    if (!dataFetchedSuccessfully)
    {
      delay(2000);
      dataFetchedSuccessfully = getreadings();
    }
    if (!dataFetchedSuccessfully)
    {
      syncTimeNTP();
      rtcData.lastSuccessfulFetchTime = 0;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    if (!rtcData.hasWeatherData || (tv_now.tv_sec - rtcData.lastWeatherFetchTime > 3600))
    {
      delay(100);
      weatherFetchedSuccessfully = getWeather();
    }
    else
    {
      weatherFetchedSuccessfully = true;
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
  }
  wifiTaskComplete = true;
  vTaskDelete(NULL);
}

int getBatteryPercentage(float voltage)
{
  const float minV = 3.3;
  const float maxV = 4.2;
  if (voltage >= maxV)
    return 100;
  if (voltage <= minV)
    return 0;
  return (int)((voltage - minV) / (maxV - minV) * 100.0);
}

// --- SETUP ---
void setup()
{
  setCpuFrequencyMhz(80);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);

  // Налаштовуємо глобальне середовище для роботи з часом
  setenv("TZ", tzInfo, 1);
  tzset();

  initDataManagement();

  rtcData.lastBatteryVoltage = getBatteryVoltage();

  pinMode(BUTTON_LEFT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PREV_PIN, INPUT_PULLUP);
  pinMode(BUTTON_NEXT_PIN, INPUT_PULLUP);

  display.init();
  display.flipScreenVertically();
  display.setContrast(100);
  display.clear();

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  time_t current_rtc_time = tv_now.tv_sec;

  bool wokeByTimer = (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) || (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED);
  bool wokeByButton = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0);

  long time_since_last_fetch;
  if (rtcData.lastSuccessfulFetchTime == 0)
    time_since_last_fetch = LONG_MAX;
  else if (current_rtc_time < rtcData.lastSuccessfulFetchTime)
  {
    time_since_last_fetch = LONG_MAX;
    rtcData.lastSuccessfulFetchTime = 0;
  }
  else
    time_since_last_fetch = current_rtc_time - rtcData.lastSuccessfulFetchTime;

  bool needsUpdate = wokeByTimer || wokeByButton || (time_since_last_fetch >= 300);

  if (wokeByButton && rtcData.failedConnectionCount > 0)
    needsUpdate = false;

  if (needsUpdate)
  {
    wifiTaskComplete = false;
    dataFetchedSuccessfully = false;
    weatherFetchedSuccessfully = false;
    xTaskCreatePinnedToCore(wifiTask, "WiFiTask", 10000, NULL, 1, NULL, 0);
  }

  if (wokeByButton)
  {
    updateDisplay();
    while (digitalRead(BUTTON_RIGHT_PIN) == LOW)
      delay(50);
    delay(100);

    unsigned long lastInputTime = millis();
    while (millis() - lastInputTime < INTERACTIVE_TIMEOUT_MS)
    {
      if (digitalRead(BUTTON_LEFT_PIN) == LOW)
      {
        rtcData.savedScreen++;
        if (rtcData.savedScreen > 2)
          rtcData.savedScreen = 0;
        weatherDayOffset = 0;
        updateDisplay();
        lastInputTime = millis();
        delay(250);
      }
      if (digitalRead(BUTTON_RIGHT_PIN) == LOW)
      {
        rtcData.savedScreen--;
        if (rtcData.savedScreen < 0)
          rtcData.savedScreen = 2;
        weatherDayOffset = 0;
        updateDisplay();
        lastInputTime = millis();
        delay(250);
      }
      if (rtcData.savedScreen == 2)
      {
        if (digitalRead(BUTTON_PREV_PIN) == LOW)
        {
          if (weatherDayOffset > 0)
            weatherDayOffset--;
          updateDisplay();
          lastInputTime = millis();
          delay(200);
        }
        if (digitalRead(BUTTON_NEXT_PIN) == LOW)
        {
          if (weatherDayOffset < 4)
            weatherDayOffset++;
          updateDisplay();
          lastInputTime = millis();
          delay(200);
        }
      }

      if (needsUpdate && wifiTaskComplete)
      {
        updateDisplay();
        needsUpdate = false;
      }
      delay(20);
    }
    display.displayOff();
  }
  else
  {
    unsigned long startWait = millis();
    while (!wifiTaskComplete && millis() - startWait < 30000)
    {
      if (digitalRead(BUTTON_LEFT_PIN) == LOW || digitalRead(BUTTON_RIGHT_PIN) == LOW)
        break;
      delay(10);
    }
  }

  gettimeofday(&tv_now, NULL);
  current_rtc_time = tv_now.tv_sec;
  long new_time_since = (rtcData.lastSuccessfulFetchTime == 0) ? 300 : (current_rtc_time - rtcData.lastSuccessfulFetchTime);
  if (new_time_since < 0 || new_time_since >= 300)
    new_time_since = 0;
  long remaining = 300 - new_time_since;
  if (remaining <= 0)
    remaining = 300;

  Serial.print("Sleep: ");
  Serial.println(remaining);
  Serial.flush();

  esp_sleep_enable_timer_wakeup(remaining * 1000000ULL);
  esp_sleep_enable_ext0_wakeup(BUTTON_RIGHT_PIN, 0);
  esp_deep_sleep_start();
}

void loop() {}