/**
 * Statistics.c - library for calculating statistics
 * with the given data.
 */

#include <limits.h>
#include <stdint.h>
#include "Statistics.h"

Statistics::Statistics(uint32_t maximumSize) {
    *_measures = new measure[maximumSize] {
        {0.0, "", false}
    };
    _maximumSize = maximumSize;
    _max   = new measure{0.0, "", true};
    _min   = new measure{0.0, "", true};
    _n     = 0;
    _total = 0;
}

int Statistics::add(float element, char *time) {
    _measures[_n]->element = element;
    _measures[_n]->time    = time;
    _measures[_n]->init    = true;
    if (element > _max->element) {
        _max->element = element;
        _max->time    = time;
    } else if (element < _min->element) {
        _min->element = element;
        _min->time    = time;
    }
    _n = (_n + 1) % _maximumSize;
    _total = (_total == LONG_MAX) ? 1 : (_total + 1);
    return _n;
}

int Statistics::getCurrentAmountOfElements() {
    int elements = 0;
    for (uint32_t i = (_maximumSize - 1); i >= 0; --i)
    {
        if (_measures[i]->init)
        {
            ++elements;
        }
    }
    
    return elements;
}

long Statistics::getTotalAmountOfElements() {
    return _total;
}

measure Statistics::getMaximum() {
    return *_max;
}

measure Statistics::getMinimum() {
    return *_min;
}

float Statistics::calculateMean() {
    int   elements = 0;
    float sum = 0.0;
    for (uint32_t i = (_maximumSize - 1); i >= 0; --i)
    {
        if (_measures[i]->init)
        {
            ++elements;
            sum += _measures[i]->element;
        }
    }
    return (elements == 0) ? INT_MIN : (sum / elements);
}

measure Statistics::getLatestValueStored() {
    return *_measures[_n];
}

Statistics::~Statistics() {
    delete[] _measures;
    delete _max;
    delete _min;
}
