
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

// ---------------- PIN CONFIGURATION ----------------

#define GAS_SENSOR A0
#define RED_LED 6
#define GREEN_LED 7
#define BUZZER 8

// ESP8266 TX -> Arduino D10
// Arduino D11 -> ESP8266 RX through voltage divider
SoftwareSerial esp8266(10, 11);

// LCD address: commonly 0x27 or 0x3F
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- WIFI CONFIGURATION ----------------

// Replace with your WiFi credentials
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Replace with your ThingSpeak Write API Key
const char* API_KEY = "YOUR_WRITE_API_KEY";

// ---------------- SYSTEM SETTINGS ----------------

// Demonstration threshold only.
// Calibrate against your actual sensor and environment.
const int GAS_THRESHOLD = 400;

const unsigned long SENSOR_INTERVAL = 500;
const unsigned long CLOUD_INTERVAL = 20000;

unsigned long previousSensorTime = 0;
unsigned long previousCloudTime = 0;

int gasValue = 0;

bool leakageDetected = false;
bool wifiConnected = false;

// ---------------- ESP8266 FUNCTIONS ----------------

String readESP(unsigned long timeout) {

  String response = "";
  unsigned long startTime = millis();

  while (millis() - startTime < timeout) {

    while (esp8266.available()) {
      response += (char)esp8266.read();
    }
  }

  return response;
}

bool sendAT(String command, String expected,
            unsigned long timeout) {

  while (esp8266.available()) {
    esp8266.read();
  }

  esp8266.println(command);

  String response = readESP(timeout);

  Serial.println(response);

  return response.indexOf(expected) >= 0;
}

// ---------------- WIFI CONNECTION ----------------

bool connectWiFi() {

  Serial.println("Connecting to ESP8266...");

  if (!sendAT("AT", "OK", 2000)) {
    Serial.println("ESP8266 not responding");
    return false;
  }

  sendAT("ATE0", "OK", 1000);
  sendAT("AT+CWMODE=1", "OK", 2000);
  sendAT("AT+CIPMUX=0", "OK", 2000);

  String command = "AT+CWJAP=\"";
  command += WIFI_SSID;
  command += "\",\"";
  command += WIFI_PASSWORD;
  command += "\"";

  Serial.println("Connecting to WiFi...");

  String response = "";

  while (esp8266.available()) {
    esp8266.read();
  }

  esp8266.println(command);

  response = readESP(20000);

  Serial.println(response);

  if (response.indexOf("OK") >= 0 ||
      response.indexOf("WIFI GOT IP") >= 0) {

    Serial.println("WiFi Connected!");
    return true;
  }

  Serial.println("WiFi Connection Failed");
  return false;
}

// ---------------- GAS SENSOR ----------------

void readGasSensor() {

  int rawValue = analogRead(GAS_SENSOR);

  // Simple exponential smoothing
  if (gasValue == 0) {
    gasValue = rawValue;
  } else {
    gasValue = (gasValue * 3 + rawValue) / 4;
  }

  Serial.print("Gas Sensor Value: ");
  Serial.println(gasValue);

  leakageDetected = (gasValue >= GAS_THRESHOLD);
}

// ---------------- ALERT SYSTEM ----------------

void activateAlert() {

  if (leakageDetected) {

    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(BUZZER, HIGH);

    lcd.setCursor(0, 0);
    lcd.print("GAS WARNING!    ");

    lcd.setCursor(0, 1);
    lcd.print("STATUS: ALERT   ");

    Serial.println("!!! GAS LEAKAGE ALERT !!!");

  } else {

    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(BUZZER, LOW);

    lcd.setCursor(0, 0);
    lcd.print("GAS: ");
    lcd.print(gasValue);
    lcd.print("       ");

    lcd.setCursor(0, 1);
    lcd.print("STATUS: SAFE    ");
  }
}

// ---------------- CLOUD DATA UPLOAD ----------------

bool uploadToThingSpeak() {

  if (!wifiConnected) {
    return false;
  }

  Serial.println("Uploading data to ThingSpeak...");

  // Create HTTP GET request
  String request = "GET /update?api_key=";
  request += API_KEY;
  request += "&field1=";
  request += String(gasValue);

  request += "&field2=";
  request += String(map(gasValue, 0, 1023, 0, 100));

  request += "&field3=";
  request += String(leakageDetected ? 1 : 0);

  request += " HTTP/1.1\r\n";
  request += "Host: api.thingspeak.com\r\n";
  request += "Connection: close\r\n\r\n";

  // Open TCP connection
  String response = "";

  while (esp8266.available()) {
    esp8266.read();
  }

  esp8266.println(
    "AT+CIPSTART=\"TCP\",\"api.thingspeak.com\",80"
  );

  response = readESP(8000);

  if (response.indexOf("CONNECT") < 0 &&
      response.indexOf("ALREADY CONNECTED") < 0) {

    Serial.println("Cloud connection failed");
    return false;
  }

  // Send HTTP request length
  esp8266.print("AT+CIPSEND=");
  esp8266.println(request.length());

  response = readESP(4000);

  if (response.indexOf(">") < 0) {
    Serial.println("Data transmission failed");
    sendAT("AT+CIPCLOSE", "OK", 2000);
    return false;
  }

  // Send HTTP request
  esp8266.print(request);

  response = readESP(8000);

  Serial.println("ThingSpeak Response:");
  Serial.println(response);

  sendAT("AT+CIPCLOSE", "OK", 2000);

  if (response.indexOf("SEND OK") >= 0) {
    Serial.println("Cloud upload request sent");
    return true;
  }

  return false;
}

// ---------------- SETUP ----------------

void setup() {

  Serial.begin(9600);
  esp8266.begin(9600);

  pinMode(GAS_SENSOR, INPUT);

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(BUZZER, LOW);

  // Initialize LCD
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("SMART GAS");
  lcd.setCursor(0, 1);
  lcd.print("MONITOR SYSTEM");

  Serial.println("Smart Gas Leakage System");
  Serial.println("Initializing...");

  delay(2000);

  lcd.clear();
  lcd.print("Sensor Warmup");

  // Demonstration startup delay only.
  // Follow the MQ-6 datasheet for proper conditioning.
  delay(5000);

  lcd.clear();

  // Connect ESP8266 to WiFi
  wifiConnected = connectWiFi();

  if (wifiConnected) {
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("WiFi Offline");
  }

  delay(1500);
  lcd.clear();
}

// ---------------- MAIN LOOP ----------------

void loop() {

  unsigned long currentTime = millis();

  // Read gas sensor periodically
  if (currentTime - previousSensorTime >=
      SENSOR_INTERVAL) {

    previousSensorTime = currentTime;

    readGasSensor();

    activateAlert();
  }

  // Upload data to ThingSpeak periodically
  if (currentTime - previousCloudTime >=
      CLOUD_INTERVAL) {

    previousCloudTime = currentTime;

    if (!wifiConnected) {

      wifiConnected = connectWiFi();

    }

    if (wifiConnected) {

      if (!uploadToThingSpeak()) {
        wifiConnected = false;
      }
    }
  }
}
