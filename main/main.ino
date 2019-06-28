/**
 * Bonsai AIO - Copyright (C) 2019 - present by Javinator9889
 * 
 * Control water level, temperature,
 * time and bonsai light with an Arduino board.
 */

// Web control & WiFi libraries
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
//#include <WiFiClientSecureBearSSL.h>

// Components specific libraries
#include <LiquidCrystal.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

// Ticker timing
#include <Ticker.h>
#include <SimpleTimer.h>

// Other libraries
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Thread.h>

// ESP8266 pinout
#include "PinConstants.h"

// Statistics library
#include "Statistics.h"

// Custom display modes
#include "DisplayModes.h"

// API Keys
#include "ApiKeys.h"

// Define whether the DEVMODE is active
// for saving sketch size - set to 0 for
// disabling
#define DEVMODE     1
// VVV - extra verbosity
#define VVV         0

// Maximum stats we will keep in memory
#define MAX_STATS   256

// Strings & URLs that will be used
#define IPIFY_URL       "http://api.ipify.org/"
//#define IPSTACK_URL     "http://api.ipstack.com/%s?access_key=%s"
#define TIMEZONE_DB_URL "http://api.timezonedb.com/v2.1/get-time-zone?key=%s&format=json&by=position&lat=%s&lng=%s"
//#define IPGEOLOCATION_URL "https://api.ipgeolocation.io/ipgeo?apiKey=%s&ip=%s&fields=time_zone"
//#define IPGEOLOCATION_BASE "https://api.ipgeolocation.io"
#define IPAPI             "http://ip-api.com/json/%s"
#define EXTREME_IP_URL    "http://extreme-ip-lookup.com/json/"

// OTA static constant values
#define OTA_URL           "http://ota.javinator9889.com"
#define OTA_PORT          80
#define OTA_PATH          "/esp8266/update/arduino.php"
#define RUNNING_VERSION   "0.9b"

//#define IPSTACK_MAX     82
#define TIMEZONE_DB_MAX 114
#define IPAPI_MAX         38
#define IPGEOLOCATION_MAX 114
#define EXTREME_IP_MAX  34

// Other "define" constants
//#define UTC_OFFSET  3600 // UTC+1
#define DHT_TYPE    DHT11
#define DHT_PIN     D1

// Web control objects
ESP8266WebServer  Server;
AutoConnect       Portal(Server);
AutoConnectConfig config;

// Statistics object
Statistics tempStats(MAX_STATS);
Statistics humdStats(MAX_STATS);
Statistics waterLevelStats(MAX_STATS);

// Time components
WiFiUDP ntpUDP;
NTPClient ntp(ntpUDP, "0.europe.pool.ntp.org");
Thread clockThread = Thread();

// Components pins
const struct {
  uint8_t rs;
  uint8_t e;
  
  uint8_t d4;
  uint8_t d5;
  uint8_t d6;
  uint8_t d7;
} LCD_PINS = {D7, D6, D5, D4, D3, D2};

const uint8_t BUTTON_PIN = D9;
const uint8_t WATER_LEVEL_DATA_PIN = A0;
const uint8_t COLUMNS = 16;
const uint8_t ROWS = 2;

// Init components
/*LiquidCrystal lcd(LCD_PINS.rs, 
                  LCD_PINS.e, 
                  LCD_PINS.d4, 
                  LCD_PINS.d5, 
                  LCD_PINS.d6, 
                  LCD_PINS.d7);*/
DHT_Unified dht(DHT_PIN, DHT_TYPE);
                  
// Global variables needed in hole project
volatile uint8_t displayMode;
#if DEVMODE
  volatile uint32_t setupFinishedTime;
  volatile uint32_t cpuEvents;
  volatile uint32_t cpuTicksPerSecond;
  volatile uint32_t waterLevelWaitingTime;
  volatile uint32_t aSecond = 0;
  bool printed = false;
#endif

// Constant structs that contains
// application data
volatile struct {
  float waterLevelSensor;
  float tempHumdSensor;
  float clockSeconds;
} waitingTimes = {18, 9000, 1};

struct {
  SimpleTimer dhtSensor;
  Ticker waterSensor;
  Ticker clockControl;
} sensors;

