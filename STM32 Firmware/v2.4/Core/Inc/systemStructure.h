/*
 * systemStructure.h
 *
 *  Created on: Jul 12, 2026
 *      Author: rodri
 */

#ifndef INC_SYSTEMSTRUCTURE_H_
#define INC_SYSTEMSTRUCTURE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "utils.h"
#include "nfc.h"

/* Fill these in for the target installation, preferably through compiler -D defines. */
#ifndef SYSTEM_WIFI_SSID
#define SYSTEM_WIFI_SSID "*****"
#endif

#ifndef SYSTEM_WIFI_PASSWORD
#define SYSTEM_WIFI_PASSWORD "*****"
#endif

#ifndef SYSTEM_LOG_EMAIL
#define SYSTEM_LOG_EMAIL "*****"
#endif

#define SYSTEM_UART_TIMEOUT_MS     5000U
#define SYSTEM_GUI_NAME_SIZE       128U
#define SYSTEM_GUI_DATETIME_SIZE   32U
#define SYSTEM_GUI_ERROR_SIZE      8U

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} systemDateTime_type;

typedef enum {
    SYSTEM_GUI_IDLE = 0,
    SYSTEM_GUI_CHECKIN_OK,
    SYSTEM_GUI_ERROR
} systemGuiMessage_type;

typedef struct {
    bool bootComplete;
    bool wifiConnected;
    systemGuiMessage_type messageType;
    char welcomeName[SYSTEM_GUI_NAME_SIZE];
    char checkinDateTime[SYSTEM_GUI_DATETIME_SIZE];
    char errorCode[SYSTEM_GUI_ERROR_SIZE];
    uint32_t sequence;
} systemGuiState_type;

extern utilsStruct_type utilsStruct;

void TouchGFXTestTask(void *argument);
bool systemCreateTasks(void);
void systemBoot(void *argument);

bool systemIsBootComplete(void);
bool systemIsWifiConnected(void);
bool systemHasValidDateTime(void);
bool systemGetDateTime(systemDateTime_type *dateTime);
void systemGetGuiState(systemGuiState_type *state);

void systemFormatHHMMSS(const systemDateTime_type *dateTime, char *dst, size_t dstSize);
void systemFormatDisplayDateTime(const systemDateTime_type *dateTime, char *dst, size_t dstSize);

void systemTestSetWifiConnected(bool connected);
void systemTestShowCheckin(const char *name, const char *dateTime);
void systemTestShowError(const char *errorCode);
void systemTestClearGuiMessage(void);
void TouchGFXTestTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* INC_SYSTEMSTRUCTURE_H_ */

