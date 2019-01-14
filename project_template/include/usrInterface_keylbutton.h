#ifndef __USRINTERFACE_KEYLBUTTON_H__
#define __USRINTERFACE_KEYLBUTTON_H__

#include "esp_common.h"

#define Dcode0	usrDats_sensor.usrDcode_0
#define Dcode1	usrDats_sensor.usrDcode_1
#define Dcode2	usrDats_sensor.usrDcode_2
#define Dcode3	usrDats_sensor.usrDcode_3

#define Dcode_FLG_ifAP			0x01
#define Dcode_FLG_ifMemory		0x02
#define Dcode_FLG_bitReserve	0x0C	

#define Dcode_bitReserve(x)		((x & 0x0C) >> 2)

typedef enum{

	press_Null = 1,
	press_Short,
	press_ShortCnt, //带连按的短按
	press_LongA,
	press_LongB,
}keyCfrm_Type;

typedef struct{

	u8 param_combinationFunPreTrig_standBy_FLG:1; //预触发标志
	u8 param_combinationFunPreTrig_standBy_keyVal:7; //预触发按键键值缓存
}param_combinationFunPreTrig;

typedef void funKey_Callback(void);

extern u8 	timeCounter_smartConfig_start;
extern bool smartconfigOpen_flg;

extern u16 	touchPadActCounter; 
extern u16 	touchPadContinueCnt;  

extern bool usrKeyCount_EN;
extern u16	usrKeyCount;

extern u16  combinationFunFLG_3S5S_cancel_counter;

void usrSmartconfig_stop(void);

void usrInterface_ThreadStart(void);

#endif

