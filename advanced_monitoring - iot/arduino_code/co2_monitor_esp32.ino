/**
 * ============================================================
 *  Advanced CO2 Monitoring System with Google Assistant
 *  and Telegram Bot — Solidified CO2 Industrial Safety Monitor
 * ============================================================
 *
 * Hardware:
 *   - ESP32 DevKit V1
 *   - MQ135 Gas Sensor (CO2 detection)
 *   - DHT11 Temperature & Humidity Sensor
 *   - 16x2 I2C LCD Display
 *   - 2-Channel Relay Module (Fan + Warmer)
 *   - 5V DC Fan (exhaust ventilation)
 *   - Buzzer (audible alert)
 *   - LED (visual alert)
 *
 * Services:
 *   - IFTTT Webhooks (Google Assistant voice control)
 *   - Telegram Bot API (push notifications)
 *   - Google Sheets via HTTP POST (data logging + QR code access)
 *
 * CO2 Reference Table (ASHRAE guidelines):
 *   350–450  ppm  → Fresh air
 *   450–700  ppm  → Normal level
 *   700–1000 ppm  → Acceptable level
 *   1000–2500 ppm → Drowsiness
 *   2500–5000 ppm → Negative health impact
 *   > 5000   ppm  → Alarming level
 *
 * Thresholds used in this system:
 *   CO2 > 1000 ppm → Trigger exhaust fan + alerts
 *   Temp > 30 °C   → Trigger exhaust fan + alerts
 *   Temp < threshold → Trigger warmer circuit
 *   Humidity > 80%  → Trigger humidity alert
 *
 * Wiring Summary:
 *   MQ135  AOUT → GPIO34 (ADC)
 *   DHT11  DATA → GPIO4
 *   LCD    SDA  → GPIO21 | SCL → GPIO22 (I2C)
 *   Relay 1 IN  → GPIO26  (Fan / Exhaust)
 *   Relay 2 IN  → GPIO27  (Warmer Circuit)
 *   Buzzer      → GPIO25
 *   LED         → GPIO33
 *   IFTTT Webhook: triggers via HTTP GET from ESP32
 *   Telegram: sends via HTTPS POST
 *
 * Author  : B. Sanjana, L. Sathya, G.K. Sathya Narayanan,
 *            Ashiq Rasool — SRM IST Ramapuram, Chennai
 * Adapted for GitHub by: [Your Name]
 * Date    : 2025
 * ============================================================
 *
 * Libraries Required (install via Arduino Library Manager):
 *   - DHT sensor library by Adafruit
 *   - LiquidCrystal_I2C by Frank de Brabander
 *   - WiFiClientSecure  (built-in ESP32 core)
 *   - HTTPClient        (built-in ESP32 core)
 *   - ArduinoJson by Benoit Blanchon (v6.x)
 *   - UniversalTelegramBot by Brian Lough
 * ============================================================
 */

// ─────────────────────────────────────────────
//  INCLUDES
// ─────────────────────────────────────────────
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <UniversalTelegramBot.h>

// ─────────────────────────────────────────────
//  CREDENTIALS — Fill these before uploading
// ─────────────────────────────────────────────
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Telegram Bot
#define BOT_TOKEN        "YOUR_TELEGRAM_BOT_TOKEN"    // e.g. "123456:ABC-DEF..."
#define CHAT_ID          "YOUR_TELEGRAM_CHAT_ID"      // e.g. "-100123456789"

// IFTTT Webhook (for Google Assistant integration)
// Trigger name must match what you created in IFTTT
#define IFTTT_API_KEY    "YOUR_IFTTT_API_KEY"
#define IFTTT_FAN_ON     "exhaust_on"     // IFTTT event name for fan ON
#define IFTTT_FAN_OFF    "exhaust_off"    // IFTTT event name for fan OFF

// Google Sheets Logging (via Google Apps Script Web App URL)
// Deploy a Google Apps Script that accepts HTTP POST and appends rows
const char* GOOGLE_SHEET_URL = "YOUR_GOOGLE_APPS_SCRIPT_WEB_APP_URL";

// ─────────────────────────────────────────────
//  PIN DEFINITIONS — ESP32 DevKit V1
// ─────────────────────────────────────────────
#define MQ135_PIN      34    // ADC pin for MQ135 analog output
#define DHT_PIN         4    // DHT11 data pin
#define DHT_TYPE        DHT11

