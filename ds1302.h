#ifndef __DS1302_H__
#define __DS1302_H__

#include "common.h"

void DS1302_Init(void);
void DS1302_Write(unsigned char addr, unsigned char dat);
unsigned char DS1302_Read(unsigned char addr);
void DS1302_SetTime(unsigned char *t);
void DS1302_ReadTime(unsigned char *t);
// Read/Write DS1302 RAM (ram index 0..30)
unsigned char DS1302_ReadRam(unsigned char ram_index);
void DS1302_WriteRam(unsigned char ram_index, unsigned char dat);

#endif