/*
 * systemStructure.c
 *
 *  Created on: Jul 12, 2026
 *      Author: rodri
 */

#include "systemStructure.h"
#include <stdio.h>
#include <string.h>
#include "uartComm.h"
#include "ws2812b.h"
#include "st7796s.h"
#include "app_touchgfx.h"

#define SYS_EVT_BOOT_COMPLETE   (1U << 0)
#define SYS_EVT_WIFI_REPLY      (1U << 1)
#define SYS_EVT_DATETIME_VALID  (1U << 2)

#define TOUCHGFX_STACK_WORDS    2048U
#define UART_RX_STACK_WORDS      512U
#define UART_EVT_STACK_WORDS     768U
#define BOOT_STACK_WORDS        1024U
#define NFC_STACK_WORDS         1024U
#define MAINT_STACK_WORDS        768U
#define LED_STACK_WORDS          512U

static EventGroupHandle_t systemEvents = NULL;
static bool bootComplete = false;
static bool wifiConnected = false;
static bool dateTimeValid = false;
static bool nfcReady = false;

static systemDateTime_type baseDateTime;
static TickType_t baseDateTimeTick = 0U;
static systemGuiState_type guiState;

static checkInData_type pendingCheckIn;
static systemDateTime_type pendingCheckInTime;
static bool checkInPending = false;
static bool filesPending = false;

static uartRequest_type activeRequest = UART_REQ_NONE;
static TickType_t activeRequestTick = 0U;
static uint32_t lastFilesSentDayKey = 0U;
static TickType_t lastFilesAttemptTick = 0U;

static TaskHandle_t ledTaskHandle = NULL;

void TouchGFXTestTask(void *argument)
{
    (void)argument;

    /*
     * Esperar a que termine todo el proceso de inicialización.
     */
    while (!systemIsBootComplete())
    {
        vTaskDelay(pdMS_TO_TICKS(100U));
    }

    /*
     * Dar tiempo a TouchGFX para cambiar:
     *
     * BootScreen -> InitScreen
     */
    vTaskDelay(pdMS_TO_TICKS(1500U));

    /*
     * ============================================================
     * Estado inicial normal
     * ============================================================
     */

    systemTestClearGuiMessage();

    vTaskDelay(pdMS_TO_TICKS(2000U));

    /*
     * ============================================================
     * WiFi desconectado
     * ============================================================
     */

    systemTestSetWifiConnected(false);

    vTaskDelay(pdMS_TO_TICKS(3000U));

    /*
     * ============================================================
     * WiFi conectado
     * ============================================================
     */

    systemTestSetWifiConnected(true);

    vTaskDelay(pdMS_TO_TICKS(3000U));

    /*
     * ============================================================
     * Error
     * ============================================================
     */

    systemTestShowError("E-01");

    vTaskDelay(pdMS_TO_TICKS(5000U));

    /*
     * ============================================================
     * Limpiar error
     * ============================================================
     */

    systemTestClearGuiMessage();

    vTaskDelay(pdMS_TO_TICKS(3000U));

    /*
     * ============================================================
     * Segunda bienvenida
     * ============================================================
     */

    systemTestShowCheckin(
        "Usuario Prueba",
        "20/09/2026 - 22:31"
    );

    vTaskDelay(pdMS_TO_TICKS(5000U));

    /*
     * Volver al estado normal.
     */

    systemTestClearGuiMessage();

    /*
     * Test terminado.
     */
    vTaskDelete(NULL);
}

static uint8_t daysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
    uint8_t result;

    if (month < 1U || month > 12U) {
        return 31U;
    }

    result = days[month - 1U];
    if (month == 2U) {
        bool leap = ((year % 4U) == 0U && (year % 100U) != 0U) || ((year % 400U) == 0U);
        if (leap) {
            result = 29U;
        }
    }
    return result;
}

static void addDays(systemDateTime_type *dt, uint32_t days)
{
    while (days-- > 0U) {
        dt->day++;
        if (dt->day > daysInMonth(dt->year, dt->month)) {
            dt->day = 1U;
            dt->month++;
            if (dt->month > 12U) {
                dt->month = 1U;
                dt->year++;
            }
        }
    }
}

