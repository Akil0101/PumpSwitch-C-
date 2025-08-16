/******************** ESP32 + A7670C/SIM7600 — PumpSwitch over HTTP ********************
 * - NO HTTPS used: plain TinyGsmClient on port 80
 * - If your server still forces HTTPS, you will see status 301 here.
 * - UART to modem: RX=26, TX=27 (keeps GPIO16/17/18 free for R/Y/B)
 **************************************************************************************/

// ===== TinyGSM config (must be BEFORE TinyGsmClient.h) =====
#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_USE_GPRS   true
#define TINY_GSM_USE_WIFI   false
//#define TINY_GSM_DEBUG Serial  // uncomment to see AT logs

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

// ====== API host & paths (HTTP ONLY) ======
const char* HOST         = "pumpswitch.com";
const int   PORT         = 80;   // <-- HTTP (no TLS)
const char* PATH_GET     = "/PumpSwitch/public/api/motor-get-data/60Ggdng2zEUWn2n4";
const char* PATH_POST    = "/PumpSwitch/public/api/changeelectricity/60Ggdng2zEUWn2n4";
const char* PATH_STOP    = "/PumpSwitch/public/api/stopmotor/60Ggdng2zEUWn2n4";

// ====== SIM/APN ======
// Normal Vi SIM:
const char* APN      = "www";
// Vi IoT SIM (if you use it, they may need to whitelist your host):
// const char* APN   = "m2m.vodafone.in";
const char* APN_USER = "";
const char* APN_PASS = "";

// ====== ESP32 UART to modem (HardwareSerial 1) ======
HardwareSerial SerialAT(1);
#define MODEM_RX     16   // ESP32 RX1  <-- modem TXD
#define MODEM_TX     17   // ESP32 TX1  --> modem RXD
#define MODEM_PWRKEY 4    // active-LOW pulse ~1.5s

// ====== Your GPIOs (same as your WiFi version) ======
#define LED_PIN 2
#define R_PIN 18
#define Y_PIN 19
#define B_PIN 21

int  lastR = -1, lastY = -1, lastB = -1;
bool stopSent = false;

// ====== TinyGSM modem & PLAIN client ======
TinyGsm modem(SerialAT);
TinyGsmClient netClient(modem);           // <-- PLAIN (HTTP)
HttpClient http(netClient, HOST, PORT);

// ====== Helpers ======
static void pulsePwrKey() {
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(100);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1100);                  // ~1.1s pulse; adjust per your board
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(2000);
}

static bool modemConnect() {
  Serial.println(F("-> Powering and starting modem (HTTP mode)..."));
  pulsePwrKey();

  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(700);

  // Normalize session (ignore failures)
  for (int i=0;i<3;i++){ SerialAT.print("AT\r"); delay(120); }
  SerialAT.print("ATE0\r");         delay(120);
  SerialAT.print("AT+IFC=0,0\r");   delay(120);
  SerialAT.print("AT+IPR=115200\r");delay(120);
  SerialAT.print("AT&W\r");         delay(150);

  if (!modem.init()) { Serial.println(F("modem.init() failed, restart()")); modem.restart(); }

  String imei = modem.getIMEI();
  Serial.print(F("IMEI: ")); Serial.println(imei);

  if (!modem.waitForNetwork(60000)) { Serial.println(F("No network")); return false; }
  Serial.println(F("Network attached"));

  if (!modem.gprsConnect(APN, APN_USER, APN_PASS)) { Serial.println(F("Data attach failed")); return false; }
  Serial.println(F("LTE data attached"));

  IPAddress ip = modem.localIP();
  Serial.print(F("Local IP: ")); Serial.println(ip);

  netClient.setTimeout(10000);
  http.setTimeout(15000);
  Serial.println(F("Mode: HTTP:80 (no TLS)"));
  return true;
}

static bool ensureData() {
  if (!modem.isNetworkConnected() && !modem.waitForNetwork(30000)) return false;
  if (!modem.isGprsConnected() && !modem.gprsConnect(APN, APN_USER, APN_PASS)) return false;
  return true;
}

