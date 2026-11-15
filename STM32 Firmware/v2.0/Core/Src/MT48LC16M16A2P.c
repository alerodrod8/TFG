#include "MT48LC16M16A2P.h"

/* Accesos volátiles para evitar optimizaciones */
#define SDRAM_U8(addr)   (*(volatile uint8_t  *)(addr))
#define SDRAM_U16(addr)  (*(volatile uint16_t *)(addr))
#define SDRAM_U32(addr)  (*(volatile uint32_t *)(addr))

volatile SDRAM_TestResult_t sdram_result;
volatile uint8_t sdram_test_done = 0;
volatile uint8_t sdram_test_passed = 0;

void sdramTask(void *argument)
{
  (void)argument;

  /*
   * Espera opcional para dejar estabilizar el sistema.
   * En FreeRTOS, aquí se usa vTaskDelay, no HAL_Delay.
   */
  vTaskDelay(100);

  sdram_test_done = 0;
  sdram_test_passed = 0;

  sdram_result = SDRAM_Test_RunAll();

  sdram_test_done = 1;

  if (sdram_result.status == SDRAM_TEST_OK)
  {
    sdram_test_passed = 1;
  }
  else
  {
    sdram_test_passed = 0;

    /*
     * Breakpoint aquí.
     *
     * Mira:
     * sdram_result.status
     * sdram_result.fail_address
     * sdram_result.expected
     * sdram_result.received
     * sdram_result.index
     * sdram_result.detail
     */
    __NOP();
  }

  while (1)
  {
    vTaskDelay(1000);
  }
}

static SDRAM_TestResult_t SDRAM_Test_MakeOk(void)
{
    SDRAM_TestResult_t r;

    r.status = SDRAM_TEST_OK;
    r.fail_address = 0;
    r.expected = 0;
    r.received = 0;
    r.index = 0;
    r.detail = 0;

    return r;
}

static SDRAM_TestResult_t SDRAM_Test_MakeError(SDRAM_TestStatus_t status,
                                               uint32_t address,
                                               uint32_t expected,
                                               uint32_t received,
                                               uint32_t index,
                                               uint32_t detail)
{
    SDRAM_TestResult_t r;

    r.status = status;
    r.fail_address = address;
    r.expected = expected;
    r.received = received;
    r.index = index;
    r.detail = detail;

    return r;
}

void SDRAM_Test_ClearResult(SDRAM_TestResult_t *result)
{
    if (result == NULL)
    {
        return;
    }

    result->status = SDRAM_TEST_OK;
    result->fail_address = 0;
    result->expected = 0;
    result->received = 0;
    result->index = 0;
    result->detail = 0;
}

/*
 * Test de bus de datos de 16 bits.
 *
 * Objetivo:
 * - Detectar DQ pegadas a 0/1
 * - Detectar bits de datos cruzados
 * - Detectar problemas claros en D0..D15
 *
 * Escribe walking 1 y walking 0 en la misma dirección.
 */
SDRAM_TestResult_t SDRAM_Test_DataBus16(void)
{
    volatile uint16_t *mem16 = (volatile uint16_t *)SDRAM_BASE_ADDR;

    uint32_t bit;
    uint16_t expected;
    uint16_t received;

    /*
     * Walking 1
     */
    for (bit = 0; bit < 16; bit++)
    {
        expected = (uint16_t)(1U << bit);
        mem16[0] = expected;

        __DSB();
        received = mem16[0];

        if (received != expected)
        {
            return SDRAM_Test_MakeError(SDRAM_TEST_DATA_BUS_ERROR,
                                        SDRAM_BASE_ADDR,
                                        expected,
                                        received,
                                        bit,
                                        0x00000001UL);
        }
    }

    /*
     * Walking 0
     */
    for (bit = 0; bit < 16; bit++)
    {
        expected = (uint16_t)(~(1U << bit));
        mem16[0] = expected;

        __DSB();
        received = mem16[0];

        if (received != expected)
        {
            return SDRAM_Test_MakeError(SDRAM_TEST_DATA_BUS_ERROR,
                                        SDRAM_BASE_ADDR,
                                        expected,
                                        received,
                                        bit,
                                        0x00000000UL);
        }
    }

    /*
     * Patrones alternos
     */
    mem16[0] = 0xAAAA;
    __DSB();
    received = mem16[0];

    if (received != 0xAAAA)
    {
        return SDRAM_Test_MakeError(SDRAM_TEST_DATA_BUS_ERROR,
                                    SDRAM_BASE_ADDR,
                                    0xAAAA,
                                    received,
                                    0,
                                    0xAAAA);
    }

    mem16[0] = 0x5555;
    __DSB();
    received = mem16[0];

    if (received != 0x5555)
    {
        return SDRAM_Test_MakeError(SDRAM_TEST_DATA_BUS_ERROR,
                                    SDRAM_BASE_ADDR,
                                    0x5555,
                                    received,
                                    0,
                                    0x5555);
    }

    return SDRAM_Test_MakeOk();
}

