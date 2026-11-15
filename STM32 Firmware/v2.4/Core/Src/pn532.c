/*
 * pn532.c
 * STM32H7 HAL software-SPI PN532 driver.
 *
 * Important PN532 SPI rules implemented here:
 *  - SPI mode 0, LSB first.
 *  - NSS is HIGH while idle.
 *  - LowVbat wake-up keeps NSS LOW for oscillator start and the first command
 *    (SAMConfiguration) is clocked without releasing NSS in between.
 *  - ACK and response frames are read with their exact lengths.
 *  - Standard frames are checked: header, LEN/LCS, DCS and postamble.
 */

#include "pn532.h"

static const uint8_t pn532ack[6] = {0x00U, 0x00U, 0xFFU, 0x00U, 0xFFU, 0x00U};

volatile uint8_t nfc_fw_raw[16] = {0U};
volatile uint8_t nfc_resp_debug[16] = {0U};
volatile uint8_t nfc_dbg_cmd_ok = 0U;
volatile uint8_t nfc_dbg_ack_ok = 0U;
volatile uint8_t nfc_dbg_response_ready = 0U;
volatile uint8_t nfc_debug_ready_reply = 0U;
volatile uint8_t nfc_debug_ready_error_bits = 0U;
volatile uint32_t nfc_debug_gpioa_idr = 0U;
volatile uint8_t nfc_last_ack[6] = {0U};
volatile uint8_t nfc_last_frame[PN532_PACKBUFFSIZ] = {0U};
volatile uint8_t nfc_last_frame_len = 0U;
volatile uint8_t nfc_last_error = PN532_DEBUG_OK;
volatile uint8_t nfc_ready_history[32] = {0U};
volatile uint8_t nfc_ready_history_idx = 0U;
volatile uint32_t nfc_test_gpioa_idr_cs_high = 0U;
volatile uint32_t nfc_test_gpioa_idr_cs_low  = 0U;

volatile uint8_t nfc_test_miso_cs_high = 0U;
volatile uint8_t nfc_test_miso_cs_low  = 0U;

volatile uint8_t nfc_test_stat_raw[16] = {0U};

