#include <Arduino.h>
#include <OLEDDisplay.h>
#include <OLEDDisplayFonts.h>
#include <OLEDDisplayUi.h>
#include <SSD1306Wire.h>
#include <TimeLib.h>
#include <time.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "esp_bt.h"

// --- !!! ЗАХИСТ ВІД ПЕРЕЗАВАНТАЖЕНЬ !!! ---
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// --- НАЛАШТУВАННЯ ЧАСУ ---
const char *tzInfo = "EET-2EEST,M3.5.0/3,M10.5.0/4";

#define I2C_SDA 44
#define I2C_SCL 43
SSD1306Wire display(0x3c, I2C_SDA, I2C_SCL);

#define BUTTON_LEFT_PIN GPIO_NUM_3
#define BUTTON_RIGHT_PIN GPIO_NUM_1
#define BUTTON_PREV_PIN GPIO_NUM_5
#define BUTTON_NEXT_PIN GPIO_NUM_6
#define BATTERY_PIN GPIO_NUM_8

#define VIBE_PIN 7

const float R1 = 67000.0;
const float R2 = 67000.0;
const float ADC_MAX_VOLTAGE = 3.3;
const int ADC_RESOLUTION = 4095;

// --- BLE UUIDs ---
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define DATA_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define SETTINGS_CHAR_UUID "a1b2c3d4-e5f6-7890-1234-56789abcdef0"

#define MAX_ALARMS 10

struct AlarmDef
{
  int hour;
  int minute;
  uint8_t daysMask;
  bool enabled;
  int lastDay;
};

// --- СТРУКТУРА ДАНИХ ---
struct PersistentData
{
  uint32_t magic;
  char lastBG[10];
  char lastDirection[20];
  float lastDelta;
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
  uint16_t bgHistory[64];
  int savedScreen;
  int screenTimeoutMs;
  bool nightMode;

  float targetMax;
  float targetMin;

  int graphHours;

  AlarmDef alarms[MAX_ALARMS];
  time_t snoozeTimestamp;
  int snoozedAlarmIndex;

  bool glucVibeEnabled;
  time_t glucSnoozeUntil;

  int highScore; // Збереження рекорду для міні-гри
};

RTC_DATA_ATTR PersistentData rtcData;
#define DATA_MAGIC 0xBEEF000D // Оновлено магічне число
Preferences preferences;

// --- СТАТУСИ ---
volatile bool bleTaskComplete = false;
volatile bool newDataReceived = false;
volatile bool deviceConnected = false;
volatile bool screenNeedsUpdate = false;
volatile bool needsVibration = false;
int weatherDayOffset = 0;
int alarmPageOffset = 0;
volatile int settingsCursor = 0;
volatile bool viewingAlarms = false;

