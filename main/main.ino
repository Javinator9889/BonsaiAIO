/**
   Bonsai AIO - Copyright (C) 2019 - present by Javinator9889

   Control water level, temperature,
   time and bonsai light with an Arduino board.
*/

// Web control & WiFi libraries
#include <Arduino.h>
#include <wiring_private.h>
#include <pins_arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFiMulti.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <AutoConnectDefs.h>

// Components specific libraries
#include <LiquidCrystal595.h>
#include <Wire.h>
//#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

// Ticker timing
#include <Ticker.h>
#include <SimpleTimer.h>

// Other libraries
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ThingSpeak.h>
#include <Time.h>

// EEPROM WiFi storage struct
extern "C" {
  #include "user_interface.h"
}

// Define whether the DEVMODE is active
// for saving sketch size - set to 0 for
// disabling
#define DEVMODE     0
// VVV - extra verbosity
#define VVV         0
// Enable or disable OTAs - disabled due to space
// restrictions
#define OTA_ENABLED 1
// Enable remote logging
#define SYSLOG      1

#if DEVMODE
#define PRINT_DEBUG_MESSAGES
#else
#undef AC_DEBUG
#endif

#if OTA_ENABLED
#ifdef AUTOCONNECT_USE_UPDATE
#undef AUTOCONNECT_USE_UPDATE
#endif
#endif

// AutoConnect libraries - must be defined here for OTA compatibility
#include <AutoConnect.h>
#include <AutoConnectCredential.h>
#include <AutoConnectAux.h>

#if OTA_ENABLED
#include <ESP8266httpUpdate.h>
#endif

#if SYSLOG
#include <Syslog.h>
#define SYSLOG_SERVER   "192.168.1.88"
#define SYSLOG_PORT     514
#define DEVICE_HOSTNAME "ESP8266"
#define APP_NAME        "BonsaiAIO"
#endif

// ThingSpeak publisher class
#include "ThingSpeakPublisher.h"

// Statistics library
#include "SensorStats.h"

// Custom display modes
#include "DisplayModes.h"

// API Keys
#include "ApiKeys.h"

// LCD Icons
#include "LCDIcons.h"

// HTML pages
#include "CustomHTML.h"

// Strings & URLs that will be used
#define TIMEZONE_DB_URL   "http://api.timezonedb.com/v2.1/get-time-zone?key=%s&format=json&by=position&lat=%s&lng=%s"
#define EXTREME_IP_URL    "http://extreme-ip-lookup.com/json/"

// OTA static constant values
#if OTA_ENABLED
#define OTA_URL           "http://ota.javinator9889.com/"
#define RUNNING_VERSION   "bonsaiaio.bin"
#endif

#define TIMEZONE_DB_MAX 114
#define EXTREME_IP_MAX  34

// Other "define" constants
#define NTP_SERVER  "pool.ntp.org"
#define DHT_TYPE    DHT11
#define CLEAR_ROW   "                "
#define UPPER_LIMIT 250
#define LOWER_LIMIT 120
#define EEPROM_SIZE 4096
#define EEPROM_CRCI 0x010
#define CREDENTIAL_OFFSET (sizeof(float) + sizeof(bool) + sizeof(short) + (2 * sizeof(long)) + (2 * sizeof(int)))

// Web control objects
WiFiClient client;
ESP8266WebServer      Server;
AutoConnect           Portal(Server);
AutoConnectConfig     config;
AutoConnectCredential credentials(CREDENTIAL_OFFSET);
ESP8266WiFiMulti      wifiMulti;

// Time components
WiFiUDP ntpUDP;
NTPClient ntp(ntpUDP, NTP_SERVER);

#if SYSLOG
Syslog syslog(ntpUDP, SYSLOG_SERVER, SYSLOG_PORT, DEVICE_HOSTNAME, APP_NAME, LOG_KERN);
#endif

// Components pins
const struct {
  uint8_t data;
  uint8_t latch;
  uint8_t clk;
} LCD_PINS = {D6, D7, D8};

const uint8_t LED_PIN = D0;
const uint8_t DHT_PIN = D1;
const uint8_t MOISTURE_ENABLE_PIN = D5;
const uint8_t WATER_LEVEL_DATA_PIN = A0;
const uint8_t COLUMNS = 16;
const uint8_t ROWS = 2;

// Init components
LiquidCrystal595 lcd(LCD_PINS.data, LCD_PINS.latch, LCD_PINS.clk);
DHT_Unified dht(DHT_PIN, DHT_TYPE);

// Global variables needed in hole project
volatile uint8_t displayMode = DEFAULT_MODE;
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
struct {
  long     waterLevelSensor;
  long     waterLevelOnTimer;
  long     tempHumdSensor;
  long     offsetSeconds;
  long     wifiTaskSeconds;
  long     tempTaskSeconds;
  long     humdTaskSeconds;
  long     wlvlTaskSeconds;
  uint32_t statisticsTaskSeconds;
  int      displayTaskSeconds;
  long     otaCheckMs;
  uint32_t clearStatsSeconds;
  uint16_t clockSyncInterval;
} waitingTimes = {
  60000,    // 60 seconds
  50000,    // 50 seconds
  9000,     // 09 seconds
  1800000,  // 30 minutes
  660000,   // 11 minutes
  300000,   // 05 minutes
  320000,   // 05 minutes 20 seconds
  680000,   // 11 minutes 20 seconds
  60,       // 60 seconds
  15,       // 15 seconds
  60000,    // 60 seconds
  86400,    // 01 day
  180       // 03 minutes
};

