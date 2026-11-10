/*
  RadioGeet Digital Pvt. Ltd.
  TATA STEEL – SPONGE IRON, JODA
  Control Room Station: SCADA Receiver Panel (LoRa Receiver)
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

const int RELAY_PINS[4] = {4, 5, 6, 7}; // Relay 1 (Hooter/Alarm), Relay 2, Relay 3, Relay 4
bool relayState[4] = {false, false, false, false};

// =======================================================================
// 2. PREFERENCES & SYSTEM VARIABLES
// =======================================================================
Preferences prefs;

String ap_ssid;
String ap_password;
String net_ip_str;
String net_gateway_str;
String net_subnet_str;
int device_pan_id;

IPAddress local_ip;
IPAddress gateway;
IPAddress subnet;

#define MAX_DEVICES 8
String macAddress = "";
WebServer server(80);
const unsigned long COMM_TIMEOUT_MS = 15000;
#define OLED_UPDATE_MS 250

bool pendingRestart = false;
unsigned long restartTime = 0;

struct Device {
  bool active;
  String id;
  String name;
  String unit;
  float minSetpoint;  // 0%   = 4.00 mA
  float maxSetpoint;  // 100% = 20.00 mA
  float rawValue;
  float percent;
  float output_mA;
  bool commActive;
  unsigned long lastPacketTime;
};

Device devices[MAX_DEVICES];

// =======================================================================
// 3. EMBEDDED SCADA WEB DASHBOARD (HTML/JS/CSS)
// =======================================================================
const char* html_page PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>RadioGeet Digital - SCADA Panel</title>
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
    
    /* 3D Animated Tank */
    .tank-wrapper { display: flex; justify-content: center; margin: 20px 0; }
    .tank-3d { width: 130px; height: 250px; border: 4px solid #cbd5e1; border-radius: 12px 12px 28px 28px; position: relative; overflow: hidden; background: #f8fafc; box-shadow: inset 0 0 20px rgba(0,0,0,0.08); }
    .tank-liquid { position: absolute; bottom: 0; width: 100%; height: 0%; background: linear-gradient(to bottom, #3b82f6, var(--primary)); transition: height 1s ease-in-out, background 0.5s; }
    .tank-liquid::before { content: ''; position: absolute; top: -10px; left: 0; width: 100%; height: 20px; background: rgba(255,255,255,0.4); border-radius: 50%; } 
    .tank-glass { position: absolute; top: 0; left: 10%; width: 25%; height: 100%; background: linear-gradient(to right, rgba(255,255,255,0.5), transparent); pointer-events: none; }

    .value { font-size: 42px; font-weight: 700; margin: 8px 0; color: var(--primary); }
    .badge { padding: 5px 12px; border-radius: 20px; font-weight: 700; font-size: 11px; display: inline-block; }
    .badge.success { background: #dcfce7; color: var(--success); }
    .badge.danger { background: #fee2e2; color: var(--danger); }
    .badge.info { background: #eff6ff; color: var(--primary); border: 1px solid #bfdbfe; }
    
    .form-group { margin-bottom: 18px; }
    label { display: block; font-weight: 600; font-size: 13px; margin-bottom: 6px; color: var(--text); }
    input[type="text"], input[type="number"], select { width: 100%; padding: 11px; border: 1px solid var(--border); border-radius: 8px; box-sizing: border-box; font-family: inherit; font-size: 14px; outline: none; transition: 0.2s; }
    input:focus, select:focus { border-color: var(--primary); box-shadow: 0 0 0 3px rgba(28, 69, 223, 0.1); }
    button { background: var(--primary); color: white; border: none; padding: 11px 22px; border-radius: 8px; cursor: pointer; font-weight: 600; font-size: 14px; transition: 0.2s; }
    button:hover { background: var(--primary-hover); }
    button.btn-danger { background: var(--danger); }
    button.btn-danger:hover { background: #dc2626; }
    
    .relay-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 15px; }
    .relay-card { background: var(--bg); border: 1px solid var(--border); padding: 20px; border-radius: 10px; display: flex; flex-direction: column; align-items: center; text-align: center; }
    .relay-indicator { width: 15px; height: 15px; border-radius: 50%; background: #94a3b8; margin-bottom: 12px; }
    .relay-indicator.active { background: var(--success); box-shadow: 0 0 10px var(--success); }
    
    .switch { position: relative; display: inline-block; width: 48px; height: 26px; margin-top: 15px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #cbd5e1; transition: .4s; border-radius: 34px; }
    .slider:before { position: absolute; content: ""; height: 18px; width: 18px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
    input:checked + .slider { background-color: var(--primary); }
    input:checked + .slider:before { transform: translateX(22px); }

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
        <p>Digital Pvt. Ltd.</p>
      </div>
      <div class="toggle-btn" onclick="toggleSidebar()" title="Toggle Sidebar">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><path d="M3 12h18M3 6h18M3 18h18"/></svg>
      </div>
    </div>
    
    <ul class="nav-menu">
      <li class="nav-item active" onclick="switchTab('dashboard', this)">
        <span class="nav-icon">📊</span><span class="nav-text">Live Dashboard</span>
      </li>
      <li class="nav-item" onclick="switchTab('config', this)">
        <span class="nav-icon">⚡</span><span class="nav-text">Relays & Control</span>
      </li>
      <li class="nav-item" onclick="switchTab('settings', this)">
        <span class="nav-icon">⚙️</span><span class="nav-text">Device Settings</span>
      </li>
      <li class="nav-item" onclick="switchTab('system', this)">
        <span class="nav-icon">🔧</span><span class="nav-text">System Configuration</span>
      </li>
    </ul>
  </div>

  <div class="main-wrapper">
    <div class="main-content">
      
      <!-- DASHBOARD TAB -->
      <div id="dashboard" class="tab-pane active">
        <h1 class="header-title">Live Tank Monitoring</h1>
        
        <div style="margin-bottom: 25px; max-width: 350px;">
          <label>Select Device to View:</label>
          <select id="view-device-select" onchange="changeDashboardSelection()"></select>
        </div>

        <div id="dashboard-display-container"></div>
      </div>

      <!-- RELAYS CONTROL TAB -->
      <div id="config" class="tab-pane">
        <h1 class="header-title">Equipment & Relay Outputs</h1>
        <div class="relay-grid">
          
          <div class="relay-card">
            <div class="relay-indicator" id="ind-r0"></div>
            <h3 style="margin:0;">Relay 1</h3>
            <p style="font-size:12px; color:var(--text-muted);">Auto Alarm / Hooter</p>
            <label class="switch"><input type="checkbox" id="r0" onchange="handleRelayToggle(0, this)"><span class="slider"></span></label>
          </div>

          <div class="relay-card">
            <div class="relay-indicator" id="ind-r1"></div>
            <h3 style="margin:0;">Relay 2</h3>
            <p style="font-size:12px; color:var(--text-muted);">Transfer Pump A</p>
            <label class="switch"><input type="checkbox" id="r1" onchange="handleRelayToggle(1, this)"><span class="slider"></span></label>
          </div>

          <div class="relay-card">
            <div class="relay-indicator" id="ind-r2"></div>
            <h3 style="margin:0;">Relay 3</h3>
            <p style="font-size:12px; color:var(--text-muted);">Transfer Pump B</p>
            <label class="switch"><input type="checkbox" id="r2" onchange="handleRelayToggle(2, this)"><span class="slider"></span></label>
          </div>

          <div class="relay-card">
            <div class="relay-indicator" id="ind-r3"></div>
            <h3 style="margin:0;">Relay 4</h3>
            <p style="font-size:12px; color:var(--text-muted);">Auxiliary Output</p>
            <label class="switch"><input type="checkbox" id="r3" onchange="handleRelayToggle(3, this)"><span class="slider"></span></label>
          </div>

        </div>
      </div>

      <!-- DEVICE SETTINGS TAB -->
      <div id="settings" class="tab-pane">
        <h1 class="header-title">Device Setpoints, Units & Registration</h1>
        <div class="grid">
          
          <div class="card">
            <h3 class="card-title">Configure Setpoints & Engineering Unit</h3>
            <div class="form-group"><label>Select Target Device</label><select id="config-device-select" onchange="loadDeviceConfig()"></select></div>
            <div class="form-group">
              <label>Engineering Unit</label>
              <select id="conf-unit">
                <option value="Raw">Raw Value (Direct Hardware)</option>
                <option value="m">Meters (m)</option>
                <option value="cm">Centimeters (cm)</option>
                <option value="mm">Millimeters (mm)</option>
                <option value="L">Liters (L)</option>
                <option value="%">Percentage (%)</option>
                <option value="ft">Feet (ft)</option>
              </select>
            </div>
            <div class="form-group"><label>Min Setpoint (0% = 4.00 mA)</label><input type="number" step="0.01" id="conf-min" placeholder="0.00"></div>
            <div class="form-group"><label>Max Setpoint (100% = 20.00 mA)</label><input type="number" step="0.01" id="conf-max" placeholder="10.00"></div>
            <button onclick="requireAuth(saveDeviceConfig)">Save Calibration</button>
          </div>

          <div class="card">
            <h3 class="card-title">Register New Field Device</h3>
            <div class="form-group"><label>Device Node ID</label><input type="text" id="add-id" placeholder="TANK-01"></div>
            <div class="form-group"><label>Descriptive Location / Name</label><input type="text" id="add-name" placeholder="Tower Tank Main"></div>
            <button onclick="requireAuth(addDevice)">Register Device</button>
          </div>
        </div>

        <div class="card" style="margin-top: 25px;">
          <h3 class="card-title">Configured Field Devices</h3>
          <table id="device-table">
            <thead><tr><th>Node ID</th><th>Name</th><th>Unit</th><th>Min (4mA)</th><th>Max (20mA)</th><th>Action</th></tr></thead>
            <tbody></tbody>
          </table>
        </div>
      </div>

      <!-- SYSTEM & NETWORK CONFIGURATION TAB -->
      <div id="system" class="tab-pane">
        <h1 class="header-title">System & Network Configuration</h1>
        <div class="grid">
          
          <div class="card">
            <h3 class="card-title">Hardware Identity</h3>
            <table style="margin-bottom: 20px;">
              <tr><th>ESP32 MAC Address</th><td id="sys-mac">Loading...</td></tr>
            </table>

            <h3 class="card-title">Device PAN ID Selection</h3>
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
            <h3 class="card-title">Network & Wireless Settings</h3>
            <div class="form-group"><label>Network Name (SSID)</label><input type="text" id="wifi-ssid"></div>
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
    let globalData = [];
    let currentSelection = 'all';

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

    function handleRelayToggle(id, checkbox) {
      const targetState = checkbox.checked;
      checkbox.checked = !targetState; 
      
      const pwd = prompt("Security Lock: Enter Admin Password to toggle Relay:");
      if (pwd === "admin123") {
         checkbox.checked = targetState;
         fetch(`/api/relay?id=${id}&state=${targetState ? 1 : 0}`)
           .then(() => showToast(`Relay ${id+1} turned ${targetState ? 'ON' : 'OFF'}`))
           .catch(e => console.error(e));
      } else if(pwd !== null) {
         alert("Authentication Failed! Incorrect Password.");
      }
    }

    function saveSystemConfig() {
      const ssid = document.getElementById('wifi-ssid').value;
      const pass = document.getElementById('wifi-pass').value;
      const pan  = document.getElementById('pan-select').value;
      const ip   = document.getElementById('net-ip').value;
      const gw   = document.getElementById('net-gw').value;
      const sub  = document.getElementById('net-sub').value;

      if(pass.length < 8) return alert("Password must be at least 8 characters.");
      
      fetch(`/api/system/update?ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}&pan=${pan}&ip=${ip}&gw=${gw}&sub=${sub}`)
        .then(r => r.text())
        .then(res => { 
           showToast(res); 
           setTimeout(() => { alert("ESP32 Rebooting with new parameters..."); location.reload(); }, 2000);
        });
    }

    function changeDashboardSelection() {
      currentSelection = document.getElementById('view-device-select').value;
      buildDashboardDOM();
      updateDashboardData();
    }

    function renderDeviceUI() {
      const viewSelect = document.getElementById('view-device-select');
      const confSelect = document.getElementById('config-device-select');
      const tableBody = document.querySelector('#device-table tbody');
      
      const prevView = viewSelect.value;
      const prevConf = confSelect.value;

      viewSelect.innerHTML = '<option value="all">-- ALL TANKS (Overview) --</option>';
      confSelect.innerHTML = '';
      tableBody.innerHTML = '';

      if (globalData.length === 0) {
        tableBody.innerHTML = '<tr><td colspan="6">No active devices registered.</td></tr>';
        buildDashboardDOM();
        return;
      }

      globalData.forEach((d, index) => {
        viewSelect.innerHTML += `<option value="${index}">${d.id} - ${d.name}</option>`;
        confSelect.innerHTML += `<option value="${index}">${d.id} - ${d.name}</option>`;
        tableBody.innerHTML += `
          <tr>
            <td><strong>${d.id}</strong></td>
            <td>${d.name}</td>
            <td><span class="badge info">${d.unit}</span></td>
            <td>${d.minSetpoint.toFixed(2)} ${d.unit}</td>
            <td>${d.maxSetpoint.toFixed(2)} ${d.unit}</td>
            <td><button class="btn-danger" style="padding: 5px 10px; font-size: 11px;" onclick="requireAuth(() => deleteDevice('${d.id}'))">Delete</button></td>
          </tr>`;
      });

      if (prevView && (prevView === 'all' || globalData[prevView])) {
        viewSelect.value = prevView;
        currentSelection = prevView;
      } else {
        viewSelect.value = 'all';
        currentSelection = 'all';
      }

      if (prevConf && globalData[prevConf]) confSelect.value = prevConf;

      buildDashboardDOM();
    }

    function buildDashboardDOM() {
      const container = document.getElementById('dashboard-display-container');
      
      if (globalData.length === 0) {
        container.innerHTML = '<div class="card"><p style="text-align:center; color:var(--text-muted);">No field devices registered yet.</p></div>';
        return;
      }

      if (currentSelection === 'all') {
        let html = '<div class="grid">';
        globalData.forEach((d, idx) => {
          html += `
          <div class="card" style="text-align: center;">
            <h3 class="card-title" style="margin-bottom:12px;">${d.id} (${d.name})</h3>
            <div class="tank-wrapper">
              <div class="tank-3d" style="width:115px; height:210px;">
                <div class="tank-liquid" id="liquid-${idx}"></div>
                <div class="tank-glass"></div>
              </div>
            </div>
            <div class="value" id="percent-${idx}" style="font-size:34px;">--%</div>
            <div style="margin-top: 10px; border-top: 1px solid var(--border); padding-top: 12px; text-align: left;">
              <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:4px;">
                <span style="font-size:13px; color:var(--text-muted); font-weight:600;">Reading:</span>
                <span class="value" id="raw-${idx}" style="font-size:20px; margin:0;">--</span>
              </div>
              <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:4px;">
                <span style="font-size:12px; color:var(--text-muted);">Current Output:</span>
                <span id="ma-${idx}" style="font-weight:700; color:var(--primary); font-size:13px;">-- mA</span>
              </div>
              <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:4px;">
                <span style="font-size:12px; color:var(--text-muted);">Comms:</span>
                <span id="comm-${idx}" class="badge danger">NO DATA</span>
              </div>
              <div style="display:flex; justify-content:space-between; align-items:center;">
                <span style="font-size:12px; color:var(--text-muted);">Status:</span>
                <span id="alarm-${idx}" class="badge success">NORMAL</span>
              </div>
            </div>
          </div>`;
        });
        html += '</div>';
        container.innerHTML = html;
      } else {
        const idx = parseInt(currentSelection);
        const d = globalData[idx];
        if(!d) return;
        container.innerHTML = `
        <div class="grid">
          <div class="card" style="text-align: center;">
            <h3 class="card-title">Tank Fill Level (3D View)</h3>
            <div class="tank-wrapper">
              <div class="tank-3d">
                <div class="tank-liquid" id="liquid-${idx}"></div>
                <div class="tank-glass"></div>
              </div>
            </div>
            <div class="value" id="percent-${idx}">--%</div>
            <p style="color: var(--text-muted); font-weight: 500;">${d.id} (${d.name})</p>
          </div>
          
          <div class="card">
            <h3 class="card-title">Live Sensor Reading</h3>
            <div class="value" id="raw-${idx}">--</div>
            <p style="color: var(--primary); font-size: 18px; font-weight: 600;" id="ma-${idx}">-- mA Output</p>
            
            <h3 class="card-title" style="margin-top: 35px;">System Diagnostics</h3>
            <p style="display: flex; justify-content: space-between;">Comms Status: <span id="comm-${idx}" class="badge danger">NO DATA</span></p>
            <p style="display: flex; justify-content: space-between;">Alarm Condition: <span id="alarm-${idx}" class="badge success">NORMAL</span></p>
            <p style="font-size: 12px; color: var(--text-muted); margin-top: 25px; text-align: right;" id="scale-info-${idx}">Scale Info</p>
          </div>
        </div>`;
      }
    }

    function updateDashboardData() {
      if (!globalData || globalData.length === 0) return;

      globalData.forEach((d, idx) => {
        const liquid = document.getElementById(`liquid-${idx}`);
        const percentTxt = document.getElementById(`percent-${idx}`);
        const rawTxt = document.getElementById(`raw-${idx}`);
        const maTxt = document.getElementById(`ma-${idx}`);
        const commBadge = document.getElementById(`comm-${idx}`);
        const alarmBadge = document.getElementById(`alarm-${idx}`);
        const scaleInfo = document.getElementById(`scale-info-${idx}`);

        if (liquid && percentTxt && rawTxt && maTxt) {
          if (d.unit === 'Raw') {
            rawTxt.innerText = Math.round(d.rawValue) + ' (Raw)';
            if (scaleInfo) scaleInfo.innerText = `Raw Hardware Range: ${d.minSetpoint} to ${d.maxSetpoint}`;
          } else {
            rawTxt.innerText = d.rawValue.toFixed(2) + ' ' + d.unit;
            if (scaleInfo) scaleInfo.innerText = `Scale: 0% (${d.minSetpoint} ${d.unit}) to 100% (${d.maxSetpoint} ${d.unit})`;
          }

          percentTxt.innerText = d.percent.toFixed(1) + '%';
          maTxt.innerText = d.output_mA.toFixed(2) + ' mA Output';
          liquid.style.height = d.percent + '%';

          if (d.commActive) {
            commBadge.className = 'badge success'; commBadge.innerText = 'LINK OK';
          } else {
            commBadge.className = 'badge danger'; commBadge.innerText = 'COMM FAIL';
            liquid.style.background = '#94a3b8';
          }

          if (d.rawValue >= d.maxSetpoint && d.commActive) {
            alarmBadge.className = 'badge danger'; alarmBadge.innerText = 'HIGH ALARM';
            liquid.style.background = 'linear-gradient(to bottom, #f87171, #dc2626)';
          } else if (d.rawValue <= d.minSetpoint && d.commActive) {
            alarmBadge.className = 'badge danger'; alarmBadge.innerText = 'LOW ALARM';
            liquid.style.background = 'linear-gradient(to bottom, #fbbf24, #d97706)';
          } else {
            alarmBadge.className = 'badge success'; alarmBadge.innerText = 'NORMAL';
            if (d.commActive) liquid.style.background = 'linear-gradient(to bottom, #3b82f6, var(--primary))';
          }
        }
      });
    }

    function addDevice() {
      const id = document.getElementById('add-id').value;
      const name = document.getElementById('add-name').value;
      fetch(`/api/device/add?id=${encodeURIComponent(id)}&name=${encodeURIComponent(name)}`)
        .then(r => r.text()).then(res => { showToast(res); document.getElementById('add-id').value = ''; fetchState(); });
    }

    function deleteDevice(id) {
      fetch(`/api/device/delete?id=${encodeURIComponent(id)}`).then(r => r.text()).then(res => { showToast(res); fetchState(); });
    }

    function loadDeviceConfig() {
      const idx = document.getElementById('config-device-select').value;
      if(globalData[idx]) {
        document.getElementById('conf-unit').value = globalData[idx].unit;
        document.getElementById('conf-min').value = globalData[idx].minSetpoint;
        document.getElementById('conf-max').value = globalData[idx].maxSetpoint;
      }
    }

    function saveDeviceConfig() {
      const idx = document.getElementById('config-device-select').value;
      if(!globalData[idx]) return;
      const unit = document.getElementById('conf-unit').value;
      const min = document.getElementById('conf-min').value;
      const max = document.getElementById('conf-max').value;
      fetch(`/api/device/config?id=${encodeURIComponent(globalData[idx].id)}&unit=${encodeURIComponent(unit)}&min=${min}&max=${max}`)
        .then(r => r.text()).then(res => { showToast(res); fetchState(); });
    }

    function fetchState() {
      fetch('/api/state').then(r => r.json()).then(data => {
        const oldLen = globalData.length; 
        globalData = data.devices;

        if (oldLen !== globalData.length) { 
          renderDeviceUI(); 
          loadDeviceConfig(); 
        }
        
        updateDashboardData();

        for(let i=0; i<4; i++) {
           let t = document.getElementById('r'+i);
           let ind = document.getElementById('ind-r'+i);
           if(t.checked !== data.relays[i]) t.checked = data.relays[i];
           if(data.relays[i]) ind.classList.add('active'); else ind.classList.remove('active');
        }

        document.getElementById('sys-mac').innerText = data.mac;
        if(document.getElementById('wifi-ssid').value === "") {
          document.getElementById('wifi-ssid').value = data.ssid;
          document.getElementById('wifi-pass').value = data.pass;
          document.getElementById('pan-select').value = data.pan;
          document.getElementById('net-ip').value = data.ip;
          document.getElementById('net-gw').value = data.gw;
          document.getElementById('net-sub').value = data.sub;
        }
      });
    }

    setInterval(fetchState, 1000); 
    fetchState();
  </script>
</body>
</html>
)rawliteral";

// =======================================================================
// 4. API ROUTING LOGIC
// =======================================================================
int findDevice(String id) {
  for (int i = 0; i < MAX_DEVICES; i++) if (devices[i].active && devices[i].id == id) return i; return -1;
}

void handleRoot() { server.send(200, "text/html", html_page); }

void handleStateAPI() {
  StaticJsonDocument<3072> doc;
  
  doc["mac"] = macAddress;
  doc["pan"] = device_pan_id;
  doc["ssid"] = ap_ssid;
  doc["pass"] = ap_password;
  doc["ip"] = net_ip_str;
  doc["gw"] = net_gateway_str;
  doc["sub"] = net_subnet_str;

  JsonArray devs = doc.createNestedArray("devices");
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (devices[i].active) {
      JsonObject obj = devs.createNestedObject();
      obj["id"] = devices[i].id;
      obj["name"] = devices[i].name;
      obj["unit"] = devices[i].unit;
      obj["minSetpoint"] = devices[i].minSetpoint;
      obj["maxSetpoint"] = devices[i].maxSetpoint;
      obj["rawValue"] = devices[i].rawValue;
      obj["percent"] = devices[i].percent;
      obj["output_mA"] = devices[i].output_mA;
      obj["commActive"] = devices[i].commActive;
    }
  }

  JsonArray relays = doc.createNestedArray("relays");
  for(int i=0; i<4; i++) relays.add(relayState[i]);

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSystemUpdate() {
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
  
  server.send(200, "text/plain", "Settings Saved. ESP32 Rebooting...");
  
  pendingRestart = true;
  restartTime = millis() + 2000;
}

void handleAddDevice() {
  String id = server.arg("id"); String name = server.arg("name");
  if (findDevice(id) != -1) { server.send(200, "text/plain", "Device ID exists!"); return; }
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (!devices[i].active) {
      devices[i].active = true; devices[i].id = id; devices[i].name = name;
      devices[i].unit = "m"; devices[i].minSetpoint = 0.0; devices[i].maxSetpoint = 10.0;
      devices[i].rawValue = 0.0; devices[i].percent = 0.0; devices[i].output_mA = 4.0; devices[i].commActive = false;
      server.send(200, "text/plain", "Device Registered Successfully"); return;
    }
  }
  server.send(200, "text/plain", "Max devices reached!");
}

void handleDeleteDevice() {
  int idx = findDevice(server.arg("id"));
  if (idx != -1) { devices[idx].active = false; server.send(200, "text/plain", "Device Deleted"); } 
  else server.send(200, "text/plain", "Device Not Found");
}

void handleConfigDevice() {
  int idx = findDevice(server.arg("id"));
  if (idx != -1) {
    devices[idx].unit = server.arg("unit");
    devices[idx].minSetpoint = server.arg("min").toFloat(); 
    devices[idx].maxSetpoint = server.arg("max").toFloat();
    server.send(200, "text/plain", "Calibration Saved Successfully");
  }
}

void handleRelayCommand() {
  if (server.hasArg("id") && server.hasArg("state")) {
    int id = server.arg("id").toInt(); int state = server.arg("state").toInt();
    if (id >= 0 && id <= 3) {
      relayState[id] = (state == 1); digitalWrite(RELAY_PINS[id], relayState[id] ? HIGH : LOW);
    }
    server.send(200, "text/plain", "OK");
  }
}

// =======================================================================
// 5. OLED DISPLAY UPDATE (Pins 39 & 38)
// =======================================================================
void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("RADIOGEET - RX PANEL");
  display.printf("PAN: 0x%04X\n", device_pan_id);
  display.drawLine(0, 18, 128, 18, SSD1306_WHITE);

  int activeIdx = -1;
  int activeCount = 0;
  for (int i = 0; i < MAX_DEVICES; i++) { 
    if (devices[i].active) { 
      if (activeIdx == -1) activeIdx = i; 
      activeCount++;
    } 
  }

  display.setCursor(0, 22);
  if (activeIdx != -1) {
    display.printf("DEV : %s\n", devices[activeIdx].id.c_str());
    if (devices[activeIdx].commActive) {
      if (devices[activeIdx].unit == "Raw") {
        display.printf("RAW : %.0f (Raw)\n", devices[activeIdx].rawValue);
      } else {
        display.printf("RAW : %.2f %s\n", devices[activeIdx].rawValue, devices[activeIdx].unit.c_str());
      }
      display.printf("LVL : %.1f %%\n", devices[activeIdx].percent);
      display.printf("mA  : %.2f mA\n", devices[activeIdx].output_mA);
    } else {
      display.println("STAT: COMM FAIL!");
      display.println("VAL : NO DATA");
    }
  } else {
    display.println("NO DEVICES REGISTERED");
  }
  
  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  display.setCursor(0, 55);
  display.printf("TANKS: %d | IP: .4.1", activeCount);
  display.display();
}

// =======================================================================
// 6. SYSTEM SETUP
// =======================================================================
void setup() {
  Serial.begin(115200); 
  
  // Preferences
  prefs.begin("rg-config", false);
  ap_ssid = prefs.getString("ssid", "RadioGeet_WTM");
  ap_password = prefs.getString("pass", "12345678"); 
  device_pan_id = prefs.getInt("pan", 34969);
  net_ip_str = prefs.getString("ip", "192.168.4.1");
  net_gateway_str = prefs.getString("gw", "192.168.4.1");
  net_subnet_str = prefs.getString("sub", "255.255.255.0");
  
  local_ip.fromString(net_ip_str);
  gateway.fromString(net_gateway_str);
  subnet.fromString(net_subnet_str);

  // Default Devices
  for (int i = 0; i < MAX_DEVICES; i++) devices[i].active = false;
  devices[0] = {true, "TANK-01", "Tower Tank Main", "m", 0.0, 10.0, 0.0, 0.0, 4.0, false, 0};
  devices[1] = {true, "tnk2", "Secondary Tank", "m", 0.0, 10.0, 0.0, 0.0, 4.0, false, 0};
  
  // Relay Pins
  for(int i=0; i<4; i++) { pinMode(RELAY_PINS[i], OUTPUT); digitalWrite(RELAY_PINS[i], LOW); }

  // OLED Display Init (Pins 39 & 38)
  Wire.begin(I2C_SDA, I2C_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(5, 20);
  display.println("RADIOGEET DIGITAL");
  display.setCursor(5, 35);
  display.println("SCADA INITIALIZING...");
  display.display();

  // LoRa UART Receiver (Pins 16 & 17)
  Serial1.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  // WiFi Setup
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
  macAddress = WiFi.softAPmacAddress();

  // Web Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleStateAPI);
  server.on("/api/system/update", HTTP_GET, handleSystemUpdate);
  server.on("/api/device/add", HTTP_GET, handleAddDevice);
  server.on("/api/device/delete", HTTP_GET, handleDeleteDevice);
  server.on("/api/device/config", HTTP_GET, handleConfigDevice);
  server.on("/api/relay", HTTP_GET, handleRelayCommand);
  server.begin();
  
  updateOLED(); 
}

// =======================================================================
// 7. MAIN LOOP
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

  // Parse Incoming LoRa Packets:
  // Supports "ID:<NODE>,RAW:<VAL>,UNIT:<U>,PCT:<P>,MA:<M>\n"
  if (Serial1.available()) {
    String incoming = Serial1.readStringUntil('\n'); 
    incoming.trim();
    if (incoming.length() > 0) {
      int targetIdx = -1;
      float parsedRaw = -1.0;
      String parsedUnit = "";

      if (incoming.indexOf("ID:") != -1 && incoming.indexOf("RAW:") != -1) {
        int idStart = incoming.indexOf("ID:") + 3;
        int idEnd = incoming.indexOf(",", idStart);
        String incId = incoming.substring(idStart, idEnd);

        int rawStart = incoming.indexOf("RAW:") + 4;
        int rawEnd = incoming.indexOf(",", rawStart);
        if (rawEnd == -1) rawEnd = incoming.length();
        parsedRaw = incoming.substring(rawStart, rawEnd).toFloat();

        if (incoming.indexOf("UNIT:") != -1) {
          int uStart = incoming.indexOf("UNIT:") + 5;
          int uEnd = incoming.indexOf(",", uStart);
          if (uEnd == -1) uEnd = incoming.length();
          parsedUnit = incoming.substring(uStart, uEnd);
        }

        targetIdx = findDevice(incId);
      }

      if (targetIdx != -1 && parsedRaw >= 0) {
        devices[targetIdx].rawValue = parsedRaw;
        // Sync incoming unit from Transmitter if present
        if (parsedUnit.length() > 0) {
          devices[targetIdx].unit = parsedUnit;
        }
        devices[targetIdx].lastPacketTime = millis();
        devices[targetIdx].commActive = true;
      }
    }
  }

  // 1-Second Control & Alarm Logic Loop
  static unsigned long lastLogicUpdate = 0;
  if (millis() - lastLogicUpdate > 1000) {
    lastLogicUpdate = millis();

    bool anyAlarmTriggered = false;
    for (int i = 0; i < MAX_DEVICES; i++) {
      if (devices[i].active) {
        if (devices[i].commActive && (millis() - devices[i].lastPacketTime > COMM_TIMEOUT_MS)) {
          devices[i].commActive = false;
        }
        
        if (devices[i].commActive) {
          float minS = devices[i].minSetpoint; 
          float maxS = devices[i].maxSetpoint;
          
          if (maxS > minS) {
            devices[i].percent = ((devices[i].rawValue - minS) / (maxS - minS)) * 100.0;
          } else {
            devices[i].percent = 0.0;
          }
          if (devices[i].percent < 0) devices[i].percent = 0; 
          if (devices[i].percent > 100) devices[i].percent = 100;

          // 4-20 mA calculation based on calibrated percentage
          devices[i].output_mA = 4.0 + (16.0 * (devices[i].percent / 100.0));

          // Trip alarm if Raw Value is outside the configured bounds
          if (devices[i].rawValue <= minS || devices[i].rawValue >= maxS) {
            anyAlarmTriggered = true;
          }
        }
      }
    }

    // Auto-operate Relay 1 on Alarm
    if (anyAlarmTriggered) { 
      relayState[0] = true; 
      digitalWrite(RELAY_PINS[0], HIGH); 
    }
  }

  // OLED Screen Refresh (250ms Rate-Limited)
  static unsigned long last_oled = 0;
  if (millis() - last_oled > OLED_UPDATE_MS) {
    updateOLED();
    last_oled = millis();
  }

  delay(1);
}
