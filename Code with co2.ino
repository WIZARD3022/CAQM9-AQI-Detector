#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_NeoPixel.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define LED_PIN 18
#define NUM_LEDS 3
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

const int liney[] = {0, 14, 28, 42, 54};

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

struct SensorConfig sensors[5];
int sensor_types[] = {SENSOR_CO2, SENSOR_PMS, SENSOR_VOC, SENSOR_NOX};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_BME280 bme;
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

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
  strip.begin();
  strip.show();
  strip.setBrightness(sensor_led_brightness);
  if (!getSysVarBool(MEM_STARTUP_COMPLETE)) {
    ledsOff();
  }
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
  String devid = "AQM9_DEVICE_001";
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
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, 0);
  }
  strip.show();
}

void setLedState(int lednum, int color) {
  uint32_t colormap[] = {0, 0x00FF00, 0xFF9900, 0xFF0000, 0x8F00FF};
  uint32_t colr = colormap[color];
  
  int bri = sensor_led_brightness;
  if (!getSysVarBool(MEM_SENSOR_LED_ON)) {
    bri = 0;
  }
  
  strip.setBrightness(bri);
  strip.setPixelColor(lednum - 1, colr);
  strip.show();
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

void printToSerial() {
  Serial.println("========================================");
  Serial.println("AQM9 Air Quality Monitor");
  Serial.println("========================================");
  Serial.print("Product: "); Serial.print(Product_model);
  Serial.print(" v"); Serial.println(Product_version);
  Serial.print("Manufacturer: "); Serial.println(Manufacturer);
  Serial.println("----------------------------------------");
  
  Serial.print("CO2: "); Serial.print(co2_value); Serial.println(" ppm");
  Serial.print("PM2.5: "); Serial.print(pm25_value); Serial.println(" µg/m³");
  Serial.print("PM10: "); Serial.print(pm10_value); Serial.println(" µg/m³");
  Serial.print("PM1.0: "); Serial.print(pm1_value); Serial.println(" µg/m³");
  Serial.print("VOC Index: "); Serial.println(voc_index_value);
  Serial.print("NOx Index: "); Serial.println(nox_index_value);
  Serial.print("Temperature: "); Serial.print(aht_temp); Serial.print(" °");
  Serial.println(temp_scale_st);
  Serial.print("Humidity: "); Serial.print(aht_hum); Serial.println(" %");
  Serial.print("Pressure: "); Serial.print(aht_hpa); Serial.println(" hPa");
  Serial.print("Air Quality Level: "); Serial.println(air_quality);
  
  Serial.print("Display Mode: "); Serial.println(display_mode);
  Serial.print("Buzzer Active: "); Serial.println(buzzer_active ? "YES" : "NO");
  Serial.print("LED Brightness: "); Serial.println(sensor_led_brightness);
  Serial.print("Uptime: "); Serial.print(getUptimeHrs()); Serial.println(" hours");
  
  Serial.println("----------------------------------------");
  Serial.print("CO2 LED Color: ");
  switch(getLedColor(SENSOR_CO2)) {
    case 1: Serial.println("GREEN (Good)"); break;
    case 2: Serial.println("ORANGE (Moderate)"); break;
    case 3: Serial.println("RED (High)"); break;
    case 4: Serial.println("VIOLET (Very High)"); break;
  }
  
  Serial.print("PM2.5 LED Color: ");
  switch(getLedColor(SENSOR_PMS)) {
    case 1: Serial.println("GREEN (Good)"); break;
    case 2: Serial.println("ORANGE (Moderate)"); break;
    case 3: Serial.println("RED (High)"); break;
    case 4: Serial.println("VIOLET (Very High)"); break;
  }
  
  Serial.print("VOC/NOx LED Color: ");
  int combined = getLedColor(SENSOR_VOC);
  if(getLedColor(SENSOR_NOX) > combined) combined = getLedColor(SENSOR_NOX);
  switch(combined) {
    case 1: Serial.println("GREEN (Good)"); break;
    case 2: Serial.println("ORANGE (Moderate)"); break;
    case 3: Serial.println("RED (High)"); break;
    case 4: Serial.println("VIOLET (Very High)"); break;
  }
  
  Serial.println("========================================\n");
}

void updateDisplay() {
  unsigned long now = millis();
  if (now - last_display_update >= 2000) {
    last_display_update = now;
    
    if (display_mode == 0) {
      showAllSensorValues();
    } else if (display_mode == 1) {
      scrollSensorValues();
    }
  }
}

void showAllSensorValues() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, liney[0]);
  display.printf("    CO2: %4d", (int)co2_value);
  
  display.setCursor(0, liney[1]);
  display.printf(" PM 2.5: %4d", (int)pm25_value);
  
  display.setCursor(0, liney[2]);
  display.printf("VOC/NOx: %3d / %3d", (int)voc_index_value, (int)nox_index_value);
  
  display.setCursor(0, liney[3]);
  display.printf("  Atm P: %.0f hPa", aht_hpa);
  
  display.setCursor(0, liney[4]);
  display.printf("Temp/RH: %.1f%s /%.0f%%", aht_temp, temp_scale_st.c_str(), aht_hum);
  
  display.display();
}

