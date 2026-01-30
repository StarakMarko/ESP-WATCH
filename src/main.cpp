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
const uint64_t SHORT_SLEEP_US = 10 * 1000000ULL;

// --- RTC Memory Variables ---
RTC_DATA_ATTR bool timeSynced = false;
RTC_DATA_ATTR char lastBG_char[10];
RTC_DATA_ATTR char lastDirection_char[20];
RTC_DATA_ATTR int lastDelta_val;
RTC_DATA_ATTR time_t lastBGSDateTime_val;
RTC_DATA_ATTR int lastDataAgeMinutes_val;
RTC_DATA_ATTR bool hasLastData_val;
RTC_DATA_ATTR time_t lastSuccessfulFetchTime = 0;
RTC_DATA_ATTR float lastBatteryVoltage_val = 0.0;

// --- Global Variables ---
bool firstrun = true;
const char *directarr = "";
volatile bool buttonPressed = false;
volatile bool wifiTaskComplete = false;
volatile bool dataFetchedSuccessfully = false;

// --- Arrow Bitmaps ---
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
  Serial.printf("Analog Read for Battery: %d\n", analogValue);
  float voltageAtADC = (float)analogValue * (ADC_MAX_VOLTAGE / ADC_RESOLUTION);
  float batteryVoltage = voltageAtADC * (R1 + R2) / R2;
  Serial.printf("Calculated Battery Voltage: %.2fV\n", batteryVoltage);
  return batteryVoltage;
}

// --- Adjust Timezone ---
void adjustTimezone(time_t &timestamp)
{
  timestamp += timezoneOffset;
}

