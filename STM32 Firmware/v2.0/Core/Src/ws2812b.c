/*
 *  WS2812B custom library
 *
 *  This library contains the inicialization and controller functions to drive the data to Adafruit NeoPixels LED's
 */

#include "ws2812b.h"

RGB_components grbStruct;
RGB_components navy = {50,35,83};

uint16_t pwmdata[DATA_LENGTH];
uint16_t pwmdataEmpty[DATA_LENGTH] = {0};
uint16_t cnt = 0;

uint8_t datasent_tim3ch1 = 0;
uint8_t datasent_tim3ch2 = 0;
uint8_t datasent_tim3ch3 = 0;
uint8_t datasent_tim3ch4 = 0;
uint8_t datasent_tim4ch1 = 0;
uint8_t datasent_tim4ch2 = 0;

void ws2812b_ledTest(){
	while(1)
	{
		ws2812b_pwmData((RGB_components){255, 0, 0});
		ws2812b_sendAllPwmdata();
		vTaskDelay(1000);
	}
}

void ws2812b_setGradientAnimation(char* color){

	ws2812b_colorConvert(color);
	RGB_components cyan = grbStruct;

	float stepG = (navy.green-cyan.green)/50.;
	float stepR = (navy.red-cyan.red)/50.;
	float stepB = (navy.blue-cyan.blue)/50.;

	float rampG = cyan.green;
	float rampR = cyan.red;
	float rampB = cyan.blue;

	for (int i=0;i<50;i++){
		grbStruct.green = (uint8_t)rampG;
		grbStruct.red = (uint8_t)rampR;
		grbStruct.blue = (uint8_t)rampB;
		ws2812b_pwmData(grbStruct);
		ws2812b_sendAllPwmdata();
		rampG += stepG;
		rampR += stepR;
		rampB += stepB;
	}

	stepG = (cyan.green-navy.green)/50.;
	stepR = (cyan.red-navy.red)/50.;
	stepB = (cyan.blue-navy.blue)/50.;

	rampG = navy.green;
	rampR = navy.red;
	rampB = navy.blue;

	for (int i=0;i<50;i++){
		grbStruct.green = (uint8_t) rampG;
		grbStruct.red = (uint8_t) rampR;
		grbStruct.blue = (uint8_t) rampB;
		ws2812b_pwmData(grbStruct);
		ws2812b_sendAllPwmdata();
		rampG += stepG;
		rampR += stepR;
		rampB += stepB;
	}
}

void ws2812b_setBlink(char* color){
	ws2812b_colorConvert(color);
	ws2812b_pwmData(grbStruct);
	//ws2812b_sendData(&htim3, TIM_CHANNEL_1, pwmdata);
	ws2812b_sendData(&htim3, TIM_CHANNEL_2, pwmdata);
	//ws2812b_sendData(&htim3, TIM_CHANNEL_3, pwmdata);
	//ws2812b_sendData(&htim3, TIM_CHANNEL_4, pwmdataEmpty);
	//ws2812b_sendData(&htim4, TIM_CHANNEL_1, pwmdataEmpty);
	//ws2812b_sendData(&htim4, TIM_CHANNEL_2, pwmdataEmpty);
	vTaskDelay(1000);
	//ws2812b_sendData(&htim3, TIM_CHANNEL_1, pwmdataEmpty);
	ws2812b_sendData(&htim3, TIM_CHANNEL_2, pwmdataEmpty);
	//ws2812b_sendData(&htim3, TIM_CHANNEL_3, pwmdataEmpty);
	//ws2812b_sendData(&htim3, TIM_CHANNEL_4, pwmdata);
	//ws2812b_sendData(&htim4, TIM_CHANNEL_1, pwmdata);
	//ws2812b_sendData(&htim4, TIM_CHANNEL_2, pwmdata);
}

void ws2812b_setColor(char* color){
	ws2812b_colorConvert(color);
	ws2812b_pwmData(grbStruct);
	ws2812b_sendAllPwmdata();
}

