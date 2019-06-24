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

// Define whether the DEVMODE is active
// for saving sketch size - comment for
// disabling
#define DEVMODE   1

// Web control objects
ESP8266WebServer  Server;
AutoConnect       Portal(Server);
AutoConnectConfig config;

// Components pins
const struct {
  int rs;
  int e;
  
  int d4;
  int d5;
  int d6;
  int d7;
} lcdPins = {12, 11, 5, 4, 3, 2};

const struct {
  int data;
  uint8_t type;
} dhtPin = {1, DHT11};

const unsigned int buttonPin = 0;

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

// Define the functions that will be 
// available
void initAutoConnect();
void rootPage();
void initCpuTicksPerSecond();
String generateRandomString();


void setup() {
  // Initialize the seed with a no connected pin
  randomSeed(analogRead(0));
  
  #if defined(DEVMODE)
    Serial.begin(9600);
    Serial.println("Serial initialized");
  #endif

  // Start the web server and cautive portal
  Server.on("/", rootPage);
  if (Portal.begin()) {
    Serial.println("HTTP server:" + WiFi.localIP().toString());
  }

  // Global variables definition
  cpuTicksPerSecond = -1;
  cpuEvents = 0;
  setupFinishedTime = millis();
}


void loop() {
  // Do not start the code until we know
  // how many ticks happens each second
  if (cpuTicksPerSecond == -1) {
    initCpuTicksPerSecond();
    return;
  }
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