// --- Bitmaps ---
const unsigned char ArrowUp[] PROGMEM = {0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0xf8, 0x0f, 0xfc, 0x1f, 0xde, 0x3d, 0xcf, 0x79, 0xc7, 0x71, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01};
const unsigned char ArrowDown[] PROGMEM = {0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc7, 0x71, 0xcf, 0x79, 0xde, 0x3d, 0xfc, 0x1f, 0xf8, 0x0f, 0xf0, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00};
const unsigned char ArrowUpS[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0x00, 0x3f, 0x80, 0x3f, 0xc0, 0x3f, 0xe0, 0x3f, 0xf0, 0x39, 0xf8, 0x38, 0x7c, 0x38, 0x3c, 0x38, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char ArrowDownS[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x3c, 0x38, 0x7c, 0x38, 0xf8, 0x38, 0xf0, 0x39, 0xe0, 0x3f, 0xc0, 0x3f, 0x80, 0x3f, 0x00, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0x00, 0x00, 0x00, 0x00};
const unsigned char ArrowSide[] PROGMEM = {0x80, 0x01, 0x80, 0x03, 0x80, 0x07, 0x00, 0x0f, 0x00, 0x1e, 0x00, 0x3c, 0xff, 0x7f, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x3c, 0x00, 0x1e, 0x00, 0x0f, 0x80, 0x07, 0x80, 0x03, 0x80, 0x01};
const unsigned char ArrowUpD[] PROGMEM = {0x08, 0x10, 0x1c, 0x38, 0x2a, 0x54, 0x49, 0x92, 0x88, 0x11, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10};
const unsigned char ArrowDownD[] PROGMEM = {0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x88, 0x11, 0x49, 0x92, 0x2a, 0x54, 0x1c, 0x38, 0x08, 0x10};

const uint8_t W_Sun[] PROGMEM = {0x80, 0x01, 0x80, 0x01, 0x00, 0x00, 0x04, 0x20, 0x28, 0x14, 0xc0, 0x03, 0x20, 0x04, 0x13, 0xc8, 0x13, 0xc8, 0x20, 0x04, 0xc0, 0x03, 0x28, 0x14, 0x04, 0x20, 0x00, 0x00, 0x80, 0x01, 0x80, 0x01};
const uint8_t W_Cloud[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x03, 0xc0, 0x0f, 0xe0, 0x1f, 0xf0, 0x3f, 0xf8, 0x7f, 0xfc, 0xff, 0xfc, 0xff, 0xfc, 0xff, 0xf8, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t W_Rain[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x80, 0x03, 0xc0, 0x0f, 0xe0, 0x1f, 0xf0, 0x3f, 0xf8, 0x7f, 0xfc, 0xff, 0xfc, 0xff, 0xfc, 0xff, 0xf8, 0x7f, 0x00, 0x00, 0x24, 0x24, 0x12, 0x48, 0x24, 0x24, 0x12, 0x48};
const uint8_t W_Snow[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x80, 0x03, 0xc0, 0x0f, 0xe0, 0x1f, 0xf0, 0x3f, 0xf8, 0x7f, 0xfc, 0xff, 0xfc, 0xff, 0xfc, 0xff, 0xf8, 0x7f, 0x00, 0x00, 0x42, 0x42, 0x24, 0x24, 0x42, 0x42, 0x00, 0x00};
const uint8_t W_Thunder[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x80, 0x03, 0xc0, 0x0f, 0xe0, 0x1f, 0xf0, 0x3f, 0xf8, 0x7f, 0xfc, 0xff, 0xfc, 0xff, 0xfc, 0xff, 0xf8, 0x7f, 0x00, 0x00, 0x20, 0x04, 0x60, 0x0c, 0xc0, 0x03, 0x40, 0x01};

// СПРАЙТИ ДЛЯ ГРИ (16x16)
const uint8_t DinoBmp[] PROGMEM = {
    0x00, 0xFF, 0x00, 0xBF, 0x00, 0xFF, 0x00, 0x0F,
    0x00, 0xFF, 0x01, 0x1F, 0x03, 0x0F, 0x07, 0x0F,
    0xFE, 0x07, 0xFC, 0x07, 0xF8, 0x01, 0xF0, 0x00,
    0x90, 0x00, 0x90, 0x00, 0x98, 0x01, 0x00, 0x00};

const uint8_t CactusBmp[] PROGMEM = {
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x8C, 0x11,
    0x8C, 0x19, 0x8C, 0x19, 0x8C, 0x19, 0xFC, 0x19,
    0xF8, 0x19, 0x80, 0x19, 0x80, 0x1F, 0x80, 0x0F,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01};

const char *const _DOW_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *const _MON_NAMES[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

const uint8_t font5x7[95][5] PROGMEM = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x2f, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7f, 0x14, 0x7f, 0x14}, {0x24, 0x2a, 0x7f, 0x2a, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62}, {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00}, {0x00, 0x1c, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1c, 0x00}, {0x14, 0x08, 0x3e, 0x08, 0x14}, {0x08, 0x08, 0x3e, 0x08, 0x08}, {0x00, 0x00, 0x50, 0x30, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00}, {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14}, {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06}, {0x32, 0x49, 0x79, 0x41, 0x3e}, {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36}, {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f}, {0x00, 0x41, 0x7f, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f}, {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01}, {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7f, 0x41, 0x41, 0x00}, {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7f, 0x00}, {0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40}, {0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78}, {0x7f, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20}, {0x38, 0x44, 0x44, 0x48, 0x7f}, {0x38, 0x54, 0x54, 0x54, 0x18}, {0x08, 0x7e, 0x09, 0x01, 0x02}, {0x0c, 0x52, 0x52, 0x52, 0x3e}, {0x7f, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7d, 0x40, 0x00}, {0x20, 0x40, 0x44, 0x3d, 0x00}, {0x7f, 0x10, 0x28, 0x44, 0x00}, {0x00, 0x41, 0x7f, 0x40, 0x00}, {0x7c, 0x04, 0x18, 0x04, 0x78}, {0x7c, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38}, {0x7c, 0x14, 0x14, 0x14, 0x08}, {0x08, 0x14, 0x14, 0x18, 0x7c}, {0x7c, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20}, {0x04, 0x3f, 0x44, 0x40, 0x20}, {0x3c, 0x40, 0x40, 0x20, 0x7c}, {0x1c, 0x20, 0x40, 0x20, 0x1c}, {0x3c, 0x40, 0x30, 0x40, 0x3c}, {0x44, 0x28, 0x10, 0x28, 0x44}, {0x0c, 0x50, 0x50, 0x50, 0x3c}, {0x44, 0x64, 0x54, 0x4c, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00}, {0x00, 0x00, 0x7f, 0x00, 0x00}, {0x00, 0x41, 0x36, 0x08, 0x00}, {0x10, 0x08, 0x10, 0x08, 0x00}};

// --- Helper Functions ---
float getBatteryVoltage()
{
  float calibration_factor = 1.0752;
  int analogValue = analogRead(BATTERY_PIN);
  float voltageAtADC = (float)analogValue * (ADC_MAX_VOLTAGE / ADC_RESOLUTION);
  return (voltageAtADC * (R1 + R2) / R2) * calibration_factor;
}

int getBatteryPercentage(float voltage)
{
  if (voltage >= 4.2)
    return 100;
  if (voltage <= 3.3)
    return 0;
  return (int)((voltage - 3.3) / 0.9 * 100.0);
}

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

void backupDataToFlash()
{
  preferences.begin("glucose_data", false);
  preferences.putBytes("data", &rtcData, sizeof(PersistentData));
  preferences.end();
}

void initDataManagement()
{
  if (rtcData.magic != DATA_MAGIC)
  {
    preferences.begin("glucose_data", true);
    if (preferences.isKey("data"))
    {
      preferences.getBytes("data", &rtcData, sizeof(PersistentData));
      rtcData.magic = DATA_MAGIC;
    }
    else
    {
      memset(&rtcData, 0, sizeof(PersistentData));
      rtcData.magic = DATA_MAGIC;
      rtcData.savedScreen = 0;
      rtcData.screenTimeoutMs = 6000;
      rtcData.nightMode = false;
      rtcData.highScore = 0;

      rtcData.targetMax = 10.0;
      rtcData.targetMin = 3.9;
      rtcData.graphHours = 5;

      for (int i = 0; i < MAX_ALARMS; i++)
      {
        rtcData.alarms[i].enabled = false;
        rtcData.alarms[i].lastDay = -1;
      }
      rtcData.snoozeTimestamp = 0;
      rtcData.snoozedAlarmIndex = -1;

      rtcData.glucVibeEnabled = true;
      rtcData.glucSnoozeUntil = 0;
    }
    preferences.end();
  }
}

void drawRetroText(int startX, int startY, String text, int scale)
{
  for (int i = 0; i < text.length(); i++)
  {
    char c = text.charAt(i);
    if (c >= 32 && c <= 126)
    {
      int idx = c - 32;
      for (int col = 0; col < 5; col++)
      {
        uint8_t line = font5x7[idx][col];
        for (int row = 0; row < 7; row++)
        {
          if (line & (1 << row))
          {
            display.fillRect(startX + col * scale, startY + row * scale, scale, scale);
          }
        }
      }
    }
    startX += 6 * scale;
  }
}

int getRetroTextWidth(String text, int scale)
{
  return text.length() * 6 * scale;
}

void drawChunkyIcon(int startX, int startY, const uint8_t *icon)
{
  for (int row = 0; row < 16; row++)
  {
    uint8_t b1 = pgm_read_byte(&icon[row * 2]);
    uint8_t b2 = pgm_read_byte(&icon[row * 2 + 1]);
    uint16_t line = b1 | (b2 << 8);
    for (int col = 0; col < 16; col++)
    {
      if (line & (1 << col))
      {
        display.fillRect(startX + col * 2, startY + row * 2, 2, 2);
      }
    }
  }
}

// Функція для малювання 16x16 спрайтів без масштабування
void drawSprite16(int startX, int startY, const uint8_t *icon)
{
  for (int row = 0; row < 16; row++)
  {
    uint8_t b1 = pgm_read_byte(&icon[row * 2]);
    uint8_t b2 = pgm_read_byte(&icon[row * 2 + 1]);
    uint16_t line = b1 | (b2 << 8);
    for (int col = 0; col < 16; col++)
    {
      if (line & (1 << col))
      {
        display.setPixel(startX + col, startY + row);
      }
    }
  }
}

// --- SCREENS ---
void drawGlucoseScreen(time_t datetimenow, String BG, int age, float batteryVoltage, String bgs0_direction, float delta)
{
  display.clear();
  display.displayOn();

  int hour24 = hour(datetimenow);
  String timeNow = (hour24 < 10 ? "0" : "") + String(hour24) + ":" + (minute(datetimenow) < 10 ? "0" : "") + String(minute(datetimenow));

  if (deviceConnected)
  {
    drawRetroText(2, 4, "CONN", 1);
  }
  else
  {
    drawRetroText(2, 4, timeNow, 1);
  }

  String ageStr = (age > 60) ? String(age) + "m !" : String(age) + " min";
  int ageW = getRetroTextWidth(ageStr, 1);
  drawRetroText(126 - ageW, 4, ageStr, 1);

  int bgW = getRetroTextWidth(BG, 3);
  drawRetroText(45 - (bgW / 2), 22, BG, 3);

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

  String batStr = "Bat: " + String(getBatteryPercentage(batteryVoltage)) + "%";
  drawRetroText(2, 53, batStr, 1);

  String bgdelta = (delta > 0) ? "+" + String(delta, 1) : String(delta, 1);
  bgdelta += " mmol/l";
  int deltaW = getRetroTextWidth(bgdelta, 1);
  drawRetroText(126 - deltaW, 53, bgdelta, 1);

  display.drawRect(0, 0, 128, 64);
  display.drawRect(0, 17, 128, 33);
  display.drawVerticalLine(90, 20, 27);

  display.display();
}

void drawClockScreen(time_t datetimenow, float batteryVoltage)
{
  display.clear();
  display.displayOn();

  String status = deviceConnected ? "CONN" : "BLE";
  drawRetroText(0, 0, status, 1);

  String bgStr = rtcData.hasLastData ? String(rtcData.lastBG) : "---";
  int bgWidth = getRetroTextWidth(bgStr, 1);
  drawRetroText(64 - (bgWidth / 2), 0, bgStr, 1);

  String batStr = String(getBatteryPercentage(batteryVoltage)) + "%";
  int batWidth = getRetroTextWidth(batStr, 1);
  drawRetroText(128 - batWidth, 0, batStr, 1);

  display.drawHorizontalLine(0, 9, 128);

  String timeStr = (hour(datetimenow) < 10 ? "0" : "") + String(hour(datetimenow)) + ":" + (minute(datetimenow) < 10 ? "0" : "") + String(minute(datetimenow));
  int timeWidth = getRetroTextWidth(timeStr, 3);
  drawRetroText(64 - (timeWidth / 2), 18, timeStr, 3);

  int wd = weekday(datetimenow) - 1;
  int mo = month(datetimenow);
  String dateStr = String(_DOW_NAMES[wd]) + " " + String(_MON_NAMES[mo]) + " " + String(day(datetimenow));

  int dateWidth = getRetroTextWidth(dateStr, 1);
  drawRetroText(64 - (dateWidth / 2), 48, dateStr, 1);

  display.display();
}

void drawWeatherScreen(time_t datetimenow)
{
  display.clear();
  display.displayOn();

  if (!rtcData.hasWeatherData)
  {
    String msg = "No Weather Data";
    int w = getRetroTextWidth(msg, 1);
    drawRetroText(64 - (w / 2), 25, msg, 1);
    display.display();
    return;
  }

  if (weatherDayOffset < 0)
    weatherDayOffset = 0;
  if (weatherDayOffset > 4)
    weatherDayOffset = 4;

  time_t forecastTime = rtcData.weatherDates[weatherDayOffset];
  adjustTimezone(forecastTime);

  int wd = weekday(forecastTime) - 1;
  int mo = month(forecastTime);
  String dateStr = String(_DOW_NAMES[wd]) + " " + String(_MON_NAMES[mo]) + " " + String(day(forecastTime));

  String headerStr = "Lviv: " + dateStr;
  if (weatherDayOffset == 0)
    headerStr = "Today: " + dateStr;

  int headerW = getRetroTextWidth(headerStr, 1);
  drawRetroText(64 - (headerW / 2), 2, headerStr, 1);

  String tempStr = String((int)rtcData.tempMax[weatherDayOffset]) + "/" + String((int)rtcData.tempMin[weatherDayOffset]);
  int tempW = getRetroTextWidth(tempStr, 2);
  drawRetroText(64 - (tempW / 2), 14, tempStr, 2);

  int code = rtcData.weatherCodes[weatherDayOffset];
  const uint8_t *weatherIcon = W_Cloud;

  if (code == 0)
    weatherIcon = W_Sun;
  else if (code >= 1 && code <= 3)
    weatherIcon = W_Cloud;
  else if (code == 45 || code == 48)
    weatherIcon = W_Cloud;
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82))
    weatherIcon = W_Rain;
  else if (code >= 71 && code <= 77)
    weatherIcon = W_Snow;
  else if (code >= 95)
    weatherIcon = W_Thunder;

  drawChunkyIcon(48, 30, weatherIcon);

  if (weatherDayOffset > 0)
  {
    drawRetroText(8, 42, "<", 1);
  }
  if (weatherDayOffset < 4)
  {
    drawRetroText(114, 42, ">", 1);
  }

  display.display();
}

