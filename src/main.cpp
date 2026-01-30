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

// --- Configuration ---
const char *ssid = "Redmi Note 11";
const char *password = "123456781";
const int httpPort = 17580;
const int timezone = 3;
const int timezoneOffset = timezone * SECS_PER_MIN * 60;

#define I2C_SDA 43
#define I2C_SCL 44
SSD1306Wire display(0x3c, I2C_SDA, I2C_SCL);

#define BUTTON_PIN GPIO_NUM_3
#define BATTERY_PIN GPIO_NUM_8

const float R1 = 61000.0;
const float R2 = 61000.0;
const float ADC_MAX_VOLTAGE = 3.3;
const int ADC_RESOLUTION = 4095;
const uint64_t FIVE_MINUTES_IN_US = 5 * 60 * 1000000ULL;
const int DISPLAY_DURATION_S = 5;

// --- RTC Memory Variables ---
RTC_DATA_ATTR bool timeSynced = false;
RTC_DATA_ATTR char lastBG_char[10];
RTC_DATA_ATTR char lastDirection_char[20];
RTC_DATA_ATTR int lastDelta_val;
RTC_DATA_ATTR time_t lastBGSDateTime_val;
RTC_DATA_ATTR int lastDataAgeMinutes_val;
RTC_DATA_ATTR bool hasLastData_val = false; // Initialized to false
RTC_DATA_ATTR time_t lastSuccessfulFetchTime = 0;
RTC_DATA_ATTR float lastBatteryVoltage_val = 0.0;

// --- Global Variables ---
const char *directarr = "";
volatile bool buttonPressed = false;
volatile bool wifiTaskComplete = false;
volatile bool dataFetchedSuccessfully = false;

void showdata(time_t datetimenow, String BG, int age, float batteryVoltage, String bgs0_direction, int delta);

// --- Arrow Bitmaps (залишено без змін) ---
const unsigned char ArrowUp[] PROGMEM = {0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0xf8, 0x0f, 0xfc, 0x1f, 0xde, 0x3d, 0xcf, 0x79, 0xc7, 0x71, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01};
const unsigned char ArrowDown[] PROGMEM = {0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc7, 0x71, 0xcf, 0x79, 0xde, 0x3d, 0xfc, 0x1f, 0xf8, 0x0f, 0xf0, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00};
const unsigned char ArrowUpS[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0x00, 0x3f, 0x80, 0x3f, 0xc0, 0x3f, 0xe0, 0x3f, 0xf0, 0x39, 0xf8, 0x38, 0x7c, 0x38, 0x3c, 0x38, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char ArrowDownS[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x3c, 0x38, 0x7c, 0x38, 0xf8, 0x38, 0xf0, 0x39, 0xe0, 0x3f, 0xc0, 0x3f, 0x80, 0x3f, 0x00, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0x00, 0x00, 0x00, 0x00};
const unsigned char ArrowSide[] PROGMEM = {0x80, 0x01, 0x80, 0x03, 0x80, 0x07, 0x00, 0x0f, 0x00, 0x1e, 0x00, 0x3c, 0xff, 0x7f, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x3c, 0x00, 0x1e, 0x00, 0x0f, 0x80, 0x07, 0x80, 0x03, 0x80, 0x01};
const unsigned char ArrowUpD[] PROGMEM = {0x08, 0x10, 0x1c, 0x38, 0x2a, 0x54, 0x49, 0x92, 0x88, 0x11, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10};
const unsigned char ArrowDownD[] PROGMEM = {0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x88, 0x11, 0x49, 0x92, 0x2a, 0x54, 0x1c, 0x38, 0x08, 0x10};

// --- Button Interrupt Handler ---
void IRAM_ATTR handleButtonPress()
{
  buttonPressed = true;
}

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

// --- Display Data Helper Function ---
void displayCurrentData()
{
  if (hasLastData_val)
  {
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    time_t current_rtc_time = tv_now.tv_sec;
    setTime(current_rtc_time);
    int current_data_age = (current_rtc_time - lastBGSDateTime_val) / 60;
    if (current_data_age < 0)
      current_data_age = 0;

    // Виклик вашої функції малювання
    showdata(current_rtc_time, String(lastBG_char), current_data_age, lastBatteryVoltage_val, String(lastDirection_char), lastDelta_val);
  }
  else
  {
    display.clear();
    display.displayOn();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 26, "No Data Stored.");
    display.drawString(64, 40, "Wait for Sync...");
    display.display();
  }
}

