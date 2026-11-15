/*
 * st7796s.h
 *
 *  Created on: May 13, 2026
 *      Author: rodri
 */

#ifndef INC_ST7796S_H_
#define INC_ST7796S_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "utils.h"
#include "string.h"

#define ST7796_WIDTH   480
#define ST7796_HEIGHT  320

#define DMA_LINES 4

extern volatile uint8_t lcd_dma_transfer_complete;

void ST7796_Init(void);
void ST7796_WriteCommand(uint8_t cmd);
void ST7796_WriteData(uint8_t data);
void ST7796_SetAddressWindow(
    uint16_t x0,
    uint16_t y0,
    uint16_t x1,
    uint16_t y1);
void ST7796_FlushArea(
    uint16_t x,
    uint16_t y,
    uint16_t w,
    uint16_t h,
    uint16_t* buffer);
void ST7796_WaitDMA(void);

void ST7796_TestPatternBlock(uint16_t *buffer,
                            uint16_t xOffset,
                            uint16_t yOffset,
                            uint16_t w,
                            uint16_t h);
void ST7796_FillColor_DMA(uint16_t color);
void ST7796_DrawTestImage(void);
void st7796s_Test(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* INC_ST7796S_H_ */
