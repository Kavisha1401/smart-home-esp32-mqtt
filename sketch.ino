// Smart Home ESP32 node. Run one copy per room (hall / bedroom / kitchen).
// Wokwi libraries to add: PubSubClient, DHT sensor library
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ====== CHANGE THESE ======
#define ROOM "hall"
#define BASE "sh/lv/home/"
#define CLIENT_SUFFIX "me1"   // any unique text, must differ from your friend's copy
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
// ==========================

#define DHT_PIN 15
#define PIR_PIN 13
#define GAS_PIN 34
#define LDR_PIN 35
#define LIGHT_PIN 26
#define FAN_PIN 27

DHT dht(DHT_PIN, DHT22);
WiFiClient net;
PubSubClient mqtt(net);
unsigned long lastPub = 0, lastTry = 0;
bool gasAlarm = false;
bool wifiReported = false;

String topic(String s) { return String(BASE) + ROOM + "/" + s; }

void setDevice(const char* dev, bool on) {
  digitalWrite(strcmp(dev, "light") == 0 ? LIGHT_PIN : FAN_PIN, on);
  mqtt.publish(topic(String(dev) + "/state").c_str(), on ? "ON" : "OFF", true);
}

void onMessage(char* t, byte* p, unsigned int len) {
  String top(t), msg;
  for (unsigned int i = 0; i < len; i++) msg += (char)p[i];
  Serial.print("MQTT message: ");
  Serial.print(top);
  Serial.print(" = ");
  Serial.println(msg);
  bool on = (msg == "ON");
  if (top.endsWith("/light/set")) setDevice("light", on);
  else if (top.endsWith("/fan/set")) setDevice("fan", on);
}

void tryMqtt() {
  String id = String("esp32-") + ROOM + "-" + CLIENT_SUFFIX;
  Serial.print("MQTT connecting as ");
  Serial.println(id);
  if (mqtt.connect(id.c_str())) {
    Serial.println("MQTT connected");
    mqtt.subscribe(topic("+/set").c_str());
    setDevice("light", digitalRead(LIGHT_PIN));
    setDevice("fan", digitalRead(FAN_PIN));
  } else {
    Serial.print("MQTT failed, rc=");
    Serial.println(mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  dht.begin();
  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);   // channel 6 is required by Wokwi
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && !wifiReported) {
    wifiReported = true;
    Serial.println("WiFi connected");
  }

  if (WiFi.status() == WL_CONNECTED && !mqtt.connected() && millis() - lastTry > 5000) {
    lastTry = millis();
    tryMqtt();
  }
  mqtt.loop();

  if (millis() - lastPub > 2000) {
    lastPub = millis();
    float t = dht.readTemperature(), h = dht.readHumidity();
    if (isnan(t)) t = 0;
    if (isnan(h)) h = 0;
    int gas = map(analogRead(GAS_PIN), 0, 4095, 0, 1000);
    int lux = map(analogRead(LDR_PIN), 0, 4095, 0, 500);
    int m = digitalRead(PIR_PIN);

    // Device-level fail-safe: works even when Wi-Fi/broker is down
    if (gas > 400 && !gasAlarm) {
      gasAlarm = true;
      setDevice("fan", true);
      mqtt.publish((String(BASE) + "alerts").c_str(), (String("GAS LEAK in ") + ROOM).c_str());
    } else if (gas < 300) gasAlarm = false;

    char buf[120];
    snprintf(buf, sizeof(buf), "{\"t\":%.1f,\"h\":%.0f,\"lux\":%d,\"m\":%d,\"g\":%d}", t, h, lux, m, gas);
    mqtt.publish(topic("sensors").c_str(), buf);
    Serial.println(buf);
  }
}
