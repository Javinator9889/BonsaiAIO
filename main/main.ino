/**
   Bonsai AIO - Copyright (C) 2019 - present by Javinator9889

   Control water level, temperature,
   time and bonsai light with an Arduino board.
*/

// Web control & WiFi libraries
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>

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
#include <Thread.h>
#include <ThingSpeak.h>

// ESP8266 pinout
//#include "PinConstants.h"

// Statistics library
//#include "Statistics.h"

// Custom display modes
#include "DisplayModes.h"

// API Keys
#include "ApiKeys.h"

// Water value calculator
//#include "WaterValues.h"

// LCD Icons
#include "LCDIcons.h"

// Define whether the DEVMODE is active
// for saving sketch size - set to 0 for
// disabling
#define DEVMODE     0
// VVV - extra verbosity
#define VVV         0
// Enable or disable OTAs - disabled due to space
// restrictions
#define OTA_ENABLED 0

#if DEVMODE
#define PRINT_DEBUG_MESSAGES
#endif

#if OTA_ENABLED
#include <ESP8266httpUpdate.h>
#endif

// Maximum stats we will keep in memory
#define MAX_STATS   512

// Strings & URLs that will be used
#define TIMEZONE_DB_URL   "http://api.timezonedb.com/v2.1/get-time-zone?key=%s&format=json&by=position&lat=%s&lng=%s"
#define EXTREME_IP_URL    "http://extreme-ip-lookup.com/json/"

// OTA static constant values
#if OTA_ENABLED
#define OTA_URL           "http://ota.javinator9889.com"
#define OTA_PORT          80
#define OTA_PATH          "/"
#define RUNNING_VERSION   "bonsaiaio"
#endif

#define TIMEZONE_DB_MAX 114
#define EXTREME_IP_MAX  34

// Other "define" constants
#define NTP_SERVER  "pool.ntp.org"
#define DHT_TYPE    DHT11
#define DHT_PIN     D1
#define CLEAR_ROW   "                "
#define UPPER_LIMIT 250
#define LOWER_LIMIT 120

// Web control objects
ESP8266WebServer  Server;
AutoConnect       Portal(Server);
AutoConnectConfig config;

// Time components
WiFiUDP ntpUDP;
NTPClient ntp(ntpUDP, NTP_SERVER);

// Threads
struct {
  Thread clockThread;
  Thread offsetTask;
  Thread wifiTask;
  Thread tempTask;
  Thread humdTask;
  Thread wlvlTask;
  Thread displayTask;
} tasks = {Thread(), Thread(), Thread(), Thread(), Thread(), Thread(), Thread()};

// Components pins
const struct {
  uint8_t data;
  uint8_t latch;
  uint8_t clk;
} LCD_PINS = {D6, D7, D8};

const uint8_t LED_PIN = D0;
//const uint8_t CONTRAST_PIN = D5;
//const uint8_t CONTRAST_VAL = 100;
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
volatile struct {
  uint16_t waterLevelSensor;
  uint16_t tempHumdSensor;
  uint16_t clockSeconds;
  uint32_t offsetSeconds;
  uint32_t wifiTaskSeconds;
  uint32_t tempTaskSeconds;
  uint32_t humdTaskSeconds;
  uint32_t wlvlTaskSeconds;
  uint32_t statisticsTaskSeconds;
  uint16_t displayTaskSeconds;
} waitingTimes = {
  18,       // 18 seconds
  9000,     // 09 seconds
  1,        // 01 second
  1800 ,    // 30 minutes
  600,      // 10 minutes
  60,      // 05 minutes
  60,      // 05 minutes
  1800,     // 30 minutes
  170,      // 02 minutes 50 secs
  10        // 10 seconds
};

