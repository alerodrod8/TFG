/*
 *  WS2812B custom library
 *
 *  This library contains the inicialization and controller functions to drive the data to Adafruit NeoPixels LED's
 */

#include "ws2812b.h"
#include <string.h>

RGB_components grbStruct = {0U, 0U, 0U};
__attribute__((aligned(32))) uint16_t pwmdata[DATA_LENGTH];
__attribute__((aligned(32))) uint16_t pwmdataEmpty[DATA_LENGTH] = {0U};

static volatile uint8_t dmaDoneCh3 = 0U;
static volatile uint8_t dmaDoneCh4 = 0U;
static volatile bool gradientAnimationEnabled = false;
static TaskHandle_t dmaWaitingTask = NULL;

void LedTestTask(void *argument)
{
    for (;;)
    {
    	ws2812b_setBlink("#FF0000");
        vTaskDelay(500);

        ws2812b_setBlink("#00FF00");
        vTaskDelay(500);

        ws2812b_setBlink("#0000FF");
        vTaskDelay(500);

        ws2812b_reset();
        ws2812b_sendAll(pwmdata);
        vTaskDelay(1000);
    }
}

static void cleanDCacheForBuffer(const void *address, uint32_t length)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) == 0U || address == NULL || length == 0U) {
        return;
    }

    uintptr_t start = ((uintptr_t)address) & ~(uintptr_t)31U;
    uintptr_t end = ((uintptr_t)address + length + 31U) & ~(uintptr_t)31U;
    SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}

static uint8_t scaleComponent(uint8_t value, uint8_t level)
{
    return (uint8_t)(((uint16_t)value * (uint16_t)level) / 255U);
}

void ws2812b_enableGradientAnimation(bool enable)
{
    gradientAnimationEnabled = enable;
}

bool ws2812b_isGradientAnimationEnabled(void)
{
    return gradientAnimationEnabled;
}

void ws2812b_setGradientAnimation(const char *color)
{
    RGB_components target;
    const uint8_t steps = 48U;

    if (!ws2812b_colorConvert(color, &target)) {
        return;
    }

    while (gradientAnimationEnabled) {
        for (uint8_t i = 0U; i <= steps && gradientAnimationEnabled; ++i) {
            uint8_t level = (uint8_t)(((uint16_t)i * 255U) / steps);
            RGB_components c = {
                scaleComponent(target.green, level),
                scaleComponent(target.red, level),
                scaleComponent(target.blue, level)
            };
            ws2812b_pwmData(c);
            if (ws2812b_sendAll(pwmdata) != HAL_OK) {
                gradientAnimationEnabled = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(18U));
        }

        for (int i = (int)steps; i >= 0 && gradientAnimationEnabled; --i) {
            uint8_t level = (uint8_t)(((uint16_t)i * 255U) / steps);
            RGB_components c = {
                scaleComponent(target.green, level),
                scaleComponent(target.red, level),
                scaleComponent(target.blue, level)
            };
            ws2812b_pwmData(c);
            if (ws2812b_sendAll(pwmdata) != HAL_OK) {
                gradientAnimationEnabled = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(18U));
        }
    }

    ws2812b_reset();
}

void ws2812b_setBlink(const char *color)
{
    for (uint8_t i = 0U; i < 5U; ++i) {
    	ws2812b_setColor(color);
    	ws2812b_sendAll(pwmdata);
    	vTaskDelay(200);
        ws2812b_reset();
        ws2812b_sendAll(pwmdata);
        vTaskDelay(200);
    }
}

void ws2812b_setColor(const char *color)
{
    RGB_components parsed;

    if (!ws2812b_colorConvert(color, &parsed)) {
        memset(&grbStruct, 0, sizeof(grbStruct));
        ws2812b_pwmData(grbStruct);
        return;
    }

    grbStruct = parsed;
    ws2812b_pwmData(grbStruct);
}

void ws2812b_reset(void)
{
    memset(&grbStruct, 0, sizeof(grbStruct));
    ws2812b_pwmData(grbStruct);
}

