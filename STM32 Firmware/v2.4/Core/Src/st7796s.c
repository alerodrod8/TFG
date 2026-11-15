/*
 * st7796s.c
 *
 *  Created on: May 13, 2026
 *      Author: rodri
 *
 *  Optimized DMA version
 *
 *  - Keeps the ST7796S initialization sequence already validated on the panel.
 *  - Uses large DMA blocks instead of one DMA transfer per scanline.
 *  - Maximum DMA block = 64 full 480-pixel lines = 61440 bytes.
 *  - Uses two ping-pong buffers so the CPU packs the next RGB565 block while
 *    SPI4 DMA is transmitting the current block.
 *  - Keeps CS asserted for the complete RAMWR transaction.
 *
 *  SPI4 should be configured externally (CubeMX/main.c) with prescaler 8,
 *  8-bit data, MSB first, CPOL low, CPHA 1 edge, software NSS.
 */

#include "st7796s.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"


/* -------------------------------------------------------------------------- */
/*                               GPIO helpers                                 */
/* -------------------------------------------------------------------------- */

#define LCD_CS_LOW()       HAL_GPIO_WritePin(DISPLAY_NSS_GPIO_Port, DISPLAY_NSS_Pin, GPIO_PIN_RESET)
#define LCD_CS_HIGH()      HAL_GPIO_WritePin(DISPLAY_NSS_GPIO_Port, DISPLAY_NSS_Pin, GPIO_PIN_SET)

#define LCD_DC_COMMAND()   HAL_GPIO_WritePin(DISPLAY_DC_RS_GPIO_Port, DISPLAY_DC_RS_Pin, GPIO_PIN_RESET)
#define LCD_DC_DATA()      HAL_GPIO_WritePin(DISPLAY_DC_RS_GPIO_Port, DISPLAY_DC_RS_Pin, GPIO_PIN_SET)

#define LCD_RESET_LOW()    HAL_GPIO_WritePin(DISPLAY_RESET_GPIO_Port, DISPLAY_RESET_Pin, GPIO_PIN_RESET)
#define LCD_RESET_HIGH()   HAL_GPIO_WritePin(DISPLAY_RESET_GPIO_Port, DISPLAY_RESET_Pin, GPIO_PIN_SET)


/* -------------------------------------------------------------------------- */
/*                              DMA configuration                             */
/* -------------------------------------------------------------------------- */

/*
 * HAL_SPI_Transmit_DMA() receives Size as uint16_t.
 *
 * 480 px * 64 lines * 2 bytes = 61440 bytes
 *
 * This stays below 65535 and reduces a full-screen refresh from
 * 320 DMA launches to only 5 DMA launches.
 */
#define ST7796_DMA_LINES          64U
#define ST7796_DMA_BLOCK_BYTES    (ST7796_WIDTH * ST7796_DMA_LINES * 2U)

#define ST7796_DMA_TIMEOUT_MS     200U


volatile uint8_t lcd_dma_transfer_complete = 1U;

static volatile bool lcdInitialized = false;
static volatile HAL_StatusTypeDef lcdDmaStatus = HAL_OK;
static TaskHandle_t lcdDmaWaitingTask = NULL;


/*
 * Ping-pong DMA buffers.
 *
 * While DMA sends one buffer, the CPU converts the next RGB565 block into
 * MSB-first SPI bytes in the other buffer.
 */
__attribute__((aligned(32)))
static uint8_t lcdTxBlock[2][ST7796_DMA_BLOCK_BYTES];


/* -------------------------------------------------------------------------- */
/*                          Low-level blocking access                         */
/* -------------------------------------------------------------------------- */

static HAL_StatusTypeDef transmitByte(uint8_t value)
{
    return HAL_SPI_Transmit(&hspi4,
                            &value,
                            1U,
                            HAL_MAX_DELAY);
}


HAL_StatusTypeDef ST7796_WriteCommand(uint8_t cmd)
{
    LCD_DC_COMMAND();

    return transmitByte(cmd);
}


HAL_StatusTypeDef ST7796_WriteData(uint8_t data)
{
    LCD_DC_DATA();

    return transmitByte(data);
}


