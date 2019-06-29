/**
 * LCDIcons.h - a collection of LCD icons.
 */
typedef struct
{
    unsigned int id;
    byte icon[8];
} IconInformation;

#ifndef LCDIcons_h
#define LCDIcons_h
static const IconInformation WATER_DROP {0, {B00100, B00100, B01010, B01010, B10001, B10001, B10001, B01110}};
static const IconInformation TERMOMETER {1, {B00100, B01010, B01010, B01110, B01110, B11111, B11111, B01110}};
static const IconInformation AVG {2, {B11100, B10100, B11100, B10011, B00101, B10011, B10101, B01011}};
static const IconInformation WIFI {3, {B00000, B00100, B01010, B10001, B00100, B01010, B00000, B00100}};
static const IconInformation KEY {4, {B00100, B00110, B00100, B00100, B00100, B01010, B01010, B00100}};
static const IconInformation WATER_LEVEL_EMPTY {5, {B10001, B10001, B10001, B10001, B10001, B10001, B10001, B11111}};
static const IconInformation WATER_LEVEL_25 {6, {B10001, B10001, B10001, B10001, B10001, B11111, B11111, B11111}};
static const IconInformation WATER_LEVEL_50 {7, {B10001, B10001, B10001, B10001, B11111, B11111, B11111, B11111}};
static const IconInformation WATER_LEVEL_75 {8, {B10001, B10001, B11111, B11111, B11111, B11111, B11111, B11111}};
static const IconInformation WATER_LEVEL_100 {9, {B11111, B11111, B11111, B11111, B11111, B11111, B11111, B11111}};
static const IconInformation WARNING {10, {B00000, B00100, B00100, B00100, B00100, B00000, B00100, B00000}};
#endif
