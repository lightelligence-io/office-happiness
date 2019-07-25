#include "config.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

struct Config {
  const char *wifiSsid = WIFI_SSID;
  const char *wifiPassword = WIFI_PASSWORD;
  const char *deviceIdentifier = DEVICE_UUID;
  const char *mqttServer = "olt-gw-lite.local";
  const uint16_t mqttPort = 1883;
} cfg;


String clientName = "ESP8266Client-";
WiFiClient wifiClient;
PubSubClient client(wifiClient);

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

  client.setServer(cfg.mqttServer, cfg.mqttPort);
}

boolean reconnect() {
  if (!client.connected()) {
    if (client.connect((char*) clientName.c_str())) {
        Serial.printf("===> mqtt connected by %s\n", clientName.c_str());
        client.publish(std::string("device/"+std::string(cfg.deviceIdentifier)+"/configuration/ip").c_str(), WiFi.localIP().toString().c_str());
    } else {
        Serial.print("---> mqtt failed, rc=");
        Serial.println(client.state());
    }
  }
  return client.connected();
}

void loop() {
  static bool connected = false;

  if (WiFi.status() == WL_CONNECTED) {
    if (!connected) {
      Serial.printf("Wifi connected: ip=%s\r\n", WiFi.localIP().toString().c_str());
      reconnect();
    }
    
    connected = true;
  } else {
    if (connected) {
      connected = false;
      Serial.println("Lost wifi connection.");
    }
  }
}
