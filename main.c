#include "reg52.h"
#include "lcd1602.h"
#include "ds1302.h"
#include "common.h"

// 按键定义
sbit KEY_MODE  = P3^1;  // K1 - 模式切换/退出
sbit KEY_SEL   = P3^0;  // K2 - 选择位置
sbit KEY_UP    = P3^2;  // K3 - 增加
sbit KEY_DOWN  = P3^3;  // K4 - 减少
sbit BEEP      = P2^0;

u8 Time[7];
u8 Temp_Time[7];
u8 Alarm_Hour = 7;
u8 Alarm_Min  = 0;
u8 alarm_triggered = 0;
u8 alarm_duration = 0;
u8 alarm_enabled = 1; // 0: off, 1: on (persisted to DS1302 RAM 0)

u8 mode = 0;           // 0:显示时间, 1:显示闹钟, 2:设置闹钟, 3:设置时间
u8 set_time_index = 0; // 时间设置项索引
u8 setting_mode = 0;   // 0:正常, 1:设置中
u8 alarm_edit_pos = 0; // 0:编辑小时, 1:编辑分钟
u8 key_press_time = 0;
u8 fast_mode = 0;
// 如果 RTC 数据不可信，开机时会强制进入设置模式
u8 rtc_invalid_start = 0;
u8 suppress_lcd = 0; // 兼容旧逻辑（现在不再长期抑制 LCD 更新）
// 非阻塞蜂鸣控制（闹钟响铃期间使用）
u8 alarm_beep_active = 0;
u8 alarm_beep_tick = 0;
u8 alarm_lcd_tick = 0;
u8 hour_mode = 0;      // 0: 24小时制, 1: 12小时制
u8 hourly_chime = 0;   // 0: 关闭整点报时, 1: 开启


#define RAM_CHECK_ADDR  0x00  // 存放校验暗号
#define RAM_ALARM_H     0x01  // 存放闹钟时
#define RAM_ALARM_M     0x02  // 存放闹钟分
#define RAM_ALARM_EN    0x03  // 存放闹钟开关
#define RAM_HOUR_MODE   0x04
#define RAM_HOURLY_EN   0x05
// DelayMs 原型（在文件顶部声明以避免在使用前编译器报错）
void DelayMs(u16 ms);

// 在闹钟首次触发时生成短促蜂鸣脉冲（阻塞），提高被动/有源蜂鸣器触发概率
void AlarmBeepBurst() {
    unsigned char i;
    // 确保 LCD 被抑制
    suppress_lcd = 1;
    for(i = 0; i < 6; i++) {
        BEEP = 0;
        DelayMs(40);
        BEEP = 1;
        DelayMs(40);
    }
}

// 校验从 RTC 读取的 BCD 时间是否在合理范围
u8 BCD_to_Decimal(u8 bcd);
u8 Decimal_to_BCD(u8 decimal);
u8 IsTimeValid(unsigned char *t) {
    u8 sec = BCD_to_Decimal(t[0]);
    u8 min = BCD_to_Decimal(t[1]);
    u8 hour = BCD_to_Decimal(t[2]);
    u8 day = BCD_to_Decimal(t[3]);
    u8 month = BCD_to_Decimal(t[4]);
    u8 week = BCD_to_Decimal(t[5]);
    u8 year = BCD_to_Decimal(t[6]);

    if(sec > 59) return 0;
    if(min > 59) return 0;
    if(hour > 23) return 0;
    if(day < 1 || day > 31) return 0;
    if(month < 1 || month > 12) return 0;
    if(week < 1 || week > 7) return 0;
    if(year > 99) return 0;
    return 1;
}

