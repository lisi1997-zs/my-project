#ifndef __LCD1602_H__
#define __LCD1602_H__
#include "common.h"

void LCD_Init(void);
void LCD_WriteCmd(unsigned char cmd);
void LCD_WriteData(unsigned char dat);
void LCD_ShowString(unsigned char row, unsigned char col, char *str);
void LCD_ShowNum(unsigned char row, unsigned char col, unsigned int num, unsigned char len);

#endif