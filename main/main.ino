/**
 * Bonsai AIO - Copyright (C) 2019 - present by Javinator9889
 * 
 * Control water level, temperature,
 * time and bonsai light with an Arduino board.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>

ESP8266WebServer  Server;
AutoConnect       Portal(Server);
AutoConnectConfig config;

void initAutoConnect();

void rootPage();

String generateRandomString();

void setup() {
  randomSeed(analogRead(0));
  Serial.begin(9600);
  Serial.println();
  Serial.println("Serial initialized");
  Server.on("/", rootPage);
  if (Portal.begin()) {
    Serial.println("HTTP server:" + WiFi.localIP().toString());
  }
}
void loop() {
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