struct {
  SimpleTimer dhtSensor;
  SimpleTimer wlvlOnTimer;
  SimpleTimer waterSensor;
  Ticker updateStatistics;
  Ticker displayTask;
  Ticker clearStatsTask;
  SimpleTimer offsetControl;
  SimpleTimer wifiTask;
  SimpleTimer tempTask;
  SimpleTimer humdTask;
  SimpleTimer wlvlTask;
#if OTA_ENABLED
  SimpleTimer ota;
#endif
} timers;

struct {
  float latestTemperature;
  float latestHumidity;
  bool hasTempChanged;
  bool hasHumdChanged;
} dhtValues = {0.0, 0.0, true, true};

struct {
  uint8_t waterValue;
  bool hasWaterValueChanged;
  uint16_t lowerLimit;
  uint16_t upperLimit;
} waterLevelValues = {0, true, 435, 900};

struct {
  String formattedTime;
  String formattedDate;
  time_t prevTime;
  bool hasTimeChanged;
  bool hasDateChanged;
  bool mustShowSeparator;
} dataTime = {"00:00 00", "01-01-1970", 0, true, true, true};

struct {
  String ip;
  String lat;
  String lng;
  uint16_t offset;
  String connectedSSID;
} geolocationInformation = {"", "", "", 0, ""};

struct {
  float temperatureFix;
  bool  ledEnabled;
  int   warningLedPercentage;
  int   tempFixAddress;
  int   ledEnabledAddress;
  int   warningLedPercentageAddress;
  int   displayTaskSecsAddress;
  int   waterLevelTimeAddress;
  int   tempHumdTimeAddress;
} options = {0.0f, true, 30, sizeof(short),
             (sizeof(short) + sizeof(float)),
             (sizeof(short) + sizeof(float) + sizeof(bool)),
             (sizeof(short) + sizeof(float) + sizeof(bool) + sizeof(int)),
             (sizeof(short) + sizeof(float) + sizeof(bool) + (2 * sizeof(int))),
             (sizeof(short) + sizeof(float) + sizeof(bool) + sizeof(long) + (2 * sizeof(int)))
            };


// MQTT information
ThingSpeakPublisher mqttWiFi(CHANNEL_ID, THINGSPEAK_API, 1, client);
ThingSpeakPublisher mqttTemp(CHANNEL_ID, THINGSPEAK_API, 2, client);
ThingSpeakPublisher mqttHumd(CHANNEL_ID, THINGSPEAK_API, 3, client);
ThingSpeakPublisher mqttWlvl(CHANNEL_ID, THINGSPEAK_API, 4, client);

SensorStats tempStats;
SensorStats humdStats;

#if DEVMODE
struct {
  volatile uint32_t latestTickerExecution;
  volatile uint32_t latestWaterExecution;
  volatile uint32_t latestDHTExecution;
} executionTimes = {0, 0, 0};
#endif

// ArduinoJson constants
const size_t TIMEZONE_DB_BUFFER_SIZE = JSON_OBJECT_SIZE(13) + 396;
const size_t EXTREME_IP_BUFFER_SIZE = JSON_OBJECT_SIZE(15) + 530;

// Other global variables
bool setupExecuted = false;
bool displayModeChanged = false;
String password;
volatile bool hasDisplayModeBeenPrinted[N_DISPLAY_MODES] = {false};
String latestSSID = "";
long latestRSSI = 0;
bool portalExecuted = false;
unsigned long wifiExecutionTime = 0L;
int dhtId;
int waterSensorTimerId;
int waterLevelTimerId;
bool moistureSensorOn = false;

// Define the functions that will be
// available
void initEEPROM(void);
void initAutoConnect(String password);
void initDHT(void);
void createLCDCustomCharacters(void);
void substituteBonsaiChar(void);
void powerOnSensor(uint8_t ePin);
void powerOffSensor(uint8_t ePin);
void turnOnWaterLevelSensor(void);
void turnOffWaterLevelSensor(void);
void lcdPrintTime(void);
void lcdPrintDHT(void);
void lcdPrintWaterLevel(void);
void lcdPrintDate(void);
void lcdPrintAvgDHT(void);
void lcdPrintWiFiInformation(void);
void rootPage(void);
void handleGPIO(void);
void sendRedirect(String uri);
void lcdPrintWiFiIP(void);
String generateRandomString(void);
bool printLCDCaptivePortalInformation(IPAddress ip);
void changeDisplayMode(void);
void updateTime(void);
String formatDigits(int value);
time_t syncTimeFromNTP(void);
void updateDHTInfo(void);
void updateWaterLevelInfo(void);
uint8_t toWiFiQuality(int32_t rssi);
void setupLatitudeLongitude(void);
void getTimezoneOffset(void);
void setupClock(void);
void publishWiFiStrength(void);
void publishTemperature(void);
void publishHumidity(void);
void publishWaterLevel(void);
void statisticsUpdate(void);
void clearStats(void);
#if OTA_ENABLED
void lookForOTAUpdates(void);
#endif


