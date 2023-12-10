// Define Blynk template ID, name, and auth token
#define BLYNK_TEMPLATE_ID "TMPL6cfzxkgHU"
#define BLYNK_TEMPLATE_NAME "Proyek Akhir"
#define BLYNK_AUTH_TOKEN "8M-vmzjXZwOeFVfgWry9Uf1BApmrdwTM"
#define BLYNK_PRINT Serial

#include "EmonLib.h"
#include <EEPROM.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>
#include "freertos/semphr.h"
#include "time.h"

LiquidCrystal_I2C lcd(0x27, 20, 4);
SemaphoreHandle_t xMutex;

const float vCalibration = 51.0;
const float currCalibration = 0.07;

char ssid[] = "JKTMATH-2.4G";
char pass[] = "bintang123";

const long  gmtOffset_sec = 25200;
const int   daylightOffset_sec = 0;
const char* ntpServer = "asia.pool.ntp.org";

EnergyMonitor emon;

BlynkTimer timer;

float kWh = 0.0;
unsigned long lastMillis = millis();

const int addrVrms = 0;
const int addrIrms = 4;
const int addrPower = 8;
const int addrKWh = 12;

void sendEnergyDataToBlynk();
void readEnergyDataFromEEPROM();
void saveEnergyDataToEEPROM();

const int ledPin = LED_BUILTIN;
const char* mqtt_server = "mqtt-dashboard.com";
int mqtt_port = 8884;
const char* mqtt_username = "birujung";
const char* mqtt_password = "birujung";
const char* mqtt_topic = "smart-meter/output";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
bool isReconnecting = false;

void setup()
{
 Serial.begin(115200);
 Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

 //init and get the time
 configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

 xMutex = xSemaphoreCreateMutex();

 lcd.init();
 lcd.backlight();

 EEPROM.begin(32);

 readEnergyDataFromEEPROM();

 emon.voltage(35, vCalibration, 1.7);
 emon.current(34, currCalibration);

 timer.setInterval(5000L, sendEnergyDataToBlynk);

 delay(1000);

 pinMode(ledPin, OUTPUT);

 mqttClient.setServer(mqtt_server, mqtt_port);
 mqttClient.setCallback(callback);
}

void loop()
{
 Blynk.run();
 timer.run();

 if (!mqttClient.connected() && !isReconnecting) {
  isReconnecting = true;
  reconnect();
 }
 mqttClient.loop();
}

void printLocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.print(&timeinfo, "%H:%M:%S");
}

void sendEnergyDataToBlynk()
{
 if(xSemaphoreTake(xMutex, portMAX_DELAY)) {
  printLocalTime();
  Serial.println("\tMutex taken");
  emon.calcVI(20, 2000);

 unsigned long currentMillis = millis();
 kWh += emon.apparentPower * (currentMillis - lastMillis) / 3600000000.0;
 lastMillis = currentMillis;

 printLocalTime();
 Serial.printf("\tVrms: %.2fV\tIrms: %.4fA\tPower: %.4fW\tkWh: %.5fkWh\n",
               emon.Vrms, emon.Irms, emon.apparentPower, kWh);

 saveEnergyDataToEEPROM();

 Blynk.virtualWrite(V0, emon.Vrms);
 Blynk.virtualWrite(V1, emon.Irms);
 Blynk.virtualWrite(V2, emon.apparentPower);
 Blynk.virtualWrite(V3, kWh);

 int hoursPerDay = 6; // Contoh pemakaian 6 jam per hari
 const float CO2_PER_KWH = 623.28; // Carbon intensity in g of CO2 per kWh in Indonesia
 int daysPerYear = 365; // 365 days
 float dailyUsage = kWh * hoursPerDay;
 float dailyCarbonFootprint = dailyUsage * CO2_PER_KWH;
 float yearlyCarbonFootprint = dailyCarbonFootprint * daysPerYear;
 float totalCarbonFt = yearlyCarbonFootprint / 1000.0;

 printLocalTime();
 Serial.printf("\tCarbon Footprint: %.5fkg/Year\n", totalCarbonFt);

 Blynk.virtualWrite(V4, totalCarbonFt);

 lcd.clear();
 lcd.setCursor(0, 0);
 lcd.print("Vrms: ");
 lcd.print(emon.Vrms, 2);
 lcd.print(" V");

 lcd.setCursor(0, 1);
 lcd.print("Irms: ");
 lcd.print(emon.Irms, 4);
 lcd.print(" A");

 lcd.setCursor(0, 2);
 lcd.print("kWh: ");
 lcd.print(kWh, 5);
 lcd.print(" kWh");

 lcd.setCursor(0, 3);
 lcd.print("Carbon Footprint: ");
 lcd.print(totalCarbonFt, 5);
 lcd.print(" kg/Year");

 if (totalCarbonFt > 18) {
  digitalWrite(ledPin, HIGH);
  bool publishSuccess = mqttClient.publish(mqtt_topic, "ALERT! YOU HAVE CONSUMED TOO MUCH ELECTRICITY");
  printLocalTime();
  Serial.println("\tPublished 'Alert' to " + String(mqtt_topic));
  Serial.print("Publish success: ");
  Serial.println(publishSuccess);
 } else {
  digitalWrite(ledPin, LOW);
  bool publishSuccess = mqttClient.publish(mqtt_topic, "You're fine~");
  printLocalTime();
  Serial.println("\tPublished 'None' to " + String(mqtt_topic));
  Serial.print("Publish success: ");
  Serial.println(publishSuccess);
 }
 xSemaphoreGive(xMutex);
 printLocalTime();
 Serial.println("\tMutex given");
 Serial.println("---------------------------------");
 }
}

void readEnergyDataFromEEPROM()
{
 EEPROM.get(addrKWh, kWh);

 if (isnan(kWh))
 {
   kWh = 0.0;
   saveEnergyDataToEEPROM();
 }
}

void saveEnergyDataToEEPROM()
{
 EEPROM.put(addrKWh, kWh);
 EEPROM.commit();
}

void callback(char* topic, byte* payload, unsigned int length) {
 Serial.print("Message arrived on topic: ");
 Serial.print(topic);
 Serial.print(". Message: ");
 String messageTemp;

 for (int i = 0; i < length; i++) {
   Serial.print((char)payload[i]);
   messageTemp += (char)payload[i];
 }
 Serial.println();
}

void reconnect() {
 if (!mqttClient.connected()) {
  Serial.print("[" + millis() + "]");
   Serial.print(" Attempting MQTT connection...");
   if (mqttClient.connect("ESP32Client - Tunjung")) {
      Serial.println("MQTT Connected!");
      mqttClient.subscribe(mqtt_topic);
      isReconnecting = false;
    } else {
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Retrying in 5 seconds...");
      delay(5000);
    }
  Serial.println("---------------------------------");
 }
}