/* -------------------------------------------------------------------------- */
/*                              Address window                                */
/* -------------------------------------------------------------------------- */

HAL_StatusTypeDef ST7796_SetAddressWindow(uint16_t x0,
                                         uint16_t y0,
                                         uint16_t x1,
                                         uint16_t y1)
{
    HAL_StatusTypeDef st = HAL_OK;

    LCD_CS_LOW();

    /* CASET - Column Address Set */
    if (ST7796_WriteCommand(0x2AU) != HAL_OK) st = HAL_ERROR;

    if (ST7796_WriteData((uint8_t)(x0 >> 8)) != HAL_OK) st = HAL_ERROR;
    if (ST7796_WriteData((uint8_t)(x0 & 0xFFU)) != HAL_OK) st = HAL_ERROR;
    if (ST7796_WriteData((uint8_t)(x1 >> 8)) != HAL_OK) st = HAL_ERROR;
    if (ST7796_WriteData((uint8_t)(x1 & 0xFFU)) != HAL_OK) st = HAL_ERROR;

    /* PASET - Page Address Set */
    if (ST7796_WriteCommand(0x2BU) != HAL_OK) st = HAL_ERROR;

    if (ST7796_WriteData((uint8_t)(y0 >> 8)) != HAL_OK) st = HAL_ERROR;
    if (ST7796_WriteData((uint8_t)(y0 & 0xFFU)) != HAL_OK) st = HAL_ERROR;
    if (ST7796_WriteData((uint8_t)(y1 >> 8)) != HAL_OK) st = HAL_ERROR;
    if (ST7796_WriteData((uint8_t)(y1 & 0xFFU)) != HAL_OK) st = HAL_ERROR;

    LCD_CS_HIGH();

    return st;
}


/* -------------------------------------------------------------------------- */
/*                              Initialization                                */
/* -------------------------------------------------------------------------- */

HAL_StatusTypeDef ST7796_Init(void)
{
    HAL_StatusTypeDef st = HAL_OK;

    lcdInitialized = false;
    lcd_dma_transfer_complete = 1U;

    LCD_CS_HIGH();
    LCD_DC_DATA();

    /* Backlight ON */
    HAL_GPIO_WritePin(DISPLAY_LED_GPIO_Port,
                      DISPLAY_LED_Pin,
                      GPIO_PIN_SET);

    /* Hardware reset */
    LCD_RESET_LOW();
    HAL_Delay(20U);

    LCD_RESET_HIGH();
    HAL_Delay(150U);

    /* Software reset */
    LCD_CS_LOW();

    if (ST7796_WriteCommand(0x01U) != HAL_OK)
    {
        st = HAL_ERROR;
    }

    LCD_CS_HIGH();
    HAL_Delay(150U);

    LCD_CS_LOW();

    /* Command Set Control */
    if (ST7796_WriteCommand(0xF0U) != HAL_OK ||
        ST7796_WriteData(0xC3U) != HAL_OK)
    {
        st = HAL_ERROR;
    }

    if (ST7796_WriteCommand(0xF0U) != HAL_OK ||
        ST7796_WriteData(0x96U) != HAL_OK)
    {
        st = HAL_ERROR;
    }

    /* Landscape + BGR */
    if (ST7796_WriteCommand(0x36U) != HAL_OK ||
        ST7796_WriteData(0x28U) != HAL_OK)
    {
        st = HAL_ERROR;
    }

    /* RGB565 */
    if (ST7796_WriteCommand(0x3AU) != HAL_OK ||
        ST7796_WriteData(0x55U) != HAL_OK)
    {
        st = HAL_ERROR;
    }

    /* Display Function Control */
    if (ST7796_WriteCommand(0xB6U) != HAL_OK ||
        ST7796_WriteData(0x80U) != HAL_OK ||
        ST7796_WriteData(0x02U) != HAL_OK ||
        ST7796_WriteData(0x3BU) != HAL_OK)
    {
        st = HAL_ERROR;
    }

    /* VCOM Control */
    if (ST7796_WriteCommand(0xC5U) != HAL_OK ||
        ST7796_WriteData(0x1FU) != HAL_OK)
    {
        st = HAL_ERROR;
    }

    /* Sleep Out */
    if (ST7796_WriteCommand(0x11U) != HAL_OK)
    {
        st = HAL_ERROR;
    }

    LCD_CS_HIGH();
    HAL_Delay(120U);

    LCD_CS_LOW();

    /* Display inversion ON - already validated on this panel */
    if (ST7796_WriteCommand(0x20U) != HAL_OK)
    {
        st = HAL_ERROR;
    }

    /* Display ON */
    if (ST7796_WriteCommand(0x29U) != HAL_OK)
    {
        st = HAL_ERROR;
    }

    LCD_CS_HIGH();
    HAL_Delay(50U);

    lcdInitialized = (st == HAL_OK);

    return st;
}


