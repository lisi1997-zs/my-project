#include "reg52.h"
#include "lcd1602.h"
#include <math.h>  // 添加数学头文件

sbit RS = P2^6;
sbit RW = P2^5;
sbit EN = P2^7;
#define LCD_PORT P0

void DelayUs(unsigned int us){ while(us--); }

void LCD_WriteCmd(unsigned char cmd){
    RS=0; RW=0; LCD_PORT=cmd; EN=1; 
    DelayUs(50); 
    EN=0;
    
    // 【新增】如果是清屏指令，额外多等一会
    // 0x01 是清屏，0x02 是光标归位，这两个特别慢
    if(cmd == 0x01 || cmd == 0x02) {
        // 这里需要一个毫秒级的延时，由于你在 lcd1602.c 里只有 DelayUs
        // 可以简单地调用多次 DelayUs，或者直接用循环
        unsigned int i = 2000; 
        while(i--); // 强行延时约 2ms
    }
}

void LCD_WriteData(unsigned char dat){
    RS=1; RW=0; LCD_PORT=dat; EN=1; DelayUs(50); EN=0;
}

void LCD_ShowString(unsigned char row, unsigned char col, char *str){
    unsigned char addr;
    addr = (row ? 0x40 : 0x00) + col;
    LCD_WriteCmd(0x80 | addr);
    while(*str) LCD_WriteData(*str++);
}

void LCD_ShowNum(unsigned char row, unsigned char col, unsigned int num, unsigned char len){
    unsigned char i;
    char buffer[6];  // 固定大小的缓冲区
    
    // 确保数字在合理范围内
    if(len > 5) len = 5;
    
    // 将数字转换为字符串
    for(i = 0; i < len; i++){
        buffer[len - i - 1] = (num % 10) + '0';
        num /= 10;
    }
    buffer[len] = '\0';  // 字符串结束符
    
    // 显示字符串
    LCD_ShowString(row, col, buffer);
}


void LCD_Init(void){
    LCD_WriteCmd(0x38);
    LCD_WriteCmd(0x0C);
    LCD_WriteCmd(0x06);
    LCD_WriteCmd(0x01);
}