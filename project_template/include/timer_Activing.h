#ifndef __TIM_ACTIVING_H__
#define __TIM_ACTIVING_H__

#include "esp_common.h"

#define timCount_ENABLE		0x01
#define timCount_DISABLE	0x00

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

	u8 		  Week_Num	:7;	//周值占位
	u8		  if_Timing	:1;	//定时是否开启
	u8		  Status_Act:3;	//定时出发后继电器应该响应的状态
	u8		  Hour		:5;	//时刻：时
	u8		  Minute;		//时刻：分
}timing_Dats;

extern sint8 sysTimeZone_H;
extern sint8 sysTimeZone_M;

extern u32_t systemUTC_current;
extern u16	 sysTimeKeep_counter;
extern stt_localTime systemTime_current;

extern u8 	ifDelay_sw_running_FLAG;//延时及绿色模式是否运行标志位
extern u16	delayCnt_onoff;			//延时动作计时计数
extern u8	delayPeriod_onoff;		//延时动作周期
extern u8	delayUp_act;			//延时响应具体动作
extern u16	delayCnt_closeLoop;		//绿色模式计时计数
extern u8	delayPeriod_closeLoop;	//绿色模式动作周期

extern bool	ifNightMode_sw_running_FLAG; //设备夜间模式运行标志

void sntp_timerActThread_Start(void);
void timActing_ThreadStart(void);

void timeZone_Reales(void);
void datsTiming_getRealse(void);
void datsDelayOP_getReales(void);

void localTimerPause_sntpTimerAct(void);
void localTimerRecover_sntpTimerAct(void);

#endif