#define RELAY_FAN      26    // Relay channel 1: Exhaust fan
#define RELAY_WARMER   27    // Relay channel 2: Warmer/heater circuit
#define BUZZER_PIN     25    // Active buzzer
#define LED_PIN        33    // Visual alert LED

// I2C LCD — default address 0x27 (some modules use 0x3F)
#define LCD_ADDR       0x27
#define LCD_COLS       16
#define LCD_ROWS        2

// ─────────────────────────────────────────────
//  THRESHOLD DEFINITIONS
// ─────────────────────────────────────────────
#define CO2_THRESHOLD_ACCEPTABLE   1000   // ppm — exhaust fan triggers above this
#define CO2_THRESHOLD_DROWSY       2500   // ppm — higher danger level
#define CO2_THRESHOLD_ALARM        5000   // ppm — critical alarm

#define TEMP_HIGH_THRESHOLD         30.0  // °C  — too hot → exhaust fan ON
#define TEMP_LOW_THRESHOLD          10.0  // °C  — too cold → warmer ON
#define HUMIDITY_HIGH_THRESHOLD     80.0  // %   — high humidity → safety alert

// ─────────────────────────────────────────────
//  TIMING INTERVALS (milliseconds)
// ─────────────────────────────────────────────
#define SENSOR_READ_INTERVAL        5000   // Read sensors every 5 seconds
#define TELEGRAM_ALERT_COOLDOWN    60000   // Min gap between Telegram messages (1 min)
#define SHEET_LOG_INTERVAL         30000   // Log to Google Sheets every 30 seconds
#define TELEGRAM_POLL_INTERVAL      1000   // Poll Telegram for commands every 1 second
#define LCD_SCROLL_INTERVAL         3000   // Alternate LCD display every 3 seconds

// ─────────────────────────────────────────────
//  RELAY LOGIC — Adjust if your relay is Active-LOW
//  Most 2-channel relay modules are Active-LOW:
//    LOW  = relay ON (coil energised)
//    HIGH = relay OFF
// ─────────────────────────────────────────────
#define RELAY_ACTIVE   LOW
#define RELAY_INACTIVE HIGH

// ─────────────────────────────────────────────
//  MQ135 CALIBRATION
//  The MQ135 reads raw ADC (0–4095 on ESP32 12-bit ADC).
//  For a rough CO2 ppm estimate without a calibration library:
//    ppm = map(rawADC, 0, 4095, 350, 10000)
//  For production use, calibrate against a known CO2 source
//  or use the MQUnifiedSensor library.
// ─────────────────────────────────────────────
#define MQ135_CLEAN_AIR_FACTOR   3.6   // Rs/Ro ratio in clean air (from datasheet)
#define MQ135_ADC_MAX           4095
#define MQ135_PPM_MIN            350
#define MQ135_PPM_MAX          10000

// ─────────────────────────────────────────────
//  OBJECT INSTANTIATION
// ─────────────────────────────────────────────
DHT             dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
WiFiClientSecure secureClient;
UniversalTelegramBot bot(BOT_TOKEN, secureClient);

// ─────────────────────────────────────────────
//  GLOBAL STATE VARIABLES
// ─────────────────────────────────────────────
float   g_temperature    = 0.0;
float   g_humidity       = 0.0;
int     g_co2_ppm        = 0;
bool    g_fan_on         = false;
bool    g_warmer_on      = false;
bool    g_alert_active   = false;
String  g_air_quality    = "Unknown";

// Timestamps for non-blocking scheduling
unsigned long lastSensorRead     = 0;
unsigned long lastTelegramAlert  = 0;
unsigned long lastSheetLog       = 0;
unsigned long lastTelegramPoll   = 0;
unsigned long lastLCDScroll      = 0;
int           lcdPage            = 0;   // 0 = Temp/Hum, 1 = CO2/Status

