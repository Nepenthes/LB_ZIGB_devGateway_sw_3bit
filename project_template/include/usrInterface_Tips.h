#ifndef __USRINTERFACE_TIPS_H__
#define __USRINTERFACE_TIPS_H__

#include "esp_common.h"

#define TIPS_SWFREELOOP_TIME	60 //用户操作释放时长周期定义 单位：s

#define DEFULAT_COLORTAB_NUM	10 //色值表数目

#define TIPSBKCOLOR_DEFAULT_ON	8  //默认开关触摸背景色：开启
#define TIPSBKCOLOR_DEFAULT_OFF 5  //默认开关触摸背景色：关闭

typedef enum{

	status_Null = 0,
	status_sysStandBy,
	status_keyFree,
	status_Normal,
	status_Night,
	status_tipsNwkOpen,
	status_tipsAPFind,
}tips_Status;

typedef enum{

	devNwkStaute_zigbNwkNormalOnly = 0, //仅zigb网络正常
	devNwkStaute_wifiNwkNormalOnly, //仅wifi网络正常
	devNwkStaute_nwkAllNormal, //zigb、wifi网络都正常
	devNwkStaute_nwkAllAbnormal, //zigb、wifi网络都异常
	devNwkStaute_zigbNwkOpen, //zigb网络开放中
	devNwkStaute_wifiNwkFind, //wifi smartConfig中
}tips_devNwkStatus;

typedef struct{

	u8 tips_Period:3;
	u8 tips_time;
	u8 tips_loop:5;
}sound_Attr;

typedef enum beepsMode{

	beepsMode_null = 0,
	beepsMode_standBy,
	beepsWorking,
	beepsComplete,
}enum_beeps;

extern u8 counter_tipsAct;
extern u8 counter_ifTipsFree;

extern u8 timeCount_zigNwkOpen;

extern tips_Status devTips_status;
extern tips_devNwkStatus devNwkTips_status;

extern enum_beeps dev_statusBeeps;
extern sound_Attr devTips_beep;

void ledBKGColorSw_Reales(void);

void beeps_usrActive(u8 tons, u8 time, u8 loop);
void tips_statusChangeToNormal(void);
void tips_statusChangeToAPFind(void);
void tips_statusChangeToZigbNwkOpen(u8 timeopen);
void tipsLED_rgbColorSet(u8 tipsRly_Num, u8 color_R, u8 color_G, u8 color_B);
void usrTips_ThreadStart(void);

#endif

