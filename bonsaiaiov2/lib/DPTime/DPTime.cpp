/**                             DPTime.cpp
*                  Copyright (C) 2019 - Javinator9889
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*      the Free Software Foundation, either version 3 of the License, or
*                   (at your option) any later version.
*
*       This program is distributed in the hope that it will be useful,
*       but WITHOUT ANY WARRANTY; without even the implied warranty of
*        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*               GNU General Public License for more details.
*
*     You should have received a copy of the GNU General Public License
*    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "DPTime.h"

DPTime::DPTime(void) {
    DPTime::time_addition = 0;
}

void DPTime::setup(rst_info *reset_information) {
    int magic_number;
    system_rtc_mem_read(RTC_MAGIC_ADDR, &magic_number, sizeof(magic_number));
    if (magic_number != MAGIC_NUMBER) {
        magic_number = MAGIC_NUMBER;
        system_rtc_mem_write(RTC_MAGIC_ADDR, &magic_number, 
                             sizeof(MAGIC_NUMBER));
        DPTime::time_addition = 0;
    } else {
        if (reset_information->reason != REASON_DEEP_SLEEP_AWAKE) {
            DPTime::time_addition = 0;
            return;
        }
        system_rtc_mem_read(RTC_TADD_ADDR, &time_addition, 
                            sizeof(time_addition));
    }
}

ulong DPTime::tmillis(void) {
    return (millis() + time_addition);
}

void DPTime::prepare_deep_sleep(void) {
    system_rtc_mem_write(RTC_TADD_ADDR, &time_addition, sizeof(time_addition));
}

DPTime::~DPTime(void) {;}