static void pn532_delay(uint32_t ms)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        HAL_Delay(ms);
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
    return (HAL_GPIO_ReadPin(NFC_MISO_GPIO_Port, NFC_MISO_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static void pn532_bb_delay(void)
{
    /* Deliberately conservative while bringing the board up. */
    for (volatile uint32_t i = 0U; i < 300U; ++i) {
        __NOP();
    }
}

static uint8_t pn532_bb_transfer(uint8_t data)
{
    uint8_t rx = 0U;

    for (uint8_t i = 0U; i < 8U; ++i) {
        /* PN532 SPI shifts least-significant bit first. */
        pn532_mosi_write((uint8_t)((data >> i) & 0x01U));
        pn532_bb_delay();

        /* SPI mode 0: clock idle LOW, sample on rising edge. */
        pn532_sck_high();
        pn532_bb_delay();
        pn532_bb_delay();

        if (pn532_miso_read() != 0U) {
            rx |= (uint8_t)(1U << i);
        }

        pn532_sck_low();
        pn532_bb_delay();
    }

    return rx;
}

static void pn532_debug_store_frame(const uint8_t *frame, uint8_t len)
{
    uint8_t copyLen = len;

    if (copyLen > PN532_PACKBUFFSIZ) {
        copyLen = PN532_PACKBUFFSIZ;
    }

    for (uint8_t i = 0U; i < PN532_PACKBUFFSIZ; ++i) {
        nfc_last_frame[i] = 0U;
    }
    for (uint8_t i = 0U; i < copyLen; ++i) {
        nfc_last_frame[i] = frame[i];
    }
    nfc_last_frame_len = copyLen;
}

static uint8_t pn532_spi_write(PN532 *dev, const uint8_t *data, uint16_t len)
{
    if (dev == NULL || data == NULL || len == 0U) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    /* If wake-up deliberately left NSS LOW, this write continues that transaction. */
    pn532_cs_low(dev);
    pn532_bb_delay();

    for (uint16_t i = 0U; i < len; ++i) {
        (void)pn532_bb_transfer(data[i]);
    }

    pn532_cs_high(dev);
    pn532_bb_delay();
    return 1U;
}

static uint8_t pn532_spi_write_read(PN532 *dev,
                                    const uint8_t *tx,
                                    uint16_t tx_len,
                                    uint8_t *rx,
                                    uint16_t rx_len)
{
    if (dev == NULL || (tx_len != 0U && tx == NULL) ||
        (rx_len != 0U && rx == NULL)) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    pn532_cs_low(dev);
    pn532_bb_delay();

    for (uint16_t i = 0U; i < tx_len; ++i) {
        (void)pn532_bb_transfer(tx[i]);
    }
    for (uint16_t i = 0U; i < rx_len; ++i) {
        rx[i] = pn532_bb_transfer(0x00U);
    }

    pn532_cs_high(dev);
    pn532_bb_delay();
    return 1U;
}

/*
 * Read one complete normal PN532 frame in a single DATAREAD/NSS transaction.
 * LEN is learned from the first five bytes, so we never guess a response size.
 */
static uint8_t pn532_read_frame(PN532 *dev, uint8_t *frame, uint8_t capacity,
                                uint8_t *frameLen)
{
    uint8_t spiCommand = PN532_SPI_DATAREAD;
    uint8_t len;
    uint8_t lcs;
    uint16_t total;
    uint8_t sum = 0U;

    if (dev == NULL || frame == NULL || frameLen == NULL || capacity < 7U) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    *frameLen = 0U;
    pn532_cs_low(dev);
    pn532_bb_delay();
    (void)pn532_bb_transfer(spiCommand);

    for (uint8_t i = 0U; i < 5U; ++i) {
        frame[i] = pn532_bb_transfer(0x00U);
    }

    if (frame[0] != PN532_PREAMBLE ||
        frame[1] != PN532_STARTCODE1 ||
        frame[2] != PN532_STARTCODE2) {
        pn532_cs_high(dev);
        pn532_debug_store_frame(frame, 5U);
        nfc_last_error = PN532_DEBUG_BAD_FRAME_HEADER;
        return 0U;
    }

    len = frame[3];
    lcs = frame[4];

    /* This project never needs PN532 extended frames. */
    if (len == 0xFFU && lcs == 0xFFU) {
        pn532_cs_high(dev);
        pn532_debug_store_frame(frame, 5U);
        nfc_last_error = PN532_DEBUG_FRAME_TOO_LONG;
        return 0U;
    }

    total = (uint16_t)len + 7U;

    /* Drain the whole normal frame even if it exceeds our local buffer. */
    for (uint16_t i = 5U; i < total; ++i) {
        uint8_t value = pn532_bb_transfer(0x00U);
        if (i < capacity) {
            frame[i] = value;
        }
    }
    pn532_cs_high(dev);
    pn532_bb_delay();

    if ((uint8_t)(len + lcs) != 0U) {
        pn532_debug_store_frame(frame, (total <= capacity) ? (uint8_t)total : capacity);
        nfc_last_error = PN532_DEBUG_BAD_LEN_CHECKSUM;
        return 0U;
    }

    if (total > capacity) {
        pn532_debug_store_frame(frame, capacity);
        nfc_last_error = PN532_DEBUG_FRAME_TOO_LONG;
        return 0U;
    }

    for (uint16_t i = 5U; i < (uint16_t)(5U + len); ++i) {
        sum = (uint8_t)(sum + frame[i]);
    }
    sum = (uint8_t)(sum + frame[5U + len]);
    if (sum != 0U) {
        pn532_debug_store_frame(frame, (uint8_t)total);
        nfc_last_error = PN532_DEBUG_BAD_DATA_CHECKSUM;
        return 0U;
    }

    if (frame[6U + len] != PN532_POSTAMBLE) {
        pn532_debug_store_frame(frame, (uint8_t)total);
        nfc_last_error = PN532_DEBUG_BAD_POSTAMBLE;
        return 0U;
    }

    *frameLen = (uint8_t)total;
    pn532_debug_store_frame(frame, *frameLen);
    memcpy(dev->packetbuffer, frame, *frameLen);
    nfc_last_error = PN532_DEBUG_OK;
    return 1U;
}

/* Returns response bytes after D5 + response-code in payload. */
static uint8_t pn532_read_response(PN532 *dev, uint8_t expectedResponse,
                                   uint8_t *payload, uint8_t payloadCapacity,
                                   uint8_t *payloadLen)
{
    uint8_t frame[PN532_PACKBUFFSIZ] = {0U};
    uint8_t frameLen = 0U;
    uint8_t dataLen;

    if (payloadLen == NULL) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }
    *payloadLen = 0U;

    if (!pn532_read_frame(dev, frame, sizeof(frame), &frameLen)) {
        return 0U;
    }

    (void)frameLen;
    if (frame[3] < 2U ||
        frame[5] != PN532_PN532TOHOST ||
        frame[6] != expectedResponse) {
        nfc_last_error = PN532_DEBUG_BAD_RESPONSE;
        return 0U;
    }

    dataLen = (uint8_t)(frame[3] - 2U);
    if (dataLen > payloadCapacity || (dataLen != 0U && payload == NULL)) {
        nfc_last_error = PN532_DEBUG_FRAME_TOO_LONG;
        return 0U;
    }

    if (dataLen != 0U) {
        memcpy(payload, &frame[7], dataLen);
    }
    *payloadLen = dataLen;
    nfc_last_error = PN532_DEBUG_OK;
    return 1U;
}

void PN532_SPI_Init(PN532 *dev, SPI_HandleTypeDef *hspi,
                    GPIO_TypeDef *cs_port, uint16_t cs_pin)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (dev == NULL || cs_port == NULL) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return;
    }

    memset(dev, 0, sizeof(*dev));
    dev->hspi = hspi;
    dev->cs_port = cs_port;
    dev->cs_pin = cs_pin;
    dev->inListedTag = 1U;

    /* CubeMX initializes SPI1 before the RTOS. Release it before GPIO bit-bang. */
    if (hspi != NULL) {
        (void)HAL_SPI_DeInit(hspi);
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Pin = NFC_SCK_Pin | NFC_MOSI_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(NFC_SCK_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = NFC_NSS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(NFC_NSS_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = NFC_MISO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(NFC_MISO_GPIO_Port, &GPIO_InitStruct);

    pn532_cs_high(dev);
    pn532_sck_low();
    pn532_mosi_write(0U);
    nfc_last_error = PN532_DEBUG_OK;
}

uint8_t PN532_Begin(PN532 *dev)
{
    if (dev == NULL) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    /* Give power/reset/oscillator plenty of margin before the explicit wake. */
    pn532_cs_high(dev);
    pn532_sck_low();
    pn532_delay(50U);

    /* Wake from LowVbat and immediately enter SAM normal mode. */
    PN532_Wakeup(dev);
    if (!PN532_SAMConfig(dev)) {
        pn532_cs_high(dev);
        return 0U;
    }

    return 1U;
}

void PN532_Wakeup(PN532 *dev)
{
    if (dev == NULL) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return;
    }

    /*
     * PN532 SPI LowVbat wake-up: assert NSS, wait T_osc_start, then transmit
     * the command WITHOUT de-asserting NSS.  PN532_Begin() calls SAMConfig()
     * immediately after this function; pn532_spi_write() therefore continues
     * with NSS already LOW and releases it only after the command frame.
     */
    pn532_cs_high(dev);
    pn532_delay(1U);
    pn532_cs_low(dev);
    pn532_delay(10U);
}

uint8_t PN532_IsReady(PN532 *dev)
{
    uint8_t cmd = PN532_SPI_STATREAD;
    uint8_t reply = 0x00U;

    nfc_debug_ready_error_bits = 0U;
    if (!pn532_spi_write_read(dev, &cmd, 1U, &reply, 1U)) {
        nfc_debug_ready_reply = 0xEEU;
        return 0U;
    }

    nfc_debug_gpioa_idr = GPIOA->IDR;
    nfc_debug_ready_reply = reply;

    if (nfc_ready_history_idx < (uint8_t)sizeof(nfc_ready_history)) {
        nfc_ready_history[nfc_ready_history_idx++] = reply;
    }
    nfc_debug_ready_error_bits = (uint8_t)(reply & PN532_SPI_STATUS_ERROR_MASK);

    if (nfc_debug_ready_error_bits != 0U) {
        nfc_last_error = PN532_DEBUG_SPI_STATUS;
        return 0U;
    }

    /* Healthy, ready PN532 status is exactly 0x01. */
    return (reply == PN532_SPI_READY) ? 1U : 0U;
}

uint8_t PN532_WaitReady(PN532 *dev, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    for (;;) {
        if (PN532_IsReady(dev) != 0U) {
            return 1U;
        }

        if (nfc_debug_ready_error_bits != 0U) {
            /* TR_FE/RCV_OVR is a real transport fault, not a normal NOT READY. */
            nfc_last_error = PN532_DEBUG_SPI_STATUS;
            return 0U;
        }

        if (timeout_ms != PN532_FOREVER_TIMEOUT_MS &&
            (HAL_GetTick() - start) >= timeout_ms) {
            nfc_last_error = PN532_DEBUG_READY_TIMEOUT;
            return 0U;
        }

        pn532_delay(2U);
    }
}

void PN532_ReadData(PN532 *dev, uint8_t *buff, uint8_t n)
{
    uint8_t cmd = PN532_SPI_DATAREAD;

    if (dev == NULL || buff == NULL || n == 0U) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return;
    }

    (void)pn532_spi_write_read(dev, &cmd, 1U, buff, n);
}