void drawGraphScreen(time_t datetimenow)
{
  display.clear();
  display.displayOn();

  drawRetroText(2, 2, "HISTORY", 1);

  String currBg = rtcData.hasLastData ? String(rtcData.lastBG) : "---";
  int w = getRetroTextWidth(currBg, 1);
  drawRetroText(126 - w, 2, currBg, 1);

  display.drawHorizontalLine(0, 11, 128);

  int bgMaxInt = (int)(rtcData.targetMax * 18.015);
  int bgMinInt = (int)(rtcData.targetMin * 18.015);

  if (bgMaxInt > 250)
    bgMaxInt = 250;
  if (bgMinInt < 40)
    bgMinInt = 40;

  int yMax = 54 - ((bgMaxInt - 40) * 40 / 210);
  int yMin = 54 - ((bgMinInt - 40) * 40 / 210);

  // Цільовий діапазон (Y-вісь)
  drawRetroText(0, yMax - 3, String(rtcData.targetMax, 1), 1);
  drawRetroText(0, yMin - 3, String(rtcData.targetMin, 1), 1);

  for (int x = 26; x < 128; x += 4)
  {
    display.setPixel(x, yMax);
    display.setPixel(x, yMin);
  }

  time_t baseTime = datetimenow;
  if (rtcData.hasLastData && rtcData.lastBGSDateTime > 0)
  {
    baseTime = rtcData.lastBGSDateTime;
    adjustTimezone(baseTime);
  }

  int hoursSpan = rtcData.graphHours;
  if (hoursSpan < 1 || hoursSpan > 5)
    hoursSpan = 5;
  int maxPoints = hoursSpan * 12;
  int startIndex = 64 - maxPoints;

  int currentMin = minute(baseTime);
  int currentHour = hour(baseTime);
  int offsetPoints = currentMin / 5;

  // Малюємо вертикальні лінії-підказки та ПОЗНАЧКИ ЧАСУ (годин) на осі X
  for (int h = 0; h <= hoursSpan; h++)
  {
    int pointIndex = 63 - offsetPoints - (h * 12);

    if (pointIndex >= startIndex && pointIndex < 64)
    {
      int x = 26 + ((pointIndex - startIndex) * 100 / (maxPoints - 1));

      // Вертикальний пунктир гратки
      for (int y = 14; y <= 54; y += 4)
      {
        display.setPixel(x, y);
      }

      // Години знизу графіка
      int displayHour = currentHour - h;
      if (displayHour < 0)
        displayHour += 24;

      String hStr = String(displayHour);
      int tw = getRetroTextWidth(hStr, 1);

      // Вирівнювання тексту, щоб не вилазив за краї екрана
      int textX = x - tw / 2;
      if (textX < 26)
        textX = 26;
      if (textX + tw > 128)
        textX = 128 - tw;

      display.drawVerticalLine(x, 54, 3); // Зарубка на осі X
      drawRetroText(textX, 57, hStr, 1);  // Сама цифра години
    }
  }

  // Малюємо самі точки графіку
  for (int i = startIndex; i < 64; i++)
  {
    if (rtcData.bgHistory[i] > 0)
    {
      int bg = rtcData.bgHistory[i];
      if (bg > 250)
        bg = 250;
      if (bg < 40)
        bg = 40;

      int x = 26 + ((i - startIndex) * 100 / (maxPoints - 1));
      int y = 54 - ((bg - 40) * 40 / 210);

      display.fillRect(x, y, 2, 2);
    }
  }

  display.display();
}

