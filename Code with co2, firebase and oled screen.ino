#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// ============ U8G2 OLED DISPLAY SETUP (0.9" 128x32) ============
// Using SH1106 driver (common for 0.9" OLEDs, compatible with SSD1306)
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ============ WIFI CREDENTIALS ============
#define WIFI_SSID "AU"
#define WIFI_PASSWORD "a45"

// ============ FIREBASE CREDENTIALS ============
#define API_KEY "AaeEveJU"
#define DATABASE_URL "https://aqi-db.firebaseio.com"
#define USER_EMAIL "tail.com"
#define USER_PASSWORD "156"

// ============ LED PIN DEFINITIONS ============
#define LED1_PIN 32    // LED1 - CO2 indicator
#define LED2_PIN 33    // LED2 - PM2.5 indicator  
#define LED3_PIN 25    // LED3 - VOC/NOx indicator

// ============ BUTTON PIN DEFINITIONS ============
#define BUTTON1_PIN 34  // B1 - Display mode button
#define BUTTON2_PIN 35  // B2 - Settings button

#define BUZZER_PIN 4
#define CO2_RX_PIN 16
#define CO2_TX_PIN 17
#define PMS_RX_PIN 19
#define PMS_TX_PIN 21
#define BME280_ADDR 0x76

#define SENSOR_CO2 1
#define SENSOR_PMS 2
#define SENSOR_VOC 3
#define SENSOR_NOX 4

#define MEM_DISPLAY_SENSOR_INFO 16
#define MEM_SENSOR_LED_ON 15
#define MEM_STARTUP_COMPLETE 14

// Line positions for 0.9" OLED (32px usable height with U8G2)
// Using font: u8g2_font_ncenB08_tr (8px height + 2px spacing = 10px per line)
// 32px height / 10px = 3 lines maximum for comfortable display
const int line_y_positions[] = {10, 22, 34, 46};  // Y positions for up to 4 lines

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;
unsigned long lastFirebaseSend = 0;
unsigned long firebaseSendInterval = 10000;

struct SensorConfig {
  float normal;
  float moderate;
  float high;
  int led_color;
  int last_level;
  int change_count;
  int alert_pct;
};

String Product_version = "1.91";
String Product_model = "AQM9";
String Manufacturer = "CLARATECH";

float co2_value = -1;
float pm25_value = -1;
float pm10_value = 0;
float pm1_value = 0;
float voc_raw_value = -1;
float voc_index_value = -1;
float nox_raw_value = -1;
float nox_index_value = -1;
float aht_temp = 0;
float aht_hum = 0;
float aht_hpa = 0;
float esp_temp = 0;

int display_mode = 0;
int display_mode_previous = 0;
int sensor_led_brightness = 50;
int sensor_led_brightness_default = 50;
int show_sensor_indx = 0;
int air_quality = 0;

unsigned long tick = 0;
unsigned long last_update = 0;
unsigned long last_serial_print = 0;
unsigned long last_display_update = 0;
unsigned long last_alert_check = 0;
unsigned long last_offset_update = 0;
unsigned long last_button_check = 0;
int level_change_count_limit = 60;
float current_uptime_hrs = 0;
float autorestart_hrs = 0;

bool buzzer_active = true;
bool onceonstartup1 = false;
bool onceonstartup2 = false;
bool cold_start = true;
float time_to_cold_start = 8;
float tempoffset = 0;
String temp_scale_st = "C";
bool clear_screen_after_warning = false;
bool lastButton1State = HIGH;
bool lastButton2State = HIGH;

struct SensorConfig sensors[5];
int sensor_types[] = {SENSOR_CO2, SENSOR_PMS, SENSOR_VOC, SENSOR_NOX};

Adafruit_BME280 bme;

void initSettingsAndFlags() {
  setSysVarBool(MEM_DISPLAY_SENSOR_INFO, false);
  setSysVarBool(MEM_SENSOR_LED_ON, true);
  sensor_led_brightness_default = 50;
  sensor_led_brightness = sensor_led_brightness_default;
  display_mode = 0;
  display_mode_previous = 0;
  updateBuzzerAndDisplayModeState();
  display_mode_previous = display_mode;
  onceonstartup1 = false;
  onceonstartup2 = false;
  cold_start = true;
  time_to_cold_start = 8;
  setTempScale();
}

