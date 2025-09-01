// Program Configuration

// MQTT Configuration
#define MQTT_CLIENTID "sht41_remote_01"
#define MQTT_SERVERIP "192.168.15.92"
#define MQTT_SERVERPORT 1883
#define MQTT_SENSORTOPIC "familyroom/temp/sht4x_01"

// Temperature
#define FAHRENHEIT true // Convert Temperatures to Fahrenheit

// Display
#define DISPLAY_TITLE "SHT41 Temp Sensor" // Max of 20 characters
#define SIGNAL_STRENGTH_DB true // Display the signal strength on display in DB (true) or Percent (false)

// Sleep Time
#define uS_TO_S_FACTOR 1000000ULL // Conversion factor for microseconds to seconds DO NOT CHANGE
#define TIME_TO_SLEEP_uS 300       // 300 Seconds = 5 Minutes - Time ESP32 will go to sleep (in seconds)