// --- Display Data Logic ---
void showdata(time_t datetimenow, String BG, int age, float batteryVoltage, String bgs0_direction, int delta)
{
  display.clear();
  display.displayOn(); // Вмикаємо дисплей

  int hour24 = hour(datetimenow);
  int hour12 = (hour24 % 12 == 0) ? 12 : hour24 % 12;
  String ampm = (hour24 >= 12) ? "PM" : "AM";
  String timeNow = String(hour12) + ":";
  if (minute(datetimenow) < 10)
    timeNow += "0";
  timeNow += String(minute(datetimenow)) + " " + ampm;

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(2, 3, String(timeNow));
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(126, 3, String(age) + " min ago");

  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(45, 19, BG);
  display.drawString(45 + 1, 19, BG); // Bold effect

  if (String(bgs0_direction) == "Flat")
  {
    display.drawXbm(101, 26, 16, 16, ArrowSide);
  }
  else if (String(bgs0_direction) == "FortyFiveUp")
  {
    display.drawXbm(101, 26, 16, 16, ArrowUpS);
  }
  else if (String(bgs0_direction) == "FortyFiveDown")
  {
    display.drawXbm(101, 26, 16, 16, ArrowDownS);
  }
  else if (String(bgs0_direction) == "SingleUp")
  {
    display.drawXbm(101, 26, 16, 16, ArrowUp);
  }
  else if (String(bgs0_direction) == "SingleDown")
  {
    display.drawXbm(101, 26, 16, 16, ArrowDown);
  }
  else if (String(bgs0_direction) == "DoubleUp")
  {
    display.drawXbm(101, 26, 16, 16, ArrowUpD);
  }
  else if (String(bgs0_direction) == "DoubleDown")
  {
    display.drawXbm(101, 26, 16, 16, ArrowDownD);
  }

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

// --- Wi-Fi Connection ---
bool connectToWiFi()
{
  Serial.print("Connecting to WIFI");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int connectAttempts = 0;
  // Зменшив кількість спроб для швидшого циклу, якщо немає мережі
  while (WiFi.status() != WL_CONNECTED && connectAttempts < 30)
  {
    Serial.print(".");
    delay(500);
    connectAttempts++;
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nConnected to WIFI");
    return true;
  }
  else
  {
    Serial.println("\nFailed to connect to WiFi.");
    return false;
  }
}

// --- Fetch Readings from xDrip+ ---
bool getreadings()
{
  WiFiClient client;
  Serial.print("Connecting to xDrip+ at ");
  Serial.println(WiFi.gatewayIP());

  if (!client.connect(WiFi.gatewayIP(), httpPort))
  {
    Serial.println("Connection to xDrip+ failed");
    return false;
  }

  String url = "/pebble";
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + WiFi.gatewayIP().toString() + "\r\n" +
               "Connection: close\r\n\r\n");

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

  // Пропускаємо заголовки
  while (client.connected())
  {
    String line = client.readStringUntil('\n');
    if (line == "\r")
      break;
  }

  String line2 = client.readStringUntil('\n');
  const size_t bufferSize = 2048;
  DynamicJsonDocument doc(bufferSize);
  DeserializationError error = deserializeJson(doc, line2);

  if (error)
  {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.f_str());
    return false;
  }

  // Parsing JSON
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

  // Update RTC Data
  struct timeval tv;
  tv.tv_sec = status0_now1;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
  setTime(status0_now1);
  timeSynced = true;

  float currentBatteryVoltage = getBatteryVoltage();

  strncpy(lastBG_char, bgs0_sgv, sizeof(lastBG_char) - 1);
  lastBG_char[sizeof(lastBG_char) - 1] = '\0';

  strncpy(lastDirection_char, bgs0_direction, sizeof(lastDirection_char) - 1);
  lastDirection_char[sizeof(lastDirection_char) - 1] = '\0';

  lastDelta_val = bgs0_bgdelta;
  lastBGSDateTime_val = bgs0_datetime2;
  lastDataAgeMinutes_val = dataAge;
  hasLastData_val = true;
  lastSuccessfulFetchTime = status0_now1;
  lastBatteryVoltage_val = currentBatteryVoltage;

  return true;
}

// --- Wi-Fi Task (Runs on Core 0) ---
void wifiTask(void *parameter)
{
  // Цей код працює паралельно з відображенням на дисплеї
  bool wifi_connected = connectToWiFi();
  if (wifi_connected)
  {
    dataFetchedSuccessfully = getreadings();
    WiFi.disconnect(true);
    Serial.println("WiFi Disconnected.");
  }
  wifiTaskComplete = true;
  vTaskDelete(NULL);
}