void setup(void) {
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

  // Init components
  lcd.begin(COLUMNS, ROWS);
  lcd.clear();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(MOISTURE_ENABLE_PIN, OUTPUT);

  initDHT();
  createLCDCustomCharacters();

  lcd.home();
  lcd.print(F("   "));
  lcd.write(BONSAI.id);
  lcd.print(F("BonsaiAIO"));
  delay(2000);
  lcd.setCursor(0, 1);
  lcd.print(F("Initializing..."));

  initEEPROM();
  password = generateRandomString();
  initAutoConnect(password);
#if DEVMODE
  Serial.println(F("Starting web server and captive portal"));
  Serial.printf("AP SSID: BonsaiAIO\n\rAP password: %s\n\r", password.c_str());
#endif
  // Start the web server and cautive portal
  Server.on(F("/"), rootPage);
  Server.on(F("/io"), handleGPIO);
  Portal.config(config);
  Portal.onDetect(printLCDCaptivePortalInformation);
  digitalWrite(LED_PIN, LOW);
  if (Portal.begin()) {
#if DEVMODE
    Serial.println(F("Successfully connected to Internet!"));
    Serial.print(F("HTTP server:")); Serial.println(WiFi.localIP().toString());
#endif
#if SYSLOG
    syslog.log(LOG_WARNING, "WiFi connected! Starting remote logging");
#endif
    lcd.clear();
    lcd.home();
    lcd.print(F("WiFi connected!"));
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
    delay(5000);
  } else {
#if DEVMODE
    Serial.println(F("Error connecting to Internet"));
#endif
  }
  turnOnWaterLevelSensor();
  lcd.clear();
  lcd.print(F("Getting time"));
  lcd.setCursor(0, 1);
  lcd.print(F("from Internet..."));

  // Global variables definition
#if DEVMODE
  cpuTicksPerSecond     = 0;
  cpuEvents             = 0;
  waterLevelWaitingTime = 0;
#endif
  displayMode           = DEFAULT_MODE;

  // Setup clock data - correct timezone (offset)
  setupClock();
#if DEVMODE
  Serial.print(F("Latitude: "));
  Serial.println(geolocationInformation.lat);
  Serial.print(F("Longitude: "));
  Serial.println(geolocationInformation.lng);
  Serial.print(F("Timezone offset: "));
  Serial.println(geolocationInformation.offset);
  delay(2000);
#endif

  // Init the NTP client - if we are here there is Internet connection
  ntp.begin();
  setSyncProvider(syncTimeFromNTP);
  setSyncInterval(waitingTimes.clockSyncInterval);
  setTime(syncTimeFromNTP());

  // Inform that we are starting now
  lcd.clear();
  lcd.print(F("Starting..."));

  // Init ThingSpeak as we have network
  ThingSpeak.begin(client);

  // Setup timers
#if OTA_ENABLED
  timers.ota.setInterval(waitingTimes.otaCheckMs, lookForOTAUpdates);
#endif
  dhtId = timers.dhtSensor.setInterval(waitingTimes.tempHumdSensor, updateDHTInfo);
  waterSensorTimerId = timers.wlvlOnTimer.setInterval(waitingTimes.waterLevelOnTimer, turnOnWaterLevelSensor);
  waterLevelTimerId = timers.waterSensor.setInterval(waitingTimes.waterLevelSensor, updateWaterLevelInfo);
  timers.offsetControl.setInterval(waitingTimes.offsetSeconds, setupClock);
  timers.wifiTask.setInterval(waitingTimes.wifiTaskSeconds, publishWiFiStrength);
  timers.tempTask.setInterval(waitingTimes.tempTaskSeconds, publishTemperature);
  timers.humdTask.setInterval(waitingTimes.humdTaskSeconds, publishHumidity);
  timers.wlvlTask.setInterval(waitingTimes.wlvlTaskSeconds, publishWaterLevel);
  timers.updateStatistics.attach(waitingTimes.statisticsTaskSeconds, statisticsUpdate);
  timers.displayTask.attach(waitingTimes.displayTaskSeconds, changeDisplayMode);
  timers.clearStatsTask.attach(waitingTimes.clearStatsSeconds, clearStats);

  updateDHTInfo();
  updateWaterLevelInfo();
  updateTime();
  statisticsUpdate();
  substituteBonsaiChar();

#if DEVMODE
  setupFinishedTime = millis();
  Serial.print(F("Setup elapsed time: "));
  Serial.println(setupFinishedTime);
#endif
#if SYSLOG
  syslog.logf(LOG_WARNING, "Finished setup. Elapsed time: %d", millis());
  syslog.logf(LOG_WARNING, "Running version: %s", ESP.getSketchMD5().c_str());
#endif
}

void loop(void) {
  updateTime();
  timers.wlvlOnTimer.run();
  timers.waterSensor.run();
  timers.dhtSensor.run();
  Portal.handleClient();
  switch ((uint8_t) displayMode) {
    case DEFAULT_MODE:
      lcdPrintTime();
      lcdPrintWaterLevel();
      lcdPrintDHT();
      break;
    case AVG_TEMP_HUM_MODE:
      lcdPrintTime();
      lcdPrintWaterLevel();
      lcdPrintAvgDHT();
      break;
    case CLOCK_AND_DATE:
      lcdPrintTime();
      lcdPrintWaterLevel();
      lcdPrintDate();
      break;
    case WIFI_INFORMATION:
      lcdPrintWiFiInformation();
      break;
    case DISPLAY_CLEAR:
      lcd.clear();
      displayMode = IP_INFORMATION;
      break;
    case IP_INFORMATION:
      lcdPrintTime();
      lcdPrintWaterLevel();
      lcdPrintWiFiIP();
      break;
    default:
      displayMode = DEFAULT_MODE;
      break;
  }
  timers.offsetControl.run();
  timers.wifiTask.run();
  timers.tempTask.run();
  timers.humdTask.run();
  timers.wlvlTask.run();
#if OTA_ENABLED
  timers.ota.run();
#endif
}


