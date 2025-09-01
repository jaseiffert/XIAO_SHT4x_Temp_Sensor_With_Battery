/*
# ################################################################################################ #
# File: main.cpp                                                                                   #
# Project: XIAO ESP32C6 SHT4x Temperature Sensor With Battery                                      #
# Created Date: Friday, August 15th 2025, 12:44 pm                                                 #
# Author: Jeffery Seiffert                                                                         #
# -----                                                                                            #
# Last Modified: Sun Aug 31 2025                                                                   #
# Modified By: Jeffery Seiffert                                                                    #
# -----                                                                                            #
# Copyright (c) 2025 Jeffery A. Seiffert                                                           #
#                                                                                                  #
# GNU General Public License v3.0                                                                  #
# -----                                                                                            #
# HISTORY:                                                                                         #
# Date      	By	Comments                                                                       #
# ----------	---	----------------------------------------------------------                     #
#                                                                                                  #
# ################################################################################################ #
*/

#include <Arduino.h>
#include <Esp.h>
#include <esp_sleep.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include <PubSubClient.h>
#include <Adafruit_SHT4x.h>

#include "configuration.h"
#include "secrets.h"

#define PRINT_DEBUG 1

#if PRINT_DEBUG == 1
#define debugp(x) Serial.print(x)
#define debugpln(x, ...) Serial.println(x)
#define debugpf(x, ...) Serial.printf(x)
#else
#define debugp(x)
#define debugpln(x, ...)
#define debugpf(x, ...)
#endif

#define FIVE_V_PIN D1
#define MQTT_BATT_VOLTAGE 0
#define I2C_SDA D4
#define I2C_SCL D5
#define BUTTON_PIN D2

#define mS_TO_S_FACTOR 1000ULL // Conversion factor for milliseconds to seconds
#define TIME_TO_SLEEP_mS 20    // Time ESP32 will go to sleep (in seconds)

#define mS_TO_M_FACTOR 60000ULL // Conversion factor for milliseconds to minute
#define SLEEP_DELAY_M 1         // Time ESP32 will go to sleep (in Minutes)

// MQTT: ID, server IP, port, username and password
const PROGMEM char* MQTT_CLIENT_ID = MQTT_CLIENTID;
const PROGMEM char* MQTT_SERVER_IP = MQTT_SERVERIP;
const PROGMEM uint16_t MQTT_SERVER_PORT = MQTT_SERVERPORT;
const PROGMEM char* MQTT_USER = MQTTUSER;
const PROGMEM char* MQTT_PASSWORD = MQTTPWD;
// MQTT: topic
const PROGMEM char* MQTT_SENSOR_TOPIC = MQTT_SENSORTOPIC;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

Adafruit_SHT4x sht4 = Adafruit_SHT4x();

void sendMQTT();
void goToSleep();
String checkTemperature();
void setupWiFi();
void WiFiEvent(WiFiEvent_t event);
void print_wakeup_reason(esp_sleep_wakeup_cause_t wakeup_reason);
void callback(char* p_topic, byte* p_payload, unsigned int p_length);
String checkBatteryVoltage();
void setupSHT4x();

/* Your WiFi Credentials */
const char* ssid = WIFI_USER;    // SSID
const char* password = WIFI_PWD; // Password

unsigned long sleep_time = 0;
unsigned long last_sleep_time = 0;

void setup() {

    Serial.begin(115200);
    delay(1000); // Take some time to open up the Serial Monitor

    // Uncomment the 5 lines below to activate the external Antenna
    // pinMode(WIFI_ENABLE, OUTPUT);   // pinMode(3, OUTPUT);
    // digitalWrite(WIFI_ENABLE, LOW); // digitalWrite(3, LOW); // Activate RF switch control
    // delay(100);
    // pinMode(WIFI_ANT_CONFIG, OUTPUT);    // pinMode(14, OUTPUT);
    // digitalWrite(WIFI_ANT_CONFIG, HIGH); // digitalWrite(14, HIGH); // Use external antenna

    setupWiFi();

    pinMode(A0, INPUT); // ADC
    // For detecting if USB is plugged in or not
    pinMode(FIVE_V_PIN, INPUT);

    // initialize the pushbutton pin as an input:
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // LED Onboard the XIAO ESP32C6
    pinMode(LED_BUILTIN, OUTPUT);

    setupSHT4x();

    // ESP Deep Sleep Wakeup Reason
    esp_sleep_wakeup_cause_t wakeup_reason;
    wakeup_reason = esp_sleep_get_wakeup_cause();

    // Print the wakeup reason for ESP32
    print_wakeup_reason(wakeup_reason);

    // To test if the GPIO is a valid wakeup pin
    // if (esp_sleep_is_valid_wakeup_gpio(GPIO_NUM_2))
    // {
    //     debugpln("GPIO Number 2 is a valid wakeup pin.");
    // }

    // Timer wakeup code
    if (esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_uS * uS_TO_S_FACTOR) == ESP_OK) {
        debugpln("Starting Sleep Timer");
    }
    else {
        debugpln("Error Starting Sleep Timer");
    }
    // For Testing - It's hard to catch it at the right moment to upload code with the timer.
    // Jumpering BUTTON_PIN to ground will wake it up, then you can start your upload.
    if (esp_deep_sleep_enable_gpio_wakeup(BIT(BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW) == ESP_OK) // GPIO2
    {
        debugpln("Starting Sleep Button");
    }
    else {
        debugpln("Error Starting Sleep Button");
    }

    // init the MQTT connection
    mqttClient.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
    mqttClient.setCallback(callback);

    if (wakeup_reason != ESP_SLEEP_WAKEUP_GPIO) {
        digitalWrite(LED_BUILTIN, HIGH);
        sendMQTT();
        delay(5000);
        digitalWrite(LED_BUILTIN, LOW);
        goToSleep();
    }
} // void setup()

