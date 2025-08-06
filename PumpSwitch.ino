#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


const char* ssid = "Thor God Of Thunder";
const char* password = "12345678";
const char* apiUrl = "https://pumpswitch.com/PumpSwitch/public/api/motor-get-data/60Ggdng2zEUWn2n4";
const char* changeRYB = "https://pumpswitch.com/PumpSwitch/public/api/changeelectricity/60Ggdng2zEUWn2n4";
const char* stopmotor = "https://pumpswitch.com/PumpSwitch/public/api/stopmotor/60Ggdng2zEUWn2n4";

// ✅ Safe GPIO pins
#define LED_PIN 2  // Onboard LED
#define R_PIN 16
#define Y_PIN 17
#define B_PIN 18

// ✅ State tracking
int lastR = -1, lastY = -1, lastB = -1;

void setup() {
  Serial.begin(115200);

  // ✅ Setup pins
  pinMode(LED_PIN, OUTPUT);
  pinMode(R_PIN, INPUT_PULLDOWN);
  pinMode(Y_PIN, INPUT_PULLDOWN);
  pinMode(B_PIN, INPUT_PULLDOWN);




  // ✅ Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Connected to WiFi!");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
}

void loop() {


  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi lost! Rebooting...");
    ESP.restart();
  }

  // ✅ Read pins
  int R = digitalRead(R_PIN);
  int Y = digitalRead(Y_PIN);
  int B = digitalRead(B_PIN);

  // ✅ Send POST only if changed
  if (R != lastR || Y != lastY || B != lastB) {
    sendPostRequest(R, Y, B);
    digitalWrite(LED_PIN, LOW);
    lastR = R;
    lastY = Y;
    lastB = B;
  }

  sendGetRequest();

  vTaskDelay(pdMS_TO_TICKS(5000));  // Non-blocking 5s delay
}

// ✅ Send GET request to fetch LED state
void sendGetRequest() {
  Serial.println("🌐 Sending GET...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, apiUrl);
  http.setTimeout(10000);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    Serial.println("✅ GET Response: " + payload);

    StaticJsonDocument<200> doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (!err) {
      int M = doc["data"]["M"];  // ✅ correct

      int R = digitalRead(R_PIN);
      int Y = digitalRead(Y_PIN);
      int B = digitalRead(B_PIN);
      if (R == HIGH && Y == HIGH && B == HIGH) {
        digitalWrite(LED_PIN, M ? HIGH : LOW);
        Serial.println(M ? "💡 LED ON" : "💡 LED OFF");
      } else {
        if (R == LOW || Y == LOW || B == LOW) {

          digitalWrite(LED_PIN, LOW);
          Serial.println("⚠️ Electricity gone! Sending stopmotor GET...");

          WiFiClientSecure stopClient;
          stopClient.setInsecure();
          HTTPClient stopHttp;

          stopHttp.begin(stopClient, stopmotor);
          int stopCode = stopHttp.GET();

          if (stopCode == 200) {
            String stopResponse = stopHttp.getString();
            Serial.println("✅ stopmotor Response: " + stopResponse);
          } else {
            Serial.println("❌ stopmotor GET failed. Code: " + String(stopCode));
          }

          stopHttp.end();
        }
      }

    } else {
      Serial.println("❌ JSON Parse Failed");
    }
  } else {
    Serial.println("❌ GET failed. Code: " + String(code));
  }

  http.end();
}

// ✅ Send POST with R/Y/B states
void sendPostRequest(int R, int Y, int B) {
  Serial.println("📤 Sending POST...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, changeRYB);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<200> doc;
  doc["R"] = R;
  doc["Y"] = Y;
  doc["B"] = B;

  String payload;
  serializeJson(doc, payload);

  Serial.println("POST Payload: " + payload);

  int code = http.POST(payload);
  if (code > 0) {
    String res = http.getString();
    Serial.println("✅ POST Response Code: " + String(code));
    Serial.println("Response: " + res);
  } else {
    Serial.println("❌ POST Failed. Code: " + String(code));
  }

  http.end();
}
