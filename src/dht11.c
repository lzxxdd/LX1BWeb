#include "dht11.h"
#include "tick.h"
#include "ls1b_gpio.h"
//////////////////////////////////////////////////////////////////////////////////	 
//������ֻ��ѧϰʹ�ã�δ��������ɣ��������������κ���;
//DHT11 ��������
//All rights reserved										  
//////////////////////////////////////////////////////////////////////////////////
	unsigned char  temperature,humidity;
//��λDHT11
void DHT11_Rst(void)	   
{                 
	DHT11_IO_OUT(); 	//SET OUTPUT
   gpio_write(DHT11_GPIO,0); 	//����DQ
    delay_ms(20);    	//��������18ms
    gpio_write(DHT11_GPIO,1); 	//DQ=1
	delay_us(30);     	//��������20~40us
}
//�ȴ�DHT11�Ļ�Ӧ
//����1:δ��⵽DHT11�Ĵ���
//����0:����
unsigned char DHT11_Check(void)
{
	unsigned char retry=0;
	DHT11_IO_IN();//SET INPUT
    while (DHT11_DQ_IN&&retry<100)//DHT11������40~80us
	{
		retry++;
		delay_us(1);
	};
	if(retry>=100)return 1;
	else retry=0;
    while (!DHT11_DQ_IN&&retry<100)//DHT11���ͺ���ٴ�����40~80us
	{
		retry++;
		delay_us(1);
	};
	if(retry>=100)return 1;
	return 0;
}
//��DHT11��ȡһ��λ
//����ֵ��1/0
unsigned char DHT11_Read_Bit(void)
{
 	unsigned char retry=0;
	while(DHT11_DQ_IN&&retry<100)//�ȴ���Ϊ�͵�ƽ
	{
		retry++;
		delay_us(1);
	}
	retry=0;
	while(!DHT11_DQ_IN&&retry<100)//�ȴ���ߵ�ƽ
	{
		retry++;
		delay_us(1);
	}
	delay_us(40);//�ȴ�40us
	if(DHT11_DQ_IN)return 1;
	else return 0;
}
//��DHT11��ȡһ���ֽ�
//����ֵ������������
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
//��DHT11��ȡһ������
//temp:�¶�ֵ(��Χ:0~50��)
//humi:ʪ��ֵ(��Χ:20%~90%)
//����ֵ��0,����;1,��ȡʧ��
unsigned char DHT11_Read_Data(unsigned char *temp,unsigned char *humi)
{
 	unsigned char buf[5];
	unsigned char i;
	DHT11_Rst();
	if(DHT11_Check()==0)
	{
		for(i=0;i<5;i++)//��ȡ40λ����
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
//��ʼ��DHT11��IO�� DQ ͬʱ���DHT11�Ĵ���
//����1:������
//����0:����
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
























