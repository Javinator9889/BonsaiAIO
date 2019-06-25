/**
 * Statistics.h - library for calculating statistics
 * with the given data.
 */
typedef struct measure
{
    float   element;
    char    *time;
    bool    init;
};

#ifndef Statistics_h
#define Statistics_h

class Statistics
{
private:
    measure        *_measures[];
    measure        *_max;
    measure        *_min;
    unsigned int   _n;
    unsigned int   _maximumSize;
    unsigned long  _total;
public:
    Statistics(unsigned int maximumSize);
    int add(float element, char *time);
    int getCurrentAmountOfElements();
    long getTotalAmountOfElements();
    float calculateMean();
    measure getMaximum();
    measure getMinimum();
    ~Statistics();
};

#endif