// ====== HTTP helpers ======
static bool httpGET(const char* path, int &statusOut, String &bodyOut) {
  if (!ensureData()) return false;

  http.beginRequest();
  http.get(path);
  // Minimal headers that help some hosts:
  http.sendHeader("Host", HOST);
  http.sendHeader("User-Agent", "ESP32-A7670C/1.0");
  http.sendHeader("Accept", "application/json");
  http.sendHeader("Connection", "close");  // avoid keep-alive quirks
  http.endRequest();

  statusOut = http.responseStatusCode();   // <= 0 means socket/open failed
  bodyOut   = http.responseBody();
  http.stop();
  return (statusOut > 0);
}

static bool httpPOSTjson(const char* path, const String& json, int &statusOut, String &bodyOut) {
  if (!ensureData()) return false;

  http.beginRequest();
  http.post(path);
  http.sendHeader("Host", HOST);
  http.sendHeader("User-Agent", "ESP32-A7670C/1.0");
  http.sendHeader("Content-Type", "application/json");
  http.sendHeader("Content-Length", json.length());
  http.sendHeader("Connection", "close");
  http.endRequest();
  http.write((const uint8_t*)json.c_str(), json.length());

  statusOut = http.responseStatusCode();
  bodyOut   = http.responseBody();
  http.stop();
  return (statusOut > 0);
}

// ====== Your logic, ported to HTTP ======
void sendGetRequest() {
  Serial.println(F("🌐 Sending GET (HTTP)..."));
  int status = 0; String payload;
  if (!httpGET(PATH_GET, status, payload)) {
    Serial.print(F("❌ GET begin error: ")); Serial.println(status);
    return;
  }

  Serial.print(F("GET status: ")); Serial.println(status);
  if (status == 200) {
    Serial.println("✅ GET Response: " + payload);

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      int M = doc["data"]["M"] | 0;

      int R = digitalRead(R_PIN);
      int Y = digitalRead(Y_PIN);
      int B = digitalRead(B_PIN);

      if (R == HIGH && Y == HIGH && B == HIGH) {
        digitalWrite(LED_PIN, M ? HIGH : LOW);
        Serial.println(M ? F("💡 LED ON") : F("💡 LED OFF"));
        stopSent = false;
      } else if ((R == LOW || Y == LOW || B == LOW) && M == HIGH && !stopSent) {
        digitalWrite(LED_PIN, LOW);
        Serial.println(F("⚠️ Electricity gone! Sending stopmotor..."));
        int st2=0; String resp2;
        if (httpGET(PATH_STOP, st2, resp2)) {
          Serial.print(F("STOP status: ")); Serial.println(st2);
          if (st2 == 200) { Serial.println("✅ stopmotor: " + resp2); stopSent = true; }
        } else {
          Serial.println(F("❌ stopmotor begin error"));
        }
      }
    } else {
      Serial.println(F("❌ JSON Parse Failed"));
    }
  } else {
    // If your host still redirects to HTTPS you’ll likely see 301 here.
    Serial.println(F("❌ GET failed (non-200). If 301, server is still forcing HTTPS."));
  }
}

void sendPostRequest(int R, int Y, int B) {
  Serial.println(F("📤 Sending POST (HTTP)..."));

  StaticJsonDocument<200> doc;
  doc["R"]=R; doc["Y"]=Y; doc["B"]=B;
  String payload; serializeJson(doc, payload);
  Serial.println("POST Payload: " + payload);

  int status=0; String resp;
  if (!httpPOSTjson(PATH_POST, payload, status, resp)) {
    Serial.print(F("❌ POST begin error: ")); Serial.println(status);
    return;
  }

  Serial.print(F("POST status: ")); Serial.println(status);
  Serial.println("Response: " + resp);
}

// ========================= Arduino entry points =========================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(R_PIN, INPUT_PULLDOWN);
  pinMode(Y_PIN, INPUT_PULLDOWN);
  pinMode(B_PIN, INPUT_PULLDOWN);

  if (!modemConnect()) Serial.println(F("Modem bring-up failed. Check power/APN."));
  else Serial.println(F("Modem ready."));
}

void loop() {
  if (!ensureData()) {
    Serial.println(F("Data not ready; retrying in 3s..."));
    delay(3000);
    return;
  }

  int R = digitalRead(R_PIN);
  int Y = digitalRead(Y_PIN);
  int B = digitalRead(B_PIN);

  if (R != lastR || Y != lastY || B != lastB) {
    digitalWrite(LED_PIN, LOW);  // match your behavior
    sendPostRequest(R, Y, B);
    lastR = R; lastY = Y; lastB = B;
  }

  sendGetRequest();
  delay(5000);
}
