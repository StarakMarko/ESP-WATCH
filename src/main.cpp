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
#include <driver/rtc_io.h> // !!! Додано бібліотеку для керування пінами в сні

// --- Configuration ---
const char *ssid = "Redmi Note 11";
const char *password = "123456781";
const int httpPort = 17580;
const int timezone = 2;
const int timezoneOffset = timezone * SECS_PER_MIN * 60;

#define I2C_SDA 43
#define I2C_SCL 44
SSD1306Wire display(0x3c, I2C_SDA, I2C_SCL);

// --- КНОПКИ ---
// GPIO 3 - Ліва кнопка (Головна кнопка пробудження)
// GPIO 0 - Права кнопка
#define BUTTON_LEFT_PIN GPIO_NUM_3
#define BUTTON_RIGHT_PIN GPIO_NUM_1

#define BATTERY_PIN GPIO_NUM_8

const float R1 = 61000.0;
const float R2 = 61000.0;
const float ADC_MAX_VOLTAGE = 3.3;
const int ADC_RESOLUTION = 4095;
const uint64_t FIVE_MINUTES_IN_US = 5 * 60 * 1000000ULL;
const int INTERACTIVE_TIMEOUT_MS = 5000;

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

// --- Global Variables ---
volatile bool wifiTaskComplete = false;
volatile bool dataFetchedSuccessfully = false;

// --- Function Prototypes ---
void drawGlucoseScreen(time_t datetimenow, String BG, int age, float batteryVoltage, String bgs0_direction, int delta);
void drawClockScreen(time_t datetimenow, float batteryVoltage);
void updateDisplay();

// --- Bitmaps (залишаємо ті самі) ---
const unsigned char ArrowUp[] PROGMEM = {0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0xf8, 0x0f, 0xfc, 0x1f, 0xde, 0x3d, 0xcf, 0x79, 0xc7, 0x71, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01};
const unsigned char ArrowDown[] PROGMEM = {0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc7, 0x71, 0xcf, 0x79, 0xde, 0x3d, 0xfc, 0x1f, 0xf8, 0x0f, 0xf0, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00};
const unsigned char ArrowUpS[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0x00, 0x3f, 0x80, 0x3f, 0xc0, 0x3f, 0xe0, 0x3f, 0xf0, 0x39, 0xf8, 0x38, 0x7c, 0x38, 0x3c, 0x38, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char ArrowDownS[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x3c, 0x38, 0x7c, 0x38, 0xf8, 0x38, 0xf0, 0x39, 0xe0, 0x3f, 0xc0, 0x3f, 0x80, 0x3f, 0x00, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0x00, 0x00, 0x00, 0x00};
const unsigned char ArrowSide[] PROGMEM = {0x80, 0x01, 0x80, 0x03, 0x80, 0x07, 0x00, 0x0f, 0x00, 0x1e, 0x00, 0x3c, 0xff, 0x7f, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x3c, 0x00, 0x1e, 0x00, 0x0f, 0x80, 0x07, 0x80, 0x03, 0x80, 0x01};
const unsigned char ArrowUpD[] PROGMEM = {0x08, 0x10, 0x1c, 0x38, 0x2a, 0x54, 0x49, 0x92, 0x88, 0x11, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10};
const unsigned char ArrowDownD[] PROGMEM = {0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x88, 0x11, 0x49, 0x92, 0x2a, 0x54, 0x1c, 0x38, 0x08, 0x10};

// --- Battery Voltage Measurement ---
float getBatteryVoltage()
{
  int analogValue = analogRead(BATTERY_PIN);
  float voltageAtADC = (float)analogValue * (ADC_MAX_VOLTAGE / ADC_RESOLUTION);
  float batteryVoltage = voltageAtADC * (R1 + R2) / R2;
  return batteryVoltage;
}

void adjustTimezone(time_t &timestamp)
{
  timestamp += timezoneOffset;
}

// --- HELPER: Draw currently selected screen ---
void updateDisplay()
{
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  time_t current_rtc_time = tv_now.tv_sec;
  setTime(current_rtc_time);

  if (currentScreenIndex == 0)
  {
    // Екран 1: Глюкоза
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
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 20, "No Data.");
      display.drawString(64, 35, "Wait Sync...");
      display.display();
    }
  }
  else if (currentScreenIndex == 1)
  {
    // Екран 2: Великий Годинник
    drawClockScreen(current_rtc_time, lastBatteryVoltage_val);
  }
}