// BCD转十进制
u8 BCD_to_Decimal(u8 bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

// 十进制转BCD
u8 Decimal_to_BCD(u8 decimal) {
    return ((decimal / 10) << 4) | (decimal % 10);
}

void DelayMs(u16 ms) {
    u16 i, j;
    for(i = 0; i < ms; i++)
        for(j = 0; j < 120; j++);
}

// 按键检测
u8 KeyScan() {
    static u8 key_pressed = 0;
    u8 key = 0;
    
    if(KEY_MODE == 0) key = 1;
    else if(KEY_SEL == 0) key = 2;
    else if(KEY_UP == 0) key = 3;
    else if(KEY_DOWN == 0) key = 4;
    
    if(key != 0) {
        if(!key_pressed) {
            key_pressed = 1;
            key_press_time = 0;
            fast_mode = 0;
            return key;
        } else {
            key_press_time++;
            if(key_press_time > 30) {
                fast_mode = 1;
                if((key_press_time % 5) == 0) {
                    return key;
                }
            }
        }
    } else {
        key_pressed = 0;
        key_press_time = 0;
        fast_mode = 0;
    }
    
    return 0;
}

void CheckAlarm() {
    u8 hour = BCD_to_Decimal(Time[2]);
    u8 min = BCD_to_Decimal(Time[1]);
    u8 sec = BCD_to_Decimal(Time[0]);
    // 触发条件：闹钟开启且小时/分钟匹配，且秒数在 0..5 范围内
    if(alarm_enabled) {
        if(hour == Alarm_Hour && min == Alarm_Min) {
            if(sec <= 5) {
                if(!alarm_triggered) {
                    alarm_triggered = 1;
                    alarm_duration = 0;
                    BEEP = 0; // 低电平有效，开始蜂鸣
                    // 启动非阻塞蜂鸣循环并重置计数器
                    alarm_beep_active = 1;
                    alarm_beep_tick = 0;
                    alarm_lcd_tick = 0;
                }
            }
        }
    }
    
    if(alarm_triggered) {
        alarm_duration++;
        if(alarm_duration >= 300) { // 30秒后自动关闭
            alarm_triggered = 0;
            alarm_duration = 0;
            BEEP = 1;
            // 关闭非阻塞蜂鸣并允许正常显示刷新
            alarm_beep_active = 0;
        }
    }
    
    // 任何按键都可以关闭闹钟
    if(alarm_triggered && (KEY_MODE == 0 || KEY_SEL == 0 || KEY_UP == 0 || KEY_DOWN == 0)) {
        alarm_triggered = 0;
        alarm_duration = 0;
        BEEP = 1;
        // 用户按键关闭闹钟，停止蜂鸣并刷新屏幕
        alarm_beep_active = 0;
        LCD_WriteCmd(0x01);
        DelayMs(200); // 消抖
    }
}

// 时间显示
// --- 【修改后的显示时间函数】 ---
void DisplayTime() {
    u8 h24, h12;
    DS1302_ReadTime(Time); // 读取最新时间
    
    // 第一行显示日期
    LCD_ShowString(0, 0, "20");
    LCD_ShowNum(0, 2, BCD_to_Decimal(Time[6]), 2);
    LCD_ShowString(0, 4, "-");
    LCD_ShowNum(0, 5, BCD_to_Decimal(Time[4]), 2);
    LCD_ShowString(0, 7, "-");
    LCD_ShowNum(0, 8, BCD_to_Decimal(Time[3]), 2);
    LCD_ShowString(0, 11, "W");
    LCD_ShowNum(0, 12, BCD_to_Decimal(Time[5]), 1);
    
    // 显示整点报时图标 (右上角显示一个 C 代表 Chime，或者空)
    if(hourly_chime) LCD_ShowString(0, 15, "C");
    else LCD_ShowString(0, 15, " ");

    // 第二行显示时间 (核心逻辑)
    h24 = BCD_to_Decimal(Time[2]); // 获取24小时制的十进制小时
    
    if(hour_mode == 0) {
        // --- 24小时制模式 ---
        LCD_ShowNum(1, 0, h24, 2);
        LCD_ShowString(1, 2, ":");
        LCD_ShowNum(1, 3, BCD_to_Decimal(Time[1]), 2);
        LCD_ShowString(1, 5, ":");
        LCD_ShowNum(1, 6, BCD_to_Decimal(Time[0]), 2);
        LCD_ShowString(1, 9, "       "); // 清空后面的 AM/PM 区域
    } else {
        // --- 12小时制模式 ---
        // 计算 12 小时制数值
        if(h24 == 0) h12 = 12;      // 0点是 12 AM
        else if(h24 <= 12) h12 = h24; 
        else h12 = h24 - 12;        // 13-23点 减12
        
        LCD_ShowNum(1, 0, h12, 2);
        LCD_ShowString(1, 2, ":");
        LCD_ShowNum(1, 3, BCD_to_Decimal(Time[1]), 2);
        LCD_ShowString(1, 5, ":");
        LCD_ShowNum(1, 6, BCD_to_Decimal(Time[0]), 2);
        
        // 显示 AM 或 PM
        if(h24 < 12) LCD_ShowString(1, 9, " AM");
        else LCD_ShowString(1, 9, " PM");
    }
}

// 闹钟显示界面
void DisplayAlarm() {
    DS1302_ReadTime(Time);
    
    LCD_ShowString(0, 0, "20");
    LCD_ShowNum(0, 2, BCD_to_Decimal(Time[6]), 2);
    LCD_ShowString(0, 4, "-");
    LCD_ShowNum(0, 5, BCD_to_Decimal(Time[4]), 2);
    LCD_ShowString(0, 7, "-");
    LCD_ShowNum(0, 8, BCD_to_Decimal(Time[3]), 2);
    LCD_ShowString(0, 13, "W");
    LCD_ShowNum(0, 14, BCD_to_Decimal(Time[5]), 1);
    
    LCD_ShowString(1, 0, "Alarm:");
    LCD_ShowNum(1, 7, Alarm_Hour, 2);
    LCD_ShowString(1, 9, ":");
    LCD_ShowNum(1, 10, Alarm_Min, 2);
    if(alarm_triggered) {
        LCD_ShowString(1, 13, "RING");
    } else {
        if(alarm_enabled) LCD_ShowString(1, 13, "ON ");
        else LCD_ShowString(1, 13, "OFF");
    }
}

// 设置闹钟界面
void DisplaySetAlarm() {
    LCD_ShowString(0, 0, "Set Alarm Time  ");
    
    if(alarm_edit_pos == 0) {
        LCD_ShowString(1, 0, ">");
        LCD_ShowNum(1, 2, Alarm_Hour, 2);
        LCD_ShowString(1, 4, ":");
        LCD_ShowNum(1, 6, Alarm_Min, 2);
        LCD_ShowString(1, 9, "Hour  ");
    } else {
        LCD_ShowString(1, 0, " ");
        LCD_ShowNum(1, 2, Alarm_Hour, 2);
        LCD_ShowString(1, 4, ":");
        LCD_ShowString(1, 6, ">");
        LCD_ShowNum(1, 7, Alarm_Min, 2);
        LCD_ShowString(1, 9, "Minute");
    }
}

// 设置时间界面
// 设置时间界面
// 设置时间界面
// 设置时间界面
void DisplaySetTime() {
    u8 hour, min, day, month, year, week;
    
    if(setting_mode) {
        hour = BCD_to_Decimal(Temp_Time[2]);
        min = BCD_to_Decimal(Temp_Time[1]);
        day = BCD_to_Decimal(Temp_Time[3]);
        month = BCD_to_Decimal(Temp_Time[4]);
        year = BCD_to_Decimal(Temp_Time[6]);  // 使用 Temp_Time 里的年份 (t[6]=year)
        week = BCD_to_Decimal(Temp_Time[5]); // 星期在 t[5]
    } else {
        DS1302_ReadTime(Time);
        hour = BCD_to_Decimal(Time[2]);
        min = BCD_to_Decimal(Time[1]);
        day = BCD_to_Decimal(Time[3]);
        month = BCD_to_Decimal(Time[4]);
        year = BCD_to_Decimal(Time[6]);  // 使用 Time 里的年份 (t[6]=year)
        week = BCD_to_Decimal(Time[5]);
    }
    
    LCD_ShowString(0, 0, "Set System Time ");
    
    switch(set_time_index) {
        case 0: // 年
            LCD_ShowString(1, 0, ">Year: 20");
            LCD_ShowNum(1, 9, year, 2);  // 显示修改后的年份
            LCD_ShowString(1, 11, "     ");
            break;
        case 1: // 月
            LCD_ShowString(1, 0, ">Month:  ");
            LCD_ShowNum(1, 8, month, 2);
            LCD_ShowString(1, 10, "     ");
            break;
        case 2: // 日
            LCD_ShowString(1, 0, ">Day:    ");
            LCD_ShowNum(1, 8, day, 2);
            LCD_ShowString(1, 10, "     ");
            break;
        case 3: // 时
            LCD_ShowString(1, 0, ">Hour:   ");
            LCD_ShowNum(1, 8, hour, 2);
            LCD_ShowString(1, 10, "     ");
            break;
        case 4: // 分
            LCD_ShowString(1, 0, ">Minute: ");
            LCD_ShowNum(1, 8, min, 2);
            LCD_ShowString(1, 10, "     ");
            break;
        case 5: // 星期
            LCD_ShowString(1, 0, ">Week:   ");
            LCD_ShowNum(1, 8, week, 1);
            LCD_ShowString(1, 9, "      ");
            break;
    }
}

// 主循环
void main() {
    u8 key;
    u8 i;

    LCD_Init();
    DS1302_Init();
    BEEP = 1;
// 1. 尝试读取“暗号”和闹钟数据
    if(DS1302_ReadRam(RAM_CHECK_ADDR) == 0xAA) {
        // 说明电池一直有电，直接把存好的闹钟拿出来用
        Alarm_Hour    = DS1302_ReadRam(RAM_ALARM_H);
        Alarm_Min     = DS1302_ReadRam(RAM_ALARM_M);
        alarm_enabled = DS1302_ReadRam(RAM_ALARM_EN);
    } else {
        // 说明是第一次用（比如刚买的电池），先写一份默认值进去
        DS1302_WriteRam(RAM_CHECK_ADDR, 0xAA);  // 种下暗号
        DS1302_WriteRam(RAM_ALARM_H, Alarm_Hour);
        DS1302_WriteRam(RAM_ALARM_M, Alarm_Min);
        DS1302_WriteRam(RAM_ALARM_EN, alarm_enabled);
    }

    // 2. 检查 RTC 时间是否乱码（无电池上电通常返回全0或垃圾值数据）
    DS1302_ReadTime(Time);
    if(!IsTimeValid(Time)) {
        // 时间不对，提示用户重新设表
        rtc_invalid_start = 1;
        LCD_ShowString(0, 0, " RTC Invalid!   ");
        LCD_ShowString(1, 0, " Please Set Time ");
        DelayMs(1500);
        LCD_WriteCmd(0x01);

        // 自动跳转到设置时间模式 (Mode 3)
        mode = 3;
        setting_mode = 1;
        set_time_index = 0;
        
        // 给出一组默认时间供修改
        Temp_Time[0] = Decimal_to_BCD(0); // 秒
        Temp_Time[1] = Decimal_to_BCD(0); // 分
        Temp_Time[2] = Decimal_to_BCD(12);// 时（默认中午12点）
        Temp_Time[3] = Decimal_to_BCD(1); // 日
        Temp_Time[4] = Decimal_to_BCD(1); // 月
        Temp_Time[5] = Decimal_to_BCD(1); // 周
        Temp_Time[6] = Decimal_to_BCD(25);// 年 (2025年)
    } else {
        // 时间正常，正常开机
        LCD_ShowString(0, 0, "  Smart Clock   ");
        LCD_ShowString(1, 0, "  Starting...   ");
        DelayMs(1000);
        LCD_WriteCmd(0x01);
    }

		if(DS1302_ReadRam(RAM_CHECK_ADDR) == 0xAA) {
        hour_mode = DS1302_ReadRam(RAM_HOUR_MODE);
        hourly_chime = DS1302_ReadRam(RAM_HOURLY_EN);
    } else {
        // 如果是新电池，初始化为默认值并保存
        DS1302_WriteRam(RAM_HOUR_MODE, 0);   // 默认 24小时制
        DS1302_WriteRam(RAM_HOURLY_EN, 0);   // 默认 关闭整点报时
    }
		
    while(1) {
        // 如果不是设置时间模式，则始终读取最新时间
        if(!(mode == 3 && setting_mode)) {
            DS1302_ReadTime(Time);  // 确保在非设置模式下读取最新时间
        }

        key = KeyScan();

        // 处理模式切换 (K1键)
        if(key == 1 && !setting_mode) {
            mode++;
            if(mode > 3) mode = 0;
            LCD_WriteCmd(0x01); // 清除屏幕
        }

switch(mode) {
            case 0:  // 显示时间模式
                if(!suppress_lcd) DisplayTime();
                
                // --- 【新增：模式 0 下的快捷键】 ---
                // 按 K3 (UP) 切换 12/24 小时制
                if(key == 3) {
                    hour_mode = !hour_mode; // 切换状态
                    DS1302_WriteRam(RAM_HOUR_MODE, hour_mode); // 立即保存
                    LCD_WriteCmd(0x01); // 清屏刷新
                }
                
                // 按 K4 (DOWN) 切换整点报时
                if(key == 4) {
                    hourly_chime = !hourly_chime; // 切换状态
                    DS1302_WriteRam(RAM_HOURLY_EN, hourly_chime); // 立即保存
                    // 蜂鸣器叫一声提示状态变化
                    BEEP = 0; DelayMs(100); BEEP = 1; 
                }
                // -----------------------------------
                break;
                
            case 1:  // 显示闹钟模式
                if(!suppress_lcd) DisplayAlarm();
                // 方便调试：在闹钟显示界面按 KEY_UP (K3) 可以手动切换闹钟响铃状态
                if(!setting_mode && key == 3) {
                    if(!alarm_triggered) {
                        // 启动闹钟（与自动触发一致的非阻塞行为）
                        alarm_triggered = 1;
                        alarm_duration = 0;
                        BEEP = 0; // 启动蜂鸣器（低电平有效）
                        alarm_beep_active = 1;
                        alarm_beep_tick = 0;
                        alarm_lcd_tick = 0;
                        // 等待按键释放，避免同次按键被检测为关闭闹钟
                        while(KEY_UP == 0) DelayMs(10);
                    } else {
                        // 如果已经在响铃，按一次停止（与其他按键行为一致）
                        alarm_triggered = 0;
                        alarm_duration = 0;
                        BEEP = 1;
                        alarm_beep_active = 0;
                        LCD_WriteCmd(0x01);
                        DelayMs(200);
                    }
                }
                // 在闹钟显示界面按 KEY_SEL (K2) 切换闹钟开关并持久保存
                if(!setting_mode && key == 2) {
                    alarm_enabled = !alarm_enabled;
                    // 持久化到 DS1302 的 RAM 0
                    DS1302_WriteRam(0, alarm_enabled ? 0x01 : 0x00);
                    if(alarm_enabled) {
                        LCD_ShowString(0, 0, " Alarm: ON     ");
                    } else {
                        LCD_ShowString(0, 0, " Alarm: OFF    ");
                    }
                    DelayMs(800);
                    LCD_WriteCmd(0x01);
                }
                break;
                
            case 2:  // 设置闹钟模式
                if(!setting_mode) {
                    DisplaySetAlarm();
                    if(key == 1) {
											
                        setting_mode = 1;
                        alarm_edit_pos = 0;
                        LCD_WriteCmd(0x01);
                    }
                } else {
                    DisplaySetAlarm();
                    
                    if(key == 2) {
                        alarm_edit_pos = !alarm_edit_pos;
                        DelayMs(200);
                    }
                    
                    if(key == 3) {
                        if(alarm_edit_pos == 0) {
                            Alarm_Hour++;
                            if(Alarm_Hour >= 24) Alarm_Hour = 0;
                        } else {
                            Alarm_Min++;
                            if(Alarm_Min >= 60) Alarm_Min = 0;
                        }
                        if(!fast_mode) DelayMs(200);
                    }
                    
                    if(key == 4) {
                        if(alarm_edit_pos == 0) {
                            if(Alarm_Hour == 0) Alarm_Hour = 23;
                            else Alarm_Hour--;
                        } else {
                            if(Alarm_Min == 0) Alarm_Min = 59;
                            else Alarm_Min--;
                        }
                        if(!fast_mode) DelayMs(200);
                    }
                    
                    if(key == 1) {
											DS1302_WriteRam(1, Alarm_Hour);
            DS1302_WriteRam(2, Alarm_Min);
            DS1302_WriteRam(3, alarm_enabled);
                        setting_mode = 0;
                        LCD_ShowString(0, 0, " Alarm Saved!   ");
                        LCD_ShowString(1, 0, "                ");
                        DelayMs(1000);
                        LCD_WriteCmd(0x01);
                    }
                }
                break;
                
            case 3:  // 设置系统时间模式
                if(!setting_mode) {
                    DisplaySetTime();
                    if(key == 1) {
                        setting_mode = 1;
                        set_time_index = 0;
                        // 保存当前时间到 Temp_Time 数组
                        for(i = 0; i < 7; i++) {
                            Temp_Time[i] = Time[i];
                        }
                        LCD_WriteCmd(0x01);
                    }
                } else {
                    DisplaySetTime();
                    
                    if(key == 2) {
                        set_time_index++;
                        if(set_time_index > 5) set_time_index = 0;
                        DelayMs(200);
                    }
                    
                    if(key == 3) {
                        switch(set_time_index) {
                            case 0: // 年
                                    {
                                        u8 v = BCD_to_Decimal(Temp_Time[6]);
                                        v++;
                                        if(v > 99) v = 0;
                                        Temp_Time[6] = Decimal_to_BCD(v);
                                    }
                                break;
                            case 1: // 月
                                    {
                                        u8 v = BCD_to_Decimal(Temp_Time[4]);
                                        v++;
                                        if(v > 12) v = 1;
                                        Temp_Time[4] = Decimal_to_BCD(v);
                                    }
                                break;
                            case 2: // 日
                                    {
                                        u8 v = BCD_to_Decimal(Temp_Time[3]);
                                        v++;
                                        if(v > 31) v = 1;
                                        Temp_Time[3] = Decimal_to_BCD(v);
                                    }
                                break;
                            case 3: // 时
                                    {
                                        u8 v = BCD_to_Decimal(Temp_Time[2]);
                                        v++;
                                        if(v >= 24) v = 0;
                                        Temp_Time[2] = Decimal_to_BCD(v);
                                    }
                                break;
                            case 4: // 分
                                    {
                                        u8 v = BCD_to_Decimal(Temp_Time[1]);
                                        v++;
                                        if(v >= 60) v = 0;
                                        Temp_Time[1] = Decimal_to_BCD(v);
                                    }
                                break;
                            case 5: // 星期
                                    {
                                        u8 v = BCD_to_Decimal(Temp_Time[5]);
                                        v++;
                                        if(v > 7) v = 1;
                                        Temp_Time[5] = Decimal_to_BCD(v);
                                    }
                                break;
                        }
                        if(!fast_mode) DelayMs(200);
                    }
                    
                    if(key == 4) {
                        switch(set_time_index) {
                            case 0: // 年
                                    {
                                        u8 v = BCD_to_Decimal(Temp_Time[6]);
                                        if(v == 0) v = 99;
                                        else v--;
                                        Temp_Time[6] = Decimal_to_BCD(v);
                                    }
                                break;
                            case 1: // 月
                                    {
                                        u8 v = BCD_to_Decimal(Temp_Time[4]);
                                        if(v == 1) v = 12;
                                        else v--;
                                        Temp_Time[4] = Decimal_to_BCD(v);
                                    }
                                break;
                            case 2: // 日
                                    {
                                        u8 v = BCD_to_Decimal(Temp_Time[3]);
                                        if(v == 1) v = 31;
                                        else v--;
                                        Temp_Time[3] = Decimal_to_BCD(v);
                                    }
                                break;
                            case 3: // 时
                                    {
                                        u8 v = BCD_to_Decimal(Temp_Time[2]);
                                        if(v == 0) v = 23;
                                        else v--;
                                        Temp_Time[2] = Decimal_to_BCD(v);
                                    }
                                break;
                            case 4: // 分
                                    {
                                        u8 v = BCD_to_Decimal(Temp_Time[1]);
                                        if(v == 0) v = 59;
                                        else v--;
                                        Temp_Time[1] = Decimal_to_BCD(v);
                                    }
                                break;
                            case 5: // 星期
                                    {
                                        u8 v = BCD_to_Decimal(Temp_Time[5]);
                                        if(v == 1) v = 7;
                                        else v--;
                                        Temp_Time[5] = Decimal_to_BCD(v);
                                    }
                                break;
                        }
                        if(!fast_mode) DelayMs(200);
                    }
                    
                    if(key == 1) {
                        setting_mode = 0;
                        set_time_index = 0;
                        // 在写入 RTC 前校验十进制范围，防止未初始化或非法数据写入
                        {
                            u8 sec = BCD_to_Decimal(Temp_Time[0]);
                            u8 min = BCD_to_Decimal(Temp_Time[1]);
                            u8 hour = BCD_to_Decimal(Temp_Time[2]);
                            u8 day = BCD_to_Decimal(Temp_Time[3]);
                            u8 month = BCD_to_Decimal(Temp_Time[4]);
                            u8 week = BCD_to_Decimal(Temp_Time[5]);
                            u8 year = BCD_to_Decimal(Temp_Time[6]);

                            if(sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12 || week < 1 || week > 7 || year > 99) {
                                LCD_ShowString(0, 0, " Invalid Time!  ");
                                LCD_ShowString(1, 0, " Save Aborted   ");
                                DelayMs(1000);
                                LCD_WriteCmd(0x01);
                                // 不写入 RTC，恢复显示
                                // 更新 Time 数组以保证界面同步
                                for(i = 0; i < 7; i++) {
                                    Time[i] = Temp_Time[i];
                                }
                                break;
                            }
                        }
                        // 保存时间到 DS1302（通过校验后写入）
                        // 将秒归零以避免未设置的秒导致写入后显示异常
                        Temp_Time[0] = Decimal_to_BCD(0);
                        DS1302_SetTime(Temp_Time); // 写入 RTC
                        // 给 RTC 少许时间稳定，然后读回确认并刷新显示数据
                        DelayMs(200);
                        DS1302_ReadTime(Time);
                        // 如果读回值仍然非法，则退回使用刚保存的 Temp_Time
                        if(!IsTimeValid(Time)) {
                            for(i = 0; i < 7; i++) {
                                Time[i] = Temp_Time[i];
                            }
                        }
                        // 保存完成后立即切换到时间显示页面并恢复正常运行
                        mode = 0;              // 切换到显示时间模式
                        rtc_invalid_start = 0; // 清除无效 RTC 标志（如有）
                        LCD_ShowString(0, 0, " Time Saved!    ");
                        LCD_ShowString(1, 0, "                ");
                        DelayMs(1000);
                        LCD_WriteCmd(0x01);
                    }
                }
                break;
        }

        CheckAlarm();
				{
            static u8 last_hour_beep = 99; // 记录上次响铃时的秒数
            u8 current_sec = BCD_to_Decimal(Time[0]);
            u8 current_min = BCD_to_Decimal(Time[1]);

            if(hourly_chime && current_min == 0 && current_sec == 0) {
                if(last_hour_beep != current_sec) {
                    // 触发报时：嘀-嘀 两声
                    BEEP = 0; DelayMs(100); BEEP = 1; DelayMs(100);
                    BEEP = 0; DelayMs(100); BEEP = 1;
                    last_hour_beep = current_sec; // 标记这一秒已经响过了
                }
            } else {
                 if(current_sec != 0) last_hour_beep = 99; // 重置标记
            }
        }

        // 非阻塞蜂鸣控制：如果处于闹钟响铃状态，以固定节拍切换 BEEP
        if(alarm_beep_active) {
            alarm_beep_tick++;
            if(alarm_beep_tick >= 2) { // 2 * 50ms = ~100ms 切换一次
                alarm_beep_tick = 0;
                BEEP = !BEEP;
            }
            // 周期性刷新屏幕（每 ~500ms）以保持显示更新
            alarm_lcd_tick++;
            if(alarm_lcd_tick >= 10) {
                alarm_lcd_tick = 0;
                // 如果当前在闹钟显示界面，刷新闹钟界面；否则刷新时间界面
                if(mode == 1) DisplayAlarm();
                else DisplayTime();
            }
        }

        DelayMs(50);
    }
}



