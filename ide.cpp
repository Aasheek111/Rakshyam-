#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Keypad.h>
#include <Servo.h>

const char* ssid = "Not Connected";  // Replace with your Wi-Fi SSID
const char* password = "password111";  // Replace with your Wi-Fi password

// Define the rows and columns of the keypad
const byte ROW_NUM    = 3;     // four rows
const byte COLUMN_NUM = 3;     // three columns
char keys[ROW_NUM][COLUMN_NUM] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
};
byte pin_rows[ROW_NUM] = {D2, D3, D4};  // Pinout for the rows
byte pin_column[COLUMN_NUM] = {D5,D6, D7};  // Pinout for the columns

Keypad keypad = Keypad(makeKeymap(keys), pin_rows, pin_column, ROW_NUM, COLUMN_NUM);

int count = 0;
const int pirPin = D0;                  // PIR sensor pin
const int servoPin = D1;                // Servo pin
const String pass = "1234";             // Password for keypad
Servo myServo;

WiFiServer server(80);

bool isDoorOpen = false;
unsigned long lastMotionTime = 0;       // Last motion detection time
const unsigned long motionCooldown = 5000; // 5 seconds cooldown

void setup() {
  Serial.begin(115200);

  // Initialize the keypad
  myServo.attach(servoPin);
  myServo.write(0);  // Initially set the servo to closed (0 degrees)
  
  // Setup Wi-Fi connection
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.println(WiFi.localIP());

  server.begin();  // Start the server to listen for requests
  
  pinMode(pirPin, INPUT);  // Initialize PIR sensor pin
}

String pressedpass = "";

void loop() {
  // Keypad check for button press
  char key = keypad.getKey();
  if (key) {
    Serial.println(key);
    
    count += 1;
    pressedpass += key;
    
    if(count == 4) {
      if(pass == pressedpass) {
        myServo.write(180);  // Open the door (servo to 180 degrees)
        Serial.println("Door opened");
      } else {
        Serial.println("Incorrect password");
      }
      count = 0;
      pressedpass = "";
    }
  }

  // Motion detection logic
  if (digitalRead(pirPin) == HIGH && millis() - lastMotionTime > motionCooldown) {
    lastMotionTime = millis();  // Update the last motion time
    Serial.println("Motion detected!");
    
    // Send HTTP request to capture image
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClient client;
      HTTPClient http;
      String serverUrl = "http://127.0.0.1:5000/captures"; // Flask server URL - update to match your Flask server's IP
      http.begin(client, serverUrl);
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

  // Handle incoming client requests for opening/closing the door (servo control)
  WiFiClient client = server.available();
  if (client) {
    String request = client.readStringUntil('\r');
    client.flush();
    
    if (request.indexOf("/open") != -1) {
      myServo.write(180);  // Open the door (servo to 180 degrees)
      isDoorOpen = true;
      client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nDoor opened");
    } else if (request.indexOf("/close") != -1) {
      myServo.write(0);   // Close the door (servo to 0 degrees)
      isDoorOpen = false;
      client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nDoor closed");
    } else {
      client.println("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nInvalid command");
    }
    delay(10);
    client.stop();  // Close the connection after responding
  }
}