static bool parseDateTime(const char *payload, systemDateTime_type *dt)
{
    int year, month, day, hour, minute, second;

    if (payload == NULL || dt == NULL) {
        return false;
    }

    if (sscanf(payload, "%4d%2d%2d-%2d%2d%2d",
               &year, &month, &day, &hour, &minute, &second) != 6) {
        return false;
    }

    if (year < 2020 || year > 2199 || month < 1 || month > 12 ||
        day < 1 || day > daysInMonth((uint16_t)year, (uint8_t)month) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        return false;
    }

    dt->year = (uint16_t)year;
    dt->month = (uint8_t)month;
    dt->day = (uint8_t)day;
    dt->hour = (uint8_t)hour;
    dt->minute = (uint8_t)minute;
    dt->second = (uint8_t)second;
    return true;
}

static void setBaseDateTime(const systemDateTime_type *dt)
{
    taskENTER_CRITICAL();
    baseDateTime = *dt;
    baseDateTimeTick = xTaskGetTickCount();
    dateTimeValid = true;
    taskEXIT_CRITICAL();
}

bool systemGetDateTime(systemDateTime_type *dateTime)
{
    systemDateTime_type local;
    TickType_t baseTick;
    uint32_t elapsedSeconds;
    uint32_t secondsOfDay;
    uint32_t extraDays;

    if (dateTime == NULL || !dateTimeValid) {
        return false;
    }

    taskENTER_CRITICAL();
    local = baseDateTime;
    baseTick = baseDateTimeTick;
    taskEXIT_CRITICAL();

    elapsedSeconds = (uint32_t)((xTaskGetTickCount() - baseTick) / configTICK_RATE_HZ);
    secondsOfDay = (uint32_t)local.hour * 3600U +
                   (uint32_t)local.minute * 60U +
                   local.second + elapsedSeconds;

    extraDays = secondsOfDay / 86400U;
    secondsOfDay %= 86400U;

    local.hour = (uint8_t)(secondsOfDay / 3600U);
    secondsOfDay %= 3600U;
    local.minute = (uint8_t)(secondsOfDay / 60U);
    local.second = (uint8_t)(secondsOfDay % 60U);
    addDays(&local, extraDays);

    *dateTime = local;
    return true;
}

void systemFormatHHMMSS(const systemDateTime_type *dateTime, char *dst, size_t dstSize)
{
    if (dateTime == NULL || dst == NULL || dstSize == 0U) {
        return;
    }
    (void)snprintf(dst, dstSize, "%02u%02u%02u",
                   dateTime->hour, dateTime->minute, dateTime->second);
}

void systemFormatDisplayDateTime(const systemDateTime_type *dateTime, char *dst, size_t dstSize)
{
    if (dateTime == NULL || dst == NULL || dstSize == 0U) {
        return;
    }
    (void)snprintf(dst, dstSize, "%02u/%02u/%04u - %02u:%02u",
                   dateTime->day, dateTime->month, dateTime->year,
                   dateTime->hour, dateTime->minute);
}

static void addSystemLog(logCodes code)
{
    systemDateTime_type dt;
    char timeText[7] = "000000";

    if (systemGetDateTime(&dt)) {
        systemFormatHHMMSS(&dt, timeText, sizeof(timeText));
    }
    (void)utils_addLog(code, timeText);
}

static void setGuiWifi(bool connected)
{
    taskENTER_CRITICAL();
    guiState.wifiConnected = connected;
    guiState.sequence++;
    taskEXIT_CRITICAL();
}

static void setGuiError(logCodes code)
{
    taskENTER_CRITICAL();
    guiState.messageType = SYSTEM_GUI_ERROR;
    (void)snprintf(guiState.errorCode, sizeof(guiState.errorCode), "E-%02u", (unsigned int)code + 1U);
    guiState.sequence++;
    taskEXIT_CRITICAL();
}

static void appendName(char *dst, size_t dstSize, const char *value)
{
    size_t used;
    if (value == NULL || *value == '\0') {
        return;
    }

    used = strlen(dst);
    if (used != 0U && used + 1U < dstSize) {
        dst[used++] = ' ';
        dst[used] = '\0';
    }
    if (used < dstSize - 1U) {
        strncat(dst, value, dstSize - used - 1U);
    }
}

