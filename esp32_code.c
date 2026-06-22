#include <WiFi.h>
#include <HTTPClient.h>
#include <Keypad.h>
#include <DHT.h>
#include <Adafruit_Fingerprint.h>

// ───────────────── WiFi ─────────────────
const char* ssid = "Connecting";
const char* password = "netkopassword";

// ───────────────── Pins ─────────────────
#define PIR_PIN 4
#define DHT_PIN 5
#define LOCK_PIN 6

#define FINGER_RX 16
#define FINGER_TX 17

// ───────────────── DHT ─────────────────
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

// ───────────────── Keypad ─────────────────
const byte ROW_NUM = 3;
const byte COLUMN_NUM = 3;

char keys[ROW_NUM][COLUMN_NUM] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'}
};

byte pin_rows[ROW_NUM] = {10,11,12};
byte pin_cols[COLUMN_NUM] = {13,14,15};

Keypad keypad = Keypad(
  makeKeymap(keys),
  pin_rows,
  pin_cols,
  ROW_NUM,
  COLUMN_NUM
);

// ───────────────── Fingerprint ─────────────────
HardwareSerial FingerSerial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FingerSerial);

// ───────────────── Server ─────────────────
WiFiServer server(80);

// ───────────────── Variables ─────────────────
bool isDoorOpen = false;

String enteredPass = "";
const String correctPass = "1234";

unsigned long lastMotionTime = 0;
const unsigned long motionCooldown = 5000;

unsigned long lastDHTTime = 0;
const unsigned long dhtInterval = 2000;

float temperature = 0;
float humidity = 0;

// ───────────────── Functions ─────────────────

void openDoor() {

  digitalWrite(LOCK_PIN, HIGH);

  isDoorOpen = true;

  Serial.println("Door Unlocked");

  delay(5000);

  digitalWrite(LOCK_PIN, LOW);

  isDoorOpen = false;

  Serial.println("Door Locked");
}

int getFingerprintID() {

  uint8_t p = finger.getImage();

  if (p != FINGERPRINT_OK)
    return -1;

  p = finger.image2Tz();

  if (p != FINGERPRINT_OK)
    return -1;

  p = finger.fingerFastSearch();

  if (p != FINGERPRINT_OK)
    return -1;

  return finger.fingerID;
}

// ───────────────── Setup ─────────────────

void setup() {

  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);

  pinMode(LOCK_PIN, OUTPUT);
  digitalWrite(LOCK_PIN, LOW);

  dht.begin();

  FingerSerial.begin(
    57600,
    SERIAL_8N1,
    FINGER_RX,
    FINGER_TX
  );

  finger.begin(57600);

  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor found!");
  } else {
    Serial.println("Fingerprint sensor NOT found!");
  }

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting...");
  }

  Serial.println("WiFi Connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  server.begin();
}

// ───────────────── Loop ─────────────────

void loop() {

  if (millis() - lastDHTTime > dhtInterval) {

    lastDHTTime = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {

      temperature = t;
      humidity = h;

      Serial.print("Temp: ");
      Serial.print(temperature);
      Serial.print(" °C  Humidity: ");
      Serial.println(humidity);
    }
  }

  char key = keypad.getKey();

  if (key) {

    Serial.println(key);

    enteredPass += key;

    if (enteredPass.length() == 4) {

      if (enteredPass == correctPass) {

        Serial.println("Correct Password");
        openDoor();

      } else {

        Serial.println("Wrong Password");
      }

      enteredPass = "";
    }
  }

  int id = getFingerprintID();

  if (id >= 0) {

    Serial.print("Fingerprint Match ID: ");
    Serial.println(id);

    openDoor();

    delay(1000);
  }

  if (
      digitalRead(PIR_PIN) == HIGH &&
      millis() - lastMotionTime > motionCooldown
     ) {

    lastMotionTime = millis();

    Serial.println("Motion Detected");

    if (WiFi.status() == WL_CONNECTED) {

      HTTPClient http;

      String url =
      "http://YOUR_PC_IP:5000/captures";

      http.begin(url);

      int code = http.GET();

      Serial.print("HTTP Code: ");
      Serial.println(code);

      http.end();
    }
  }

  WiFiClient client = server.available();

  if (client) {

    String request =
    client.readStringUntil('\r');

    client.flush();

    if (request.indexOf("/open") != -1) {

      openDoor();

      client.println(
        "HTTP/1.1 200 OK\r\n\r\nDoor Unlocked"
      );
    }

    else if (request.indexOf("/close") != -1) {

      digitalWrite(LOCK_PIN, LOW);

      client.println(
        "HTTP/1.1 200 OK\r\n\r\nDoor Locked"
      );
    }

    else if (request.indexOf("/dht") != -1) {

      String body =
      "Temp: " + String(temperature) +
      " C Humidity: " + String(humidity);

      client.println(
        "HTTP/1.1 200 OK\r\n\r\n" + body
      );
    }

    else {

      client.println(
        "HTTP/1.1 404 Not Found\r\n\r\nInvalid Request"
      );
    }

    client.stop();
  }
}