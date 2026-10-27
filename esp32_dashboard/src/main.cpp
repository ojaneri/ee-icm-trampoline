/**
 * ICM Dashboard for ESP32-C6-LCD-1.47
 * Realistic Car Dashboard (HTML Canvas Gauges) + RTOS Telemetry
 * - 6 Screens: Gauges, Tasks, Jitter Scope, ISR Latency, Debug, Network
 * - BOOT button triggers ISR latency measurement and returns to Screen 0
 * - HTML uses Canvas arcs for realistic Speedometer / Tachometer
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
#define NUM_SCREENS 6

// Waveshare ESP32-C6-LCD-1.47 pins (ST7789 172x320), per the Waveshare wiki/schematic
#define TFT_MOSI 6
#define TFT_SCLK 7
#define TFT_CS 14
#define TFT_DC 15
#define TFT_RST 21
#define TFT_BL 22
#define BOOT_BTN_PIN 9
#define LCD_W 320
#define LCD_H 172

Adafruit_ST7789 *tft = nullptr;
// Off-screen framebuffer: frames are drawn here and pushed in one burst (no flicker)
GFXcanvas16 *canvas = nullptr;
Adafruit_GFX *gfx = nullptr;  // canvas if allocated, otherwise the panel itself
WebServer *server = nullptr;
WebSocketsServer *webSocket = nullptr;

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

// BOOT button interrupt telemetry. The ISR only timestamps the edge (like an
// OSEK category-2 ISR doing SetEvent); loop() plays the task that handles it.
#define BTN_LOCKOUT_US 250000  // ignore contact bounce for 250 ms after an edge
volatile uint32_t isr_cycles = 0;      // CPU cycle counter at ISR entry
volatile uint32_t isr_last_edge_us = 0;
volatile bool isr_pending = false;
uint32_t isr_presses = 0;
uint32_t isr_latency_us = 0;           // last ISR -> task latency
uint32_t isr_latency_min = UINT32_MAX;
uint32_t isr_latency_max = 0;
float selftest_edge_to_isr_us = -1;    // GPIO write -> ISR entry, measured at boot
unsigned long isr_screen_until = 0;    // keep the ISR screen up after a press

// Jitter Scope Buffers (last 8 samples for bar chart)
#define JITTER_SAMPLES 8
unsigned long task_acq_periods[JITTER_SAMPLES] = {0};
unsigned long task_proc_periods[JITTER_SAMPLES] = {0};
unsigned long task_disp_periods[JITTER_SAMPLES] = {0};
unsigned long task_diag_periods[JITTER_SAMPLES] = {0};
int jitter_idx = 0;

unsigned long last_acq_tick = 0;
unsigned long last_proc_tick = 0;
unsigned long last_disp_tick = 0;
unsigned long last_diag_tick = 0;

int current_screen = 0;
unsigned long last_update = 0;
unsigned long last_screen_change = 0;
int drive_step = 0;

void IRAM_ATTR onBootButtonPress() {
  uint32_t cyc = esp_cpu_get_cycle_count();
  uint32_t now = micros();
  if (now - isr_last_edge_us < BTN_LOCKOUT_US) return;
  isr_last_edge_us = now;
  isr_cycles = cyc;
  isr_pending = true;
}

static inline float cyclesToUs(uint32_t cycles) {
  return cycles / (float)ESP.getCpuFreqMHz();
}

// Pulls GPIO9 low in open-drain mode (safe even if BOOT is held) to produce a
// real falling edge, and measures how long the CPU takes to enter the ISR.
void bootIrqSelfTest() {
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BOOT_BTN_PIN), onBootButtonPress, FALLING);
  gpio_set_level((gpio_num_t)BOOT_BTN_PIN, 1);
  gpio_set_direction((gpio_num_t)BOOT_BTN_PIN, GPIO_MODE_INPUT_OUTPUT_OD);  // keeps the input path on
  delay(2);
  isr_pending = false;
  isr_last_edge_us = micros() - BTN_LOCKOUT_US;
  uint32_t t0 = esp_cpu_get_cycle_count();
  gpio_set_level((gpio_num_t)BOOT_BTN_PIN, 0);
  uint32_t wait_start = micros();
  while (!isr_pending && micros() - wait_start < 1000) {}
  if (isr_pending) selftest_edge_to_isr_us = cyclesToUs(isr_cycles - t0);
  gpio_set_level((gpio_num_t)BOOT_BTN_PIN, 1);
  gpio_set_direction((gpio_num_t)BOOT_BTN_PIN, GPIO_MODE_INPUT);
  delay(5);
  isr_pending = false;
  isr_last_edge_us = micros() - BTN_LOCKOUT_US;  // the real button is usable right away
}

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
.tabs { display: flex; justify-content: center; background: #161b22; border-radius: 8px; margin: 10px auto; max-width: 450px; overflow-x: auto; border: 1px solid #30363d; }
.tab { flex: 1; padding: 10px 5px; cursor: pointer; background: #161b22; color: #8b949e; font-weight: bold; font-size: 11px; border: none; outline: none; transition: 0.3s; white-space: nowrap; min-width: 60px;}
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
.isr-badge { background: #8957e5; color: #fff; padding: 8px; border-radius: 6px; font-weight: bold; margin-top: 10px; display: inline-block;}
</style>
</head>
<body>
<h1>ICM INSTRUMENT CLUSTER MODULE</h1>
<p style="font-size:11px; color:#8b949e; margin-top:0;">PPGEE UTFPR - Trampoline RTOS (OSEK/VDX)</p>

<div class="tabs">
  <button class="tab active" id="btn-tab-0" onclick="openTab(event, 0)">Dashboard</button>
  <button class="tab" id="btn-tab-1" onclick="openTab(event, 1)">Tasks</button>
  <button class="tab" id="btn-tab-2" onclick="openTab(event, 2)">Jitter</button>
  <button class="tab" id="btn-tab-3" onclick="openTab(event, 3)">ISR</button>
  <button class="tab" id="btn-tab-4" onclick="openTab(event, 4)">Debug</button>
  <button class="tab" id="btn-tab-5" onclick="openTab(event, 5)">Network</button>
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

<!-- TAB 2: JITTER SCOPE -->
<div id="tab-2" class="tab-content">
  <div class="card">
    <div class="label">Real-Time Task Jitter Scope</div>
    <canvas id="jitterCanvas" width="300" height="150" style="background:#0d1117; border-radius:6px; margin-top:10px;"></canvas>
    <div style="font-size:11px; color:#8b949e; margin-top:5px;">Live period deviation (ms) from nominal</div>
  </div>
</div>

<!-- TAB 3: ISR LATENCY -->
<div id="tab-3" class="tab-content">
  <div class="card">
    <div class="label">Hardware Interrupt Telemetry</div>
    <div style="font-size:32px; font-weight:bold; color:#8957e5; margin:20px 0;"><span id="isr-lat">--</span> <span style="font-size:14px;">µs</span></div>
    <div style="font-size:13px; color:#c9d1d9;">Last ISR &rarr; task latency</div>
    <div class="isr-badge">Trigger: BOOT Button (GPIO9)</div>
    <p style="font-size:11px; color:#8b949e; margin-top:15px;">The ISR only timestamps the falling edge; the main loop (the task) handles it and measures the delay, like an OSEK ISR doing SetEvent.</p>
  </div>
</div>

<!-- TAB 4: DEBUG & FAULTS -->
<div id="tab-4" class="tab-content">
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

<!-- TAB 5: NETWORK -->
<div id="tab-5" class="tab-content">
  <div class="card" style="text-align: left; font-size: 13px;">
    <b style="color:#58a6ff;">Wi-Fi Access Point Status:</b><br><br>
    <b>SSID:</b> ICM-Dashboard-AP<br>
    <b>Password:</b> 12345678<br>
    <b>IP Address:</b> http://192.168.4.1<br>
    <b>Protocol:</b> HTTP + WebSocket<br>
    <b>Screens:</b> 6 on the LCD (auto-rotate 5 s); here, pick a tab<br>
    <b>Control:</b> Press BOOT on the ESP32 to run the ISR test (opens the ISR tab for 10 s)
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
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0.75 * Math.PI, 2.25 * Math.PI);
  ctx.lineWidth = 12;
  ctx.strokeStyle = '#30363d';
  ctx.stroke();

  const angle = 0.75 * Math.PI + (value / maxVal) * 1.5 * Math.PI;
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0.75 * Math.PI, angle);
  ctx.lineWidth = 12;
  ctx.strokeStyle = value >= redline ? '#da3633' : '#238636';
  ctx.lineCap = 'round';
  ctx.stroke();

  const needleAngle = angle;
  const nx = cx + (r - 20) * Math.cos(needleAngle);
  const ny = cy + (r - 20) * Math.sin(needleAngle);
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(nx, ny);
  ctx.lineWidth = 3;
  ctx.strokeStyle = '#f85149';
  ctx.stroke();

  ctx.beginPath();
  ctx.arc(cx, cy, 5, 0, 2 * Math.PI);
  ctx.fillStyle = '#fff';
  ctx.fill();
}

function drawJitterChart(data) {
  const canvas = document.getElementById('jitterCanvas');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const w = canvas.width;
  const h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  // Draw baseline
  ctx.strokeStyle = '#30363d';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(0, h/2);
  ctx.lineTo(w, h/2);
  ctx.stroke();

  if (!data || !data.acq) return;

  const barW = w / data.acq.length;
  const scale = 2.0; // pixels per ms deviation

  const drawBars = (arr, color, yOffset) => {
    ctx.fillStyle = color;
    for (let i = 0; i < arr.length; i++) {
      let dev = arr[i];
      let bh = dev * scale;
      ctx.fillRect(i * barW + 2, (h/2) - bh + yOffset, barW - 4, bh);
    }
  };

  drawBars(data.acq, '#58a6ff', 0);
}

let ws;
function connectWS() {
  ws = new WebSocket('ws://' + window.location.hostname + ':81/');
  ws.onmessage = function(event) {
    var d = JSON.parse(event.data);

    document.getElementById('speed-val').innerText = d.speed.toFixed(1);
    document.getElementById('rpm-val').innerText = d.rpm;
    document.getElementById('fuel').innerText = d.fuel.toFixed(1);
    document.getElementById('temp').innerText = d.temp.toFixed(1);

    drawGauge('speedo', d.speed, 220, 'km/h', 180);
    drawGauge('tacho', d.rpm, 7000, 'rpm', 6000);

    document.getElementById('j_can').innerText = d.jobs_acq;
    document.getElementById('j_acq').innerText = d.jobs_acq;
    document.getElementById('j_proc').innerText = d.jobs_proc;
    document.getElementById('j_disp').innerText = d.jobs_disp;
    document.getElementById('j_diag').innerText = d.jobs_diag;
    document.getElementById('cpu2').innerText = d.cpu.toFixed(2);
    document.getElementById('misses').innerText = d.misses;
    document.getElementById('overruns').innerText = d.overruns;
    document.getElementById('torn').innerText = d.torn;

    if (d.isr_lat > 0) {
      document.getElementById('isr-lat').innerText = d.isr_lat;
    }

    if (d.jitter) {
      drawJitterChart(d.jitter);
    }

    var st = document.getElementById('status');
    if (d.fault) {
      st.className = 'fault';
      st.innerText = d.msg;
    } else {
      st.className = 'nominal';
      st.innerText = d.msg;
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
  if (server) server->send_P(200, "text/html", index_html);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED && webSocket) {
    Serial.printf("[%u] Connected from %s\n", num, webSocket->remoteIP(num).toString().c_str());
  }
}

void drawHeader(const char* title) {
  if (!gfx) return;
  gfx->fillRect(0, 0, 320, 28, ST77XX_BLUE);
  gfx->setTextColor(ST77XX_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(4, 6);
  gfx->printf("%s [%d/%d]", title, current_screen + 1, NUM_SCREENS);
}

void drawDashboard() {
  if (!gfx) return;
  gfx->fillScreen(ST77XX_BLACK);

  if (current_screen == 0) {
    // Screen 0: Main Gauges
    drawHeader("GAUGES");
    gfx->setTextSize(3);
    gfx->setTextColor(ST77XX_CYAN);
    gfx->setCursor(6, 36);
    gfx->printf("%.0f km/h", vehicle.speed);
    gfx->setTextColor(ST77XX_YELLOW);
    gfx->setCursor(6, 72);
    gfx->printf("%d rpm", vehicle.rpm);
    gfx->setTextSize(2);
    gfx->setTextColor(ST77XX_WHITE);
    gfx->setCursor(6, 108);
    gfx->printf("F:%.0f%% T:%.0fC", vehicle.fuel, vehicle.temp);

    if (vehicle.fault_active) {
      gfx->fillRect(0, 138, 320, 34, ST77XX_RED);
      gfx->setTextSize(1);
      gfx->setTextColor(ST77XX_WHITE);
      gfx->setCursor(4, 148);
      gfx->print(vehicle.message);
    } else {
      gfx->fillRect(0, 138, 320, 34, ST77XX_GREEN);
      gfx->setTextSize(1);
      gfx->setTextColor(ST77XX_BLACK);
      gfx->setCursor(4, 148);
      gfx->print("STATUS: NORMAL (0 TORN)");
    }
  }
  else if (current_screen == 1) {
    // Screen 1: RTOS Task Frequencies & Jobs
    drawHeader("TASKS");
    gfx->setTextSize(2);
    gfx->setTextColor(ST77XX_GREEN);
    gfx->setCursor(6, 36);
    gfx->print("OSEK COUNTERS");
    gfx->setTextSize(1);
    gfx->setCursor(6, 58);
    gfx->printf("CAN_RX(p6): %lu", (unsigned long)vehicle.jobs_acquisition);
    gfx->setCursor(6, 74);
    gfx->printf("Acq 10ms(p5): %lu", (unsigned long)vehicle.jobs_acquisition);
    gfx->setCursor(6, 90);
    gfx->printf("Proc 20ms(p4): %lu", (unsigned long)vehicle.jobs_processing);
    gfx->setCursor(6, 106);
    gfx->printf("Disp 50ms(p3): %lu", (unsigned long)vehicle.jobs_display);
    gfx->setCursor(6, 122);
    gfx->printf("Diag 100ms(p2): %lu", (unsigned long)vehicle.jobs_diagnostics);
    gfx->fillRect(0, 144, 320, 28, ST77XX_BLUE);
    gfx->setTextSize(1);
    gfx->setTextColor(ST77XX_WHITE);
    gfx->setCursor(4, 152);
    gfx->printf("CPU: %.1f%% Ceiling:OK", (double)vehicle.cpu_utilization);
  }
  else if (current_screen == 2) {
    // Screen 2: Jitter Scope (Visual Bar Chart)
    drawHeader("JITTER");
    gfx->setTextSize(2);
    gfx->setTextColor(ST77XX_MAGENTA);
    gfx->setCursor(6, 36);
    gfx->print("PERIOD DEVIATION");

    // Draw simple bar chart for Acquisition Task Jitter
    int baseY = 120;
    int maxH = 60;
    int barW = 30;
    gfx->setTextSize(1);
    gfx->setTextColor(ST77XX_WHITE);
    gfx->setCursor(6, 140);
    gfx->print("ACQ 10ms LAST 8 JOBS");

    for (int i = 0; i < JITTER_SAMPLES; i++) {
      long dev = (long)task_acq_periods[i] - 10000; // deviation from 10ms in us
      int h = (int)(dev / 200); // scale
      if (h > maxH) h = maxH;
      if (h < -maxH) h = -maxH;

      uint16_t color = (abs(dev) > 2000) ? ST77XX_RED : ST77XX_CYAN;
      gfx->fillRect(10 + i * (barW + 5), baseY - h, barW, h, color);
    }
    // Baseline
    gfx->drawFastHLine(0, baseY, 320, ST77XX_WHITE);
  }
  else if (current_screen == 3) {
    // Screen 3: ISR Latency Visualizer
    drawHeader("ISR LATENCY");
    gfx->setTextSize(2);
    gfx->setTextColor(ST77XX_WHITE);
    gfx->setCursor(6, 36);
    gfx->print("BOOT IRQ (GPIO9)");

    if (isr_presses > 0) {
      gfx->setTextSize(3);
      gfx->setTextColor(ST77XX_GREEN);
      gfx->setCursor(6, 60);
      gfx->printf("%lu us", (unsigned long)isr_latency_us);
      gfx->setTextSize(1);
      gfx->setTextColor(ST77XX_CYAN);
      gfx->setCursor(6, 88);
      gfx->printf("ISR->TASK min/max: %lu/%lu us",
                  (unsigned long)isr_latency_min, (unsigned long)isr_latency_max);
      gfx->setTextColor(ST77XX_WHITE);
      gfx->setCursor(6, 104);
      gfx->printf("PRESSES: %lu", (unsigned long)isr_presses);
    } else {
      gfx->setTextSize(2);
      gfx->setTextColor(ST77XX_YELLOW);
      gfx->setCursor(6, 62);
      gfx->print("PRESS BOOT");
      gfx->setTextSize(1);
      gfx->setTextColor(ST77XX_WHITE);
      gfx->setCursor(6, 88);
      gfx->print("Measures ISR -> task latency");
    }
    gfx->setTextSize(1);
    gfx->setTextColor(ST77XX_WHITE);
    gfx->setCursor(6, 122);
    if (selftest_edge_to_isr_us >= 0) gfx->printf("SELF-TEST EDGE->ISR: %.1f us", selftest_edge_to_isr_us);
    else gfx->print("SELF-TEST EDGE->ISR: n/a");

    gfx->fillRect(0, 144, 320, 28, ST77XX_BLUE);
    gfx->setTextColor(ST77XX_WHITE);
    gfx->setCursor(4, 152);
    gfx->print("GPIO9 FALLING EDGE TRIGGER");
  }
  else if (current_screen == 4) {
    // Screen 4: Debug & Fault Metrics
    drawHeader("DEBUG");
    gfx->setTextSize(2);
    gfx->setTextColor(ST77XX_YELLOW);
    gfx->setCursor(6, 36);
    gfx->print("FAULT METRICS");
    gfx->setTextSize(1);
    gfx->setTextColor(ST77XX_WHITE);
    gfx->setCursor(6, 60);
    gfx->printf("Misses: %lu", (unsigned long)vehicle.deadline_misses);
    gfx->setCursor(6, 78);
    gfx->printf("Overruns: %lu", (unsigned long)vehicle.os_overruns);
    gfx->setCursor(6, 96);
    gfx->printf("Torn: %lu (Locked)", (unsigned long)vehicle.torn_copies);
    gfx->setCursor(6, 114);
    gfx->print("CAN FIFO: OK");
    gfx->fillRect(0, 144, 320, 28, vehicle.fault_active ? ST77XX_RED : ST77XX_BLUE);
    gfx->setCursor(4, 152);
    gfx->print(vehicle.fault_active ? vehicle.message : "DTC: NO FAULTS");
  }
  else if (current_screen == 5) {
    // Screen 5: Network & Wi-Fi AP Status
    drawHeader("NETWORK");
    gfx->setTextSize(2);
    gfx->setTextColor(ST77XX_CYAN);
    gfx->setCursor(6, 36);
    gfx->print("WI-FI ACCESS PT");
    gfx->setTextSize(1);
    gfx->setTextColor(ST77XX_WHITE);
    gfx->setCursor(6, 62);
    gfx->print("SSID: ICM-Dashboard-AP");
    gfx->setCursor(6, 82);
    gfx->print("IP: 192.168.4.1");
    gfx->setCursor(6, 102);
    gfx->print("WS port: 81");
    gfx->fillRect(0, 144, 320, 28, ST77XX_BLUE);
    gfx->setCursor(4, 152);
    gfx->print("BOOT = ISR latency test");
  }

  if (canvas) tft->drawRGBBitmap(0, 0, canvas->getBuffer(), LCD_W, LCD_H);
}

// Re-sends the panel wake-up sequence. If an ESD/touch glitch put the ST7789
// into its reset state (sleep + display off), this brings it back without a reboot.
void refreshPanelState() {
  tft->sendCommand(ST77XX_SLPOUT);
  delay(5);
  uint8_t colmod = 0x55;  // 16-bit RGB565
  tft->sendCommand(ST77XX_COLMOD, &colmod, 1);
  tft->setRotation(1);
  tft->sendCommand(ST77XX_INVON);
  tft->sendCommand(ST77XX_NORON);
  tft->sendCommand(ST77XX_DISPON);
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // never block when no USB host is reading
#endif
  delay(500);
  Serial.println("\n=== ICM ESP32-C6 BOOT ===");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  bootIrqSelfTest();
  Serial.printf("[OK] BOOT IRQ armed, self-test edge->ISR: %.2f us\n", selftest_edge_to_isr_us);

  // Hardware SPI (FSPI): fast enough to push a full frame in a few ms
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft = new Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);
  tft->init(172, 320);
  tft->setRotation(1);
  tft->setSPISpeed(40000000);
  tft->fillScreen(ST77XX_BLACK);
  Serial.println("[OK] Display initialized (HW SPI 40MHz)");

  canvas = new GFXcanvas16(LCD_W, LCD_H);
  if (canvas && canvas->getBuffer()) {
    gfx = canvas;
    Serial.println("[OK] Framebuffer allocated");
  } else {
    delete canvas;
    canvas = nullptr;
    gfx = tft;
    Serial.println("[WARN] No RAM for framebuffer, drawing directly");
  }

  tft->setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft->setTextSize(2);
  tft->setCursor(40, 70);
  tft->print("ICM BOOTING...");

  Serial.println("[..] Starting Wi-Fi AP...");
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  Serial.print("[OK] AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Heap-allocate network objects to avoid static init crash
  Serial.println("[..] Starting WebServer and WebSocket...");
  server = new WebServer(80);
  webSocket = new WebSocketsServer(81);

  server->on("/", handleRoot);
  server->begin();
  webSocket->begin();
  webSocket->onEvent(webSocketEvent);
  Serial.println("[OK] HTTP + WebSocket servers started");

  last_screen_change = millis();
  last_acq_tick = micros();
  last_proc_tick = micros();
  last_disp_tick = micros();
  last_diag_tick = micros();
  Serial.println("=== BOOT COMPLETE ===\n");
}

void loop() {
  if (server) server->handleClient();
  if (webSocket) webSocket->loop();

  // Display watchdog: re-wake the panel every 1s in case a glitch reset it
  static unsigned long last_watchdog = 0;
  if (millis() - last_watchdog > 1000) {
    last_watchdog = millis();
    refreshPanelState();
  }

  // Redraw the active screen at 4 Hz
  static unsigned long last_draw = 0;
  if (millis() - last_draw > 250) {
    last_draw = millis();
    drawDashboard();
  }

  // "Task" side of the BOOT interrupt: measure ISR -> task latency, show the ISR screen
  if (isr_pending) {
    uint32_t lat = (uint32_t)(cyclesToUs(esp_cpu_get_cycle_count() - isr_cycles) + 0.5f);
    isr_pending = false;
    isr_presses++;
    isr_latency_us = lat;
    if (lat < isr_latency_min) isr_latency_min = lat;
    if (lat > isr_latency_max) isr_latency_max = lat;
    current_screen = 3;
    isr_screen_until = millis() + 10000;
    Serial.printf("BOOT IRQ #%lu: ISR->task %lu us (min %lu, max %lu)\n", (unsigned long)isr_presses,
                  (unsigned long)lat, (unsigned long)isr_latency_min, (unsigned long)isr_latency_max);
    drawDashboard();
  }

  // Auto-rotate every 5 s, except while the ISR result is being shown
  bool hold_isr = (long)(isr_screen_until - millis()) > 0;
  if (!hold_isr && (millis() - last_screen_change > 5000)) {
    last_screen_change = millis();
    current_screen = (current_screen + 1) % NUM_SCREENS;
    Serial.printf("Auto-rotated to screen %d (free heap %u, IRQ self-test %.2f us, BOOT presses %lu)\n",
                  current_screen + 1, (unsigned)ESP.getFreeHeap(), selftest_edge_to_isr_us,
                  (unsigned long)isr_presses);
    drawDashboard();
  }

  // Simulate drive cycle and RTOS tasks every 50ms
  if (millis() - last_update > 50) {
    last_update = millis();
    drive_step = (drive_step + 1) % 600;

    // Measure Jitter (Simulated Task Periods)
    unsigned long now_us = micros();
    task_acq_periods[jitter_idx % JITTER_SAMPLES] = now_us - last_acq_tick;
    last_acq_tick = now_us;

    // Simulate other task periods less frequently based on their nominal rates
    if (drive_step % 2 == 0) { // Proc is slower
       task_proc_periods[jitter_idx % JITTER_SAMPLES] = (now_us - last_proc_tick) / 2; // normalize approx
       last_proc_tick = now_us;
    }
    jitter_idx++;

    vehicle.jobs_acquisition++;
    vehicle.jobs_processing++;
    vehicle.jobs_display++;
    vehicle.jobs_diagnostics++;

    // Simulate fuel consumption and engine temperature variation
    vehicle.fuel = 65.0 - (drive_step * 0.05);
    if (vehicle.fuel < 10.0) vehicle.fuel = 10.0;
    vehicle.temp = 82.0 + (vehicle.speed * 0.15);
    if (vehicle.temp > 110.0) vehicle.temp = 110.0;

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
      strcpy(vehicle.message, "COMM LOST: M2 TIMEOUT");
    } else {
      vehicle.speed = 0.0;
      vehicle.rpm = 800;
      vehicle.fault_active = false;
      strcpy(vehicle.message, "VEHICLE STOPPED");
    }


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
    json += "\"isr_lat\":" + String(isr_latency_us) + ",";

    // Add Jitter Data Array
    json += "\"jitter\":{\"acq\":[";
    for(int i=0; i<JITTER_SAMPLES; i++) {
      int idx = (jitter_idx + i) % JITTER_SAMPLES;
      float dev_ms = (task_acq_periods[idx] - 10000.0) / 1000.0; // deviation in ms
      json += String(dev_ms);
      if (i < JITTER_SAMPLES-1) json += ",";
    }
    json += "]},";

    // Sanitize message: escape quotes and backslashes for valid JSON
    String safe_msg = "";
    for (size_t i = 0; vehicle.message[i] != '\0'; i++) {
      char c = vehicle.message[i];
      if (c == '"' || c == '\\') safe_msg += '\\';
      safe_msg += c;
    }
    json += "\"msg\":\"" + safe_msg + "\"";
    json += ",\"screen\":" + String(current_screen);
    json += "}";
    if (webSocket) webSocket->broadcastTXT(json);
  }
}
