/**
   Bonsai AIO - Copyright (C) 2019 - present by Javinator9889

   Control water level, temperature,
   time and bonsai light with an Arduino board.
*/

// Web control & WiFi libraries
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFiMulti.h>
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
#include <Time.h>
#include <TimeLib.h>

// ThingSpeak publisher class
#include "ThingSpeakPublisher.h"

// ESP8266 pinout
//#include "PinConstants.h"

// Statistics library
#include "SensorStats.h"

// Custom display modes
#include "DisplayModes.h"

// API Keys
#include "ApiKeys.h"

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
#define OTA_ENABLED 1

#if DEVMODE
#define PRINT_DEBUG_MESSAGES
#endif

#if OTA_ENABLED
#include <ESP8266httpUpdate.h>
#endif

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
#define DHT_PIN     D1
// As the setup is inside a box, we have to "fix" the temperature (about 2 ºC degrees)
#define TEMPERATURE_FIX -1.5
#define CLEAR_ROW   "                "
#define UPPER_LIMIT 250
#define LOWER_LIMIT 120

// Web control objects
WiFiClient client;
ESP8266WebServer  Server;
AutoConnect       Portal(Server);
AutoConnectConfig config;

// Time components
WiFiUDP ntpUDP;
NTPClient ntp(ntpUDP, NTP_SERVER);

// Threads
struct {
  Thread offsetTask;
  Thread displayTask;
} tasks = {Thread(), Thread()};

// Components pins
const struct {
  uint8_t data;
  uint8_t latch;
  uint8_t clk;
} LCD_PINS = {D6, D7, D8};

const uint8_t LED_PIN = D0;
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
  uint16_t otaCheckMs;
  uint32_t clearStatsSeconds;
  uint16_t clockSyncInterval;
} waitingTimes = {
  18,       // 18 seconds
  9000,     // 09 seconds
  1,        // 01 second
  1800 ,    // 30 minutes
  615,      // 10 minutes 05 seconds
  332,      // 05 minutes 32 seconds
  297,      // 04 minutes 57 seconds
  1807,     // 30 minutes 07 seconds
  60,       // 60 seconds
  10,       // 10 seconds
  60000,    // 60 seconds
  86400,    // 01 day
  60        // 60 seconds
};

struct {
  SimpleTimer dhtSensor;
  SimpleTimer clockControl;
  Ticker waterSensor;
  Ticker offsetControl;
  Ticker updateStatistics;
  Ticker displayTask;
  Ticker clearStatsTask;
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
} waterLevelValues = {0, true};

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
} geolocationInformation = {"", "", "", 0};

// MQTT information
ThingSpeakPublisher mqttWiFi(CHANNEL_ID, THINGSPEAK_API, 1, client);
ThingSpeakPublisher mqttTemp(CHANNEL_ID, THINGSPEAK_API, 2, client);
ThingSpeakPublisher mqttHumd(CHANNEL_ID, THINGSPEAK_API, 3, client);
ThingSpeakPublisher mqttWlvl(CHANNEL_ID, THINGSPEAK_API, 4, client);

typedef struct {
  int16_t upperLimit;
  int16_t lowerLimit;
} percentagesLimit;

SensorStats tempStats;
SensorStats humdStats;

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
String formatDigits(int value);
time_t syncTimeFromNTP(void);
void updateDHTInfo(void);
void updateWaterLevelInfo(void);
bool areTimesDifferent(String time1, String time2);
void setupLatituteLongitude(void);
void getTimezoneOffset(void);
void setupClock(void);
void launchOffsetTask(void);
void launchDisplayTask(void);
void publishWiFiStrength(void);
void publishTemperature(void);
void publishHumidity(void);
void publishWaterLevel(void);
void statisticsUpdate(void);
float getTempMean(void);
float getHumdMean(void);
uint8_t normalizeValue(uint16_t analogValue);
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

  // Init ThingSpeak as we have network
  ThingSpeak.begin(client);

  // Setup timers
  tasks.offsetTask.onRun(setupClock);
  tasks.offsetTask.setInterval(waitingTimes.offsetSeconds * 1000);
  tasks.displayTask.onRun(changeDisplayMode);
  tasks.displayTask.setInterval(waitingTimes.displayTaskSeconds * 1000);

#if OTA_ENABLED
  timers.ota.setInterval(waitingTimes.otaCheckMs, lookForOTAUpdates);