void initLEDs() {
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  
  ledsOff();
}

void initSensorThresholds() {
  sensors[SENSOR_CO2].normal = 800;
  sensors[SENSOR_CO2].moderate = 1200;
  sensors[SENSOR_CO2].high = 2000;
  sensors[SENSOR_CO2].led_color = 0;
  sensors[SENSOR_CO2].last_level = 0;
  sensors[SENSOR_CO2].change_count = 0;
  sensors[SENSOR_CO2].alert_pct = 105;
  
  sensors[SENSOR_PMS].normal = 5;
  sensors[SENSOR_PMS].moderate = 20;
  sensors[SENSOR_PMS].high = 50;
  sensors[SENSOR_PMS].led_color = 0;
  sensors[SENSOR_PMS].last_level = 0;
  sensors[SENSOR_PMS].change_count = 0;
  sensors[SENSOR_PMS].alert_pct = 120;
  
  sensors[SENSOR_VOC].normal = 100;
  sensors[SENSOR_VOC].moderate = 200;
  sensors[SENSOR_VOC].high = 350;
  sensors[SENSOR_VOC].led_color = 0;
  sensors[SENSOR_VOC].last_level = 0;
  sensors[SENSOR_VOC].change_count = 0;
  sensors[SENSOR_VOC].alert_pct = 105;
  
  sensors[SENSOR_NOX].normal = 1;
  sensors[SENSOR_NOX].moderate = 20;
  sensors[SENSOR_NOX].high = 200;
  sensors[SENSOR_NOX].led_color = 0;
  sensors[SENSOR_NOX].last_level = 0;
  sensors[SENSOR_NOX].change_count = 0;
  sensors[SENSOR_NOX].alert_pct = 105;
}

void initVariables() {
  tick = 0;
  co2_value = -1;
  pm10_value = 0;
  pm1_value = 0;
  pm25_value = -1;
  voc_raw_value = -1;
  voc_index_value = -1;
  nox_raw_value = -1;
  nox_index_value = -1;
  aht_temp = 0;
  aht_hum = 0;
  aht_hpa = 0;
  show_sensor_indx = 0;
  buzzer_active = true;
  level_change_count_limit = 60;
  esp_temp = 0;
  tempoffset = 0;
  air_quality = 0;
  autorestart_hrs = getSysMemAsNumber(11);
  current_uptime_hrs = 0;
  clear_screen_after_warning = false;
}

String getDeviceId() {
  String devid = "AQM9_" + String(WiFi.macAddress());
  devid.replace(":", "");
  return devid;
}

int getSysMemAsNumber(int num) {
  return 0;
}

void setSysMemNum(int num, int val) {
}

bool getSysVarBool(int num) {
  return false;
}

void setSysVarBool(int num, bool val) {
}

void updateBuzzerAndDisplayModeState() {
  int mem = getSysMemAsNumber(16);
  buzzer_active = (mem != 0);
  
  mem = getSysMemAsNumber(15);
  if (mem >= 0) display_mode = mem;
  
  mem = getSysMemAsNumber(14);
  if (mem >= 10 && mem <= 100) {
    sensor_led_brightness = mem;
  } else {
    sensor_led_brightness = sensor_led_brightness_default;
    setSysMemNum(14, sensor_led_brightness);
  }
}

float getUptimeHrs() {
  return (millis() / 1000.0 / 60.0 / 60.0);
}

void setTempScale() {
  temp_scale_st = "C";
}

void ledsOff() {
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);
}