void drawAlarmsScreen(time_t datetimenow)
{
  display.clear();
  display.displayOn();

  drawRetroText(2, 2, "ALARMS LIST", 1);
  display.drawHorizontalLine(0, 11, 128);

  int enabledCount = 0;
  int activeAlarms[MAX_ALARMS];

  for (int i = 0; i < MAX_ALARMS; i++)
  {
    if (rtcData.alarms[i].enabled)
    {
      activeAlarms[enabledCount] = i;
      enabledCount++;
    }
  }

  if (enabledCount == 0)
  {
    int w = getRetroTextWidth("NO ACTIVE ALARMS", 1);
    drawRetroText(64 - w / 2, 30, "NO ACTIVE ALARMS", 1);
    display.display();
    return;
  }

  int maxOffset = (enabledCount > 4) ? enabledCount - 4 : 0;
  if (alarmPageOffset > maxOffset)
    alarmPageOffset = maxOffset;
  if (alarmPageOffset < 0)
    alarmPageOffset = 0;

  int y = 14;
  for (int i = alarmPageOffset; i < min(alarmPageOffset + 4, enabledCount); i++)
  {
    int idx = activeAlarms[i];
    String hStr = (rtcData.alarms[idx].hour < 10 ? "0" : "") + String(rtcData.alarms[idx].hour);
    String mStr = (rtcData.alarms[idx].minute < 10 ? "0" : "") + String(rtcData.alarms[idx].minute);

    String timeStr = hStr + ":" + mStr;

    uint8_t mask = rtcData.alarms[idx].daysMask;
    String daysStr = "";

    if (mask == 127)
      daysStr = "ALL DAYS";
    else if (mask == 62)
      daysStr = "WORKDAYS";
    else if (mask == 65)
      daysStr = "WEEKEND";
    else
    {
      if (mask & 2)
        daysStr += "M ";
      if (mask & 4)
        daysStr += "T ";
      if (mask & 8)
        daysStr += "W ";
      if (mask & 16)
        daysStr += "T ";
      if (mask & 32)
        daysStr += "F ";
      if (mask & 64)
        daysStr += "S ";
      if (mask & 1)
        daysStr += "S";
    }

    drawRetroText(2, y, timeStr, 1);
    drawRetroText(40, y, daysStr, 1);
    y += 11;
  }

  if (alarmPageOffset > 0)
    drawRetroText(118, 14, "^", 1);
  if (alarmPageOffset < maxOffset)
    drawRetroText(118, 45, "v", 1);

  display.display();
}