// --- Screen 1: Glucose ---
void drawGlucoseScreen(time_t datetimenow, String BG, int age, float batteryVoltage, String bgs0_direction, int delta)
{
  display.clear();
  display.displayOn();

  int hour24 = hour(datetimenow);
  String timeNow = String(hour24) + ":";
  if (minute(datetimenow) < 10)
    timeNow += "0";
  timeNow += String(minute(datetimenow));

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(2, 3, String(timeNow));
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(126, 3, String(age) + " min ago");

  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(45, 19, BG);
  display.drawString(45 + 1, 19, BG);

  // Стрілки
  if (String(bgs0_direction) == "Flat")
    display.drawXbm(101, 26, 16, 16, ArrowSide);
  else if (String(bgs0_direction) == "FortyFiveUp")
    display.drawXbm(101, 26, 16, 16, ArrowUpS);
  else if (String(bgs0_direction) == "FortyFiveDown")
    display.drawXbm(101, 26, 16, 16, ArrowDownS);
  else if (String(bgs0_direction) == "SingleUp")
    display.drawXbm(101, 26, 16, 16, ArrowUp);
  else if (String(bgs0_direction) == "SingleDown")
    display.drawXbm(101, 26, 16, 16, ArrowDown);
  else if (String(bgs0_direction) == "DoubleUp")
    display.drawXbm(101, 26, 16, 16, ArrowUpD);
  else if (String(bgs0_direction) == "DoubleDown")
    display.drawXbm(101, 26, 16, 16, ArrowDownD);

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(2, 50, "Bat: " + String(batteryVoltage, 2) + "V");
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  String bgdelta = (delta < 0) ? String(delta) : (delta > 0) ? "+" + String(delta)
                                                             : String(delta);
  display.drawString(126, 50, bgdelta + " mg/dl ");

  display.drawRect(0, 0, 128, 64);
  display.drawRect(0, 17, 128, 33);
  display.drawVerticalLine(90, 20, 27);
  display.display();
}