void setLedState(int lednum, int color) {
  int ledPin;
  int brightness;
  
  if (lednum == 1) {
    ledPin = LED1_PIN;
  } else if (lednum == 2) {
    ledPin = LED2_PIN;
  } else {
    ledPin = LED3_PIN;
  }
  
  if (!getSysVarBool(MEM_SENSOR_LED_ON)) {
    digitalWrite(ledPin, LOW);
    return;
  }
  
  switch(color) {
    case 1: // Good
      brightness = map(sensor_led_brightness, 0, 100, 10, 30);
      break;
    case 2: // Moderate
      brightness = map(sensor_led_brightness, 0, 100, 30, 60);
      break;
    case 3: // High
      brightness = map(sensor_led_brightness, 0, 100, 60, 85);
      break;
    case 4: // Very High
      brightness = map(sensor_led_brightness, 0, 100, 85, 100);
      break;
    default:
      brightness = 0;
  }
  
  int pwmValue = map(brightness, 0, 100, 0, 255);
  analogWrite(ledPin, pwmValue);
}

void checkButtons() {
  bool button1State = digitalRead(BUTTON1_PIN);
  bool button2State = digitalRead(BUTTON2_PIN);
  
  if (button1State == LOW && lastButton1State == HIGH) {
    display_mode++;
    if (display_mode > 2) display_mode = 0;
    setSysMemNum(15, display_mode);
    Serial.print("Display mode changed to: ");
    Serial.println(display_mode);
    buzzer(1);
    delay(200);
  }
  
  if (button2State == LOW && lastButton2State == HIGH) {
    buzzer_active = !buzzer_active;
    setSysMemNum(16, buzzer_active ? 1 : 0);
    Serial.print("Buzzer is now: ");
    Serial.println(buzzer_active ? "ON" : "OFF");
    if (buzzer_active) buzzer(1);
    delay(200);
  }
  
  lastButton1State = button1State;
  lastButton2State = button2State;
}

float getSensorValue(int sensor_type) {
  if (sensor_type == SENSOR_CO2) return co2_value;
  else if (sensor_type == SENSOR_PMS) return pm25_value;
  else if (sensor_type == SENSOR_VOC) return voc_index_value;
  else if (sensor_type == SENSOR_NOX) return nox_index_value;
  return -1;
}

int getPollutionLevel(float value, float normal, float moderate, float high) {
  if (value <= normal) return 1;
  else if (value <= moderate) return 2;
  else if (value <= high) return 3;
  else return 4;
}

bool isLevelWellAboveSetlimits(int sensor_type) {
  struct SensorConfig* cfg = &sensors[sensor_type];
  float value = getSensorValue(sensor_type);
  float pct = cfg->alert_pct / 100.0;
  
  float newnormal = cfg->normal * pct;
  float newmedium = cfg->moderate * pct;
  float newhigh = cfg->high * pct;
  
  if (value >= newnormal || value >= newmedium || value >= newhigh) {
    if (cfg->change_count >= level_change_count_limit) {
      return true;
    }
  }
  return false;
}

void buzzer(int beeps) {
  if (buzzer_active) {
    for (int i = 0; i < beeps; i++) {
      tone(BUZZER_PIN, 2000, 200);
      delay(250);
      noTone(BUZZER_PIN);
    }
  }
}

void loadSensorValues() {
  aht_temp = bme.readTemperature();
  aht_hum = bme.readHumidity();
  aht_hpa = bme.readPressure() / 100.0F;
  
  // Replace with actual sensor readings
  if (co2_value < 0) co2_value = 400 + random(0, 100);
  if (pm25_value < 0) pm25_value = 5 + random(0, 20);
  if (voc_index_value < 0) voc_index_value = 50 + random(0, 100);
  if (nox_index_value < 0) nox_index_value = 10 + random(0, 50);
}

float calculateTempOffset(float uptime_mins) {
  if (uptime_mins <= 5) return -0.5;
  else if (uptime_mins <= 8) return -1.25;
  else if (uptime_mins <= 12) return -2;
  else if (uptime_mins <= 18) return -3;
  else if (uptime_mins <= 30) return -3.5;
  else if (uptime_mins <= 60) return -4;
  else if (uptime_mins <= 90) return -4.5;
  else return -5;
}

void applyOffsets(float temp_offset, float uptime_mins) {
  float new_hum_offset = (temp_offset * -1) * 4;
  
  float final_temp_offset = temp_offset;
  if (temp_scale_st == "F") {
    final_temp_offset = temp_offset * 2;
  }
  
  if (final_temp_offset != tempoffset) {
    tempoffset = final_temp_offset;
    setSysMemNum(9, (int)uptime_mins);
  }
}

