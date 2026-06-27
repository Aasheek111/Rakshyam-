#include <WiFi.h>
#include <HTTPClient.h>
#include <Keypad.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= WiFi =================
const char* ssid     = "Pikachu";
const char* password = "123456789";
bool motionPending = false;
unsigned long motionDetectedTime = 0;

// ================= Pins =================
#define BULB_RELAY   3
#define PIR_PIN      4
#define DHT_PIN      5
#define LOCK_PIN     6

// ================= OLED (SSD1306 128x64, I2C - 4 pins: VCC, GND, SDA, SCL) =================
// SDA/SCL are wired to the board's I2C bus. Override here if your board's
// default Wire pins don't match your wiring.
#define OLED_SDA     8
#define OLED_SCL     9
#define OLED_WIDTH   128
#define OLED_HEIGHT  64
#define OLED_RESET   -1   // no dedicated reset pin used
#define OLED_ADDR    0x3C // common default; some boards use 0x3D

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;

unsigned long lastOledUpdate = 0;
const unsigned long oledUpdateInterval = 1000;

// ================= DHT =================
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

// ================= Keypad =================
// 3x3 matrix keypad needs 6 microcontroller pins total: 3 ROW pins + 3 COLUMN pins.
// Physical keypad header is usually labeled R1,R2,R3,C1,C2,C3 (order can vary by
// keypad model - check the silkscreen/datasheet of your specific keypad).
const byte ROWS = 3;
const byte COLS = 3;
bool lastPirState = LOW;

char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'}
};

// Row pins: connect to keypad pins R1, R2, R3
byte rowPins[ROWS] = {10, 11, 12};   // R1=10, R2=11, R3=12

// Column pins: connect to keypad pins C1, C2, C3
byte colPins[COLS] = {13, 14, 15};   // C1=13, C2=14, C3=15

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ================= Web Server =================
WiFiServer server(80);

// ================= State Variables =================
bool bulbState  = false;
bool isDoorOpen = false;

String enteredPass = "";
const String correctPass = "1278";

unsigned long doorOpenTime = 0;
const unsigned long doorOpenDuration = 5000;

unsigned long lastKeyTime = 0;
const unsigned long keyTimeout = 8000;

unsigned long lastDHTTime = 0;
const unsigned long dhtInterval = 2000;

unsigned long lastMotionTime = 0;
const unsigned long motionCooldown = 5000;

unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 10000;

float temperature = 0.0;
float humidity = 0.0;

// ================= Helper =================
void sendResponse(WiFiClient& client, int code, const String& body)
{
  String statusLine = (code == 200) ? "200 OK" : "404 Not Found";

  client.println("HTTP/1.1 " + statusLine);
  client.println("Content-Type: text/plain");
  client.println("Connection: close");
  client.println();
  client.println(body);
}

// ================= OLED Helpers =================
void initOled()
{
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
  {
    Serial.println("[OLED] SSD1306 init failed");
    oledReady = false;
    return;
  }

  oledReady = true;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Booting...");
  display.display();
  Serial.println("[OLED] Initialized");
}

void updateOledStatus()
{
  if (!oledReady) return;
  if (millis() - lastOledUpdate < oledUpdateInterval) return;
  lastOledUpdate = millis();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.print("WiFi: ");
  display.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");

  display.print("Door: ");
  display.println(isDoorOpen ? "OPEN" : "LOCKED");

  display.print("Bulb: ");
  display.println(bulbState ? "ON" : "OFF");

  display.print("Temp: ");
  display.print(temperature, 1);
  display.println(" C");

  display.print("Hum:  ");
  display.print(humidity, 1);
  display.println(" %");

  display.display();
}

void oledMessage(const String& line1, const String& line2 = "")
{
  if (!oledReady) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  if (line2.length() > 0)
    display.println(line2);
  display.display();

  // Force the next periodic status refresh so the message isn't
  // immediately overwritten before the user can read it.
  lastOledUpdate = millis();
}

// ================= Door Control =================
void unlockDoor()
{
  if (isDoorOpen) return;

  isDoorOpen = true;
  doorOpenTime = millis();

  digitalWrite(LOCK_PIN, LOW);
  Serial.println("[DOOR] Unlocked");
  oledMessage("Door Unlocked");
}

void updateDoor()
{
  if (isDoorOpen && millis() - doorOpenTime >= doorOpenDuration)
  {
    digitalWrite(LOCK_PIN, HIGH);
    isDoorOpen = false;
    Serial.println("[DOOR] Locked");
    oledMessage("Door Locked");
  }
}

// ================= WiFi Reconnect =================
void ensureWiFi()
{
  if (millis() - lastWiFiCheck < wifiCheckInterval)
    return;

  lastWiFiCheck = millis();

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[WiFi] Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
  }
}