/*
 * Test de bus de direcciones.
 *
 * Objetivo:
 * - Detectar líneas A pegadas
 * - Detectar alias de direcciones
 * - Detectar direcciones cruzadas
 *
 * Usa offsets potencia de 2.
 */
SDRAM_TestResult_t SDRAM_Test_AddressBus(void)
{
    volatile uint32_t *base = (volatile uint32_t *)SDRAM_BASE_ADDR;

    const uint32_t pattern     = 0xAAAAAAAAUL;
    const uint32_t antipattern = 0x55555555UL;

    uint32_t offset;
    uint32_t test_offset;
    uint32_t received;

    /*
     * Trabajamos en words de 32 bits.
     * 32 MiB / 4 = 8M words.
     */
    const uint32_t total_words = SDRAM_SIZE_BYTES / 4UL;

    /*
     * Inicializa todas las posiciones potencia de 2 con pattern.
     */
    for (offset = 1; offset < total_words; offset <<= 1)
    {
        base[offset] = pattern;
    }

    /*
     * Escribe antipattern en base[0].
     * Si alguna línea de dirección está pegada o aliasa con 0,
     * alguna posición potencia de 2 cambiará.
     */
    base[0] = antipattern;
    __DSB();

    for (offset = 1; offset < total_words; offset <<= 1)
    {
        received = base[offset];

        if (received != pattern)
        {
            return SDRAM_Test_MakeError(SDRAM_TEST_ADDRESS_BUS_ERROR,
                                        SDRAM_BASE_ADDR + (offset * 4UL),
                                        pattern,
                                        received,
                                        offset,
                                        0x00000001UL);
        }
    }

    /*
     * Ahora escribe pattern en base[0] y prueba cada offset individualmente
     * con antipattern.
     */
    base[0] = pattern;

    for (test_offset = 1; test_offset < total_words; test_offset <<= 1)
    {
        base[test_offset] = antipattern;
        __DSB();

        received = base[0];

        if (received != pattern)
        {
            return SDRAM_Test_MakeError(SDRAM_TEST_ADDRESS_BUS_ERROR,
                                        SDRAM_BASE_ADDR,
                                        pattern,
                                        received,
                                        test_offset,
                                        0x00000002UL);
        }

        for (offset = 1; offset < total_words; offset <<= 1)
        {
            if (offset == test_offset)
            {
                continue;
            }

            received = base[offset];

            if (received != pattern)
            {
                return SDRAM_Test_MakeError(SDRAM_TEST_ADDRESS_BUS_ERROR,
                                            SDRAM_BASE_ADDR + (offset * 4UL),
                                            pattern,
                                            received,
                                            test_offset,
                                            offset);
            }
        }

        base[test_offset] = pattern;
    }

    return SDRAM_Test_MakeOk();
}

/*
 * Test rápido de patrones en el primer 1 MiB.
 *
 * Si este falla con errores a índices concretos, normalmente apunta a:
 * - timing
 * - integridad de señal
 * - DCache/MPU
 * - alguna DQ inestable
 */
