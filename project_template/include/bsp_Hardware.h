#ifndef __BSP_HARDWARE_H__
#define __BSP_HARDWARE_H__

#include "esp_common.h"

#define HARDWARE_VERSION_DEBUG	1

#if(HARDWARE_VERSION_DEBUG == 1)

#define spi595U7_pinRck(a)	GPIO_OUTPUT_SET(GPIO_ID_PIN(0), a)	//597SCK引脚与PL引脚共用，省下一个pin
#define spi595_pinDout(a)	GPIO_OUTPUT_SET(GPIO_ID_PIN(4), a)
#define spi595U7_pinClk(a)	GPIO_OUTPUT_SET(GPIO_ID_PIN(5), a)	
#define spi597_pinDin()		gpio16_input_get()
#define TIPS_RGBSET_G(a)	GPIO_OUTPUT_SET(GPIO_ID_PIN(13), a)	
#define TIPS_RGBSET_B(a)	GPIO_OUTPUT_SET(GPIO_ID_PIN(14), a)	
#define TIPS_RGBSET_R(a)	GPIO_OUTPUT_SET(GPIO_ID_PIN(15), a)	

#else

#define spi595U7_pinRck(a)	GPIO_OUTPUT_SET(GPIO_ID_PIN(0), a)	//597SCK引脚与PL引脚共用，省下一个pin
#define spi595_pinDout(a)	GPIO_OUTPUT_SET(GPIO_ID_PIN(4), a)	
#define spi595U7_pinClk(a)	GPIO_OUTPUT_SET(GPIO_ID_PIN(5), a)	
#define spi597_pinDin()		gpio16_input_get()
#define TIPS_RGBSET_B(a)	GPIO_OUTPUT_SET(GPIO_ID_PIN(14), a)	
#define TIPS_RGBSET_G(a)	GPIO_OUTPUT_SET(GPIO_ID_PIN(13), a)	
#define TIPS_RGBSET_R(a)	GPIO_OUTPUT_SET(GPIO_ID_PIN(12), a)

#endif

//#define DUTYSET_CHANNAL_R	0
//#define DUTYSET_CHANNAL_G	1
//#define DUTYSET_CHANNAL_B	2

//#define TIPS_LEDRGB_PERIOD_MAX	22222
//#define TIPS_LEDRGB_PERIOD		1000
//#define TIPS_LEDRGB_DUTYUINT	TIPS_LEDRGB_PERIOD * 10 / 45 

#define RGB_ENABLE				0
#define RGB_DISABLE				1

#define TIPS_BASECOLOR_NUM		3	//底色数量 RGB三种
#define RLY_TIPS_PERIODUNITS	32	//颜色调制周期
#define RLY_TIPS_NUM			4	//tipsLED灯数量

typedef struct{

	u8 usrKeyIn_fun_0:1;
	u8 usrKeyIn_rly_2:1;
	u8 usrKeyIn_rly_1:1;
	u8 usrKeyIn_rly_0:1;
	u8 usrDcode_3:1;
	u8 usrDcode_2:1;
	u8 usrDcode_1:1;
	u8 usrDcode_0:1;	
}stt_HC597_datsIn;

typedef struct{

	u8 color_R:5;
	u8 color_G:5;
	u8 color_B:5;
}func_ledRGB;

typedef struct{

	u8 conDatsOut_ZigbeeRst:1;
	u8 conDatsOut_usrFunTips0:1;
	u8 conDatsOut_rly_0:1;
	u8 conDatsOut_rly_1:1;
	u8 conDatsOut_rly_2:1;
	func_ledRGB func_tipsLedRGB[RLY_TIPS_NUM];
}stt_HC595_datsOut;

extern stt_HC597_datsIn		usrDats_sensor;
extern stt_HC595_datsOut 	usrDats_actuator;

void dats595and597_keepRealesingStart(void);

#endif