struct {
  float latestTemperature;
  float latestHumidity;
  bool hasTempChanged;
  bool hasHumdChanged;
} dhtValues = {0.0, 0.0, true, true};

struct {
  uint16_t waterValue;
  bool hasWaterValueChanged;
} waterLevelValues = {0, true};

struct {
  String formattedTime;
  String formattedDate;
  bool hasTimeChanged;
  bool hasDateChanged;
  bool mustShowSeparator;
} dataTime = {"00:00 00", "1900-01-01", true, true, true};

struct {
  String ip;
  String lat;
  String lng;
  uint16_t offset; 
} geolocationInformation = {"", "", "", 0};

#if DEVMODE
  struct {
    volatile uint32_t latestTickerExecution;
    volatile uint32_t latestWaterExecution;
    volatile uint32_t latestDHTExecution;
  } executionTimes = {0, 0, 0};
#endif

// ArduinoJson constants
/*const size_t IPSTACK_BUFFER_SIZE = JSON_ARRAY_SIZE(5) + 5 * JSON_OBJECT_SIZE(3) + 
                                   JSON_OBJECT_SIZE(8) + JSON_OBJECT_SIZE(13);*/
//const size_t IPAPI_BUFFER_SIZE = 3 * JSON_OBJECT_SIZE(14);
const size_t TIMEZONE_DB_BUFFER_SIZE = JSON_OBJECT_SIZE(13) + 226;
const size_t EXTREME_IP_BUFFER_SIZE = JSON_OBJECT_SIZE(15) + 286;
//const size_t IPGEOLOCATION_BUFFER_SIZE = JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(6);

// Define the functions that will be 
// available
void initAutoConnect();
void initDHT();
void rootPage();
//void initCpuTicksPerSecond();
String generateRandomString();
ICACHE_RAM_ATTR void changeDisplayMode();
void launchClockThread();
void updateTime();
void updateDHTInfo();
void updateWaterLevelInfo();
bool areTimesDifferent(String time1, String time2);
void getIPAddress();
void setupLatituteLongitude();
void getTimezoneOffset();
void setupClock();
void lookForOTAUpdates();


void setup() {
  #if DEVMODE
    Serial.begin(9600);
    Serial.println();
    Serial.println();
    Serial.println();

    for (uint8_t t = 4; t > 0; --t) {
      Serial.printf("[SERIAL SETUP] Wait %d...\n", t);
      Serial.flush();
      delay(1000);
    }
    while (!Serial) {
      continue;
    }
    Serial.println(F("Serial initialized"));
  #endif
  // Initialize the seed with a no connected pin
  randomSeed(analogRead(0));

  Serial.println(F("Starting web server and cautive portal"));
  // Start the web server and cautive portal
  Server.on("/", rootPage);
  if (Portal.begin()) {
    #if DEVMODE
      Serial.println(F("Successfully connected to Internet!"));
      Serial.print(F("HTTP server:")); Serial.println(WiFi.localIP().toString());
    #endif
  } else {
    #if DEVMODE
      Serial.println(F("Error connecting to Internet"));
    #endif
  }

  // Init components
//  lcd.begin(COLUMNS, ROWS);
  
  /*digitalWrite(D1, LOW); // sets output to gnd
  pinMode(D1, OUTPUT); // switches power to DHT on
  delay(1000); // delay necessary after power up for DHT to stabilize*/
//  dht.begin();
  initDHT();
  
  // Global variables definition
  #if DEVMODE
    cpuTicksPerSecond     = 0;
    cpuEvents             = 0;
    waterLevelWaitingTime = 0;
  #endif
  displayMode           = DEFAULT_MODE;
  
  #if DEVMODE
    Serial.print(F("Setup elapsed time: "));
    Serial.println(millis());
  #endif

  // Setup clock data - correct timezone (offset)
  setupClock();
  #if DEVMODE
//    Serial.print(F("Public IP address: "));
//    Serial.println(geolocationInformation.ip);
    Serial.print(F("Latitude: "));
    Serial.println(geolocationInformation.lat);
    Serial.print(F("Longitude: "));
    Serial.println(geolocationInformation.lng);
    Serial.print(F("Timezone offset: "));
    Serial.println(geolocationInformation.offset);
  #endif

  // Init the NTP client - if we are here there is Internet connection
  ntp.setTimeOffset(geolocationInformation.offset);
  ntp.begin();

  // Register button interruption
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), changeDisplayMode, CHANGE);
  
  // Setup timers
  clockThread.onRun(updateTime);
  clockThread.setInterval(1000);

  sensors.dhtSensor.setInterval(waitingTimes.tempHumdSensor, updateDHTInfo);
  sensors.waterSensor.attach(waitingTimes.waterLevelSensor, updateWaterLevelInfo);
  sensors.clockControl.attach(waitingTimes.clockSeconds, launchClockThread);
  
  updateDHTInfo();
  updateWaterLevelInfo();
  updateTime();
  
  setupFinishedTime = millis();
}


