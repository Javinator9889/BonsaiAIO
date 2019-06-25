/**
 * Bonsai AIO - Copyright (C) 2019 - present by Javinator9889
 * 
 * Control water level, temperature,
 * time and bonsai light with an Arduino board.
 */

// Web control & WiFi libraries
//#include <ESP8266WiFi.h>
//#include <ESP8266WebServer.h>
//#include <AutoConnect.h>

// Components specific libraries
#include <LiquidCrystal.h>
#include <DHT.h>

// ESP8266 pinout
#include "PinConstants.h"

// Define whether the DEVMODE is active
// for saving sketch size - comment for
// disabling
#define DEVMODE   1

// Web control objects
//ESP8266WebServer  Server;
//AutoConnect       Portal(Server);
//AutoConnectConfig config;

// Components pins
const struct {
  uint8_t rs;
  uint8_t e;
  
  uint8_t d4;
  uint8_t d5;
  uint8_t d6;
  uint8_t d7;
} lcdPins = {D7, D6, D5, D4, D3, D2};

const struct {
  uint8_t data;
  uint8_t type;
} dhtPin = {D1, DHT11};

const unsigned int buttonPin = D0;
const int waterLevelDataPin = A0;

// Init components
LiquidCrystal lcd(lcdPins.rs, 
                  lcdPins.e, 
                  lcdPins.d4, 
                  lcdPins.d5, 
                  lcdPins.d6, 
                  lcdPins.d7);
DHT dht(dhtPin.data, dhtPin.type);
                  
// Global variables needed in hole project
unsigned long setupFinishedTime;
unsigned long cpuTicksPerSecond;
unsigned int cpuEvents;
unsigned long waterLevelWaitingTime;
bool printed;

// Define the functions that will be 
// available
void initAutoConnect();
void rootPage();
void initCpuTicksPerSecond();
String generateRandomString();


void setup() {
  // Initialize the seed with a no connected pin
//  randomSeed(analogRead(0));

  // Start the web server and cautive portal
//  Server.on("/", rootPage);
//  if (Portal.begin()) {
//    Serial.println("HTTP server:" + WiFi.localIP().toString());
//  }

  // Global variables definition
  cpuTicksPerSecond = -1;
  cpuEvents = 0;
  waterLevelWaitingTime = 0;
  printed = false;
  #if defined(DEVMODE)
    delay(500);
    Serial.begin(9600);
    Serial.println("Serial initialized");
  #endif
  setupFinishedTime = millis();
}


void loop() {
  // Do not start the code until we know
  // how many ticks happens each second
  if (cpuTicksPerSecond == -1) {
    initCpuTicksPerSecond();
    return;
  }
  if (!printed) {
    Serial.print("Número de ticks por segundo: ");
    Serial.println(cpuTicksPerSecond);
  }
  if (waterLevelWaitingTime == (cpuTicksPerSecond * 2)) {
    int waterValue = analogRead(waterLevelDataPin);
    Serial.println(waterValue);
    waterLevelWaitingTime = 0;
  } else
    ++waterLevelWaitingTime;
//  Portal.handleClient();
}



void initAutoConnect(String password) {
//  config.apid = "BonsaiAIO";
//  config.psk = password;
}

void rootPage() {
  char content[] = "Hello, world";
//  Server.send(200, "text/plain", content);
}

void initCpuTicksPerSecond() {
  unsigned long timeDifference = millis() - setupFinishedTime;
  ++cpuEvents;
  if ((timeDifference >= 1000) && (timeDifference <= 1100))
    cpuTicksPerSecond = cpuEvents;
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