void setTempoffset() {
  int toffset_status = getSysMemAsNumber(12);
  if (toffset_status <= 0) {
    Serial.println("Auto temp-offset is disabled");
    return;
  }
  
  float uptime_mins = (millis() / 1000.0) / 60.0;
  float new_temp_offset = calculateTempOffset(uptime_mins);
  applyOffsets(new_temp_offset, uptime_mins);
}

void updateLedColors() {
  for (int i = 0; i < 4; i++) {
    int sensor_type = sensor_types[i];
    float value = getSensorValue(sensor_type);
    sensors[sensor_type].led_color = getPollutionLevel(value, 
      sensors[sensor_type].normal, 
      sensors[sensor_type].moderate, 
      sensors[sensor_type].high);
  }
}

int getLedColor(int sensor_type) {
  return sensors[sensor_type].led_color;
}

void shineleds() {
  updateLedColors();
  
  int voc_color = getLedColor(SENSOR_VOC);
  int nox_color = getLedColor(SENSOR_NOX);
  int voc_nox_combined = voc_color;
  if (nox_color > voc_color) voc_nox_combined = nox_color;
  
  setLedState(1, getLedColor(SENSOR_CO2));
  setLedState(2, getLedColor(SENSOR_PMS));
  setLedState(3, voc_nox_combined);
}

void sendToFirebase() {
  if (!signupOK) {
    Serial.println("Firebase not authenticated, skipping send");
    return;
  }
  
  String deviceId = getDeviceId();
  String path = "/devices/" + deviceId + "/sensors/";
  
  Serial.println("Sending data to Firebase...");
  
  Firebase.RTDB.setFloat(&fbdo, path + "co2/value", co2_value);
  Firebase.RTDB.setFloat(&fbdo, path + "pm25/value", pm25_value);
  Firebase.RTDB.setFloat(&fbdo, path + "pm10/value", pm10_value);
  Firebase.RTDB.setFloat(&fbdo, path + "pm1/value", pm1_value);
  Firebase.RTDB.setFloat(&fbdo, path + "voc/value", voc_index_value);
  Firebase.RTDB.setFloat(&fbdo, path + "nox/value", nox_index_value);
  Firebase.RTDB.setFloat(&fbdo, path + "temperature/value", aht_temp);
  Firebase.RTDB.setFloat(&fbdo, path + "humidity/value", aht_hum);
  Firebase.RTDB.setFloat(&fbdo, path + "pressure/value", aht_hpa);
  
  computeAirQuality();
  Firebase.RTDB.setInt(&fbdo, path + "air_quality_index/value", air_quality);
  
  String sysPath = "/devices/" + deviceId + "/system/";
  Firebase.RTDB.setString(&fbdo, sysPath + "product_model", Product_model);
  Firebase.RTDB.setString(&fbdo, sysPath + "product_version", Product_version);
  Firebase.RTDB.setString(&fbdo, sysPath + "manufacturer", Manufacturer);
  Firebase.RTDB.setFloat(&fbdo, sysPath + "uptime_hours", getUptimeHrs());
  Firebase.RTDB.setInt(&fbdo, sysPath + "display_mode", display_mode);
  Firebase.RTDB.setBool(&fbdo, sysPath + "buzzer_active", buzzer_active);
  Firebase.RTDB.setInt(&fbdo, sysPath + "led_brightness", sensor_led_brightness);
  Firebase.RTDB.setFloat(&fbdo, "/devices/" + deviceId + "/last_update", millis() / 1000.0);
  
  Serial.println("Firebase data send complete!");
}

void printToSerial() {
  Serial.println("========================================");
  Serial.println("AQM9 Air Quality Monitor");
  Serial.println("========================================");
  Serial.print("CO2: "); Serial.print(co2_value); Serial.println(" ppm");
  Serial.print("PM2.5: "); Serial.print(pm25_value); Serial.println(" µg/m³");
  Serial.print("VOC: "); Serial.print(voc_index_value);
  Serial.print("  NOx: "); Serial.println(nox_index_value);
  Serial.print("Temp: "); Serial.print(aht_temp); Serial.print("°");
  Serial.println(temp_scale_st);
  Serial.print("Humidity: "); Serial.print(aht_hum); Serial.println("%");
  Serial.print("Pressure: "); Serial.print(aht_hpa); Serial.println(" hPa");
  Serial.print("Air Quality: "); Serial.println(air_quality);
  Serial.print("Display Mode: "); Serial.println(display_mode);
  Serial.println("========================================\n");
}