void loop() {
  #if DEVMODE
    if (dataTime.hasDateChanged) {
      Serial.print(F("Date: "));
      Serial.println(dataTime.formattedDate);
      dataTime.hasDateChanged = false;
      delay(10);
    }
    if (dataTime.hasTimeChanged) {
      Serial.print(F("Time: "));
      Serial.println(dataTime.formattedTime);
      dataTime.hasTimeChanged = false;
      delay(10);
    }
    if (dhtValues.hasTempChanged) {
      Serial.print(F("Temp: ")); Serial.println(String(dhtValues.latestTemperature) + " ºC");
      dhtValues.hasTempChanged = false;
      delay(10);
    }
    if (dhtValues.hasHumdChanged) {
      Serial.print(F("Humd: ")); Serial.println(String(dhtValues.latestHumidity) + " %");
      dhtValues.hasHumdChanged = false;
      delay(10);
    }
    if (waterLevelValues.hasWaterValueChanged) {
      Serial.print(F("Water level: ")); Serial.println(String(waterLevelValues.waterValue));
      waterLevelValues.hasWaterValueChanged = false;
      delay(10);
    }
  #endif
  sensors.dhtSensor.run();
//  delay(1000);
//  updateDHTInfo();
  /*Serial.print(F("Temp: ");
  Serial.print(dhtValues.latestTemperature);
  Serial.println(F(" ºC");
  Serial.print(F("Humd: ");
  Serial.print(dhtValues.latestHumidity);
  Serial.println(F(" %");*/
  /*
   // Do not start the code until we know
   // how many ticks happens each second
   if (cpuTicksPerSecond == 0) {
    initCpuTicksPerSecond();
    return;
  }
  #if DEVMODE
    if (!printed) {
      Serial.print(F("Ticks per second: ");
      Serial.println(cpuTicksPerSecond);
      printed = true;
    }
    if (aSecond == cpuTicksPerSecond) {
      Serial.println(F("A second has passed");
      aSecond = 0;
    } else {
      ++aSecond;
    }
  #endif
  if (waterLevelWaitingTime == waitingTimes.waterLevelSensor) {
    int waterValue = analogRead(WATER_LEVEL_DATA_PIN);
    #if DEVMODE
      Serial.print(F("Valor del agua: ");
      Serial.println(waterValue);
    #endif
    waterLevelWaitingTime = 0;
  } else {
    ++waterLevelWaitingTime;
  }*/
  Portal.handleClient();
}


void initAutoConnect(String password) {
  config.apid = "BonsaiAIO";
  config.psk = password;
}

void initDHT() {
  // Initialize device.
  dht.begin();
  // Print temperature sensor details.
  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  #if DEVMODE
  Serial.println(F("------------------------------------"));
  Serial.println(F("Temperature Sensor"));
  Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
  Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
  Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
  Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("°C"));
  Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("°C"));
  Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("°C"));
  Serial.println(F("------------------------------------"));
  #endif
  // Print humidity sensor details.
  dht.humidity().getSensor(&sensor);
  #if DEVMODE
  Serial.println(F("Humidity Sensor"));
  Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
  Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
  Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
  Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("%"));
  Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("%"));
  Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("%"));
  Serial.println(F("------------------------------------"));
  #endif
}

void rootPage() {
  char content[] = "Hello, world";
  Server.send(200, "text/plain", content);
}

/**
 * Unused since "Ticker" installation
 */
/*void initCpuTicksPerSecond() {
  uint32_t timeDifference = (uint32_t) (millis() - setupFinishedTime);
  ++cpuEvents;
  if (timeDifference >= 999) {
    cpuTicksPerSecond = cpuEvents;
    waitingTimes.waterLevelSensor *= cpuTicksPerSecond;
    waitingTimes.tempHumdSensor *= cpuTicksPerSecond;
    waitingTimes.clockSeconds *= cpuTicksPerSecond;
  }
}*/

