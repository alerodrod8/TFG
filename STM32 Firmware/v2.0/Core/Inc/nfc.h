/*
 * nfc.h
 *
 * STM32F767 + CMSIS/FreeRTOS NFC reader using PN532 software SPI.
 */

#ifndef INC_NFC_H_
#define INC_NFC_H_

#include "cmsis_os.h"
#include "main.h"
#include "string.h"
#include "stdio.h"
#include "stdint.h"
#include <stddef.h>

#include "pn532.h"
#include "aes.h"

#define AES_BYTES       16U
#define STRING_LENGTH   256U
#define CYPHER_LENGTH   256U

/*
 * Debug/status codes for nfc_status:
 * 10 = initializing PN532
 * 11 = PN532_Begin failed
 * 12 = reading firmware
 * 13 = firmware read failed
 * 14 = firmware OK
 * 15 = SAMConfig failed
 * 16 = PN532 initialized
 * 20 = waiting card
 * 21 = card detected
 * 30 = card read/decrypted OK
 *
 * status codes while card processing:
 * 0 = OK
 * 1 = auth block 4 failed
 * 2 = read block 4 failed
 * 3 = auth data block failed
 * 4 = read data block failed
 * 5 = data-block loop failed
 * 6 = totalLength invalid/out of buffer
 * 7 = totalLength not AES block aligned
 * 8 = PKCS7/decrypted length invalid
 */
extern volatile size_t decryptedLength;

extern uint8_t key[AES_BYTES];
extern uint8_t keya[6];
extern PN532 pn532;

extern uint8_t iv[AES_BYTES];
extern uint8_t decypher[CYPHER_LENGTH];
extern char decypheredText[STRING_LENGTH];

extern SPI_HandleTypeDef hspi6;

void nfcTest(void *argument);

#endif /* INC_NFC_H_ */