/* -------------------------------------------------------------------------- */
/*                                  Cache                                     */
/* -------------------------------------------------------------------------- */

static void cleanDCacheForBuffer(void *address,
                                 uint32_t length)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) == 0U ||
        address == NULL ||
        length == 0U)
    {
        return;
    }

    uintptr_t start =
        ((uintptr_t)address) & ~((uintptr_t)31U);

    uintptr_t end =
        (((uintptr_t)address + length + 31U) &
         ~((uintptr_t)31U));

    SCB_CleanDCache_by_Addr((uint32_t *)start,
                            (int32_t)(end - start));
}


/* -------------------------------------------------------------------------- */
/*                                  DMA                                       */
/* -------------------------------------------------------------------------- */

static HAL_StatusTypeDef startBlockDMA(uint8_t *data,
                                       uint16_t length)
{
    if (data == NULL || length == 0U)
    {
        return HAL_ERROR;
    }

    lcd_dma_transfer_complete = 0U;
    lcdDmaStatus = HAL_OK;

    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        /*
         * Clear any notification left by a DMA which completed before the
         * previous wait function got a chance to block.
         */
        (void)ulTaskNotifyTake(pdTRUE, 0U);

        lcdDmaWaitingTask = xTaskGetCurrentTaskHandle();
    }
    else
    {
        lcdDmaWaitingTask = NULL;
    }

    cleanDCacheForBuffer(data, length);

    if (HAL_SPI_Transmit_DMA(&hspi4,
                             data,
                             length) != HAL_OK)
    {
        lcd_dma_transfer_complete = 1U;
        lcdDmaWaitingTask = NULL;

        return HAL_ERROR;
    }

    return HAL_OK;
}


void ST7796_WaitDMA(void)
{
    if (lcd_dma_transfer_complete != 0U)
    {
        /*
         * DMA may have completed while the task was packing the next buffer.
         * Drain the already-posted task notification so it cannot be mistaken
         * for the following DMA transfer.
         */
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        {
            (void)ulTaskNotifyTake(pdTRUE, 0U);
        }

        lcdDmaWaitingTask = NULL;
        return;
    }

    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        TickType_t start = xTaskGetTickCount();

        while (lcd_dma_transfer_complete == 0U)
        {
            TickType_t elapsed = xTaskGetTickCount() - start;

            if (elapsed >= pdMS_TO_TICKS(ST7796_DMA_TIMEOUT_MS))
            {
                break;
            }

            TickType_t remaining =
                pdMS_TO_TICKS(ST7796_DMA_TIMEOUT_MS) - elapsed;

            (void)ulTaskNotifyTake(pdTRUE, remaining);
        }
    }
    else
    {
        uint32_t start = HAL_GetTick();

        while ((lcd_dma_transfer_complete == 0U) &&
               ((HAL_GetTick() - start) < ST7796_DMA_TIMEOUT_MS))
        {
        }
    }

    lcdDmaWaitingTask = NULL;
}


static HAL_StatusTypeDef waitBlockDMA(void)
{
    ST7796_WaitDMA();

    if (lcd_dma_transfer_complete == 0U)
    {
        (void)HAL_SPI_Abort(&hspi4);

        lcd_dma_transfer_complete = 1U;
        lcdDmaStatus = HAL_TIMEOUT;
        lcdDmaWaitingTask = NULL;

        return HAL_TIMEOUT;
    }

    return lcdDmaStatus;
}