String generateRandomString() {
  const char* letters = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int position = -1;
  String randomString = "";
  for (int i = 0; i < 8; i++) {
    position = random(62);
    randomString += letters[position];
  }
  return randomString;
}

void changeDisplayMode() {
  // TODO
}

void launchClockThread() {
//  if (clockThread.shouldRun())
  clockThread.run();
}

void updateTime() {
  #if DEVMODE
    uint32_t currentTime = millis();
    uint32_t diff = currentTime - executionTimes.latestTickerExecution;
    Serial.print(F("Latest time execution about ")); Serial.println(String(diff) + " ms. ago");
    executionTimes.latestTickerExecution = currentTime;
  #endif
  if (WiFi.status() == WL_CONNECTED) {
    ntp.update();

    // Format the date
    String ntpFormattedDate = ntp.getFormattedDate();
    int splitIndexForDate = ntpFormattedDate.indexOf("T");
    String formattedDate = ntpFormattedDate.substring(0, splitIndexForDate);
    if (formattedDate != dataTime.formattedDate) {
      dataTime.formattedDate = formattedDate;
      dataTime.hasDateChanged = true;
    } else {
      dataTime.hasDateChanged = false;
    }

    // Format the time
    String ntpTime = ntpFormattedDate.substring((splitIndexForDate + 1), (ntpFormattedDate.length() - 1));
    String formattedTime = ntpTime.substring(0, 2);
    formattedTime += (dataTime.mustShowSeparator) ? ":" : " ";
    formattedTime += ntpTime.substring(3, 5) + " " + ntpTime.substring(6, 8);
    if (areTimesDifferent(formattedTime, dataTime.formattedTime)) {
      dataTime.formattedTime = formattedTime;
      dataTime.mustShowSeparator = !dataTime.mustShowSeparator;
      dataTime.hasTimeChanged = true;
    } else {
      dataTime.hasTimeChanged = false;
    }
  } else {
    dataTime.formattedTime = "No Internet!";
    dataTime.formattedDate = "";
  }
}

void updateDHTInfo() {
  #if DEVMODE
    uint32_t currentTime = millis();
    uint32_t diff = currentTime - executionTimes.latestDHTExecution;
    Serial.print(F("Latest DHT execution about ")); Serial.println(String(diff) + " ms. ago");
    executionTimes.latestDHTExecution = currentTime;
  #endif*/
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) {
    #if DEVMODE
    Serial.println(F("Error reading temperature!"));
    #endif
  }
  else {
    #if VVV
    Serial.print(F("Temperature: "));
    Serial.print(event.temperature);
    Serial.println(F("°C"));
    #endif
    if (event.temperature != dhtValues.latestTemperature) {
      dhtValues.latestTemperature = event.temperature;
      dhtValues.hasTempChanged = true;
    }
  }
  // Get humidity event and print its value.
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) {
    #if DEVMODE
    Serial.println(F("Error reading humidity!"));
    #endif
  }
  else {
    #if VVV
    Serial.print(F("Humidity: "));
    Serial.print(event.relative_humidity);
    Serial.println(F("%"));
    #endif
    if (event.relative_humidity != dhtValues.latestHumidity) {
      dhtValues.latestHumidity = event.relative_humidity;
      dhtValues.hasHumdChanged = true;
    }
  }
}

void updateWaterLevelInfo() {
  #if DEVMODE
    uint32_t currentTime = millis();
    uint32_t diff = currentTime - executionTimes.latestWaterExecution;
    Serial.print(F("Latest water execution about ")); Serial.print(String(diff) + " ms. ago");
    executionTimes.latestWaterExecution = currentTime;
  #endif  
  uint16_t currentWaterValue = analogRead(WATER_LEVEL_DATA_PIN);
  if (currentWaterValue != waterLevelValues.waterValue) {
    waterLevelValues.waterValue = currentWaterValue;
    waterLevelValues.hasWaterValueChanged = true;
  }
}

