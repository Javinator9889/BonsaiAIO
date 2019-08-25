#include <limits>
#include "SensorStats.h"

SensorStats::SensorStats(void) {
    reset();
}

void SensorStats::reset(void) {
    _sum = 0;
    _elements = 0;
    _min = std::numeric_limits<float>::max();
    _max = std::numeric_limits<float>::min();
    _latest = 0;
    return;
}

double SensorStats::getMean(void) {
    return ((_elements != 0) ? (_sum / _elements) : _latest);
}

float SensorStats::getMax(void) {
    return _max;
}

float SensorStats::getMin(void) {
    return _min;
}

void SensorStats::add(float value) {
    if (_sum >= (std::numeric_limits<double>::max() - value) ||
        _elements == (std::numeric_limits<long>::max() - 1))
    {
        reset();
    }
    _sum += value;
    ++_elements;
    _latest = value;

    if (value > _max) {
        _max = value;
    }
    if (value < _min) {
        _min = value;
    }

    return;
}

SensorStats::~SensorStats() {
    reset();
}