/*
 * c file for USART protocol communications
 */

#include "uartComm.h"
#include <ctype.h>
#include <string.h>

static QueueHandle_t uartQueueRx = NULL;
static QueueHandle_t uartQueueTx = NULL;
static QueueHandle_t uartMessageQueue = NULL;
static SemaphoreHandle_t uartTxMutex = NULL;

static uint8_t rxData;
static uint8_t txData;
static char buffer[UARTCOMM_RX_BUFFER_SIZE];

uartComm_type uartCommStruct;

static volatile uint8_t uartTxBusy = 0U;
static volatile uartRequest_type pendingRequest = UART_REQ_NONE;

volatile uint32_t uartRxCount = 0U;
volatile uint32_t uartRxErrCount = 0U;
volatile uint32_t uartTxCount = 0U;
volatile uint32_t uartTxErrCount = 0U;

void UartTestTask(void *argument)
{
    uartComm_type message;

    (void)argument;

    if (!uartComm_init())
    {
        Error_Handler();
    }

    xTaskCreate(uartComm_receiveDataFromUSART,
                "uartRx",
                512,
                NULL,
                3,
                NULL);

    vTaskDelay(1000);

    for (;;)
    {
        if (!uartComm_isBusy())
        {
            uartComm_sendRequest(UART_REQ_DATETIME,
                                 "SSID=*****,PWD=*****\r\n");
        }

        if (uartComm_getMessage(&message,
                                5000))
        {
            /*
             * BREAKPOINT AQUÍ
             */
            __NOP();
        }

        vTaskDelay(5000);
    }
}

static bool isDateTimePayload(const char *s)
{
    /* YYYYMMDD-HHMMSS[-weekday] */
    if (s == NULL || strlen(s) < 15U) {
        return false;
    }

    for (uint8_t i = 0U; i < 8U; ++i) {
        if (!isdigit((unsigned char)s[i])) {
            return false;
        }
    }
    if (s[8] != '-') {
        return false;
    }
    for (uint8_t i = 9U; i < 15U; ++i) {
        if (!isdigit((unsigned char)s[i])) {
            return false;
        }
    }
    return true;
}

static void kickTx(void)
{
    taskENTER_CRITICAL();
    if (uartTxBusy == 0U && uartQueueTx != NULL) {
        if (xQueueReceive(uartQueueTx, &txData, 0U) == pdPASS) {
            uartTxBusy = 1U;
            if (HAL_UART_Transmit_IT(&huart1, &txData, 1U) != HAL_OK) {
                uartTxErrCount++;
                uartTxBusy = 0U;
            }
        }
    }
    taskEXIT_CRITICAL();
}

bool uartComm_init(void)
{
    if (uartQueueRx != NULL) {
        return true;
    }

    uartQueueRx = xQueueCreate(UARTCOMM_RX_QUEUE_LENGTH, sizeof(uint8_t));
    uartQueueTx = xQueueCreate(UARTCOMM_TX_QUEUE_LENGTH, sizeof(uint8_t));
    uartMessageQueue = xQueueCreate(UARTCOMM_MESSAGE_QUEUE_LENGTH, sizeof(uartComm_type));
    uartTxMutex = xSemaphoreCreateMutex();

    return (uartQueueRx != NULL && uartQueueTx != NULL &&
            uartMessageQueue != NULL && uartTxMutex != NULL);
}

void uartComm_receiveDataFromUSART(void *pArg)
{
    (void)pArg;

    size_t i = 0U;
    uint8_t byte;

    memset(buffer, 0, sizeof(buffer));

    if (HAL_UART_Receive_IT(&huart1, &rxData, 1U) != HAL_OK)
    {
        uartRxErrCount++;
    }

    for (;;)
    {
        if (xQueueReceive(uartQueueRx,
                          &byte,
                          portMAX_DELAY) == pdPASS)
        {
            if ((byte == '\r') || (byte == '\n'))
            {
                if (i > 0U)
                {
                    buffer[i] = '\0';

                    uartComm_filterMessage(buffer);

                    i = 0U;
                    memset(buffer, 0, sizeof(buffer));
                }
            }
            else
            {
                if (i < (sizeof(buffer) - 1U))
                {
                    buffer[i++] = (char)byte;
                }
                else
                {
                    /* Frame too long: discard it. */
                    i = 0U;
                    memset(buffer, 0, sizeof(buffer));
                    uartRxErrCount++;
                }
            }
        }
    }
}

void uartComm_sendDataToUSART(const char *data)
{
    if (data == NULL || uartQueueTx == NULL || uartTxMutex == NULL) {
        return;
    }

    if (xSemaphoreTake(uartTxMutex, portMAX_DELAY) != pdPASS) {
        return;
    }

    while (*data != '\0') {
        uint8_t byte = (uint8_t)*data++;
        (void)xQueueSend(uartQueueTx, &byte, portMAX_DELAY);
    }

    /* Every protocol frame is terminated here. */
    {
        const uint8_t eol[2] = {'\r', '\n'};
        (void)xQueueSend(uartQueueTx, &eol[0], portMAX_DELAY);
        (void)xQueueSend(uartQueueTx, &eol[1], portMAX_DELAY);
    }

    kickTx();
    xSemaphoreGive(uartTxMutex);
}

