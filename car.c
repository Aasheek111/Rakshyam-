#include <WiFi.h>
#include <WebServer.h>

// ================= WiFi AP =================
const char* ssid = "ESP32_RC_CAR";
const char* password = "12345678";

WebServer server(80);

// ================= L298N Pins =================
#define IN1 4
#define IN2 5
#define IN3 6
#define IN4 7
#define ENA 8
#define ENB 9

// ================= Settings =================
int speedValue = 220;

const int freq = 1000;
const int resolution = 8;

// ================= Motor Functions =================
void stopCar() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void moveForward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void moveBackward() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void turnLeft() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void turnRight() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

// ================= Web Page =================
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">

<title>ESP32 RC Car</title>

<style>

*{
margin:0;
padding:0;
box-sizing:border-box;
font-family:Arial,sans-serif;
}

body{
background:linear-gradient(135deg,#0f172a,#1e293b);
color:white;
min-height:100vh;
display:flex;
justify-content:center;
align-items:center;
padding:20px;
}

.card{
width:100%;
max-width:450px;
background:#111827;
padding:25px;
border-radius:25px;
box-shadow:0 0 25px rgba(0,255,255,.15);
text-align:center;
}

h1{
color:#38bdf8;
margin-bottom:20px;
}

.speed-box{
margin-bottom:20px;
}

.speed{
font-size:22px;
color:#22d3ee;
margin-bottom:10px;
}

.slider{
width:100%;
}

.pad{
display:flex;
flex-direction:column;
align-items:center;
gap:12px;
margin-top:20px;
}

.row{
display:flex;
gap:12px;
}

.btn{
width:100px;
height:100px;
border:none;
border-radius:20px;
font-size:40px;
font-weight:bold;
cursor:pointer;
transition:0.15s;
box-shadow:0 5px 15px rgba(0,0,0,.4);
}

.btn:active{
transform:scale(0.95);
}

.forward{
background:#22c55e;
color:white;
}

.backward{
background:#ef4444;
color:white;
}

.left{
background:#3b82f6;
color:white;
}

.right{
background:#3b82f6;
color:white;
}

.stop{
background:#f59e0b;
color:black;
}

.info{
margin-top:20px;
font-size:14px;
color:#94a3b8;
}

</style>
</head>

<body>

<div class="card">

<h1>🚗 ESP32 RC CAR</h1>

<div class="speed-box">
<div class="speed">
Speed: <span id="speedValue">220</span>
</div>

<input
type="range"
min="0"
max="255"
value="220"
class="slider"
id="speedSlider">
</div>

<div class="pad">

<button class="btn forward"
onmousedown="sendCmd('forward')"
onmouseup="sendCmd('stop')"
onmouseleave="sendCmd('stop')"
ontouchstart="sendCmd('forward')"
ontouchend="sendCmd('stop')">
▲
</button>

<div class="row">

<button class="btn left"
onmousedown="sendCmd('left')"
onmouseup="sendCmd('stop')"
onmouseleave="sendCmd('stop')"
ontouchstart="sendCmd('left')"
ontouchend="sendCmd('stop')">
◀
</button>

<button class="btn stop"
onclick="sendCmd('stop')">
■
</button>

<button class="btn right"
onmousedown="sendCmd('right')"
onmouseup="sendCmd('stop')"
onmouseleave="sendCmd('stop')"
ontouchstart="sendCmd('right')"
ontouchend="sendCmd('stop')">
▶
</button>

</div>

<button class="btn backward"
onmousedown="sendCmd('backward')"
onmouseup="sendCmd('stop')"
onmouseleave="sendCmd('stop')"
ontouchstart="sendCmd('backward')"
ontouchend="sendCmd('stop')">
▼
</button>

</div>

<div class="info">
ESP32-S3 WiFi RC Car Controller
</div>

</div>

<script>

function sendCmd(cmd){
fetch('/' + cmd);
}

const slider = document.getElementById("speedSlider");
const speedText = document.getElementById("speedValue");

slider.addEventListener("input", function(){

speedText.innerHTML = this.value;

fetch('/speed?value=' + this.value);

});

</script>

</body>
</html>
)rawliteral";

// ================= Setup =================
void setup() {

  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // ESP32-S3 PWM
  ledcAttach(ENA, freq, resolution);
  ledcAttach(ENB, freq, resolution);

  ledcWrite(ENA, speedValue);
  ledcWrite(ENB, speedValue);

  stopCar();

  // Start Access Point
  WiFi.softAP(ssid, password);

  Serial.println();
  Serial.println("================================");
  Serial.println("ESP32 RC CAR READY");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("================================");

  // Home Page
  server.on("/", []() {
    server.send(200, "text/html", webpage);
  });

  // Forward
  server.on("/forward", []() {
    moveForward();
    server.send(200, "text/plain", "OK");
  });

  // Backward
  server.on("/backward", []() {
    moveBackward();
    server.send(200, "text/plain", "OK");
  });

  // Left
  server.on("/left", []() {
    turnLeft();
    server.send(200, "text/plain", "OK");
  });

  // Right
  server.on("/right", []() {
    turnRight();
    server.send(200, "text/plain", "OK");
  });

  // Stop
  server.on("/stop", []() {
    stopCar();
    server.send(200, "text/plain", "OK");
  });

  // Speed Control
  server.on("/speed", []() {

    if (server.hasArg("value")) {

      speedValue = server.arg("value").toInt();

      speedValue = constrain(speedValue, 0, 255);

      ledcWrite(ENA, speedValue);
      ledcWrite(ENB, speedValue);
    }

    server.send(200, "text/plain", "OK");
  });

  server.begin();

  Serial.println("Web Server Started");
}

// ================= Loop =================
void loop() {
  server.handleClient();
}