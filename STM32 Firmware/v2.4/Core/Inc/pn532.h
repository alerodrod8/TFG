/*
 * pn532.h
 * STM32H7 HAL software-SPI PN532 driver.
 *
 * The transport is PN532 SPI mode 0, LSB first.  Hardware SPI is explicitly
 * de-initialized before the NFC pins are switched to GPIO bit-banging.
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
#define PN532_PREAMBLE             (0x00U)
#define PN532_STARTCODE1           (0x00U)
#define PN532_STARTCODE2           (0xFFU)
#define PN532_POSTAMBLE            (0x00U)
#define PN532_HOSTTOPN532          (0xD4U)
#define PN532_PN532TOHOST          (0xD5U)

/* PN532 commands */
#define PN532_COMMAND_GETFIRMWAREVERSION   (0x02U)
#define PN532_COMMAND_SAMCONFIGURATION     (0x14U)
#define PN532_COMMAND_RFCONFIGURATION      (0x32U)
#define PN532_COMMAND_INLISTPASSIVETARGET  (0x4AU)
#define PN532_COMMAND_INDATAEXCHANGE       (0x40U)

#define PN532_RESPONSE_GETFIRMWAREVERSION  (0x03U)
#define PN532_RESPONSE_SAMCONFIGURATION    (0x15U)
#define PN532_RESPONSE_RFCONFIGURATION     (0x33U)
#define PN532_RESPONSE_INLISTPASSIVETARGET (0x4BU)
#define PN532_RESPONSE_INDATAEXCHANGE      (0x41U)

/* SPI protocol bytes */
#define PN532_SPI_DATAWRITE        (0x01U)
#define PN532_SPI_STATREAD         (0x02U)
#define PN532_SPI_DATAREAD         (0x03U)
#define PN532_SPI_READY            (0x01U)

/* SPI status exception bits (PN532 SPIStatus register). */
#define PN532_SPI_STATUS_READY     (0x01U)
#define PN532_SPI_STATUS_RCV_OVR   (0x04U)
#define PN532_SPI_STATUS_TR_FE     (0x08U)
#define PN532_SPI_STATUS_ERROR_MASK (PN532_SPI_STATUS_RCV_OVR | PN532_SPI_STATUS_TR_FE)

/* Card type */
#define PN532_MIFARE_ISO14443A     (0x00U)

/* MIFARE commands */
#define MIFARE_CMD_AUTH_A          (0x60U)
#define MIFARE_CMD_AUTH_B          (0x61U)
#define MIFARE_CMD_READ            (0x30U)
#define MIFARE_CMD_WRITE           (0xA0U)

#ifndef PN532_DEFAULT_TIMEOUT_MS
#define PN532_DEFAULT_TIMEOUT_MS   1000U
#endif

#ifndef PN532_FOREVER_TIMEOUT_MS
#define PN532_FOREVER_TIMEOUT_MS   0U
#endif

#define PN532_PACKBUFFSIZ          80U

/* nfc_last_error values, intended to be inspected directly in the debugger. */
typedef enum
{
    PN532_DEBUG_OK = 0U,
    PN532_DEBUG_BAD_ARGUMENT = 1U,
    PN532_DEBUG_TX_TOO_LONG = 2U,
    PN532_DEBUG_READY_TIMEOUT = 3U,
    PN532_DEBUG_BAD_ACK = 4U,
    PN532_DEBUG_BAD_FRAME_HEADER = 5U,
    PN532_DEBUG_BAD_LEN_CHECKSUM = 6U,
    PN532_DEBUG_FRAME_TOO_LONG = 7U,
    PN532_DEBUG_BAD_DATA_CHECKSUM = 8U,
    PN532_DEBUG_BAD_POSTAMBLE = 9U,
    PN532_DEBUG_BAD_RESPONSE = 10U,
    PN532_DEBUG_PN532_STATUS = 11U,
    PN532_DEBUG_UID_LENGTH = 12U,
    PN532_DEBUG_SPI_STATUS = 13U
} PN532_DebugError;

typedef struct
{
    /* Kept so PN532_SPI_Init can cleanly de-init CubeMX SPI1 before GPIO bit-bang. */
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    uint8_t packetbuffer[PN532_PACKBUFFSIZ];
    uint8_t inListedTag;
    uint16_t lastAtqa;
    uint8_t lastSak;
} PN532;

void     PN532_SPI_Init(PN532 *dev, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin);
uint8_t  PN532_Begin(PN532 *dev);
void     PN532_Wakeup(PN532 *dev);
uint8_t  PN532_SAMConfig(PN532 *dev);
uint32_t PN532_GetFirmwareVersion(PN532 *dev);
uint8_t  PN532_SetPassiveActivationRetries(PN532 *dev, uint8_t maxRetries);

uint8_t  PN532_ReadPassiveTargetID_Timeout(PN532 *dev, uint8_t cardbaudrate, uint8_t *uid,
                                           uint8_t *uidLength, uint32_t timeout_ms);
uint8_t  PN532_ReadDetectedPassiveTargetID(PN532 *dev, uint8_t *uid, uint8_t *uidLength);

uint8_t  PN532_MifareClassic_AuthenticateBlock(PN532 *dev, uint8_t *uid, uint8_t uidLen,
                                               uint32_t blockNumber, uint8_t keyNumber,
                                               uint8_t *keyData);
uint8_t  PN532_MifareClassic_ReadDataBlock(PN532 *dev, uint8_t blockNumber, uint8_t *data);
uint8_t  PN532_MifareClassic_WriteDataBlock(PN532 *dev, uint8_t blockNumber, uint8_t *data);

/* Low-level helpers exposed for debugger bring-up. */
uint8_t  PN532_SendCommandCheckAck(PN532 *dev, uint8_t *cmd, uint8_t cmdlen,
                                   uint32_t timeout_ms);
uint8_t  PN532_IsReady(PN532 *dev);
uint8_t  PN532_WaitReady(PN532 *dev, uint32_t timeout_ms);
uint8_t  PN532_ReadAck(PN532 *dev);
void     PN532_ReadData(PN532 *dev, uint8_t *buff, uint8_t n);
void     PN532_WriteCommand(PN532 *dev, uint8_t *cmd, uint8_t cmdlen);

/* Bring-up diagnostics. */
extern volatile uint8_t nfc_fw_raw[16];
extern volatile uint8_t nfc_resp_debug[16];
extern volatile uint8_t nfc_dbg_cmd_ok;
extern volatile uint8_t nfc_dbg_ack_ok;
extern volatile uint8_t nfc_dbg_response_ready;
extern volatile uint8_t nfc_debug_ready_reply;
extern volatile uint8_t nfc_debug_ready_error_bits;
extern volatile uint32_t nfc_debug_gpioa_idr;
extern volatile uint8_t nfc_last_ack[6];
extern volatile uint8_t nfc_last_frame[PN532_PACKBUFFSIZ];
extern volatile uint8_t nfc_last_frame_len;
extern volatile uint8_t nfc_last_error;
extern volatile uint8_t nfc_ready_history[32];
extern volatile uint8_t nfc_ready_history_idx;

#ifdef __cplusplus
}
#endif

#endif /* INC_PN532_H_ */