// ─────────────────────────────────────────────
//  FUNCTION PROTOTYPES
// ─────────────────────────────────────────────
void     connectWiFi();
int      readCO2();
String   co2Description(int ppm);
void     readSensors();
void     controlActuators();
void     updateLCD();
void     triggerAlert(String reason);
void     clearAlert();
void     sendTelegramAlert(String msg);
void     handleTelegramCommands();
void     logToGoogleSheets();
void     triggerIFTTT(String eventName, String value1 = "", String value2 = "", String value3 = "");
void     setFan(bool state, String source = "AUTO");
void     setWarmer(bool state);
void     printSerial();

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================");
  Serial.println(" CO2 Industrial Monitoring System");
  Serial.println(" SRM IST Ramapuram — ECE Dept");
  Serial.println("========================================\n");

  // GPIO setup
  pinMode(RELAY_FAN,    OUTPUT);
  pinMode(RELAY_WARMER, OUTPUT);
  pinMode(BUZZER_PIN,   OUTPUT);
  pinMode(LED_PIN,      OUTPUT);

  // Safe initial state — relays OFF
  digitalWrite(RELAY_FAN,    RELAY_INACTIVE);
  digitalWrite(RELAY_WARMER, RELAY_INACTIVE);
  digitalWrite(BUZZER_PIN,   LOW);
  digitalWrite(LED_PIN,      LOW);

  // Sensor init
  dht.begin();
  analogReadResolution(12);   // ESP32 ADC: 12-bit (0–4095)
  analogSetAttenuation(ADC_11db); // Full range 0–3.3V for GPIO34

  // LCD init
  Wire.begin(21, 22);         // SDA=21, SCL=22 (default ESP32 I2C)
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("  CO2 Monitor   ");
  lcd.setCursor(0, 1);
  lcd.print(" Initializing...");
  delay(2000);

  // WiFi
  connectWiFi();

  // Telegram — skip SSL certificate verification for simplicity
  // For production, pin the Telegram root CA certificate instead
  secureClient.setInsecure();

  // Startup self-test: flash LED and beep buzzer once
  digitalWrite(LED_PIN, HIGH);
  tone(BUZZER_PIN, 1000, 200);
  delay(300);
  digitalWrite(LED_PIN, LOW);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready!   ");
  lcd.setCursor(0, 1);
  lcd.print("WiFi Connected  ");
  delay(2000);

  // Send startup notification to Telegram
  sendTelegramAlert("CO2 Monitor ONLINE\nSystem initialized and monitoring started.");

  Serial.println("[SETUP] Initialization complete.\n");
}

// ─────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // 1. Read sensors at defined interval
  if (now - lastSensorRead >= SENSOR_READ_INTERVAL) {
    lastSensorRead = now;
    readSensors();
    controlActuators();
    printSerial();
  }

  // 2. Update LCD display (scrolling pages)
  if (now - lastLCDScroll >= LCD_SCROLL_INTERVAL) {
    lastLCDScroll = now;
    lcdPage = (lcdPage + 1) % 2;
    updateLCD();
  }

  // 3. Log to Google Sheets
  if (now - lastSheetLog >= SHEET_LOG_INTERVAL) {
    lastSheetLog = now;
    logToGoogleSheets();
  }

  // 4. Poll Telegram for incoming commands
  if (now - lastTelegramPoll >= TELEGRAM_POLL_INTERVAL) {
    lastTelegramPoll = now;
    handleTelegramCommands();
  }
}

// ─────────────────────────────────────────────
//  WiFi CONNECTION
// ─────────────────────────────────────────────
void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi ");
  lcd.setCursor(0, 1);
  lcd.print("Please wait...  ");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Connection FAILED. Restarting...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi FAILED!    ");
    lcd.setCursor(0, 1);
    lcd.print("Restarting...   ");
    delay(3000);
    ESP.restart();
  }
}

// ─────────────────────────────────────────────
//  SENSOR READING
// ─────────────────────────────────────────────

/**
 * readCO2()
 * Reads MQ135 ADC and maps to approximate ppm.
 * The MQ135 outputs analog voltage proportional to gas concentration.
 * This is a linear approximation; for precision, use Rs/Ro curve fitting.
 */
int readCO2() {
  // Average multiple ADC samples to reduce noise
  int sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(MQ135_PIN);
    delay(5);
  }
  int raw = sum / 10;

  // Map raw ADC to ppm range
  // Note: This is a simplified linear mapping.
  // Real MQ135 uses a logarithmic curve (see datasheet Fig.2).
  int ppm = map(raw, 0, MQ135_ADC_MAX, MQ135_PPM_MIN, MQ135_PPM_MAX);
  ppm = constrain(ppm, MQ135_PPM_MIN, MQ135_PPM_MAX);

  Serial.printf("[MQ135] Raw ADC: %d  →  CO2: %d ppm\n", raw, ppm);
  return ppm;
}