void drawSettingsScreen(time_t datetimenow)
{
  if (viewingAlarms)
  {
    drawAlarmsScreen(datetimenow);
    return;
  }

  display.clear();
  display.displayOn();

  drawRetroText(2, 0, "MENU", 1);
  display.drawHorizontalLine(0, 9, 128);

  String items[5] = {
      "ALARMS LIST",
      "MINI GAME",
      "GLUC VIBE: " + String(rtcData.glucVibeEnabled ? "ON" : "OFF"),
      "SNOOZE 1 HOUR",
      "SNOOZE 8 HOURS"};

  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  if (rtcData.glucSnoozeUntil > tv_now.tv_sec)
  {
    int remains = (rtcData.glucSnoozeUntil - tv_now.tv_sec) / 60;
    items[3] = "SNOOZED (" + String(remains) + "m)";
    items[4] = "CANCEL SNOOZE";
  }

  int startIdx = 0;
  if (settingsCursor > 3)
    startIdx = settingsCursor - 3;

  for (int i = 0; i < 4; i++)
  {
    int itemIdx = startIdx + i;
    if (itemIdx >= 5)
      break;

    int y = 13 + i * 12;
    if (settingsCursor == itemIdx)
    {
      drawRetroText(0, y, ">", 1);
    }
    drawRetroText(8, y, items[itemIdx], 1);
  }

  display.display();
}

void updateDisplay()
{
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  time_t current_rtc_time = tv_now.tv_sec;

  time_t local_display_time = current_rtc_time;
  adjustTimezone(local_display_time);

  if (rtcData.nightMode)
    display.invertDisplay();
  else
    display.normalDisplay();

  if (rtcData.savedScreen == 0)
  {
    if (rtcData.hasLastData)
    {
      int current_data_age = (current_rtc_time - rtcData.lastBGSDateTime) / 60;
      if (current_data_age < 0)
        current_data_age = 0;
      drawGlucoseScreen(local_display_time, String(rtcData.lastBG), current_data_age, rtcData.lastBatteryVoltage, String(rtcData.lastDirection), rtcData.lastDelta);
    }
    else
    {
      display.clear();
      display.displayOn();
      String waitMsg = "Waiting BLE...";
      int waitW = getRetroTextWidth(waitMsg, 1);
      drawRetroText(64 - (waitW / 2), 25, waitMsg, 1);
      if (strlen(rtcData.fetchStatus) > 0)
      {
        int statW = getRetroTextWidth(String(rtcData.fetchStatus), 1);
        drawRetroText(64 - (statW / 2), 53, String(rtcData.fetchStatus), 1);
      }
      if (deviceConnected)
      {
        String connMsg = "[CONNECTED]";
        int connW = getRetroTextWidth(connMsg, 1);
        drawRetroText(64 - (connW / 2), 10, connMsg, 1);
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
  else if (rtcData.savedScreen == 3)
  {
    drawGraphScreen(local_display_time);
  }
  else if (rtcData.savedScreen == 4)
  {
    drawSettingsScreen(local_display_time);
  }
}

// --- NATIVE BLE CALLBACKS ---
class ServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
    screenNeedsUpdate = true;
    Serial.println("Телефон ПІДКЛЮЧИВСЯ!");
  }
  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
    screenNeedsUpdate = true;
    Serial.println("Телефон ВІД'ЄДНАВСЯ!");
  }
};

class DataCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string rxValue = pCharacteristic->getValue();
    String payload = "";
    for (int i = 0; i < rxValue.length(); i++)
    {
      payload += rxValue[i];
    }
    payload.trim();

    String values[20];
    int count = 0, startIndex = 0;
    int commaIndex = payload.indexOf(',');

    while (commaIndex != -1 && count < 20)
    {
      values[count] = payload.substring(startIndex, commaIndex);
      values[count].trim();
      count++;
      startIndex = commaIndex + 1;
      commaIndex = payload.indexOf(',', startIndex);
    }
    if (startIndex < payload.length() && count < 20)
    {
      values[count] = payload.substring(startIndex);
      values[count].trim();
      count++;
    }

    if (count >= 4)
    {
      int newTime = values[3].toInt();

      float rawBg = values[0].toFloat();
      int graphBg = (rawBg < 30.0) ? (int)(rawBg * 18.01) : (int)rawBg;

      if (newTime != rtcData.lastBGSDateTime)
      {
        float checkBgMmol = (rawBg < 30.0) ? rawBg : (rawBg / 18.01);
        if (checkBgMmol > rtcData.targetMax || checkBgMmol < rtcData.targetMin)
        {
          struct timeval tv_now;
          gettimeofday(&tv_now, NULL);
          if (rtcData.glucVibeEnabled && tv_now.tv_sec > rtcData.glucSnoozeUntil)
          {
            needsVibration = true;
          }
        }
      }

      if (rtcData.lastBGSDateTime == 0)
      {
        rtcData.bgHistory[63] = graphBg;
      }
      else if (newTime > rtcData.lastBGSDateTime)
      {
        int diffSeconds = newTime - rtcData.lastBGSDateTime;
        int shiftSlots = (diffSeconds + 150) / 300;

        if (shiftSlots <= 0)
          shiftSlots = 1;

        if (shiftSlots >= 64)
        {
          memset(rtcData.bgHistory, 0, sizeof(rtcData.bgHistory));
          rtcData.bgHistory[63] = graphBg;
        }
        else
        {
          for (int i = 0; i < 64 - shiftSlots; i++)
          {
            rtcData.bgHistory[i] = rtcData.bgHistory[i + shiftSlots];
          }
          for (int i = 64 - shiftSlots; i < 63; i++)
          {
            rtcData.bgHistory[i] = 0;
          }
          rtcData.bgHistory[63] = graphBg;
        }
      }

      strncpy(rtcData.lastBG, values[0].c_str(), sizeof(rtcData.lastBG) - 1);
      rtcData.lastBG[sizeof(rtcData.lastBG) - 1] = '\0';

      strncpy(rtcData.lastDirection, values[1].c_str(), sizeof(rtcData.lastDirection) - 1);
      rtcData.lastDirection[sizeof(rtcData.lastDirection) - 1] = '\0';

      rtcData.lastDelta = values[2].toFloat();
      rtcData.lastBGSDateTime = newTime;
      rtcData.hasLastData = true;

      rtcData.lastSuccessfulFetchTime = rtcData.lastBGSDateTime;
      rtcData.lastBatteryVoltage = getBatteryVoltage();
      strcpy(rtcData.fetchStatus, "BLE OK");

      newDataReceived = true;
    }

    if (count >= 19)
    {
      for (int i = 0; i < 5; i++)
      {
        int baseIndex = 4 + (i * 3);
        rtcData.weatherCodes[i] = values[baseIndex].toInt();
        rtcData.tempMax[i] = values[baseIndex + 1].toFloat();
        rtcData.tempMin[i] = values[baseIndex + 2].toFloat();
        rtcData.weatherDates[i] = rtcData.lastBGSDateTime + (i * 86400);
      }
      rtcData.hasWeatherData = true;
      rtcData.lastWeatherFetchTime = rtcData.lastBGSDateTime;
    }

    if (count >= 4)
    {
      backupDataToFlash();
    }
  }
};

class SettingsCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string rxValue = pCharacteristic->getValue();
    String payload = "";
    for (int i = 0; i < rxValue.length(); i++)
    {
      payload += rxValue[i];
    }
    payload.trim();

    if (payload.startsWith("TIME:"))
    {
      time_t newTime = payload.substring(5).toInt();
      if (newTime > 1600000000)
      {
        struct timeval tv = {.tv_sec = newTime};
        settimeofday(&tv, NULL);
        newDataReceived = true;
      }
    }
    else if (payload.startsWith("CONF:"))
    {
      if (payload.indexOf("TIMEOUT=") != -1)
      {
        int tIndex = payload.indexOf("TIMEOUT=") + 8;
        int tEnd = payload.indexOf(";", tIndex);
        if (tEnd == -1)
          tEnd = payload.length();
        int sec = payload.substring(tIndex, tEnd).toInt();
        if (sec >= 5 && sec <= 60)
          rtcData.screenTimeoutMs = sec * 1000;
      }
      if (payload.indexOf("NIGHT=") != -1)
      {
        int nIndex = payload.indexOf("NIGHT=") + 6;
        char nVal = payload.charAt(nIndex);
        rtcData.nightMode = (nVal == '1');
      }
      if (payload.indexOf("MAX=") != -1)
      {
        int idx = payload.indexOf("MAX=") + 4;
        int end = payload.indexOf(";", idx);
        if (end == -1)
          end = payload.length();
        rtcData.targetMax = payload.substring(idx, end).toFloat();
      }
      if (payload.indexOf("MIN=") != -1)
      {
        int idx = payload.indexOf("MIN=") + 4;
        int end = payload.indexOf(";", idx);
        if (end == -1)
          end = payload.length();
        rtcData.targetMin = payload.substring(idx, end).toFloat();
      }

      if (payload.indexOf("ALMCLEAR=1") != -1)
      {
        for (int i = 0; i < MAX_ALARMS; i++)
        {
          rtcData.alarms[i].enabled = false;
        }
      }

      for (int i = 0; i < MAX_ALARMS; i++)
      {
        String key = "ALM" + String(i) + "=";
        int idx = payload.indexOf(key);
        if (idx != -1)
        {
          idx += key.length();
          int hour = payload.substring(idx, idx + 2).toInt();
          int min = payload.substring(idx + 3, idx + 5).toInt();

          int comma1 = payload.indexOf(',', idx);
          int comma2 = payload.indexOf(',', comma1 + 1);
          int end = payload.indexOf(';', comma2);
          if (end == -1)
            end = payload.length();

          int mask = payload.substring(comma1 + 1, comma2).toInt();
          bool en = (payload.substring(comma2 + 1, end).toInt() == 1);

          rtcData.alarms[i].hour = hour;
          rtcData.alarms[i].minute = min;
          rtcData.alarms[i].daysMask = mask;
          rtcData.alarms[i].enabled = en;
          rtcData.alarms[i].lastDay = -1;
        }
      }

      backupDataToFlash();
      newDataReceived = true;
    }
  }
};

