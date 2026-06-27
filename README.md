# CO2 Industrial Monitoring System
### Advanced Monitoring with Google Assistant & Telegram Bot — ESP32

> **Research Paper:** *Advanced Monitoring Solutions with Google Assistant and Telegram Bot: A Focus on Solidified Carbon Dioxide (CO₂)*  
> Department of ECE, SRM Institute of Science and Technology, Ramapuram Campus, Chennai

---

## Overview

A real-time industrial safety system that monitors **CO₂ concentration, temperature, and humidity** using an ESP32 microcontroller. When thresholds are crossed, the system automatically activates ventilation/heating, triggers audible and visual alerts, and pushes notifications via **Telegram Bot**. Operators can control the exhaust fan remotely via **Google Assistant (IFTTT Webhooks)**. All sensor data is logged to **Google Sheets** and accessible instantly via **QR code**.

---

## Hardware Components

| Component | Purpose |
|-----------|---------|
| ESP32 DevKit V1 | Main microcontroller (WiFi + processing) |
| MQ135 Gas Sensor | CO₂ / air quality detection (analog) |
| DHT11 Sensor | Temperature & humidity measurement |
| 16×2 I2C LCD | Real-time local display |
| 2-Channel Relay Module | Controls fan and warmer circuit |
| 5V DC Fan | Exhaust ventilation |
| Buzzer | Audible alert |
| LED | Visual alert indicator |

---

## Wiring Diagram

```
ESP32 DevKit V1
┌────────────────────────────────────┐
│  GPIO34  ←── MQ135 AOUT           │  (ADC input — CO₂ analog signal)
│  GPIO4   ←── DHT11 DATA           │  (Digital signal — Temp & Humidity)
│  GPIO21  ──→ LCD SDA (I2C)        │
│  GPIO22  ──→ LCD SCL (I2C)        │
│  GPIO26  ──→ Relay IN1 (Fan)      │  (Active LOW relay)
│  GPIO27  ──→ Relay IN2 (Warmer)   │  (Active LOW relay)
│  GPIO25  ──→ Buzzer +             │
│  GPIO33  ──→ LED + (with 220Ω)    │
│  3.3V    ──→ DHT11 VCC            │
│  5V      ──→ MQ135 VCC, Relay VCC │
│  GND     ──→ All GND rails        │
└────────────────────────────────────┘

Relay Module:
  Relay 1 COM/NO ──→ DC Fan (+ terminal)
  Relay 2 COM/NO ──→ Warmer/Heater circuit
  Both relay coils: 5V from ESP32 5V pin, GND common
```

---

## CO₂ Reference Thresholds (ASHRAE Guidelines)

| CO₂ Level (ppm) | Air Quality Status |
|------------------|-------------------|
| 350 – 450 | 🟢 Fresh Air |
| 450 – 700 | 🟢 Normal |
| 700 – 1000 | 🟡 Acceptable |
| 1000 – 2500 | 🟠 Poor Ventilation / Drowsiness |
| 2500 – 5000 | 🔴 Negative Health Impact |
| > 5000 | 🚨 Alarming — Evacuate |

**System triggers exhaust fan at > 1000 ppm** and sends alerts.  
**System triggers warmer at temperature < 10 °C.**

---

## Software Setup

### 1. Arduino IDE Libraries

Install via **Tools → Manage Libraries**:

| Library | Author |
|---------|--------|
| `DHT sensor library` | Adafruit |
| `LiquidCrystal_I2C` | Frank de Brabander |
| `ArduinoJson` | Benoit Blanchon (v6.x) |
| `UniversalTelegramBot` | Brian Lough |

Board: **ESP32 Dev Module** — Install via Board Manager:  
`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

---

### 2. Credentials Configuration

Open `co2_monitor_esp32.ino` and fill in:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD  = "YOUR_WIFI_PASSWORD";

#define BOT_TOKEN   "YOUR_TELEGRAM_BOT_TOKEN"
#define CHAT_ID     "YOUR_TELEGRAM_CHAT_ID"

#define IFTTT_API_KEY   "YOUR_IFTTT_API_KEY"

const char* GOOGLE_SHEET_URL = "YOUR_GOOGLE_APPS_SCRIPT_URL";
```

