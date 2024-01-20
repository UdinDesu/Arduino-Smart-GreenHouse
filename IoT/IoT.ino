#include <WiFi.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

#define DHTPIN 26
#define SOIL_PIN 32
#define RELAY_PIN1 16
#define RELAY_PIN2 5
#define RELAY_PIN3 17
#define DHTTYPE DHT11
#define MQTT_SERVER "test.mosquitto.org"
#define MQTT_PORT 1883
#define MQTT_TOPIC_SOIL "home/kelembapan_tanah"
#define MQTT_TOPIC_TEMPERATURE "home/temperature"
#define MQTT_TOPIC_HUMIDITY "home/humidity"
#define MQTT_TOPIC_RELAY1 "home/relay1"
#define MQTT_TOPIC_RELAY2 "home/relay2"
#define MQTT_TOPIC_RELAY3 "home/relay3"
#define MQTT_TOPIC_TANAH "home/tanah"
#define MQTT_TOPIC_SUHU "home/suhu"
#define MQTT_TOPIC_HIDUP "home/hidup"
#define MQTT_TOPIC_MATI "home/mati"

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7L * 60L * 60L;
const int daylightOffset_sec = 3600;

DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);

#define WIFI_SSID "Udn"
#define WIFI_PASSWORD "DolByX123"

float old_hum = 0;
float old_temp = 0;
int old_soil = 0;

long time_1 = 0;
int interval = 500;
int tanah = 0;
int suhu = 0;
short soilMin = 50;
short soilMax = 70;
short suhuMin = 25;
short suhuMax = 32;
short toleransi = 3;
bool pumpState, fanState, lampTime;
String hidup,mati;

LiquidCrystal_I2C lcd(0x27, 16, 2);

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message received on topic ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  // Control relay2 based on the received message
  if (strcmp(topic, MQTT_TOPIC_RELAY2) == 0) {
    if (message == "ON") {
      digitalWrite(RELAY_PIN2, LOW);
    } else if (message == "OFF") {
      digitalWrite(RELAY_PIN2, HIGH);
    }
  }

  if (strcmp(topic, MQTT_TOPIC_TANAH) == 0) {

    tanah = message.toInt();
    if (tanah > 100) tanah = 100;
    if (tanah < 0) tanah = 0;

    soilMin = tanah - toleransi;
    soilMax = tanah + toleransi;
    if (soilMin > 100) soilMin = 100;
    if (soilMax < 0) soilMax = 0;
  }

  if (strcmp(topic, MQTT_TOPIC_SUHU) == 0) {

    suhu = message.toInt();

    suhuMin = suhu - toleransi;
    suhuMax = suhu + toleransi;
  }

  if (strcmp(topic, MQTT_TOPIC_HIDUP) == 0) {
    hidup = message;
  }

  if (strcmp(topic, MQTT_TOPIC_MATI) == 0) {
    mati = message;
  }
}


void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  lcd.begin(16, 2);
  lcd.clear();

  Serial.println("DHT11 and Soil Moisture Sensor test!");
  dht.begin();

  pinMode(SOIL_PIN, INPUT);
  pinMode(RELAY_PIN1, OUTPUT);
  pinMode(RELAY_PIN2, OUTPUT);
  pinMode(RELAY_PIN3, OUTPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.print("Connected: ");
  Serial.println(WiFi.localIP());

  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Display ESP IP address on the LCD
  lcd.clear();
  lcd.print("MQTT: Connecting");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT broker...");
    if (client.connect("ESP8266_Client")) {
      Serial.println("connected");
      // Subscribe to the relay2 topic
      client.subscribe(MQTT_TOPIC_RELAY2);
      client.subscribe(MQTT_TOPIC_TANAH);
      client.subscribe(MQTT_TOPIC_SUHU);
      client.subscribe(MQTT_TOPIC_HIDUP);
      client.subscribe(MQTT_TOPIC_MATI);
      lcd.clear();
      lcd.print("MQTT: Connected");
      lcd.setCursor(0, 1);
      lcd.print(WiFi.localIP().toString());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  readSensors();
}

void readSensors() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int soilValue = analogRead(SOIL_PIN);
  short max = 3500;
  short min = 1100;
  if (soilValue <= min) soilValue = min;
  if (soilValue >= max) soilValue = max;
  soilValue = map(soilValue, min, max, 100, 0);

  if (!pumpState && (soilValue <= soilMin)) {
    pumpState = true;
    digitalWrite(RELAY_PIN1, LOW);
  }
  if (pumpState && (soilValue >= soilMax)) {
    pumpState = false;
    digitalWrite(RELAY_PIN1, HIGH);
  }

  if (!fanState && (t >= suhuMin)) {
    fanState = true;
    digitalWrite(RELAY_PIN3, LOW);
  }
  if (fanState && (t <= suhuMax)) {
    fanState = false;
    digitalWrite(RELAY_PIN3, HIGH);
  }

  if (!lampTime && (getTime() == hidup)) {
    lampTime = true;
    digitalWrite(RELAY_PIN2, LOW);
  }
  if (lampTime && (getTime() == mati)) {
    lampTime = false;
    digitalWrite(RELAY_PIN2, HIGH);
  }


  if (h != old_hum || t != old_temp || soilValue != old_soil) {
    if (millis() >= time_1 + interval) {
      time_1 = millis();
      if (!isnan(h) && !isnan(t)) {
        Serial.print("Humidity: ");
        Serial.print(h);
        Serial.print(" %\t");
        Serial.print("Temperature: ");
        Serial.print(t);
        Serial.print(" *C \t ");
        Serial.print("Soil Moisture: ");
        Serial.print(soilValue);
        Serial.print(" % \t ");
        Serial.print("kalibrasi Tanah: ");
        Serial.print(tanah);
        Serial.print(" % \t ");
        Serial.print("kalibrasi Suhu: ");
        Serial.print(suhu);
        Serial.print(" % \t ");
        Serial.print("Waktu: ");
        Serial.print(getTime());
        Serial.print(" \t ");
        Serial.print("Waktu hidup: ");
        Serial.print(hidup);
        Serial.print(" \t ");
        Serial.print("Waktu mati: ");
        Serial.println(mati);
        // Publish sensor data to MQTT topics
        client.publish(MQTT_TOPIC_TEMPERATURE, String(t).c_str());
        client.publish(MQTT_TOPIC_HUMIDITY, String(h).c_str());
        client.publish(MQTT_TOPIC_SOIL, String(soilValue).c_str());
      } else {
        Serial.println("Failed to read from DHT sensor!");
      }
    }
    old_hum = h;
    old_temp = t;
    old_soil = soilValue;
  }
}
String getTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "Erorr get Time";
  }
  String time="";
  char timeHour[3];
  strftime(timeHour,3, "%H", &timeinfo);
  char timeMinute[3];
  strftime(timeMinute,3, "%M", &timeinfo);
  time+=String(timeHour);
  time+=":";
  time+=String(timeMinute);
  return time;
}