void initEEPROM(void) {
  short eepromCrcI;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, eepromCrcI);
  if (eepromCrcI != EEPROM_CRCI) {
    EEPROM.put(0, EEPROM_CRCI);
    EEPROM.put(options.tempFixAddress, options.temperatureFix);
    EEPROM.put(options.ledEnabledAddress, options.ledEnabled);
    EEPROM.put(options.warningLedPercentageAddress, options.warningLedPercentage);
    EEPROM.put(options.displayTaskSecsAddress, waitingTimes.displayTaskSeconds);
    EEPROM.put(options.waterLevelTimeAddress, waitingTimes.waterLevelSensor);
    EEPROM.put(options.tempHumdTimeAddress, waitingTimes.tempHumdSensor);
    EEPROM.commit();
  } else {
    EEPROM.get(options.tempFixAddress, options.temperatureFix);
    EEPROM.get(options.ledEnabledAddress, options.ledEnabled);
    EEPROM.get(options.warningLedPercentageAddress, options.warningLedPercentage);
    EEPROM.get(options.displayTaskSecsAddress, waitingTimes.displayTaskSeconds);
    EEPROM.get(options.waterLevelTimeAddress, waitingTimes.waterLevelSensor);
    EEPROM.get(options.tempHumdTimeAddress, waitingTimes.tempHumdSensor);
    waitingTimes.waterLevelOnTimer = (waitingTimes.waterLevelSensor - 1000L);
  }
}

void initAutoConnect(String password) {
  config.apid = "BonsaiAIO";
  config.psk = password;
  config.retainPortal = false;
  config.hostName = "BonsaiAIO";
  config.title = "BonsaiAIO";
  config.ticker = true;
  config.tickerPort = LED_PIN;
  config.tickerOn = HIGH;
  config.boundaryOffset = CREDENTIAL_OFFSET;
}

