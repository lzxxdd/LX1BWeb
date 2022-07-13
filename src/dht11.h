#ifndef __DHT11_H
#define __DHT11_H 
//////////////////////////////////////////////////////////////////////////////////	 
//������ֻ��ѧϰʹ�ã�δ��������ɣ��������������κ���;
//DHT11 ��������
//All rights reserved										  
//////////////////////////////////////////////////////////////////////////////////

////IO��������
#define DHT11_GPIO    7//LCD_EN
#define LED0_GPIO     4//LCD_CLK
#define MOTOR_GPIO    5//LCD_VSYNC
#define POWER_GPIO    6//LCD_HSYNC
//IO��������
#define DHT11_IO_IN()   gpio_enable(DHT11_GPIO,DIR_IN)	//PG9����ģʽ
#define DHT11_IO_OUT()  gpio_enable(DHT11_GPIO,DIR_OUT) 	//PG9���ģʽ
#define DHT11_DQ_IN     gpio_read(DHT11_GPIO)

unsigned char DHT11_Init(void);//��ʼ��DHT11
unsigned char DHT11_Read_Data(unsigned char *temp,unsigned char *humi);//��ȡ��ʪ��
unsigned char DHT11_Read_Byte(void);//����һ���ֽ�
unsigned char DHT11_Read_Bit(void);//����һ��λ
unsigned char DHT11_Check(void);//����Ƿ����DHT11
void DHT11_Rst(void);//��λDHT11    


extern		unsigned char  temperature,humidity;
#endif