SDRAM_TestResult_t SDRAM_Test_QuickPattern32(void)
{
    volatile uint32_t *mem32 = (volatile uint32_t *)SDRAM_BASE_ADDR;

    uint32_t i;
    uint32_t expected;
    uint32_t received;

    /*
     * Patrón dependiente de índice.
     */
    for (i = 0; i < SDRAM_QUICK_TEST_WORDS; i++)
    {
        mem32[i] = 0xA5A50000UL ^ i;
    }

    __DSB();

    for (i = 0; i < SDRAM_QUICK_TEST_WORDS; i++)
    {
        expected = 0xA5A50000UL ^ i;
        received = mem32[i];

        if (received != expected)
        {
            return SDRAM_Test_MakeError(SDRAM_TEST_PATTERN_ERROR,
                                        SDRAM_BASE_ADDR + (i * 4UL),
                                        expected,
                                        received,
                                        i,
                                        0xA5A50000UL);
        }
    }

    /*
     * Patrón inverso.
     */
    for (i = 0; i < SDRAM_QUICK_TEST_WORDS; i++)
    {
        mem32[i] = 0x5A5A0000UL ^ (~i);
    }

    __DSB();

    for (i = 0; i < SDRAM_QUICK_TEST_WORDS; i++)
    {
        expected = 0x5A5A0000UL ^ (~i);
        received = mem32[i];

        if (received != expected)
        {
            return SDRAM_Test_MakeError(SDRAM_TEST_PATTERN_ERROR,
                                        SDRAM_BASE_ADDR + (i * 4UL),
                                        expected,
                                        received,
                                        i,
                                        0x5A5A0000UL);
        }
    }

    /*
     * Patrones fijos.
     */
    for (i = 0; i < SDRAM_QUICK_TEST_WORDS; i++)
    {
        mem32[i] = 0x00000000UL;
    }

    __DSB();

    for (i = 0; i < SDRAM_QUICK_TEST_WORDS; i++)
    {
        received = mem32[i];

        if (received != 0x00000000UL)
        {
            return SDRAM_Test_MakeError(SDRAM_TEST_PATTERN_ERROR,
                                        SDRAM_BASE_ADDR + (i * 4UL),
                                        0x00000000UL,
                                        received,
                                        i,
                                        0x00000000UL);
        }
    }

    for (i = 0; i < SDRAM_QUICK_TEST_WORDS; i++)
    {
        mem32[i] = 0xFFFFFFFFUL;
    }

    __DSB();

    for (i = 0; i < SDRAM_QUICK_TEST_WORDS; i++)
    {
        received = mem32[i];

        if (received != 0xFFFFFFFFUL)
        {
            return SDRAM_Test_MakeError(SDRAM_TEST_PATTERN_ERROR,
                                        SDRAM_BASE_ADDR + (i * 4UL),
                                        0xFFFFFFFFUL,
                                        received,
                                        i,
                                        0xFFFFFFFFUL);
        }
    }

    return SDRAM_Test_MakeOk();
}

/*
 * Test de accesos de byte y halfword.
 *
 * Objetivo:
 * - Comprobar DQM/NBL0/NBL1
 * - Comprobar accesos de 8/16 bits
 * - Comprobar endianess esperada
 */