void PN532_WriteCommand(PN532 *dev, uint8_t *cmd, uint8_t cmdlen)
{
    uint8_t frame[PN532_PACKBUFFSIZ];
    uint8_t idx = 0U;
    uint8_t checksum;

    if (dev == NULL || cmd == NULL || cmdlen == 0U) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return;
    }

    if ((uint16_t)cmdlen + 9U > sizeof(frame)) {
        nfc_last_error = PN532_DEBUG_TX_TOO_LONG;
        return;
    }

    frame[idx++] = PN532_SPI_DATAWRITE;
    frame[idx++] = PN532_PREAMBLE;
    frame[idx++] = PN532_STARTCODE1;
    frame[idx++] = PN532_STARTCODE2;
    frame[idx++] = (uint8_t)(cmdlen + 1U);
    frame[idx++] = (uint8_t)(0U - (uint8_t)(cmdlen + 1U));
    frame[idx++] = PN532_HOSTTOPN532;

    checksum = PN532_HOSTTOPN532;
    for (uint8_t i = 0U; i < cmdlen; ++i) {
        frame[idx++] = cmd[i];
        checksum = (uint8_t)(checksum + cmd[i]);
    }

    frame[idx++] = (uint8_t)(0U - checksum);
    frame[idx++] = PN532_POSTAMBLE;

    if (pn532_spi_write(dev, frame, idx) != 0U) {
        nfc_last_error = PN532_DEBUG_OK;
    }
}