// --- Display Data ---
void showdata(time_t datetimenow, String BG, int age, float batteryVoltage, String bgs0_direction, int delta)
{
  display.clear();
  display.displayOn();
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
  display.drawString(45 + 1, 19, BG);
  if (String(bgs0_direction) == "Flat")
  {
    directarr = "→";
    display.drawXbm(101, 26, 16, 16, ArrowSide);
  }
  else if (String(bgs0_direction) == "FortyFiveUp")
  {
    directarr = "↗";
    display.drawXbm(101, 26, 16, 16, ArrowUpS);
  }
  else if (String(bgs0_direction) == "FortyFiveDown")
  {
    directarr = "↘";
    display.drawXbm(101, 26, 16, 16, ArrowDownS);
  }
  else if (String(bgs0_direction) == "SingleUp")
  {
    directarr = "↑";
    display.drawXbm(101, 26, 16, 16, ArrowUp);
  }
  else if (String(bgs0_direction) == "SingleDown")
  {
    directarr = "↓";
    display.drawXbm(101, 26, 16, 16, ArrowDown);
  }
  else if (String(bgs0_direction) == "DoubleUp")
  {
    directarr = "↑↑";
    display.drawXbm(101, 26, 16, 16, ArrowUpD);
  }
  else if (String(bgs0_direction) == "DoubleDown")
  {
    directarr = "↓↓";
    display.drawXbm(101, 26, 16, 16, ArrowDownD);
  }
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(2, 50, "Батарея: " + String(batteryVoltage, 2) + "V");
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
  while (WiFi.status() != WL_CONNECTED && connectAttempts < 40)
  {
    Serial.print(".");
    delay(500);
    connectAttempts++;
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nConnected to WIFI");
    Serial.println(WiFi.localIP());
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
  while (client.connected())
  {
    String line = client.readStringUntil('\n');
    if (line == "\r")
      break;
    delay(10);
  }
  String line2 = client.readStringUntil('\n');
  Serial.println("==========");
  Serial.println(line2);
  const size_t bufferSize = 2048;
  DynamicJsonDocument doc(bufferSize);
  DeserializationError error = deserializeJson(doc, line2);
  if (error)
  {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.f_str());
    return false;
  }
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

// --- Wi-Fi Task ---
void wifiTask(void *parameter)
{
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
  Serial.setTimeout(2000);
  Serial.println();
  display.init();
  display.flipScreenVertically();
  display.clear();
  display.displayOff();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup(BUTTON_PIN, 0);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonPress, FALLING);
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  uint64_t next_sleep_duration_us = FIVE_MINUTES_IN_US;

  switch (wakeup_reason)
  {
  case ESP_SLEEP_WAKEUP_EXT0:
  {
    Serial.println("Wakeup by EXT0 (button)");
    // Display last known data if available
    if (hasLastData_val)
    {
      struct timeval tv_now;
      gettimeofday(&tv_now, NULL);
      time_t current_rtc_time = tv_now.tv_sec;
      setTime(current_rtc_time);
      int current_data_age = (current_rtc_time - lastBGSDateTime_val) / 60;
      if (current_data_age < 0)
        current_data_age = 0;
      showdata(current_rtc_time, String(lastBG_char), current_data_age, lastBatteryVoltage_val, String(lastDirection_char), lastDelta_val);
      delay(DISPLAY_DURATION_S * 1000);
      display.displayOff(); // Explicitly turn off display after 5 seconds
    }
    else
    {
      display.clear();
      display.displayOn();
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 26, "No Data Stored.");
      display.drawString(64, 40, "Waiting for sync...");
      display.display();
      delay(DISPLAY_DURATION_S * 1000);
      display.displayOff(); // Explicitly turn off display after 5 seconds
    }

    // Calculate time since last fetch
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    time_t current_rtc_time = tv_now.tv_sec;
    long time_since_last_actual_fetch_seconds = (lastSuccessfulFetchTime == 0) ? LONG_MAX : (current_rtc_time - lastSuccessfulFetchTime);
    long five_minutes_in_seconds = FIVE_MINUTES_IN_US / 1000000ULL;

    // If more than 5 minutes since last fetch, trigger a new fetch
    if (time_since_last_actual_fetch_seconds >= five_minutes_in_seconds)
    {
      buttonPressed = false;
      wifiTaskComplete = false;
      dataFetchedSuccessfully = false;
      xTaskCreatePinnedToCore(wifiTask, "WiFiTask", 10000, NULL, 1, NULL, 0);

      // Wait for Wi-Fi task to complete
      unsigned long startTime = millis();
      while (!wifiTaskComplete && millis() - startTime < 20000)
      {
        // Handle button presses during Wi-Fi task
        if (buttonPressed && hasLastData_val)
        {
          struct timeval tv_now_inner;
          gettimeofday(&tv_now_inner, NULL);
          time_t current_rtc_time_inner = tv_now_inner.tv_sec;
          setTime(current_rtc_time_inner);
          int current_data_age = (current_rtc_time_inner - lastBGSDateTime_val) / 60;
          if (current_data_age < 0)
            current_data_age = 0;
          showdata(current_rtc_time_inner, String(lastBG_char), current_data_age, lastBatteryVoltage_val, String(lastDirection_char), lastDelta_val);
          delay(DISPLAY_DURATION_S * 1000);
          display.displayOff(); // Explicitly turn off display after 5 seconds
          buttonPressed = false;
        }
        delay(50); // Reduced delay for faster button response
      }

      // After Wi-Fi task completes or times out
      if (!dataFetchedSuccessfully && hasLastData_val && timeSynced)
      {
        struct timeval tv_now;
        gettimeofday(&tv_now, NULL);
        time_t current_rtc_time = tv_now.tv_sec;
        setTime(current_rtc_time);
        int current_data_age = (current_rtc_time - lastBGSDateTime_val) / 60;
        if (current_data_age < 0)
          current_data_age = 0;
        showdata(current_rtc_time, String(lastBG_char), current_data_age, lastBatteryVoltage_val, String(lastDirection_char), lastDelta_val);
        delay(DISPLAY_DURATION_S * 1000);
        display.displayOff(); // Explicitly turn off display after 5 seconds
      }
      else if (!dataFetchedSuccessfully && !hasLastData_val)
      {
        display.clear();
        display.displayOn();
        display.setFont(ArialMT_Plain_10);
        display.setTextAlignment(TEXT_ALIGN_CENTER);
        display.drawString(64, 26, "No data available.");
        display.drawString(64, 40, "Try again later.");
        display.display();
        delay(DISPLAY_DURATION_S * 1000);
        display.displayOff(); // Explicitly turn off display after 5 seconds
      }
      // After fetch, sleep for 5 minutes to maintain schedule
      next_sleep_duration_us = FIVE_MINUTES_IN_US;
    }
    else
    {
      // Sleep until the next 5-minute mark
      long remaining_sleep_seconds = five_minutes_in_seconds - (time_since_last_actual_fetch_seconds % five_minutes_in_seconds);
      Serial.printf("Sleeping for remaining %ld seconds to hit 5-min mark.\n", remaining_sleep_seconds);
      next_sleep_duration_us = (uint64_t)remaining_sleep_seconds * 1000000ULL;
    }
    break;
  }

  case ESP_SLEEP_WAKEUP_TIMER:
  case ESP_SLEEP_WAKEUP_UNDEFINED:
  default:
  {
    Serial.println("Normal boot or reset / Wakeup by timer");
    buttonPressed = false;
    wifiTaskComplete = false;
    dataFetchedSuccessfully = false;
    xTaskCreatePinnedToCore(wifiTask, "WiFiTask", 10000, NULL, 1, NULL, 0);

    // Handle button presses and display updates on Core 1
    unsigned long startTime = millis();
    while (!wifiTaskComplete && millis() - startTime < 20000)
    {
      if (buttonPressed && hasLastData_val)
      {
        struct timeval tv_now;
        gettimeofday(&tv_now, NULL);
        time_t current_rtc_time = tv_now.tv_sec;
        setTime(current_rtc_time);
        int current_data_age = (current_rtc_time - lastBGSDateTime_val) / 60;
        if (current_data_age < 0)
          current_data_age = 0;
        showdata(current_rtc_time, String(lastBG_char), current_data_age, lastBatteryVoltage_val, String(lastDirection_char), lastDelta_val);
        delay(DISPLAY_DURATION_S * 1000);
        display.displayOff(); // Explicitly turn off display after 5 seconds
        buttonPressed = false;
      }
      delay(50); // Reduced delay for faster button response
    }

    // After Wi-Fi task completes or times out
    if (!dataFetchedSuccessfully && hasLastData_val && timeSynced)
    {
      struct timeval tv_now;
      gettimeofday(&tv_now, NULL);
      time_t current_rtc_time = tv_now.tv_sec;
      setTime(current_rtc_time);
      int current_data_age = (current_rtc_time - lastBGSDateTime_val) / 60;
      if (current_data_age < 0)
        current_data_age = 0;
      showdata(current_rtc_time, String(lastBG_char), current_data_age, lastBatteryVoltage_val, String(lastDirection_char), lastDelta_val);
      delay(DISPLAY_DURATION_S * 1000);
      display.displayOff(); // Explicitly turn off display after 5 seconds
    }
    else if (!dataFetchedSuccessfully && !hasLastData_val)
    {
      display.clear();
      display.displayOn();
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 26, "No data available.");
      display.drawString(64, 40, "Try again later.");
      display.display();
      delay(DISPLAY_DURATION_S * 1000);
      display.displayOff(); // Explicitly turn off display after 5 seconds
    }
    next_sleep_duration_us = FIVE_MINUTES_IN_US;
    break;
  }
  }

  // Prepare for Deep Sleep
  detachInterrupt(digitalPinToInterrupt(BUTTON_PIN));
  display.displayOff();
  Serial.printf("Going to deep sleep for %llu microseconds...\n", next_sleep_duration_us);
  esp_sleep_enable_timer_wakeup(next_sleep_duration_us);
  esp_deep_sleep_start();
}

// --- Loop Function ---
void loop()
{
  // Not used due to deep sleep
}