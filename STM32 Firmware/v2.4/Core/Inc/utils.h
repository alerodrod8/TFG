/*
 * utils.h
 *
 *  Created on: Apr 26, 2026
 *      Author: rodri
 */

#ifndef INC_UTILS_H_
#define INC_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "stm32h7xx.h"
#include <core_cm7.h>

#define UTILS_MAX_DAILY_LOGS   25U
#define UTILS_EMAIL_SIZE       64U
#define UTILS_LOG_BUFFER_SIZE  512U

typedef enum {
    wifiInitFail = 0,
    nfcInitFail,
    displayInitFail,
    uartInitFail,
    nfcReadFail,
    nfcDecryptFail,
    nfcParseFail,
    checkinBadRequest,
    checkinServerError,
    uartTimeout,
    datetimeRequestFail,
    filesSendFail,
    wifiConnectionLost,
    ledDmaFail,
    malformedUartResponse,
    logCodeCount
} logCodes;

typedef struct {
    char email[UTILS_EMAIL_SIZE];
    uint16_t logIndex;
    char logs[UTILS_LOG_BUFFER_SIZE];
    uint8_t logHour;
} utilsStruct_type;

extern utilsStruct_type utilsStruct;
extern const uint8_t test_font[4][5];

void ITM_Print(char *msg);
uint16_t rgb2bgr565(uint16_t c);

void utils_init(const char *email, uint8_t logHour);
bool utils_addLog(logCodes code, const char *hhmmss);
void utils_clearLogs(void);
uint16_t utils_getLogCount(void);
bool utils_buildFilesMessage(char *dst, size_t dstSize);

#ifdef __cplusplus
}
#endif

#endif /* INC_UTILS_H_ */