static void setGuiCheckInSuccess(const checkInData_type *data, const systemDateTime_type *dt)
{
    char name[SYSTEM_GUI_NAME_SIZE] = {0};
    char dateText[SYSTEM_GUI_DATETIME_SIZE] = {0};

    appendName(name, sizeof(name), data->username1);
    appendName(name, sizeof(name), data->username2);
    appendName(name, sizeof(name), data->surname1);
    appendName(name, sizeof(name), data->surname2);
    appendName(name, sizeof(name), data->surname3);
    systemFormatDisplayDateTime(dt, dateText, sizeof(dateText));

    taskENTER_CRITICAL();
    guiState.messageType = SYSTEM_GUI_CHECKIN_OK;
    strncpy(guiState.welcomeName, name, sizeof(guiState.welcomeName) - 1U);
    strncpy(guiState.checkinDateTime, dateText, sizeof(guiState.checkinDateTime) - 1U);
    guiState.errorCode[0] = '\0';
    guiState.sequence++;
    taskEXIT_CRITICAL();
}

void systemGetGuiState(systemGuiState_type *state)
{
    if (state == NULL) {
        return;
    }

    taskENTER_CRITICAL();
    *state = guiState;
    taskEXIT_CRITICAL();
}

bool systemIsBootComplete(void)
{
    return bootComplete;
}

bool systemIsWifiConnected(void)
{
    return wifiConnected;
}

bool systemHasValidDateTime(void)
{
    return dateTimeValid;
}

static bool systemSendRequest(uartRequest_type request, const char *message)
{
    bool sent = uartComm_sendRequest(request, message);
    if (sent) {
        taskENTER_CRITICAL();
        activeRequest = request;
        activeRequestTick = xTaskGetTickCount();
        taskEXIT_CRITICAL();
    }
    return sent;
}

static void clearActiveRequest(uartRequest_type request)
{
    taskENTER_CRITICAL();
    if (activeRequest == request) {
        activeRequest = UART_REQ_NONE;
    }
    taskEXIT_CRITICAL();
}

static bool requestHasTimedOut(uartRequest_type request, uint32_t timeoutMs)
{
    bool timedOut = false;
    taskENTER_CRITICAL();
    if (activeRequest == request) {
        timedOut = ((xTaskGetTickCount() - activeRequestTick) >= pdMS_TO_TICKS(timeoutMs));
    }
    taskEXIT_CRITICAL();
    return timedOut;
}

static bool buildCheckInMessage(const checkInData_type *data, char *dst, size_t dstSize)
{
    int n = snprintf(dst, dstSize,
        "STATE=%s,HQ=%s,DEPARTMENT=%s,USERNAME=%s,USERNAME2=%s,SURNAME1=%s,SURNAME2=%s,SURNAME3=%s,JOBPOS=%s,USERID=%s,ISADMIN=%s",
        data->state, data->hq, data->department, data->username1, data->username2,
        data->surname1, data->surname2, data->surname3, data->jobpos,
        data->userid, data->isadmin);

    return (n > 0 && (size_t)n < dstSize);
}

static bool wifiStatusMeansConnected(const char *status)
{
    return (status != NULL &&
            (strcmp(status, "WL_CONNECTED") == 0 || strcmp(status, "WL_GOT_IP") == 0));
}

