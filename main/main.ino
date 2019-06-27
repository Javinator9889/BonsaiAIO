/**
 * Bonsai AIO - Copyright (C) 2019 - present by Javinator9889
 * 
 * Control water level, temperature,
 * time and bonsai light with an Arduino board.
 */

// Web control & WiFi libraries
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>

// Components specific libraries
#include <LiquidCrystal.h>
#include <DHT.h>
// Custom display modes
#include "DisplayModes.h"

// Other libraries
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Thread.h>

// ESP8266 pinout
#include "PinConstants.h"

// Statistics library
#include "Statistics.h"

// Ticker timing
#include <Ticker.h>

// Define whether the DEVMODE is active
// for saving sketch size - set to 0 for
// disabling
//
// VVV - extra verbosity
#define DEVMODE     1
#define VVV         0

// Maximum stats we will keep in memory
#define MAX_STATS   256

// Other "define" constants
#define UTC_OFFSET  3600 // UTC+1

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
NTPClient ntp(ntpUDP, "europe.pool.ntp.org", UTC_OFFSET);
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

const struct {
  uint8_t data;
  uint8_t type;
} DHT_PIN = {D1, DHT11};

const uint8_t BUTTON_PIN = D9;
const uint8_t WATER_LEVEL_DATA_PIN = A0;

// Init components
LiquidCrystal lcd(LCD_PINS.rs, 
                  LCD_PINS.e, 
                  LCD_PINS.d4, 
                  LCD_PINS.d5, 
                  LCD_PINS.d6, 
                  LCD_PINS.d7);
DHT dht(DHT_PIN.data, DHT_PIN.type);
                  
// Global variables needed in hole project
volatile uint8_t displayMode;
#if DEVMODE
  volatile uint32_t setupFinishedTime;
  volatile uint32_t cpuEvents;
  volatile uint32_t cpuTicksPerSecond;
  volatile uint32_t waterLevelWaitingTime;
  volatile uint32_t aSecond = 0;
  volatile uint32_t latestTickerExecution = 0;
  bool printed = false;
#endif

// Constant structs that contains
// application data
volatile struct {
  float waterLevelSensor;
  float tempHumdSensor;
  float clockSeconds;
} waitingTimes = {7200, 180, 1};

struct {
  Ticker dhtSensor;
  Ticker waterSensor;
  Ticker clockControl;
} sensors;

struct {
  float latestTemperature;
  float latestHumidity;
} dhtValues = {0.0, 0.0};

struct {
  uint16_t waterValue;
} waterLevelValues = {0};

struct {
  String formattedTime;
  String formattedDate;
  bool hasTimeChanged;
  bool hasDateChanged;
  bool mustShowSeparator;
} dataTime = {"00:00 00", "1900-01-01", true, true, true};

// Define the functions that will be 
// available
void initAutoConnect();
void rootPage();
//void initCpuTicksPerSecond();
String generateRandomString();
ICACHE_RAM_ATTR void changeDisplayMode();
void launchClockThread();
void updateTime();
void updateDHTInfo();
void updateWaterLevelInfo();
bool areTimesDifferent(String time1, String time2);


void setup() {
  #if DEVMODE
    Serial.begin(9600);
    Serial.println("Serial initialized");
  #endif
  // Initialize the seed with a no connected pin
  randomSeed(analogRead(0));

  Serial.println("Starting web server and cautive portal");
  // Start the web server and cautive portal
  Server.on("/", rootPage);
  if (Portal.begin()) {
    #if DEVMODE
      Serial.println("Successfully connected to Internet!");
      Serial.println("HTTP server:" + WiFi.localIP().toString());
    #endif
  } else {
    #if DEVMODE
      Serial.println("Error connecting to Internet");
    #endif
  }

  // Init the UTP client - if we are here there is Internet connection
  ntp.begin();
  
  // Global variables definition
  cpuTicksPerSecond     = 0;
  cpuEvents             = 0;
  waterLevelWaitingTime = 0;
  displayMode           = DEFAULT_MODE;
  
  #if DEVMODE
    Serial.print("Setup elapsed time: ");
    Serial.println(millis());
  #endif

  // Register button interruption
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), changeDisplayMode, CHANGE);
  
  // Setup timers
  clockThread.onRun(updateTime);
  clockThread.setInterval(1000);

  sensors.dhtSensor.attach(waitingTimes.tempHumdSensor, updateDHTInfo);
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
      Serial.print("Date: ");
      Serial.println(dataTime.formattedDate);
      dataTime.hasDateChanged = false;
    }
    if (dataTime.hasTimeChanged) {
      Serial.print("Time: ");
      Serial.println(dataTime.formattedTime);
      dataTime.hasTimeChanged = false;
    }
  #endif
  /*
   // Do not start the code until we know
   // how many ticks happens each second
   if (cpuTicksPerSecond == 0) {
    initCpuTicksPerSecond();
    return;
  }
  #if DEVMODE
    if (!printed) {
      Serial.print("Ticks per second: ");
      Serial.println(cpuTicksPerSecond);
      printed = true;
    }
    if (aSecond == cpuTicksPerSecond) {
      Serial.println("A second has passed");
      aSecond = 0;
    } else {
      ++aSecond;
    }
  #endif
  if (waterLevelWaitingTime == waitingTimes.waterLevelSensor) {
    int waterValue = analogRead(WATER_LEVEL_DATA_PIN);
    #if DEVMODE
      Serial.print("Valor del agua: ");
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
    uint32_t diff = currentTime - latestTickerExecution;
    Serial.println("Latest ticker execution about " + String(diff) + " ms. ago");
    latestTickerExecution = currentTime;
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
  dhtValues.latestTemperature = dht.readTemperature();
  dhtValues.latestHumidity = dht.readHumidity();
}

void updateWaterLevelInfo() {
  waterLevelValues.waterValue = analogRead(WATER_LEVEL_DATA_PIN);
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
