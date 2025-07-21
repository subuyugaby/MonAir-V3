// ===== Librerías =====
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "bsec.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Adafruit_PM25AQI.h"
#include <ArduinoOTA.h>  // OTA

// ===== WiFi y OTA =====

const char* ssid_sta = "galileo";
const char* password_sta = "";
//const char *ssid = "CLARO1_C6CC23";     
//const char *password = "Z6951UXSBQ";
const char *ota_password = "lui";
const char *ap_ssid = "MonAirPrueba";
const char *ap_password = "12345678"; // Mínimo 8 caracteres

// ===== MQTT =====
const char *mqtt_server = "test.mosquitto.org";
const char *user = "";
const char *passwd = "";
const char *clientID = "airmonq_006570A0";

// ===== Dashboard & NeoPixel =====
#define TEAM_NAME "airmon/006570A0"
#define NEOPIXEL_PIN 25
#define NUMPIXELS 16

// ===== Variables Globales =====
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

Bsec iaqSensor;
String output, output2;

double temp, ds18, hume, pres = 0;
double aqi, sAQI, AQIa = 0;
double CO2e, VOCe, gas, rssi, pm10, pm25 = 0;
char msg[50];
char msg_r[50];
char topic_name[250];

const int pinDatosDQ = 32;
OneWire oneWireObjeto(pinDatosDQ);
DallasTemperature sensorDS18B20(&oneWireObjeto);

WiFiClient espClient;
PubSubClient mqtt_client(espClient);

Adafruit_PM25AQI particulas = Adafruit_PM25AQI();
PM25_AQI_Data data;

Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("[BOOT] Iniciando ESP32 en modo AP+STA");

  // Modo doble: se conecta a red + crea AP propio
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid_sta, password_sta);

  Serial.print("[WiFi] Conectando a red principal");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Conectado!");
  Serial.print("[WiFi] IP local (STA): ");
  Serial.println(WiFi.localIP());

  // Crear Access Point
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("[AP] Punto de acceso activo. IP: ");
  Serial.println(WiFi.softAPIP());

  // OTA
  ArduinoOTA.setHostname("esp32-dual");
  ArduinoOTA.setPassword(ota_password);
  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Inicio de actualización...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] ¡Actualización finalizada!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progreso: %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Fallo de autenticación");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Fallo en inicio");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Fallo de conexión");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Fallo de recepción");
    else if (error == OTA_END_ERROR) Serial.println("Fallo al finalizar");
  });
  ArduinoOTA.begin();
  Serial.println("[OTA] Listo para recibir nuevas versiones");

  setupMQTT();
  timeClient.begin();
  timeClient.setTimeOffset(-21600);

  Wire.begin(21, 22);
  iaqSensor.begin(0x77, Wire);
  output = "BSEC library version: " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
  Serial.println(output);

  bsec_virtual_sensor_t sensorList[10] = {
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
  };
  iaqSensor.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);

  pixels.begin();

  if (!particulas.begin_I2C()) {
    Serial.println("No se encontró el sensor PM2.5");
    while (1) delay(10);
  } else {
    Serial.println("Sensor PM25 detectado");
  }
}

// ===== LOOP =====
void loop() {
  ArduinoOTA.handle();

  if (WiFi.status() != WL_CONNECTED) {
    pixelSignals(255, 0, 0, 500);
    WiFi.begin(ssid_sta, password_sta);
    return;
  }

  mqtt_client.loop();

  if (mqtt_client.state() != 0) {
    pixelSignals(255, 255, 0, 500);
    reconnect();
  }

  timeClient.update();

  if (timeClient.getSeconds() == 15) {
    if ((WiFi.status() == WL_CONNECTED) && (mqtt_client.connect(clientID, user, passwd))) {
      mqtt_client.publish(getTopic("Online"), "Estacion en línea");
    }
    delay(1500);
  }

  if ((timeClient.getMinutes() % 10 == 0) && (timeClient.getSeconds() == 0)) {
    Serial.println("Publicando datos...");
    postData();
    pixelSignals(0, 0, 255, 1000);
    delay(1500);
  }

  if ((timeClient.getHours() % 23 == 0) && (timeClient.getMinutes() % 55 == 0) && (timeClient.getSeconds() % 55 == 0)) {
    ESP.restart();
  }

  preHeatSensor();
}

// ===== FUNCIONES =====
void setupMQTT() {
  pixels.setPixelColor(0, pixels.Color(255, 255, 0));
  pixels.show();
  mqtt_client.setServer(mqtt_server, 1883);
  mqtt_client.setCallback(callback);
}