#endif
  timers.dhtSensor.setInterval(waitingTimes.tempHumdSensor, updateDHTInfo);
  timers.waterSensor.attach(waitingTimes.waterLevelSensor, updateWaterLevelInfo);
//  timers.clockControl.setInterval(waitingTimes.clockSeconds * 1000, updateTime);
  timers.offsetControl.attach(waitingTimes.offsetSeconds, launchOffsetTask);
  timers.wifiTask.setInterval(waitingTimes.wifiTaskSeconds * 1000, publishWiFiStrength);
  timers.tempTask.setInterval(waitingTimes.tempTaskSeconds * 1000, publishTemperature);
  timers.humdTask.setInterval(waitingTimes.humdTaskSeconds * 1000, publishHumidity);
  timers.wlvlTask.setInterval(waitingTimes.wlvlTaskSeconds * 1000, publishWaterLevel);
  timers.updateStatistics.attach(waitingTimes.statisticsTaskSeconds, statisticsUpdate);
  timers.displayTask.attach(waitingTimes.displayTaskSeconds, changeDisplayMode);
  timers.clearStatsTask.attach(waitingTimes.clearStatsSeconds, clearStats);

  updateDHTInfo();
  updateWaterLevelInfo();
  updateTime();
  statisticsUpdate();

  digitalWrite(LED_PIN, LOW);

#if DEVMODE
  setupFinishedTime = millis();
  Serial.print(F("Setup elapsed time: "));
  Serial.println(setupFinishedTime);
#endif
}

void loop(void) {
//  timers.clockControl.run();
  updateTime();
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
  timers.wifiTask.run();
  timers.tempTask.run();
  timers.humdTask.run();
  timers.wlvlTask.run();
}


void initAutoConnect(String password) {
  config.apid = "BonsaiAIO";
  config.psk = password;
  config.retainPortal = false;
  config.hostName = "BonsaiAIO";
  config.title = "BonsaiAIO";
  config.dns1 = IPAddress(8, 8, 8, 8);
  config.dns2 = IPAddress(8, 8, 4, 4);
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
    if (displayModeChanged)
      displayModeChanged = false;
  }
}

void lcdPrintAvgDHT(void) {
  if (dhtValues.hasTempChanged || displayModeChanged) {
    lcd.setCursor(0, 1);
    lcd.print(F("/"));
    lcd.write(TERMOMETER.id);
    lcd.print(getTempMean(), 2);
    lcd.print(F(" "));
    lcd.print((char)223);
    lcd.print(F("C"));
    dhtValues.hasTempChanged = false;
  }
  if (dhtValues.hasHumdChanged || displayModeChanged) {
    lcd.setCursor(10, 1);
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
    Server.on("/", rootPage);
    Portal.config(config);
    Portal.onDetect(printLCDCaptivePortalInformation);
    Portal.begin();
  }
}

void rootPage(void) {
  char content[] = "<a href=\"/_ac\">Click here to go to configuration</a>";
  Server.send(200, "text/html", content);
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
      formattedTime += currentMinutes + " " + currentSeconds;
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

/*void updateTime(void) {
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
}*/

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
#if DEVMODE
    Serial.print(F("Temperature: "));
    Serial.print(event.temperature + TEMPERATURE_FIX);
    Serial.println(F("°C"));
    Serial.printf("No fixed temp: %.2f\n", event.temperature);
#endif
    if (event.temperature != dhtValues.latestTemperature) {
      dhtValues.latestTemperature = (event.temperature + TEMPERATURE_FIX);
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
    http.begin(client, EXTREME_IP_URL);
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
    http.begin(client, formattedUrl);
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

void launchDisplayTask(void) {
  tasks.displayTask.run();
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

float getTempMean(void) {
#if DEVMODE
  Serial.println("Calculating temp mean...");
  Serial.printf("Temperature mean: %.2f\n", tempStats.getMean());
#endif
  return (tempStats.getMean());
}

float getHumdMean(void) {
#if DEVMODE
  Serial.println("Calculating humd mean...");
  Serial.printf("Humidity mean: %.2f\n", humdStats.getMean());
#endif
  return (humdStats.getMean());
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

void clearStats(void) {
  tempStats.reset();
  humdStats.reset();
}

#if OTA_ENABLED
void lookForOTAUpdates(void) {
  if (WiFi.status() == WL_CONNECTED) {
//    WiFiClient client;
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
        break;
    }
  }
}
#endif