// --- Screen 2: Big Clock ---
void drawClockScreen(time_t datetimenow, float batteryVoltage)
{
  display.clear();
  display.displayOn();

  int hour24 = hour(datetimenow);
  int minVal = minute(datetimenow);

  String timeStr = String(hour24) + ":";
  if (minVal < 10)
    timeStr += "0";
  timeStr += String(minVal);

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

// --- Wi-Fi Connection ---
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

// --- Fetch Readings ---
bool getreadings()
{
  WiFiClient client;
  if (!client.connect(WiFi.gatewayIP(), httpPort))
    return false;

  String url = "/pebble";
  client.print(String("GET ") + url + " HTTP/1.1\r\nHost: " + WiFi.gatewayIP().toString() + "\r\nConnection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0)
  {
    if (millis() - timeout > 5000)
    {
      client.stop();
      return false;
    }
  }

  while (client.connected())
  {
    String line = client.readStringUntil('\n');
    if (line == "\r")
      break;
  }
  String line2 = client.readStringUntil('\n');

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, line2);
  if (error)
    return false;

  // Parsing
  String status0_now_str = doc["status"][0]["now"].as<String>();
  status0_now_str = status0_now_str.substring(0, status0_now_str.length() - 3);
  time_t status0_now1 = status0_now_str.toInt();

  JsonObject bgs0 = doc["bgs"][0];
  const char *bgs0_sgv = bgs0["sgv"];
  const char *bgs0_direction = bgs0["direction"];
  String bgs0_datetime_str = bgs0["datetime"].as<String>();
  bgs0_datetime_str = bgs0_datetime_str.substring(0, bgs0_datetime_str.length() - 3);
  time_t bgs0_datetime2 = bgs0_datetime_str.toInt();

  adjustTimezone(status0_now1);
  adjustTimezone(bgs0_datetime2);
  int bgs0_bgdelta = bgs0["bgdelta"];
  int dataAge = (status0_now1 - bgs0_datetime2) / 60;

  // Update RTC
  struct timeval tv;
  tv.tv_sec = status0_now1;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
  setTime(status0_now1);
  timeSynced = true;
  float currentBatteryVoltage = getBatteryVoltage();

  strncpy(lastBG_char, bgs0_sgv, sizeof(lastBG_char) - 1);
  strncpy(lastDirection_char, bgs0_direction, sizeof(lastDirection_char) - 1);
  lastDelta_val = bgs0_bgdelta;
  lastBGSDateTime_val = bgs0_datetime2;
  lastDataAgeMinutes_val = dataAge;
  hasLastData_val = true;
  lastSuccessfulFetchTime = status0_now1;
  lastBatteryVoltage_val = currentBatteryVoltage;

  return true;
}

// --- Wi-Fi Task ---
void wifiTask(void *parameter)
{
  if (connectToWiFi())
  {
    dataFetchedSuccessfully = getreadings();
    WiFi.disconnect(true);
  }
  wifiTaskComplete = true;
  vTaskDelete(NULL);
}

// --- SETUP ---
void setup()
{
  Serial.begin(115200);

  // Налаштовуємо піни
  pinMode(BUTTON_LEFT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT_PIN, INPUT_PULLUP);

  display.init();
  display.flipScreenVertically();
  display.clear();

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // Час
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  time_t current_rtc_time = tv_now.tv_sec;
  long time_since_last_fetch = (lastSuccessfulFetchTime == 0) ? LONG_MAX : (current_rtc_time - lastSuccessfulFetchTime);

  bool wokeByTimer = (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER);
  bool wokeByButton = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0);

  if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED)
    wokeByTimer = true;

  // --- ЛОГІКА ---

  // 1. Потрібно оновити дані?
  bool needsUpdate = wokeByTimer || (time_since_last_fetch >= 300);

  if (needsUpdate)
  {
    wifiTaskComplete = false;
    dataFetchedSuccessfully = false;
    xTaskCreatePinnedToCore(wifiTask, "WiFiTask", 10000, NULL, 1, NULL, 0);
  }

  // 2. Інтерактивний режим (Якщо натиснута кнопка)
  if (wokeByButton)
  {
    // !!! ЗМІНА 1: Спочатку показуємо екран, який запам'ятали (з RTC пам'яті)
    updateDisplay();

    // !!! ЗМІНА 2: Чекаємо, поки користувач ВІДПУСТИТЬ кнопку пробудження.
    // Якщо цього не зробити, код нижче відразу подумає, що ви хочете переключити екран.
    while (digitalRead(BUTTON_LEFT_PIN) == LOW)
    {
      delay(50);
      // Можна додати таймаут, щоб не зависло навічно, якщо кнопка залипне
    }
    delay(100); // Антибрязкіт після відпускання

    unsigned long lastInputTime = millis();

    // Цикл: поки не пройде 5 секунд бездіяльності
    while (millis() - lastInputTime < INTERACTIVE_TIMEOUT_MS)
    {
      // Читаємо ОБИДВІ кнопки
      if (digitalRead(BUTTON_LEFT_PIN) == LOW)
      {
        if (currentScreenIndex != 0)
        {
          currentScreenIndex = 0; // Перехід на Глюкозу
          updateDisplay();
        }
        lastInputTime = millis();
        delay(200); // Антибрязкіт
      }

      if (digitalRead(BUTTON_RIGHT_PIN) == LOW)
      {
        if (currentScreenIndex != 1)
        {
          currentScreenIndex = 1; // Перехід на Годинник
          updateDisplay();
        }
        lastInputTime = millis();
        delay(200);
      }

      // Перевірка Wi-Fi (якщо прокинулись кнопкою, але саме час оновити дані)
      if (needsUpdate && wifiTaskComplete)
      {
        if (dataFetchedSuccessfully && currentScreenIndex == 0)
        {
          updateDisplay();
        }
        needsUpdate = false;
      }
      delay(10);
    }
    // Час вийшов -> вимикаємо екран
    display.displayOff();
  }
  else
  {
    // Якщо ТАЙМЕР: чекаємо Wi-Fi без екрану
    unsigned long startWait = millis();
    while (!wifiTaskComplete && millis() - startWait < 25000)
    {
      // Якщо натиснули кнопку ПОКИ чекаємо таймер - просто прокидаємось повністю
      if (digitalRead(BUTTON_LEFT_PIN) == LOW || digitalRead(BUTTON_RIGHT_PIN) == LOW)
      {
        break;
      }
      delay(10);
    }
  }

  // --- СОН ---
  long remaining_sleep_seconds = 300 - (time_since_last_fetch % 300);
  if (remaining_sleep_seconds < 10)
    remaining_sleep_seconds = 300;

  uint64_t sleep_us = (uint64_t)remaining_sleep_seconds * 1000000ULL;

  esp_sleep_enable_timer_wakeup(sleep_us);

  // Прокидання від лівої кнопки
  esp_sleep_enable_ext0_wakeup(BUTTON_LEFT_PIN, 0);

  Serial.println("Going to sleep...");
  Serial.flush();
  esp_deep_sleep_start();
}

void loop()
{
}