void loop() {
} // void loop()

void goToSleep() {
    WiFi.disconnect(true); // disconnect and turn off wifi
    delay(1000);
    WiFi.mode(WIFI_OFF);
    debugpln("Going to sleep now");
    Serial.flush();
    esp_deep_sleep_start();
    debugpln("This will never be printed");

} // void goToSleep()

void sendMQTT() {
    String mqttData = "{";
    mqttData += checkTemperature();
    mqttData += ",";
    mqttData += checkBatteryVoltage();
    mqttData += ",";
    mqttData += "\"wifisig\":\"" + String(WiFi.RSSI()) + "\"";
    mqttData += "}";

    debugpln("Attempting MQTT connection...");

    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
        debugpln("Connected to MQTT Server");
    }
    else {
        debugpln("Error Connecting to MQTT Server");
    }
    if (mqttClient.connected()) {
        debugpln(mqttData);
        if (mqttClient.publish(MQTT_SENSOR_TOPIC, mqttData.c_str(), true)) {
            debugpln("MQTT Message sent successfully");
        }
        else {
            debugpln("Error sending MQTT Message");
        }

        debugpln("INFO: Closing the MQTT connection");
        mqttClient.disconnect();
    } // if (mqttClient.connected())
}

String checkTemperature() {
    sensors_event_t humidity, temp;
    String tempUnit = "";
    float currentTemp = 0;
    float currentHumidity = 0;

    uint32_t timestamp = millis();
    sht4.getEvent(&humidity, &temp); // populate temp and humidity objects with fresh data
    timestamp = millis() - timestamp;

    if (FAHRENHEIT) {
        currentTemp = (temp.temperature * 9.0 / 5.0) + 32.0;
        tempUnit = " F";
    }
    else {
        currentTemp = temp.temperature;
        tempUnit = " C";
    }
    currentHumidity = humidity.relative_humidity;

    debugp("Temperature: ");
    debugp(currentTemp);
    debugpln(tempUnit);

    debugp("Humidity: ");
    debugp(currentHumidity);
    debugpln("% rH");

    debugp("Read duration (ms): ");
    debugpln(timestamp);

    String data = "";
    data += "\"temperature\":\"" + String(currentTemp) + "\",";
    // data += "\"tempunit\":\"" + tempUnit + "\",";
    data += "\"humidity\":\"" + String(currentHumidity) + "\"";
    debugp("Data String: ");
    debugpln((data));

    return data;
}

void setupWiFi() {
    // Connect WiFi
    debugpln("---------- Start WiFi Connect ----------");
    int connectCount = 0;
    // Auto reconnect is set true as default
    // To set auto connect off, use the following function
    WiFi.setAutoReconnect(true);

    WiFi.onEvent(WiFiEvent);

    debugpln(F("Connect to WiFi"));

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    debugpln("Connecting");
    while (WiFi.status() != WL_CONNECTED && connectCount < 101) {
        delay(500);
        debugp(".");
        connectCount++;
    }
    debugpln("");
    debugp("Connected to WiFi network with IP Address: ");
    debugpln(WiFi.localIP());
    debugpln("---------- End WiFi Connect ----------");

} // void setupWiFi()

