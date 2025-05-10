// Blynk Configuration
#define BLYNK_TEMPLATE_ID "TMPL3DnvLTlTA"
#define BLYNK_TEMPLATE_NAME "BIN1"
#define BLYNK_AUTH_TOKEN "vdNX9wTPo2Zjn_fY2ljeW7LBZrCARKbh"

// WiFi Credentials
#define WIFI_SSID "Vamsi.."
#define WIFI_PASSWORD "1234567890"

// Firebase Configuration
#define FIREBASE_HOST "https://smart-bin-management-system-default-rtdb.firebaseio.com/"
#define FIREBASE_AUTH "your-database-secret"

#include <Arduino.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <FirebaseESP32.h>
#include <time.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Hardware Configuration
const int trigPin1 = 25;         // First ultrasonic sensor
const int echoPin1 = 26;
const int trigPin2 = 32;         // Second ultrasonic sensor
const int echoPin2 = 33;
const int odorSensorAOPin = 34;  // Analog input
const int servoPin = 13;         // Servo control

// LCD Configuration (Confirm address with I2C scanner)
#define LCD_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// NTP Configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;  // IST: UTC+5:30

// Servo Control Parameters
const int SERVO_OPEN_ANGLE = 100;
const int SERVO_CLOSE_ANGLE = 5;
const int SERVO_STEP = 1;
const int SERVO_DELAY = 50;

// Global Variables
Servo myServo;
unsigned long previousMillis = 0;
const long interval = 1000;
bool binFullNotificationSent = false;
bool motionNotificationSent = false;
bool odorNotificationSent = false;

// Firebase Objects
FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

String binID = "BIN1";

void initializeSensors() {
  pinMode(trigPin1, OUTPUT);
  pinMode(echoPin1, INPUT);
  pinMode(trigPin2, OUTPUT);
  pinMode(echoPin2, INPUT);
  pinMode(odorSensorAOPin, INPUT);
}

float measureDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  return pulseIn(echoPin, HIGH) * 0.034 / 2;
}

int calculateBinLevel(float distance) {
  const float maxDist = 12.0;
  const float minDist = 2.0;
  distance = constrain(distance, minDist, maxDist);
  return 100 - ((distance - minDist) / (maxDist - minDist)) * 100;
}

void updateLCD(float distance) {
  lcd.setCursor(0, 0);
  if (distance < 3) {
    lcd.print("Bin FULL!       ");
  } else {
    lcd.print("Level: ");
    lcd.print(calculateBinLevel(distance));
    lcd.print("%      ");
  }
}

void moveServoSlowly(int targetAngle) {
  static int currentAngle = SERVO_CLOSE_ANGLE;
  
  if (currentAngle < targetAngle) {
    for (; currentAngle <= targetAngle; currentAngle += SERVO_STEP) {
      myServo.write(currentAngle);
      delay(SERVO_DELAY);
    }
  } else {
    for (; currentAngle >= targetAngle; currentAngle -= SERVO_STEP) {
      myServo.write(currentAngle);
      delay(SERVO_DELAY);
    }
  }
}

void checkMotionDetection(float distance) {
  if (distance < 29) {
    if (!motionNotificationSent) {
      moveServoSlowly(SERVO_OPEN_ANGLE);
      Blynk.logEvent("motion_detected", "Motion near bin!");
      motionNotificationSent = true;
    }
  } else {
    if (motionNotificationSent) {
      moveServoSlowly(SERVO_CLOSE_ANGLE);
      motionNotificationSent = false;
    }
  }
}

void sendToFirebase(float d1, float d2, int odor) {
  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);
  
  char timestamp[20];
  strftime(timestamp, 20, "%Y-%m-%d_%H-%M-%S", ti);
  
  String path = "/" + binID + "/readings/" + timestamp;
  
  FirebaseJson json;
  json.set("bin_level", calculateBinLevel(d1));
  json.set("motion_distance", d2);
  json.set("odor_value", odor);
  json.set("timestamp", timestamp);

  Firebase.setJSON(firebaseData, path, json);
}

void setup() {
  Serial.begin(115200);
  initializeSensors();

  // Initialize LCD
  Wire.begin(21, 22);  // Default ESP32 I2C pins
  lcd.init();
  lcd.backlight();
  lcd.print("Initializing...");

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  lcd.clear();
  lcd.print("WiFi Connected!");

  // Initialize services
  Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASSWORD);
  configTime(gmtOffset_sec, 0, ntpServer);
  while (!time(nullptr)) delay(1000);

  // Firebase setup
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);

  // Servo setup
  myServo.attach(servoPin);
  myServo.write(SERVO_CLOSE_ANGLE);

  delay(2000);
  lcd.clear();
}

void loop() {
  Blynk.run();

  float d1 = measureDistance(trigPin1, echoPin1);
  float d2 = measureDistance(trigPin2, echoPin2);
  int odor = analogRead(odorSensorAOPin);

  updateLCD(d1);
  
  // Blynk updates
  Blynk.virtualWrite(V0, calculateBinLevel(d1));
  Blynk.virtualWrite(V1, d2);
  Blynk.virtualWrite(V2, odor);
  Blynk.virtualWrite(V3, myServo.read());

  // Notifications
  if (d1 < 2 && !binFullNotificationSent) {
    Blynk.logEvent("bin_full", "Bin needs emptying!");
    binFullNotificationSent = true;
  } else if (d1 >= 2) binFullNotificationSent = false;

  if (odor > 1500 && !odorNotificationSent) {
    Blynk.logEvent("bad_odor", "Foul smell detected!");
    odorNotificationSent = true;
  } else if (odor <= 1500) odorNotificationSent = false;

  checkMotionDetection(d2);

  // Firebase updates
  if (millis() - previousMillis >= interval) {
    previousMillis = millis();
    sendToFirebase(d1, d2, odor);
  }
  
  delay(500);
}