#ifndef __TIM_ACTIVING_H__
#define __TIM_ACTIVING_H__

#include "esp_common.h"

#define timCount_ENABLE		0x01
#define timCount_DISABLE	0x00

#define TIMEER_TABLENGTH	8

typedef struct{

	u8 time_Year;
	u8 time_Month;
	u8 time_Week;
	u8 time_Day;
	u8 time_Hour;
	u8 time_Minute;
	u8 time_Second;
}stt_localTime;

typedef struct{

	u8 	Week_Num	:7;	//��ֵռλ
	u8	if_Timing	:1;	//��ʱ�Ƿ���
	u8	Status_Act	:3;	//��ʱ������̵���Ӧ����Ӧ��״̬
	u8	Hour		:5;	//ʱ�̣�ʱ
	u8	Minute;		//ʱ�̣���
}timing_Dats;

extern sint8 sysTimeZone_H;
extern sint8 sysTimeZone_M;

extern u32_t systemUTC_current;
extern u16	 sysTimeKeep_counter;
extern stt_localTime systemTime_current;

extern u8 	ifDelay_sw_running_FLAG;//��ʱ����ɫģʽ�Ƿ����б�־λ
extern u16	delayCnt_onoff;			//��ʱ������ʱ����
extern u8	delayPeriod_onoff;		//��ʱ��������
extern u8	delayUp_act;			//��ʱ��Ӧ���嶯��
extern u16	delayCnt_closeLoop;		//��ɫģʽ��ʱ����
extern u8	delayPeriod_closeLoop;	//��ɫģʽ��������

extern bool	ifNightMode_sw_running_FLAG; //�豸ҹ��ģʽ���б�־

void sntp_timerActThread_Start(void);
void timActing_ThreadStart(void);

void timeZone_Reales(void);
void datsTiming_getRealse(void);
void datsDelayOP_getReales(void);

void localTimerPause_sntpTimerAct(void);
void localTimerRecover_sntpTimerAct(void);

#endif

