#include <string.h>
#include <stdlib.h>
#include "lwip/debug.h"
#include "httpd.h"
#include "lwip/tcp.h"
#include "fs.h"
#include "dht11.h"
#include "ls1b_gpio.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//All rights reserved
//*******************************************************************************
//修改信息
//无
////////////////////////////////////////////////////////////////////////////////// 	
#define NUM_CONFIG_CGI_URIS	2  //CGI的URI数量
#define NUM_CONFIG_SSI_TAGS	4  //SSI的TAG数量

//控制LED和BEEP的CGI handler
const char* LEDS_CGI_Handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]);
const char* BEEP_CGI_Handler(int iIndex,int iNumParams,char *pcParam[],char *pcValue[]);

static const char *ppcTAGs[]=  //SSI的Tag
{
	"w", //温度值
	"h", //湿度值
	"p", //PH值
	"k"  //钾肥
};

static const tCGI ppcURLs[]= //cgi程序
{
	{"/leds.cgi",LEDS_CGI_Handler},
	{"/beep.cgi",BEEP_CGI_Handler},
};


//当web客户端请求浏览器的时候,使用此函数被CGI handler调用
static int FindCGIParameter(const char *pcToFind,char *pcParam[],int iNumParams)
{
	int iLoop;
	for(iLoop = 0;iLoop < iNumParams;iLoop ++ )
	{
		if(strcmp(pcToFind,pcParam[iLoop]) == 0)
		{
			return (iLoop); //返回iLOOP
		}
	}
	return (-1);
}


//SSIHandler中需要用到的处理ADC的函数
void Temper_Handler(char *pcInsert)
{
	char Digit1=0, Digit2=0, Digit3=0, Digit4=0;//,Digit5=0;
	Digit1 = temperature / 10;
	Digit2 = temperature% 10;
  //  Digit5 = ((short)Temperate) % 10;
	//添加到html中的数据
	*pcInsert 	  = (char)(Digit1+0x30);
	*(pcInsert+1) = (char)(Digit2+0x30);
	*(pcInsert+2) = '\0';
//	*(pcInsert+2) =	(char)(0x20);
//	*(pcInsert+3) = (char)(0x20);
//	*(pcInsert+4) = (char)(Digit3+0x30);
//	*(pcInsert+5) = (char)(Digit4+0x30);
}

//SSIHandler中需要用到的处理内部温度传感器的函数
void Humidity_Handler(char *pcInsert)
{
	char Digit1=0, Digit2=0, Digit3=0, Digit4=0;//
	Digit1 = humidity / 10;
	Digit2 = humidity% 10;
	*pcInsert 	  = (char)(Digit1+0x30);
	*(pcInsert+1) = (char)(Digit2+0x30);
     *(pcInsert+2) = '\0';
}

//SSIHandler中需要用到的处理RTC时间的函数
void PH_Handler(char *pcInsert)
{
	static unsigned char PH=10;
	*pcInsert = 	(char)((PH/10) + 0x30);
	*(pcInsert+1) = (char)((PH%10) + 0x30);
    *(pcInsert+2) = '\0';
	if(++PH>99)
       PH=10;
}

//SSIHandler中需要用到的处理RTC日期的函数
void Ka_Handler(char *pcInsert)
{
	static unsigned char PH=99;
	*pcInsert = 	(char)((PH/10) + 0x30);
	*(pcInsert+1) = (char)((PH%10) + 0x30);
	*(pcInsert+2) = '\0';
	if(--PH<10)
       PH=99;
}
//SSI的Handler句柄
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

//CGI LED控制句柄
const char* LEDS_CGI_Handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
	unsigned char i=0;  //注意根据自己的GET的参数的多少来选择i值范围
	//iIndex = FindCGIParameter("button1",pcParam,iNumParams);  //找到led的索引号
	//for (i=0; i<iNumParams; i++) //检查CGI参数
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

	//只有一个CGI句柄 iIndex=0
/*	if (iIndex != -1)
	{
		LED2=1;  //关闭LED1灯
		for (i=0; i<iNumParams; i++) //检查CGI参数
		{
		  if (strcmp(pcParam[i] , "LED1")==0)  //检查参数"led" 属于控制LED1灯的
		  {
			if(strcmp(pcValue[i], "LED1ON") ==0)  //改变LED1状态
				LED2=0; //打开LED1
			else if(strcmp(pcValue[i],"LED1OFF") == 0)
				LED2=1; //关闭LED1
		  }
		}
	 }
//	if(LED2==0&&BEEP==0)		return "/STM32_LED_ON_BEEP_OFF.shtml";	//LED1开,BEEP关
//	else if(LED2==0&&BEEP==1) 	return "/STM32_LED_ON_BEEP_ON.shtml";	//LED1开,BEEP开
//	else if(LED2==1&&BEEP==1) 	return "/STM32_LED_OFF_BEEP_ON.shtml";	//LED1关,BEEP开
//	else return "/STM32_LED_OFF_BEEP_OFF.shtml";						//LED1关,BEEP关
*/

   return "/index.shtml";						//LED1关,BEEP关
}

//BEEP的CGI控制句柄

const char *BEEP_CGI_Handler(int iIndex,int iNumParams,char *pcParam[],char *pcValue[])
{
	unsigned char i=0;
/*	iIndex = FindCGIParameter("BEEP",pcParam,iNumParams);  //找到BEEP的索引号
	if(iIndex != -1) 	//找到BEEP索引号
	{
		BEEP=0;  		//关闭
		for(i = 0;i < iNumParams;i++)
		{
			if(strcmp(pcParam[i],"BEEP") == 0)  //查找CGI参数
			{
				if(strcmp(pcValue[i],"BEEPON") == 0) //打开BEEP
					BEEP = 1;
				else if(strcmp(pcValue[i],"BEEPOFF") == 0) //关闭BEEP
					BEEP = 0;
			}
		}
	}
	if(LED2==0&&BEEP==0)		return "/STM32_LED_ON_BEEP_OFF.shtml";	//LED1开,BEEP关
	else if(LED2==0&&BEEP==1)	return "/STM32_LED_ON_BEEP_ON.shtml";	//LED1开,BEEP开
	else if(LED2==1&&BEEP==1)	return "/STM32_LED_OFF_BEEP_ON.shtml";	//LED1关,BEEP开
	else return "/STM32_LED_OFF_BEEP_OFF.shtml";						//LED1关,BEEP关
*/
}

//SSI句柄初始化
void httpd_ssi_init(void)
{
	//配置SSI句柄
	http_set_ssi_handler(SSIHandler,ppcTAGs,NUM_CONFIG_SSI_TAGS);
}

//CGI句柄初始化
void httpd_cgi_init(void)
{
	//配置CGI句柄
	http_set_cgi_handlers(ppcURLs, NUM_CONFIG_CGI_URIS);
}







