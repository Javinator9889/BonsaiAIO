#ifndef SensorStats_h
#define SensorStats_h

class SensorStats
{
    public:
        SensorStats(void);
        void reset(void);
        double getMean(void);
        float getMax(void);
        float getMin(void);
        void add(float value);
        ~SensorStats(void);
    private:
        double _sum;
        long   _elements;
        float  _min;
        float  _max;
        float  _latest;
};

#endif