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
#define DEVMODE     1

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
volatile uint32_t setupFinishedTime;
volatile uint32_t cpuEvents;
volatile uint32_t cpuTicksPerSecond;
volatile uint32_t waterLevelWaitingTime;
volatile uint8_t  displayMode;
#if DEVMODE
  volatile uint32_t aSecond = 0;
  bool printed = false;
#endif

// Constant structs that contains
// application data
volatile struct {
  uint32_t waterLevelSensor;
  uint32_t tempHumdSensor;
  uint32_t clockSeconds;
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

// Other useful variables
String formattedTime = "00:00 00";
bool mustShowSeparator;

// Define the functions that will be 
// available
void initAutoConnect();
void rootPage();
void initCpuTicksPerSecond();
String generateRandomString();
void changeDisplayMode();
void launchClockThread();
void updateTime();
void updateDHTInfo();
void updateWaterLevelInfo();


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
  mustShowSeparator     = true;
  
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
  // Do not start the code until we know
  // how many ticks happens each second
  /*if (cpuTicksPerSecond == 0) {
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
 * Unised since "Ticker" installation
 */
void initCpuTicksPerSecond() {
  uint32_t timeDifference = (uint32_t) (millis() - setupFinishedTime);
  ++cpuEvents;
  if (timeDifference >= 999) {
    cpuTicksPerSecond = cpuEvents;
    waitingTimes.waterLevelSensor *= cpuTicksPerSecond;
    waitingTimes.tempHumdSensor *= cpuTicksPerSecond;
    waitingTimes.clockSeconds *= cpuTicksPerSecond;
  }
}

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
  clockThread.run();
}

void updateTime() {
  if (WiFi.status() == WL_CONNECTED) {
    ntp.update();
    formattedTime = ntp.getHours();
    formattedTime += (mustShowSeparator) ? ":" : " ";
    formattedTime += ntp.getMinutes() + " " + ntp.getSeconds();
    mustShowSeparator = !mustShowSeparator;
  } else {
    formattedTime = "No Internet!";
  }
}

void updateDHTInfo() {
  dhtValues.latestTemperature = dht.readTemperature();
  dhtValues.latestHumidity = dht.readHumidity();
}

void updateWaterLevelInfo() {
  waterLevelValues.waterValue = analogRead(WATER_LEVEL_DATA_PIN);
}