void initDHT(void) {
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

void createLCDCustomCharacters(void) {
  lcd.createChar(WATER_DROP.id, WATER_DROP.icon);
  lcd.createChar(TERMOMETER.id, TERMOMETER.icon);
  lcd.createChar(WIFI.id, WIFI.icon);
  lcd.createChar(KEY.id, KEY.icon);
  lcd.createChar(WATER_LEVEL_EMPTY.id, WATER_LEVEL_EMPTY.icon);
  lcd.createChar(WATER_LEVEL_25.id, WATER_LEVEL_25.icon);
  lcd.createChar(WATER_LEVEL_75.id, WATER_LEVEL_75.icon);
  lcd.createChar(BONSAI.id, BONSAI.icon);
}

void substituteBonsaiChar(void) {
  lcd.createChar(WATER_LEVEL_50.id, WATER_LEVEL_50.icon);
}

void powerOnSensor(uint8_t ePin) {
#if DEVMODE
  Serial.printf("Turning on sensor at pin: %i\n", ePin);
#endif
  digitalWrite(ePin, HIGH);

}

void powerOffSensor(uint8_t ePin) {
#if DEVMODE
  Serial.printf("Turning off sensor at pin: %i\n", ePin);
#endif
  digitalWrite(ePin, LOW);
}

void turnOnWaterLevelSensor(void) {
  if (!moistureSensorOn) {
    powerOnSensor(MOISTURE_ENABLE_PIN);
    moistureSensorOn = true;
  }
}

void turnOffWaterLevelSensor(void) {
  if (moistureSensorOn) {
    powerOffSensor(MOISTURE_ENABLE_PIN);
    moistureSensorOn = false;
  }
}

void lcdPrintTime(void) {
  if (dataTime.hasTimeChanged || displayModeChanged) {
#if DEVMODE
    Serial.print(F("Time: "));
    Serial.println(dataTime.formattedTime);
    delay(10);
#endif
    lcd.home();
    lcd.print(F("        "));
    lcd.home();
    lcd.print(F(" "));
    lcd.print(dataTime.formattedTime);
    if (dataTime.hasTimeChanged)
      dataTime.hasTimeChanged = false;
  }
}

void lcdPrintDHT(void) {
  if (dhtValues.hasTempChanged || displayModeChanged || dhtValues.hasHumdChanged) {
#if DEVMODE
    Serial.print(F("Temp: ")); Serial.println(String(dhtValues.latestTemperature) + " ºC");
    Serial.print(F("Humd: ")); Serial.println(String(dhtValues.latestHumidity) + " %");
    delay(10);
#endif
    lcd.setCursor(0, 1);
    lcd.print(CLEAR_ROW);
    lcd.setCursor(0, 1);
    lcd.print(F(" "));
    lcd.write(TERMOMETER.id);
    lcd.print(dhtValues.latestTemperature, 1);
    lcd.print(F(" "));
    lcd.print((char)223);
    lcd.print(F("C"));
    if (dhtValues.hasTempChanged)
      dhtValues.hasTempChanged = false;
    lcd.setCursor(9, 1);
    lcd.print(F("  "));
    lcd.write(WATER_DROP.id);
    lcd.print(dhtValues.latestHumidity, 0);
    lcd.print(F("%"));
    if (dhtValues.hasHumdChanged)
      dhtValues.hasHumdChanged = false;
  }
  if (displayModeChanged)
    displayModeChanged = false;
}

void lcdPrintWaterLevel(void) {
  if (waterLevelValues.hasWaterValueChanged || displayModeChanged) {
#if DEVMODE
    Serial.print(F("Water level: ")); Serial.println(String(waterLevelValues.waterValue));
    waterLevelValues.hasWaterValueChanged = false;
    delay(10);
#endif
    lcd.setCursor(8, 0);
    lcd.print(F("        "));
    if (waterLevelValues.waterValue <= options.warningLedPercentage) {
      if (options.ledEnabled)
        digitalWrite(LED_PIN, HIGH);
      lcd.setCursor(8, 0);
      lcd.print(F("!"));
      lcd.write(WATER_LEVEL_EMPTY.id);
    } else {
      digitalWrite(LED_PIN, LOW);
      lcd.setCursor(9, 0);
      if ((waterLevelValues.waterValue >= 40) && (waterLevelValues.waterValue <= 60)) {
        lcd.write(WATER_LEVEL_50.id);
      } else if (waterLevelValues.waterValue < 40) {
        lcd.write(WATER_LEVEL_25.id);
      } else {
        lcd.write(WATER_LEVEL_75.id);
      }
    }
    lcd.print(F(" "));
    lcd.print(waterLevelValues.waterValue);
    lcd.print(F("%"));
    if (waterLevelValues.hasWaterValueChanged)
      waterLevelValues.hasWaterValueChanged = false;
  }
}

void lcdPrintDate(void) {
  if (dataTime.hasDateChanged || displayModeChanged) {
#if DEVMODE
    Serial.print(F("Date: "));
    Serial.println(dataTime.formattedDate);
    delay(10);
#endif
    lcd.setCursor(0, 1);
    lcd.print(CLEAR_ROW);
    lcd.setCursor(3, 1);
    lcd.print(dataTime.formattedDate);
    if (dataTime.hasDateChanged)
      dataTime.hasDateChanged = false;
    if (displayModeChanged)
      displayModeChanged = false;
  }
}

void lcdPrintAvgDHT(void) {
  if (displayModeChanged) {
    lcd.setCursor(0, 1);
    lcd.print(F("/"));
    lcd.write(TERMOMETER.id);
    lcd.print(tempStats.getMean(), 2);
    lcd.print(F(" "));
    lcd.print((char)223);
    lcd.print(F("C"));
    lcd.setCursor(10, 1);
    lcd.print(F(" /"));
    lcd.write(WATER_DROP.id);
    lcd.print(humdStats.getMean(), 0);
    lcd.print(F("%"));
    displayModeChanged = false;
  }
}

void lcdPrintWiFiInformation(void) {
  if (WiFi.status() == WL_CONNECTED) {
    long rssi = WiFi.RSSI();
    String ssid = WiFi.SSID();
    uint8_t wifiQuality = toWiFiQuality(rssi);
    if (displayModeChanged || (rssi != latestRSSI) || (ssid != latestSSID)) {
      lcd.clear();
      lcd.print(ssid.c_str());
      lcd.setCursor(0, 1);
      lcd.write(WIFI.id);
      lcd.print(F(" "));
      lcd.print(rssi);
      lcd.print(F(" dBm - "));
      lcd.print(wifiQuality);
      lcd.print(F("%"));
      latestRSSI = rssi;
      latestSSID = ssid;
      if (displayModeChanged)
        displayModeChanged = false;
    }
  } else {
    if (!portalExecuted) {
      portalExecuted = true;
      if (Portal.begin()) {
        portalExecuted = false;
      }
    }
  }
}

void lcdPrintWiFiIP(void) {
  if (displayModeChanged) {
    lcd.setCursor(0, 1);
    lcd.print(CLEAR_ROW);
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString().c_str());
    displayModeChanged = false;
  }
}

void rootPage(void) {
  String page = html;
  String pageBody = body;
  String bodyOptions = setupOptions;

  bodyOptions.replace("{MENU}", cogMenu);
  bodyOptions.replace("{LED_STATUS}", (options.ledEnabled ? String(F("Tomato\">Enabled")) : String(F("SlateBlue\">Disabled"))));
  bodyOptions.replace("{WLVL_TIME}", String((waitingTimes.waterLevelSensor / 1000)));
  bodyOptions.replace("{TEMP_HUMD_TIME}", String((waitingTimes.tempHumdSensor / 1000)));
  bodyOptions.replace("{LED_PERCENTAGE}", String(options.warningLedPercentage));
  bodyOptions.replace("{DISPLAY_SECONDS}", String(waitingTimes.displayTaskSeconds));
  bodyOptions.replace("{TEMP_FIX}", String(options.temperatureFix));

  pageBody.replace("{BODY}", bodyOptions);

  page.replace("{HEAD}", head);
  page.replace("{BODY}", pageBody);

  Server.send(200, "text/html", page);
}