uint8_t PN532_ReadAck(PN532 *dev)
{
    uint8_t ackbuff[6] = {0U};

    if (dev == NULL) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    PN532_ReadData(dev, ackbuff, sizeof(ackbuff));
    for (uint8_t i = 0U; i < sizeof(ackbuff); ++i) {
        nfc_last_ack[i] = ackbuff[i];
    }

    if (memcmp(ackbuff, pn532ack, sizeof(pn532ack)) != 0) {
        nfc_last_error = PN532_DEBUG_BAD_ACK;
        return 0U;
    }

    nfc_last_error = PN532_DEBUG_OK;
    return 1U;
}

uint8_t PN532_SendCommandCheckAck(PN532 *dev, uint8_t *cmd,
                                  uint8_t cmdlen, uint32_t timeout_ms)
{
    nfc_dbg_cmd_ok = 0U;
    nfc_dbg_ack_ok = 0U;
    nfc_dbg_response_ready = 0U;
    nfc_last_error = PN532_DEBUG_OK;

    nfc_ready_history_idx = 0U;
    for (uint8_t i = 0U; i < (uint8_t)sizeof(nfc_ready_history); ++i) {
        nfc_ready_history[i] = 0U;
    }

    PN532_WriteCommand(dev, cmd, cmdlen);
    if (nfc_last_error != PN532_DEBUG_OK) {
        return 0U;
    }

    if (!PN532_WaitReady(dev, timeout_ms)) {
        return 0U;
    }
    nfc_dbg_cmd_ok = 1U;

    if (!PN532_ReadAck(dev)) {
        return 0U;
    }
    nfc_dbg_ack_ok = 1U;

    /* No guessed 50 ms sleep: poll the documented READY status instead. */
    if (!PN532_WaitReady(dev, timeout_ms)) {
        return 0U;
    }
    nfc_dbg_response_ready = 1U;
    return 1U;
}

