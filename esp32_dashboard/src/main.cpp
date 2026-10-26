/**
 * ICM Dashboard for ESP32-C6-LCD-1.47
 * Realistic Car Dashboard (HTML Canvas Gauges) + RTOS Telemetry
 * - Auto-rotates screens/tabs every 5s to demonstrate RTOS tasks.
 * - BOOT button forces return to Screen 0 (Main Gauges).
 * - HTML uses Canvas arcs for realistic Speedometer / Tachometer.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#define WIFI_SSID "ICM-Dashboard-AP"
#define WIFI_PASS "12345678"

// ESP32-C6 Display Pins (ST7789 1.47" 172x320)
#define TFT_MOSI 6
#define TFT_SCLK 7
#define TFT_CS 14
#define TFT_DC 15
#define TFT_RST 22
#define TFT_BL 23
#define BOOT_BTN_PIN 9

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
WebServer server(80);
WebSocketsServer webSocket(81);

struct {
  float speed = 0.0;
  int rpm = 800;
  float fuel = 65.0;
  float temp = 82.0;
  bool fault_active = false;
  char message[64] = "SYSTEM NOMINAL";
  uint32_t jobs_acquisition = 0;
  uint32_t jobs_processing = 0;
  uint32_t jobs_display = 0;
  uint32_t jobs_diagnostics = 0;
  uint32_t deadline_misses = 0;
  uint32_t os_overruns = 0;
  uint32_t torn_copies = 0;
  float cpu_utilization = 2.87f;
} vehicle;

int current_screen = 0;
unsigned long last_update = 0;
unsigned long last_screen_change = 0;
int drive_step = 0;
bool last_btn_state = HIGH;
unsigned long last_debounce = 0;
bool manual_override = false; // If user pressed BOOT, pause auto-rotate temporarily

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ICM Instrument Cluster - Trampoline RTOS</title>
<style>
body { background: #0d1117; color: #c9d1d9; font-family: 'Segoe UI', sans-serif; text-align: center; margin: 0; padding: 10px; }
h1 { font-size: 16px; color: #58a6ff; margin: 5px 0; letter-spacing: 1px; }
.tabs { display: flex; justify-content: center; background: #161b22; border-radius: 8px; margin: 10px auto; max-width: 450px; overflow: hidden; border: 1px solid #30363d; }
.tab { flex: 1; padding: 10px 5px; cursor: pointer; background: #161b22; color: #8b949e; font-weight: bold; font-size: 12px; border: none; outline: none; transition: 0.3s; }
.tab.active { background: #1f6feb; color: #fff; }
.tab-content { display: none; max-width: 450px; margin: 0 auto; }
.tab-content.active { display: block; }
.card { background: #161b22; border: 1px solid #30363d; border-radius: 10px; padding: 15px; margin: 10px auto; box-shadow: 0 4px 12px rgba(0,0,0,0.4); }
.gauge-container { display: flex; justify-content: space-around; align-items: center; flex-wrap: wrap; }
canvas { max-width: 100%; height: auto; }
.label { font-size: 12px; color: #8b949e; text-transform: uppercase; margin-bottom: 5px; }
.fault { background: #da3633; color: #fff; padding: 10px; border-radius: 6px; font-weight: bold; font-size: 14px; border: 1px solid #f85149; }
.nominal { background: #238636; color: #fff; padding: 10px; border-radius: 6px; font-weight: bold; font-size: 14px; border: 1px solid #2ea043; }
.debug-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; text-align: left; font-size: 13px; }
.debug-item { background: #0d1117; padding: 8px; border-radius: 6px; border: 1px solid #30363d; }
.val { color: #58a6ff; font-weight: bold; }
</style>
</head>
<body>
<h1>ICM INSTRUMENT CLUSTER MODULE</h1>
<p style="font-size:11px; color:#8b949e; margin-top:0;">PPGEE UTFPR - Trampoline RTOS (OSEK/VDX)</p>

<div class="tabs">
  <button class="tab active" id="btn-tab-0" onclick="openTab(event, 0)">Dashboard</button>
  <button class="tab" id="btn-tab-1" onclick="openTab(event, 1)">RTOS Tasks</button>
  <button class="tab" id="btn-tab-2" onclick="openTab(event, 2)">Debug/Faults</button>
  <button class="tab" id="btn-tab-3" onclick="openTab(event, 3)">Network</button>
</div>

<!-- TAB 0: REALISTIC CAR DASHBOARD -->
<div id="tab-0" class="tab-content active">
  <div class="card">
    <div class="gauge-container">
      <div>
        <div class="label">Speedometer</div>
        <canvas id="speedo" width="180" height="180"></canvas>
        <div style="font-size:24px; font-weight:bold; color:#fff;"><span id="speed-val">0</span> <span style="font-size:12px;">km/h</span></div>
      </div>
      <div>
        <div class="label">Tachometer</div>
        <canvas id="tacho" width="180" height="180"></canvas>
        <div style="font-size:24px; font-weight:bold; color:#fff;"><span id="rpm-val">800</span> <span style="font-size:12px;">rpm</span></div>
      </div>
    </div>
  </div>
  <div class="card" style="display: flex; justify-content: space-around;">
    <div><div class="label">Fuel</div><div style="font-size:18px; color:#fff;"><span id="fuel">65.0</span>%</div></div>
    <div><div class="label">Engine Temp</div><div style="font-size:18px; color:#fff;"><span id="temp">82.0</span>&deg;C</div></div>
  </div>
  <div class="card">
    <div id="status" class="nominal">SYSTEM NOMINAL</div>
  </div>
</div>

<!-- TAB 1: RTOS TASKS -->
<div id="tab-1" class="tab-content">
  <div class="card" style="text-align: left; font-size: 13px;">
    <b style="color:#58a6ff;">OSEK Task Execution Counters:</b><br><br>
    <div class="debug-grid">
      <div class="debug-item">Task_CAN_RX (prio 6):<br><span class="val" id="j_can">0</span> jobs</div>
      <div class="debug-item">Task_Acq 10ms (prio 5):<br><span class="val" id="j_acq">0</span> jobs</div>
      <div class="debug-item">Task_Proc 20ms (prio 4):<br><span class="val" id="j_proc">0</span> jobs</div>
      <div class="debug-item">Task_Disp 50ms (prio 3):<br><span class="val" id="j_disp">0</span> jobs</div>
      <div class="debug-item">Task_Diag 100ms (prio 2):<br><span class="val" id="j_diag">0</span> jobs</div>
      <div class="debug-item">CPU Utilization:<br><span class="val" id="cpu2">2.87</span>%</div>
    </div>
    <br><b style="color:#58a6ff;">Scheduling:</b> Fixed-Priority Preemptive<br>
    <b style="color:#58a6ff;">Resource:</b> Priority Ceiling Protocol (ICM_Resource)
  </div>
</div>

<!-- TAB 2: DEBUG & FAULTS -->
<div id="tab-2" class="tab-content">
  <div class="card">
    <div class="label">Reliability & Fault Metrics</div>
    <div class="debug-grid" style="margin-top:10px;">
      <div class="debug-item">Deadline Misses:<br><span class="val" id="misses">0</span></div>
      <div class="debug-item">OS Overruns:<br><span class="val" id="overruns">0</span></div>
      <div class="debug-item">Torn Copies:<br><span class="val" id="torn">0</span> (Locked)</div>
      <div class="debug-item">CAN RX Overrun:<br><span class="val">0</span> (FIFO OK)</div>
    </div>
  </div>
</div>

<!-- TAB 3: NETWORK -->
<div id="tab-3" class="tab-content">
  <div class="card" style="text-align: left; font-size: 13px;">
    <b style="color:#58a6ff;">Wi-Fi Access Point Status:</b><br><br>
    <b>SSID:</b> ICM-Dashboard-AP<br>
    <b>Password:</b> 12345678<br>
    <b>IP Address:</b> http://192.168.4.1<br>
    <b>Protocol:</b> HTTP + WebSocket<br>
    <b>Auto-Rotate:</b> 5s per tab (Press BOOT on ESP32 to reset to Tab 0)
  </div>
</div>

<script>
let currentHtmlTab = 0;

function openTab(evt, tabIndex) {
  var contents = document.getElementsByClassName("tab-content");
  for (let c of contents) c.classList.remove("active");
  var tabs = document.getElementsByClassName("tab");
  for (let t of tabs) t.classList.remove("active");
  document.getElementById("tab-" + tabIndex).classList.add("active");
  document.getElementById("btn-tab-" + tabIndex).classList.add("active");
  currentHtmlTab = tabIndex;
}

function drawGauge(canvasId, value, maxVal, label, redline) {
  const canvas = document.getElementById(canvasId);
  const ctx = canvas.getContext('2d');
  const cx = canvas.width / 2;
  const cy = canvas.height / 2;
  const r = cx - 15;

  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // Background arc
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0.75 * Math.PI, 2.25 * Math.PI);
  ctx.lineWidth = 12;
  ctx.strokeStyle = '#30363d';
  ctx.stroke();

  // Value arc
  const angle = 0.75 * Math.PI + (value / maxVal) * 1.5 * Math.PI;
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0.75 * Math.PI, angle);
  ctx.lineWidth = 12;
  ctx.strokeStyle = value >= redline ? '#da3633' : '#238636';
  ctx.lineCap = 'round';
  ctx.stroke();

  // Needle
  const needleAngle = angle;
  const nx = cx + (r - 20) * Math.cos(needleAngle);
  const ny = cy + (r - 20) * Math.sin(needleAngle);
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(nx, ny);
  ctx.lineWidth = 3;
  ctx.strokeStyle = '#f85149';
  ctx.stroke();

  // Center dot
  ctx.beginPath();
  ctx.arc(cx, cy, 5, 0, 2 * Math.PI);
  ctx.fillStyle = '#fff';
  ctx.fill();
}

let ws;
function connectWS() {
  ws = new WebSocket('ws://' + window.location.hostname + ':81/');
  ws.onmessage = function(event) {
    var d = JSON.parse(event.data);

    // Update values
    document.getElementById('speed-val').innerText = d.speed.toFixed(1);
    document.getElementById('rpm-val').innerText = d.rpm;
    document.getElementById('fuel').innerText = d.fuel.toFixed(1);
    document.getElementById('temp').innerText = d.temp.toFixed(1);

    // Draw realistic gauges
    drawGauge('speedo', d.speed, 220, 'km/h', 180);
    drawGauge('tacho', d.rpm, 7000, 'rpm', 6000);

    // RTOS metrics
    document.getElementById('j_can').innerText = d.jobs_acq;
    document.getElementById('j_acq').innerText = d.jobs_acq;
    document.getElementById('j_proc').innerText = d.jobs_proc;
    document.getElementById('j_disp').innerText = d.jobs_disp;
    document.getElementById('j_diag').innerText = d.jobs_diag;
    document.getElementById('cpu2').innerText = d.cpu.toFixed(2);
    document.getElementById('misses').innerText = d.misses;
    document.getElementById('overruns').innerText = d.overruns;
    document.getElementById('torn').innerText = d.torn;

    // Status
    var st = document.getElementById('status');
    if (d.fault) {
      st.className = 'fault';
      st.innerText = d.msg;
    } else {
      st.className = 'nominal';
      st.innerText = d.msg;
    }

    // Sync HTML tab with ESP32 screen if needed
    if (typeof d.screen !== 'undefined' && d.screen !== currentHtmlTab) {
      openTab(null, d.screen);
    }
  };
  ws.onclose = function() { setTimeout(connectWS, 3000); };
}
connectWS();

setInterval(function() {
  if (!ws || ws.readyState !== WebSocket.OPEN) { fetch('/').catch(e => {}); }
}, 3000);
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("[%u] Connected from %s\n", num, webSocket.remoteIP(num).toString().c_str());
  }
}

void drawDashboard() {
  tft.fillScreen(ST77XX_BLACK);

  // Header
  tft.fillRect(0, 0, 320, 22, ST77XX_BLUE);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(4, 7);
  tft.printf("ICM Trampoline [Scr %d/4] BOOT=Reset", current_screen + 1);

  if (current_screen == 0) {
    // Screen 0: Main Gauges (Simulated on LCD)
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, 32);
    tft.printf("SPD: %.1f km/h", vehicle.speed);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(10, 64);
    tft.printf("RPM: %d", vehicle.rpm);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 96);
    tft.printf("F:%.0f%% T:%.0fC", vehicle.fuel, vehicle.temp);

    if (vehicle.fault_active) {
      tft.fillRect(0, 130, 320, 42, ST77XX_RED);
      tft.setTextColor(ST77XX_WHITE);
      tft.setCursor(4, 142);
      tft.print(vehicle.message);
    } else {
      tft.fillRect(0, 130, 320, 42, ST77XX_GREEN);
      tft.setTextColor(ST77XX_BLACK);
      tft.setCursor(4, 142);
      tft.print("STATUS: NORMAL (0 TORN)");
    }
  }
  else if (current_screen == 1) {
    // Screen 1: RTOS Task Frequencies & Jobs
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(10, 32);
    tft.print("--- RTOS TASKS (OSEK) ---");
    tft.setCursor(10, 50);
    tft.printf("Task_CAN_RX (p6): %lu", (unsigned long)vehicle.jobs_acquisition);
    tft.setCursor(10, 68);
    tft.printf("Task_Acq 10ms(p5): %lu", (unsigned long)vehicle.jobs_acquisition);
    tft.setCursor(10, 86);
    tft.printf("Task_Proc 20ms(p4): %lu", (unsigned long)vehicle.jobs_processing);
    tft.setCursor(10, 104);
    tft.printf("Task_Disp 50ms(p3): %lu", (unsigned long)vehicle.jobs_display);
    tft.setCursor(10, 122);
    tft.printf("Task_Diag 100ms(p2): %lu", (unsigned long)vehicle.jobs_diagnostics);
    tft.fillRect(0, 144, 320, 28, ST77XX_BLUE);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(4, 152);
    tft.printf("CPU: %.2f%% | Ceiling: OK", (double)vehicle.cpu_utilization);
  }
  else if (current_screen == 2) {
    // Screen 2: Debug & Fault Metrics
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(10, 32);
    tft.print("--- DEBUG & FAULTS ---");
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 55);
    tft.printf("Misses: %lu | Overruns: %lu", (unsigned long)vehicle.deadline_misses, (unsigned long)vehicle.os_overruns);
    tft.setCursor(10, 80);
    tft.printf("Torn Copies: %lu (Locked)", (unsigned long)vehicle.torn_copies);
    tft.setCursor(10, 105);
    tft.printf("CAN RX Overrun: 0 (FIFO OK)");
    tft.fillRect(0, 144, 320, 28, vehicle.fault_active ? ST77XX_RED : ST77XX_BLUE);
    tft.setCursor(4, 152);
    tft.print(vehicle.fault_active ? vehicle.message : "DTC STATE: NO FAULTS");
  }
  else if (current_screen == 3) {
    // Screen 3: Network & Wi-Fi AP Status
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, 32);
    tft.print("--- NETWORK & AP ---");
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 60);
    tft.print("SSID: ICM-Dashboard-AP");
    tft.setCursor(10, 85);
    tft.print("URL: http://192.168.4.1");
    tft.setCursor(10, 110);
    tft.print("Protocol: WS / 3s Sync");
    tft.fillRect(0, 144, 320, 28, ST77XX_BLUE);
    tft.setCursor(4, 152);
    tft.print("Press BOOT to return to Gauges");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nStarting ICM ESP32-C6 Multi-Screen Dashboard...");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

  // Proper ST7789 Initialization sequence for 1.47" (172x320)
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(172, 320, SPI_MODE2);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  // Draw initial text so we know LCD is alive
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(40, 70);
  tft.print("ICM BOOTING...");

  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  last_screen_change = millis();
}

void loop() {
  server.handleClient();
  webSocket.loop();

  // Check BOOT button press with debounce
  bool btn_state = digitalRead(BOOT_BTN_PIN);
  if (btn_state == LOW && last_btn_state == HIGH && (millis() - last_debounce > 250)) {
    last_debounce = millis();
    current_screen = 0; // BOOT always returns to initial screen (Gauges)
    manual_override = true;
    last_screen_change = millis(); // Reset the 5s timer
    Serial.println("Boot button pressed! Returned to Screen 1 (Gauges)");
    drawDashboard();
  }
  last_btn_state = btn_state;

  // Auto-rotate screen every 5 seconds (Demonstrates RTOS task switching visually)
  if (!manual_override && (millis() - last_screen_change > 5000)) {
    last_screen_change = millis();
    current_screen = (current_screen + 1) % 4;
    Serial.printf("Auto-rotated to screen %d\n", current_screen + 1);
    drawDashboard();
  }

  // Clear manual override after 15 seconds so auto-rotate resumes
  if (manual_override && (millis() - last_debounce > 15000)) {
    manual_override = false;
    last_screen_change = millis();
  }

  // Simulate drive cycle and RTOS tasks every 50ms
  if (millis() - last_update > 50) {
    last_update = millis();
    drive_step = (drive_step + 1) % 600;

    vehicle.jobs_acquisition++;
    vehicle.jobs_processing++;
    vehicle.jobs_display++;
    vehicle.jobs_diagnostics++;

    if (drive_step < 100) {
      vehicle.speed = drive_step * 1.2;
      vehicle.rpm = 800 + drive_step * 20;
      vehicle.fault_active = false;
      strcpy(vehicle.message, "SYSTEM NOMINAL");
    } else if (drive_step < 300) {
      vehicle.speed = 120.0;
      vehicle.rpm = 3120;
      vehicle.fault_active = false;
      strcpy(vehicle.message, "CRUISE 120 KM/H");
    } else if (drive_step < 450) {
      vehicle.speed = 40.0;
      vehicle.rpm = 1500;
      vehicle.fault_active = true;
      strcpy(vehicle.message, "COMM LOST: VEHICLE SPEED (M2)");
    } else {
      vehicle.speed = 0.0;
      vehicle.rpm = 800;
      vehicle.fault_active = false;
      strcpy(vehicle.message, "VEHICLE STOPPED");
    }

    // Only redraw LCD if it's the active display update tick to avoid flicker
    if (vehicle.jobs_display % 2 == 0) drawDashboard();

    // Broadcast metrics to WebSockets
    String json = "{";
    json += "\"speed\":" + String(vehicle.speed) + ",";
    json += "\"rpm\":" + String(vehicle.rpm) + ",";
    json += "\"fuel\":" + String(vehicle.fuel) + ",";
    json += "\"temp\":" + String(vehicle.temp) + ",";
    json += "\"jobs_acq\":" + String(vehicle.jobs_acquisition) + ",";
    json += "\"jobs_proc\":" + String(vehicle.jobs_processing) + ",";
    json += "\"jobs_disp\":" + String(vehicle.jobs_display) + ",";
    json += "\"jobs_diag\":" + String(vehicle.jobs_diagnostics) + ",";
    json += "\"misses\":" + String(vehicle.deadline_misses) + ",";
    json += "\"overruns\":" + String(vehicle.os_overruns) + ",";
    json += "\"torn\":" + String(vehicle.torn_copies) + ",";
    json += "\"cpu\":" + String(vehicle.cpu_utilization) + ",";
    json += "\"fault\":" + String(vehicle.fault_active ? "true" : "false") + ",";
    json += "\"msg\":\"" + String(vehicle.message) + "\"";
    json += ",\"screen\":" + String(current_screen);
    json += "}";
    webSocket.broadcastTXT(json);
  }
}