/* -------------------------------------------------------------------------- */
/*                         RGB565 block conversion                            */
/* -------------------------------------------------------------------------- */

static uint16_t getRowsPerBlock(uint16_t width,
                                uint16_t rowsRemaining)
{
    uint32_t bytesPerRow;
    uint32_t rows;

    if (width == 0U)
    {
        return 0U;
    }

    bytesPerRow = (uint32_t)width * 2U;

    rows = ST7796_DMA_BLOCK_BYTES / bytesPerRow;

    if (rows == 0U)
    {
        rows = 1U;
    }

    if (rows > rowsRemaining)
    {
        rows = rowsRemaining;
    }

    return (uint16_t)rows;
}


static uint16_t packFramebufferRows(uint8_t *dst,
                                    const uint16_t *framebuffer,
                                    uint16_t x,
                                    uint16_t firstRow,
                                    uint16_t width,
                                    uint16_t rows)
{
    uint32_t dstIndex = 0U;

    for (uint16_t row = 0U; row < rows; ++row)
    {
        const uint16_t *src =
            &framebuffer[
                ((uint32_t)(firstRow + row) * ST7796_WIDTH) + x
            ];

        for (uint16_t col = 0U; col < width; ++col)
        {
            uint16_t pixel = src[col];

            dst[dstIndex++] = (uint8_t)(pixel >> 8);
            dst[dstIndex++] = (uint8_t)(pixel & 0xFFU);
        }
    }

    return (uint16_t)dstIndex;
}


/* -------------------------------------------------------------------------- */
/*                          Optimized framebuffer flush                       */
/* -------------------------------------------------------------------------- */

HAL_StatusTypeDef ST7796_FlushArea(uint16_t x,
                                  uint16_t y,
                                  uint16_t w,
                                  uint16_t h,
                                  const uint16_t *framebuffer)
{
    HAL_StatusTypeDef status = HAL_OK;

    if (!lcdInitialized ||
        framebuffer == NULL ||
        w == 0U ||
        h == 0U ||
        x >= ST7796_WIDTH ||
        y >= ST7796_HEIGHT)
    {
        return HAL_ERROR;
    }

    if ((uint32_t)x + w > ST7796_WIDTH)
    {
        w = (uint16_t)(ST7796_WIDTH - x);
    }

    if ((uint32_t)y + h > ST7796_HEIGHT)
    {
        h = (uint16_t)(ST7796_HEIGHT - y);
    }

    status = ST7796_SetAddressWindow(
        x,
        y,
        (uint16_t)(x + w - 1U),
        (uint16_t)(y + h - 1U)
    );

    if (status != HAL_OK)
    {
        return status;
    }

    LCD_CS_LOW();

    if (ST7796_WriteCommand(0x2CU) != HAL_OK)
    {
        LCD_CS_HIGH();
        return HAL_ERROR;
    }

    LCD_DC_DATA();

    /*
     * Reutilizamos lcdTxBlock[0] simplemente como buffer temporal.
     * Aquí NO se usa DMA.
     */
    for (uint16_t row = 0U; row < h; ++row)
    {
        const uint16_t *src =
            &framebuffer[
                ((uint32_t)(y + row) * ST7796_WIDTH) + x
            ];

        uint8_t *tx = lcdTxBlock[0];

        for (uint16_t col = 0U; col < w; ++col)
        {
            uint16_t pixel = src[col];

            tx[(uint32_t)col * 2U] =
                (uint8_t)(pixel >> 8);

            tx[(uint32_t)col * 2U + 1U] =
                (uint8_t)(pixel & 0xFFU);
        }

        status = HAL_SPI_Transmit(
            &hspi4,
            tx,
            (uint16_t)(w * 2U),
            HAL_MAX_DELAY
        );

        if (status != HAL_OK)
        {
            break;
        }
    }

    LCD_CS_HIGH();

    return status;
}


/* -------------------------------------------------------------------------- */
/*                        Fast solid-color DMA test                           */
/* -------------------------------------------------------------------------- */

