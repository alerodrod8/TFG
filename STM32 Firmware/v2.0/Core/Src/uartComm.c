/*
 * c file for USART protocol communications
 */

#include "uartComm.h"

extern QueueHandle_t uartQueueTx;
extern QueueHandle_t uartQueueRx;

uint8_t rxData;
uint8_t txData;
char buffer[64];

volatile uint8_t uartTxBusy = 0;
volatile uint32_t uartRxCount = 0;
volatile uint32_t uartRxErrCount = 0;
volatile uint8_t uartLastByte = 0;
volatile uint32_t uartTxCount = 0;
volatile uint32_t uartTxStartCount = 0;
volatile uint32_t uartTxErrCount = 0;
volatile uint8_t uartLastTxByte = 0;
volatile HAL_StatusTypeDef uartLastTxStatus = HAL_OK;

void uartComm_receiveDataFromUSART(void* pArg){

	uint8_t i = 0;
    uint8_t byte;

	HAL_UART_Receive_IT(&huart1, &rxData, 1);
	while(1){
		if(xQueueReceive(uartQueueRx, &byte, portMAX_DELAY) == pdPASS){
			uartLastByte = byte;
			if(byte == '\r' || byte == '\n'){
				buffer[i] = '\0';
				if(i>0) uartComm_filterMessage(buffer);
				i = 0;
			} else {
				if(i<sizeof(buffer)-1){
					buffer[i++] = byte;
				} else {
					i = 0;
					memset(buffer, 0, sizeof(buffer));
				}
			}
		}
	}
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	uartRxCount++;
	xQueueSendFromISR(uartQueueRx, &rxData, &xHigherPriorityTaskWoken);
	if(HAL_UART_Receive_IT(&huart1, &rxData, 1) != HAL_OK) uartRxErrCount++;
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void uartComm_sendDataToUSART(char* data){
	while(*data){
		uint8_t byte = (uint8_t)*data++;
		xQueueSend(uartQueueTx, &byte, portMAX_DELAY);
	}
	taskENTER_CRITICAL();
	if(!uartTxBusy){
		if(xQueueReceive(uartQueueTx, &txData, 0) == pdPASS){
			uartTxBusy = 1;
			uartLastTxStatus = HAL_UART_Transmit_IT(&huart1, &txData, 1);
			if(uartLastTxStatus == HAL_OK) {
				uartTxStartCount++;
			}else{
				uartTxErrCount++;
				uartTxBusy = 0;
			}
		}
	}
	taskEXIT_CRITICAL();
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart){
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	uartTxCount++;
	uartLastTxByte = txData;
	if(xQueueReceiveFromISR(uartQueueTx, &txData, &xHigherPriorityTaskWoken) == pdPASS){
		uartLastTxStatus = HAL_UART_Transmit_IT(&huart1, &txData, 1);
		if(uartLastTxStatus == HAL_OK){
			uartTxStartCount++;
		}else{
			uartTxErrCount++;
			uartTxBusy = 0;
		}
	}else{
		uartTxBusy = 0;
	}
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void uartComm_filterMessage(char * data){
	char *tokensPtr = strtok(data, ",");
	if(tokensPtr == NULL) return;

	if (strcmp(tokensPtr, "SUCCESS") == 0){
		char *success = strtok(NULL, ",");
		char *thrd = strtok(NULL, ",");

		if(success){
			int status = atoi(success);
			if(thrd){
				//TODO
			}else{
				//TODO
			}
		}

	} else if (strcmp(tokensPtr, "ERROR") == 0){
		char *error = strtok(NULL, ",");
		if(error){
			int status = atoi(error);
			//TODO
		}
	} else if (strncmp(tokensPtr, "WL_", 3) == 0){
		//TODO
	}
}

void uartComm_formatData(char *array){
	//TODO
}

void uartComm_testSendRecData(void*){
	vTaskDelay(5000);
	uartComm_sendDataToUSART("SSID=*****,PWD=*****\n");
	while(1){
		vTaskDelay(5000);
	}
}
