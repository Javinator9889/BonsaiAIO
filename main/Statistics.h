/**
 * Statistics.h - library for calculating statistics
 * with the given data.
 */
typedef struct
{
    float   element;
    char    *time;
    bool    init;
} measure;

#ifndef Statistics_h
#define Statistics_h

#include <stdint.h>

class Statistics
{
private:
    measure        *_measures[];
    measure        *_max;
    measure        *_min;
    uint32_t   _n;
    uint32_t   _maximumSize;
    unsigned long  _total;
public:
    Statistics(uint32_t maximumSize);
    int add(float element, char *time);
    int getCurrentAmountOfElements();
    long getTotalAmountOfElements();
    float calculateMean();
    measure getMaximum();
    measure getMinimum();
    measure getLatestValueStored();
    ~Statistics();
};

#endif
