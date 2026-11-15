/*
 * Header file for USART protocol communications
 */

#ifndef INC_UARTCOMM_H_
#define INC_UARTCOMM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define UARTCOMM_RX_BUFFER_SIZE       128U
#define UARTCOMM_RESPONSE_SIZE        64U
#define UARTCOMM_WIFI_STATUS_SIZE     24U
#define UARTCOMM_RX_QUEUE_LENGTH      256U
#define UARTCOMM_TX_QUEUE_LENGTH      1024U
#define UARTCOMM_MESSAGE_QUEUE_LENGTH 12U

typedef enum {
    CODE_SUCCESS = 0,
    CODE_ERROR
} errorCode_type;

typedef enum {
    UART_REQ_NONE = 0,
    UART_REQ_WIFI_CONFIG,
    UART_REQ_WIFI_STATUS,
    UART_REQ_CHECKIN,
    UART_REQ_DATETIME,
    UART_REQ_FILES
} uartRequest_type;

typedef enum {
    UART_MSG_NONE = 0,
    UART_MSG_RESPONSE,
    UART_MSG_DATETIME,
    UART_MSG_WIFI_STATUS,
    UART_MSG_INVALID
} uartMessage_type;

typedef struct {
    uartMessage_type messageType;
    uartRequest_type requestType;
    errorCode_type errorCode;
    char response[UARTCOMM_RESPONSE_SIZE];
    char wifiStatus[UARTCOMM_WIFI_STATUS_SIZE];
} uartComm_type;

extern UART_HandleTypeDef huart1;
extern uartComm_type uartCommStruct;

extern volatile uint32_t uartRxCount;
extern volatile uint32_t uartRxErrCount;
extern volatile uint32_t uartTxCount;
extern volatile uint32_t uartTxErrCount;

void UartTestTask(void *argument);
bool uartComm_init(void);
void uartComm_receiveDataFromUSART(void *pArg);
void uartComm_sendDataToUSART(const char *data);
bool uartComm_sendRequest(uartRequest_type request, const char *data);
bool uartComm_getMessage(uartComm_type *message, TickType_t timeout);
bool uartComm_isBusy(void);
uartRequest_type uartComm_getPendingRequest(void);
void uartComm_abortPendingRequest(void);
void uartComm_filterMessage(char *data);

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* INC_UARTCOMM_H_ */

