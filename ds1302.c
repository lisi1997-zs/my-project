#include "reg52.h"
#include "ds1302.h"

// =========================================================
// ⚠警告：请务必确认以下引脚与你的开发板原理图一致！
// 如果你的“简单代码”里用的不是这几个脚，请必须改掉！
// =========================================================
sbit DS1302_CLK = P3^6;  // 对应你的 SCK
sbit DS1302_IO  = P3^4;  // 对应你的 IO/DAT
sbit DS1302_RST = P3^5;  // 对应你的 RST/CE

// 简单的短延时，确保波形稳定
void DS1302_Delay(void) {
    unsigned char i;
    for(i = 0; i < 2; i++); 
}

// 标准的单字节写入（上升沿写入）
void DS1302_WriteByte(unsigned char dat) {
    unsigned char i;
    for(i = 0; i < 8; i++) {
        DS1302_IO = dat & 0x01; // 1. 先准备数据
        DS1302_Delay();
        DS1302_CLK = 1;         // 2. 拉高时钟（写入数据）
        DS1302_Delay();
        DS1302_CLK = 0;         // 3. 拉低时钟
        DS1302_Delay();
        dat >>= 1;              // 4. 移位
    }
}

// 标准的单字节读取（下降沿读取数据有效性）
unsigned char DS1302_ReadByte(void) {
    unsigned char i, dat = 0;
    for(i = 0; i < 8; i++) {
        dat >>= 1;
        // DS1302 在时钟下降沿后输出数据，所以此时直接读取
        if(DS1302_IO) {
            dat |= 0x80;
        }
        
        // 产生一个时钟脉冲，为下一位数据做准备
        DS1302_CLK = 1;
        DS1302_Delay();
        DS1302_CLK = 0;
        DS1302_Delay();
    }
    return dat;
}

// 写入寄存器：先写命令，再写数据
void DS1302_Write(unsigned char addr, unsigned char dat) {
    DS1302_RST = 0;
    DS1302_CLK = 0;
    DS1302_RST = 1; // 开启通信
    
    DS1302_WriteByte(addr); // 写地址
    DS1302_WriteByte(dat);  // 写数据
    
    DS1302_RST = 0; // 结束通信
}

// 读取寄存器：先写命令，再读数据
unsigned char DS1302_Read(unsigned char addr) {
    unsigned char temp;
    DS1302_RST = 0;
    DS1302_CLK = 0;
    DS1302_RST = 1; // 开启通信
    
    DS1302_WriteByte(addr); // 写地址
    temp = DS1302_ReadByte(); // 读数据
    
    DS1302_RST = 0; // 结束通信
    // 注意：读取完后必须把 IO 口复位，防止干扰
    DS1302_CLK = 0;
    return temp;
}

// 初始化 DS1302 (解决时钟暂停问题)
void DS1302_Init(void) {
    unsigned char sec;
    
    // 1. 解除写保护
    DS1302_Write(0x8E, 0x00);
    
    // 2. 读取秒寄存器，检查是否处于 Halt (暂停) 状态
    sec = DS1302_Read(0x81);
    if(sec & 0x80) { // 如果最高位是1，说明时钟停了
        DS1302_Write(0x80, 0x00); // 写入 0 秒，并启动时钟
    }
    
    // 3. 再次解除写保护 (为了后续正常写入时间)
    DS1302_Write(0x8E, 0x00);
}

// 读取时间到数组
void DS1302_ReadTime(unsigned char *t) {
    t[0] = DS1302_Read(0x81); // 秒
    t[1] = DS1302_Read(0x83); // 分
    t[2] = DS1302_Read(0x85); // 时
    t[3] = DS1302_Read(0x87); // 日
    t[4] = DS1302_Read(0x89); // 月
    t[5] = DS1302_Read(0x8B); // 周
    t[6] = DS1302_Read(0x8D); // 年
}

// 设置时间
void DS1302_SetTime(unsigned char *t) {
    DS1302_Write(0x8E, 0x00); // 关闭写保护
    
    DS1302_Write(0x80, t[0]); // 秒
    DS1302_Write(0x82, t[1]); // 分
    DS1302_Write(0x84, t[2]); // 时
    DS1302_Write(0x86, t[3]); // 日
    DS1302_Write(0x88, t[4]); // 月
    DS1302_Write(0x8A, t[5]); // 周
    DS1302_Write(0x8C, t[6]); // 年
    
    DS1302_Write(0x8E, 0x80); // 打开写保护
}

unsigned char DS1302_ReadRam(unsigned char ram_index) {
    unsigned char addr = 0xC0 + (ram_index << 1);
    return DS1302_Read(addr | 0x01);
}

void DS1302_WriteRam(unsigned char ram_index, unsigned char dat) {
    unsigned char addr = 0xC0 + (ram_index << 1);
    DS1302_Write(0x8E, 0x00);
    DS1302_Write(addr, dat);
    DS1302_Write(0x8E, 0x80);
}