/**
 * WaterValues.h - normalize water analog value
 */
typedef struct 
{
    int16_t upperLimit;
    int16_t lowerLimit;
} percentagesLimit;


#ifndef WaterValues_h
#define WaterValues_h

#include <stdint.h>

class WaterValues
{
private:
    int16_t _upperLimit;
    int16_t _lowerLimit;
    percentagesLimit *_percentagesLimit[];
public:
    WaterValues(int16_t upperLimit, int16_t lowerLimit);
    void setPercentageLimit(uint8_t percentage, int16_t upperLimit, int16_t lowerLimit);
    uint8_t normalizeValue(int16_t value);
    ~WaterValues(void);
};

#endif