void ws2812b_reset(){
	//ws2812b_sendData(&htim3, TIM_CHANNEL_1, pwmdataEmpty);
	ws2812b_sendData(&htim3, TIM_CHANNEL_2, pwmdataEmpty);
	//ws2812b_sendData(&htim3, TIM_CHANNEL_3, pwmdataEmpty);
	//ws2812b_sendData(&htim3, TIM_CHANNEL_4, pwmdataEmpty);
	//ws2812b_sendData(&htim4, TIM_CHANNEL_1, pwmdataEmpty);
	//ws2812b_sendData(&htim4, TIM_CHANNEL_2, pwmdataEmpty);
}

//void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim){
//	uint32_t channel = HAL_TIM_GetActiveChannel(htim);
//	HAL_TIM_PWM_Stop_DMA(htim, htim->Channel);
//	if(htim->Instance == TIM3){
//		switch (channel){
//		case HAL_TIM_ACTIVE_CHANNEL_2: datasent_tim3ch2 = 1; break;
//		default: break;
//		}
//	}
//}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance == TIM3)
    {
        datasent_tim3ch2 = 1;
        uint32_t val = TIM3->CCR2;
        (void)val;
    }
}

void ws2812b_sendData(TIM_HandleTypeDef *htim, uint32_t channel, uint16_t *data){
	HAL_TIM_PWM_Start(htim, channel);
	__disable_irq();
	for(uint16_t i = 0; i<DATA_LENGTH; i++){
		__HAL_TIM_SET_COUNTER(htim, 0);
		__HAL_TIM_CLEAR_FLAG(htim, TIM_FLAG_UPDATE);
		__HAL_TIM_SET_COMPARE(htim, channel, data[i]);
		while(__HAL_TIM_GET_FLAG(htim, TIM_FLAG_UPDATE) == RESET);
	}
	__HAL_TIM_SET_COMPARE(htim, channel, 0);
	__enable_irq();
	HAL_TIM_PWM_Stop(htim, channel);
	vTaskDelay(1);
}

uint8_t getStatusFlag(TIM_HandleTypeDef *htim){
	uint32_t channel = HAL_TIM_GetActiveChannel(htim);
	if(htim->Instance == TIM3){
			switch (channel){
			case HAL_TIM_ACTIVE_CHANNEL_2: return datasent_tim3ch2;
			default: break;
			}
	}
	return 0;
}

void ws2812b_colorConvert(char* color){
	// #dfad0e
	char string[7];
	strcpy(string, color+1);
	grbStruct.red = ws2812b_ASCIIToInt(string[0])*16 + ws2812b_ASCIIToInt(string[1]);
	grbStruct.green = ws2812b_ASCIIToInt(string[2])*16 + ws2812b_ASCIIToInt(string[3]);
	grbStruct.blue = ws2812b_ASCIIToInt(string[4])*16 + ws2812b_ASCIIToInt(string[5]);
}

void ws2812b_pwmData(RGB_components s){
	cnt = 0;
	uint32_t colorData = (s.green<<16) | (s.red<<8) | (s.blue);
	for(int i=23; i>=0;--i){
		if (colorData&(1<<i)){
			pwmdata[cnt] = 48;	// 2/3 de 67
		}else{
			pwmdata[cnt] = 20;	// 1/3 de 67
		}
		cnt++;
	}
	for(int i=0; i<50;i++){
		pwmdata[cnt] = 0;
		cnt++;
	}
}

int ws2812b_ASCIIToInt(char c){
	if(c >= '0' && c <= '9') return c-'0';
	if(c >= 'a' && c <= 'f') return c-'a'+10;
	if(c >= 'A' && c <= 'F') return c-'A'+10;
	return 0;
}

void ws2812b_sendAllPwmdata(){
	ws2812b_sendData(&htim3, TIM_CHANNEL_1, pwmdata);
	ws2812b_sendData(&htim3, TIM_CHANNEL_2, pwmdata);
	ws2812b_sendData(&htim3, TIM_CHANNEL_3, pwmdata);
	ws2812b_sendData(&htim3, TIM_CHANNEL_4, pwmdata);
	ws2812b_sendData(&htim4, TIM_CHANNEL_1, pwmdata);
	ws2812b_sendData(&htim4, TIM_CHANNEL_2, pwmdata);
}