void handleGPIO(void) {
  WebServerClass& server = Portal.host();
  bool hasAnyValueChanged = false;
  if (server.arg("v") == "disable") {
    if (options.ledEnabled) {
      options.ledEnabled = false;
      digitalWrite(LED_PIN, LOW);
      EEPROM.put(options.ledEnabledAddress, options.ledEnabled);
      hasAnyValueChanged = true;
    }
  }
  else if (server.arg("v") == "enable") {
    if (!options.ledEnabled) {
      options.ledEnabled = true;
      EEPROM.put(options.ledEnabledAddress, options.ledEnabled);
      hasAnyValueChanged = true;
    }
  }
  if (server.arg("wlvl") != "") {
    long waterLevelTime = (atol(server.arg("wlvl").c_str()) * 1000L);
    if ((waterLevelTime != waitingTimes.waterLevelSensor) && (waterLevelTime >= 2000L)) {
      waitingTimes.waterLevelSensor = waterLevelTime;
      EEPROM.put(options.waterLevelTimeAddress, waitingTimes.waterLevelSensor);
      hasAnyValueChanged = true;
      waitingTimes.waterLevelOnTimer = (waitingTimes.waterLevelSensor - 1000L);
      timers.waterSensor.deleteTimer(waterLevelTimerId);
      timers.wlvlOnTimer.deleteTimer(waterSensorTimerId);
      waterSensorTimerId = timers.wlvlOnTimer.setInterval(waitingTimes.waterLevelOnTimer, turnOnWaterLevelSensor);
      waterLevelTimerId = timers.waterSensor.setInterval(waitingTimes.waterLevelSensor, updateWaterLevelInfo);
    }
  }
  if (server.arg("dht") != "") {
    long tempHumdTime = (atol(server.arg("dht").c_str()) * 1000L);
    if ((tempHumdTime != waitingTimes.tempHumdSensor) && (tempHumdTime >= 3000L)) {
      waitingTimes.tempHumdSensor = tempHumdTime;
      EEPROM.put(options.tempHumdTimeAddress, waitingTimes.tempHumdSensor);
      hasAnyValueChanged = true;
      timers.dhtSensor.deleteTimer(dhtId);
      dhtId = timers.dhtSensor.setInterval(waitingTimes.tempHumdSensor, updateDHTInfo);
    }
  }
  if (server.arg("temp") != "") {
    float fixedTemperature = atof(server.arg("temp").c_str());
    if (fixedTemperature != options.temperatureFix) {
      options.temperatureFix = fixedTemperature;
      EEPROM.put(options.tempFixAddress, options.temperatureFix);
      hasAnyValueChanged = true;
    }
  }
  if (server.arg("perc") != "") {
    int percentage = atoi(server.arg("perc").c_str());
    if ((percentage != options.warningLedPercentage) && (percentage >= 0)) {
      options.warningLedPercentage = percentage;
      EEPROM.put(options.warningLedPercentageAddress, (int) options.warningLedPercentage);
      hasAnyValueChanged = true;
    }
  }
  if (server.arg("secs") != "") {
    int seconds = atoi(server.arg("secs").c_str());
    if ((seconds != waitingTimes.displayTaskSeconds) && (seconds >= 5)) {
      waitingTimes.displayTaskSeconds = seconds;
      EEPROM.put(options.displayTaskSecsAddress, waitingTimes.displayTaskSeconds);
      hasAnyValueChanged = true;
      timers.displayTask.detach();
      timers.displayTask.attach(waitingTimes.displayTaskSeconds, changeDisplayMode);
    }
  }
  if (hasAnyValueChanged)
    EEPROM.commit();
  sendRedirect("/");
}

void sendRedirect(String uri) {
  Server.sendHeader("Location", uri, true);
  Server.send(302, "text/plain", "");
  Server.client().stop();
}

String generateRandomString(void) {
  const char* letters = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int position = -1;
  String randomString = "";
  for (int i = 0; i < 8; i++) {
    position = random(62);
    randomString += letters[position];
  }
  return randomString;
}

bool printLCDCaptivePortalInformation(IPAddress ip) {
  if ((!setupExecuted) || (displayMode == WIFI_INFORMATION)) {
    lcd.clear();
    lcd.home();
    lcd.write(WIFI.id);
    lcd.setCursor(2, 0);
    lcd.print("BonsaiAIO");
    lcd.setCursor(0, 1);
    lcd.write(KEY.id);
    lcd.setCursor(2, 1);
    lcd.print(password);
  }
  return true;
}

void changeDisplayMode(void) {
  displayMode = (displayMode + 1) % N_DISPLAY_MODES;
  displayModeChanged = true;
#if DEVMODE
  Serial.printf("Changing display mode: %d\n", displayMode);
#endif
}

void updateTime(void) {
  if (timeStatus() != timeNotSet) {
    if (now() != dataTime.prevTime) {
      dataTime.prevTime = now();

      // Format the date
      String currentDay = formatDigits(day());
      String currentMonth = formatDigits(month());
      String currentYear = String(year());
      String formattedDate = currentDay + "-" + currentMonth + "-" + currentYear;
      if (formattedDate != dataTime.formattedDate) {
        dataTime.formattedDate = formattedDate;
        dataTime.hasDateChanged = true;
      } else {
        dataTime.hasDateChanged = false;
      }

      // Format the time
      String currentHour = formatDigits(hour());
      String currentMinutes = formatDigits(minute());
      String currentSeconds = formatDigits(second());
      String formattedTime = currentHour;
      formattedTime += (dataTime.mustShowSeparator) ? ":" : " ";
      formattedTime += currentMinutes; // + " " + currentSeconds;
      // Here, times are different (prevDisplay != now())
      dataTime.formattedTime = formattedTime;
      dataTime.mustShowSeparator = !dataTime.mustShowSeparator;
      dataTime.hasTimeChanged = true;
    }
  }
}

