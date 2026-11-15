/*
 * st7796s.c
 *
 *  Created on: May 13, 2026
 *      Author: rodri
 */

#include "st7796s.h"

extern SPI_HandleTypeDef hspi5;
volatile uint8_t lcd_dma_transfer_complete = 1;

static inline void LCD_CS_LOW(void)
{
    HAL_GPIO_WritePin(DISPLAY_NSS_GPIO_Port,
                      DISPLAY_NSS_Pin,
                      GPIO_PIN_RESET);
}

static inline void LCD_CS_HIGH(void)
{
    HAL_GPIO_WritePin(DISPLAY_NSS_GPIO_Port,
                      DISPLAY_NSS_Pin,
                      GPIO_PIN_SET);
}

static inline void LCD_DC_COMMAND(void)
{
    HAL_GPIO_WritePin(DISPLAY_DC_RS_GPIO_Port,
                      DISPLAY_DC_RS_Pin,
                      GPIO_PIN_RESET);
}

static inline void LCD_DC_DATA(void)
{
    HAL_GPIO_WritePin(DISPLAY_DC_RS_GPIO_Port,
                      DISPLAY_DC_RS_Pin,
                      GPIO_PIN_SET);
}

static inline void LCD_RESET_LOW(void)
{
    HAL_GPIO_WritePin(DISPLAY_RESET_GPIO_Port,
                      DISPLAY_RESET_Pin,
                      GPIO_PIN_RESET);
}

static inline void LCD_RESET_HIGH(void)
{
    HAL_GPIO_WritePin(DISPLAY_RESET_GPIO_Port,
                      DISPLAY_RESET_Pin,
                      GPIO_PIN_SET);
}

void ST7796_WriteCommand(uint8_t cmd)
{
    LCD_DC_COMMAND();
    HAL_SPI_Transmit(&hspi5, &cmd, 1, HAL_MAX_DELAY);
}

void ST7796_WriteData(uint8_t data)
{
    LCD_DC_DATA();
    HAL_SPI_Transmit(&hspi5, &data, 1, HAL_MAX_DELAY);
}

void ST7796_SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
	LCD_CS_LOW();
    ST7796_WriteCommand(0x2A);

    ST7796_WriteData(x0 >> 8);
    ST7796_WriteData(x0 & 0xFF);
    ST7796_WriteData(x1 >> 8);
    ST7796_WriteData(x1 & 0xFF);

    ST7796_WriteCommand(0x2B);

    ST7796_WriteData(y0 >> 8);
    ST7796_WriteData(y0 & 0xFF);
    ST7796_WriteData(y1 >> 8);
    ST7796_WriteData(y1 & 0xFF);

    ST7796_WriteCommand(0x2C);
    LCD_CS_HIGH();
}

void ST7796_Init(void)
{
	HAL_GPIO_WritePin(DISPLAY_LED_GPIO_Port, DISPLAY_LED_Pin, SET);

	LCD_RESET_LOW();
	HAL_Delay(50);

	LCD_RESET_HIGH();
	HAL_Delay(150);

	LCD_CS_LOW();

	ST7796_WriteCommand(0xF0);
	ST7796_WriteData(0xC3);

	ST7796_WriteCommand(0xF0);
	ST7796_WriteData(0x96);

	ST7796_WriteCommand(0x36);
	ST7796_WriteData(0x28);

	ST7796_WriteCommand(0x3A);
	ST7796_WriteData(0x55);

	ST7796_WriteCommand(0x11);
	HAL_Delay(120);

	ST7796_WriteCommand(0x29);
	HAL_Delay(20);

	ST7796_WriteCommand(0x20); //Color inversion off
	HAL_Delay(20);

	LCD_CS_HIGH();
	HAL_Delay(20);

}

void ST7796_WaitDMA(void)
{
    while (!lcd_dma_transfer_complete)
    {
    }
}

void ST7796_FlushArea(
    uint16_t x,
    uint16_t y,
    uint16_t w,
    uint16_t h,
    uint16_t* buffer)
{
    uint32_t size;

    size = w * h * 2;

    ST7796_WaitDMA();

    SCB_CleanDCache_by_Addr(
        (uint32_t*)buffer,
        size);

    ST7796_SetAddressWindow(
        x,
        y,
        x + w - 1,
        y + h - 1);

    LCD_DC_DATA();
    LCD_CS_LOW();

    lcd_dma_transfer_complete = 0;

    HAL_SPI_Transmit_DMA(
        &hspi5,
        (uint8_t*)buffer,
        size);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if(hspi->Instance == SPI5)
    {
        LCD_CS_HIGH();
        lcd_dma_transfer_complete = 1;
    }
}

__attribute__((section(".dmaBuffer") ,aligned(32)))
static uint8_t dmaBuffer[ST7796_WIDTH*DMA_LINES*2];

