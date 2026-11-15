/*
 * pn532.c
 * STM32 HAL software-SPI PN532 driver.
 *
 * This uses PN532 SPI mode 0, LSB first, implemented by GPIO bit-banging.
 * It keeps the original PN532_* API so nfc.c does not need to know whether
 * the transport is hardware SPI or software SPI.
 */

#include "pn532.h"

static const uint8_t pn532ack[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

static void pn532_delay(uint32_t ms)
{
    /*
     * Runs inside a FreeRTOS task in this project.
     * HAL_Delay also works after HAL_Init, but vTaskDelay is RTOS-friendly.
     */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        vTaskDelay(ms);
    } else {
    	vTaskDelay(ms);
    }
}

static void pn532_cs_low(PN532 *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
}

static void pn532_cs_high(PN532 *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
}

static void pn532_sck_low(void)
{
    HAL_GPIO_WritePin(NFC_SCK_GPIO_Port, NFC_SCK_Pin, GPIO_PIN_RESET);
}

static void pn532_sck_high(void)
{
    HAL_GPIO_WritePin(NFC_SCK_GPIO_Port, NFC_SCK_Pin, GPIO_PIN_SET);
}

static void pn532_mosi_write(uint8_t bit)
{
    HAL_GPIO_WritePin(NFC_MOSI_GPIO_Port, NFC_MOSI_Pin,
                      bit ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint8_t pn532_miso_read(void)
{
    return HAL_GPIO_ReadPin(NFC_MISO_GPIO_Port, NFC_MISO_Pin) == GPIO_PIN_SET;
}

static void pn532_bb_delay(void)
{
    /*
     * Small delay for software SPI. Increase the loop count if cabling is long.
     */
    for (volatile uint32_t i = 0; i < 300; i++) {
        __NOP();
    }
}

/*
 * PN532 software SPI transfer.
 * Adafruit PN532 SPI uses LSB first, SPI mode 0.
 */
static uint8_t pn532_bb_transfer(uint8_t data)
{
    uint8_t rx = 0;

    for (uint8_t i = 0; i < 8; i++) {
        pn532_mosi_write((data >> i) & 0x01);

        pn532_bb_delay();
        pn532_sck_high();
        pn532_bb_delay();

        if (pn532_miso_read()) {
            rx |= (1U << i);
        }

        pn532_sck_low();
        pn532_bb_delay();
    }

    return rx;
}

static uint8_t pn532_spi_write(PN532 *dev, const uint8_t *data, uint16_t len)
{
    pn532_cs_low(dev);

    for (uint16_t i = 0; i < len; i++) {
        (void)pn532_bb_transfer(data[i]);
    }

    pn532_cs_high(dev);
    return 1;
}

static uint8_t pn532_spi_write_read(PN532 *dev,
                                    const uint8_t *tx,
                                    uint16_t tx_len,
                                    uint8_t *rx,
                                    uint16_t rx_len)
{
    pn532_cs_low(dev);

    for (uint16_t i = 0; i < tx_len; i++) {
        (void)pn532_bb_transfer(tx[i]);
    }

    for (uint16_t i = 0; i < rx_len; i++) {
        rx[i] = pn532_bb_transfer(0x00);
    }

    pn532_cs_high(dev);
    return 1;
}

/*
 * Finds the D5 response header in either:
 *   00 00 FF LEN LCS D5 RESP ...
 * or the shorter read alignment sometimes seen on SPI:
 *   00 FF LEN LCS D5 RESP ...
 *
 * Returns the index where D5 is located, or -1 if not found.
 */
static int PN532_GetResponseOffset(uint8_t *p, uint8_t expected_response)
{
    if (p[0] == 0x00 &&
        p[1] == 0x00 &&
        p[2] == 0xFF &&
        p[5] == PN532_PN532TOHOST &&
        p[6] == expected_response)
    {
        return 5;
    }

    if (p[0] == 0x00 &&
        p[1] == 0xFF &&
        p[4] == PN532_PN532TOHOST &&
        p[5] == expected_response)
    {
        return 4;
    }

    return -1;
}

void PN532_SPI_Init(PN532 *dev, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    memset(dev, 0, sizeof(*dev));
    dev->hspi = hspi;
    dev->cs_port = cs_port;
    dev->cs_pin = cs_pin;
    dev->inListedTag = 1;

    /*
     * Release SPI6 pins and use them as GPIO for software SPI.
     */
    if (hspi != NULL) {
        HAL_SPI_DeInit(hspi);
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PA4 NSS, PA5 SCK, PA7 MOSI as output */
    GPIO_InitStruct.Pin = NFC_NSS_Pin | NFC_SCK_Pin | NFC_MOSI_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA6 MISO as input */
    GPIO_InitStruct.Pin = NFC_MISO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(NFC_MISO_GPIO_Port, &GPIO_InitStruct);

    pn532_cs_high(dev);
    pn532_sck_low();
    pn532_mosi_write(0);
}

uint8_t PN532_Begin(PN532 *dev)
{
    if (dev == NULL) {
        return 0;
    }

    pn532_cs_high(dev);
    pn532_delay(50);

    PN532_Wakeup(dev);
    pn532_delay(100);

    return 1;
}

void PN532_Wakeup(PN532 *dev)
{
    pn532_cs_high(dev);
    pn532_delay(50);

    pn532_cs_low(dev);
    pn532_delay(100);

    pn532_cs_high(dev);
    pn532_delay(500);
}

uint8_t PN532_IsReady(PN532 *dev)
{
    uint8_t cmd = PN532_SPI_STATREAD;
    uint8_t reply = 0x00;

    if (!pn532_spi_write_read(dev, &cmd, 1, &reply, 1)) {
        return 0;
    }

    return (reply == PN532_SPI_READY);
}

uint8_t PN532_WaitReady(PN532 *dev, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while (1) {
        if (PN532_IsReady(dev)) {
            return 1;
        }

        if (timeout_ms != PN532_FOREVER_TIMEOUT_MS) {
            if ((HAL_GetTick() - start) >= timeout_ms) {
                return 0;
            }
        }

        pn532_delay(10);
    }
}

void PN532_ReadData(PN532 *dev, uint8_t *buff, uint8_t n)
{
    uint8_t cmd = PN532_SPI_DATAREAD;

    if (!pn532_spi_write_read(dev, &cmd, 1, buff, n)) {
    }
}

void PN532_WriteCommand(PN532 *dev, uint8_t *cmd, uint8_t cmdlen)
{
    uint8_t frame[PN532_PACKBUFFSIZ];
    uint8_t idx = 0;
    uint8_t checksum;

    if ((uint16_t)cmdlen + 9U > sizeof(frame)) {
        return;
    }

    frame[idx++] = PN532_SPI_DATAWRITE;
    frame[idx++] = PN532_PREAMBLE;
    frame[idx++] = PN532_STARTCODE1;
    frame[idx++] = PN532_STARTCODE2;
    frame[idx++] = (uint8_t)(cmdlen + 1U);
    frame[idx++] = (uint8_t)(~(cmdlen + 1U) + 1U);
    frame[idx++] = PN532_HOSTTOPN532;

    checksum = PN532_HOSTTOPN532;

    for (uint8_t i = 0; i < cmdlen; i++) {
        frame[idx++] = cmd[i];
        checksum += cmd[i];
    }

    frame[idx++] = (uint8_t)(~checksum + 1U);
    frame[idx++] = PN532_POSTAMBLE;

    if (!pn532_spi_write(dev, frame, idx)) {
    }
}

uint8_t PN532_ReadAck(PN532 *dev)
{
    uint8_t ackbuff[6] = {0};

    PN532_ReadData(dev, ackbuff, sizeof(ackbuff));

    return (memcmp(ackbuff, pn532ack, sizeof(pn532ack)) == 0);
}

uint8_t PN532_SendCommandCheckAck(PN532 *dev, uint8_t *cmd, uint8_t cmdlen, uint32_t timeout_ms)
{
    PN532_WriteCommand(dev, cmd, cmdlen);

    if (!PN532_WaitReady(dev, timeout_ms)) {
        return 0;
    }

    if (!PN532_ReadAck(dev)) {
        return 0;
    }

    pn532_delay(50);

    if (!PN532_WaitReady(dev, timeout_ms)) {
        return 0;
    }

    return 1;
}

uint8_t PN532_SAMConfig(PN532 *dev)
{
    uint8_t *p = dev->packetbuffer;
    int off;

    p[0] = PN532_COMMAND_SAMCONFIGURATION;
    p[1] = 0x01;  /* normal mode */
    p[2] = 0x14;  /* timeout = 1 s */
    p[3] = 0x00;  /* no IRQ, polling */

    if (!PN532_SendCommandCheckAck(dev, p, 4, PN532_DEFAULT_TIMEOUT_MS)) {
        return 0;
    }

    PN532_ReadData(dev, p, 9);

    off = PN532_GetResponseOffset(p, 0x15);
    return (off >= 0);
}

uint32_t PN532_GetFirmwareVersion(PN532 *dev)
{
    uint8_t *p = dev->packetbuffer;
    uint8_t resp[16] = {0};
    uint32_t response = 0;

    p[0] = PN532_COMMAND_GETFIRMWAREVERSION;

    if (!PN532_SendCommandCheckAck(dev, p, 1, 3000)) {
        return 0;
    }

    PN532_ReadData(dev, resp, 13);

    /* Normal frame: 00 00 FF 06 FA D5 03 32 01 06 07 E8 00 */
    if (resp[0] == 0x00 &&
        resp[1] == 0x00 &&
        resp[2] == 0xFF &&
        resp[5] == PN532_PN532TOHOST &&
        resp[6] == 0x03)
    {
        response  = ((uint32_t)resp[7]  << 24);
        response |= ((uint32_t)resp[8]  << 16);
        response |= ((uint32_t)resp[9]  << 8);
        response |= ((uint32_t)resp[10]);

        memcpy(p, resp, 13);
        return response;
    }

    /* Shifted SPI frame: 00 FF 06 FA D5 03 32 01 06 07 E8 00 ... */
    if (resp[0] == 0x00 &&
        resp[1] == 0xFF &&
        resp[2] == 0x06 &&
        resp[3] == 0xFA &&
        resp[4] == PN532_PN532TOHOST &&
        resp[5] == 0x03)
    {
        response  = ((uint32_t)resp[6]  << 24);
        response |= ((uint32_t)resp[7]  << 16);
        response |= ((uint32_t)resp[8]  << 8);
        response |= ((uint32_t)resp[9]);

        memcpy(p, resp, 13);
        return response;
    }

    return 0;
}

uint8_t PN532_SetPassiveActivationRetries(PN532 *dev, uint8_t maxRetries)
{
    uint8_t *p = dev->packetbuffer;

    p[0] = PN532_COMMAND_RFCONFIGURATION;
    p[1] = 5;
    p[2] = 0xFF;
    p[3] = 0x01;
    p[4] = maxRetries;

    return PN532_SendCommandCheckAck(dev, p, 5, PN532_DEFAULT_TIMEOUT_MS);
}

uint8_t PN532_ReadPassiveTargetID_Timeout(PN532 *dev, uint8_t cardbaudrate, uint8_t *uid, uint8_t *uidLength, uint32_t timeout_ms)
{
    uint8_t *p = dev->packetbuffer;

    p[0] = PN532_COMMAND_INLISTPASSIVETARGET;
    p[1] = 1;
    p[2] = cardbaudrate;

    if (!PN532_SendCommandCheckAck(dev, p, 3, timeout_ms)) {
        return 0;
    }

    return PN532_ReadDetectedPassiveTargetID(dev, uid, uidLength);
}

uint8_t PN532_ReadDetectedPassiveTargetID(PN532 *dev, uint8_t *uid, uint8_t *uidLength)
{
    uint8_t *p = dev->packetbuffer;
    int off;

    PN532_ReadData(dev, p, 24);

    off = PN532_GetResponseOffset(p, PN532_RESPONSE_INLISTPASSIVETARGET);
    if (off < 0) {
        return 0;
    }

    if (p[off + 2] != 1) {
        return 0;
    }

    dev->inListedTag = p[off + 3];
    *uidLength = p[off + 7];

    if (*uidLength > 7) {
        return 0;
    }

    memcpy(uid, &p[off + 8], *uidLength);
    return 1;
}

uint8_t PN532_MifareClassic_ReadDataBlock(PN532 *dev, uint8_t blockNumber, uint8_t *data)
{
    uint8_t *p = dev->packetbuffer;
    int off;

    p[0] = PN532_COMMAND_INDATAEXCHANGE;
    p[1] = 1;
    p[2] = MIFARE_CMD_READ;
    p[3] = blockNumber;

    if (!PN532_SendCommandCheckAck(dev, p, 4, PN532_DEFAULT_TIMEOUT_MS)) {
        return 0;
    }

    PN532_ReadData(dev, p, 26);

    off = PN532_GetResponseOffset(p, PN532_RESPONSE_INDATAEXCHANGE);
    if (off < 0) {
        return 0;
    }

    if (p[off + 2] != 0x00) {
        return 0;
    }

    memcpy(data, &p[off + 3], 16);
    return 1;
}