String formatDigits(int value) {
  return ((value < 10) ? ("0" + String(value)) : String(value));
}

time_t syncTimeFromNTP(void) {
  if (WiFi.status() == WL_CONNECTED) {
    ntp.update();
    return ((time_t) ntp.getEpochTime());
  } else {
    return now();
  }
}

void updateDHTInfo(void) {
#if DEVMODE
  uint32_t currentTime = millis();
  uint32_t diff = currentTime - executionTimes.latestDHTExecution;
  Serial.print(F("Latest DHT execution about ")); Serial.println(String(diff) + " ms. ago");
  executionTimes.latestDHTExecution = currentTime;
#endif
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  uint8_t tempAttemps = 0;
#if DEVMODE
  if (isnan(event.temperature)) {
    Serial.println(F("Error reading temperature!"));
    Serial.println(F("Retrying..."));
  }
#endif
  while (isnan(event.temperature) && (++tempAttemps <= 100)) {
    dht.temperature().getEvent(&event);
    delay(10);
  }
#if DEVMODE
  Serial.printf("Temperature attemps: %i\n", tempAttemps);
#endif
  if (!isnan(event.temperature)) {
#if DEVMODE
    Serial.print(F("Temperature: "));
    Serial.print(event.temperature + options.temperatureFix);
    Serial.println(F("°C"));
    Serial.printf("No fixed temp: %.2f\n", event.temperature);
#endif
    if (event.temperature != dhtValues.latestTemperature) {
      dhtValues.latestTemperature = (event.temperature + options.temperatureFix);
      dhtValues.hasTempChanged = true;
    }
  } 
#if SYSLOG
  else {
    syslog.logf(LOG_ERR, "Error reading temperature after %d attemps", tempAttemps);
  }
#endif
  // Get humidity event and print its value.
  dht.humidity().getEvent(&event);
  uint8_t humdAttemps = 0;
#if DEVMODE
  if (isnan(event.relative_humidity)) {
    Serial.println(F("Error reading humidity!"));
    Serial.println(F("Retrying..."));
  }
#endif
  while (isnan(event.relative_humidity) && (++humdAttemps <= 100)) {
    dht.humidity().getEvent(&event);
    delay(10);
  }
#if DEVMODE
  Serial.printf("Humidity attemps: %i\n", humdAttemps);
#endif
  if (!isnan(event.relative_humidity)) {
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
#if SYSLOG
  else {
    syslog.logf(LOG_ERR, "Error reading humidity after %d attemps", humdAttemps);
  }
#endif
}

void updateWaterLevelInfo(void) {
#if DEVMODE
  uint32_t currentTime = millis();
  uint32_t diff = currentTime - executionTimes.latestWaterExecution;
  Serial.print(F("Latest water execution about ")); Serial.print(String(diff) + " ms. ago\n");
  executionTimes.latestWaterExecution = currentTime;
#endif
  uint16_t currentWaterValue = analogRead(WATER_LEVEL_DATA_PIN);
  uint8_t percentageWaterValue = map(currentWaterValue, waterLevelValues.upperLimit,
                                     waterLevelValues.lowerLimit, 0, 100);
  turnOffWaterLevelSensor();
#if DEVMODE
  Serial.printf("Water value (analog): %i | Percentage: %i%\n", currentWaterValue, 
                                                                percentageWaterValue);
#endif
  if (percentageWaterValue > 100)
    percentageWaterValue = 100;
  if (percentageWaterValue != waterLevelValues.waterValue) {
    waterLevelValues.waterValue = percentageWaterValue;
    waterLevelValues.hasWaterValueChanged = true;
  }
}

uint8_t toWiFiQuality(int32_t rssi) {
  unsigned int qu;
  if (rssi == 31) // WiFi signal is weak and RSSI value is unreliable.
    qu = 0;
  else if (rssi <= -100)
    qu = 0;
  else if (rssi >= -50)
    qu = 100;
  else
    qu = 2 * (rssi + 100);
  return qu;
}

void setupLatitudeLongitude(void) {
  if (WiFi.status() == WL_CONNECTED) {
    if (geolocationInformation.connectedSSID != WiFi.SSID()) {
      HTTPClient http;
#if DEVMODE
      Serial.print(F("EXTREME URL: "));
      Serial.println(EXTREME_IP_URL);
#endif
      http.begin(client, EXTREME_IP_URL);
      int httpCode = http.GET();
      if (httpCode > 0) {
//        DynamicJsonDocument jsonBuffer(EXTREME_IP_BUFFER_SIZE);
        StaticJsonDocument<EXTREME_IP_BUFFER_SIZE> jsonBuffer;
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
#if SYSLOG
          syslog.logf(LOG_ERR, "IP-API deserialization failed with code %s", error.c_str());
#endif
        }
        jsonBuffer.clear();
      }
      else {
#if DEVMODE
        Serial.printf("[HTTPS] GET... failed, error: %s\n\r", http.errorToString(httpCode).c_str());
#endif
#if SYSLOG
        syslog.logf(LOG_ERR, "[HTTP] GET (IP Lookup)... failed, error: %s\n\r", http.errorToString(httpCode).c_str());
#endif
      }
      http.end();
      if (httpCode > 0)
        geolocationInformation.connectedSSID = WiFi.SSID();
    }
  }
}

