#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_BME280.h>
#include <MHZ19.h>
#include <PMS.h>
#include <SensirionI2CSgp41.h>
#include <Preferences.h>

// Firebase
#include <Firebase_ESP_Client.h>

// ---------------- WIFI ----------------
#define WIFI_SSID "ACIC_SGTU"
#define WIFI_PASSWORD "acic12345"

// ---------------- FIREBASE ----------------
#define API_KEY "AIzaSyDuA46c6I8JZVekPPN1SLSt8ZZ0aeEveJU"
#define DATABASE_URL "https://aqi-database-71b10-default-rtdb.firebaseio.com"
#define USER_EMAIL "test@gmail.com"
#define USER_PASSWORD "123456"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ---------------- DISPLAY ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------------- LED ----------------
#define LED_PIN 18
#define LED_COUNT 3
Adafruit_NeoPixel leds(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------------- BUZZER ----------------
#define BUZZER_PIN 15

// ---------------- BUTTONS ----------------
#define BTN_MENU 0
#define BTN_OPTION 2

// ---------------- SENSORS ----------------
HardwareSerial mhzSerial(2);
MHZ19 mhz;

HardwareSerial pmsSerial(1);
PMS pms(pmsSerial);
PMS::DATA pmsData;

Adafruit_BME280 bme;
SensirionI2CSgp41 sgp41;

// ---------------- VALUES ----------------
int co2 = -1;
int pm25 = -1;
int pm10 = 0;
int voc = 0;
int nox = 0;
float temp = 0;
float hum = 0;
float pres = 0;

// ---------------- SETTINGS ----------------
int ledBrightness = 50;
bool buzzerEnabled = true;

// ---------------- TIMERS ----------------
unsigned long lastSensor = 0;
unsigned long lastDisplay = 0;
unsigned long lastLED = 0;
unsigned long lastFirebase = 0;

// ---------------- WIFI CONNECT ----------------
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nConnected!");
}

// ---------------- FIREBASE SEND ----------------
void sendToFirebase() {
  FirebaseJson json;

  json.set("co2", co2);
  json.set("pm25", pm25);
  json.set("pm10", pm10);
  json.set("voc", voc);
  json.set("nox", nox);
  json.set("temperature", temp);
  json.set("humidity", hum);
  json.set("pressure", pres);

  if (Firebase.RTDB.setJSON(&fbdo, "/AQM/data", &json)) {
    Serial.println("Firebase updated");
  } else {
    Serial.println("Firebase error: " + fbdo.errorReason());
  }
}

// ---------------- UTILS ----------------
int getLevel(int v, int n, int m, int h) {
  if (v <= n) return 1;
  else if (v <= m) return 2;
  else if (v <= h) return 3;
  return 4;
}

uint32_t getColor(int level) {
  switch(level) {
    case 1: return leds.Color(0,255,0);
    case 2: return leds.Color(255,153,0);
    case 3: return leds.Color(255,0,0);
    case 4: return leds.Color(143,0,255);
  }
  return 0;
}

// ---------------- BUZZER ----------------
void beep(int times) {
  if (!buzzerEnabled) return;
  for(int i=0;i<times;i++) {
    tone(BUZZER_PIN, 2000, 100);
    delay(200);
  }
}

// ---------------- LED ----------------
void updateLEDs() {
  int co2L = getLevel(co2, 800,1200,2000);
  int pmL  = getLevel(pm25,5,20,50);
  int vocL = getLevel(voc,100,200,350);
  int noxL = getLevel(nox,1,20,200);

  int comb = max(vocL, noxL);

  leds.setBrightness(ledBrightness);
  leds.setPixelColor(0, getColor(co2L));
  leds.setPixelColor(1, getColor(pmL));
  leds.setPixelColor(2, getColor(comb));
  leds.show();
}

// ---------------- DISPLAY ----------------
void drawMain() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0,0);
  display.printf("CO2: %d ppm\n", co2);
  display.printf("PM2.5: %d\n", pm25);
  display.printf("VOC/NOx: %d/%d\n", voc, nox);
  display.printf("Temp: %.1f C\n", temp);
  display.printf("Hum: %.1f %%\n", hum);

  display.display();
}

// ---------------- SENSOR READ ----------------
void readSensors() {
  co2 = mhz.getCO2();

  if (pms.read(pmsData)) {
    pm25 = pmsData.PM_AE_UG_2_5;
    pm10 = pmsData.PM_AE_UG_10_0;
  }

  temp = bme.readTemperature();
  hum = bme.readHumidity();
  pres = bme.readPressure()/100.0F;

  uint16_t srawVoc = 0;
  uint16_t srawNox = 0;
  sgp41.measureRawSignals(0, 0, srawVoc, srawNox);
  voc = srawVoc;
  nox = srawNox;
}

void tokenStatusCallback(TokenInfo info) {
  Serial.printf("Token info: type = %d, status = %d\n", info.type, info.status);
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_OPTION, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  Wire.begin(21,22);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  leds.begin();

  bme.begin(0x76);

  mhzSerial.begin(9600, SERIAL_8N1, 16, 17);
  mhz.begin(mhzSerial);

  pmsSerial.begin(9600, SERIAL_8N1, 26, 27);

  sgp41.begin(Wire);

  // WIFI + FIREBASE
  connectWiFi();

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  display.println("System Ready");
  display.display();
  delay(2000);
}

// ---------------- LOOP ----------------
void loop() {
  unsigned long now = millis();

  if (now - lastSensor > 5000) {
    readSensors();
    lastSensor = now;
  }

  if (now - lastLED > 2000) {
    updateLEDs();
    lastLED = now;
  }

  if (now - lastDisplay > 2000) {
    drawMain();
    lastDisplay = now;
  }

  // 🔥 SEND TO FIREBASE every 5 sec
  if (now - lastFirebase > 5000) {
    sendToFirebase();
    lastFirebase = now;
  }

  if (digitalRead(BTN_MENU) == LOW) {
    beep(1);
    delay(300);
  }

  if (digitalRead(BTN_OPTION) == LOW) {
    buzzerEnabled = !buzzerEnabled;
    beep(2);
    delay(300);
  }
}