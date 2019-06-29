/**
 * WaterValues.cpp - normalize water analog value
 */
#include <stdint.h>
#include "WaterValues.h"

WaterValues::WaterValues(int16_t upperLimit, int16_t lowerLimit) {
    _upperLimit = upperLimit;
    _lowerLimit = lowerLimit;
    *_percentagesLimit = new percentagesLimit[11] { 
        {
            0,  // upperLimit
            0   // lowerLimit
        }
    };
}

void WaterValues::setPercentageLimit(uint8_t percentage, int16_t upperLimit, int16_t lowerLimit) {
    uint8_t arrayPosition = (percentage / 10);
    if (arrayPosition > 10)
        return;
    _percentagesLimit[arrayPosition]->upperLimit = upperLimit;
    _percentagesLimit[arrayPosition]->lowerLimit = lowerLimit;
}

uint8_t WaterValues::normalizeValue(int16_t value) {
    if (value >= _upperLimit)
        return 100;
    else if (value <= _lowerLimit)
        return 0;
    uint8_t i = 10;
    for ( ; i >= 0; --i)
    {
        if ((value >= _percentagesLimit[i]->lowerLimit) && (value < _percentagesLimit[i]->upperLimit))
            break;
    }
    return (i * 10);
}

WaterValues::~WaterValues(void) {
    delete[] _percentagesLimit;
}
