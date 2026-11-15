/*

 * nfc.c

 *

 * Reads the encrypted payload from a MIFARE Classic NFC card through PN532

 * software SPI, decrypts it with AES-128-CBC and stores the plaintext in

 * decypheredText, matching the original Arduino behavior.

 */

#include "nfc.h"

#include <ctype.h>

#include <stdio.h>

#include <string.h>

extern SPI_HandleTypeDef hspi1;

uint8_t key[AES_BYTES] =

{

    0x0d, 0x02, 0x0d, 0x0f,

    0x02, 0x07, 0x0f, 0x08,

    0x04, 0x0f, 0x03, 0x0a,

    0x0f, 0x0c, 0x06, 0x0e

};

uint8_t keya[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

PN532 pn532;

uint8_t iv[AES_BYTES];

uint8_t decypher[CYPHER_LENGTH];

char decypheredText[STRING_LENGTH];

volatile size_t decryptedLength = 0U;

/* Diagnostic variables for the real NFC initialization flow. */

volatile uint8_t nfc_test_stage = 0U;

volatile uint8_t nfc_test_begin = 0U;

volatile uint32_t nfc_test_firmware = 0U;

volatile uint8_t nfc_test_sam = 0U;

volatile uint8_t nfc_test_init_ok = 0U;

/* Defined in pn532.c. */

extern volatile uint8_t nfc_fw_raw[16];

extern volatile uint8_t nfc_resp_debug[16];

extern volatile uint8_t nfc_dbg_cmd_ok;

extern volatile uint8_t nfc_dbg_ack_ok;

extern volatile uint8_t nfc_dbg_response_ready;

static size_t removePKCS7(uint8_t *buffer, size_t dataLength)

{

    if (buffer == NULL || dataLength == 0U) {

        return 0U;

    }

    uint8_t padding = buffer[dataLength - 1U];

    if (padding == 0U || padding > AES_BYTES || padding > dataLength) {

        return 0U;

    }

    for (uint8_t i = 0U; i < padding; ++i) {

        if (buffer[dataLength - 1U - i] != padding) {

            return 0U;

        }

    }

    return dataLength - padding;

}

static size_t decryptAES_CBC(uint8_t *data, size_t totalLength, uint8_t *ivLocal)

{

    struct AES_ctx ctx;

    AES_init_ctx_iv(&ctx, key, ivLocal);

    AES_CBC_decrypt_buffer(&ctx, data, totalLength);

    return removePKCS7(data, totalLength);

}

static void clearBuffers(void)

{

    memset(iv, 0, sizeof(iv));

    memset(decypher, 0, sizeof(decypher));

    memset(decypheredText, 0, sizeof(decypheredText));

    decryptedLength = 0U;

}

static bool mifareIsTrailerBlock(uint8_t blockNumber)
{
    if (blockNumber < 128U) {
        return (((uint8_t)(blockNumber + 1U) % 4U) == 0U);
    }

    /* MIFARE Classic 4K large sectors (32..39) have 16 blocks/sector. */
    return (((uint8_t)(blockNumber + 1U) % 16U) == 0U);
}

static bool mifareNextDataBlock(uint8_t *blockNumber)
{
    if (blockNumber == NULL) {
        return false;
    }

    do {
        if (*blockNumber == 0xFFU) {
            return false;
        }
        ++(*blockNumber);
    } while (mifareIsTrailerBlock(*blockNumber));

    return true;
}

static bool mifareReadAuthenticated(const uint8_t *uid, uint8_t uidLength,
                                    uint8_t blockNumber, uint8_t *data)
{
    if (uid == NULL || data == NULL || mifareIsTrailerBlock(blockNumber)) {
        return false;
    }

    if (!PN532_MifareClassic_AuthenticateBlock(&pn532,
                                                (uint8_t *)uid,
                                                uidLength,
                                                blockNumber,
                                                0U,       /* Key A */
                                                keya)) {
        return false;
    }

    return PN532_MifareClassic_ReadDataBlock(&pn532, blockNumber, data) != 0U;
}

static nfcResult_type readAndDecryptCurrentCard(uint8_t *uid, uint8_t uidLength)
{
    uint8_t blockdata[16] = {0U};
    uint8_t blockNumber = 4U;
    size_t totalLength;
    size_t ivOffset = 0U;
    size_t cipherOffset = 0U;

    if (uid == NULL || (uidLength != 4U && uidLength != 7U)) {
        return NFC_RESULT_READ_ERROR;
    }

    /* Block 4 stores the encrypted payload length in its first two bytes. */
    if (!mifareReadAuthenticated(uid, uidLength, blockNumber, blockdata)) {
        return NFC_RESULT_READ_ERROR;
    }

    totalLength = ((size_t)blockdata[0] << 8) | (size_t)blockdata[1];
    if (totalLength == 0U || totalLength > CYPHER_LENGTH ||
        (totalLength % AES_BYTES) != 0U) {
        return NFC_RESULT_DECRYPT_ERROR;
    }

    /*
     * Card format keeps four useful bytes per MIFARE DATA block.  Sector trailer
     * blocks (7, 11, 15, ...; and every 16th block in Classic 4K large sectors)
     * are NEVER payload.  The old implementation incorrectly consumed block 7
     * as part of the IV and later consumed further trailers as ciphertext.
     */
    while (ivOffset < sizeof(iv)) {
        if (!mifareNextDataBlock(&blockNumber)) {
            return NFC_RESULT_READ_ERROR;
        }
        memset(blockdata, 0, sizeof(blockdata));
        if (!mifareReadAuthenticated(uid, uidLength, blockNumber, blockdata)) {
            return NFC_RESULT_READ_ERROR;
        }

        memcpy(&iv[ivOffset], blockdata, 4U);
        ivOffset += 4U;
    }

    while (cipherOffset < totalLength) {
        if (!mifareNextDataBlock(&blockNumber)) {
            return NFC_RESULT_READ_ERROR;
        }
        memset(blockdata, 0, sizeof(blockdata));
        if (!mifareReadAuthenticated(uid, uidLength, blockNumber, blockdata)) {
            return NFC_RESULT_READ_ERROR;
        }

        if ((cipherOffset + 4U) > sizeof(decypher)) {
            return NFC_RESULT_DECRYPT_ERROR;
        }
        memcpy(&decypher[cipherOffset], blockdata, 4U);
        cipherOffset += 4U;
    }

    decryptedLength = decryptAES_CBC(decypher, totalLength, iv);
    if (decryptedLength == 0U || decryptedLength >= STRING_LENGTH) {
        return NFC_RESULT_DECRYPT_ERROR;
    }

    memcpy(decypheredText, decypher, decryptedLength);
    decypheredText[decryptedLength] = '\0';
    return NFC_RESULT_OK;
}

static char *trim(char *s)

{

    char *end;

    while (*s != '\0' && isspace((unsigned char)*s)) {

        ++s;

    }

    end = s + strlen(s);

    while (end > s &&

           (isspace((unsigned char)end[-1]) || end[-1] == '"' || end[-1] == '}')) {

        *--end = '\0';

    }

    while (*s == '"' || *s == '{') {

        ++s;

    }

    return s;

}

static void uppercase(char *s)

{

    while (*s != '\0') {

        *s = (char)toupper((unsigned char)*s);

        ++s;

    }

}

static void copyField(char *dst, size_t dstSize, const char *src)

{

    if (dstSize == 0U) {

        return;

    }

    strncpy(dst, src, dstSize - 1U);

    dst[dstSize - 1U] = '\0';

}

bool nfc_parseDecryptedCheckIn(const char *plainText, checkInData_type *checkIn)

{

    char working[STRING_LENGTH];

    char *savePtr = NULL;

    char *item;

    if (plainText == NULL || checkIn == NULL || *plainText == '\0') {

        return false;

    }

    memset(checkIn, 0, sizeof(*checkIn));

    strncpy(working, plainText, sizeof(working) - 1U);

    working[sizeof(working) - 1U] = '\0';

    item = strtok_r(working, ",;\r\n", &savePtr);

    while (item != NULL) {

        char *separator;

        char *keyText;

        char *valueText;

        char keyUpper[24];

        item = trim(item);

        separator = strchr(item, '=');

        if (separator == NULL) {

            separator = strchr(item, ':');

        }

        if (separator != NULL) {

            *separator = '\0';

            keyText = trim(item);

            valueText = trim(separator + 1);

            strncpy(keyUpper, keyText, sizeof(keyUpper) - 1U);

            keyUpper[sizeof(keyUpper) - 1U] = '\0';

            uppercase(keyUpper);

            if (strcmp(keyUpper, "STATE") == 0) copyField(checkIn->state, sizeof(checkIn->state), valueText);

            else if (strcmp(keyUpper, "HQ") == 0) copyField(checkIn->hq, sizeof(checkIn->hq), valueText);

            else if (strcmp(keyUpper, "DEPARTMENT") == 0) copyField(checkIn->department, sizeof(checkIn->department), valueText);

            else if (strcmp(keyUpper, "USERNAME") == 0 || strcmp(keyUpper, "USERNAME1") == 0) copyField(checkIn->username1, sizeof(checkIn->username1), valueText);

            else if (strcmp(keyUpper, "USERNAME2") == 0) copyField(checkIn->username2, sizeof(checkIn->username2), valueText);

            else if (strcmp(keyUpper, "SURNAME1") == 0) copyField(checkIn->surname1, sizeof(checkIn->surname1), valueText);

            else if (strcmp(keyUpper, "SURNAME2") == 0) copyField(checkIn->surname2, sizeof(checkIn->surname2), valueText);

            else if (strcmp(keyUpper, "SURNAME3") == 0) copyField(checkIn->surname3, sizeof(checkIn->surname3), valueText);

            else if (strcmp(keyUpper, "JOBPOS") == 0) copyField(checkIn->jobpos, sizeof(checkIn->jobpos), valueText);

            else if (strcmp(keyUpper, "USERID") == 0) copyField(checkIn->userid, sizeof(checkIn->userid), valueText);

            else if (strcmp(keyUpper, "ISADMIN") == 0) copyField(checkIn->isadmin, sizeof(checkIn->isadmin), valueText);

        }

        item = strtok_r(NULL, ",;\r\n", &savePtr);

    }

    return (checkIn->username1[0] != '\0' && checkIn->userid[0] != '\0');

}

bool nfc_init(void)
{
    uint32_t firmware;

    nfc_test_stage = 1U;
    nfc_test_begin = 0U;
    nfc_test_firmware = 0U;
    nfc_test_sam = 0U;
    nfc_test_init_ok = 0U;

    /*
     * CubeMX configured SPI1 first.  Pass its handle so PN532_SPI_Init can
     * de-initialize the peripheral/AF pins before switching to software SPI.
     */
    PN532_SPI_Init(&pn532, &hspi1, NFC_NSS_GPIO_Port, NFC_NSS_Pin);
    nfc_test_stage = 2U;

    /* PN532_Begin performs the LowVbat SPI wake-up and SAMConfiguration. */
    nfc_test_begin = PN532_Begin(&pn532);
    nfc_test_sam = nfc_test_begin;
    if (!nfc_test_begin) {
        nfc_test_stage = 20U;
        return false;
    }

    /* Firmware is queried only after the PN532 has entered SAM normal mode. */
    nfc_test_stage = 3U;
    firmware = PN532_GetFirmwareVersion(&pn532);
    nfc_test_firmware = firmware;
    if (firmware == 0U) {
        nfc_test_stage = 30U;
        return false;
    }

    nfc_test_stage = 4U;
    if (!PN532_SetPassiveActivationRetries(&pn532, 0x02U)) {
        nfc_test_stage = 40U;
        return false;
    }

    nfc_test_stage = 5U;
    nfc_test_init_ok = 1U;
    return true;
}

nfcResult_type nfc_pollCheckIn(checkInData_type *checkIn, uint32_t timeoutMs)

{

    uint8_t uid[7] = {0};

    uint8_t uidLength = 0U;

    nfcResult_type result;

    if (checkIn == NULL) {

        return NFC_RESULT_PARSE_ERROR;

    }

    if (!PN532_ReadPassiveTargetID_Timeout(&pn532,

                                           PN532_MIFARE_ISO14443A,

                                           uid,

                                           &uidLength,

                                           timeoutMs)) {

        return NFC_RESULT_NO_CARD;

    }

    clearBuffers();

    result = readAndDecryptCurrentCard(uid, uidLength);

    if (result != NFC_RESULT_OK) {

        return result;

    }

    if (!nfc_parseDecryptedCheckIn(decypheredText, checkIn)) {

        return NFC_RESULT_PARSE_ERROR;

    }

    return NFC_RESULT_OK;

}

void nfcTest(void *argument)
{
	PN532_SPI_Init(&pn532,
	               &hspi1,
	               NFC_NSS_GPIO_Port,
	               NFC_NSS_Pin);

	/*
	 * PON BREAKPOINT AQUÍ.
	 */
	volatile uint32_t mode =
	    (GPIOA->MODER >> (6U * 2U)) & 0x3U;

	volatile uint32_t pupd =
	    (GPIOA->PUPDR >> (6U * 2U)) & 0x3U;

	volatile uint32_t raw =
	    (GPIOA->IDR >> 6U) & 0x1U;
}
