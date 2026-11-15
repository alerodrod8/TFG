/*
 * nfc.h
 *
 * STM32H7A3 + CMSIS/FreeRTOS NFC reader using PN532 software SPI.
 */

#ifndef INC_NFC_H_
#define INC_NFC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cmsis_os.h"
#include "main.h"
#include "pn532.h"
#include "aes.h"

#define AES_BYTES       16U
#define STRING_LENGTH   256U
#define CYPHER_LENGTH   256U

#define CHECKIN_STATE_SIZE       16U
#define CHECKIN_HQ_SIZE          32U
#define CHECKIN_DEPARTMENT_SIZE  32U
#define CHECKIN_NAME_SIZE        32U
#define CHECKIN_JOBPOS_SIZE      48U
#define CHECKIN_USERID_SIZE      24U
#define CHECKIN_ADMIN_SIZE       8U

typedef struct {
    char state[CHECKIN_STATE_SIZE];
    char hq[CHECKIN_HQ_SIZE];
    char department[CHECKIN_DEPARTMENT_SIZE];
    char username1[CHECKIN_NAME_SIZE];
    char username2[CHECKIN_NAME_SIZE];
    char surname1[CHECKIN_NAME_SIZE];
    char surname2[CHECKIN_NAME_SIZE];
    char surname3[CHECKIN_NAME_SIZE];
    char jobpos[CHECKIN_JOBPOS_SIZE];
    char userid[CHECKIN_USERID_SIZE];
    char isadmin[CHECKIN_ADMIN_SIZE];
} checkInData_type;

typedef enum {
    NFC_RESULT_NO_CARD = 0,
    NFC_RESULT_OK,
    NFC_RESULT_READ_ERROR,
    NFC_RESULT_DECRYPT_ERROR,
    NFC_RESULT_PARSE_ERROR
} nfcResult_type;

extern volatile size_t decryptedLength;
extern uint8_t key[AES_BYTES];
extern uint8_t keya[6];
extern PN532 pn532;
extern uint8_t iv[AES_BYTES];
extern uint8_t decypher[CYPHER_LENGTH];
extern char decypheredText[STRING_LENGTH];

/* NFC bring-up state (watch these directly in STM32CubeIDE). */
extern volatile uint8_t nfc_test_stage;
extern volatile uint8_t nfc_test_begin;
extern volatile uint32_t nfc_test_firmware;
extern volatile uint8_t nfc_test_sam;
extern volatile uint8_t nfc_test_init_ok;

bool nfc_init(void);
nfcResult_type nfc_pollCheckIn(checkInData_type *checkIn, uint32_t timeoutMs);
bool nfc_parseDecryptedCheckIn(const char *plainText, checkInData_type *checkIn);
void nfcTest(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* INC_NFC_H_ */