struct {
  SimpleTimer dhtSensor;
  Ticker waterSensor;
  Ticker clockControl;
  Ticker offsetControl;
  Ticker wifiTask;
  Ticker tempTask;
  Ticker humdTask;
  Ticker wlvlTask;
  Ticker updateStatistics;
  Ticker displayTask;
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

// MQTT information
struct {
  struct {
    unsigned int wifiStrength;
    unsigned int temperature;
    unsigned int humidity;
    unsigned int waterLevel;
  } fields;
  unsigned long channelId;
  const char *apiKey;
} mqtt = {{1, 2, 3, 4}, CHANNEL_ID, THINGSPEAK_API};

// Custom types definition
typedef struct {
  float value;
  bool initialized;
} tempMeasure;

typedef struct {
  uint16_t value;
  bool initialized;
} humdMeasure;

typedef struct {
  int16_t upperLimit;
  int16_t lowerLimit;
} percentagesLimit;

struct {
  tempMeasure tempStats[MAX_STATS];
  humdMeasure humdStats[MAX_STATS];
  uint16_t tempCurrentElement;
  uint16_t humdCurrentElement;
  int16_t tempDefault;
  int16_t humdDefault;
} statistics = {{0, 0}, {0, 0}, 0, 0, -999, -9};

struct {
  percentagesLimit percentageLimit[11];
} waterLevelPercentages = {{0, 0}};

#if DEVMODE
struct {
  volatile uint32_t latestTickerExecution;
  volatile uint32_t latestWaterExecution;
  volatile uint32_t latestDHTExecution;
} executionTimes = {0, 0, 0};
#endif

// ArduinoJson constants
const size_t TIMEZONE_DB_BUFFER_SIZE = JSON_OBJECT_SIZE(13) + 226;
const size_t EXTREME_IP_BUFFER_SIZE = JSON_OBJECT_SIZE(15) + 286;

// Other global variables
bool setupExecuted = false;
bool displayModeChanged = false;
String password;
volatile bool hasDisplayModeBeenPrinted[N_DISPLAY_MODES] = {false};
String latestSSID = "";
long latestRSSI = 0;
WiFiClient client;

// Define the functions that will be
// available
void initAutoConnect(String password);
void initDHT(void);
void initWaterValuesPercentages(void);
void createLCDCustomCharacters(void);
void lcdPrintTime(void);
void lcdPrintDHT(void);
void lcdPrintWaterLevel(void);
void lcdPrintDate(void);
void lcdPrintAvgDHT(void);
void lcdPrintWiFiInformation(void);
void rootPage(void);
String generateRandomString(void);
bool printLCDCaptivePortalInformation(IPAddress ip);
void changeDisplayMode(void);
void launchClockThread(void);
void updateTime(void);
void updateDHTInfo(void);
void updateWaterLevelInfo(void);
bool areTimesDifferent(String time1, String time2);
void setupLatituteLongitude(void);
void getTimezoneOffset(void);
void setupClock(void);
void launchOffsetTask(void);
void launchWiFiMQTTTask(void);
void launchTempMQTTTask(void);
void launchHumdMQTTTask(void);
void launchWLvlMQTTTask(void);
void launchDisplayTask(void);
void publishWiFiStrength(void);
void publishTemperature(void);
void publishHumidity(void);
void publishWaterLevel(void);
void statisticsUpdate(void);
float getTempMean(void);
float getHumdMean(void);
uint8_t normalizeValue(uint16_t analogValue);
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
  /*for (int32_t i = (MAX_STATS) - 1; i >= 0; --i) {
    Serial.printf("Evaluating element #%i with values: %.1f(%d) | %d(%d)\n", 
                  i, statistics.tempStats[i].value, statistics.tempStats[i].initialized,
                  statistics.humdStats[i].value, statistics.humdStats[i].initialized);
    delay(100);
  }*/
#endif
  // Setup custom value inside stats arrays
  for (int32_t i = (MAX_STATS) - 1; i >= 0; --i) {
    statistics.tempStats[i].initialized = false;
    statistics.humdStats[i].initialized = false;
  }/*
#if DEVMODE
  for (int32_t i = (MAX_STATS) - 1; i >= 0; --i) {
    Serial.printf("Evaluating element #%i with values: %.1f | %d\n", i, statistics.tempStats[i], statistics.humdStats[i]);
    delay(100);
  }
#endif*/

  // Init components
  /*pinMode(CONTRAST_PIN, OUTPUT);
  analogWrite(CONTRAST_PIN, CONTRAST_VAL);*/
  lcd.begin(COLUMNS, ROWS);
  lcd.clear();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  initDHT();
  initWaterValuesPercentages();
  createLCDCustomCharacters();