// ============ U8G2 DISPLAY FUNCTIONS ============

void updateDisplay() {
  unsigned long now = millis();
  if (now - last_display_update >= 2000) {
    last_display_update = now;
    
    u8g2.clearBuffer();  // Clear the internal buffer (like display.clearDisplay())
    
    if (display_mode == 0) {
      showAllSensorValues();
    } else if (display_mode == 1) {
      scrollSensorValues();
    } else if (display_mode == 2) {
      showDetailedSensorValues();
    }
    
    u8g2.sendBuffer();   // Transfer buffer to display (like display.display())
  }
}

void showAllSensorValues() {
  // Set font for better readability on 0.9" OLED
  u8g2.setFont(u8g2_font_ncenB08_tr);  // 8px height font
  
  // Line 1: CO2
  u8g2.setCursor(0, line_y_positions[0]);
  u8g2.printf("CO2:%4dppm", (int)co2_value);
  
  // Line 2: PM2.5
  u8g2.setCursor(0, line_y_positions[1]);
  u8g2.printf("PM2.5:%4d", (int)pm25_value);
  
  // Line 3: VOC/NOx
  u8g2.setCursor(0, line_y_positions[2]);
  u8g2.printf("V:%3d N:%3d", (int)voc_index_value, (int)nox_index_value);
  
  // Line 4: Temp/Hum (if fits)
  u8g2.setCursor(0, line_y_positions[3]);
  u8g2.printf("%.1f%c %.0f%%", aht_temp, temp_scale_st[0], aht_hum);
}

void scrollSensorValues() {
  if (show_sensor_indx > 4) show_sensor_indx = 0;
  
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  switch(show_sensor_indx) {
    case 0:
      u8g2.setCursor(0, 20);
      u8g2.printf("CO2: %4d ppm", (int)co2_value);
      break;
    case 1:
      u8g2.setCursor(0, 20);
      u8g2.printf("PM2.5: %4d", (int)pm25_value);
      break;
    case 2:
      u8g2.setCursor(0, 15);
      u8g2.printf("VOC: %3d", (int)voc_index_value);
      u8g2.setCursor(0, 30);
      u8g2.printf("NOx: %3d", (int)nox_index_value);
      break;
    case 3:
      u8g2.setCursor(0, 15);
      u8g2.printf("Temp: %.1f%c", aht_temp, temp_scale_st[0]);
      u8g2.setCursor(0, 30);
      u8g2.printf("Hum: %.0f%%", aht_hum);
      break;
    case 4:
      u8g2.setCursor(0, 20);
      u8g2.printf("Pres: %.0f hPa", aht_hpa);
      break;
  }
  
  show_sensor_indx++;
}

void showDetailedSensorValues() {
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  // Show AQI
  u8g2.setCursor(0, 12);
  u8g2.printf("AQI: %d", air_quality);
  
  // Show uptime
  u8g2.setCursor(0, 24);
  u8g2.printf("Up: %.1fh", getUptimeHrs());
  
  // Show WiFi status
  u8g2.setCursor(0, 36);
  if (WiFi.status() == WL_CONNECTED) {
    u8g2.printf("WiFi: OK");
  } else {
    u8g2.printf("WiFi: NO");
  }
  
  // Show buzzer status
  u8g2.setCursor(0, 48);
  u8g2.printf("Buz: %s", buzzer_active ? "ON" : "OFF");
}

void showWarning(int sensor_type) {
  if (getSysVarBool(MEM_DISPLAY_SENSOR_INFO)) {
    String sensorst = getSensorName(sensor_type);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.setCursor(0, 20);
    u8g2.print("HIGH");
    u8g2.setCursor(0, 40);
    u8g2.print(sensorst);
    u8g2.sendBuffer();
    tick = -5;
    delay(1500);  // Show warning for 1.5 seconds
  }
}