bool uartComm_sendRequest(uartRequest_type request, const char *data)
{
    if (request == UART_REQ_NONE || data == NULL) {
        return false;
    }

    taskENTER_CRITICAL();
    if (pendingRequest != UART_REQ_NONE) {
        taskEXIT_CRITICAL();
        return false;
    }
    pendingRequest = request;
    taskEXIT_CRITICAL();

    uartComm_sendDataToUSART(data);
    return true;
}

bool uartComm_getMessage(uartComm_type *message, TickType_t timeout)
{
    if (message == NULL || uartMessageQueue == NULL) {
        return false;
    }
    return (xQueueReceive(uartMessageQueue, message, timeout) == pdPASS);
}

bool uartComm_isBusy(void)
{
    return pendingRequest != UART_REQ_NONE;
}

uartRequest_type uartComm_getPendingRequest(void)
{
    return pendingRequest;
}

void uartComm_abortPendingRequest(void)
{
    taskENTER_CRITICAL();
    pendingRequest = UART_REQ_NONE;
    taskEXIT_CRITICAL();
}

void uartComm_filterMessage(char *data)
{
    char *savePtr = NULL;
    char *token;
    char *payload;
    uartRequest_type requestSnapshot;

    if (data == NULL || *data == '\0') {
        return;
    }
    memset(&uartCommStruct, 0, sizeof(uartCommStruct));
    requestSnapshot = pendingRequest;
    uartCommStruct.requestType = requestSnapshot;
    token = strtok_r(data, ",", &savePtr);
    if (token == NULL) {
        return;
    }

    if (strcmp(token, "SUCCESS") == 0 || strcmp(token, "ERROR") == 0) {
        uartCommStruct.errorCode = (strcmp(token, "SUCCESS") == 0) ? CODE_SUCCESS : CODE_ERROR;
        payload = strtok_r(NULL, ",", &savePtr);
        if (payload != NULL) {
            strncpy(uartCommStruct.response, payload, sizeof(uartCommStruct.response) - 1U);
        }
        if (uartCommStruct.errorCode == CODE_SUCCESS && isDateTimePayload(payload)) {
            uartCommStruct.messageType = UART_MSG_DATETIME;
        } else {
            uartCommStruct.messageType = UART_MSG_RESPONSE;
        }
        taskENTER_CRITICAL();
        pendingRequest = UART_REQ_NONE;
        taskEXIT_CRITICAL();
    }
    else if (strncmp(token, "WL_", 3U) == 0) {
        uartCommStruct.messageType = UART_MSG_WIFI_STATUS;
        uartCommStruct.errorCode = CODE_SUCCESS;
        strncpy(uartCommStruct.wifiStatus, token, sizeof(uartCommStruct.wifiStatus) - 1U);
        strncpy(uartCommStruct.response, token, sizeof(uartCommStruct.response) - 1U);

        if (requestSnapshot == UART_REQ_WIFI_CONFIG || requestSnapshot == UART_REQ_WIFI_STATUS) {
            taskENTER_CRITICAL();
            pendingRequest = UART_REQ_NONE;
            taskEXIT_CRITICAL();
        }
    }
    else {
        uartCommStruct.messageType = UART_MSG_INVALID;
        uartCommStruct.errorCode = CODE_ERROR;
        strncpy(uartCommStruct.response, "MALFORMED", sizeof(uartCommStruct.response) - 1U);
    }
    if (uartMessageQueue != NULL) {
        (void)xQueueSend(uartMessageQueue, &uartCommStruct, 0U);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (huart == NULL || huart->Instance != USART1 || uartQueueRx == NULL) {
        return;
    }
    uartRxCount++;
    (void)xQueueSendFromISR(uartQueueRx, &rxData, &xHigherPriorityTaskWoken);
    if (HAL_UART_Receive_IT(&huart1, &rxData, 1U) != HAL_OK) {
        uartRxErrCount++;
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (huart == NULL || huart->Instance != USART1 || uartQueueTx == NULL) {
        return;
    }
    uartTxCount++;
    if (xQueueReceiveFromISR(uartQueueTx, &txData, &xHigherPriorityTaskWoken) == pdPASS) {
        if (HAL_UART_Transmit_IT(&huart1, &txData, 1U) != HAL_OK) {
            uartTxErrCount++;
            uartTxBusy = 0U;
        }
    } else {
        uartTxBusy = 0U;
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == NULL || huart->Instance != USART1) {
        return;
    }
    uartRxErrCount++;
    (void)HAL_UART_AbortReceive(huart);
    if (HAL_UART_Receive_IT(&huart1, &rxData, 1U) != HAL_OK) {
        uartRxErrCount++;
    }
}
