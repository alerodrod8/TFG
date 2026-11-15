#ifndef SDRAM_TEST_H
#define SDRAM_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f7xx_hal.h"
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

/* Bank 1 SDRAM FMC base address */
#define SDRAM_BASE_ADDR        ((uint32_t)0xC0000000UL)

/*
 * MT48LC16M16A2:
 * 256 Mbit = 32 MiB
 */
#define SDRAM_SIZE_BYTES       ((uint32_t)(32UL * 1024UL * 1024UL))

/*
 * Para test rápido inicial.
 * 256K words x 4 bytes = 1 MiB
 */
#define SDRAM_QUICK_TEST_WORDS ((uint32_t)(256UL * 1024UL))

typedef enum
{
    SDRAM_TEST_OK = 0,

    SDRAM_TEST_DATA_BUS_ERROR,
    SDRAM_TEST_ADDRESS_BUS_ERROR,
    SDRAM_TEST_PATTERN_ERROR,
    SDRAM_TEST_HALFWORD_ERROR,
    SDRAM_TEST_BYTE_ERROR,
    SDRAM_TEST_STRIDE_ERROR,
    SDRAM_TEST_FULL_ERROR

} SDRAM_TestStatus_t;

typedef struct
{
    SDRAM_TestStatus_t status;

    uint32_t fail_address;
    uint32_t expected;
    uint32_t received;
    uint32_t index;

    /*
     * Información extra para depurar.
     * Por ejemplo:
     * - bit de datos que falla
     * - offset de dirección
     * - patrón usado
     */
    uint32_t detail;

} SDRAM_TestResult_t;

void SDRAM_Test_ClearResult(SDRAM_TestResult_t *result);

void sdramTask(void *argument);
SDRAM_TestResult_t SDRAM_Test_DataBus16(void);
SDRAM_TestResult_t SDRAM_Test_AddressBus(void);
SDRAM_TestResult_t SDRAM_Test_QuickPattern32(void);
SDRAM_TestResult_t SDRAM_Test_ByteHalfwordAccess(void);
SDRAM_TestResult_t SDRAM_Test_StrideFullMemory(void);
SDRAM_TestResult_t SDRAM_Test_DQ4_Stress(void);
SDRAM_TestResult_t SDRAM_Test_HalfwordOnlyAccess(void);

/*
 * Test principal recomendado.
 * Ejecuta varios tests en orden y devuelve el primer fallo.
 */
SDRAM_TestResult_t SDRAM_Test_RunAll(void);

#ifdef __cplusplus
}
#endif

#endif
