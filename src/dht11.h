#ifndef __DHT11_H
#define __DHT11_H 
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//DHT11 驱动代码
//All rights reserved										  
//////////////////////////////////////////////////////////////////////////////////

////IO操作函数
#define DHT11_GPIO    7//LCD_EN
#define LED0_GPIO     4//LCD_CLK
#define MOTOR_GPIO    5//LCD_VSYNC
#define POWER_GPIO    6//LCD_HSYNC
//IO方向设置
#define DHT11_IO_IN()   gpio_enable(DHT11_GPIO,DIR_IN)	//PG9输入模式
#define DHT11_IO_OUT()  gpio_enable(DHT11_GPIO,DIR_OUT) 	//PG9输出模式
#define DHT11_DQ_IN     gpio_read(DHT11_GPIO)

unsigned char DHT11_Init(void);//初始化DHT11
unsigned char DHT11_Read_Data(unsigned char *temp,unsigned char *humi);//读取温湿度
unsigned char DHT11_Read_Byte(void);//读出一个字节
unsigned char DHT11_Read_Bit(void);//读出一个位
unsigned char DHT11_Check(void);//检测是否存在DHT11
void DHT11_Rst(void);//复位DHT11    


extern		unsigned char  temperature,humidity;
#endif