uint8_t PN532_SAMConfig(PN532 *dev)
{
    uint8_t *p;
    uint8_t payloadLen = 0U;

    if (dev == NULL) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    p = dev->packetbuffer;
    p[0] = PN532_COMMAND_SAMCONFIGURATION;
    p[1] = 0x01U;  /* Normal mode. */
    p[2] = 0x14U;  /* Timeout = 1 s. */
    p[3] = 0x00U;  /* No IRQ, polling. */

    if (!PN532_SendCommandCheckAck(dev, p, 4U, PN532_DEFAULT_TIMEOUT_MS)) {
        return 0U;
    }

    return pn532_read_response(dev, PN532_RESPONSE_SAMCONFIGURATION,
                               NULL, 0U, &payloadLen);
}

uint32_t PN532_GetFirmwareVersion(PN532 *dev)
{
    uint8_t *p;
    uint8_t payload[8] = {0U};
    uint8_t payloadLen = 0U;
    uint32_t response;

    if (dev == NULL) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    for (uint8_t i = 0U; i < sizeof(nfc_fw_raw); ++i) {
        nfc_fw_raw[i] = 0U;
        nfc_resp_debug[i] = 0U;
    }

    p = dev->packetbuffer;
    p[0] = PN532_COMMAND_GETFIRMWAREVERSION;

    if (!PN532_SendCommandCheckAck(dev, p, 1U, 3000U)) {
        return 0U;
    }

    if (!pn532_read_response(dev, PN532_RESPONSE_GETFIRMWAREVERSION,
                             payload, sizeof(payload), &payloadLen)) {
        return 0U;
    }
    if (payloadLen != 4U) {
        nfc_last_error = PN532_DEBUG_BAD_RESPONSE;
        return 0U;
    }

    /* Preserve complete raw response and the legacy 16-byte debug window. */
    for (uint8_t i = 0U; i < nfc_last_frame_len && i < sizeof(nfc_fw_raw); ++i) {
        nfc_fw_raw[i] = nfc_last_frame[i];
        nfc_resp_debug[i] = nfc_last_frame[i];
    }

    response  = ((uint32_t)payload[0] << 24);
    response |= ((uint32_t)payload[1] << 16);
    response |= ((uint32_t)payload[2] << 8);
    response |= (uint32_t)payload[3];
    return response;
}

uint8_t PN532_SetPassiveActivationRetries(PN532 *dev, uint8_t maxRetries)
{
    uint8_t *p;
    uint8_t payloadLen = 0U;

    if (dev == NULL) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    p = dev->packetbuffer;
    p[0] = PN532_COMMAND_RFCONFIGURATION;
    p[1] = 0x05U;
    p[2] = 0xFFU;
    p[3] = 0x01U;
    p[4] = maxRetries;

    if (!PN532_SendCommandCheckAck(dev, p, 5U, PN532_DEFAULT_TIMEOUT_MS)) {
        return 0U;
    }

    /* Consume D5 33 so no stale response remains before InListPassiveTarget. */
    return pn532_read_response(dev, PN532_RESPONSE_RFCONFIGURATION,
                               NULL, 0U, &payloadLen);
}

