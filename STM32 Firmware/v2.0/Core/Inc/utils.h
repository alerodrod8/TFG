/*
 * utils.h
 *
 *  Created on: Apr 26, 2026
 *      Author: rodri
 */

#ifndef SRC_UTILS_H_
#define SRC_UTILS_H_

#include <stdint.h>
#include "stm32f7xx.h"
#include <core_cm7.h>

extern const uint8_t test_font[4][5];

void ITM_Print(char *msg);
uint16_t rgb2bgr565(uint16_t);

#endif /* SRC_UTILS_H_ */
