/*
 * nfc.c
 *
 * Reads the encrypted payload from a MIFARE Classic NFC card through PN532
 * software SPI, decrypts it with AES-128-CBC and stores the plaintext in
 * decypheredText, matching the original Arduino behavior.
 */

#include "nfc.h"

uint8_t key[AES_BYTES] =
{
    0x0d, 0x02, 0x0d, 0x0f,
    0x02, 0x07, 0x0f, 0x08,
    0x04, 0x0f, 0x03, 0x0a,
    0x0f, 0x0c, 0x06, 0x0e
};

uint8_t keya[6] =
{
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};

PN532 pn532;

uint8_t iv[AES_BYTES];
uint8_t decypher[CYPHER_LENGTH];
char decypheredText[STRING_LENGTH];
volatile size_t decryptedLength = 0;

static size_t removePKCS7(uint8_t *buffer, size_t dataLength)
{
    if (dataLength == 0U) {
        return 0U;
    }

    uint8_t padding = buffer[dataLength - 1U];

    if (padding == 0U || padding > AES_BYTES) {
        return 0U;
    }

    for (uint8_t i = 0; i < padding; i++) {
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

/*
 * Reads the card using the same storage layout as your Arduino code:
 *
 * block 4:
 *   byte 0..1 = encrypted totalLength, big endian
 *
 * blocks 5..8:
 *   IV, 4 bytes per block
 *
 * blocks 9+:
 *   encrypted data, 4 bytes per block
 *
 * NOTE: This intentionally keeps the 4-byte-per-block layout from your
 * working Arduino sketch, even though each MIFARE block contains 16 bytes.
 */
static uint8_t readAndDecryptCurrentCard(uint8_t *uid, uint8_t uidLength)
{
    uint8_t dataToRead[16] = {0};
    uint8_t success;
    size_t totalLength;
    int blocks;

//    success = PN532_MifareClassic_AuthenticateBlock(&pn532, uid, uidLength, 4, 0, keya);
//    if (!success) {
//        status = 1;
//        nfc_status = 31;
//        vTaskDelay(3000);
//        return 0;
//    }

    success = PN532_MifareClassic_ReadDataBlock(&pn532, 4, dataToRead);
    if (!success) {
        return 0;
    }

    totalLength = ((size_t)dataToRead[0] << 8) | dataToRead[1];

    if (totalLength == 0U || totalLength > CYPHER_LENGTH) {
        return 0;
    }

    if ((totalLength % AES_BYTES) != 0U) {
        return 0;
    }

    blocks = 1 + ((int)sizeof(iv) / 4) + ((int)totalLength / 4);

    for (int addr = 1; addr < blocks; addr++) {
        uint8_t blockdata[16] = {0};
        uint8_t bNum = (uint8_t)(addr + 4);

//        success = PN532_MifareClassic_AuthenticateBlock(&pn532, uid, uidLength, bNum, 0, keya);
//        if (!success) {
//            status = 3;
//            return 0;
//        }

        success = PN532_MifareClassic_ReadDataBlock(&pn532, bNum, blockdata);
        if (!success) {
            return 0;
        }

        if (addr >= 1 && addr <= 4) {
            memcpy(&iv[(addr - 1) * 4], blockdata, 4);
        } else if (addr >= 5) {
            size_t offset = (size_t)(addr - 5) * 4U;

            if ((offset + 4U) <= sizeof(decypher)) {
                memcpy(&decypher[offset], blockdata, 4);
            } else {
                return 0;
            }
        }
    }

    decryptedLength = decryptAES_CBC(decypher, totalLength, iv);

    if (decryptedLength == 0U || decryptedLength >= STRING_LENGTH) {
        return 0;
    }

    memcpy(decypheredText, decypher, decryptedLength);
    decypheredText[decryptedLength] = '\0';

    return 1;
}

void nfcTest(void *argument)
{
    uint8_t uid[7];
    uint8_t uidLength;
    uint8_t success;

    PN532_SPI_Init(&pn532, &hspi6, NFC_NSS_GPIO_Port, NFC_NSS_Pin);

    PN532_Reset(&pn532);

    if (!PN532_Begin(&pn532)) {
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /*
     * Expected value observed on your hardware:
     * fw_debug = 0x32010607
     */

    if (!PN532_SAMConfig(&pn532)) {
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /*
     * Avoid forever blocking while debugging card detection.
     */
    (void)PN532_SetPassiveActivationRetries(&pn532, 0x05);

    while (1) {
        memset(uid, 0, sizeof(uid));
        uidLength = 0U;

        success = PN532_ReadPassiveTargetID_Timeout(&pn532,
                                                    PN532_MIFARE_ISO14443A,
                                                    uid,
                                                    &uidLength,
                                                    3000);

        if (success) {
            clearBuffers();

            if (readAndDecryptCurrentCard(uid, uidLength)) {
                /*
                 * Final OK state:
                 *   status = 0
                 *   nfc_status = 30
                 *   decypheredText contains the decrypted plaintext
                 *   decryptedLength contains its length
                 */
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
