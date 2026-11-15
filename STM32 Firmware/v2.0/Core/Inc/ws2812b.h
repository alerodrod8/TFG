/*
 * WS2812B custom library
 *
 *  This is the header file of the corresponding .c file
 */

#include <stm32f7xx.h>
#include <stm32f7xx_hal_tim.h>
#include <stdlib.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>
#include <math.h>

extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;

#define PURE_GREEN "#FF0000"
#define PURE_RED "#00FF00"
#define PURE_BLUE "#0000FF"

#define CORP_YELLOW "#dfad0e"
#define CORP_WHITE "#ebebeb"
#define CORP_CBLUE "#6099c4"
#define DATA_LENGTH 24+50

typedef struct{
	uint8_t green;
	uint8_t red;
	uint8_t blue;
}RGB_components;

extern RGB_components grbStruct;
extern RGB_components navy;

extern uint16_t pwmdata[DATA_LENGTH];
extern uint16_t pwmdataEmpty[DATA_LENGTH];
extern uint16_t cnt;

extern uint8_t datasent_tim3ch1;
extern uint8_t datasent_tim3ch2;
extern uint8_t datasent_tim3ch3;
extern uint8_t datasent_tim3ch4;
extern uint8_t datasent_tim4ch1;
extern uint8_t datasent_tim4ch2;

void ws2812b_ledTest();
void ws2812b_setGradientAnimation(char* color);
void ws2812b_setBlink(char* color);
void ws2812b_setColor(char* color);
void ws2812b_reset();
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim);
void ws2812b_sendData(TIM_HandleTypeDef* htim, uint32_t channel, uint16_t *data);
uint8_t getStatusFlag(TIM_HandleTypeDef* htim);
void ws2812b_colorConvert(char* color);
void ws2812b_pwmData(RGB_components s);
void ws2812b_sendAllPwmdata();
int ws2812b_ASCIIToInt(char c);
