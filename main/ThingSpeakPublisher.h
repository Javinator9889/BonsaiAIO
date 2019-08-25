#ifndef ThingSpeakPublisher_h
#define ThingSpeakPublisher_h

#include <ESP8266WiFi.h>
#include "ThingSpeak.h"

class ThingSpeakPublisher
{
    public:
        ThingSpeakPublisher(unsigned long channelId, const char *api,
                            unsigned int fieldNumber, WiFiClient client);
        int publish(long value);
        int publish(float value);
        int publish(int value);
        int publish(const char *value);
        ~ThingSpeakPublisher(void);
    private:
        unsigned long _channelId;
        const char *_api;
        unsigned int _field;
};

#endif