void WiFiEvent(WiFiEvent_t event) {
    // debugpf("[WiFi-event] event: %d\n", event);

    switch (event) {
    case ARDUINO_EVENT_WIFI_READY:
        debugpln("WiFi interface ready");
        break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
        debugpln("Completed scan for access points");
        break;
    case ARDUINO_EVENT_WIFI_STA_START:
        debugpln("WiFi client started");
        break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
        debugpln("WiFi clients stopped");
        break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        debugpln("Connected to access point");
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        debugpln("Disconnected from WiFi access point");
        // digitalWrite(LED_BLUE, HIGH);
        break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
        debugpln("Authentication mode of access point has changed");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        debugp("Obtained IP address: ");
        debugpln(WiFi.localIP());
        debugp("WiFi Signal Strength: ");
        debugpln(WiFi.RSSI());
        // digitalWrite(LED_BLUE, LOW);
        break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        debugpln("Lost IP address and IP address is reset to 0");
        break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
        debugpln("WiFi Protected Setup (WPS): succeeded in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
        debugpln("WiFi Protected Setup (WPS): failed in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
        debugpln("WiFi Protected Setup (WPS): timeout in enrollee mode");
        break;
    case ARDUINO_EVENT_WPS_ER_PIN:
        debugpln("WiFi Protected Setup (WPS): pin code in enrollee mode");
        break;
    case ARDUINO_EVENT_WIFI_AP_START:
        debugpln("WiFi access point started");
        break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
        debugpln("WiFi access point  stopped");
        break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        debugpln("Client connected");
        break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        debugpln("Client disconnected");
        break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        debugpln("Assigned IP address to client");
        break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
        debugpln("Received probe request");
        break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
        debugpln("AP IPv6 is preferred");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
        debugpln("STA IPv6 is preferred");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP6:
        debugpln("Ethernet IPv6 is preferred");
        break;
    case ARDUINO_EVENT_ETH_START:
        debugpln("Ethernet started");
        break;
    case ARDUINO_EVENT_ETH_STOP:
        debugpln("Ethernet stopped");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        debugpln("Ethernet connected");
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        debugpln("Ethernet disconnected");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        debugpln("Obtained IP address");
        break;
    default:
        break;
    }
} // void WiFiEvent(WiFiEvent_t event)

// Method to print the reason by which ESP32 has been awaken from sleep
void print_wakeup_reason(esp_sleep_wakeup_cause_t wakeup_reason) {
    switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
        debugpln("Wakeup caused by external signal using RTC_IO");
        break;
    case ESP_SLEEP_WAKEUP_EXT1:
        debugpln("Wakeup caused by external signal using RTC_CNTL");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        debugpln("Wakeup caused by timer");
        break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        debugpln("Wakeup caused by touchpad");
        break;
    case ESP_SLEEP_WAKEUP_ULP:
        debugpln("Wakeup caused by ULP program");
        break;
    case ESP_SLEEP_WAKEUP_GPIO:
        debugpln("Wakeup caused by GPIO");
        break;
    default:
        debugpf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
        break;
    }
} // void print_wakeup_reason()

// function called when a MQTT message arrived
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {
}

// Check Battery Voltage and send to MQTT Client
String checkBatteryVoltage() {
    bool fiveVoltPin = digitalRead(FIVE_V_PIN);
    debugp("USB is Plugged In: ");
    debugpln(fiveVoltPin);
    float Vbattf = 0.0;
    String pwrSource = "";
    // If USB Plugged In, don't measure battery
    if (!fiveVoltPin) {
        // Check battery voltage
        pwrSource = "On Battery";
        uint32_t Vbatt = 0;
        for (int i = 0; i < 16; i++) {
            Vbatt = Vbatt + analogReadMilliVolts(A0); // ADC with correction
        }
        Vbattf = 2 * Vbatt / 16 / 1000.0; // attenuation ratio 1/2, mV --> V
        debugpln(Vbattf, 3);
    }
    else {
        pwrSource = "On USB";
    }
    String data = "";
    data += "\"batt\":\"" + String(Vbattf) + "\",";
    data += "\"pwr\":\"" + pwrSource + "\"";

    debugp("Data String: ");
    debugpln((data));

    return data;
} // void checkBatteryVoltage()

void setupSHT4x() {
    debugpln("Adafruit SHT4x test");
    if (!sht4.begin()) {
        debugpln("Couldn't find SHT4x");
        while (1)
            delay(1);
    }
    debugpln("Found SHT4x sensor");
    debugp("Serial number 0x");
    debugpln(sht4.readSerial(), HEX);

    // You can have 3 different precisions, higher precision takes longer
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    switch (sht4.getPrecision()) {
    case SHT4X_HIGH_PRECISION:
        debugpln("High precision");
        break;
    case SHT4X_MED_PRECISION:
        debugpln("Med precision");
        break;
    case SHT4X_LOW_PRECISION:
        debugpln("Low precision");
        break;
    }

    // You can have 6 different heater settings
    // higher heat and longer times uses more power
    // and reads will take longer too!
    sht4.setHeater(SHT4X_NO_HEATER);
    switch (sht4.getHeater()) {
    case SHT4X_NO_HEATER:
        debugpln("No heater");
        break;
    case SHT4X_HIGH_HEATER_1S:
        debugpln("High heat for 1 second");
        break;
    case SHT4X_HIGH_HEATER_100MS:
        debugpln("High heat for 0.1 second");
        break;
    case SHT4X_MED_HEATER_1S:
        debugpln("Medium heat for 1 second");
        break;
    case SHT4X_MED_HEATER_100MS:
        debugpln("Medium heat for 0.1 second");
        break;
    case SHT4X_LOW_HEATER_1S:
        debugpln("Low heat for 1 second");
        break;
    case SHT4X_LOW_HEATER_100MS:
        debugpln("Low heat for 0.1 second");
        break;
    }
} // void setupSHT4x()