void bleTask(void *parameter)
{
  BLEDevice::init("GlucoWatch_ESP");

  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N12);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_N12);

  BLEDevice::setMTU(256);

  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *pDataChar = pService->createCharacteristic(
      DATA_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  pDataChar->setCallbacks(new DataCallbacks());

  BLECharacteristic *pSetChar = pService->createCharacteristic(
      SETTINGS_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  pSetChar->setCallbacks(new SettingsCallbacks());

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  unsigned long absoluteStart = millis();
  unsigned long dataGraceStart = 0;

  while (true)
  {
    unsigned long elapsed = millis() - absoluteStart;

    if (!deviceConnected && elapsed > 10000)
    {
      if (!newDataReceived)
        strcpy(rtcData.fetchStatus, "BLE Sleep");
      break;
    }

    if (elapsed > 15000)
    {
      break;
    }

    if (newDataReceived && dataGraceStart == 0)
    {
      dataGraceStart = millis();
    }

    if (dataGraceStart > 0 && (millis() - dataGraceStart > 2000))
    {
      break;
    }

    delay(50);
  }

  BLEDevice::deinit(true);
  bleTaskComplete = true;
  vTaskDelete(NULL);
}

// РУШІЙ МІНІ-ГРИ З ДИНОЗАВРОМ І ЗБЕРЕЖЕННЯМ РЕКОРДУ
void playMiniGame()
{
  bool inGame = true;
  float playerY = 34; // Базова висота через 16x16 спрайт
  float velocity = 0;
  float gravity = 0.8;
  int obsX = 128;
  int score = 0;
  bool gameOver = false;

  display.displayOn();

  while (inGame)
  {
    display.clear();

    if (gameOver)
    {
      int w = getRetroTextWidth("GAME OVER", 2);
      drawRetroText(64 - w / 2, 15, "GAME OVER", 2);

      String scoreStr = "SCORE: " + String(score);
      int sw = getRetroTextWidth(scoreStr, 1);
      drawRetroText(64 - sw / 2, 35, scoreStr, 1);

      String hiStr = "HI: " + String(rtcData.highScore);
      int hw = getRetroTextWidth(hiStr, 1);
      drawRetroText(64 - hw / 2, 45, hiStr, 1);

      display.display();

      // Рестарт гри
      if (digitalRead(BUTTON_PREV_PIN) == LOW || digitalRead(BUTTON_NEXT_PIN) == LOW)
      {
        playerY = 34;
        velocity = 0;
        obsX = 128;
        score = 0;
        gameOver = false;
        delay(300);
      }
      // Вихід з гри
      if (digitalRead(BUTTON_LEFT_PIN) == LOW)
      {
        inGame = false;
        delay(300);
      }
    }
    else
    {
      // Стрибок
      if ((digitalRead(BUTTON_PREV_PIN) == LOW || digitalRead(BUTTON_NEXT_PIN) == LOW) && playerY >= 34)
      {
        velocity = -6.0;
      }
      // Вихід
      if (digitalRead(BUTTON_LEFT_PIN) == LOW)
      {
        inGame = false;
        delay(300);
      }

      // Фізика
      velocity += gravity;
      playerY += velocity;
      if (playerY > 34)
      {
        playerY = 34;
        velocity = 0;
      }

      // Рух перешкоди
      obsX -= (3 + (score / 5));
      if (obsX < -16)
      {
        obsX = 128;
        score++;
      }

      // Точне зіткнення
      if (obsX < 22 && obsX + 12 > 14 && playerY + 14 > 36)
      {
        gameOver = true;

        // Збереження нового рекорду в незалежну пам'ять
        if (score > rtcData.highScore)
        {
          rtcData.highScore = score;
          backupDataToFlash();
        }
      }

      // Малюємо Динозавра та Кактус
      drawSprite16(10, (int)playerY, DinoBmp);
      drawSprite16(obsX, 34, CactusBmp);

      // Рахунок та Рекорд
      drawRetroText(64 - getRetroTextWidth(String(score), 1) / 2, 2, String(score), 1);

      String hiStr = "HI:" + String(rtcData.highScore);
      drawRetroText(126 - getRetroTextWidth(hiStr, 1), 2, hiStr, 1);

      display.drawHorizontalLine(0, 50, 128); // Земля

      display.display();
    }

    delay(30);
  }
}

void setup()
{
  setCpuFrequencyMhz(80);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);

  pinMode(VIBE_PIN, OUTPUT);
  digitalWrite(VIBE_PIN, LOW);

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

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  time_t current_rtc_time = tv_now.tv_sec;

  bool wokeByTimer = (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER);
  bool wokeByButton = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0);
  bool coldBoot = (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED);

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

  bool needsUpdate = wokeByTimer || coldBoot || wokeByButton || (time_since_last_fetch >= 300);

  if (needsUpdate)
  {
    bleTaskComplete = false;
    xTaskCreatePinnedToCore(bleTask, "BLETask", 10000, NULL, 1, NULL, 0);
  }
  else
  {
    bleTaskComplete = true;
  }

  // --- ЛОГІКА ТРИГЕРА БУДИЛЬНИКА ---
  time_t local_display_time = current_rtc_time;
  adjustTimezone(local_display_time);

  bool triggerAlarm = false;
  int currentAlarmIndex = -1;

  int currentDayOfWeek = weekday(local_display_time) - 1;
  int currentDayOfMonth = day(local_display_time);

  for (int i = 0; i < MAX_ALARMS; i++)
  {
    if (rtcData.alarms[i].enabled)
    {
      if (rtcData.alarms[i].daysMask & (1 << currentDayOfWeek))
      {
        if (hour(local_display_time) == rtcData.alarms[i].hour &&
            minute(local_display_time) >= rtcData.alarms[i].minute &&
            minute(local_display_time) < rtcData.alarms[i].minute + 5)
        {

          if (currentDayOfMonth != rtcData.alarms[i].lastDay)
          {
            triggerAlarm = true;
            currentAlarmIndex = i;
            break;
          }
        }
      }
    }
  }

  if (rtcData.snoozeTimestamp > 0 && current_rtc_time >= rtcData.snoozeTimestamp)
  {
    triggerAlarm = true;
    currentAlarmIndex = rtcData.snoozedAlarmIndex;
  }

  if (triggerAlarm && currentAlarmIndex != -1)
  {
    display.clear();
    display.displayOn();
    int w = getRetroTextWidth("WAKE UP!", 2);
    drawRetroText(64 - w / 2, 10, "WAKE UP!", 2);

    String tStr = (rtcData.alarms[currentAlarmIndex].hour < 10 ? "0" : "") + String(rtcData.alarms[currentAlarmIndex].hour) + ":" + (rtcData.alarms[currentAlarmIndex].minute < 10 ? "0" : "") + String(rtcData.alarms[currentAlarmIndex].minute);
    int tw = getRetroTextWidth(tStr, 1);
    drawRetroText(64 - tw / 2, 30, tStr, 1);

    drawRetroText(0, 55, "<SNOOZE", 1);
    int stopW = getRetroTextWidth("STOP>", 1);
    drawRetroText(128 - stopW, 55, "STOP>", 1);
    display.display();

    unsigned long alarmStart = millis();
    bool snoozed = false;

    while (millis() - alarmStart < 30000)
    {
      digitalWrite(VIBE_PIN, HIGH);
      delay(200);
      digitalWrite(VIBE_PIN, LOW);
      delay(200);

      if (digitalRead(BUTTON_LEFT_PIN) == LOW)
      {
        snoozed = true;
        break;
      }
      if (digitalRead(BUTTON_RIGHT_PIN) == LOW)
      {
        break;
      }
    }

    digitalWrite(VIBE_PIN, LOW);

    if (snoozed)
    {
      rtcData.snoozeTimestamp = current_rtc_time + 300;
      rtcData.snoozedAlarmIndex = currentAlarmIndex;
    }
    else
    {
      rtcData.alarms[currentAlarmIndex].lastDay = currentDayOfMonth;
      rtcData.snoozeTimestamp = 0;
    }
    backupDataToFlash();
  }

  if (wokeByButton || coldBoot || triggerAlarm)
  {
    updateDisplay();

    if (wokeByButton)
    {
      while (digitalRead(BUTTON_RIGHT_PIN) == LOW)
        delay(50);
      delay(100);
    }

    unsigned long lastInputTime = millis();
    int currentTimeout = (rtcData.screenTimeoutMs > 0) ? rtcData.screenTimeoutMs : 6000;
    bool screenRefreshedByBLE = false;

    while (millis() - lastInputTime < currentTimeout)
    {

      if (digitalRead(BUTTON_LEFT_PIN) == LOW)
      {
        if (rtcData.savedScreen == 4 && viewingAlarms)
        {
          viewingAlarms = false;
        }
        else
        {
          rtcData.savedScreen++;
          if (rtcData.savedScreen > 4)
            rtcData.savedScreen = 0;
          weatherDayOffset = 0;
          alarmPageOffset = 0;
          settingsCursor = 0;
          viewingAlarms = false;
        }
        updateDisplay();
        lastInputTime = millis();
        delay(250);
      }
      if (digitalRead(BUTTON_RIGHT_PIN) == LOW)
      {
        if (rtcData.savedScreen == 4 && viewingAlarms)
        {
          viewingAlarms = false;
        }
        else
        {
          rtcData.savedScreen--;
          if (rtcData.savedScreen < 0)
            rtcData.savedScreen = 4;
          weatherDayOffset = 0;
          alarmPageOffset = 0;
          settingsCursor = 0;
          viewingAlarms = false;
        }
        updateDisplay();
        lastInputTime = millis();
        delay(250);
      }

      if (digitalRead(BUTTON_PREV_PIN) == LOW)
      {
        if (rtcData.savedScreen == 2)
        {
          if (weatherDayOffset > 0)
            weatherDayOffset--;
        }
        else if (rtcData.savedScreen == 3)
        {
          if (rtcData.graphHours > 1)
            rtcData.graphHours--;
        }
        else if (rtcData.savedScreen == 4)
        {
          if (viewingAlarms)
          {
            if (alarmPageOffset > 0)
              alarmPageOffset--;
          }
          else
          {
            if (settingsCursor == 0)
            {
              viewingAlarms = true;
            }
            else if (settingsCursor == 1)
            {
              playMiniGame();
              lastInputTime = millis();
              screenNeedsUpdate = true;
            }
            else if (settingsCursor == 2)
            {
              rtcData.glucVibeEnabled = !rtcData.glucVibeEnabled;
              backupDataToFlash();
            }
            else if (settingsCursor == 3 || settingsCursor == 4)
            {
              struct timeval tv_now;
              gettimeofday(&tv_now, NULL);
              if (rtcData.glucSnoozeUntil > tv_now.tv_sec)
              {
                rtcData.glucSnoozeUntil = 0;
              }
              else
              {
                rtcData.glucSnoozeUntil = tv_now.tv_sec + (settingsCursor == 3 ? 3600 : 28800);
              }
              backupDataToFlash();
            }
          }
        }
        updateDisplay();
        lastInputTime = millis();
        delay(200);
      }

      if (digitalRead(BUTTON_NEXT_PIN) == LOW)
      {
        if (rtcData.savedScreen == 2)
        {
          if (weatherDayOffset < 4)
            weatherDayOffset++;
        }
        else if (rtcData.savedScreen == 3)
        {
          if (rtcData.graphHours < 5)
            rtcData.graphHours++;
        }
        else if (rtcData.savedScreen == 4)
        {
          if (viewingAlarms)
          {
            alarmPageOffset++;
          }
          else
          {
            settingsCursor++;
            if (settingsCursor > 4)
              settingsCursor = 0;
          }
        }
        updateDisplay();
        lastInputTime = millis();
        delay(200);
      }

      if (screenNeedsUpdate)
      {
        updateDisplay();
        screenNeedsUpdate = false;
      }

      if (newDataReceived && !screenRefreshedByBLE)
      {
        updateDisplay();
        screenRefreshedByBLE = true;
      }

      delay(20);
    }
    display.displayOff();
  }
  else
  {
    unsigned long startWait = millis();
    while (!bleTaskComplete && millis() - startWait < 15000)
    {
      if (digitalRead(BUTTON_RIGHT_PIN) == LOW || digitalRead(BUTTON_LEFT_PIN) == LOW)
      {
        updateDisplay();
        break;
      }
      delay(10);
    }
  }

  if (needsVibration)
  {
    for (int i = 0; i < 3; i++)
    {
      digitalWrite(VIBE_PIN, HIGH);
      delay(250);
      digitalWrite(VIBE_PIN, LOW);
      delay(250);
    }
    needsVibration = false;
  }

  digitalWrite(VIBE_PIN, LOW);

  gettimeofday(&tv_now, NULL);
  current_rtc_time = tv_now.tv_sec;
  long new_time_since = (rtcData.lastSuccessfulFetchTime == 0) ? 300 : (current_rtc_time - rtcData.lastSuccessfulFetchTime);
  if (new_time_since < 0 || new_time_since >= 300)
    new_time_since = 0;

  long remaining = 300 - new_time_since;
  if (remaining <= 0)
    remaining = 300;

  esp_sleep_enable_timer_wakeup(remaining * 1000000ULL);
  esp_sleep_enable_ext0_wakeup(BUTTON_RIGHT_PIN, 0);
  esp_deep_sleep_start();
}

void loop() {}