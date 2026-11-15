/*
 * utils.c
 *
 *  Created on: Apr 26, 2026
 *      Author: rodri
 */

#include "utils.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

utilsStruct_type utilsStruct;

static void utils_lock(void)
{
    taskENTER_CRITICAL();
}

static void utils_unlock(void)
{
    taskEXIT_CRITICAL();
}

void utils_init(const char *email, uint8_t logHour)
{
    memset(&utilsStruct, 0, sizeof(utilsStruct));
    utilsStruct.logHour = logHour;
    if (email != NULL) {
        strncpy(utilsStruct.email, email, sizeof(utilsStruct.email) - 1U);
        utilsStruct.email[sizeof(utilsStruct.email) - 1U] = '\0';
    }
}

bool utils_addLog(logCodes code, const char *hhmmss)
{
    char entry[20];
    size_t used;
    int written;

    if (code >= logCodeCount || hhmmss == NULL || strlen(hhmmss) != 6U) {
        return false;
    }
    utils_lock();
    if (utilsStruct.logIndex >= UTILS_MAX_DAILY_LOGS) {
        utils_unlock();
        return false;
    }
    /* Enum 0 is serialized as E01, enum 1 as E02, ... */
    written = snprintf(entry, sizeof(entry), "E%02u&%s;", (unsigned int)code + 1U, hhmmss);
    if (written <= 0 || (size_t)written >= sizeof(entry)) {
        utils_unlock();
        return false;
    }
    used = strlen(utilsStruct.logs);
    if ((used + (size_t)written + 1U) > sizeof(utilsStruct.logs)) {
        utils_unlock();
        return false;
    }
    memcpy(&utilsStruct.logs[used], entry, (size_t)written + 1U);
    utilsStruct.logIndex++;
    utils_unlock();
    return true;
}

void utils_clearLogs(void)
{
    utils_lock();
    utilsStruct.logIndex = 0U;
    utilsStruct.logs[0] = '\0';
    utils_unlock();
}

uint16_t utils_getLogCount(void)
{
    uint16_t count;
    utils_lock();
    count = utilsStruct.logIndex;
    utils_unlock();
    return count;
}

bool utils_buildFilesMessage(char *dst, size_t dstSize)
{
    int n;
    if (dst == NULL || dstSize == 0U) {
        return false;
    }
    utils_lock();
    n = snprintf(dst, dstSize, "EMAIL=%s,LOGS=%s", utilsStruct.email, utilsStruct.logs);
    utils_unlock();
    return (n > 0 && (size_t)n < dstSize);
}

void ITM_Print(char *msg)
{
    while (msg != NULL && *msg) {
        ITM_SendChar(*msg++);
    }
}

uint16_t rgb2bgr565(uint16_t c)
{
    return ((c & 0xF800U) >> 11) |
           ( c & 0x07E0U)       |
           ((c & 0x001FU) << 11);
}

const uint8_t test_font[4][5] =
{
    { 0b11111, 0b00100, 0b00100, 0b00100, 0b00100 },
    { 0b11111, 0b10000, 0b11110, 0b10000, 0b11111 },
    { 0b11111, 0b10000, 0b11111, 0b00001, 0b11111 },
    { 0b11111, 0b00100, 0b00100, 0b00100, 0b00100 }
};

