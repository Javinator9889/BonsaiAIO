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

#include <string.h>
#include "MuX16.h"


MuX16::MuX16(char cs_pins[4], char e_pin, char poweroff_pin, char read_pin) {
    memcpy(MuX16::channel_pins, cs_pins, 4);
    MuX16::e = e_pin;
    MuX16::poweroff_pin = poweroff_pin;
    MuX16::read_pin = read_pin;

    if (MuX16::e != -1)
        pinMode(MuX16::e, OUTPUT);
    if (MuX16::poweroff_pin != -1)
        pinMode(MuX16::poweroff_pin, OUTPUT);
    if (MuX16::read_pin != -1)
        pinMode(MuX16::read_pin, OUTPUT);

    for (int i = 0; i < 4; ++i) {
        if (MuX16::channel_pins[i] != -1)
            pinMode(MuX16::channel_pins[i], OUTPUT);
    }
}

u8 MuX16::set_channel(u_char ch) {
    if (ch >= 16)
        return EXIT_FAILURE;
    for (int i = 0; i < 4; ++i) {
        if (MuX16::channel_pins[i] == -1)
            continue;
        u8 cbit = ch & 0x1;
        digitalWrite(MuX16::channel_pins[i], cbit);
    }
    return EXIT_SUCCESS;
}

u16 MuX16::read(void) {
    if (MuX16::read_pin == -1)
        return EXIT_FAILURE;
    MuX16::poweron();
    pinMode(MuX16::read_pin, INPUT);
    return analogRead(MuX16::read_pin);
}

void MuX16::write(u16 value) {
    if (MuX16::read_pin == -1)
        return;
    MuX16::poweron();
    if (value >= 1024)
        value = 1023;
    pinMode(MuX16::read_pin, OUTPUT);
    analogWrite(MuX16::read_pin, value);
}

void MuX16::poweron(void) {
    if (MuX16::poweroff_pin == -1)
        return;
    digitalWrite(MuX16::poweroff_pin, HIGH);
}

void MuX16::poweroff(void) {
    if (MuX16::poweroff_pin == -1)
        return;
    digitalWrite(MuX16::poweroff_pin, LOW);
}

MuX16::~MuX16(void) {
    for (int i = 0; i < 4; ++i) {
        if (MuX16::channel_pins[i] == -1)
            continue;
        digitalWrite(MuX16::channel_pins[i], LOW);
    }
    if (MuX16::e != -1)
        digitalWrite(MuX16::e, LOW);
    if (MuX16::poweroff_pin != -1)
        digitalWrite(MuX16::poweroff_pin, LOW);
    if (MuX16::read_pin != -1) {
        pinMode(MuX16::read_pin, OUTPUT);
        analogWrite(MuX16::read_pin, 0);
    }
}