static void systemUartTask(void *argument)
{
    uartComm_type message;
    (void)argument;

    for (;;) {
        if (!uartComm_getMessage(&message, portMAX_DELAY)) {
            continue;
        }

        if (message.messageType == UART_MSG_WIFI_STATUS) {
            bool wasConnected = wifiConnected;
            bool nowConnected = wifiStatusMeansConnected(message.wifiStatus);
            wifiConnected = nowConnected;
            setGuiWifi(nowConnected);
            clearActiveRequest(message.requestType);
            xEventGroupSetBits(systemEvents, SYS_EVT_WIFI_REPLY);

            if (bootComplete && wasConnected && !nowConnected) {
                addSystemLog(wifiConnectionLost);
            }
        }
        else if (message.messageType == UART_MSG_DATETIME && message.errorCode == CODE_SUCCESS) {
            systemDateTime_type dt;
            if (parseDateTime(message.response, &dt)) {
                setBaseDateTime(&dt);
                clearActiveRequest(message.requestType);
                xEventGroupSetBits(systemEvents, SYS_EVT_DATETIME_VALID);
            } else {
                addSystemLog(datetimeRequestFail);
            }
        }
        else if (message.messageType == UART_MSG_RESPONSE) {
            clearActiveRequest(message.requestType);

            if (message.requestType == UART_REQ_CHECKIN) {
                if (message.errorCode == CODE_SUCCESS && strcmp(message.response, "200") == 0) {
                    setGuiCheckInSuccess(&pendingCheckIn, &pendingCheckInTime);
                } else if (strcmp(message.response, "400") == 0) {
                    addSystemLog(checkinBadRequest);
                    setGuiError(checkinBadRequest);
                } else {
                    addSystemLog(checkinServerError);
                    setGuiError(checkinServerError);
                }
                checkInPending = false;
            }
            else if (message.requestType == UART_REQ_FILES) {
                if (message.errorCode == CODE_SUCCESS && strcmp(message.response, "200") == 0) {
                    systemDateTime_type now;
                    if (systemGetDateTime(&now)) {
                        lastFilesSentDayKey = (uint32_t)now.year * 10000U +
                                              (uint32_t)now.month * 100U + now.day;
                    }
                    utils_clearLogs();
                } else {
                    addSystemLog(filesSendFail);
                }
                filesPending = false;
            }
            else if (message.errorCode == CODE_ERROR) {
                addSystemLog(malformedUartResponse);
            }
        }
        else if (message.messageType == UART_MSG_INVALID) {
            addSystemLog(malformedUartResponse);
        }
    }
}

static void systemNfcTask(void *argument)
{
    (void)argument;

    xEventGroupWaitBits(systemEvents, SYS_EVT_BOOT_COMPLETE, pdFALSE, pdTRUE, portMAX_DELAY);

    if (!nfcReady) {
        vTaskDelete(NULL);
    }

    for (;;) {
        checkInData_type data;
        nfcResult_type result;

        if (checkInPending || uartComm_isBusy()) {
            vTaskDelay(pdMS_TO_TICKS(100U));
            continue;
        }

        result = nfc_pollCheckIn(&data, 250U);
        if (result == NFC_RESULT_OK) {
            char message[512];
            systemDateTime_type now = {0};

            if (!buildCheckInMessage(&data, message, sizeof(message))) {
                addSystemLog(nfcParseFail);
                setGuiError(nfcParseFail);
            } else if (systemSendRequest(UART_REQ_CHECKIN, message)) {
                pendingCheckIn = data;
                if (systemGetDateTime(&now)) {
                    pendingCheckInTime = now;
                } else {
                    memset(&pendingCheckInTime, 0, sizeof(pendingCheckInTime));
                }
                checkInPending = true;
            }

            /* Prevent the same card from being accepted repeatedly while it remains on the reader. */
            vTaskDelay(pdMS_TO_TICKS(1500U));
        }
        else if (result == NFC_RESULT_READ_ERROR) {
            addSystemLog(nfcReadFail);
            setGuiError(nfcReadFail);
            vTaskDelay(pdMS_TO_TICKS(500U));
        }
        else if (result == NFC_RESULT_DECRYPT_ERROR) {
            addSystemLog(nfcDecryptFail);
            setGuiError(nfcDecryptFail);
            vTaskDelay(pdMS_TO_TICKS(500U));
        }
        else if (result == NFC_RESULT_PARSE_ERROR) {
            addSystemLog(nfcParseFail);
            setGuiError(nfcParseFail);
            vTaskDelay(pdMS_TO_TICKS(500U));
        }
        else {
            vTaskDelay(pdMS_TO_TICKS(100U));
        }
    }
}