void reconnect() {
  int cont = 1;
  while (!mqtt_client.connected()) {
    Serial.print("Intentando reconexión MQTT...");
    if (mqtt_client.connect(clientID, user, passwd)) {
      Serial.println("Conectado");
      pixelSignals(0, 255, 0, 500);
    } else {
      Serial.print("Fallo, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" reintentando en 5s");
      cont++;
      if (cont > 150) break;
      delay(5000);
    }
  }
}

void callback(char *topic, byte *payload, unsigned int length) {
  Serial.printf("Mensaje [%s]: ", topic);
  for (int i = 0; i < length; i++) msg_r[i] = (char)payload[i];
  msg_r[length] = 0;
  Serial.println(msg_r);
}

char *getTopic(char *topic) {
  sprintf(topic_name, "%s/%s", TEAM_NAME, topic);
  return topic_name;
}

void publish(char *topic, char *payload) {
  mqtt_client.publish(topic, payload);
}

void pixelSignals(int red, int green, int blue, int delay_time) {
  pixels.setPixelColor(0, pixels.Color(red, green, blue));
  pixels.show();
  delay(delay_time);
  pixels.clear();
  pixels.show();
  delay(delay_time);
}

void postData() {
  mqtt_client.setServer(mqtt_server, 1883);
  mqtt_client.setCallback(callback);
  delay(100);

  String missing = "Datos no enviados:\n";
  String sent = "Datos enviados: \n";

  if (mqtt_client.connect(clientID, user, passwd)) {
    temp = iaqSensor.temperature;
    hume = iaqSensor.humidity;
    pres = iaqSensor.pressure;
    aqi = iaqSensor.iaq;
    sAQI = iaqSensor.staticIaq;
    AQIa = iaqSensor.iaqAccuracy;
    gas = iaqSensor.gasResistance / 1000;
    CO2e = iaqSensor.co2Equivalent;
    VOCe = iaqSensor.breathVocEquivalent;
    rssi = WiFi.RSSI();

    sensorDS18B20.requestTemperatures();
    ds18 = sensorDS18B20.getTempCByIndex(0);

    float dataList[] = {temp, hume, pres, aqi, sAQI, AQIa, gas, CO2e, VOCe, rssi, ds18};
    const char *topics[] = {"temp", "hume", "pres", "aqi", "sAQI", "AQIa", "gas", "CO2e", "VOCe", "rssi", "ds18"};

    for (int i = 0; i < 11; i++) {
      String(dataList[i]).toCharArray(msg, 50);
      (mqtt_client.publish(getTopic((char *)topics[i]), msg)) ? sent += "\t- " + String(topics[i]) + "\n" : missing += topics[i];
    }

    if (particulas.read(&data)) {
      pm25 = data.pm25_env;
      pm10 = data.pm100_env;
      String(pm25).toCharArray(msg, 50);
      (mqtt_client.publish(getTopic("pm25"), msg)) ? sent += "\t- PM2.5\n" : missing += "PM2.5\n";
      String(pm10).toCharArray(msg, 50);
      (mqtt_client.publish(getTopic("pm10"), msg)) ? sent += "\t- PM10\n" : missing += "PM10\n";
    }

    Serial.println(sent);
    String log = (missing != "Datos no enviados:\n") ? missing : "Todos los datos enviados.";
    log.toCharArray(msg, 1000);
    mqtt_client.publish(getTopic("info"), msg);
  } else {
    Serial.println("MQTT NO conectado");
  }
}

void preHeatSensor() {
  unsigned long time_trigger = millis();
  if (iaqSensor.run()) {
    output2 = String(time_trigger);
    output2 += ", " + String(iaqSensor.rawTemperature);
    output2 += ", " + String(iaqSensor.pressure);
    output2 += ", " + String(iaqSensor.rawHumidity);
    output2 += ", " + String(iaqSensor.gasResistance);
    output2 += ", " + String(iaqSensor.iaq);
    output2 += ", " + String(iaqSensor.iaqAccuracy);
    output2 += ", " + String(iaqSensor.temperature);
    output2 += ", " + String(iaqSensor.humidity);
    output2 += ", " + String(iaqSensor.staticIaq);
    output2 += ", " + String(iaqSensor.co2Equivalent);
    output2 += ", " + String(iaqSensor.breathVocEquivalent);
  }
}
