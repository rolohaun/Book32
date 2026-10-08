#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool inkGenesisOpenGame(uint8_t* rom,size_t bytes);
void inkGenesisCloseGame(void);
bool inkGenesisFrame(uint16_t buttons,uint8_t* shades);
bool inkGenesisStep(uint16_t buttons,uint8_t* shades,bool render);
bool inkGenesisImage(uint8_t* shades);
bool inkGenesisRestart(void);
unsigned inkGenesisPeriod(void);
const char* inkGenesisError(void);
size_t inkGenesisStateSize(void);
bool inkGenesisStateExport(void* state,size_t bytes);
bool inkGenesisStateImport(const void* state,size_t bytes);
