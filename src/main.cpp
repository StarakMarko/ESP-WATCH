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
#include <sntp.h> // Додано для контролю NTP

// --- Configuration ---
const char *ssid = "Redmi Note 11";
const char *password = "123456781";

// Глюкоза (Nightscout/Pebble)
const int httpPort = 17580;

// NTP (Час)
const char *ntpServer = "pool.ntp.org";

// Погода (Open-Meteo API)
const char *weatherHost = "api.open-meteo.com";
const int weatherPort = 80;

const int timezone = 2; // Ваш часовий пояс (UTC+2)
const int timezoneOffset = timezone * SECS_PER_MIN * 60;

#define I2C_SDA 44
#define I2C_SCL 43
SSD1306Wire display(0x3c, I2C_SDA, I2C_SCL);

// --- КНОПКИ ---
#define BUTTON_LEFT_PIN GPIO_NUM_3
#define BUTTON_RIGHT_PIN GPIO_NUM_1
#define BUTTON_PREV_PIN GPIO_NUM_5
#define BUTTON_NEXT_PIN GPIO_NUM_6
#define BATTERY_PIN GPIO_NUM_8

const float R1 = 67000.0;
const float R2 = 67000.0;
// Варіант 2: Залишаємо це значення стандартним (3.3V)
const float ADC_MAX_VOLTAGE = 3.3;
const int ADC_RESOLUTION = 4095;
const int INTERACTIVE_TIMEOUT_MS = 6000;

// --- RTC Memory Variables ---
RTC_DATA_ATTR bool timeSynced = false;
RTC_DATA_ATTR char lastBG_char[10];
RTC_DATA_ATTR char lastDirection_char[20];
RTC_DATA_ATTR int lastDelta_val;
RTC_DATA_ATTR time_t lastBGSDateTime_val;
RTC_DATA_ATTR int lastDataAgeMinutes_val;
RTC_DATA_ATTR bool hasLastData_val = false;
RTC_DATA_ATTR time_t lastSuccessfulFetchTime = 0;
RTC_DATA_ATTR float lastBatteryVoltage_val = 0.0;
RTC_DATA_ATTR int currentScreenIndex = 0;

// Погода RTC
RTC_DATA_ATTR bool hasWeatherData = false;
RTC_DATA_ATTR time_t lastWeatherFetchTime = 0;
RTC_DATA_ATTR int weatherCodes[5];
RTC_DATA_ATTR float tempMax[5];
RTC_DATA_ATTR float tempMin[5];
RTC_DATA_ATTR time_t weatherDates[5];

// --- Global Variables ---
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
bool syncTimeNTP(); // Прототип нової функції