  lcd.home();
  lcd.print(F("   "));
  lcd.write(BONSAI.id);
  lcd.print("BonsaiAIO");
  delay(2000);
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  password = generateRandomString();
  initAutoConnect(password);
#if DEVMODE
  Serial.println(F("Starting web server and cautive portal"));
  Serial.printf("AP SSID: BonsaiAIO\n\rAP password: %s\n\r", password.c_str());
#endif
  // Start the web server and cautive portal
  Server.on("/", rootPage);
  Portal.config(config);
  Portal.onDetect(printLCDCaptivePortalInformation);
  if (Portal.begin()) {
#if DEVMODE
    Serial.println(F("Successfully connected to Internet!"));
    Serial.print(F("HTTP server:")); Serial.println(WiFi.localIP().toString());
#endif
    lcd.clear();
    lcd.home();
    lcd.print("WiFi connected!");
    lcd.setCursor(0, 1);
    lcd.print("IP: ");
    lcd.print(WiFi.localIP().toString());
    delay(5000);
  } else {
#if DEVMODE
    Serial.println(F("Error connecting to Internet"));
#endif
  }

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

  // Init ThingSpeak as we have network
  ThingSpeak.begin(client);

  // Register button interruption
  /*attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), changeDisplayMode, CHANGE);*/

  // Setup timers
  tasks.clockThread.onRun(updateTime);
  tasks.clockThread.setInterval(waitingTimes.clockSeconds * 1000);
  tasks.offsetTask.onRun(setupClock);
  tasks.offsetTask.setInterval(waitingTimes.offsetSeconds * 1000);
  tasks.wifiTask.onRun(publishWiFiStrength);
  tasks.wifiTask.setInterval(waitingTimes.wifiTaskSeconds * 1000);
  tasks.tempTask.onRun(publishTemperature);
  tasks.tempTask.setInterval(waitingTimes.tempTaskSeconds * 1000);
  tasks.humdTask.onRun(publishHumidity);
  tasks.humdTask.setInterval(waitingTimes.humdTaskSeconds * 1000);
  tasks.wlvlTask.onRun(publishWaterLevel);
  tasks.wlvlTask.setInterval(waitingTimes.wlvlTaskSeconds * 1000);
  tasks.displayTask.onRun(changeDisplayMode);
  tasks.displayTask.setInterval(waitingTimes.displayTaskSeconds * 1000);

#if OTA_ENABLED
  timers.ota.setInterval(5000, lookForOTAUpdates);
#endif
  timers.dhtSensor.setInterval(waitingTimes.tempHumdSensor, updateDHTInfo);
  timers.waterSensor.attach(waitingTimes.waterLevelSensor, updateWaterLevelInfo);
  timers.clockControl.attach(waitingTimes.clockSeconds, launchClockThread);
  timers.offsetControl.attach(waitingTimes.offsetSeconds, launchOffsetTask);
  timers.wifiTask.attach(waitingTimes.wifiTaskSeconds, launchWiFiMQTTTask);
  timers.tempTask.attach(waitingTimes.tempTaskSeconds, launchTempMQTTTask);
  timers.humdTask.attach(waitingTimes.humdTaskSeconds, launchHumdMQTTTask);
  timers.wlvlTask.attach(waitingTimes.wlvlTaskSeconds, launchWLvlMQTTTask);
  timers.updateStatistics.attach(waitingTimes.statisticsTaskSeconds, statisticsUpdate);
  timers.displayTask.attach(waitingTimes.displayTaskSeconds, changeDisplayMode);

  updateDHTInfo();
  updateWaterLevelInfo();
  updateTime();
  statisticsUpdate();

  digitalWrite(LED_PIN, LOW);

#if DEVMODE
  setupFinishedTime = millis();
#endif
}

