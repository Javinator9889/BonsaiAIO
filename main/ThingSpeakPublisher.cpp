#include "ThingSpeakPublisher.h"

#include <ESP8266WiFi.h>
#include "ThingSpeak.h"

ThingSpeakPublisher::ThingSpeakPublisher(unsigned long channelId, const char *api,
                                         unsigned int fieldNumber, WiFiClient client) {
    _channelId = channelId;
    _api = api;
    _field = fieldNumber;
    ThingSpeak.begin(client);
}

int ThingSpeakPublisher::publish(long value) {
    return (ThingSpeak.writeField(_channelId, _field, value, _api));
}

int ThingSpeakPublisher::publish(float value) {
    return (ThingSpeak.writeField(_channelId, _field, value, _api));
}

int ThingSpeakPublisher::publish(int value) {
    return (ThingSpeak.writeField(_channelId, _field, value, _api));
}

int ThingSpeakPublisher::publish(const char *value) {
    return (ThingSpeak.writeField(_channelId, _field, value, _api));
}

ThingSpeakPublisher::~ThingSpeakPublisher(void) {
    return;
}