// --- Bitmaps (Залишаємо без змін) ---
const unsigned char ArrowUp[] PROGMEM = {0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0xf8, 0x0f, 0xfc, 0x1f, 0xde, 0x3d, 0xcf, 0x79, 0xc7, 0x71, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01};
const unsigned char ArrowDown[] PROGMEM = {0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc7, 0x71, 0xcf, 0x79, 0xde, 0x3d, 0xfc, 0x1f, 0xf8, 0x0f, 0xf0, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00};
const unsigned char ArrowUpS[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0x00, 0x3f, 0x80, 0x3f, 0xc0, 0x3f, 0xe0, 0x3f, 0xf0, 0x39, 0xf8, 0x38, 0x7c, 0x38, 0x3c, 0x38, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char ArrowDownS[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x3c, 0x38, 0x7c, 0x38, 0xf8, 0x38, 0xf0, 0x39, 0xe0, 0x3f, 0xc0, 0x3f, 0x80, 0x3f, 0x00, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0x00, 0x00, 0x00, 0x00};
const unsigned char ArrowSide[] PROGMEM = {0x80, 0x01, 0x80, 0x03, 0x80, 0x07, 0x00, 0x0f, 0x00, 0x1e, 0x00, 0x3c, 0xff, 0x7f, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x3c, 0x00, 0x1e, 0x00, 0x0f, 0x80, 0x07, 0x80, 0x03, 0x80, 0x01};
const unsigned char ArrowUpD[] PROGMEM = {0x08, 0x10, 0x1c, 0x38, 0x2a, 0x54, 0x49, 0x92, 0x88, 0x11, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10};
const unsigned char ArrowDownD[] PROGMEM = {0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x88, 0x11, 0x49, 0x92, 0x2a, 0x54, 0x1c, 0x38, 0x08, 0x10};

// --- Weather Icons (32x32) ---
const unsigned char IconSun[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x0f, 0x00, 0x00, 0xfc, 0x3f, 0x00,
    0x00, 0xfe, 0x7f, 0x00, 0x80, 0xff, 0xff, 0x01, 0x80, 0xff, 0xff, 0x01, 0xc0, 0xff, 0xff, 0x03,
    0xe0, 0xff, 0xff, 0x07, 0xe0, 0xff, 0xff, 0x07, 0xf0, 0xff, 0xff, 0x0f, 0xf0, 0xff, 0xff, 0x0f,
    0xf0, 0xff, 0xff, 0x0f, 0xf0, 0xff, 0xff, 0x0f, 0xf0, 0xff, 0xff, 0x0f, 0xf0, 0xff, 0xff, 0x0f,
    0xf0, 0xff, 0xff, 0x0f, 0xf0, 0xff, 0xff, 0x0f, 0xe0, 0xff, 0xff, 0x07, 0xe0, 0xff, 0xff, 0x07,
    0xc0, 0xff, 0xff, 0x03, 0x80, 0xff, 0xff, 0x01, 0x80, 0xff, 0xff, 0x01, 0x00, 0xfe, 0x7f, 0x00,
    0x00, 0xfc, 0x3f, 0x00, 0x00, 0xf0, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const unsigned char IconCloud[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xF0, 0x03, 0x00, 0x00, 0xFC, 0x0F, 0x00, 0x00, 0x0E, 0x1C, 0x00,
    0x00, 0x02, 0x30, 0x00, 0x00, 0x03, 0x60, 0x00, 0x80, 0x01, 0x40, 0x00,
    0xC0, 0x00, 0xC0, 0x00, 0x60, 0x00, 0x80, 0x01, 0x20, 0x00, 0x00, 0x01,
    0x30, 0x00, 0x00, 0x03, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02,
    0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x20, 0x00, 0x00, 0x01,
    0x60, 0x00, 0x80, 0x01, 0xC0, 0x00, 0xC0, 0x00, 0x80, 0xFF, 0x7F, 0x00,
    0x00, 0xFF, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const unsigned char IconRain[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x03, 0x00,
    0x00, 0xFC, 0x0F, 0x00, 0x00, 0x0E, 0x1C, 0x00, 0x00, 0x02, 0x30, 0x00,
    0x00, 0x03, 0x60, 0x00, 0x80, 0x01, 0x40, 0x00, 0xC0, 0x00, 0xC0, 0x00,
    0x60, 0x00, 0x80, 0x01, 0x20, 0x00, 0x00, 0x01, 0x30, 0x00, 0x00, 0x03,
    0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02,
    0x30, 0x00, 0x00, 0x02, 0x20, 0x00, 0x00, 0x01, 0x60, 0x00, 0x80, 0x01,
    0xC0, 0x00, 0xC0, 0x00, 0x80, 0xFF, 0x7F, 0x00, 0x00, 0xFF, 0x3F, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x80, 0x01, 0x00,
    0x20, 0x40, 0x08, 0x00, 0x20, 0x40, 0x08, 0x00, 0x10, 0x20, 0x04, 0x00,
    0x10, 0x20, 0x04, 0x00, 0x08, 0x10, 0x02, 0x00, 0x08, 0x10, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const unsigned char IconSnow[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x03, 0x00,
    0x00, 0xFC, 0x0F, 0x00, 0x00, 0x0E, 0x1C, 0x00, 0x00, 0x02, 0x30, 0x00,
    0x00, 0x03, 0x60, 0x00, 0x80, 0x01, 0x40, 0x00, 0xC0, 0x00, 0xC0, 0x00,
    0x60, 0x00, 0x80, 0x01, 0x20, 0x00, 0x00, 0x01, 0x30, 0x00, 0x00, 0x03,
    0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02,
    0x30, 0x00, 0x00, 0x02, 0x20, 0x00, 0x00, 0x01, 0x60, 0x00, 0x80, 0x01,
    0xC0, 0x00, 0xC0, 0x00, 0x80, 0xFF, 0x7F, 0x00, 0x00, 0xFF, 0x3F, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x11, 0x00, 0x00, 0x28, 0x0A, 0x00,
    0x00, 0x10, 0x04, 0x00, 0x00, 0x28, 0x0A, 0x00, 0x00, 0x44, 0x11, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const unsigned char IconThunder[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x03, 0x00,
    0x00, 0xFC, 0x0F, 0x00, 0x00, 0x0E, 0x1C, 0x00, 0x00, 0x02, 0x30, 0x00,
    0x00, 0x03, 0x60, 0x00, 0x80, 0x01, 0x40, 0x00, 0xC0, 0x00, 0xC0, 0x00,
    0x60, 0x00, 0x80, 0x01, 0x20, 0x00, 0x00, 0x01, 0x30, 0x00, 0x00, 0x03,
    0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02, 0x30, 0x00, 0x00, 0x02,
    0x30, 0x00, 0x00, 0x02, 0x20, 0x00, 0x00, 0x01, 0x60, 0x00, 0x80, 0x01,
    0xC0, 0x00, 0xC0, 0x00, 0x80, 0xFF, 0x7F, 0x00, 0x00, 0xFF, 0x3F, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00,
    0x00, 0x60, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0xFF, 0x01, 0x00,
    0x00, 0x1E, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// --- Helper Functions ---
// --- ЗМІНЕНО ДЛЯ ВАРІАНТУ 2 ---
float getBatteryVoltage()
{
  // Розрахунок коефіцієнту: 4.00 (реальна) / 3.72 (відображена) = ~1.0752
  float calibration_factor = 1.0752;

  int analogValue = analogRead(BATTERY_PIN);
  float voltageAtADC = (float)analogValue * (ADC_MAX_VOLTAGE / ADC_RESOLUTION);
  float rawVoltage = voltageAtADC * (R1 + R2) / R2;

  // Повертаємо відкаліброване значення
  return rawVoltage * calibration_factor;
}

void adjustTimezone(time_t &timestamp)
{
  timestamp += timezoneOffset;
}

// --- DISPLAY UPDATE MANAGER ---
void updateDisplay()
{
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  time_t current_rtc_time = tv_now.tv_sec;
  setTime(current_rtc_time);

  if (currentScreenIndex == 0)
  { // SCREEN 0: GLUCOSE
    if (hasLastData_val)
    {
      int current_data_age = (current_rtc_time - lastBGSDateTime_val) / 60;
      if (current_data_age < 0)
        current_data_age = 0;
      drawGlucoseScreen(current_rtc_time, String(lastBG_char), current_data_age, lastBatteryVoltage_val, String(lastDirection_char), lastDelta_val);
    }
    else
    {
      display.clear();
      display.displayOn();
      display.setFont(ArialMT_Plain_16);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 25, "No Glucose Data");
      display.display();
    }
  }
  else if (currentScreenIndex == 1)
  { // SCREEN 1: CLOCK
    drawClockScreen(current_rtc_time, lastBatteryVoltage_val);
  }
  else if (currentScreenIndex == 2)
  { // SCREEN 2: WEATHER
    drawWeatherScreen(current_rtc_time);
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
  display.drawString(126, 3, String(age) + " min ago");

  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(45, 19, BG);
  display.drawString(46, 19, BG); // Bold

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
  display.drawString(2, 50, "Bat: " + String(batteryVoltage, 2) + "V");
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
  display.drawString(126, 0, String(batteryVoltage, 2) + "V");
  display.drawRect(0, 0, 128, 64);
  display.display();
}

// --- SCREEN 3: WEATHER ---
void drawWeatherScreen(time_t datetimenow)
{
  display.clear();
  display.displayOn();

  if (!hasWeatherData)
  {
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 25, "Data Fetching...");
    display.drawString(64, 38, "Or Failed.");
    display.display();
    return;
  }

  if (weatherDayOffset < 0)
    weatherDayOffset = 0;
  if (weatherDayOffset > 4)
    weatherDayOffset = 4;

  time_t forecastTime = weatherDates[weatherDayOffset];
  String dateStr = String(day(forecastTime)) + "/" + String(month(forecastTime));
  if (weatherDayOffset == 0)
    dateStr = "Today";
  if (weatherDayOffset == 1)
    dateStr = "Tomorrow";

  // Дата (вгорі)
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, "Lviv: " + dateStr);

  // Температура (Трохи вище, щоб влізла іконка)
  display.setFont(ArialMT_Plain_24);
  String tempStr = String((int)tempMax[weatherDayOffset]) + " / " + String((int)tempMin[weatherDayOffset]);
  display.drawString(64, 12, tempStr);

  // --- ЛОГІКА ВИБОРУ ІКОНКИ ---
  int code = weatherCodes[weatherDayOffset];
  const unsigned char *weatherIcon = IconCloud; // Іконка за замовчуванням

  // Вибір іконки за кодом Open-Meteo
  if (code == 0)
  {
    weatherIcon = IconSun;
  }
  else if (code >= 1 && code <= 3)
  {
    weatherIcon = IconCloud;
  }
  else if (code == 45 || code == 48)
  {
    weatherIcon = IconCloud; // Туман як хмара
  }
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82))
  {
    weatherIcon = IconRain;
  }
  else if (code >= 71 && code <= 77)
  {
    weatherIcon = IconSnow;
  }
  else if (code >= 95)
  {
    weatherIcon = IconThunder;
  }

  // Малюємо іконку по центру знизу (64 - 16 = 48)
  // X = 48 (центр екрану 64 мінус половина ширини іконки 16)
  // Y = 36 (знизу під температурою)
  display.drawXbm(48, 36, 32, 32, weatherIcon);

  // Стрілки навігації
  display.setFont(ArialMT_Plain_10);
  if (weatherDayOffset > 0)
    display.drawString(10, 25, "<");
  if (weatherDayOffset < 4)
    display.drawString(118, 25, ">");

  display.drawRect(0, 0, 128, 64);
  display.display();
}

String getWeatherDescription(int code)
{
  if (code == 0)
    return "Clear";
  if (code >= 1 && code <= 3)
    return "Cloudy";
  if (code == 45 || code == 48)
    return "Fog";
  if (code >= 51 && code <= 55)
    return "Drizzle";
  if (code >= 61 && code <= 67)
    return "Rain";
  if (code >= 71 && code <= 77)
    return "Snow";
  if (code >= 80 && code <= 82)
    return "Showers";
  if (code >= 95)
    return "Thunder";
  return String(code);
}

// --- NETWORK ---
bool connectToWiFi()
{
  Serial.print("Connecting to WIFI");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int connectAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && connectAttempts < 30)
  {
    Serial.print(".");
    delay(500);
    connectAttempts++;
  }
  return (WiFi.status() == WL_CONNECTED);
}

bool getreadings()
{
  WiFiClient client;
  if (!client.connect(WiFi.gatewayIP(), httpPort))
    return false;

  client.print(String("GET /pebble HTTP/1.1\r\nHost: ") + WiFi.gatewayIP().toString() + "\r\nConnection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0)
  {
    if (millis() - timeout > 5000)
    {
      client.stop();
      return false;
    }
  }

  // Skip headers
  while (client.connected())
  {
    String line = client.readStringUntil('\n');
    if (line == "\r")
      break;
  }

  // Parse Glucose JSON
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, client); // Read directly from stream
  if (error)
    return false;

  String status0_now_str = doc["status"][0]["now"].as<String>();
  status0_now_str = status0_now_str.substring(0, status0_now_str.length() - 3);
  time_t status0_now1 = status0_now_str.toInt();

  JsonObject bgs0 = doc["bgs"][0];
  adjustTimezone(status0_now1);
  time_t bgs0_datetime2 = bgs0["datetime"].as<String>().substring(0, 10).toInt();
  adjustTimezone(bgs0_datetime2);

  struct timeval tv = {.tv_sec = status0_now1};
  settimeofday(&tv, NULL);
  setTime(status0_now1);
  timeSynced = true;

  strncpy(lastBG_char, bgs0["sgv"], sizeof(lastBG_char) - 1);
  strncpy(lastDirection_char, bgs0["direction"], sizeof(lastDirection_char) - 1);
  lastDelta_val = bgs0["bgdelta"];
  lastBGSDateTime_val = bgs0_datetime2;
  lastDataAgeMinutes_val = (status0_now1 - bgs0_datetime2) / 60;
  hasLastData_val = true;
  lastSuccessfulFetchTime = status0_now1;
  lastBatteryVoltage_val = getBatteryVoltage();

  return true;
}

// --- SYNC NTP (НОВА ФУНКЦІЯ) ---
bool syncTimeNTP()
{
  Serial.println("Syncing time via NTP...");
  // Налаштовуємо час: offset у секундах, daylightOffset, адреса сервера
  // Використовуємо вашу змінну timezone (2) * 3600 секунд
  configTime(timezone * 3600, 0, ntpServer);

  struct tm timeinfo;
  // Чекаємо до 5 секунд на синхронізацію
  if (!getLocalTime(&timeinfo, 5000))
  {
    Serial.println("NTP Sync Failed");
    return false;
  }

  Serial.println("NTP Sync Success");

  // Оновлюємо також TimeLib, щоб бібліотека time() і TimeLib були синхронізовані
  time_t now;
  time(&now);
  setTime(now);

  timeSynced = true;
  return true;
}

// --- GET WEATHER (Optimized for Memory) ---
bool getWeather()
{
  WiFiClient client;

  // Збільшимо таймаут до 10 секунд
  client.setTimeout(10000);

  Serial.print("Connecting to weather... ");
  if (!client.connect(weatherHost, weatherPort))
  {
    Serial.println("Connection failed");
    return false;
  }
  Serial.println("Connected!");

  // URL для запиту
  String url = "/v1/forecast?latitude=49.84&longitude=24.03&daily=weathercode,temperature_2m_max,temperature_2m_min&timezone=auto";

  // !!! ВАЖЛИВО: Використовуємо HTTP/1.0, щоб уникнути Chunked Encoding
  client.print(String("GET ") + url + " HTTP/1.0\r\n" +
               "Host: " + weatherHost + "\r\n" +
               "Connection: close\r\n\r\n");

  // Перевірка відповіді сервера
  unsigned long timeout = millis();
  while (client.available() == 0)
  {
    if (millis() - timeout > 5000)
    {
      Serial.println("Client Timeout !");
      client.stop();
      return false;
    }
  }

  // --- ПРОПУСК ЗАГОЛОВКІВ ---
  if (!client.find("\r\n\r\n"))
  {
    Serial.println("Invalid response (no headers)");
    client.stop();
    return false;
  }

  // --- ПАРСИНГ JSON ---
  StaticJsonDocument<200> filter;
  filter["daily"]["weathercode"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;

  DynamicJsonDocument doc(3072);

  DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter));

  if (error)
  {
    Serial.print("DeserializeJson failed: ");
    Serial.println(error.f_str());
    client.stop();
    return false;
  }

  if (!doc.containsKey("daily"))
  {
    Serial.println("JSON valid but no 'daily' data!");
    client.stop();
    return false;
  }

  JsonArray daily_code = doc["daily"]["weathercode"];
  JsonArray daily_max = doc["daily"]["temperature_2m_max"];
  JsonArray daily_min = doc["daily"]["temperature_2m_min"];

  if (daily_code.size() == 0)
  {
    Serial.println("Arrays are empty!");
    return false;
  }

  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);

  for (int i = 0; i < 5; i++)
  {
    weatherCodes[i] = daily_code[i];
    tempMax[i] = daily_max[i];
    tempMin[i] = daily_min[i];
    weatherDates[i] = tv_now.tv_sec + (i * 86400);
  }

  hasWeatherData = true;
  lastWeatherFetchTime = tv_now.tv_sec;

  Serial.println("Weather updated successfully!");
  client.stop();
  return true;
}

