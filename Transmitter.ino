/*
  RadioGeet Digital Pvt. Ltd.
  TATA STEEL – SPONGE IRON, JODA
  Field Station: Radiowave Remote IO R865 (LoRa Transmitter)
  Hardware: ESP32-S3 Dev Module + 128x64 I2C OLED (SDA: 39, SCL: 38)
  Default WiFi Password: 12345678
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// =======================================================================
// 1. HARDWARE PINOUT & CONFIGURATION
// =======================================================================
#define I2C_SDA 39
#define I2C_SCL 38

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define LORA_RX_PIN 16
#define LORA_TX_PIN 17

// REAL WIRED ANALOG SENSOR INPUT (ESP32-S3 ADC1 Channel)
// Connect sensor wire to GPIO 14 (with 150/250 ohm shunt to GND for 4-20mA loops)
#define WIRED_SENSOR_PIN 14

// RS485 Interface Pins for Masibus Controller (Hardware Serial 2)
#define RS485_RX_PIN 18
#define RS485_TX_PIN 8

// =======================================================================
// 2. PREFERENCES & SYSTEM VARIABLES
// =======================================================================
Preferences prefs;
WebServer server(80);

String target_device_id;
String sensor_unit;
int tx_interval_ms;
float min_setpoint;  // 0%   = 4.00 mA
float max_setpoint;  // 100% = 20.00 mA
int adc_cal_zero;    // ADC value at 0%
int adc_cal_span;    // ADC value at 100%
int device_pan_id;

String ap_ssid;
String ap_password;
String net_ip_str;
String net_gateway_str;
String net_subnet_str;

IPAddress local_ip;
IPAddress gateway;
IPAddress subnet;

String macAddress = "";
int liveHardwareADC = 0;
float currentRawValue = 0.0;
float currentPercent = 0.0;
float current_mA = 4.0;
String lastPayloadSent = "";
unsigned long lastTxTime = 0;
unsigned long packetCounter = 0;
#define OLED_UPDATE_MS 250

bool pendingRestart = false;
unsigned long restartTime = 0;

// =======================================================================
// 3. EMBEDDED TRANSMITTER SCADA WEB DASHBOARD (HTML/JS/CSS)
// =======================================================================
const char* tx_html_page PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>RadioGeet - Transmitter SCADA</title>
  <style>
    :root { 
      --bg: #f8fafc; --sidebar: #ffffff; --card: #ffffff; 
      --text: #0f172a; --text-muted: #64748b; 
      --primary: #1c45df; --primary-hover: #1533a6; 
      --border: #e2e8f0; --success: #10b981; --danger: #ef4444; --warning: #f59e0b;
    }
    body { font-family: 'Segoe UI', Tahoma, sans-serif; background: var(--bg); color: var(--text); margin: 0; display: flex; height: 100vh; overflow: hidden; }
    
    .sidebar { width: 280px; background: var(--sidebar); border-right: 1px solid var(--border); display: flex; flex-direction: column; transition: width 0.3s ease; overflow: hidden; white-space: nowrap; position: relative; z-index: 10; box-shadow: 2px 0 10px rgba(0,0,0,0.03); }
    .sidebar.collapsed { width: 75px; }
    
    .sidebar-header { display: flex; align-items: center; justify-content: space-between; padding: 18px 16px; border-bottom: 1px solid var(--border); height: 75px; box-sizing: border-box; }
    .sidebar.collapsed .sidebar-header { justify-content: center; padding: 18px 0; }
    
    .header-text { display: flex; flex-direction: column; }
    .header-text h2 { margin: 0; color: var(--primary); font-size: 19px; font-weight: 700; letter-spacing: 0.5px; }
    .header-text p { margin: 3px 0 0; font-size: 11px; color: var(--text-muted); font-weight: 600; text-transform: uppercase; }
    .sidebar.collapsed .header-text { display: none; }
    
    .toggle-btn { color: var(--text); width: 38px; height: 38px; border-radius: 8px; display: flex; align-items: center; justify-content: center; cursor: pointer; transition: 0.2s; border: 1px solid var(--border); background: #f8fafc; flex-shrink: 0; }
    .toggle-btn:hover { background: #ede9fe; color: var(--primary); border-color: var(--primary); }
    .sidebar.collapsed .toggle-btn { display: flex !important; }

    .nav-menu { list-style: none; padding: 0; margin: 15px 0; }
    .nav-item { padding: 16px 20px; font-weight: 600; cursor: pointer; border-left: 4px solid transparent; color: var(--text-muted); transition: all 0.2s ease; display: flex; align-items: center; }
    .nav-item:hover, .nav-item.active { background: #f1f5f9; border-left-color: var(--primary); color: var(--primary); }
    .nav-icon { min-width: 35px; font-size: 18px; font-weight: bold; text-align: center; }
    .sidebar.collapsed .nav-text { display: none; }

    .main-wrapper { flex: 1; display: flex; flex-direction: column; overflow-y: auto; position: relative; }
    .main-content { padding: 35px 35px 10px 35px; flex: 1 0 auto; }
    .footer { text-align: center; padding: 18px; color: #94a3b8; font-size: 13px; font-weight: 500; border-top: 1px solid var(--border); margin-top: auto; flex-shrink: 0; }

    .tab-pane { display: none; animation: fadeIn 0.3s ease; }
    .tab-pane.active { display: block; }
    @keyframes fadeIn { from { opacity: 0; transform: translateY(10px); } to { opacity: 1; transform: translateY(0); } }
    
    .header-title { font-size: 26px; margin-top: 0; margin-bottom: 25px; color: var(--text); font-weight: 600; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 25px; }
    .card { background: var(--card); padding: 25px; border-radius: 12px; border: 1px solid var(--border); box-shadow: 0 4px 15px rgba(0,0,0,0.03); }
    .card-title { margin-top: 0; color: var(--text-muted); font-size: 13px; text-transform: uppercase; letter-spacing: 1px; border-bottom: 1px solid var(--border); padding-bottom: 10px; margin-bottom: 20px;}

    /* 3D Tank View */
    .tank-wrapper { display: flex; justify-content: center; margin: 20px 0; }
    .tank-3d { width: 130px; height: 250px; border: 4px solid #cbd5e1; border-radius: 12px 12px 28px 28px; position: relative; overflow: hidden; background: #f8fafc; box-shadow: inset 0 0 20px rgba(0,0,0,0.08); }
    .tank-liquid { position: absolute; bottom: 0; width: 100%; height: 0%; background: linear-gradient(to bottom, #3b82f6, var(--primary)); transition: height 1s ease-in-out, background 0.5s; }
    .tank-liquid::before { content: ''; position: absolute; top: -10px; left: 0; width: 100%; height: 20px; background: rgba(255,255,255,0.4); border-radius: 50%; } 
    .tank-glass { position: absolute; top: 0; left: 10%; width: 25%; height: 100%; background: linear-gradient(to right, rgba(255,255,255,0.5), transparent); pointer-events: none; }

    .value { font-size: 42px; font-weight: 700; margin: 8px 0; color: var(--primary); }
    .badge { padding: 5px 12px; border-radius: 20px; font-weight: 700; font-size: 11px; display: inline-block; }
    .badge.success { background: #dcfce7; color: var(--success); }
    .badge.info { background: #eff6ff; color: var(--primary); border: 1px solid #bfdbfe; }

    .payload-box { background: #0f172a; color: #38bdf8; font-family: monospace; font-size: 14px; padding: 12px; border-radius: 8px; word-break: break-all; margin-top: 10px; }
    
    .form-group { margin-bottom: 18px; }
    label { display: block; font-weight: 600; font-size: 13px; margin-bottom: 6px; color: var(--text); }
    input[type="text"], input[type="number"], select { width: 100%; padding: 11px; border: 1px solid var(--border); border-radius: 8px; box-sizing: border-box; font-family: inherit; font-size: 14px; outline: none; transition: 0.2s; }
    input:focus, select:focus { border-color: var(--primary); box-shadow: 0 0 0 3px rgba(28, 69, 223, 0.1); }
    button { background: var(--primary); color: white; border: none; padding: 11px 22px; border-radius: 8px; cursor: pointer; font-weight: 600; font-size: 14px; transition: 0.2s; }
    button:hover { background: var(--primary-hover); }

    table { width: 100%; border-collapse: collapse; margin-top: 10px; }
    th, td { padding: 12px; border-bottom: 1px solid var(--border); text-align: left; font-size: 13px; }
    th { color: var(--text-muted); font-weight: 600; background: #f8fafc; }
    
    #toast { visibility: hidden; min-width: 250px; background-color: #1e293b; color: #fff; text-align: center; border-radius: 8px; padding: 14px; position: fixed; z-index: 100; bottom: 25px; right: 25px; font-weight: 500; font-size: 13px; opacity: 0; transition: opacity 0.3s, visibility 0.3s; box-shadow: 0 5px 15px rgba(0,0,0,0.2); }
    #toast.show { visibility: visible; opacity: 1; }
  </style>
</head>
<body>
  
  <div class="sidebar" id="sidebar">
    <div class="sidebar-header">
      <div class="header-text">
        <h2>RadioGeet</h2>
        <p>Field Station TX</p>
      </div>
      <div class="toggle-btn" onclick="toggleSidebar()" title="Toggle Sidebar">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><path d="M3 12h18M3 6h18M3 18h18"/></svg>
      </div>
    </div>
    
    <ul class="nav-menu">
      <li class="nav-item active" onclick="switchTab('dashboard', this)">
        <span class="nav-icon">📊</span><span class="nav-text">Live Dashboard</span>
      </li>
      <li class="nav-item" onclick="switchTab('calibration', this)">
        <span class="nav-icon">⚙️</span><span class="nav-text">Sensor Calibration</span>
      </li>
      <li class="nav-item" onclick="switchTab('system', this)">
        <span class="nav-icon">🔧</span><span class="nav-text">System & Network</span>
      </li>
    </ul>
  </div>

  <div class="main-wrapper">
    <div class="main-content">
      
      <!-- TAB 1: LIVE TRANSMITTER DASHBOARD -->
      <div id="dashboard" class="tab-pane active">
        <h1 class="header-title">Field Transmitter Telemetry</h1>

        <div class="grid">
          <!-- 3D Tank Card -->
          <div class="card" style="text-align: center;">
            <h3 class="card-title">Tank Fill Level (3D View)</h3>
            <div class="tank-wrapper">
              <div class="tank-3d">
                <div class="tank-liquid" id="tank-liquid"></div>
                <div class="tank-glass"></div>
              </div>
            </div>
            <div class="value" id="val-percent">--%</div>
            <p style="color: var(--text-muted); font-weight: 500;" id="val-target-name">Target Node: --</p>
          </div>

          <!-- Live Wired Sensor Card -->
          <div class="card">
            <h3 class="card-title">Live Sensor Reading (Wired)</h3>
            <div class="value" id="val-raw">--</div>
            <p style="color: var(--primary); font-size: 18px; font-weight: 600;" id="val-ma">-- mA Output</p>
            
            <h3 class="card-title" style="margin-top: 25px;">Transmission Diagnostics</h3>
            <p style="display:flex; justify-content:space-between; margin:6px 0;">Packets Sent: <strong id="val-pkts">0</strong></p>
            <p style="display:flex; justify-content:space-between; margin:6px 0;">TX Frequency: <strong id="val-rate">-- ms</strong></p>
            <p style="display:flex; justify-content:space-between; margin:6px 0;">Hardware ADC: <strong id="val-hw-adc" style="color:var(--primary);">-- / 4095</strong></p>
            <p style="display:flex; justify-content:space-between; margin:6px 0;">Radio Status: <span class="badge success">LINK BROADCASTING</span></p>

            <h4 style="margin: 15px 0 5px 0; font-size: 11px; color: var(--text-muted); text-transform: uppercase;">Wireless Packet Broadcast:</h4>
            <div class="payload-box" id="val-payload">ID:--,RAW:--</div>
            <p style="font-size: 11px; color: var(--text-muted); margin-top: 15px; text-align: right;">Updated: <span id="val-time">Never</span></p>
          </div>
        </div>
      </div>

      <!-- TAB 2: SENSOR CALIBRATION & SETPOINTS -->
      <div id="calibration" class="tab-pane">
        <h1 class="header-title">Sensor Calibration, Units & 4-20mA Scaling</h1>
        <div class="grid">
          
          <div class="card">
            <h3 class="card-title">Target Device & Engineering Units</h3>
            <div class="form-group">
              <label>Target Device Node ID (Matches Receiver)</label>
              <input type="text" id="cfg-target" placeholder="TANK-01">
            </div>
            <div class="form-group">
              <label>Engineering Unit</label>
              <select id="cfg-unit">
                <option value="Raw">Raw Value (Direct Hardware)</option>
                <option value="m">Meters (m)</option>
                <option value="cm">Centimeters (cm)</option>
                <option value="mm">Millimeters (mm)</option>
                <option value="L">Liters (L)</option>
                <option value="%">Percentage (%)</option>
                <option value="ft">Feet (ft)</option>
              </select>
            </div>
            <div class="form-group">
              <label>Transmission Interval (ms)</label>
              <input type="number" id="cfg-interval" placeholder="1500">
            </div>
          </div>

          <div class="card">
            <h3 class="card-title">Setpoints (0-100% & 4-20mA Calibration)</h3>
            <div class="form-group">
              <label>Min Setpoint (0% = 4.00 mA)</label>
              <input type="number" step="0.01" id="cfg-min" placeholder="0.00">
            </div>
            <div class="form-group">
              <label>Max Setpoint (100% = 20.00 mA)</label>
              <input type="number" step="0.01" id="cfg-max" placeholder="10.00">
            </div>
            <div class="form-group">
              <label>ADC Zero Calibration (Wire reading at 4mA / 0%)</label>
              <input type="number" id="cfg-adczero" placeholder="0">
            </div>
            <div class="form-group">
              <label>ADC Span Calibration (Wire reading at 20mA / 100%)</label>
              <input type="number" id="cfg-adcspan" placeholder="4095">
            </div>
            <button onclick="requireAuth(saveSensorConfig)">Save Calibration & Scaling</button>
          </div>

        </div>
      </div>

      <!-- TAB 3: SYSTEM & NETWORK CONFIGURATION -->
      <div id="system" class="tab-pane">
        <h1 class="header-title">System & Network Configuration</h1>
        <div class="grid">
          
          <div class="card">
            <h3 class="card-title">Hardware Identity</h3>
            <table style="margin-bottom: 20px;">
              <tr><th>ESP32 MAC Address</th><td id="sys-mac">Loading...</td></tr>
            </table>

            <h3 class="card-title">Transmitter PAN ID Selection</h3>
            <div class="form-group">
              <label>Select Device PAN ID</label>
              <select id="pan-select">
                <option value="4097">PAN ID 1 (0x1001)</option>
                <option value="4098">PAN ID 2 (0x1002)</option>
                <option value="4099">PAN ID 3 (0x1003)</option>
                <option value="4100">PAN ID 4 (0x1004)</option>
                <option value="34969">PAN ID 5 (0x8899)</option>
              </select>
            </div>
          </div>

          <div class="card">
            <h3 class="card-title">Field Access Point Wireless Settings</h3>
            <div class="form-group"><label>Field Network Name (SSID)</label><input type="text" id="wifi-ssid"></div>
            <div class="form-group"><label>WiFi Password (Min 8 Chars)</label><input type="text" id="wifi-pass"></div>
            <div class="form-group"><label>Static IP Address</label><input type="text" id="net-ip"></div>
            <div class="form-group"><label>Gateway IP</label><input type="text" id="net-gw"></div>
            <div class="form-group"><label>Subnet Mask</label><input type="text" id="net-sub"></div>
            <p style="color: var(--danger); font-size: 12px; font-weight: 600;">*Changing network settings will restart the ESP32.</p>
            <button onclick="requireAuth(saveSystemConfig)">Save Settings & Restart</button>
          </div>

        </div>
      </div>

    </div>

    <div class="footer">
      &copy; 2026 RadioGeet Digital Pvt. Ltd. All Rights Reserved.
    </div>
  </div>

  <div id="toast">Settings Saved successfully!</div>

  <script>
    function toggleSidebar() {
      document.getElementById('sidebar').classList.toggle('collapsed');
    }

    function showToast(msg) {
      const toast = document.getElementById("toast");
      toast.innerText = msg;
      toast.className = "show";
      setTimeout(() => { toast.className = toast.className.replace("show", ""); }, 3000);
    }

    function requireAuth(actionFunction) {
      const pwd = prompt("Security Lock: Enter Admin Password:");
      if (pwd === "admin123") {
        actionFunction();
      } else if (pwd !== null) {
        alert("Authentication Failed! Incorrect Password.");
      }
    }

    function switchTab(tabId, elem) {
      document.querySelectorAll('.tab-pane').forEach(tab => tab.classList.remove('active'));
      document.querySelectorAll('.nav-item').forEach(nav => nav.classList.remove('active'));
      document.getElementById(tabId).classList.add('active');
      elem.classList.add('active');
    }

    function saveSensorConfig() {
      const target = document.getElementById('cfg-target').value;
      const unit = document.getElementById('cfg-unit').value;
      const interval = document.getElementById('cfg-interval').value;
      const minVal = document.getElementById('cfg-min').value;
      const maxVal = document.getElementById('cfg-max').value;
      const adcZero = document.getElementById('cfg-adczero').value;
      const adcSpan = document.getElementById('cfg-adcspan').value;

      fetch(`/api/tx/savesensor?target=${encodeURIComponent(target)}&unit=${encodeURIComponent(unit)}&int=${interval}&min=${minVal}&max=${maxVal}&zero=${adcZero}&span=${adcSpan}`)
        .then(r => r.text())
        .then(res => { showToast(res); fetchTelemetry(); });
    }

    function saveSystemConfig() {
      const ssid = document.getElementById('wifi-ssid').value;
      const pass = document.getElementById('wifi-pass').value;
      const pan  = document.getElementById('pan-select').value;
      const ip   = document.getElementById('net-ip').value;
      const gw   = document.getElementById('net-gw').value;
      const sub  = document.getElementById('net-sub').value;

      if(pass.length < 8) return alert("Password must be at least 8 characters.");
      
      fetch(`/api/tx/savesystem?ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}&pan=${pan}&ip=${ip}&gw=${gw}&sub=${sub}`)
        .then(r => r.text())
        .then(res => { 
           showToast(res); 
           setTimeout(() => { alert("Transmitter Rebooting with new parameters..."); location.reload(); }, 2000);
        });
    }

    function fetchTelemetry() {
      fetch('/api/tx/telemetry').then(r => r.json()).then(data => {
        if (data.unit === 'Raw') {
          document.getElementById('val-raw').innerText = Math.round(data.raw) + ' (Raw ADC)';
        } else {
          document.getElementById('val-raw').innerText = data.raw.toFixed(2) + ' ' + data.unit;
        }

        document.getElementById('val-percent').innerText = data.percent.toFixed(1) + '%';
        document.getElementById('val-ma').innerText = data.ma.toFixed(2) + ' mA Output';
        document.getElementById('val-target-name').innerText = 'Target Node: ' + data.target;
        document.getElementById('val-rate').innerText = data.interval + ' ms';
        document.getElementById('val-pkts').innerText = data.packets;
        document.getElementById('val-hw-adc').innerText = data.hw_adc + ' / 4095';
        document.getElementById('val-payload').innerText = data.payload;
        document.getElementById('val-time').innerText = new Date().toLocaleTimeString();

        const liquid = document.getElementById('tank-liquid');
        liquid.style.height = data.percent + '%';
        if (data.percent >= 85) liquid.style.background = 'linear-gradient(to bottom, #f87171, #dc2626)';
        else if (data.percent <= 15) liquid.style.background = 'linear-gradient(to bottom, #fbbf24, #d97706)';
        else liquid.style.background = 'linear-gradient(to bottom, #3b82f6, var(--primary))';

        document.getElementById('sys-mac').innerText = data.mac;

        if (document.getElementById('cfg-target').value === "") {
          document.getElementById('cfg-target').value = data.target;
          document.getElementById('cfg-unit').value = data.unit;
          document.getElementById('cfg-interval').value = data.interval;
          document.getElementById('cfg-min').value = data.min;
          document.getElementById('cfg-max').value = data.max;
          document.getElementById('cfg-adczero').value = data.adczero;
          document.getElementById('cfg-adcspan').value = data.adcspan;

          document.getElementById('wifi-ssid').value = data.ssid;
          document.getElementById('wifi-pass').value = data.pass;
          document.getElementById('pan-select').value = data.pan;
          document.getElementById('net-ip').value = data.ip;
          document.getElementById('net-gw').value = data.gw;
          document.getElementById('net-sub').value = data.sub;
        }
      });
    }

    setInterval(fetchTelemetry, 1000);
    fetchTelemetry();
  </script>
</body>
</html>
)rawliteral";

// =======================================================================
// 4. API ROUTING LOGIC
// =======================================================================
void handleRoot() { server.send(200, "text/html", tx_html_page); }

void handleTelemetryAPI() {
  StaticJsonDocument<1536> doc;
  
  doc["raw"] = currentRawValue;
  doc["percent"] = currentPercent;
  doc["ma"] = current_mA;
  doc["hw_adc"] = liveHardwareADC;
  doc["target"] = target_device_id;
  doc["unit"] = sensor_unit;
  doc["interval"] = tx_interval_ms;
  doc["packets"] = packetCounter;
  doc["payload"] = lastPayloadSent;
  
  doc["min"] = min_setpoint;
  doc["max"] = max_setpoint;
  doc["adczero"] = adc_cal_zero;
  doc["adcspan"] = adc_cal_span;
  
  doc["mac"] = macAddress;
  doc["pan"] = device_pan_id;
  doc["ssid"] = ap_ssid;
  doc["pass"] = ap_password;
  doc["ip"] = net_ip_str;
  doc["gw"] = net_gateway_str;
  doc["sub"] = net_subnet_str;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSaveSensor() {
  target_device_id = server.arg("target");
  sensor_unit = server.arg("unit");
  tx_interval_ms = server.arg("int").toInt();
  min_setpoint = server.arg("min").toFloat();
  max_setpoint = server.arg("max").toFloat();
  adc_cal_zero = server.arg("zero").toInt();
  adc_cal_span = server.arg("span").toInt();

  prefs.putString("target_id", target_device_id);
  prefs.putString("unit", sensor_unit);
  prefs.putInt("tx_int", tx_interval_ms);
  prefs.putFloat("min_sp", min_setpoint);
  prefs.putFloat("max_sp", max_setpoint);
  prefs.putInt("adc_zero", adc_cal_zero);
  prefs.putInt("adc_span", adc_cal_span);

  server.send(200, "text/plain", "Sensor Calibration Saved!");
}

void handleSaveSystem() {
  ap_ssid = server.arg("ssid");
  ap_password = server.arg("pass");
  device_pan_id = server.arg("pan").toInt();
  net_ip_str = server.arg("ip");
  net_gateway_str = server.arg("gw");
  net_subnet_str = server.arg("sub");

  prefs.putString("ssid", ap_ssid);
  prefs.putString("pass", ap_password);
  prefs.putInt("pan", device_pan_id);
  prefs.putString("ip", net_ip_str);
  prefs.putString("gw", net_gateway_str);
  prefs.putString("sub", net_subnet_str);

  server.send(200, "text/plain", "Transmitter Settings Saved. Rebooting...");

  pendingRestart = true;
  restartTime = millis() + 2000;
}

// =======================================================================
// 5. INDUSTRIAL 32-SAMPLE TRIMMED-MEAN NOISE FILTER (NO RANDOM SIMULATION)
// =======================================================================
void sampleWiredSensor() {
  const int SAMPLES = 32;
  int rawReadings[SAMPLES];

  // Collect 32 samples with delay to avoid noise spikes
  for (int i = 0; i < SAMPLES; i++) {
    rawReadings[i] = analogRead(WIRED_SENSOR_PIN);
    delayMicroseconds(250);
  }

  // Simple bubble sort to discard outliers
  for (int i = 0; i < SAMPLES - 1; i++) {
    for (int j = i + 1; j < SAMPLES; j++) {
      if (rawReadings[i] > rawReadings[j]) {
        int temp = rawReadings[i];
        rawReadings[i] = rawReadings[j];
        rawReadings[j] = temp;
      }
    }
  }

  // Discard 4 lowest and 4 highest readings; average the middle 24
  long sum = 0;
  for (int i = 4; i < SAMPLES - 4; i++) {
    sum += rawReadings[i];
  }
  int filteredADC = sum / (SAMPLES - 8);
  liveHardwareADC = filteredADC;

  // Zero-Cutoff / Noise Threshold:
  // If pin has floating background noise (< 40 counts above zero calibration), clamp to zero
  if (filteredADC <= (adc_cal_zero + 35)) {
    filteredADC = adc_cal_zero;
  }

  // Calculate fraction based on calibrated zero and span
  float fraction = 0.0;
  if (adc_cal_span > adc_cal_zero) {
    fraction = (float)(filteredADC - adc_cal_zero) / (float)(adc_cal_span - adc_cal_zero);
  }
  if (fraction < 0.0) fraction = 0.0;
  if (fraction > 1.0) fraction = 1.0;

  // Percentage & 4-20mA Calculation
  currentPercent = fraction * 100.0;
  current_mA = 4.0 + (16.0 * fraction);

  // Raw Reading Determination:
  // If user selected "Raw", output the direct hardware ADC count without applying setpoint math!
  if (sensor_unit == "Raw") {
    currentRawValue = (float)filteredADC;
  } else {
    currentRawValue = min_setpoint + (fraction * (max_setpoint - min_setpoint));
  }
}

// =======================================================================
// 6. OLED DISPLAY UPDATE (Pins 39 & 38)
// =======================================================================
void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("RADIOGEET - TX PANEL");
  display.printf("PAN: 0x%04X\n", device_pan_id);
  display.drawLine(0, 18, 128, 18, SSD1306_WHITE);

  display.setCursor(0, 22);
  display.printf("NODE : %s\n", target_device_id.c_str());
  if (sensor_unit == "Raw") {
    display.printf("RAW  : %.0f (ADC)\n", currentRawValue);
  } else {
    display.printf("RAW  : %.2f %s\n", currentRawValue, sensor_unit.c_str());
  }
  display.printf("LVL  : %.1f %%\n", currentPercent);
  display.printf("CURR : %.2f mA\n", current_mA);

  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  display.setCursor(0, 55);
  display.printf("PKT SENT: %lu", packetCounter);
  display.display();
}

// =======================================================================
// 7. SYSTEM SETUP
// =======================================================================
void setup() {
  Serial.begin(115200);

  // Configure ADC1 Channel for Sensor Wire Input
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(WIRED_SENSOR_PIN, INPUT);

  // Load Preferences
  prefs.begin("rg-tx", false);
  target_device_id = prefs.getString("target_id", "TANK-01");
  sensor_unit = prefs.getString("unit", "m");
  tx_interval_ms = prefs.getInt("tx_int", 1500);
  min_setpoint = prefs.getFloat("min_sp", 0.0);
  max_setpoint = prefs.getFloat("max_sp", 10.0);
  adc_cal_zero = prefs.getInt("adc_zero", 0);
  adc_cal_span = prefs.getInt("adc_span", 4095);
  device_pan_id = prefs.getInt("pan", 34969);

  ap_ssid = prefs.getString("ssid", "RadioGeet_TX_FIELD");
  ap_password = prefs.getString("pass", "12345678"); // Default 12345678
  net_ip_str = prefs.getString("ip", "192.168.4.1");
  net_gateway_str = prefs.getString("gw", "192.168.4.1");
  net_subnet_str = prefs.getString("sub", "255.255.255.0");

  local_ip.fromString(net_ip_str);
  gateway.fromString(net_gateway_str);
  subnet.fromString(net_subnet_str);

  // Hardware OLED Init (Pins 39 & 38)
  Wire.begin(I2C_SDA, I2C_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(5, 20);
  display.println("RADIOGEET DIGITAL");
  display.setCursor(5, 35);
  display.println("TX FIELD STATION...");
  display.display();

  // LoRa Radio UART Init (Pins 16 & 17)
  Serial1.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  // WiFi AP Setup
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
  macAddress = WiFi.softAPmacAddress();

  // Web Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/tx/telemetry", HTTP_GET, handleTelemetryAPI);
  server.on("/api/tx/savesensor", HTTP_GET, handleSaveSensor);
  server.on("/api/tx/savesystem", HTTP_GET, handleSaveSystem);
  server.begin();

  updateOLED();
}

// =======================================================================
// 8. MAIN LOOP
// =======================================================================
void loop() {
  server.handleClient();

  // Reboot Handler
  if (pendingRestart && millis() > restartTime) {
    display.clearDisplay();
    display.setCursor(15, 30);
    display.print("REBOOTING...");
    display.display();
    delay(500);
    ESP.restart();
  }

  // Periodic Transmission Loop
  if (millis() - lastTxTime >= (unsigned long)tx_interval_ms) {
    lastTxTime = millis();
    sampleWiredSensor(); // Multi-sample filtered real wire read

    // Packet Protocol:
    // Format: "ID:<NODE>,RAW:<VAL>,UNIT:<U>,PCT:<P>,MA:<M>\n"
    lastPayloadSent = "ID:" + target_device_id + 
                      ",RAW:" + String(currentRawValue, 2) + 
                      ",UNIT:" + sensor_unit + 
                      ",PCT:" + String(currentPercent, 1) + 
                      ",MA:" + String(current_mA, 2);

    String packet = lastPayloadSent + "\n";
    Serial1.print(packet);
    Serial.print("LoRa TX -> " + packet);

    packetCounter++;
  }

  // OLED Screen Refresh (250ms Rate-Limited)
  static unsigned long last_oled = 0;
  if (millis() - last_oled > OLED_UPDATE_MS) {
    updateOLED();
    last_oled = millis();
  }

  delay(1);
}