---

### 3. Telegram Bot Setup

1. Open Telegram → search **@BotFather**
2. Send `/newbot` → follow prompts → copy the **bot token**
3. Start a chat with your bot, then visit:  
   `https://api.telegram.org/bot<TOKEN>/getUpdates`  
   to find your **Chat ID**
4. Paste both into the sketch

**Available Bot Commands:**

| Command | Action |
|---------|--------|
| `/status` | Returns live sensor readings |
| `/fanon` | Manually turn exhaust fan ON |
| `/fanoff` | Manually turn exhaust fan OFF |
| `/warmeron` | Manually turn heater ON |
| `/warmeroff` | Manually turn heater OFF |
| `/help` | Show command list |

---

### 4. Google Assistant via IFTTT

1. Go to [ifttt.com](https://ifttt.com) → Create Applet
2. **IF** → Google Assistant → *Say a specific phrase* → `"Exhaust ON"`
3. **THEN** → Webhooks → Make a web request:
   - URL: `https://maker.ifttt.com/trigger/exhaust_on/with/key/YOUR_KEY`
   - Method: `POST`
4. Repeat for `"Exhaust OFF"` / `exhaust_off` event
5. Copy your **Webhooks API Key** from [ifttt.com/maker_webhooks](https://ifttt.com/maker_webhooks)

Voice commands (as shown in Fig.7 of the paper):
- **"Exhaust ON"** → Fan turns on
- **"STOP"** → Fan turns off

---

### 5. Google Sheets Logging + QR Code

**Apps Script (paste into Extensions → Apps Script):**

```javascript
function doPost(e) {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
  var data  = JSON.parse(e.postData.contents);
  var now   = new Date();

  sheet.appendRow([
    Utilities.formatDate(now, Session.getScriptTimeZone(), "MM/dd/yyyy HH:mm"),
    data.temperature,
    data.humidity,
    data.co2,
    data.remarks,
    data.alert
  ]);

  return ContentService
    .createTextOutput(JSON.stringify({ status: "OK" }))
    .setMimeType(ContentService.MimeType.JSON);
}
```

**Deploy:** Click **Deploy → New Deployment → Web App**  
- Execute as: *Me*  
- Access: *Anyone*  
Copy the Web App URL → paste into `GOOGLE_SHEET_URL` in the sketch.

**QR Code:** Generate a QR code pointing to your Google Sheet URL using any online QR generator, then print and place it near the equipment — workers can scan for instant data access (as described in the paper, Section IV-E).

---

## System Architecture

```
┌──────────────────────────────────────────────────────────┐
│                     ESP32 DevKit V1                       │
│                                                          │
│  MQ135 ──ADC──→  CO₂ Logic  ──→  Relay (Fan)            │
│  DHT11 ──I/O──→  Temp Logic  ──→  Relay (Warmer)         │
│                  Humidity     ──→  Alert System           │
│                      │                                    │
│               ┌──────┴──────┐                            │
│               ↓             ↓                            │
│          Buzzer/LED     WiFi (802.11)                    │
│          LCD Display        │                            │
│                     ┌───────┴────────┐                   │
│                     ↓               ↓                    │
│               Telegram Bot    Google Sheets              │
│               (Alerts +       (Data Logging              │
│               Commands)        + QR Code)                │
│                                     │                    │
│                              IFTTT Webhooks              │
│                            (Google Assistant)            │
└──────────────────────────────────────────────────────────┘
```

---

## Project Structure

```
co2-monitor-esp32/
├── co2_monitor_esp32.ino   ← Main Arduino sketch
├── README.md               ← This file
└── assets/
    └── wiring_diagram.png  ← (Add your own)
```



---

## License

This project is for academic and educational use.