// --- Wi-Fi Task ---
void wifiTask(void *parameter)
{
  if (connectToWiFi())
  {
    // 1. Glucose
    dataFetchedSuccessfully = getreadings();

    // 1.1 FALLBACK: Якщо глюкоза не прийшла, беремо час з NTP
    if (!dataFetchedSuccessfully)
    {
      Serial.println("Glucose failed. Attempting NTP time sync...");
      syncTimeNTP();
    }

    // 2. Weather
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    if (!hasWeatherData || (tv_now.tv_sec - lastWeatherFetchTime > 3600))
    {
      delay(100);
      weatherFetchedSuccessfully = getWeather();
    }
    else
    {
      weatherFetchedSuccessfully = true;
    }

    WiFi.disconnect(true);
  }
  wifiTaskComplete = true;
  vTaskDelete(NULL);
}

// --- SETUP ---
void setup()
{
  Serial.begin(115200);

  pinMode(BUTTON_LEFT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PREV_PIN, INPUT_PULLUP);
  pinMode(BUTTON_NEXT_PIN, INPUT_PULLUP);

  display.init();
  display.flipScreenVertically();
  display.clear();

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  time_t current_rtc_time = tv_now.tv_sec;

  bool wokeByTimer = (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) || (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED);
  bool wokeByButton = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0);
  long time_since_last_fetch = (lastSuccessfulFetchTime == 0) ? LONG_MAX : (current_rtc_time - lastSuccessfulFetchTime);

  bool needsUpdate = wokeByTimer || (time_since_last_fetch >= 300);

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
    while (digitalRead(BUTTON_LEFT_PIN) == LOW)
      delay(50);
    delay(100);

    unsigned long lastInputTime = millis();
    while (millis() - lastInputTime < INTERACTIVE_TIMEOUT_MS)
    {
      // Ліва/Права - Екрани
      if (digitalRead(BUTTON_LEFT_PIN) == LOW)
      {
        currentScreenIndex++;
        if (currentScreenIndex > 2)
          currentScreenIndex = 0;
        weatherDayOffset = 0;
        updateDisplay();
        lastInputTime = millis();
        delay(250);
      }
      if (digitalRead(BUTTON_RIGHT_PIN) == LOW)
      {
        currentScreenIndex--;
        if (currentScreenIndex < 0)
          currentScreenIndex = 2;
        weatherDayOffset = 0;
        updateDisplay();
        lastInputTime = millis();
        delay(250);
      }
      // Додаткові - Погода
      if (currentScreenIndex == 2)
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
        // Оновлюємо екран, якщо прийшли дані (глюкоза АБО погода АБО просто NTP час)
        updateDisplay();
        needsUpdate = false;
      }
      delay(10);
    }
    display.displayOff();
  }
  else
  {
    unsigned long startWait = millis();
    while (!wifiTaskComplete && millis() - startWait < 25000)
    {
      if (digitalRead(BUTTON_LEFT_PIN) == LOW || digitalRead(BUTTON_RIGHT_PIN) == LOW)
        break;
      delay(10);
    }
  }

  long remaining = 300 - (time_since_last_fetch % 300);
  if (remaining < 10)
    remaining = 300;
  esp_sleep_enable_timer_wakeup(remaining * 1000000ULL);
  esp_sleep_enable_ext0_wakeup(BUTTON_LEFT_PIN, 0);

  Serial.println("Sleep.");
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {}