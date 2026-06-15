#include <WiFi.h>
#include <HTTPClient.h>
#include <Keypad.h>
#include <Servo.h>
#include <DHT.h>

// ───────────────── WiFi ─────────────────
const char* ssid = "Connecting";
const char* password = "netkopassword";

// ───────────────── Pins (ESP32-S3 GPIO) ─────────────────
#define PIR_PIN 4
#define DHT_PIN 5
#define SERVO_PIN 6

#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

// ───────────────── Keypad (ESP32-S3 safe GPIOs) ─────────────────
const byte ROW_NUM = 3;
const byte COLUMN_NUM = 3;

char keys[ROW_NUM][COLUMN_NUM] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'}
};

// Avoid strapping pins on ESP32-S3
byte pin_rows[ROW_NUM]      = {10, 11, 12};
byte pin_column[COLUMN_NUM] = {13, 14, 15};

Keypad keypad = Keypad(makeKeymap(keys), pin_rows, pin_column, ROW_NUM, COLUMN_NUM);

// ───────────────── Servo ─────────────────
Servo myServo;

// ───────────────── Server ─────────────────
WiFiServer server(80);

// ───────────────── System Variables ─────────────────
bool isDoorOpen = false;
int count = 0;
String pressedpass = "";
const String pass = "1234";

// ───────────────── Timing ─────────────────
unsigned long lastMotionTime = 0;
const unsigned long motionCooldown = 5000;

unsigned long lastDHTTime = 0;
const unsigned long dhtInterval = 2000;

float temperature = 0.0;
float humidity = 0.0;

void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  dht.begin();

  // Servo setup (ESP32 uses LEDC internally)
  myServo.attach(SERVO_PIN);
  myServo.write(0);

  // WiFi connect
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  Serial.println("Connected!");
  Serial.println(WiFi.localIP());

  server.begin();
}

void loop() {

  // ── 1. DHT SENSOR ─────────────────────────────
  if (millis() - lastDHTTime > dhtInterval) {
    lastDHTTime = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      temperature = t;
      humidity = h;

      Serial.print("Temp: ");
      Serial.print(temperature);
      Serial.print(" °C | Humidity: ");
      Serial.println(humidity);
    } else {
      Serial.println("DHT read failed");
    }
  }

  // ── 2. KEYPAD INPUT ───────────────────────────
  char key = keypad.getKey();
  if (key) {
    Serial.println(key);
    count++;
    pressedpass += key;

    if (count == 4) {
      if (pressedpass == pass) {
        myServo.write(180);
        isDoorOpen = true;
        Serial.println("Door Opened");
      } else {
        Serial.println("Wrong Password");
      }

      count = 0;
      pressedpass = "";
    }
  }

  // ── 3. PIR MOTION + HTTP REQUEST ─────────────
  if (digitalRead(PIR_PIN) == HIGH &&
      millis() - lastMotionTime > motionCooldown) {

    lastMotionTime = millis();
    Serial.println("Motion detected!");

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;

      // IMPORTANT: ESP32 cannot use 127.0.0.1
      String serverUrl = "http://YOUR_PC_IP:5000/captures";

      http.begin(serverUrl);
      int code = http.GET();

      if (code > 0) {
        Serial.println("Trigger success");
      } else {
        Serial.print("HTTP failed: ");
        Serial.println(code);
      }

      http.end();
    }
  }

  // ── 4. SIMPLE HTTP SERVER ─────────────────────
  WiFiClient client = server.available();

  if (client) {
    String request = client.readStringUntil('\r');
    client.flush();

    if (request.indexOf("/open") != -1) {
      myServo.write(180);
      isDoorOpen = true;
      client.println("HTTP/1.1 200 OK\r\n\r\nDoor opened");

    } else if (request.indexOf("/close") != -1) {
      myServo.write(0);
      isDoorOpen = false;
      client.println("HTTP/1.1 200 OK\r\n\r\nDoor closed");

    } else if (request.indexOf("/dht") != -1) {
      String body = "Temp: " + String(temperature) +
                    " C, Humidity: " + String(humidity);

      client.println("HTTP/1.1 200 OK\r\n\r\n" + body);

    } else {
      client.println("HTTP/1.1 404 OK\r\n\r\nInvalid");
    }

    delay(10);
    client.stop();
  }
}