#include <string.h>
#include <stdlib.h>
#include "lwip/debug.h"
#include "httpd.h"
#include "lwip/tcp.h"
#include "fs.h"
#include "dht11.h"
#include "ls1b_gpio.h"
//////////////////////////////////////////////////////////////////////////////////	 
//������ֻ��ѧϰʹ�ã�δ��������ɣ��������������κ���;
//All rights reserved
//*******************************************************************************
//�޸���Ϣ
//��
////////////////////////////////////////////////////////////////////////////////// 	
#define NUM_CONFIG_CGI_URIS	2  //CGI��URI����
#define NUM_CONFIG_SSI_TAGS	4  //SSI��TAG����

//����LED��BEEP��CGI handler
const char* LEDS_CGI_Handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]);
const char* BEEP_CGI_Handler(int iIndex,int iNumParams,char *pcParam[],char *pcValue[]);

static const char *ppcTAGs[]=  //SSI��Tag
{
	"w", //�¶�ֵ
	"h", //ʪ��ֵ
	"p", //PHֵ
	"k"  //�ط�
};

static const tCGI ppcURLs[]= //cgi����
{
	{"/leds.cgi",LEDS_CGI_Handler},
	{"/beep.cgi",BEEP_CGI_Handler},
};


//��web�ͻ��������������ʱ��,ʹ�ô˺�����CGI handler����
static int FindCGIParameter(const char *pcToFind,char *pcParam[],int iNumParams)
{
	int iLoop;
	for(iLoop = 0;iLoop < iNumParams;iLoop ++ )
	{
		if(strcmp(pcToFind,pcParam[iLoop]) == 0)
		{
			return (iLoop); //����iLOOP
		}
	}
	return (-1);
}


//SSIHandler����Ҫ�õ��Ĵ���ADC�ĺ���
void Temper_Handler(char *pcInsert)
{
	char Digit1=0, Digit2=0, Digit3=0, Digit4=0;//,Digit5=0;
	Digit1 = temperature / 10;
	Digit2 = temperature% 10;
  //  Digit5 = ((short)Temperate) % 10;
	//��ӵ�html�е�����
	*pcInsert 	  = (char)(Digit1+0x30);
	*(pcInsert+1) = (char)(Digit2+0x30);
	*(pcInsert+2) = '\0';
//	*(pcInsert+2) =	(char)(0x20);
//	*(pcInsert+3) = (char)(0x20);
//	*(pcInsert+4) = (char)(Digit3+0x30);
//	*(pcInsert+5) = (char)(Digit4+0x30);
}

//SSIHandler����Ҫ�õ��Ĵ����ڲ��¶ȴ������ĺ���
void Humidity_Handler(char *pcInsert)
{
	char Digit1=0, Digit2=0, Digit3=0, Digit4=0;//
	Digit1 = humidity / 10;
	Digit2 = humidity% 10;
	*pcInsert 	  = (char)(Digit1+0x30);
	*(pcInsert+1) = (char)(Digit2+0x30);
     *(pcInsert+2) = '\0';
}

//SSIHandler����Ҫ�õ��Ĵ���RTCʱ��ĺ���
void PH_Handler(char *pcInsert)
{
	static unsigned char PH=10;
	*pcInsert = 	(char)((PH/10) + 0x30);
	*(pcInsert+1) = (char)((PH%10) + 0x30);
    *(pcInsert+2) = '\0';
	if(++PH>99)
       PH=10;
}

//SSIHandler����Ҫ�õ��Ĵ���RTC���ڵĺ���
void Ka_Handler(char *pcInsert)
{
	static unsigned char PH=99;
	*pcInsert = 	(char)((PH/10) + 0x30);
	*(pcInsert+1) = (char)((PH%10) + 0x30);
	*(pcInsert+2) = '\0';
	if(--PH<10)
       PH=99;
}
//SSI��Handler���
static u16_t SSIHandler(int iIndex,char *pcInsert,int iInsertLen)
{
	switch(iIndex)
	{
		case 0:
				Temper_Handler(pcInsert);
				break;
		case 1:
				Humidity_Handler(pcInsert);
				break;
		case 2:
				PH_Handler(pcInsert);
				break;
		case 3:
				Ka_Handler(pcInsert);
				break;
	}
	return strlen(pcInsert);
}