String getSensorName(int sensor_type) {
  if (sensor_type == SENSOR_CO2) return "CO2";
  else if (sensor_type == SENSOR_PMS) return "PM2.5";
  else if (sensor_type == SENSOR_VOC) return "VOC";
  else if (sensor_type == SENSOR_NOX) return "NOx";
  return "";
}

void raiseAlert() {
  for (int i = 0; i < 4; i++) {
    int sensor_type = sensor_types[i];
    struct SensorConfig* cfg = &sensors[sensor_type];
    
    int color = cfg->led_color;
    int last_level = cfg->last_level;
    
    if (color > 2 && color > last_level) {
      if (isLevelWellAboveSetlimits(sensor_type)) {
        buzzer(color);
        showWarning(sensor_type);
        cfg->last_level = color;
        cfg->change_count = 0;
      }
    }
    
    if (cfg->last_level > color) {
      cfg->last_level = color;
    }
  }
}

void computeAirQuality() {
  int aq = 0;
  for (int i = 0; i < 4; i++) {
    int color = getLedColor(sensor_types[i]);
    if (color > aq) aq = color;
  }
  air_quality = aq;
}

void checkAutoRestart() {
  current_uptime_hrs = getUptimeHrs();
  if (autorestart_hrs > 0 && current_uptime_hrs >= autorestart_hrs) {
    Serial.println("Auto-restart triggered");
    ESP.restart();
  }
}

void jsonAppend() {
  computeAirQuality();
}

void setupWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
}

void setupFirebase() {
  Serial.println("Setting up Firebase...");
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  
  config.token_status_callback = tokenStatusCallback;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  signupOK = true;
  Serial.println("Firebase setup complete!");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n");
  Serial.println("AQM9 Sensor Display System Starting...");
  
  // Initialize U8G2 OLED display (0.9" 128x32)
  u8g2.begin();
  u8g2.enableUTF8Print();
  Serial.println("U8G2 OLED initialized successfully");
  
  // Test display
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.setCursor(0, 20);
  u8g2.print("Starting...");
  u8g2.sendBuffer();
  delay(2000);
  
  setupWiFi();
  setupFirebase();
  
  if (!bme.begin(BME280_ADDR)) {
    Serial.println("ERROR: BME280 sensor not found!");
  } else {
    Serial.println("BME280 sensor initialized successfully");
  }
  
  initSettingsAndFlags();
  initLEDs();
  initSensorThresholds();
  initVariables();
  setTempScale();
  updateBuzzerAndDisplayModeState();
  
  last_update = millis();
  last_serial_print = millis();
  last_display_update = millis();
  last_alert_check = millis();
  last_offset_update = millis();
  lastFirebaseSend = millis();
  last_button_check = millis();
  
  // Show ready message
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.setCursor(0, 20);
  u8g2.print("Ready!");
  u8g2.sendBuffer();
  delay(1000);
  
  Serial.println("Setup Complete!\n");
}

void loop() {
  unsigned long now = millis();
  
  if (now - last_button_check >= 100) {
    last_button_check = now;
    checkButtons();
  }
  
  if (now - last_update >= 1000) {
    last_update = now;
    tick++;
    
    loadSensorValues();
    shineleds();
    computeAirQuality();
    
    if (cold_start && tick >= (unsigned long)(time_to_cold_start * 1000)) {
      cold_start = false;
      Serial.println("Warm-up period complete");
    }
  }
  
  if (now - last_serial_print >= 5000) {
    last_serial_print = now;
    printToSerial();
  }
  
  if (now - lastFirebaseSend >= firebaseSendInterval) {
    lastFirebaseSend = now;
    sendToFirebase();
  }
  
  updateDisplay();  // Uses U8G2 library
  
  if (now - last_alert_check >= 60000) {
    last_alert_check = now;
    raiseAlert();
    checkAutoRestart();
  }
  
  if (now - last_offset_update >= 3600000) {
    last_offset_update = now;
    setTempoffset();
  }
  
  delay(100);
}