HAL_StatusTypeDef ws2812b_sendData(TIM_HandleTypeDef *htim, uint32_t channel, uint16_t *data)
{
    if (htim == NULL || data == NULL) {
        return HAL_ERROR;
    }

    return HAL_TIM_PWM_Start_DMA(htim,
                                 channel,
                                 (const uint32_t *)data,
                                 (uint16_t)DATA_LENGTH);
}

HAL_StatusTypeDef ws2812b_sendAll(uint16_t *data)
{
    HAL_StatusTypeDef st3;
    HAL_StatusTypeDef st4;

    if (data == NULL) {
        return HAL_ERROR;
    }

    cleanDCacheForBuffer(data, sizeof(uint16_t) * DATA_LENGTH);

    dmaDoneCh3 = 0U;
    dmaDoneCh4 = 0U;
    dmaWaitingTask = xTaskGetCurrentTaskHandle();

    /* Start both LED strings from the same WS2812 frame at effectively the same time. */
    __HAL_TIM_SET_COUNTER(&htim3, 0U);
    st3 = ws2812b_sendData(&htim3, TIM_CHANNEL_3, data);
    st4 = ws2812b_sendData(&htim3, TIM_CHANNEL_4, data);

    if (st3 != HAL_OK || st4 != HAL_OK) {
        if (st3 == HAL_OK) {
            (void)HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_3);
        }
        if (st4 == HAL_OK) {
            (void)HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_4);
        }
        dmaWaitingTask = NULL;
        return HAL_ERROR;
    }

    /* A complete WS2812 frame is < 0.2 ms; 10 ms is a generous fault timeout. */
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10U)) == 0U) {
        (void)HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_3);
        (void)HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_4);
        dmaWaitingTask = NULL;
        return HAL_TIMEOUT;
    }

    (void)HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_3);
    (void)HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_4);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0U);
    dmaWaitingTask = NULL;

    return (dmaDoneCh3 != 0U && dmaDoneCh4 != 0U) ? HAL_OK : HAL_ERROR;
}

bool ws2812b_colorConvert(const char *color, RGB_components *out)
{
    if (color == NULL || out == NULL || strlen(color) != 7U || color[0] != '#') {
        return false;
    }

    for (uint8_t i = 1U; i < 7U; ++i) {
        if (ws2812b_ASCIIToInt(color[i]) < 0) {
            return false;
        }
    }

    out->red = (uint8_t)(ws2812b_ASCIIToInt(color[1]) * 16 + ws2812b_ASCIIToInt(color[2]));
    out->green = (uint8_t)(ws2812b_ASCIIToInt(color[3]) * 16 + ws2812b_ASCIIToInt(color[4]));
    out->blue = (uint8_t)(ws2812b_ASCIIToInt(color[5]) * 16 + ws2812b_ASCIIToInt(color[6]));
    return true;
}

void ws2812b_pwmData(RGB_components color)
{
    uint16_t index = 0U;
    uint32_t grb = ((uint32_t)color.green << 16) |
                   ((uint32_t)color.red << 8) |
                   (uint32_t)color.blue;

    for (uint8_t led = 0U; led < NUM_LEDS; ++led) {
        for (int bit = 23; bit >= 0; --bit) {
            pwmdata[index++] = ((grb & (1UL << bit)) != 0U) ?
                               WS2812_T1H_TICKS : WS2812_T0H_TICKS;
        }
    }

    while (index < DATA_LENGTH) {
        pwmdata[index++] = 0U;
    }
}

int ws2812b_ASCIIToInt(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void ws2812b_gradientTask(void *argument)
{
    const char *color = (const char *)argument;
    ws2812b_setGradientAnimation((color != NULL) ? color : CORP_CBLUE);
    vTaskDelete(NULL);
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (htim == NULL || htim->Instance != TIM3) {
        return;
    }

    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3) {
        dmaDoneCh3 = 1U;
    } else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_4) {
        dmaDoneCh4 = 1U;
    }

    if (dmaDoneCh3 != 0U && dmaDoneCh4 != 0U && dmaWaitingTask != NULL) {
        vTaskNotifyGiveFromISR(dmaWaitingTask, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
