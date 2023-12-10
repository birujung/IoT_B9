// Define Blynk template ID, name, and auth token
#define BLYNK_TEMPLATE_ID "TMPL6cfzxkgHU"
#define BLYNK_TEMPLATE_NAME "Proyek Akhir"
#define BLYNK_AUTH_TOKEN "8M-vmzjXZwOeFVfgWry9Uf1BApmrdwTM"
#define BLYNK_PRINT Serial

// MQTT configuration
#define mqtt_server "broker.hivemq.com"
#define mqtt_port 8884
#define mqtt_topic "smart-meter/output"

// Include necessary libraries
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

// Initialize LCD and mutex
LiquidCrystal_I2C lcd(0x27, 20, 4);
SemaphoreHandle_t xMutex;

// Calibration values
const float vCalibration = 51.0;
const float currCalibration = 0.07;

// WiFi credentials
char ssid[] = "JKTMATH-2.4G";
char pass[] = "bintang123";
uint32_t lastPublish = 0;

// NTP server configuration
const long  gmtOffset_sec = 25200;
const int   daylightOffset_sec = 0;
const char* ntpServer = "asia.pool.ntp.org";

// Energy monitoring
EnergyMonitor emon;

// Blynk timer
BlynkTimer timer;

// Energy variables
float kWh = 0.0;
unsigned long lastMillis = millis();

// EEPROM addresses
const int addrVrms = 0;
const int addrIrms = 4;
const int addrPower = 8;
const int addrKWh = 12;

void sendEnergyDataToBlynk();
void readEnergyDataFromEEPROM();
void saveEnergyDataToEEPROM();

// LED Pin
const int ledPin = LED_BUILTIN;

// WiFi and MQTT clients
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
bool isReconnecting = false;

void setup()
{
  // Start serial communication
  Serial.begin(115200);

  // Initialize Blynk
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Set up MQTT server and callback
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callback);

  // Initialize time synchronization
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Create mutex
  xMutex = xSemaphoreCreateMutex();

  // Initialize LCD
  lcd.init();
  lcd.backlight();

  // Initialize EEPROM
  EEPROM.begin(32);

  // Read energy data from EEPROM
  readEnergyDataFromEEPROM();

  // Initialize energy monitor
  emon.voltage(35, vCalibration, 1.7);
  emon.current(34, currCalibration);

  // Set up Blynk timer interval
  timer.setInterval(5000L, sendEnergyDataToBlynk);

  // Delay for stability
  delay(1000);

  // Set up LED pin
  pinMode(ledPin, OUTPUT);
}

void loop()
{
  // Run Blynk and timer
  Blynk.run();
  timer.run();

  // Create and run MQTT task on a separate core
  xTaskCreatePinnedToCore(
    mqttTask,           // Function to execute
    "MQTT_Task",        // Task name
    8192,               // Stack size
    NULL,               // Parameters to pass to the function
    1,                  // Priority (1 is lower than loop())
    NULL,               // Task handle
    1             // Run on the second core
  );

  // Reconnect to MQTT if necessary
  if (!mqttClient.connected() && !isReconnecting) {
    isReconnecting = true;
    reconnect();
  }
  
  mqttClient.loop();

  // Publish message to MQTT Topic every 50 seconds
  if (millis() - lastPublish >= 50000 && mqttClient.connected()) {
    lastPublish = millis();
    mqttClient.publish(mqtt_topic, "Using electricity from lamp.");
    printLocalTime();
    Serial.println("\tPublished message to MQTT Topic");
  }
}

// Print local time
void printLocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.print(&timeinfo, "%H:%M:%S");
}

// MQTT task
void mqttTask(void *pvParameters) {
  (void)pvParameters;

  while (true) {
    // Handle MQTT events
    if (!mqttClient.connected() && !isReconnecting) {
      isReconnecting = true;
      reconnect();
    }
    mqttClient.loop();
    
    // Sleep for a short duration to allow other tasks to run
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Send energy data to Blynk
void sendEnergyDataToBlynk()
{
  if(xSemaphoreTake(xMutex, portMAX_DELAY)) {
    printLocalTime();
    Serial.println("\tMutex taken");
    emon.calcVI(20, 2000);

    // Calculation for kWh
    unsigned long currentMillis = millis();
    kWh += emon.apparentPower * (currentMillis - lastMillis) / 3600000000.0;
    lastMillis = currentMillis;

    printLocalTime();
    Serial.printf("\tVrms: %.2fV\tIrms: %.4fA\tPower: %.4fW\tkWh: %.5fkWh\n",
                  emon.Vrms, emon.Irms, emon.apparentPower, kWh);

    saveEnergyDataToEEPROM();

    // Write to Blynk
    Blynk.virtualWrite(V0, emon.Vrms);
    Blynk.virtualWrite(V1, emon.Irms);
    Blynk.virtualWrite(V2, emon.apparentPower);
    Blynk.virtualWrite(V3, kWh);

    // Initialize for Carbon Footprint
    int hoursPerDay = 6; // Average usage per lamp is 6 hours
    const float CO2_PER_KWH = 623.28; // Carbon intensity in g of CO2 per kWh in Indonesia
    int daysPerYear = 365; // 365 days

    // Calculation for Carbon Footprint
    float dailyUsage = kWh * hoursPerDay;
    float dailyCarbonFootprint = dailyUsage * CO2_PER_KWH;
    float yearlyCarbonFootprint = dailyCarbonFootprint * daysPerYear;
    float totalCarbonFt = yearlyCarbonFootprint / 1000.0;

    printLocalTime();
    Serial.printf("\tCarbon Footprint: %.5fkg/Year\n", totalCarbonFt);
    Blynk.virtualWrite(V4, totalCarbonFt);

    // Write to LCD Display
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
    lcd.print("CO2: ");
    lcd.print(totalCarbonFt, 5);
    lcd.print(" kg/Year");

    // Write and Alert with LED BUILTIN
    if (totalCarbonFt > 18) {
      digitalWrite(ledPin, HIGH);
      printLocalTime();
      Serial.println("\tALERT! YOU HAVE CONSUMED TOO MUCH ELECTRICITY");
    } else {
      digitalWrite(ledPin, LOW);
      printLocalTime();
      Serial.println("\tYou're still using SAFE amount of electricity");
    }

    xSemaphoreGive(xMutex);
    printLocalTime();
    Serial.println("\tMutex given");
    Serial.println("---------------------------------");
  }
}

// Read energy data from EEPROM
void readEnergyDataFromEEPROM()
{
  EEPROM.get(addrKWh, kWh);

  if (isnan(kWh)) {
    kWh = 0.0;
    saveEnergyDataToEEPROM();
  }
}

// Save energy data to EEPROM
void saveEnergyDataToEEPROM()
{
  EEPROM.put(addrKWh, kWh);
  EEPROM.commit();
}

// MQTT callback function
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

// Reconnect to MQTT
void reconnect() {
  if (!mqttClient.connected()) {
    Serial.println("Attempting MQTT connection...");
    if (mqttClient.connect("ESP32Client - B9")) {
      Serial.println("MQTT Connected!");
      mqttClient.subscribe(mqtt_topic);
      Serial.println("Subscribed to MQTT Topic");
      isReconnecting = false;
    } else {
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Retrying in 5 seconds...");
      delay(5000);
    }
  }
}