SDRAM_TestResult_t SDRAM_Test_ByteHalfwordAccess(void)
{
    uint32_t addr;
    uint32_t received32;
    uint16_t received16;
    uint8_t received8;

    addr = SDRAM_BASE_ADDR + 0x1000UL;

    /*
     * Limpieza inicial.
     */
    SDRAM_U32(addr) = 0x00000000UL;
    __DSB();

    /*
     * Escritura byte a byte.
     */
    SDRAM_U8(addr + 0) = 0x12;
    SDRAM_U8(addr + 1) = 0x34;
    SDRAM_U8(addr + 2) = 0x56;
    SDRAM_U8(addr + 3) = 0x78;
    __DSB();

    received32 = SDRAM_U32(addr);

    /*
     * Cortex-M es little endian:
     * bytes 12 34 56 78 -> word 0x78563412.
     */
    if (received32 != 0x78563412UL)
    {
        return SDRAM_Test_MakeError(SDRAM_TEST_BYTE_ERROR,
                                    addr,
                                    0x78563412UL,
                                    received32,
                                    0,
                                    0x00000008UL);
    }

    /*
     * Escritura halfword baja.
     */
    SDRAM_U32(addr) = 0x00000000UL;
    __DSB();

    SDRAM_U16(addr) = 0xABCD;
    __DSB();

    received32 = SDRAM_U32(addr);

    if (received32 != 0x0000ABCDUL)
    {
        return SDRAM_Test_MakeError(SDRAM_TEST_HALFWORD_ERROR,
                                    addr,
                                    0x0000ABCDUL,
                                    received32,
                                    0,
                                    0x00000010UL);
    }

    /*
     * Escritura halfword alta.
     */
    SDRAM_U32(addr) = 0x00000000UL;
    __DSB();

    SDRAM_U16(addr + 2) = 0x5678;
    __DSB();

    received32 = SDRAM_U32(addr);

    if (received32 != 0x56780000UL)
    {
        return SDRAM_Test_MakeError(SDRAM_TEST_HALFWORD_ERROR,
                                    addr,
                                    0x56780000UL,
                                    received32,
                                    0,
                                    0x00000011UL);
    }

    /*
     * Lectura byte individual.
     */
    SDRAM_U32(addr) = 0x44332211UL;
    __DSB();

    received8 = SDRAM_U8(addr + 0);
    if (received8 != 0x11)
    {
        return SDRAM_Test_MakeError(SDRAM_TEST_BYTE_ERROR,
                                    addr + 0,
                                    0x11,
                                    received8,
                                    0,
                                    0x00000020UL);
    }

    received8 = SDRAM_U8(addr + 1);
    if (received8 != 0x22)
    {
        return SDRAM_Test_MakeError(SDRAM_TEST_BYTE_ERROR,
                                    addr + 1,
                                    0x22,
                                    received8,
                                    1,
                                    0x00000021UL);
    }

    received8 = SDRAM_U8(addr + 2);
    if (received8 != 0x33)
    {
        return SDRAM_Test_MakeError(SDRAM_TEST_BYTE_ERROR,
                                    addr + 2,
                                    0x33,
                                    received8,
                                    2,
                                    0x00000022UL);
    }

    received8 = SDRAM_U8(addr + 3);
    if (received8 != 0x44)
    {
        return SDRAM_Test_MakeError(SDRAM_TEST_BYTE_ERROR,
                                    addr + 3,
                                    0x44,
                                    received8,
                                    3,
                                    0x00000023UL);
    }

    /*
     * Lectura halfword individual.
     */
    received16 = SDRAM_U16(addr + 0);
    if (received16 != 0x2211)
    {
        return SDRAM_Test_MakeError(SDRAM_TEST_HALFWORD_ERROR,
                                    addr + 0,
                                    0x2211,
                                    received16,
                                    0,
                                    0x00000030UL);
    }

    received16 = SDRAM_U16(addr + 2);
    if (received16 != 0x4433)
    {
        return SDRAM_Test_MakeError(SDRAM_TEST_HALFWORD_ERROR,
                                    addr + 2,
                                    0x4433,
                                    received16,
                                    2,
                                    0x00000031UL);
    }

    return SDRAM_Test_MakeOk();
}

/*
 * Test por saltos en toda la SDRAM.
 *
 * No escribe los 32 MB completos.
 * Toca muchas zonas para ver si hay problemas de líneas altas de dirección.
 */
SDRAM_TestResult_t SDRAM_Test_StrideFullMemory(void)
{
    volatile uint32_t *mem32 = (volatile uint32_t *)SDRAM_BASE_ADDR;

    const uint32_t total_words = SDRAM_SIZE_BYTES / 4UL;

    /*
     * 1024 words = 4096 bytes.
     * Toca una palabra cada 4 KiB.
     */
    const uint32_t stride_words = 1024UL;

    uint32_t i;
    uint32_t expected;
    uint32_t received;

    for (i = 0; i < total_words; i += stride_words)
    {
        mem32[i] = 0xC0DE0000UL ^ i;
    }

    __DSB();

    for (i = 0; i < total_words; i += stride_words)
    {
        expected = 0xC0DE0000UL ^ i;
        received = mem32[i];

        if (received != expected)
        {
            return SDRAM_Test_MakeError(SDRAM_TEST_STRIDE_ERROR,
                                        SDRAM_BASE_ADDR + (i * 4UL),
                                        expected,
                                        received,
                                        i,
                                        stride_words);
        }
    }

    return SDRAM_Test_MakeOk();
}

