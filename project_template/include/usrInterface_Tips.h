#ifndef __USRINTERFACE_TIPS_H__
#define __USRINTERFACE_TIPS_H__

#include "esp_common.h"

#define TIPS_SWFREELOOP_TIME	10 //�û������ͷ�ʱ�����ڶ��� ��λ��s

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

	devNwkStaute_zigbNwkNormalOnly = 0, //��zigb��������
	devNwkStaute_wifiNwkNormalOnly, //��wifi��������
	devNwkStaute_nwkAllNormal, //zigb��wifi���綼����
	devNwkStaute_nwkAllAbnormal, //zigb��wifi���綼�쳣
	devNwkStaute_zigbNwkOpen, //zigb���翪����
	devNwkStaute_wifiNwkFind, //wifi smartConfig��
}tips_devNwkStatus;


extern u8 counter_tipsAct;
extern u8 counter_ifTipsFree;

extern u8 timeCount_zigNwkOpen;

extern tips_Status devTips_status;
extern tips_devNwkStatus devNwkTips_status;

void ledBKGColorSw_Reales(void);

void tips_statusChangeToNormal(void);
void tips_statusChangeToAPFind(void);
void tips_statusChangeToZigbNwkOpen(u8 timeopen);
void tipsLED_rgbColorSet(u8 tipsRly_Num, u8 color_R, u8 color_G, u8 color_B);
void usrTips_ThreadStart(void);

#endif

