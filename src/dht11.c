#include "dht11.h"
#include "tick.h"
#include "ls1b_gpio.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//DHT11 驱动代码
//All rights reserved										  
//////////////////////////////////////////////////////////////////////////////////
	unsigned char  temperature,humidity;
//复位DHT11
void DHT11_Rst(void)	   
{                 
	DHT11_IO_OUT(); 	//SET OUTPUT
   gpio_write(DHT11_GPIO,0); 	//拉低DQ
    delay_ms(20);    	//拉低至少18ms
    gpio_write(DHT11_GPIO,1); 	//DQ=1
	delay_us(30);     	//主机拉高20~40us
}
//等待DHT11的回应
//返回1:未检测到DHT11的存在
//返回0:存在
unsigned char DHT11_Check(void)
{
	unsigned char retry=0;
	DHT11_IO_IN();//SET INPUT
    while (DHT11_DQ_IN&&retry<100)//DHT11会拉低40~80us
	{
		retry++;
		delay_us(1);
	};
	if(retry>=100)return 1;
	else retry=0;
    while (!DHT11_DQ_IN&&retry<100)//DHT11拉低后会再次拉高40~80us
	{
		retry++;
		delay_us(1);
	};
	if(retry>=100)return 1;
	return 0;
}
//从DHT11读取一个位
//返回值：1/0
unsigned char DHT11_Read_Bit(void)
{
 	unsigned char retry=0;
	while(DHT11_DQ_IN&&retry<100)//等待变为低电平
	{
		retry++;
		delay_us(1);
	}
	retry=0;
	while(!DHT11_DQ_IN&&retry<100)//等待变高电平
	{
		retry++;
		delay_us(1);
	}
	delay_us(40);//等待40us
	if(DHT11_DQ_IN)return 1;
	else return 0;
}
//从DHT11读取一个字节
//返回值：读到的数据
unsigned char DHT11_Read_Byte(void)
{
    unsigned char i,dat;
    dat=0;
	for (i=0;i<8;i++)
	{
   		dat<<=1;
	    dat|=DHT11_Read_Bit();
    }
    return dat;
}
//从DHT11读取一次数据
//temp:温度值(范围:0~50°)
//humi:湿度值(范围:20%~90%)
//返回值：0,正常;1,读取失败
unsigned char DHT11_Read_Data(unsigned char *temp,unsigned char *humi)
{
 	unsigned char buf[5];
	unsigned char i;
	DHT11_Rst();
	if(DHT11_Check()==0)
	{
		for(i=0;i<5;i++)//读取40位数据
		{
			buf[i]=DHT11_Read_Byte();
		}
		if((buf[0]+buf[1]+buf[2]+buf[3])==buf[4])
		{
			*humi=buf[0];
			*temp=buf[2];
		}
	}else return 1;
	return 0;
}
//初始化DHT11的IO口 DQ 同时检测DHT11的存在
//返回1:不存在
//返回0:存在
unsigned char DHT11_Init(void)
{
    gpio_enable(DHT11_GPIO,DIR_OUT);
    gpio_enable(LED0_GPIO,DIR_OUT);
    gpio_enable(MOTOR_GPIO,DIR_OUT);
 //   gpio_enable(POWER_GPIO,DIR_OUT);
    gpio_write(LED0_GPIO,1);
    gpio_write(MOTOR_GPIO,0);
 //   gpio_write(POWER_GPIO,01);
    DHT11_Rst();
	return DHT11_Check();
}
