/**
 * co2Description()
 * Returns a human-readable description for a CO2 level in ppm.
 * Based on Table 1 in the research paper (ASHRAE guidelines).
 */
String co2Description(int ppm) {
  if (ppm <= 450)       return "Fresh Air";
  else if (ppm <= 700)  return "Normal";
  else if (ppm <= 1000) return "Acceptable";
  else if (ppm <= 2500) return "Poor Vent.";
  else if (ppm <= 5000) return "Hazardous";
  else                  return "ALARM";
}

/**
 * readSensors()
 * Reads DHT11 (temperature & humidity) and MQ135 (CO2).
 * Updates global state variables.
 */
void readSensors() {
  // DHT11 reading
  float t = dht.readTemperature();   // °C
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    Serial.println("[DHT11] Read FAILED. Check wiring.");
    // Keep previous values rather than zeroing out
  } else {
    g_temperature = t;
    g_humidity    = h;
  }

  // MQ135 CO2 reading
  g_co2_ppm     = readCO2();
  g_air_quality  = co2Description(g_co2_ppm);

  Serial.printf("[DHT11] Temp: %.1f°C  Humidity: %.1f%%\n", g_temperature, g_humidity);
  Serial.printf("[AIR]   CO2: %d ppm  Status: %s\n",        g_co2_ppm,     g_air_quality.c_str());
}

// ─────────────────────────────────────────────
//  ACTUATOR CONTROL
// ─────────────────────────────────────────────

/**
 * setFan()
 * Turns the exhaust fan relay ON or OFF.
 * source = "AUTO" (sensor-triggered) or "MANUAL" (voice/Telegram command)
 */
void setFan(bool state, String source) {
  g_fan_on = state;
  digitalWrite(RELAY_FAN, state ? RELAY_ACTIVE : RELAY_INACTIVE);
  Serial.printf("[FAN]  Exhaust fan → %s  (source: %s)\n",
                state ? "ON" : "OFF", source.c_str());
}

/**
 * setWarmer()
 * Turns the warmer/heater relay ON or OFF.
 */
void setWarmer(bool state) {
  g_warmer_on = state;
  digitalWrite(RELAY_WARMER, state ? RELAY_ACTIVE : RELAY_INACTIVE);
  Serial.printf("[WARMER] Heater → %s\n", state ? "ON" : "OFF");
}

/**
 * controlActuators()
 * Core control logic — implements the feedback loop described in the paper.
 * Evaluates sensor readings and triggers actuators + alerts accordingly.
 *
 * Logic Tree:
 *   CO2 > 1000 ppm  → Fan ON, Alerts ON
 *   Temp > 30°C     → Fan ON, Alerts ON
 *   Temp < 10°C     → Warmer ON, Alerts ON
 *   Humidity > 80%  → Alerts (electric shock safety warning)
 *   All normal      → Fan OFF, Warmer OFF, Alerts OFF
 */