void loop(void) {
  timers.dhtSensor.run();
#if OTA_ENABLED
  timers.ota.run();
#endif
  Portal.handleClient();
  switch((uint8_t) displayMode) {
    case DEFAULT_MODE:
      lcdPrintTime();
      lcdPrintDHT();
      break;
    case AVG_TEMP_HUM_MODE:
      lcdPrintTime();
      lcdPrintAvgDHT();
      break;
    case WATER_LEVEL_INDICATOR:
      lcdPrintTime();
      lcdPrintWaterLevel();
      break;
    case CLOCK_AND_DATE:
      lcdPrintTime();
      lcdPrintDate();
      break;
    case WIFI_INFORMATION:
      lcdPrintWiFiInformation();
      break;
    default:
      displayMode = DEFAULT_MODE;
      break;
  }
}


void initAutoConnect(String password) {
  config.apid = "BonsaiAIO";
  config.psk = password;
  config.retainPortal = true;
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

void initWaterValuesPercentages(void) {
  waterLevelPercentages.percentageLimit[9].upperLimit = 250;
  waterLevelPercentages.percentageLimit[9].lowerLimit = 240;
  waterLevelPercentages.percentageLimit[8].upperLimit = 240;
  waterLevelPercentages.percentageLimit[8].lowerLimit = 230;
  waterLevelPercentages.percentageLimit[7].upperLimit = 230;
  waterLevelPercentages.percentageLimit[7].lowerLimit = 220;
  waterLevelPercentages.percentageLimit[6].upperLimit = 220;
  waterLevelPercentages.percentageLimit[6].lowerLimit = 210;
  waterLevelPercentages.percentageLimit[5].upperLimit = 210;
  waterLevelPercentages.percentageLimit[5].lowerLimit = 200;
  waterLevelPercentages.percentageLimit[4].upperLimit = 200;
  waterLevelPercentages.percentageLimit[4].lowerLimit = 190;
  waterLevelPercentages.percentageLimit[3].upperLimit = 190;
  waterLevelPercentages.percentageLimit[3].lowerLimit = 180;
  waterLevelPercentages.percentageLimit[2].upperLimit = 180;
  waterLevelPercentages.percentageLimit[2].lowerLimit = 150;
  waterLevelPercentages.percentageLimit[1].upperLimit = 150;
  waterLevelPercentages.percentageLimit[1].lowerLimit = 120;
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

void lcdPrintTime(void) {
  if (dataTime.hasTimeChanged || displayModeChanged) {
#if DEVMODE
    Serial.print(F("Time: "));
    Serial.println(dataTime.formattedTime);
    delay(10);
#endif
    lcd.home();
    lcd.print(CLEAR_ROW);
    lcd.home();
    lcd.print(F("    "));
    lcd.print(dataTime.formattedTime);
    lcd.print(F("    "));
    dataTime.hasTimeChanged = false;
    /*if (displayModeChanged)
      displayModeChanged = false;*/
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
    dhtValues.hasTempChanged = false;
    lcd.setCursor(9, 1);
    lcd.print(F("  "));
    lcd.write(WATER_DROP.id);
    lcd.print(dhtValues.latestHumidity, 0);
    lcd.print(F("%"));
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
    lcd.setCursor(0, 1);
    lcd.print(CLEAR_ROW);
    if (waterLevelValues.waterValue < 25) {
      digitalWrite(LED_PIN, HIGH);
      lcd.setCursor(4, 1);
      lcd.print(F("! "));
      lcd.write(WATER_LEVEL_EMPTY.id);
    } else if ((waterLevelValues.waterValue >= 25) && (waterLevelValues.waterValue < 50)) {
      digitalWrite(LED_PIN, LOW);
      lcd.setCursor(6, 1);
      lcd.write(WATER_LEVEL_25.id);
    } else {
      digitalWrite(LED_PIN, LOW);
      lcd.setCursor(6, 1);
      lcd.write(WATER_LEVEL_75.id);
    }
    lcd.print(F(" "));
    lcd.print(waterLevelValues.waterValue);
    lcd.print(F("%"));
    if (displayModeChanged)
      displayModeChanged = false;
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
    if (displayModeChanged)
      displayModeChanged = false;
  }
}

void lcdPrintAvgDHT(void) {
  if (dhtValues.hasTempChanged || displayModeChanged) {
    lcd.setCursor(0, 1);
    lcd.print(F(" /"));
    lcd.write(TERMOMETER.id);
    lcd.print(getTempMean(), 1);
    lcd.print(F(" "));
    lcd.print((char)223);
    lcd.print(F("C"));
    dhtValues.hasTempChanged = false;
  }
  if (dhtValues.hasHumdChanged || displayModeChanged) {
    lcd.setCursor(9, 1);
    lcd.print(F(" /"));
    lcd.write(WATER_DROP.id);
    lcd.print(getHumdMean(), 0);
    lcd.print(F("%"));
    dhtValues.hasHumdChanged = false;
  }
  if (displayModeChanged)
      displayModeChanged = false;
}

void lcdPrintWiFiInformation(void) {
  if (WiFi.status() == WL_CONNECTED) {
    long rssi = WiFi.RSSI();
    String ssid = WiFi.SSID();
    if (displayModeChanged || (rssi != latestRSSI) || (ssid != latestSSID)) {
      lcd.clear();
      lcd.print(ssid.c_str());
      lcd.setCursor(0, 1);
      lcd.write(WIFI.id);
      lcd.print(F(" "));
      lcd.print(rssi);
      lcd.print(F(" dBm"));
      latestRSSI = rssi;
      latestSSID = ssid;
      if (displayModeChanged)
        displayModeChanged = false;
    }
  } else {
    printLCDCaptivePortalInformation(IPAddress(255, 255, 255, 255));
  }
}

void rootPage(void) {
  char content[] = "<a href=\"/_ac\">Click here to go to configuration</a>";
  Server.send(200, "text/plain", content);
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

void launchClockThread(void) {
  tasks.clockThread.run();
}

void updateTime(void) {
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

void updateDHTInfo(void) {
#if DEVMODE
  uint32_t currentTime = millis();
  uint32_t diff = currentTime - executionTimes.latestDHTExecution;
  Serial.print(F("Latest DHT execution about ")); Serial.println(String(diff) + " ms. ago");
  executionTimes.latestDHTExecution = currentTime;
#endif
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

void updateWaterLevelInfo(void) {
#if DEVMODE
  uint32_t currentTime = millis();
  uint32_t diff = currentTime - executionTimes.latestWaterExecution;
  Serial.print(F("Latest water execution about ")); Serial.print(String(diff) + " ms. ago");
  executionTimes.latestWaterExecution = currentTime;
#endif
  uint16_t currentWaterValue = analogRead(WATER_LEVEL_DATA_PIN);
  uint8_t percentageWaterValue = normalizeValue(currentWaterValue);
  if (percentageWaterValue != waterLevelValues.waterValue) {
    waterLevelValues.waterValue = percentageWaterValue;
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

void setupLatituteLongitude(void) {
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

void getTimezoneOffset(void) {
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
    if (httpCode > 0) {
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

void setupClock(void) {
  setupLatituteLongitude();
  getTimezoneOffset();
  ntp.setTimeOffset(geolocationInformation.offset);
}

void launchOffsetTask(void) {
  tasks.offsetTask.run();
}

void launchWiFiMQTTTask(void) {
  tasks.wifiTask.run();
}

void launchTempMQTTTask(void) {
  tasks.tempTask.run();
}

void launchHumdMQTTTask(void) {
  tasks.humdTask.run();
}

void launchWLvlMQTTTask(void) {
  tasks.wlvlTask.run();
}

void launchDisplayTask(void) {
  tasks.displayTask.run();
}

void publishWiFiStrength(void) {
  long rssi = 0;
  if (WiFi.status() == WL_CONNECTED) {
    rssi = WiFi.RSSI();
    int httpCode = ThingSpeak.writeField(mqtt.channelId, mqtt.fields.wifiStrength, rssi, mqtt.apiKey);
#if DEVMODE
  Serial.printf("Writing value to channel %i to field %i with value %.2f and api %s\n", mqtt.channelId, mqtt.fields.temperature, rssi, mqtt.apiKey);
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

    int httpCode = ThingSpeak.writeField(mqtt.channelId, mqtt.fields.temperature, dhtValues.latestTemperature, mqtt.apiKey);
#if DEVMODE
    Serial.printf("Writing value to channel %i to field %i with value %.2f and api %s\n", mqtt.channelId, mqtt.fields.temperature, dhtValues.latestTemperature, mqtt.apiKey);
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

    int httpCode = ThingSpeak.writeField(mqtt.channelId, mqtt.fields.humidity, dhtValues.latestHumidity, mqtt.apiKey);
#if DEVMODE
    Serial.printf("Writing value to channel %i to field %i with value %.2f and api %s\n", mqtt.channelId, mqtt.fields.temperature, dhtValues.latestHumidity, mqtt.apiKey);
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

    int httpCode = ThingSpeak.writeField(mqtt.channelId, mqtt.fields.temperature, waterLevelValues.waterValue, mqtt.apiKey);
#if DEVMODE
    Serial.printf("Writing value to channel %i to field %i with value %.2f and api %s\n", mqtt.channelId, mqtt.fields.temperature, waterLevelValues.waterValue, mqtt.apiKey);
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
  uint16_t temp_n = statistics.tempCurrentElement;
  uint16_t humd_n = statistics.humdCurrentElement;
#if DEVMODE
  Serial.printf("Saving values to field #%i|%i\n", temp_n, humd_n);
#endif
  statistics.tempStats[temp_n].value = dhtValues.latestTemperature;
  statistics.tempStats[temp_n].initialized = true;
  statistics.humdStats[humd_n].value = dhtValues.latestHumidity;
  statistics.humdStats[humd_n].initialized = true;

  statistics.tempCurrentElement = ((temp_n + 1) % MAX_STATS);
  statistics.humdCurrentElement = ((humd_n + 1) % MAX_STATS);
}

float getTempMean(void) {
#if DEVMODE
  Serial.println("Calculating temp mean...");
#endif
  uint32_t elements = 0;
  double sum = 0.0;
  tempMeasure element;
  for (int32_t i = (MAX_STATS) - 1; i >= 0; --i) {
    element = statistics.tempStats[i];
    if (element.initialized == true) {
      sum += element.value;
      ++elements;
#if DEVMODE
      Serial.printf("Current sum: %f - elements found: %i\n", sum, elements);
      delay(10);
#endif
    } else {
      break;
    }
  }
#if DEVMODE
  Serial.printf("Temperature mean: %.2f (elements: %i)\n", (elements == 0) ? dhtValues.latestTemperature : (sum / elements), elements);
#endif
  return (elements == 0) ? dhtValues.latestTemperature : (sum / elements);
}

float getHumdMean(void) {
#if DEVMODE
  Serial.println("Calculating humd mean...");
#endif
  uint32_t elements = 0;
  uint64_t sum = 0;
  humdMeasure element;
  for (uint32_t i = (MAX_STATS) - 1; i >= 0; --i) {
    element = statistics.humdStats[i];
    if (element.initialized == true) {
      sum += element.value;
      ++elements;
#if DEVMODE
      Serial.printf("Current sum: %f - elements found: %i\n", sum, elements);
      delay(10);
#endif
    } else {
      break;
    }
  }
#if DEVMODE
  Serial.printf("Humidity mean: %.2f (elements: %i)\n", (elements == 0) ? dhtValues.latestHumidity : (sum / elements), elements);
#endif
  return (elements == 0) ? dhtValues.latestHumidity : (sum / elements);
}

uint8_t normalizeValue(uint16_t analogValue) {
  if (analogValue >= UPPER_LIMIT)
    return 100;
  else if (analogValue <= LOWER_LIMIT)
    return 0;
  uint8_t i = 9;
  for ( ; i >= 1; --i) {
    if ((analogValue >= waterLevelPercentages.percentageLimit[i].lowerLimit) && (analogValue < waterLevelPercentages.percentageLimit[i].upperLimit))
      break;
  }
  return (i * 10);
}

#if OTA_ENABLED
void lookForOTAUpdates(void) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
#if DEVMODE
    Serial.println(F("Looking for OTAs..."));
    ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);
#endif
    t_httpUpdate_return returnValue = ESPhttpUpdate.update(client, OTA_URL, OTA_PORT, OTA_PATH, RUNNING_VERSION);

    switch (returnValue) {
      case HTTP_UPDATE_FAILED:
#if DEVMODE
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
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
#endif
