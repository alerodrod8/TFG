/*
 * utils.c
 *
 *  Created on: Apr 26, 2026
 *      Author: rodri
 */

#include "utils.h"

void ITM_Print(char *msg)
{
    while (*msg)
    {
        ITM_SendChar(*msg++);
    }
}

uint16_t rgb2bgr565(uint16_t c)
{
    return ((c & 0xF800) >> 11) |
           ( c & 0x07E0)       |
           ((c & 0x001F) << 11);
}

const uint8_t test_font[4][5] =
{
    // T
    {
        0b11111,
        0b00100,
        0b00100,
        0b00100,
        0b00100
    },

    // E
    {
        0b11111,
        0b10000,
        0b11110,
        0b10000,
        0b11111
    },

    // S
    {
        0b11111,
        0b10000,
        0b11111,
        0b00001,
        0b11111
    },

    // T
    {
        0b11111,
        0b00100,
        0b00100,
        0b00100,
        0b00100
    }
};