static void systemMaintenanceTask(void *argument)
{
    (void)argument;

    xEventGroupWaitBits(systemEvents, SYS_EVT_BOOT_COMPLETE, pdFALSE, pdTRUE, portMAX_DELAY);

    for (;;) {
        TickType_t nowTick = xTaskGetTickCount();
        systemDateTime_type now;

        if (checkInPending && requestHasTimedOut(UART_REQ_CHECKIN, SYSTEM_UART_TIMEOUT_MS)) {
            uartComm_abortPendingRequest();
            clearActiveRequest(UART_REQ_CHECKIN);
            checkInPending = false;
            addSystemLog(uartTimeout);
            setGuiError(uartTimeout);
        }

        if (filesPending && requestHasTimedOut(UART_REQ_FILES, SYSTEM_UART_TIMEOUT_MS)) {
            uartComm_abortPendingRequest();
            clearActiveRequest(UART_REQ_FILES);
            filesPending = false;
            addSystemLog(filesSendFail);
        }

        if (systemGetDateTime(&now)) {
            uint32_t dayKey = (uint32_t)now.year * 10000U + (uint32_t)now.month * 100U + now.day;

            if (now.hour >= utilsStruct.logHour && dayKey != lastFilesSentDayKey &&
                !filesPending && !uartComm_isBusy() &&
                (nowTick - lastFilesAttemptTick) >= pdMS_TO_TICKS(60000U)) {
                char filesMessage[UTILS_LOG_BUFFER_SIZE + UTILS_EMAIL_SIZE + 32U];
                lastFilesAttemptTick = nowTick;

                if (utils_buildFilesMessage(filesMessage, sizeof(filesMessage)) &&
                    systemSendRequest(UART_REQ_FILES, filesMessage)) {
                    filesPending = true;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}

void systemBoot(void *argument)
{
    bool nfcOk;
    char wifiMessage[160];
    EventBits_t bits;
    (void)argument;

    utils_init(SYSTEM_LOG_EMAIL, 20U);

    if (!ST7796_IsInitialized()) {
        addSystemLog(displayInitFail);
    }

    nfcOk = nfc_init();
    nfcReady = nfcOk;
    if (!nfcOk) {
        addSystemLog(nfcInitFail);
    }

    if (SYSTEM_WIFI_SSID[0] != '\0') {
        (void)snprintf(wifiMessage, sizeof(wifiMessage), "SSID=%s,PWD=%s",
                       SYSTEM_WIFI_SSID, SYSTEM_WIFI_PASSWORD);

        xEventGroupClearBits(systemEvents, SYS_EVT_WIFI_REPLY);
        if (systemSendRequest(UART_REQ_WIFI_CONFIG, wifiMessage)) {
            bits = xEventGroupWaitBits(systemEvents, SYS_EVT_WIFI_REPLY,
                                       pdTRUE, pdFALSE, pdMS_TO_TICKS(10000U));
            if ((bits & SYS_EVT_WIFI_REPLY) == 0U) {
                uartComm_abortPendingRequest();
                clearActiveRequest(UART_REQ_WIFI_CONFIG);
                wifiConnected = false;
                setGuiWifi(false);
                addSystemLog(wifiInitFail);
            }
        } else {
            addSystemLog(wifiInitFail);
        }
    } else {
        addSystemLog(wifiInitFail);
    }

    /* Get the reference date/time once. From this point the local clock is derived from RTOS ticks. */
    xEventGroupClearBits(systemEvents, SYS_EVT_DATETIME_VALID);
    if (!uartComm_isBusy() && systemSendRequest(UART_REQ_DATETIME, "DATETIME")) {
        bits = xEventGroupWaitBits(systemEvents, SYS_EVT_DATETIME_VALID,
                                   pdTRUE, pdFALSE, pdMS_TO_TICKS(SYSTEM_UART_TIMEOUT_MS));
        if ((bits & SYS_EVT_DATETIME_VALID) == 0U) {
            uartComm_abortPendingRequest();
            clearActiveRequest(UART_REQ_DATETIME);
            addSystemLog(datetimeRequestFail);
        }
    } else {
        addSystemLog(datetimeRequestFail);
    }

    ws2812b_enableGradientAnimation(false);

    taskENTER_CRITICAL();
    bootComplete = true;
    guiState.bootComplete = true;
    guiState.wifiConnected = wifiConnected;
    guiState.sequence++;
    taskEXIT_CRITICAL();

    xEventGroupSetBits(systemEvents, SYS_EVT_BOOT_COMPLETE);
    vTaskDelete(NULL);
}

bool systemCreateTasks(void)
{
    memset(&guiState, 0, sizeof(guiState));
    systemEvents = xEventGroupCreate();
    if (systemEvents == NULL || !uartComm_init()) {
        return false;
    }

    ws2812b_enableGradientAnimation(true);

    if (xTaskCreate(TouchGFX_Task, "TouchGFX", TOUCHGFX_STACK_WORDS, NULL, 3U, NULL) != pdPASS ||
		xTaskCreate(TouchGFXTestTask, "guiTest", 512U, NULL, 1U, NULL) != pdPASS ||
        xTaskCreate(uartComm_receiveDataFromUSART, "uartRx", UART_RX_STACK_WORDS, NULL, 4U, NULL) != pdPASS ||
        xTaskCreate(systemUartTask, "uartEvt", UART_EVT_STACK_WORDS, NULL, 3U, NULL) != pdPASS ||
        xTaskCreate(ws2812b_gradientTask, "ledBoot", LED_STACK_WORDS,
                    (void *)CORP_CBLUE, 1U, &ledTaskHandle) != pdPASS ||
        xTaskCreate(systemBoot, "sysBoot", BOOT_STACK_WORDS, NULL, 3U, NULL) != pdPASS ||
        xTaskCreate(systemNfcTask, "nfc", NFC_STACK_WORDS, NULL, 2U, NULL) != pdPASS ||
        xTaskCreate(systemMaintenanceTask, "maint", MAINT_STACK_WORDS, NULL, 2U, NULL) != pdPASS) {
        return false;
    }

    return true;
}

void systemTestSetWifiConnected(bool connected)
{
    taskENTER_CRITICAL();

    wifiConnected = connected;
    guiState.wifiConnected = connected;
    guiState.sequence++;

    taskEXIT_CRITICAL();
}


void systemTestShowCheckin(const char *name,
                           const char *dateTime)
{
    taskENTER_CRITICAL();

    guiState.messageType = SYSTEM_GUI_CHECKIN_OK;

    guiState.welcomeName[0] = '\0';
    guiState.checkinDateTime[0] = '\0';
    guiState.errorCode[0] = '\0';

    if (name != NULL)
    {
        strncpy(guiState.welcomeName,
                name,
                sizeof(guiState.welcomeName) - 1U);

        guiState.welcomeName[
            sizeof(guiState.welcomeName) - 1U
        ] = '\0';
    }

    if (dateTime != NULL)
    {
        strncpy(guiState.checkinDateTime,
                dateTime,
                sizeof(guiState.checkinDateTime) - 1U);

        guiState.checkinDateTime[
            sizeof(guiState.checkinDateTime) - 1U
        ] = '\0';
    }

    guiState.sequence++;

    taskEXIT_CRITICAL();
}


void systemTestShowError(const char *errorCode)
{
    taskENTER_CRITICAL();

    guiState.messageType = SYSTEM_GUI_ERROR;

    guiState.welcomeName[0] = '\0';
    guiState.checkinDateTime[0] = '\0';
    guiState.errorCode[0] = '\0';

    if (errorCode != NULL)
    {
        strncpy(guiState.errorCode,
                errorCode,
                sizeof(guiState.errorCode) - 1U);

        guiState.errorCode[
            sizeof(guiState.errorCode) - 1U
        ] = '\0';
    }

    guiState.sequence++;

    taskEXIT_CRITICAL();
}


void systemTestClearGuiMessage(void)
{
    taskENTER_CRITICAL();

    guiState.messageType = SYSTEM_GUI_IDLE;
    guiState.welcomeName[0] = '\0';
    guiState.checkinDateTime[0] = '\0';
    guiState.errorCode[0] = '\0';
    guiState.sequence++;

    taskEXIT_CRITICAL();
}