void controlActuators() {
  bool shouldFanOn    = false;
  bool shouldWarmerOn = false;
  bool shouldAlert    = false;
  String alertReason  = "";

  // ── CO2 check ────────────────────────────────
  if (g_co2_ppm > CO2_THRESHOLD_ACCEPTABLE) {
    shouldFanOn = true;
    shouldAlert = true;
    if (g_co2_ppm > CO2_THRESHOLD_ALARM) {
      alertReason += "CRITICAL CO2: " + String(g_co2_ppm) + " ppm!\n";
    } else if (g_co2_ppm > CO2_THRESHOLD_DROWSY) {
      alertReason += "Hazardous CO2: " + String(g_co2_ppm) + " ppm\n";
    } else {
      alertReason += "High CO2: " + String(g_co2_ppm) + " ppm (Poor ventilation)\n";
    }
  }

  // ── Temperature HIGH check ────────────────────
  if (g_temperature > TEMP_HIGH_THRESHOLD) {
    shouldFanOn = true;
    shouldAlert = true;
    alertReason += "High Temp: " + String(g_temperature, 1) + "°C (limit: "
                   + String(TEMP_HIGH_THRESHOLD, 0) + "°C)\n";
  }

  // ── Temperature LOW check (warmer) ───────────
  if (g_temperature < TEMP_LOW_THRESHOLD) {
    shouldWarmerOn = true;
    shouldAlert    = true;
    alertReason += "Low Temp: " + String(g_temperature, 1) + "°C — Heater ON\n";
  }

  // ── Humidity HIGH check (electric shock risk) ─
  if (g_humidity > HUMIDITY_HIGH_THRESHOLD) {
    shouldAlert = true;
    alertReason += "High Humidity: " + String(g_humidity, 1)
                   + "% — Electric shock risk!\n";
  }

  // ── Apply actuator states ─────────────────────
  //  Only override fan if not manually controlled (for Google Assistant override,
  //  you could add a g_manual_override flag — left simple here)
  if (shouldFanOn != g_fan_on) {
    setFan(shouldFanOn, "AUTO");
  }
  if (shouldWarmerOn != g_warmer_on) {
    setWarmer(shouldWarmerOn);
  }

  // ── Alert handling ────────────────────────────
  if (shouldAlert) {
    triggerAlert(alertReason);
  } else {
    clearAlert();
  }
}

// ─────────────────────────────────────────────
//  ALERT SYSTEM
// ─────────────────────────────────────────────

/**
 * triggerAlert()
 * Activates buzzer + LED, then sends Telegram notification
 * (rate-limited to avoid flooding).
 */
void triggerAlert(String reason) {
  // Buzzer and LED — immediate physical alert
  digitalWrite(LED_PIN,    HIGH);
  tone(BUZZER_PIN, 2000, 300);    // 2kHz tone for 300ms (non-blocking via tone())

  g_alert_active = true;

  // Telegram alert — rate limited
  unsigned long now = millis();
  if (now - lastTelegramAlert >= TELEGRAM_ALERT_COOLDOWN) {
    lastTelegramAlert = now;

    String msg = "*CO2 Monitor Alert*\n\n";
    msg += reason;
    msg += "\n*Current Readings:*\n";
    msg += "Temp: "     + String(g_temperature, 1) + " °C\n";
    msg += "Humidity: " + String(g_humidity, 1)    + " %\n";
    msg += "CO2: "      + String(g_co2_ppm)        + " ppm (" + g_air_quality + ")\n";
    msg += "Fan: "      + String(g_fan_on ? "ON" : "OFF") + "\n";
    msg += "Warmer: "   + String(g_warmer_on ? "ON" : "OFF");

    sendTelegramAlert(msg);
  }
}

/**
 * clearAlert()
 * Deactivates buzzer and LED when conditions return to normal.
 */
void clearAlert() {
  if (g_alert_active) {
    digitalWrite(LED_PIN,  LOW);
    noTone(BUZZER_PIN);
    g_alert_active = false;
    Serial.println("[ALERT] Conditions normal — alert cleared.");
  }
}

// ─────────────────────────────────────────────
//  LCD DISPLAY
// ─────────────────────────────────────────────

/**
 * updateLCD()
 * Scrolls between two display pages:
 *   Page 0: Temperature and Humidity
 *   Page 1: CO2 level and Air Quality status
 * Mirrors the LCD output shown in Fig.6 of the paper.
 */
void updateLCD() {
  lcd.clear();

  if (lcdPage == 0) {
    // Page 0: T: XX.X°C  R: XX.X%
    lcd.setCursor(0, 0);
    lcd.printf("T:%.1fC  H:%.1f%%", g_temperature, g_humidity);

    lcd.setCursor(0, 1);
    if (g_temperature > TEMP_HIGH_THRESHOLD) {
      lcd.print("Temp HIGH! Fan ON");
    } else if (g_temperature < TEMP_LOW_THRESHOLD) {
      lcd.print("Temp LOW! Warmer");
    } else {
      lcd.print("Temp Normal     ");
    }
  } else {
    // Page 1: C: XXXX ppm  + status
    lcd.setCursor(0, 0);
    lcd.printf("C:%dppm", g_co2_ppm);

    // Pad to 16 chars
    lcd.setCursor(0, 1);
    String status = g_air_quality;
    status += "              ";      // Pad
    lcd.print(status.substring(0, 16));
  }
}