void ST7796_FillColor_DMA(uint16_t color)
{
    uint8_t high = color >> 8;
    uint8_t low = color & 0xFF;

    for(uint32_t i = 0; i < ST7796_WIDTH*DMA_LINES*2; i += 2)
    {
    	dmaBuffer[i]     = high;
    	dmaBuffer[i + 1] = low;
    }

    ST7796_SetAddressWindow(0, 0, ST7796_WIDTH - 1, ST7796_HEIGHT - 1);

    for(uint16_t y = 0; y < ST7796_HEIGHT; y += 1)
    {
        ST7796_WaitDMA();

        SCB_CleanDCache_by_Addr((uint32_t*)dmaBuffer,
                                ST7796_WIDTH*DMA_LINES*2);

        LCD_DC_DATA();
        LCD_CS_LOW();

        lcd_dma_transfer_complete = 0;

        HAL_SPI_Transmit_DMA(&hspi5,
        		dmaBuffer,
				ST7796_WIDTH*DMA_LINES*2);
    }

    ST7796_WaitDMA();
}

static uint8_t line[ST7796_WIDTH*2];

void ST7796_DrawTestImage(void)
{
	uint16_t white = rgb2bgr565(0xFFFF);
	    uint8_t wh = white >> 8;
	    uint8_t wl = white & 0xFF;

	    int scale = 20;
	    int char_w = 5 * scale;
	    int char_h = 5 * scale;
	    int total_w = 4 * char_w + 3 * scale;

	    int start_x = (ST7796_WIDTH - total_w) / 2;
	    int start_y = (ST7796_HEIGHT - char_h) / 2;

	    ST7796_SetAddressWindow(0, 0, ST7796_WIDTH - 1, ST7796_HEIGHT - 1);

	    LCD_DC_DATA();
	    LCD_CS_LOW();

	    for(int y = 0; y < ST7796_HEIGHT; y++)
	    {
	        memset(line, 0x00, sizeof(line)); // fondo negro

	        for(int c = 0; c < 4; c++)
	        {
	            for(int row = 0; row < 5; row++)
	            {
	                for(int col = 0; col < 5; col++)
	                {
	                    if(test_font[c][row] & (1 << (4 - col)))
	                    {
	                        int px0 = start_x + c * (char_w + scale) + col * scale;
	                        int py0 = start_y + row * scale;

	                        if(y >= py0 && y < py0 + scale)
	                        {
	                            for(int sx = 0; sx < scale; sx++)
	                            {
	                                int x = px0 + sx;

	                                if(x >= 0 && x < ST7796_WIDTH)
	                                {
	                                    line[x * 2]     = wh;
	                                    line[x * 2 + 1] = wl;
	                                }
	                            }
	                        }
	                    }
	                }
	            }
	        }

	        HAL_SPI_Transmit(&hspi5, line, sizeof(line), HAL_MAX_DELAY);
	    }

	    LCD_CS_HIGH();
}

////////////////////////////////////////
////////////////////////////////////////

void ST7796_TestPatternBlock(uint16_t *buffer,
                            uint16_t xOffset,
                            uint16_t yOffset,
                            uint16_t w,
                            uint16_t h)
{
    for (uint16_t y = 0; y < h; y++)
    {
        for (uint16_t x = 0; x < w; x++)
        {
            uint8_t r = (x + xOffset) % 256;
            uint8_t g = (y + yOffset) % 256;
            uint8_t b = (x + y) % 256;

            buffer[y * w + x] =
                ((r >> 3) << 11) |
                ((g >> 2) << 5)  |
                ((b >> 3));
        }
    }
}

//void st7796s_Test(void *argument)
//{
//	ST7796_Init();
//	ST7796_SetAddressWindow(0,0,ST7796_WIDTH-1,ST7796_HEIGHT-1);
//
//	LCD_DC_DATA();
//	LCD_CS_LOW();
//
//	uint16_t bgr = rgb2bgr565(0x07e0);
//	uint8_t color[2] = {bgr >> 8, bgr & 0xFF};
//	for (int i = 0; i < ST7796_WIDTH*ST7796_HEIGHT; i++)
//	{
//		HAL_SPI_Transmit(&hspi5, color, 2, HAL_MAX_DELAY);
//	}
//
//	LCD_CS_HIGH();
//
//	while(1){
//		vTaskDelay(1000);
//	}
//}

//void st7796s_Test(void *argument)
//{
//	ST7796_Init();
//	while(1) {
//		ST7796_FillColor_DMA(0x07E0); // verde
//		vTaskDelay(1000);
//
//		ST7796_FillColor_DMA(0x001F); // rojo
//		vTaskDelay(1000);
//
//		ST7796_FillColor_DMA(0xF800); // azul
//		vTaskDelay(1000);
//	    }
//}

void st7796s_Test(void *argument)
{
	ST7796_Init();
	ST7796_DrawTestImage();

	while(1) {
		vTaskDelay(1000);
	}
}