/*
 * Ejecuta todos los tests en orden.
 *
 * El orden está pensado para diagnosticar:
 *
 * 1. Data bus 16:
 *    Si falla aquí, sospecha DQ0..DQ15, soldadura, NBL, alimentación.
 *
 * 2. Address bus:
 *    Si falla aquí, sospecha A0..A12, BA0/BA1, geometría FMC.
 *
 * 3. Byte/Halfword:
 *    Si falla aquí, sospecha NBL0/NBL1, DQML/DQMH, byte enables.
 *
 * 4. Quick pattern:
 *    Si falla aquí, sospecha timing, SI, refresh, cache/MPU.
 *
 * 5. Stride full:
 *    Si falla aquí, sospecha direcciones altas o tamaño/geometría.
 */
SDRAM_TestResult_t SDRAM_Test_RunAll(void)
{
    SDRAM_TestResult_t result;

    result = SDRAM_Test_DataBus16();
    if (result.status != SDRAM_TEST_OK)
    {
        return result;
    }

    result = SDRAM_Test_AddressBus();
    if (result.status != SDRAM_TEST_OK)
    {
        return result;
    }

    result = SDRAM_Test_DQ4_Stress();
    if (result.status != SDRAM_TEST_OK)
    {
        return result;
    }

    result = SDRAM_Test_HalfwordOnlyAccess();
    if (result.status != SDRAM_TEST_OK)
    {
        return result;
    }

//    result = SDRAM_Test_ByteHalfwordAccess();
//    if (result.status != SDRAM_TEST_OK)
//    {
//        return result;
//    }

    result = SDRAM_Test_QuickPattern32();
    if (result.status != SDRAM_TEST_OK)
    {
        return result;
    }

    result = SDRAM_Test_StrideFullMemory();
    if (result.status != SDRAM_TEST_OK)
    {
        return result;
    }

    return SDRAM_Test_MakeOk();
}

SDRAM_TestResult_t SDRAM_Test_DQ4_Stress(void)
{
    volatile uint16_t *mem16 = (volatile uint16_t *)SDRAM_BASE_ADDR;
    uint32_t i;
    uint16_t expected;
    uint16_t received;

    for (i = 0; i < 65536; i++)
    {
        mem16[i] = 0x0010;
    }

    __DSB();

    for (i = 0; i < 65536; i++)
    {
        expected = 0x0010;
        received = mem16[i];

        if (received != expected)
        {
            return SDRAM_Test_MakeError(SDRAM_TEST_PATTERN_ERROR,
                                        SDRAM_BASE_ADDR + (i * 2UL),
                                        expected,
                                        received,
                                        i,
                                        0x00000010UL);
        }
    }

    for (i = 0; i < 65536; i++)
    {
        mem16[i] = 0xFFEF;
    }

    __DSB();

    for (i = 0; i < 65536; i++)
    {
        expected = 0xFFEF;
        received = mem16[i];

        if (received != expected)
        {
            return SDRAM_Test_MakeError(SDRAM_TEST_PATTERN_ERROR,
                                        SDRAM_BASE_ADDR + (i * 2UL),
                                        expected,
                                        received,
                                        i,
                                        0x0000FFEFUL);
        }
    }

    return SDRAM_Test_MakeOk();
}

SDRAM_TestResult_t SDRAM_Test_HalfwordOnlyAccess(void)
{
    uint32_t addr = SDRAM_BASE_ADDR + 0x1000UL;
    uint32_t received32;

    SDRAM_U32(addr) = 0x00000000UL;
    __DSB();

    SDRAM_U16(addr + 0) = 0x3412;
    __DSB();

    SDRAM_U16(addr + 2) = 0x7856;
    __DSB();

    received32 = SDRAM_U32(addr);

    if (received32 != 0x78563412UL)
    {
        return SDRAM_Test_MakeError(SDRAM_TEST_HALFWORD_ERROR,
                                    addr,
                                    0x78563412UL,
                                    received32,
                                    0,
                                    0x00000040UL);
    }

    return SDRAM_Test_MakeOk();
}
