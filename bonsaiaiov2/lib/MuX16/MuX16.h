/**                             MuX16.h
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

#ifndef MUX16_H
#define MUX16_H

#include <Arduino.h>


class MuX16 {
    public:
        MuX16(char cs_pins[4], char e_pin, char poweroff_pin, char read_pin);
        u8 set_channel(u_char ch);
        u16 read(void);
        void write(u16 value);
        void poweron(void);
        void poweroff(void);
        ~MuX16(void);
    private:
        char channel_pins[4];
        char e;
        char poweroff_pin;
        char read_pin;
};

#endif // MUX16_H