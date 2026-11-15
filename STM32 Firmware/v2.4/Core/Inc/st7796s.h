/*
 * st7796s.h
 */

#ifndef INC_ST7796S_H_
#define INC_ST7796S_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

#define ST7796_WIDTH       480U
#define ST7796_HEIGHT      320U
#define ST7796_LINE_BYTES  (ST7796_WIDTH * 2U)

extern SPI_HandleTypeDef hspi4;

extern volatile uint8_t lcd_dma_transfer_complete;

void DisplayTestTask(void *argument);

HAL_StatusTypeDef ST7796_Init(void);

HAL_StatusTypeDef ST7796_WriteCommand(uint8_t cmd);

HAL_StatusTypeDef ST7796_WriteData(uint8_t data);

HAL_StatusTypeDef ST7796_SetAddressWindow(
    uint16_t x0,
    uint16_t y0,
    uint16_t x1,
    uint16_t y1
);

HAL_StatusTypeDef ST7796_FlushArea(
    uint16_t x,
    uint16_t y,
    uint16_t w,
    uint16_t h,
    const uint16_t *framebuffer
);

void ST7796_WaitDMA(void);

bool ST7796_IsInitialized(void);

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi);

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi);

#ifdef __cplusplus
}
#endif

#endif /* INC_ST7796S_H_ */
