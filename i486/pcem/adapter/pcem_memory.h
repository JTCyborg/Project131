#ifndef I486_PCEM_ADAPTER_MEMORY_H
#define I486_PCEM_ADAPTER_MEMORY_H

/*
 * Narrow memory contract for the imported PCem CPU interpreter.
 * The implementation belongs to i486/ and must not use Spectrum/ATM memory.
 */
#include <stdint.h>
#include <stddef.h>

extern uintptr_t *readlookup2;
extern uintptr_t *writelookup2;

uint8_t readmembl(uint32_t address);
uint16_t readmemwl(uint32_t address);
uint32_t readmemll(uint32_t address);
uint64_t readmemql(uint32_t address);

void writemembl(uint32_t address, uint8_t value);
void writememwl(uint32_t address, uint16_t value);
void writememll(uint32_t address, uint32_t value);
void writememql(uint32_t address, uint64_t value);

uint8_t *getpccache(uint32_t address);

#endif
