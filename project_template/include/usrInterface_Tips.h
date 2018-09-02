#ifndef __USRINTERFACE_TIPS_H__
#define __USRINTERFACE_TIPS_H__

#include "esp_common.h"

#define TIPS_SWFREELOOP_TIME	10 //用户操作释放时长周期定义 单位：s

typedef enum{

	status_Null = 0,
	status_sysStandBy,
	status_keyFree,
	status_Normal,
}tips_Status;

extern u8 counter_ifTipsFree;

void ledBKGColorSw_Reales(void);

void tips_statusChangeToNormal(void);
void tipsLED_rgbColorSet(u8 tipsRly_Num, u8 color_R, u8 color_G, u8 color_B);
void usrTips_ThreadStart(void);

#endif