String getSensorName(int sensor_type) {
  if (sensor_type == SENSOR_CO2) return "CO2";
  else if (sensor_type == SENSOR_PMS) return "PM2";
  else if (sensor_type == SENSOR_VOC) return "VOC";
  else if (sensor_type == SENSOR_NOX) return "NOx";
  return "";
}

void showWarning(int sensor_type) {
  if (getSysVarBool(MEM_DISPLAY_SENSOR_INFO)) {
    String sensorst = getSensorName(sensor_type);
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("HIGH");
    display.setTextSize(1);
    display.setCursor(0, 28);
    display.println(sensorst);
    display.display();
    tick = -5;
  }
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

void scrollSensorValues() {
  if (show_sensor_indx > 5) show_sensor_indx = 0;
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  
  switch(show_sensor_indx) {
    case 0:
      display.printf("%4d", (int)co2_value);
      display.setTextSize(1);
      display.setCursor(0, 48);
      display.print("CO2 ppm");
      break;
    case 1:
      display.printf("%4d", (int)pm25_value);
      display.setTextSize(1);
      display.setCursor(0, 48);
      display.print("PM 2.5");
      break;
    case 2:
      display.printf("%3d", (int)voc_index_value);
      display.setTextSize(1);
      display.setCursor(0, 48);
      display.print("VOC Index");
      break;
    case 3:
      display.printf("%3d", (int)nox_index_value);
      display.setTextSize(1);
      display.setCursor(0, 48);
      display.print("NOx Index");
      break;
    case 4:
      display.printf("%.1f", aht_temp);
      display.setTextSize(1);
      display.setCursor(0, 48);
      display.printf("Degrees %s", temp_scale_st.c_str());
      break;
    case 5:
      display.printf("%.0f", aht_hum);
      display.setTextSize(1);
      display.setCursor(0, 48);
      display.print("Rel Humidity");
      break;
  }
  display.display();
  
  show_sensor_indx++;
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

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n");
  Serial.println("========================================");
  Serial.println("AQM9 Sensor Display System Starting...");
  Serial.println("========================================\n");
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("ERROR: SSD1306 display not found!");
  } else {
    Serial.println("Display initialized successfully");
  }
  display.clearDisplay();
  display.display();
  
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
  
  Serial.println("\nSetup Complete! Starting main loop...\n");
}

void loop() {
  unsigned long now = millis();
  
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
  
  // Print to serial every 5 seconds
  if (now - last_serial_print >= 5000) {
    last_serial_print = now;
    printToSerial();
  }
  
  updateDisplay();
  
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
