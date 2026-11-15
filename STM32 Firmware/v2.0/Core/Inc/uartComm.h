/*
 * Header file for USART protocol communications
 */

#include <stm32f7xx_hal.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <string.h>
#include <stdlib.h>

extern UART_HandleTypeDef huart1;
extern QueueHandle_t uartQueueTx;
extern QueueHandle_t uartQueueRx;

extern uint8_t rxData;
extern uint8_t txData;
extern char buffer[64];
extern volatile uint32_t uartRxCount;
extern volatile uint32_t uartRxErrCount;
extern volatile uint8_t uartLastByte;

void uartComm_receiveDataFromUSART(void* pArg);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void uartComm_sendDataToUSART(char* data);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void uartComm_filterMessage(char * data);
void uartComm_formatData(char *array);
void uartComm_testSendRecData(void*);