static HAL_StatusTypeDef ST7796_FillColorDMA(uint16_t color)
{
    HAL_StatusTypeDef status;

    uint8_t high = (uint8_t)(color >> 8);
    uint8_t low  = (uint8_t)(color & 0xFFU);

    uint32_t totalBytes =
        (uint32_t)ST7796_WIDTH *
        (uint32_t)ST7796_HEIGHT *
        2U;

    uint32_t bytesRemaining = totalBytes;


    if (!lcdInitialized)
    {
        return HAL_ERROR;
    }


    /*
     * Fill a complete reusable DMA block with the requested solid color.
     */
    for (uint32_t i = 0U;
         i < ST7796_DMA_BLOCK_BYTES;
         i += 2U)
    {
        lcdTxBlock[0][i]     = high;
        lcdTxBlock[0][i + 1U] = low;
    }


    status = ST7796_SetAddressWindow(
        0U,
        0U,
        ST7796_WIDTH - 1U,
        ST7796_HEIGHT - 1U
    );

    if (status != HAL_OK)
    {
        return status;
    }


    LCD_CS_LOW();

    if (ST7796_WriteCommand(0x2CU) != HAL_OK)
    {
        LCD_CS_HIGH();
        return HAL_ERROR;
    }

    LCD_DC_DATA();


    while (bytesRemaining > 0U)
    {
        uint16_t bytesThisTransfer =
            (bytesRemaining > ST7796_DMA_BLOCK_BYTES)
                ? (uint16_t)ST7796_DMA_BLOCK_BYTES
                : (uint16_t)bytesRemaining;

        status =
            startBlockDMA(
                lcdTxBlock[0],
                bytesThisTransfer
            );

        if (status != HAL_OK)
        {
            LCD_CS_HIGH();
            return status;
        }

        status = waitBlockDMA();

        if (status != HAL_OK)
        {
            LCD_CS_HIGH();
            return status;
        }

        bytesRemaining -= bytesThisTransfer;
    }


    LCD_CS_HIGH();

    return HAL_OK;
}


void DisplayTestTask(void *argument)
{
    HAL_StatusTypeDef st;

    (void)argument;


    if (!ST7796_IsInitialized())
    {
        st = ST7796_Init();

        if (st != HAL_OK)
        {
            Error_Handler();
        }
    }


    for (;;)
    {
        st = ST7796_FillColorDMA(0xF800U);

        if (st != HAL_OK)
        {
            Error_Handler();
        }

        vTaskDelay(pdMS_TO_TICKS(2000U));


        st = ST7796_FillColorDMA(0x07E0U);

        if (st != HAL_OK)
        {
            Error_Handler();
        }

        vTaskDelay(pdMS_TO_TICKS(2000U));


        st = ST7796_FillColorDMA(0x001FU);

        if (st != HAL_OK)
        {
            Error_Handler();
        }

        vTaskDelay(pdMS_TO_TICKS(2000U));
    }
}


/* -------------------------------------------------------------------------- */
/*                                  Status                                    */
/* -------------------------------------------------------------------------- */

bool ST7796_IsInitialized(void)
{
    return lcdInitialized;
}


/* -------------------------------------------------------------------------- */
/*                              SPI callbacks                                 */
/* -------------------------------------------------------------------------- */

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (hspi == NULL ||
        hspi->Instance != SPI4)
    {
        return;
    }

    lcdDmaStatus = HAL_OK;
    lcd_dma_transfer_complete = 1U;

    if (lcdDmaWaitingTask != NULL)
    {
        vTaskNotifyGiveFromISR(
            lcdDmaWaitingTask,
            &xHigherPriorityTaskWoken
        );

        portYIELD_FROM_ISR(
            xHigherPriorityTaskWoken
        );
    }
}


void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (hspi == NULL ||
        hspi->Instance != SPI4)
    {
        return;
    }

    lcdDmaStatus = HAL_ERROR;
    lcd_dma_transfer_complete = 1U;

    if (lcdDmaWaitingTask != NULL)
    {
        vTaskNotifyGiveFromISR(
            lcdDmaWaitingTask,
            &xHigherPriorityTaskWoken
        );

        portYIELD_FROM_ISR(
            xHigherPriorityTaskWoken
        );
    }
}
