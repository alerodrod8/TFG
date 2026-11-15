#ifndef INC_WS2812B_H_
#define INC_WS2812B_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

extern TIM_HandleTypeDef htim3;

#define PURE_GREEN "#00FF00"
#define PURE_RED   "#FF0000"
#define PURE_BLUE  "#0000FF"

#define CORP_YELLOW "#dfad0e"
#define CORP_WHITE  "#ebebeb"
#define CORP_CBLUE  "#6099c4"

#define NUM_LEDS              3U
#define WS2812_BITS_PER_LED   24U
#define WS2812_RESET_SLOTS    50U
#define DATA_LENGTH           (WS2812_BITS_PER_LED * NUM_LEDS + WS2812_RESET_SLOTS)

/* TIM3 is clocked at 280 MHz. ARR=349 gives 800 kHz / 1.25 us bit time. */
#define WS2812_TIM_PERIOD_TICKS 350U
#define WS2812_T0H_TICKS         98U   /* ~0.35 us */
#define WS2812_T1H_TICKS        196U   /* ~0.70 us */

typedef struct {
    uint8_t green;
    uint8_t red;
    uint8_t blue;
} RGB_components;

extern RGB_components grbStruct;
extern uint16_t pwmdata[DATA_LENGTH];
extern uint16_t pwmdataEmpty[DATA_LENGTH];

void LedTestTask(void *argument);

void ws2812b_setGradientAnimation(const char *color);
void ws2812b_setBlink(const char *color);
void ws2812b_setColor(const char *color);
void ws2812b_reset(void);

HAL_StatusTypeDef ws2812b_sendData(TIM_HandleTypeDef *htim, uint32_t channel, uint16_t *data);
HAL_StatusTypeDef ws2812b_sendAll(uint16_t *data);
void ws2812b_pwmData(RGB_components color);
bool ws2812b_colorConvert(const char *color, RGB_components *out);
int ws2812b_ASCIIToInt(char c);

void ws2812b_enableGradientAnimation(bool enable);
bool ws2812b_isGradientAnimationEnabled(void);
void ws2812b_gradientTask(void *argument);

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim);

#ifdef __cplusplus
}
#endif

#endif /* INC_WS2812B_H_ */