uint8_t PN532_ReadPassiveTargetID_Timeout(PN532 *dev, uint8_t cardbaudrate,
                                          uint8_t *uid, uint8_t *uidLength,
                                          uint32_t timeout_ms)
{
    uint8_t *p;

    if (dev == NULL || uid == NULL || uidLength == NULL) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    p = dev->packetbuffer;
    p[0] = PN532_COMMAND_INLISTPASSIVETARGET;
    p[1] = 0x01U;
    p[2] = cardbaudrate;

    if (!PN532_SendCommandCheckAck(dev, p, 3U, timeout_ms)) {
        return 0U;
    }

    return PN532_ReadDetectedPassiveTargetID(dev, uid, uidLength);
}

uint8_t PN532_ReadDetectedPassiveTargetID(PN532 *dev, uint8_t *uid,
                                          uint8_t *uidLength)
{
    uint8_t payload[PN532_PACKBUFFSIZ] = {0U};
    uint8_t payloadLen = 0U;
    uint8_t nfcidLen;

    if (dev == NULL || uid == NULL || uidLength == NULL) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    if (!pn532_read_response(dev, PN532_RESPONSE_INLISTPASSIVETARGET,
                             payload, sizeof(payload), &payloadLen)) {
        return 0U;
    }

    /* NbTg,Tg,SENS_RES[2],SEL_RES,NFCIDLength => six bytes before UID. */
    if (payloadLen < 6U || payload[0] != 1U) {
        nfc_last_error = PN532_DEBUG_BAD_RESPONSE;
        return 0U;
    }

    nfcidLen = payload[5];
    if ((nfcidLen != 4U && nfcidLen != 7U) ||
        payloadLen < (uint8_t)(6U + nfcidLen)) {
        nfc_last_error = PN532_DEBUG_UID_LENGTH;
        return 0U;
    }

    dev->inListedTag = payload[1];
    dev->lastAtqa = (uint16_t)(((uint16_t)payload[2] << 8) | payload[3]);
    dev->lastSak = payload[4];
    *uidLength = nfcidLen;
    memcpy(uid, &payload[6], nfcidLen);
    nfc_last_error = PN532_DEBUG_OK;
    return 1U;
}

uint8_t PN532_MifareClassic_AuthenticateBlock(PN532 *dev, uint8_t *uid,
                                               uint8_t uidLen,
                                               uint32_t blockNumber,
                                               uint8_t keyNumber,
                                               uint8_t *keyData)
{
    uint8_t *p;
    uint8_t payload[4] = {0U};
    uint8_t payloadLen = 0U;
    const uint8_t *uid4;

    if (dev == NULL || uid == NULL || keyData == NULL || blockNumber > 255U) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    /* MIFARE auth carries four UID bytes.  For a 7-byte UID use the last four. */
    if (uidLen == 4U) {
        uid4 = uid;
    } else if (uidLen == 7U) {
        uid4 = &uid[3];
    } else {
        nfc_last_error = PN532_DEBUG_UID_LENGTH;
        return 0U;
    }

    p = dev->packetbuffer;
    p[0] = PN532_COMMAND_INDATAEXCHANGE;
    p[1] = dev->inListedTag;
    p[2] = (keyNumber == 0U) ? MIFARE_CMD_AUTH_A : MIFARE_CMD_AUTH_B;
    p[3] = (uint8_t)blockNumber;
    memcpy(&p[4], keyData, 6U);
    memcpy(&p[10], uid4, 4U);

    if (!PN532_SendCommandCheckAck(dev, p, 14U, PN532_DEFAULT_TIMEOUT_MS)) {
        return 0U;
    }
    if (!pn532_read_response(dev, PN532_RESPONSE_INDATAEXCHANGE,
                             payload, sizeof(payload), &payloadLen)) {
        return 0U;
    }
    if (payloadLen < 1U || payload[0] != 0x00U) {
        nfc_last_error = PN532_DEBUG_PN532_STATUS;
        return 0U;
    }

    nfc_last_error = PN532_DEBUG_OK;
    return 1U;
}