#define BEEP LED3 //wfn

//CGI LED���ƾ��
const char* LEDS_CGI_Handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
	unsigned char i=0;  //ע������Լ���GET�Ĳ����Ķ�����ѡ��iֵ��Χ
	//iIndex = FindCGIParameter("button1",pcParam,iNumParams);  //�ҵ�led��������
	//for (i=0; i<iNumParams; i++) //���CGI����
	{
//		printf("%d %s %s\r\n",iNumParams,pcParam[i],pcValue[i]);
	}
//	printf("-----\r\n");
	if(FindCGIParameter("button1",pcParam,iNumParams)!=-1)
	{
		if(strstr(pcValue[0], "AA") !=0)
	  {
		  gpio_write(LED0_GPIO,0);
	   }
	  else if (strcmp(pcValue[0], "D8") !=0)
	  {
		 gpio_write(LED0_GPIO,1);
	  }
	}
	else if(FindCGIParameter("button2",pcParam,iNumParams)!=-1)
	{
		if(strstr(pcValue[0], "AA") !=0)
	  {
		 gpio_write(MOTOR_GPIO,1);
	   }
	  else if (strcmp(pcValue[0], "D8") !=0)
	  {
		  gpio_write(MOTOR_GPIO,0);
	  }
	}

	//ֻ��һ��CGI��� iIndex=0
/*	if (iIndex != -1)
	{
		LED2=1;  //�ر�LED1��
		for (i=0; i<iNumParams; i++) //���CGI����
		{
		  if (strcmp(pcParam[i] , "LED1")==0)  //������"led" ���ڿ���LED1�Ƶ�
		  {
			if(strcmp(pcValue[i], "LED1ON") ==0)  //�ı�LED1״̬
				LED2=0; //��LED1
			else if(strcmp(pcValue[i],"LED1OFF") == 0)
				LED2=1; //�ر�LED1
		  }
		}
	 }
//	if(LED2==0&&BEEP==0)		return "/STM32_LED_ON_BEEP_OFF.shtml";	//LED1��,BEEP��
//	else if(LED2==0&&BEEP==1) 	return "/STM32_LED_ON_BEEP_ON.shtml";	//LED1��,BEEP��
//	else if(LED2==1&&BEEP==1) 	return "/STM32_LED_OFF_BEEP_ON.shtml";	//LED1��,BEEP��
//	else return "/STM32_LED_OFF_BEEP_OFF.shtml";						//LED1��,BEEP��
*/

   return "/index.shtml";						//LED1��,BEEP��
}

//BEEP��CGI���ƾ��

const char *BEEP_CGI_Handler(int iIndex,int iNumParams,char *pcParam[],char *pcValue[])
{
	unsigned char i=0;
/*	iIndex = FindCGIParameter("BEEP",pcParam,iNumParams);  //�ҵ�BEEP��������
	if(iIndex != -1) 	//�ҵ�BEEP������
	{
		BEEP=0;  		//�ر�
		for(i = 0;i < iNumParams;i++)
		{
			if(strcmp(pcParam[i],"BEEP") == 0)  //����CGI����
			{
				if(strcmp(pcValue[i],"BEEPON") == 0) //��BEEP
					BEEP = 1;
				else if(strcmp(pcValue[i],"BEEPOFF") == 0) //�ر�BEEP
					BEEP = 0;
			}
		}
	}
	if(LED2==0&&BEEP==0)		return "/STM32_LED_ON_BEEP_OFF.shtml";	//LED1��,BEEP��
	else if(LED2==0&&BEEP==1)	return "/STM32_LED_ON_BEEP_ON.shtml";	//LED1��,BEEP��
	else if(LED2==1&&BEEP==1)	return "/STM32_LED_OFF_BEEP_ON.shtml";	//LED1��,BEEP��
	else return "/STM32_LED_OFF_BEEP_OFF.shtml";						//LED1��,BEEP��
*/
}

//SSI�����ʼ��
void httpd_ssi_init(void)
{
	//����SSI���
	http_set_ssi_handler(SSIHandler,ppcTAGs,NUM_CONFIG_SSI_TAGS);
}

//CGI�����ʼ��
void httpd_cgi_init(void)
{
	//����CGI���
	http_set_cgi_handlers(ppcURLs, NUM_CONFIG_CGI_URIS);
}