// ================= Setup =================
void setup()
{
  Serial.begin(115200);

  pinMode(LOCK_PIN, OUTPUT);
  digitalWrite(LOCK_PIN, HIGH);

  pinMode(BULB_RELAY, OUTPUT);
  digitalWrite(BULB_RELAY, LOW);

  pinMode(PIR_PIN, INPUT);

  dht.begin();
  initOled();

  // WiFi Connect
  Serial.print("[WiFi] Connecting");
  oledMessage("WiFi connecting...");

  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttempt < 15000)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("[WiFi] Connected IP: ");
    Serial.println(WiFi.localIP());
    oledMessage("WiFi Connected", WiFi.localIP().toString());
  }
  else
  {
    Serial.println("[WiFi] Connection failed");
    oledMessage("WiFi Failed");
  }

  server.begin();
  Serial.println("[HTTP] Server Started");
}

// ================= Loop =================
void loop()
{
  ensureWiFi();
  updateDoor();

  // ================= DHT =================
  if (millis() - lastDHTTime >= dhtInterval)
  {
    lastDHTTime = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h))
    {
      temperature = t;
      humidity = h;

      Serial.print("[DHT] Temp: ");
      Serial.print(temperature);
      Serial.print(" C  Humidity: ");
      Serial.print(humidity);
      Serial.println("%");
    }
  }

  // ================= Keypad =================
  if (enteredPass.length() > 0 &&
      millis() - lastKeyTime > keyTimeout)
  {
    enteredPass = "";
    Serial.println("[KEYPAD] Timeout Cleared");
  }

  char key = keypad.getKey();

  if (key)
  {
    Serial.print("[KEYPAD] ");
    Serial.println(key);

    lastKeyTime = millis();
    enteredPass += key;

    oledMessage("Keypad:", enteredPass);

    if (enteredPass.length() == 4)
    {
      if (enteredPass == correctPass)
      {
        Serial.println("[KEYPAD] Correct Password");
        unlockDoor();
      }
      else
      {
        Serial.println("[KEYPAD] Wrong Password");
        oledMessage("Wrong Password");
      }

      enteredPass = "";
    }
  }

  /// ================= PIR Motion =================
bool pirState = digitalRead(PIR_PIN);

if (pirState == HIGH && lastPirState == LOW)
{
    Serial.println("[PIR] Motion Detected");
    oledMessage("Motion Detected");

    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;

        http.begin("http://192.168.2.1:5001/captures");
        http.addHeader("Content-Type", "application/json");

        int httpCode = http.POST("{\"source\":\"pir\"}");

        Serial.print("[PIR] HTTP Code: ");
        Serial.println(httpCode);

        http.end();
    }
}

lastPirState = pirState;

  // ================= Web Server =================
  WiFiClient client = server.available();

  if (client)
  {
    String request = "";

    unsigned long timeout = millis();

    while (client.connected() &&
           millis() - timeout < 1000)
    {
      if (client.available())
      {
        char c = client.read();
        request += c;

        if (c == '\n')
          break;
      }
    }

    Serial.print("[HTTP] ");
    Serial.println(request);

    while (client.available())
      client.read();

    // Open Door
    if (request.indexOf("GET /open_door") >= 0)
    {
      unlockDoor();
      sendResponse(client, 200, "Door Unlocked");
    }

    // Close Door
    else if (request.indexOf("GET /close_door") >= 0)
    {
      digitalWrite(LOCK_PIN, LOW);
      isDoorOpen = false;

      sendResponse(client, 200, "Door Locked");
      oledMessage("Door Locked");
    }

    // DHT Data
    else if (request.indexOf("GET /dht") >= 0)
{
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  String json = "{\"temperature\":" + String(temp) +
                ",\"humidity\":" + String(hum) + "}";

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET");
  client.println("Access-Control-Allow-Headers: *");
  client.println("Connection: close");
  client.println();

  client.println(json);
}

    // Bulb ON
    else if (request.indexOf("GET /bulbon") >= 0)
    {
      digitalWrite(BULB_RELAY, HIGH);
      bulbState = true;

      sendResponse(client, 200, "Bulb ON");
    }

    // Bulb OFF
    else if (request.indexOf("GET /bulboff") >= 0)
    {
      digitalWrite(BULB_RELAY, LOW);
      bulbState = false;

      sendResponse(client, 200, "Bulb OFF");
    }

    // Status
    else if (request.indexOf("GET /status") >= 0)
    {
      String body = "";

      body += "Door: ";
      body += (isDoorOpen ? "OPEN" : "LOCKED");

      body += "\nBulb: ";
      body += (bulbState ? "ON" : "OFF");

      body += "\nTemperature: ";
      body += String(temperature, 1);

      body += " C";

      body += "\nHumidity: ";
      body += String(humidity, 1);

      body += " %";

      sendResponse(client, 200, body);
    }

    else
    {
      sendResponse(client, 404, "Invalid Request");
    }

    client.stop();
  }

  updateOledStatus();
}