uint8_t PN532_MifareClassic_ReadDataBlock(PN532 *dev, uint8_t blockNumber,
                                          uint8_t *data)
{
    uint8_t *p;
    uint8_t payload[20] = {0U};
    uint8_t payloadLen = 0U;

    if (dev == NULL || data == NULL) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    p = dev->packetbuffer;
    p[0] = PN532_COMMAND_INDATAEXCHANGE;
    p[1] = dev->inListedTag;
    p[2] = MIFARE_CMD_READ;
    p[3] = blockNumber;

    if (!PN532_SendCommandCheckAck(dev, p, 4U, PN532_DEFAULT_TIMEOUT_MS)) {
        return 0U;
    }
    if (!pn532_read_response(dev, PN532_RESPONSE_INDATAEXCHANGE,
                             payload, sizeof(payload), &payloadLen)) {
        return 0U;
    }
    if (payloadLen < 17U || payload[0] != 0x00U) {
        nfc_last_error = PN532_DEBUG_PN532_STATUS;
        return 0U;
    }

    memcpy(data, &payload[1], 16U);
    nfc_last_error = PN532_DEBUG_OK;
    return 1U;
}

uint8_t PN532_MifareClassic_WriteDataBlock(PN532 *dev, uint8_t blockNumber,
                                           uint8_t *data)
{
    uint8_t *p;
    uint8_t payload[4] = {0U};
    uint8_t payloadLen = 0U;

    if (dev == NULL || data == NULL) {
        nfc_last_error = PN532_DEBUG_BAD_ARGUMENT;
        return 0U;
    }

    p = dev->packetbuffer;
    p[0] = PN532_COMMAND_INDATAEXCHANGE;
    p[1] = dev->inListedTag;
    p[2] = MIFARE_CMD_WRITE;
    p[3] = blockNumber;
    memcpy(&p[4], data, 16U);

    if (!PN532_SendCommandCheckAck(dev, p, 20U, PN532_DEFAULT_TIMEOUT_MS)) {
        return 0U;
    }
    if (!pn532_read_response(dev, PN532_RESPONSE_INDATAEXCHANGE,
                             payload, sizeof(payload), &payloadLen)) {
        return 0U;
    }
    if (payloadLen < 1U || payload[0] != 0x00U) {
        nfc_last_error = PN532_DEBUG_PN532_STATUS;
        return 0U;
    }

    nfc_last_error = PN532_DEBUG_OK;
    return 1U;
}

void PN532_SPI_BusTest(PN532 *dev)
{
    if (dev == NULL)
    {
        return;
    }

    /*
     * Estado conocido.
     */
    pn532_sck_low();
    pn532_mosi_write(0U);

    /*
     * =========================================================
     * MISO con CS HIGH
     * =========================================================
     */

    pn532_cs_high(dev);

    HAL_Delay(10);

    nfc_test_gpioa_idr_cs_high = GPIOA->IDR;

    nfc_test_miso_cs_high =
        (HAL_GPIO_ReadPin(NFC_MISO_GPIO_Port,
                          NFC_MISO_Pin) == GPIO_PIN_SET)
        ? 1U
        : 0U;

    /*
     * =========================================================
     * MISO con CS LOW
     * =========================================================
     */

    pn532_cs_low(dev);

    HAL_Delay(10);

    nfc_test_gpioa_idr_cs_low = GPIOA->IDR;

    nfc_test_miso_cs_low =
        (HAL_GPIO_ReadPin(NFC_MISO_GPIO_Port,
                          NFC_MISO_Pin) == GPIO_PIN_SET)
        ? 1U
        : 0U;

    pn532_cs_high(dev);

    HAL_Delay(10);

    /*
     * =========================================================
     * Leer STATREAD 16 veces directamente.
     *
     * SIN parser.
     * SIN WaitReady.
     * SIN SAM.
     * =========================================================
     */

    for (uint8_t n = 0U; n < 16U; n++)
    {
        uint8_t status;

        pn532_cs_low(dev);

        pn532_bb_delay();

        /*
         * SPI command:
         * 0x02 = STATREAD
         */
        (void)pn532_bb_transfer(0x02U);

        /*
         * Segundo byte: clocks para recibir SPIstatus.
         */
        status = pn532_bb_transfer(0x00U);

        pn532_cs_high(dev);

        nfc_test_stat_raw[n] = status;

        HAL_Delay(2);
    }
}