bool areTimesDifferent(String time1, String time2) {
  #if VVV
    Serial.println(time1.substring(0, 2) + " | " + time2.substring(0, 2));
    Serial.println(time1.substring(3, 5) + " | " + time2.substring(3, 5));
    Serial.println(time1.substring(6, 8) + " | " + time2.substring(6, 8));
  #endif
  if (time1.substring(0, 2) != time2.substring(0, 2))
    return true;
  else if (time1.substring(3, 5) != time2.substring(3, 5))
    return true;
  else if (time1.substring(6, 8) != time2.substring(6, 8))
    return true;
  else
    return false;
}

void getIPAddress() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    #if DEVMODE
      Serial.print(F("ipify URL: "));
      Serial.println(IPIFY_URL);
    #endif
    http.begin(IPIFY_URL);
    if (http.GET() == 200) {
      geolocationInformation.ip = http.getString();
    }
    http.end();
  }
}

void setupLatituteLongitude() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    #if DEVMODE
      Serial.print(F("EXTREME URL: "));
      Serial.println(EXTREME_IP_URL);
    #endif
    http.begin(EXTREME_IP_URL);
    int httpCode = http.GET();
    if (httpCode > 0) {
      DynamicJsonDocument jsonBuffer(EXTREME_IP_BUFFER_SIZE);
      DeserializationError error = deserializeJson(jsonBuffer, http.getString());
      if (error == DeserializationError::Ok) {
        #if DEVMODE
          Serial.println(jsonBuffer["lat"].as<float>());
          Serial.println(jsonBuffer["lon"].as<float>());
        #endif
        geolocationInformation.lat = jsonBuffer["lat"].as<String>();
        geolocationInformation.lng = jsonBuffer["lon"].as<String>();
      } else {
        #if DEVMODE
          Serial.print(F("\"IP-API\" deserializeJson() failed with code "));
          Serial.println(error.c_str());
        #endif
      }
      jsonBuffer.clear();
    }
    else {
      #if DEVMODE
      Serial.printf("[HTTPS] GET... failed, error: %s\n\r", http.errorToString(httpCode).c_str());
      #endif
    }
    http.end();
  }
}

void getTimezoneOffset() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    char *formattedUrl = new char[TIMEZONE_DB_MAX];
    sprintf(formattedUrl, TIMEZONE_DB_URL, TIMEZONE_DB, geolocationInformation.lat.c_str(), geolocationInformation.lng.c_str());
    #if DEVMODE
      Serial.print(F("TIMEZONE DB URL: "));
      Serial.println(formattedUrl);
    #endif
    http.begin(formattedUrl);
    int httpCode = http.GET();
    if (httpCode == 200) {
      DynamicJsonDocument jsonBuffer(TIMEZONE_DB_BUFFER_SIZE);
      DeserializationError error = deserializeJson(jsonBuffer, http.getString());
      if (error == DeserializationError::Ok) {
        #if DEVMODE
          Serial.println(jsonBuffer["gmtOffset"].as<int>());
        #endif
        geolocationInformation.offset = jsonBuffer["gmtOffset"].as<int>();
      } else {
        #if DEVMODE
          Serial.print("\"timezone\" deserializeJson() failed with code ");
          Serial.println(error.c_str());
        #endif
      }
      jsonBuffer.clear();
    } else {
      #if DEVMODE
      Serial.printf("[HTTPS] GET... failed, error: %s\n\r", http.errorToString(httpCode).c_str());
      #endif
    }
    delete[] formattedUrl;
    http.end();
  }
}

void setupClock() {
  setupLatituteLongitude();
  getTimezoneOffset();
}

void lookForOTAUpdates() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    #if DEVMODE
    ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);
    #endif
    t_httpUpdate_return returnValue = ESPhttpUpdate.update(OTA_URL, OTA_PORT, OTA_PATH, RUNNING_VERSION);
    switch(returnValue) {
    case HTTP_UPDATE_FAILED:
      #if DEVMODE
        Serial.println("[update] Update failed.");
      #endif
        break;
    case HTTP_UPDATE_NO_UPDATES:
      #if DEVMODE
        Serial.println("[update] Update no Update.");
      #endif
        break;
    case HTTP_UPDATE_OK:
      #if DEVMODE
        Serial.println("[update] Update ok."); // may not called we reboot the ESP
      #endif
        break;
    }
  }
}
