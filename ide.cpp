#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Keypad.h>
#include <Servo.h>
#include <DHT.h>

const char* ssid = "Connecting";
const char* password = "netkopassword";

#define PIR_PIN D0
#define DHT_PIN D3
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

const byte ROW_NUM    = 4;
const byte COLUMN_NUM = 3;
char keys[ROW_NUM][COLUMN_NUM] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'}
};
byte pin_rows[ROW_NUM]      = {D3, D4, D5};
byte pin_column[COLUMN_NUM] = {D6, D7, D8};

Keypad keypad = Keypad(makeKeymap(keys), pin_rows, pin_column, ROW_NUM, COLUMN_NUM);

int count = 0;
const int servoPin  = D1;
const String pass   = "1234";
Servo myServo;

WiFiServer server(80);

bool isDoorOpen = false;

unsigned long lastMotionTime = 0;
const unsigned long motionCooldown = 5000;

unsigned long lastDHTTime = 0;
const unsigned long dhtInterval = 2000;   // Read DHT every 2 seconds

float temperature = 0.0;
float humidity    = 0.0;

String pressedpass = "";

void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  dht.begin();

  myServo.attach(servoPin);
  myServo.write(0);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.println(WiFi.localIP());

  server.begin();
}

void loop() {

  // ── 1. DHT READ (non-blocking, every 2 seconds) ──────────────────────────
  if (millis() - lastDHTTime > dhtInterval) {
    lastDHTTime = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      Serial.println("Failed to read from DHT sensor!");
    } else {
      temperature = t;
      humidity    = h;
      Serial.print("Temperature: "); Serial.print(temperature); Serial.print(" °C");
      Serial.print(" | Humidity: ");  Serial.print(humidity);    Serial.println(" %");
    }
  }

  // ── 2. KEYPAD (always active, NOT inside PIR block) ──────────────────────
  char key = keypad.getKey();
  if (key) {
    Serial.println(key);
    count++;
    pressedpass += key;

    if (count == 4) {
      if (pass == pressedpass) {
        myServo.write(180);
        isDoorOpen = true;
        Serial.println("Door opened via keypad");
      } else {
        Serial.println("Incorrect password");
      }
      count = 0;
      pressedpass = "";
    }
  }

  // ── 3. PIR MOTION DETECTION ───────────────────────────────────────────────
  if (digitalRead(PIR_PIN) == HIGH && millis() - lastMotionTime > motionCooldown) {
    lastMotionTime = millis();
    Serial.println("Motion detected!");

    if (WiFi.status() == WL_CONNECTED) {
      WiFiClient httpClient;
      HTTPClient http;
      String serverUrl = "http://127.0.0.1:5000/captures";
      http.begin(httpClient, serverUrl);
      int httpResponseCode = http.GET();
      if (httpResponseCode == 200) {
        Serial.println("Image capture triggered successfully.");
      } else {
        Serial.print("Failed to trigger image capture. HTTP Response Code: ");
        Serial.println(httpResponseCode);
      }
      http.end();
    } else {
      Serial.println("Wi-Fi not connected");
    }
  }

  // ── 4. HTTP SERVER (open / close / dht) ──────────────────────────────────
  WiFiClient client = server.available();
  if (client) {
    String request = client.readStringUntil('\r');
    client.flush();

    if (request.indexOf("/open") != -1) {
      myServo.write(180);
      isDoorOpen = true;
      Serial.println("Open request received");
      client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nDoor opened");

    } else if (request.indexOf("/close") != -1) {
      myServo.write(0);
      isDoorOpen = false;
      Serial.println("Close request received");
      client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nDoor closed");

    } else if (request.indexOf("/dht") != -1) {
      String body = "Temperature: " + String(temperature, 1) + " C, Humidity: " + String(humidity, 1) + " %";
      client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n" + body);
      Serial.println("DHT data sent: " + body);

    } else {
      client.println("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nInvalid command");
    }

    delay(10);
    client.stop();
  }
}