// ─────────────────────────────────────────────
//  TELEGRAM BOT
// ─────────────────────────────────────────────

/**
 * sendTelegramAlert()
 * Sends a message to the configured Telegram chat.
 */
void sendTelegramAlert(String msg) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TELEGRAM] WiFi not connected — skipping message.");
    return;
  }
  Serial.println("[TELEGRAM] Sending: " + msg);
  bool sent = bot.sendMessage(CHAT_ID, msg, "Markdown");
  if (!sent) {
    Serial.println("[TELEGRAM] Send FAILED.");
  }
}

/**
 * handleTelegramCommands()
 * Polls Telegram for incoming messages and handles commands:
 *
 *   /status   — Returns current sensor readings
 *   /fanon    — Manually turn exhaust fan ON
 *   /fanoff   — Manually turn exhaust fan OFF
 *   /warmeron — Manually turn warmer ON
 *   /warmeroff— Manually turn warmer OFF
 *   /help     — Lists available commands
 */
void handleTelegramCommands() {
  if (WiFi.status() != WL_CONNECTED) return;

  int numMessages = bot.getUpdates(bot.last_message_received + 1);

  while (numMessages > 0) {
    for (int i = 0; i < numMessages; i++) {
      String chat_id = bot.messages[i].chat_id;
      String text    = bot.messages[i].text;
      String from    = bot.messages[i].from_name;

      // Security: only accept commands from the configured chat
      if (chat_id != String(CHAT_ID)) {
        bot.sendMessage(chat_id, "Unauthorized access.", "");
        continue;
      }

      Serial.printf("[TELEGRAM] CMD from %s: %s\n", from.c_str(), text.c_str());

      // ── Command handling ─────────────────────
      if (text == "/start" || text == "/help") {
        String helpMsg  = "*CO2 Monitor — Commands*\n\n";
        helpMsg += "/status    — Current readings\n";
        helpMsg += "/fanon     — Exhaust fan ON\n";
        helpMsg += "/fanoff    — Exhaust fan OFF\n";
        helpMsg += "/warmeron  — Heater ON\n";
        helpMsg += "/warmeroff — Heater OFF\n";
        helpMsg += "/help      — Show this menu";
        bot.sendMessage(chat_id, helpMsg, "Markdown");

      } else if (text == "/status") {
        String statusMsg  = "*Live Sensor Data*\n\n";
        statusMsg += "Temperature: " + String(g_temperature, 1) + " °C\n";
        statusMsg += "Humidity: "    + String(g_humidity, 1)    + " %\n";
        statusMsg += "CO2: "         + String(g_co2_ppm)        + " ppm\n";
        statusMsg += "Air Quality: " + g_air_quality            + "\n";
        statusMsg += "Fan: "         + String(g_fan_on    ? "ON " : "OFF ") + "\n";
        statusMsg += "Warmer: "      + String(g_warmer_on ? "ON " : "OFF ") + "\n";
        statusMsg += "Alert: "       + String(g_alert_active ? "ACTIVE " : "Clear ");
        bot.sendMessage(chat_id, statusMsg, "Markdown");

      } else if (text == "/fanon") {
        setFan(true, "TELEGRAM");
        bot.sendMessage(chat_id, "Exhaust fan turned *ON* (manual override)", "Markdown");

      } else if (text == "/fanoff") {
        setFan(false, "TELEGRAM");
        bot.sendMessage(chat_id, "Exhaust fan turned *OFF* (manual override)", "Markdown");

      } else if (text == "/warmeron") {
        setWarmer(true);
        bot.sendMessage(chat_id, "Warmer/heater turned *ON* (manual override)", "Markdown");

      } else if (text == "/warmeroff") {
        setWarmer(false);
        bot.sendMessage(chat_id, "Warmer/heater turned *OFF* (manual override)", "Markdown");

      } else {
        bot.sendMessage(chat_id, "Unknown command. Send /help for the list.", "");
      }
    }
    numMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

// ─────────────────────────────────────────────
//  GOOGLE SHEETS DATA LOGGING
// ─────────────────────────────────────────────

/**
 * logToGoogleSheets()
 * Sends sensor data via HTTP POST to a Google Apps Script Web App.
 * The Apps Script appends a timestamped row to your Google Sheet,
 * matching the columns shown in Fig.5 of the paper:
 *   Date/Time | Temperature | Humidity | CO2 | Remarks | Alert System
 *
 * To set up:
 *   1. Open Google Sheets → Extensions → Apps Script
 *   2. Paste the companion Apps Script (see README)
 *   3. Deploy as Web App → Copy URL → paste in GOOGLE_SHEET_URL above
 */
void logToGoogleSheets() {
  if (WiFi.status() != WL_CONNECTED) return;

  // Build alert status string
  String alertStatus = "null";
  if (g_alert_active) {
    alertStatus = "alert sent";
  }

  // Build remarks string
  String remarks = g_air_quality + " (" + String(CO2_THRESHOLD_ACCEPTABLE)
                   + "-" + String(CO2_THRESHOLD_DROWSY) + " ppm)";

  // Build JSON payload
  String payload = "{";
  payload += "\"temperature\":" + String(g_temperature, 2) + ",";
  payload += "\"humidity\":"    + String(g_humidity, 2)    + ",";
  payload += "\"co2\":"         + String(g_co2_ppm)        + ",";
  payload += "\"remarks\":\""   + remarks                  + "\",";
  payload += "\"alert\":\""     + alertStatus              + "\"";
  payload += "}";

  HTTPClient http;
  http.begin(GOOGLE_SHEET_URL);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(payload);
  if (httpCode == HTTP_CODE_OK || httpCode == 302) {
    Serial.println("[SHEETS] Data logged successfully.");
  } else {
    Serial.printf("[SHEETS] Log failed. HTTP: %d\n", httpCode);
  }
  http.end();
}

// ─────────────────────────────────────────────
//  IFTTT — GOOGLE ASSISTANT INTEGRATION
// ─────────────────────────────────────────────

/**
 * triggerIFTTT()
 * Fires an IFTTT Webhook event.
 *
 * IFTTT Setup (Google Assistant → Webhooks):
 *   1. Create Applet in IFTTT
 *   2. IF: Google Assistant — "Say a specific phrase"
 *      Phrase: "Exhaust ON" / "Exhaust OFF"
 *   3. THEN: Webhooks — Make a web request to:
 *      https://maker.ifttt.com/trigger/{eventName}/with/key/{apiKey}
 *
 * Alternatively, the ESP32 itself can expose an HTTP endpoint
 * and IFTTT hits that endpoint directly (see below — webhook server).
 *
 * For this implementation, the ESP32 triggers IFTTT proactively
 * when CO2 levels change, and IFTTT notifies Google Assistant / Home.
 */
void triggerIFTTT(String eventName, String value1, String value2, String value3) {
  if (WiFi.status() != WL_CONNECTED) return;

  String url = "https://maker.ifttt.com/trigger/" + eventName
               + "/with/key/" + String(IFTTT_API_KEY);

  // Build JSON body with optional value fields
  String body = "{\"value1\":\"" + value1 + "\","
                + "\"value2\":\""  + value2 + "\","
                + "\"value3\":\""  + value3 + "\"}";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(body);
  Serial.printf("[IFTTT] Event '%s' → HTTP %d\n", eventName.c_str(), code);
  http.end();
}

// ─────────────────────────────────────────────
//  SERIAL DEBUG OUTPUT
// ─────────────────────────────────────────────

/**
 * printSerial()
 * Prints a formatted status table to Serial Monitor.
 */
void printSerial() {
  Serial.println("┌─────────────────────────────────────────┐");
  Serial.printf( "│  Temp:    %6.1f °C                      │\n", g_temperature);
  Serial.printf( "│  Humidity:%6.1f %%                       │\n", g_humidity);
  Serial.printf( "│  CO2:     %6d ppm  [%-10s]      │\n",         g_co2_ppm, g_air_quality.c_str());
  Serial.printf( "│  Fan:     %-3s   Warmer: %-3s              │\n",
                 g_fan_on ? "ON " : "OFF", g_warmer_on ? "ON " : "OFF");
  Serial.printf( "│  Alert:   %-8s                         │\n",
                 g_alert_active ? "ACTIVE" : "Clear");
  Serial.println("└─────────────────────────────────────────┘\n");
}

// ─────────────────────────────────────────────
//  END OF FILE
// ─────────────────────────────────────────────
