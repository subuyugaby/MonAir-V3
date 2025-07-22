#include <WiFi.h>
#include <ArduinoOTA.h>

const char* ssid_sta = "galileo";
const char* password_sta = "";
//const char* ssid_sta = "CLARO1_C6CC23";
//const char* password_sta = "Z6951UXSBQ";

const char* ap_ssid = "ESP32_OTA_AP";
const char* ap_password = "12345678";

const char* ota_password = "lui";

unsigned long lastCheck = 0;
int previousClients = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[BOOT] Starting ESP32 in AP+STA mode...");

  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.mode(WIFI_AP_STA);

  // STA connection
  WiFi.begin(ssid_sta, password_sta);
  Serial.print("[WIFI] Connecting to STA ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WIFI] Connected to STA network");
  Serial.print("[WIFI] STA IP Address: ");
  Serial.println(WiFi.localIP());

  // AP setup with fixed channel
  bool ap_result = WiFi.softAP(ap_ssid, ap_password, 6);
  if (ap_result) {
    Serial.println("[AP] ESP32 AP started");
    Serial.print("[AP] AP IP Address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("[AP] Failed to start AP!");
  }

  // OTA
  ArduinoOTA.setHostname("esp32-dual");
  ArduinoOTA.setPassword(ota_password);

  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Start updating...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Update finished!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("[OTA] Ready for uploads");
}

void loop() {
  ArduinoOTA.handle();

  // Check number of AP clients every 10s
  if (millis() - lastCheck > 10000) {
    lastCheck = millis();
    int clients = WiFi.softAPgetStationNum();
    if (clients != previousClients) {
      Serial.printf("[AP] Connected clients: %d\n", clients);
      previousClients = clients;
    }
  }
}
