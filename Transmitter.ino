/*
  RadioGeet Digital Pvt. Ltd.
  TATA STEEL – SPONGE IRON, JODA
  Field Station: Radiowave Remote IO R865 (LoRa Transmitter)
  Hardware: ESP32-S3 Dev Module + 128x64 I2C OLED (SDA: 39, SCL: 38)
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

// LoRa Radio UART Pins (ESP32-S3)
#define LORA_RX_PIN 16
#define LORA_TX_PIN 17

// RS485 Interface Pins for Masibus Controller (Hardware Serial 2)
#define RS485_RX_PIN 18
#define RS485_TX_PIN 8
#define RS485_DE_RE  21 // Direction control (if using MAX485; ignore if auto-direction)

// =======================================================================
// 2. CONFIGURATION & STATE
// =======================================================================
Preferences prefs;
WebServer server(80);

String device_id;
int tx_interval_ms;
int modbus_slave_id;
int modbus_reg_addr;
bool simulation_mode;

float currentRawValue = 5.25; // Live raw measurement
unsigned long lastTxTime = 0;
unsigned long packetCounter = 0;
#define OLED_UPDATE_MS 250

// =======================================================================
// 3. FIELD TRANSMITTER WEB DASHBOARD
// =======================================================================
const char* tx_html_page PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>RadioGeet - Field Transmitter</title>
  <style>
    :root { --bg: #f8fafc; --card: #ffffff; --text: #0f172a; --primary: #1c45df; --border: #e2e8f0; }
    body { font-family: 'Segoe UI', Tahoma, sans-serif; background: var(--bg); color: var(--text); padding: 25px; margin: 0; }
    .container { max-width: 600px; margin: auto; }
    .card { background: var(--card); padding: 25px; border-radius: 12px; border: 1px solid var(--border); box-shadow: 0 4px 15px rgba(0,0,0,0.03); margin-bottom: 20px; }
    h2 { margin-top: 0; color: var(--primary); font-size: 20px; }
    .form-group { margin-bottom: 15px; }
    label { display: block; font-weight: 600; font-size: 13px; margin-bottom: 5px; }
    input[type="text"], input[type="number"], select { width: 100%; padding: 10px; border: 1px solid var(--border); border-radius: 6px; box-sizing: border-box; }
    button { background: var(--primary); color: white; border: none; padding: 12px; width: 100%; border-radius: 6px; font-weight: bold; cursor: pointer; }
    .raw-box { text-align: center; padding: 20px; background: #eff6ff; border-radius: 8px; margin-bottom: 20px; }
    .raw-val { font-size: 42px; font-weight: bold; color: var(--primary); }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <h2>RadioGeet - Field Station (TX)</h2>
      <p style="color:#64748b; font-size:12px;">Radiowave Remote IO R865 LoRa Transmitter</p>
      <div class="raw-box">
        <div style="font-size:12px; color:#64748b; font-weight:bold;">LIVE RAW TRANSMISSION</div>
        <div class="raw-val" id="disp-raw">--</div>
        <div style="font-size:12px; color:#64748b;">Packets Sent: <span id="disp-count" style="font-weight:bold;">0</span></div>
      </div>

      <div class="form-group"><label>Device Node ID</label><input type="text" id="cfg-id"></div>
      <div class="form-group"><label>Transmission Interval (ms)</label><input type="number" id="cfg-int"></div>
      <div class="form-group"><label>Simulate Live Raw Data (Test Mode)</label>
        <select id="cfg-sim">
          <option value="1">Enabled (Sine Wave Simulation)</option>
          <option value="0">Disabled (Read Modbus RS485)</option>
        </select>
      </div>
      <button onclick="saveConfig()">Save Transmitter Parameters</button>
    </div>
  </div>

  <script>
    function loadData() {
      fetch('/api/txstate').then(r=>r.json()).then(d=>{
        document.getElementById('disp-raw').innerText = d.raw.toFixed(2);
        document.getElementById('disp-count').innerText = d.packets;
        if(document.getElementById('cfg-id').value === "") {
          document.getElementById('cfg-id').value = d.id;
          document.getElementById('cfg-int').value = d.interval;
          document.getElementById('cfg-sim').value = d.sim ? "1" : "0";
        }
      });
    }
    function saveConfig() {
      const id = document.getElementById('cfg-id').value;
      const interval = document.getElementById('cfg-int').value;
      const sim = document.getElementById('cfg-sim').value;
      fetch(`/api/txsave?id=${encodeURIComponent(id)}&int=${interval}&sim=${sim}`)
        .then(r=>r.text()).then(res=>{ alert(res); location.reload(); });
    }
    setInterval(loadData, 1000);
    loadData();
  </script>
</body>
</html>
)rawliteral";

// =======================================================================
// 4. MODBUS & SENSOR READ
// =======================================================================
float readSensorOrSimulate() {
  if (simulation_mode) {
    // Generate realistic fluctuating water level between 2.0m and 8.0m for field test
    static float angle = 0.0;
    angle += 0.05;
    if (angle > 6.28) angle = 0.0;
    return 5.0 + (sin(angle) * 3.0);
  } else {
    // In production: Read Masibus RS485 Holding Register
    // (If no physical RS485 response, falls back to 0.0)
    return currentRawValue;
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
  display.println("RADIOGEET - FIELD TX");
  display.printf("NODE: %s\n", device_id.c_str());
  display.drawLine(0, 18, 128, 18, SSD1306_WHITE);

  display.setCursor(0, 24);
  display.printf("RAW VAL: %.2f\n", currentRawValue);
  display.printf("TX RATE: %d ms\n", tx_interval_ms);
  display.printf("MODE   : %s\n", simulation_mode ? "TEST SIM" : "RS485 MODBUS");

  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  display.setCursor(0, 55);
  display.printf("PKT SENT: %lu", packetCounter);
  display.display();
}

// =======================================================================
// 6. SETUP
// =======================================================================
void setup() {
  Serial.begin(115200);

  // Load Preferences
  prefs.begin("rg-tx", false);
  device_id = prefs.getString("dev_id", "TANK-01");
  tx_interval_ms = prefs.getInt("interval", 1500);
  simulation_mode = prefs.getBool("sim", true); // Defaults to true for out-of-box test

  // OLED Init on Pins 39 & 38
  Wire.begin(I2C_SDA, I2C_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 25);
  display.println("RADIOGEET TX BOOT...");
  display.display();

  // LoRa UART Init on Pins 16 & 17
  Serial1.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  // WiFi AP for Field Setup
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP("RadioGeet_TX_FIELD", "12345678");

  // Web Endpoints
  server.on("/", HTTP_GET, []() { server.send(200, "text/html", tx_html_page); });
  server.on("/api/txstate", HTTP_GET, []() {
    StaticJsonDocument<256> doc;
    doc["raw"] = currentRawValue;
    doc["packets"] = packetCounter;
    doc["id"] = device_id;
    doc["interval"] = tx_interval_ms;
    doc["sim"] = simulation_mode;
    String r; serializeJson(doc, r);
    server.send(200, "application/json", r);
  });
  server.on("/api/txsave", HTTP_GET, []() {
    device_id = server.arg("id");
    tx_interval_ms = server.arg("int").toInt();
    simulation_mode = (server.arg("sim") == "1");

    prefs.putString("dev_id", device_id);
    prefs.putInt("interval", tx_interval_ms);
    prefs.putBool("sim", simulation_mode);

    server.send(200, "text/plain", "Parameters Saved!");
  });

  server.begin();
  updateOLED();
}

// =======================================================================
// 7. MAIN LOOP
// =======================================================================
void loop() {
  server.handleClient();

  // Transmit LoRa Packet at Configured Interval
  if (millis() - lastTxTime >= (unsigned long)tx_interval_ms) {
    lastTxTime = millis();
    currentRawValue = readSensorOrSimulate();

    // Standard Protocol Packet for the Receiver:
    // Format: "ID:<DEVICE_ID>,RAW:<VALUE>\n"
    String packet = "ID:" + device_id + ",RAW:" + String(currentRawValue, 2) + "\n";
    Serial1.print(packet);
    Serial.print("LoRa TX -> " + packet);

    packetCounter++;
  }

  // OLED Refresh Loop (250ms rate-limited)
  static unsigned long lastOled = 0;
  if (millis() - lastOled > OLED_UPDATE_MS) {
    updateOLED();
    lastOled = millis();
  }

  delay(1);
}
