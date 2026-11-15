/*
 * pn532.h
 * STM32 HAL software-SPI PN532 driver.
 * API compatible with the nfc.c used in this project.
 */

#ifndef INC_PN532_H_
#define INC_PN532_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <FreeRTOS.h>
#include "task.h"
#include <string.h>

/* PN532 frame constants */
#define PN532_PREAMBLE             (0x00)
#define PN532_STARTCODE1           (0x00)
#define PN532_STARTCODE2           (0xFF)
#define PN532_POSTAMBLE            (0x00)
#define PN532_HOSTTOPN532          (0xD4)
#define PN532_PN532TOHOST          (0xD5)

/* PN532 commands */
#define PN532_COMMAND_GETFIRMWAREVERSION   (0x02)
#define PN532_COMMAND_SAMCONFIGURATION     (0x14)
#define PN532_COMMAND_RFCONFIGURATION      (0x32)
#define PN532_COMMAND_INLISTPASSIVETARGET  (0x4A)
#define PN532_COMMAND_INDATAEXCHANGE       (0x40)

#define PN532_RESPONSE_INLISTPASSIVETARGET (0x4B)
#define PN532_RESPONSE_INDATAEXCHANGE      (0x41)

/* SPI protocol bytes */
#define PN532_SPI_STATREAD         (0x02)
#define PN532_SPI_DATAWRITE        (0x01)
#define PN532_SPI_DATAREAD         (0x03)
#define PN532_SPI_READY            (0x01)

/* Card type */
#define PN532_MIFARE_ISO14443A     (0x00)

/* MIFARE commands */
#define MIFARE_CMD_AUTH_A          (0x60)
#define MIFARE_CMD_AUTH_B          (0x61)
#define MIFARE_CMD_READ            (0x30)
#define MIFARE_CMD_WRITE           (0xA0)

#ifndef PN532_DEFAULT_TIMEOUT_MS
#define PN532_DEFAULT_TIMEOUT_MS   1000U
#endif

#ifndef PN532_FOREVER_TIMEOUT_MS
#define PN532_FOREVER_TIMEOUT_MS   0U
#endif

#define PN532_PACKBUFFSIZ          80U

typedef struct
{
    /*
     * hspi is kept only for API compatibility.
     * The driver below uses software SPI on:
     * NFC_NSS_Pin, NFC_SCK_Pin, NFC_MISO_Pin, NFC_MOSI_Pin.
     */
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    uint8_t packetbuffer[PN532_PACKBUFFSIZ];
    uint8_t inListedTag;
} PN532;

void     PN532_SPI_Init(PN532 *dev, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin);
uint8_t  PN532_Begin(PN532 *dev);
void     PN532_Reset(PN532 *dev);
void     PN532_Wakeup(PN532 *dev);
uint8_t  PN532_SAMConfig(PN532 *dev);
uint32_t PN532_GetFirmwareVersion(PN532 *dev);
uint8_t  PN532_SetPassiveActivationRetries(PN532 *dev, uint8_t maxRetries);

uint8_t  PN532_ReadPassiveTargetID_Timeout(PN532 *dev, uint8_t cardbaudrate, uint8_t *uid, uint8_t *uidLength, uint32_t timeout_ms);
uint8_t  PN532_ReadDetectedPassiveTargetID(PN532 *dev, uint8_t *uid, uint8_t *uidLength);

uint8_t  PN532_MifareClassic_AuthenticateBlock(PN532 *dev, uint8_t *uid, uint8_t uidLen,
                                               uint32_t blockNumber, uint8_t keyNumber, uint8_t *keyData);
uint8_t  PN532_MifareClassic_ReadDataBlock(PN532 *dev, uint8_t blockNumber, uint8_t *data);
uint8_t  PN532_MifareClassic_WriteDataBlock(PN532 *dev, uint8_t blockNumber, uint8_t *data);

/* Low-level helpers exposed for debugging. */
uint8_t  PN532_SendCommandCheckAck(PN532 *dev, uint8_t *cmd, uint8_t cmdlen, uint32_t timeout_ms);
uint8_t  PN532_IsReady(PN532 *dev);
uint8_t  PN532_WaitReady(PN532 *dev, uint32_t timeout_ms);
uint8_t  PN532_ReadAck(PN532 *dev);
void     PN532_ReadData(PN532 *dev, uint8_t *buff, uint8_t n);
void     PN532_WriteCommand(PN532 *dev, uint8_t *cmd, uint8_t cmdlen);

#ifdef __cplusplus
}
#endif

#endif /* INC_PN532_H_ */