void getTimezoneOffset(void) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    char *formattedUrl = new char[TIMEZONE_DB_MAX];
    sprintf(formattedUrl, TIMEZONE_DB_URL, TIMEZONE_DB, geolocationInformation.lat.c_str(), geolocationInformation.lng.c_str());
#if DEVMODE
    Serial.print(F("TIMEZONE DB URL: "));
    Serial.println(formattedUrl);
#endif
    http.begin(client, formattedUrl);
    int httpCode = http.GET();
    if (httpCode > 0) {
//      DynamicJsonDocument jsonBuffer(TIMEZONE_DB_BUFFER_SIZE);
      StaticJsonDocument<TIMEZONE_DB_BUFFER_SIZE> jsonBuffer;
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
#if SYSLOG
        syslog.logf(LOG_ERR, "IP-API deserialization failed with code %s", error.c_str());
#endif
      }
      jsonBuffer.clear();
    } else {
#if DEVMODE
      Serial.printf("[HTTPS] GET... failed, error: %s\n\r", http.errorToString(httpCode).c_str());
#endif
#if SYSLOG
      syslog.logf(LOG_ERR, "[HTTP] GET (TimeZone DB)... failed, error: %s\n\r", http.errorToString(httpCode).c_str());
#endif
    }
    delete[] formattedUrl;
    http.end();
  }
}

void setupClock(void) {
  setupLatitudeLongitude();
  getTimezoneOffset();
  ntp.setTimeOffset(geolocationInformation.offset);
}

void publishWiFiStrength(void) {
  if (WiFi.status() == WL_CONNECTED) {
    long rssi = WiFi.RSSI();
    int httpCode = mqttWiFi.publish(rssi);
#if DEVMODE
    if (httpCode == 200) {
      Serial.println("Correctly published WiFi Strength");
    } else {
      Serial.printf("Problem publishing WiFi Strength - error code: %i\n", httpCode);
    }
#endif
  } else {
#if DEVMODE
    Serial.println(F("No Internet connection"));
#endif
  }
}

void publishTemperature(void) {
  if (WiFi.status() == WL_CONNECTED) {
    int httpCode = mqttTemp.publish(dhtValues.latestTemperature);
#if DEVMODE
    if (httpCode == 200) {
      Serial.println("Correctly published Temperature");
    } else {
      Serial.printf("Problem publishing Temperature - error code: %i\n", httpCode);
    }
#endif
  } else {
#if DEVMODE
    Serial.println(F("No Internet connection"));
#endif
  }
}

void publishHumidity(void) {
  if (WiFi.status() == WL_CONNECTED) {
    int httpCode = mqttHumd.publish(dhtValues.latestHumidity);
#if DEVMODE
    if (httpCode == 200) {
      Serial.println("Correctly published Humidity\n");
    } else {
      Serial.printf("Problem publishing Humidity - error code: %i", httpCode);
    }
#endif
  } else {
#if DEVMODE
    Serial.println(F("No Internet connection"));
#endif
  }
}

void publishWaterLevel(void) {
  if (WiFi.status() == WL_CONNECTED) {
    int httpCode = mqttWlvl.publish(waterLevelValues.waterValue);
#if DEVMODE
    if (httpCode == 200) {
      Serial.println("Correctly published Water Value");
    } else {
      Serial.printf("Problem publishing Water Value - error code: %i\n", httpCode);
    }
#endif
  } else {
#if DEVMODE
    Serial.println(F("No Internet connection"));
#endif
  }
}

void statisticsUpdate(void) {
#if DEVMODE
  Serial.println("Updating statistics");
  Serial.printf("Latest temperature: %f | Latest humidity: %d\n",
                dhtValues.latestTemperature, dhtValues.latestHumidity);
#endif
  tempStats.add(dhtValues.latestTemperature);
  humdStats.add(dhtValues.latestHumidity);
}

void clearStats(void) {
  tempStats.reset();
  humdStats.reset();
}

#if OTA_ENABLED
void lookForOTAUpdates(void) {
  if (WiFi.status() == WL_CONNECTED) {
#if DEVMODE
    Serial.println(F("Looking for OTAs..."));
#endif
    ESPhttpUpdate.setLedPin(LED_PIN, HIGH);
    t_httpUpdate_return returnValue = ESPhttpUpdate.update(client, OTA_URL, RUNNING_VERSION);

    switch (returnValue) {
      case HTTP_UPDATE_FAILED:
#if DEVMODE
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
#endif
#if SYSLOG
        syslog.logf(LOG_ERR, "HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
#endif
        break;
      case HTTP_UPDATE_NO_UPDATES:
#if DEVMODE
        Serial.println("[update] Update no Update.");
#endif
        break;
      case HTTP_UPDATE_OK:
        lcd.clear();
        lcd.write(WIFI.id);
        lcd.print(F(" "));
        lcd.print(F("Updating..."));
#if DEVMODE
        Serial.println("[update] Update ok."); // may not called we reboot the ESP
#endif
#if SYSLOG
        syslog.log(LOG_WARNING, F("[update] Update OK"));
#endif
        break;
    }
  }
}
#endif
