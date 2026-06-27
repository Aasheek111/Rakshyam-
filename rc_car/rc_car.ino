#include <WiFi.h>
#include <WebServer.h>

// ================= WiFi AP =================
const char* ssid     = "ESP32_RC_CAR";
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
const int freq       = 1000;
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
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>RC Car</title>
<style>
  /* ── reset & base ── */
  *, *::before, *::after {
    margin: 0; padding: 0;
    box-sizing: border-box;
    -webkit-tap-highlight-color: transparent;
    /* FIX: kill the long-press copy menu everywhere */
    -webkit-touch-callout: none;
    -webkit-user-select: none;
    user-select: none;
  }

  :root {
    --bg:       #0a0a0f;
    --surface:  #13131a;
    --panel:    #1a1a24;
    --border:   #2a2a3a;
    --accent:   #7c6af7;
    --accent2:  #a78bfa;
    --fwd:      #22c55e;
    --bwd:      #ef4444;
    --lr:       #7c6af7;
    --stop:     #f59e0b;
    --text:     #e2e8f0;
    --muted:    #64748b;
    --glow:     rgba(124,106,247,0.25);
  }

  html, body {
    height: 100%;
    background: var(--bg);
    color: var(--text);
    font-family: 'Segoe UI', system-ui, sans-serif;
    display: flex;
    align-items: center;
    justify-content: center;
    overflow: hidden;
  }

  /* ── card ── */
  .card {
    width: 100%;
    max-width: 380px;
    padding: 24px 20px 28px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 28px;
    display: flex;
    flex-direction: column;
    gap: 22px;
  }

  /* ── header ── */
  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .logo {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .logo-icon {
    width: 36px; height: 36px;
    background: linear-gradient(135deg, var(--accent), var(--accent2));
    border-radius: 10px;
    display: flex; align-items: center; justify-content: center;
    font-size: 18px;
  }

  .logo-text {
    font-size: 15px;
    font-weight: 600;
    letter-spacing: 0.04em;
    color: var(--text);
  }

  .logo-sub {
    font-size: 11px;
    color: var(--muted);
    letter-spacing: 0.06em;
    text-transform: uppercase;
  }

  /* status dot */
  .status {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 12px;
    color: var(--muted);
  }

  .dot {
    width: 7px; height: 7px;
    border-radius: 50%;
    background: var(--fwd);
    box-shadow: 0 0 6px var(--fwd);
    animation: pulse 2s ease-in-out infinite;
  }

  @keyframes pulse {
    0%,100% { opacity: 1; }
    50%      { opacity: 0.4; }
  }

  /* ── speed panel ── */
  .speed-panel {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 16px;
    padding: 14px 16px;
  }

  .speed-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 10px;
  }

  .speed-label {
    font-size: 12px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.08em;
  }

  .speed-val {
    font-size: 22px;
    font-weight: 700;
    color: var(--accent2);
    font-variant-numeric: tabular-nums;
    min-width: 40px;
    text-align: right;
  }

  /* custom slider */
  input[type=range] {
    -webkit-appearance: none;
    appearance: none;
    width: 100%;
    height: 6px;
    border-radius: 3px;
    background: var(--border);
    outline: none;
    cursor: pointer;
  }

  input[type=range]::-webkit-slider-thumb {
    -webkit-appearance: none;
    appearance: none;
    width: 20px; height: 20px;
    border-radius: 50%;
    background: var(--accent2);
    box-shadow: 0 0 10px var(--glow);
    cursor: pointer;
    transition: transform 0.1s;
  }

  input[type=range]:active::-webkit-slider-thumb {
    transform: scale(1.2);
  }

  /* ── direction pad ── */
  .pad {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    grid-template-rows: repeat(3, 1fr);
    gap: 10px;
    aspect-ratio: 1;
  }

  /* grid positions */
  .btn-fwd  { grid-column: 2; grid-row: 1; }
  .btn-left { grid-column: 1; grid-row: 2; }
  .btn-stop { grid-column: 2; grid-row: 2; }
  .btn-rgt  { grid-column: 3; grid-row: 2; }
  .btn-bwd  { grid-column: 2; grid-row: 3; }

  .btn {
    border: none;
    border-radius: 18px;
    font-size: 26px;
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: center;
    transition: transform 0.08s, filter 0.08s;
    position: relative;
    overflow: hidden;
    touch-action: none;   /* FIX: prevents browser hijacking touch */
  }

  /* subtle inner shine */
  .btn::after {
    content: '';
    position: absolute;
    inset: 0;
    border-radius: inherit;
    background: linear-gradient(160deg, rgba(255,255,255,0.08) 0%, transparent 60%);
    pointer-events: none;
  }

  .btn:active, .btn.pressed {
    transform: scale(0.91);
    filter: brightness(0.85);
  }

  .btn-fwd  { background: #166534; color: #4ade80; border: 1px solid #15803d; }
  .btn-bwd  { background: #7f1d1d; color: #f87171; border: 1px solid #991b1b; }
  .btn-left { background: #1e1b4b; color: #a78bfa; border: 1px solid #3730a3; }
  .btn-rgt  { background: #1e1b4b; color: #a78bfa; border: 1px solid #3730a3; }
  .btn-stop { background: #451a03; color: #fbbf24; border: 1px solid #92400e; font-size: 20px; }

  /* ── state bar ── */
  .state-bar {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 8px;
    padding: 10px 16px;
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 12px;
    font-size: 13px;
    color: var(--muted);
    letter-spacing: 0.04em;
    min-height: 42px;
    transition: color 0.2s;
  }

  .state-bar span {
    font-weight: 600;
    color: var(--text);
    transition: color 0.2s;
  }

  /* ── footer ── */
  .footer {
    text-align: center;
    font-size: 11px;
    color: var(--muted);
    letter-spacing: 0.05em;
  }
</style>
</head>
<body>

<div class="card">

  <!-- header -->
  <div class="header">
    <div class="logo">
      <div class="logo-icon">🚗</div>
      <div>
        <div class="logo-text">RC Car</div>
        <div class="logo-sub">ESP32-S3</div>
      </div>
    </div>
    <div class="status">
      <div class="dot"></div>
      Connected
    </div>
  </div>

  <!-- speed -->
  <div class="speed-panel">
    <div class="speed-row">
      <div class="speed-label">Speed</div>
      <div class="speed-val" id="speedVal">220</div>
    </div>
    <input type="range" min="0" max="255" value="220" id="speedSlider">
  </div>

  <!-- d-pad -->
  <div class="pad">

    <button class="btn btn-fwd"
      id="btn-forward"
      ontouchstart="press(event,'forward')"
      ontouchend="release(event)"
      onmousedown="press(event,'forward')"
      onmouseup="release(event)"
      onmouseleave="release(event)">▲</button>

    <button class="btn btn-left"
      id="btn-left"
      ontouchstart="press(event,'left')"
      ontouchend="release(event)"
      onmousedown="press(event,'left')"
      onmouseup="release(event)"
      onmouseleave="release(event)">◀</button>

    <button class="btn btn-stop"
      id="btn-stop"
      ontouchstart="press(event,'stop')"
      ontouchend="stopOnly(event)"
      onmousedown="press(event,'stop')"
      onmouseup="stopOnly(event)">■</button>

    <button class="btn btn-rgt"
      id="btn-right"
      ontouchstart="press(event,'right')"
      ontouchend="release(event)"
      onmousedown="press(event,'right')"
      onmouseup="release(event)"
      onmouseleave="release(event)">▶</button>

    <button class="btn btn-bwd"
      id="btn-backward"
      ontouchstart="press(event,'backward')"
      ontouchend="release(event)"
      onmousedown="press(event,'backward')"
      onmouseup="release(event)"
      onmouseleave="release(event)">▼</button>

  </div>

  <!-- state -->
  <div class="state-bar" id="stateBar">
    State: <span id="stateText">Stopped</span>
  </div>

  <div class="footer">ESP32_RC_CAR · 192.168.4.1</div>

</div>

<script>
  // ── FIX: block long-press context menu on all buttons ──
  document.querySelectorAll('.btn').forEach(btn => {
    btn.addEventListener('contextmenu', e => e.preventDefault());
    btn.addEventListener('touchstart',  e => e.preventDefault(), { passive: false });
  });

  let currentCmd = 'stop';

  const stateLabels = {
    forward:  'Forward',
    backward: 'Backward',
    left:     'Turning left',
    right:    'Turning right',
    stop:     'Stopped'
  };

  const stateColors = {
    forward:  '#4ade80',
    backward: '#f87171',
    left:     '#a78bfa',
    right:    '#a78bfa',
    stop:     '#fbbf24'
  };

  function sendCmd(cmd) {
    fetch('/' + cmd).catch(() => {});
  }

  function setStateUI(cmd) {
    const t = document.getElementById('stateText');
    t.textContent = stateLabels[cmd] || cmd;
    t.style.color = stateColors[cmd] || '#e2e8f0';
  }

  function press(e, cmd) {
    e.preventDefault();
    currentCmd = cmd;
    sendCmd(cmd);
    setStateUI(cmd);
    // visual pressed state
    document.querySelectorAll('.btn').forEach(b => b.classList.remove('pressed'));
    const idMap = {
      forward: 'btn-forward', backward: 'btn-backward',
      left: 'btn-left', right: 'btn-right', stop: 'btn-stop'
    };
    const el = document.getElementById(idMap[cmd]);
    if (el) el.classList.add('pressed');
  }

  function release(e) {
    e.preventDefault();
    if (currentCmd === 'stop') return;
    currentCmd = 'stop';
    sendCmd('stop');
    setStateUI('stop');
    document.querySelectorAll('.btn').forEach(b => b.classList.remove('pressed'));
  }

  function stopOnly(e) {
    e.preventDefault();
    currentCmd = 'stop';
    sendCmd('stop');
    setStateUI('stop');
    document.querySelectorAll('.btn').forEach(b => b.classList.remove('pressed'));
  }

  // speed slider
  const slider   = document.getElementById('speedSlider');
  const speedVal = document.getElementById('speedVal');

  slider.addEventListener('input', function () {
    speedVal.textContent = this.value;
    fetch('/speed?value=' + this.value).catch(() => {});
  });

  // keyboard support (bonus)
  const keyMap = {
    ArrowUp: 'forward', ArrowDown: 'backward',
    ArrowLeft: 'left',  ArrowRight: 'right',
    ' ': 'stop', 'w': 'forward', 's': 'backward',
    'a': 'left', 'd': 'right'
  };
  const held = new Set();

  document.addEventListener('keydown', e => {
    const cmd = keyMap[e.key];
    if (!cmd || held.has(e.key)) return;
    held.add(e.key);
    press({ preventDefault: () => {} }, cmd);
  });

  document.addEventListener('keyup', e => {
    const cmd = keyMap[e.key];
    if (!cmd) return;
    held.delete(e.key);
    if (cmd !== 'stop') release({ preventDefault: () => {} });
    else stopOnly({ preventDefault: () => {} });
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

  ledcAttach(ENA, freq, resolution);
  ledcAttach(ENB, freq, resolution);

  ledcWrite(ENA, speedValue);
  ledcWrite(ENB, speedValue);

  stopCar();

  WiFi.softAP(ssid, password);

  Serial.println();
  Serial.println("================================");
  Serial.println("   ESP32 RC CAR READY");
  Serial.print("   IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("================================");

  server.on("/", []() {
    server.send_P(200, "text/html", webpage);
  });

  server.on("/forward",  []() { moveForward();  server.send(200, "text/plain", "OK"); });
  server.on("/backward", []() { moveBackward(); server.send(200, "text/plain", "OK"); });
  server.on("/left",     []() { turnLeft();     server.send(200, "text/plain", "OK"); });
  server.on("/right",    []() { turnRight();    server.send(200, "text/plain", "OK"); });
  server.on("/stop",     []() { stopCar();      server.send(200, "text/plain", "OK"); });

  server.on("/speed", []() {
    if (server.hasArg("value")) {
      speedValue = constrain(server.arg("value").toInt(), 0, 255);
      ledcWrite(ENA, speedValue);
      ledcWrite(ENB, speedValue);
    }
    server.send(200, "text/plain", "OK");
  });

  server.begin();
  Serial.println("   Web server started");
}

// ================= Loop =================
void loop() {
  server.handleClient();
}
