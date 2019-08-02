#include "config.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <MQ135.h>

struct Config {
  const char *wifiSsid = WIFI_SSID;
  const char *wifiPassword = WIFI_PASSWORD;
  const char *deviceIdentifier = DEVICE_UUID;
  const char *mqttServer = "olt-gw-lite.local";
  const uint16_t mqttPort = 1883;
  const int pinMotion = D6;
} cfg;

DHT_Unified dht(D5, DHT11);

MQ135 gasSensor = MQ135(A0);

String clientName = "ESP8266Client-";
WiFiClient wifiClient;
PubSubClient client(wifiClient);

long nextEnvCheck = 0;
const int numReadings = 1000;
long nextMotionCheck = 0;
long nextMotionSend = 0;
int readings[numReadings];
int readIndex = 0;
int total = 0;
float average = 0;

void setup() {
  Serial.begin(115200);
  delay(100);
  
  WiFi.mode(WIFI_STA);
  Serial.printf("Configuring wifi: %s\r\n", cfg.wifiSsid);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPassword);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    // wait 1 second for re-trying
    delay(1000);
  }
  
  delay(1000);

  pinMode(cfg.pinMotion, INPUT);
  dht.begin();

  client.setServer(cfg.mqttServer, cfg.mqttPort);
}

boolean reconnect() {
  if (!client.connected()) {
    if (client.connect((char*) clientName.c_str())) {
        Serial.printf("===> mqtt connected by %s\n", clientName.c_str());
        sendConfiguration("ip", WiFi.localIP().toString().c_str());
    } else {
        Serial.print("---> mqtt failed, rc=");
        Serial.println(client.state());
    }
  }
  return client.connected();
}

boolean sendConfiguration(const char* key, const char* value) {
  Serial.printf("====> sending configuration %s: value=%s\r\n", key, value);
  return client.publish(std::string("device/"+std::string(cfg.deviceIdentifier)+"/configuration/"+std::string(key)).c_str(), value);
}

boolean sendAttribute(const char* key, const char* value) {
  Serial.printf("====> sending attribute %s: value=%s\r\n", key, value);
  return client.publish(std::string("device/"+std::string(cfg.deviceIdentifier)+"/attributes/"+std::string(key)).c_str(), value);
}

void loop() {
  static bool connected = false;

  if (WiFi.status() == WL_CONNECTED) {
    if (!connected) {
      Serial.printf("Wifi connected: ip=%s\r\n", WiFi.localIP().toString().c_str());
      connected = reconnect();
    }
  } else {
    if (connected) {
      connected = false;
      Serial.println("Lost wifi connection.");
    }
  }

  if (connected && millis() > nextEnvCheck) {
    // Read DHT
    sensors_event_t event;
    dht.temperature().getEvent(&event);
    if (!isnan(event.temperature)) {
      sendAttribute("temperature", String(event.temperature).c_str());
    }
    dht.humidity().getEvent(&event);
    if (!isnan(event.relative_humidity)) {
      sendAttribute("humidity", String(event.relative_humidity).c_str());
    }

    // Read gas sensor
    if (!isnan(event.relative_humidity) && !isnan(event.temperature)) {
      float ppm = gasSensor.getCorrectedPPM(event.temperature, event.relative_humidity);
      sendAttribute("ppm", String(ppm).c_str());
    }

    nextEnvCheck += 30000;
  }

  if (connected && millis() > nextMotionCheck) {
     // Read motion sensor
     total = total - readings[readIndex];
     readings[readIndex] = digitalRead(cfg.pinMotion);
     total = total + readings[readIndex];
     average = ((float) total / numReadings);
     if (millis() > nextMotionSend) {
       Serial.printf("PIR state is %d (total %d), latest value %d\r\n", average, total, readings[readIndex]);
       sendAttribute("motion", String(average).c_str());
       nextMotionSend += 30000;
    }
    readIndex = readIndex + 1;
    if (readIndex >= numReadings) {
      readIndex = 0;
    }
    nextMotionCheck += 10;
  }

  client.loop();
}