// --- Setup Function ---
void setup()
{
  Serial.begin(115200);

  // Ініціалізація дисплея
  display.init();
  display.flipScreenVertically();
  display.clear();
  display.displayOff(); // Спочатку вимкнений

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup(BUTTON_PIN, 0);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonPress, FALLING);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  uint64_t next_sleep_duration_us = FIVE_MINUTES_IN_US;

  // Оновлюємо час RTC
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  time_t current_rtc_time = tv_now.tv_sec;

  // Логіка часу останнього оновлення
  long time_since_last_actual_fetch_seconds = (lastSuccessfulFetchTime == 0) ? LONG_MAX : (current_rtc_time - lastSuccessfulFetchTime);
  long five_minutes_in_seconds = FIVE_MINUTES_IN_US / 1000000ULL;

  // Якщо прокинулися від кнопки:
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
  {
    Serial.println("Wakeup by BUTTON");
    buttonPressed = true; // Примусово ставимо прапорець
  }

  // --- ГОЛОВНА ЛОГІКА ---
  // Якщо натиснута кнопка - показуємо негайно (не чекаючи Wi-Fi)
  if (buttonPressed)
  {
    displayCurrentData();
    // Тут дисплей буде світитись, поки ми думаємо про Wi-Fi
  }

  // Чи потрібен Wi-Fi? (Таймер спрацював АБО дані застаріли)
  bool needsUpdate = (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) ||
                     (time_since_last_actual_fetch_seconds >= five_minutes_in_seconds);

  if (needsUpdate)
  {
    Serial.println("Starting WiFi Task on Core 0...");
    buttonPressed = false; // Скидаємо прапорець, щоб відловити нові натискання
    wifiTaskComplete = false;
    dataFetchedSuccessfully = false;

    // Запускаємо Wi-Fi на ЯДРІ 0 (Core 0), щоб не блокувати ЯДРО 1 (де кнопка і дисплей)
    xTaskCreatePinnedToCore(wifiTask, "WiFiTask", 10000, NULL, 1, NULL, 0);

    // --- ЦИКЛ ОЧІКУВАННЯ (на ЯДРІ 1) ---
    unsigned long startTime = millis();
    // Чекаємо поки Wi-Fi працює (макс 25 сек)
    while (!wifiTaskComplete && millis() - startTime < 25000)
    {
      // Якщо користувач натиснув кнопку ПІД ЧАС з'єднання Wi-Fi
      if (buttonPressed)
      {
        Serial.println("Button pressed DURING WiFi connection!");
        displayCurrentData(); // Миттєво малюємо старі дані

        // Тримаємо дисплей увімкненим 5 секунд, АЛЕ не блокуємо логіку надовго
        unsigned long displayStart = millis();
        while (millis() - displayStart < DISPLAY_DURATION_S * 1000)
        {
          // Якщо Wi-Fi закінчив роботу поки ми світимо дисплеєм, оновимо дані?
          if (wifiTaskComplete)
            break;
          delay(10);
        }
        display.displayOff();
        buttonPressed = false;
      }
      delay(10); // Дуже коротка пауза, щоб швидко реагувати
    }

    // Якщо Wi-Fi завершився успішно і ми ще не показували дані (або хочемо оновити)
    if (dataFetchedSuccessfully && hasLastData_val)
    {
      // Оновлюємо дані на екрані, якщо він був вимкнений (або можна пропустити це)
      // АЛЕ якщо це був таймер і кнопку НЕ тиснули, екран не треба вмикати?
      // Зазвичай ми хочемо оновити дані і піти спати.
    }

    // Розрахунок сну
    next_sleep_duration_us = FIVE_MINUTES_IN_US;
  }
  else
  {
    // Якщо оновлення не треба, але ми прокинулись від кнопки -> почекали 5 сек вище -> спимо далі
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
    {
      delay(DISPLAY_DURATION_S * 1000); // Дочекатися кінця показу
      display.displayOff();
    }

    // Досипаємо решту часу
    long remaining_sleep_seconds = five_minutes_in_seconds - (time_since_last_actual_fetch_seconds % five_minutes_in_seconds);
    if (remaining_sleep_seconds < 0)
      remaining_sleep_seconds = five_minutes_in_seconds;
    next_sleep_duration_us = (uint64_t)remaining_sleep_seconds * 1000000ULL;
  }

  // --- DEEP SLEEP ---
  detachInterrupt(digitalPinToInterrupt(BUTTON_PIN));
  display.displayOff();
  Serial.printf("Deep Sleep: %llu us\n", next_sleep_duration_us);
  esp_sleep_enable_timer_wakeup(next_sleep_duration_us);
  esp_deep_sleep_start();
}

void loop()
{
  // Не використовується
}