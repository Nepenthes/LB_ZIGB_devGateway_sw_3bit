#include "timer_Activing.h"

#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "espressif/espconn.h"
#include "espressif/airkiss.h"

#include "hwPeripherial_Actuator.h"
#include "datsManage.h"
#include "usrInterface_Tips.h"
#include "usrInterface_keylbutton.h"

extern bool nwkInternetOnline_IF;
extern xQueueHandle xMsgQ_timeStampGet;

sint8 sysTimeZone_H = 0; /*时区：时*///暂定东八区
sint8 sysTimeZone_M = 0; /*时区：分*/

u32_t systemUTC_current = 0; //系统本地UTC时间
stt_localTime systemTime_current = {0}; //系统本地时间
u16	sysTimeKeep_counter	= 0; //系统本地时间维持更新计时

/*因频繁查询flash会导致定时器表现不稳，业务定时表改为全局变量，将仅在定时表变更时读写flash，减少读写次数*/
timing_Dats timDatsTemp_CalibrateTab[4] = {0}; 		//普通时刻定时表
u8 			swTim_onShoot_FLAG 			= 0; 	 	//普通时刻定时一次性使能标志
bool 		ifTim_sw_running_FLAG 		= false; 	//普通时刻定时正在运行标志

timing_Dats nightDatsTemp_CalibrateTab[2] = {0};	//设备夜间模式定时表
bool		ifNightMode_sw_running_FLAG	  = false;	//设备夜间模式运行标志位

u8 	ifDelay_sw_running_FLAG	= 0;	//延时及绿色模式是否运行标志位 bit1-延时标志位 bit0-绿色模式标志位
u16	delayCnt_onoff			= 0;	//延时动作计时计数
u8	delayPeriod_onoff		= 0;	//延时动作周期
u8	delayUp_act				= 0;	//延时响应具体动作
u16	delayCnt_closeLoop		= 0;	//绿色模式计时计数
u8	delayPeriod_closeLoop	= 0;	//绿色模式动作周期

LOCAL os_timer_t timer_sntpTimerAct;
LOCAL xTaskHandle pxTaskHandle_threadtimActing;
/*---------------------------------------------------------------------------------------------*/

LOCAL void ICACHE_FLASH_ATTR timerFunCB_sntpTimerInit(void *para);

LOCAL void ICACHE_FLASH_ATTR
devStnp_Init(void){

	sntp_stop();
	while(false == sntp_set_timezone(0))vTaskDelay(100); //sntp时区设为 0，直接获取UTC时间
	sntp_setservername(0, "0.cn.pool.ntp.org");
	sntp_setservername(1, "1.cn.pool.ntp.org");
	sntp_setservername(2, "2.cn.pool.ntp.org");
	sntp_init();
	vTaskDelay(1000 / portTICK_RATE_MS);
}

LOCAL void ICACHE_FLASH_ATTR
timerFunCB_sntpTimerAct(void *para){

	static u8 timeLog_Cnt = 0;
	const  u8 timeLog_Period = 3;
	u32_t timeStmap_temp = 0UL;

	if(timeLog_Cnt < timeLog_Period)timeLog_Cnt ++;
	else{

		timeLog_Cnt = 0;

		struct ip_info ipConfig_info;
		if(wifi_station_get_connect_status() == STATION_GOT_IP && ipConfig_info.ip.addr != 0){

			nwkInternetOnline_IF = true;

			usrSmartconfig_stop(); //smartconfig尝试关闭
				
			timeStmap_temp = sntp_get_current_timestamp();
			if(timeStmap_temp){

				os_printf("[Tips_timer]: date:%s\n", sntp_get_real_time(timeStmap_temp));

				xQueueSend(xMsgQ_timeStampGet, (void *)&timeStmap_temp, 10);
			}
			
		}else{

			nwkInternetOnline_IF = false;

			os_timer_disarm(&timer_sntpTimerAct);
			os_printf("[Tips_timer]: sntp outline, reDetecting!!!\n");
			os_timer_setfn(&timer_sntpTimerAct, timerFunCB_sntpTimerInit, NULL);
			os_timer_arm(&timer_sntpTimerAct, 1000, false);
		}
	}
}

LOCAL void ICACHE_FLASH_ATTR
timerFunCB_sntpTimerInit(void *para){

	struct ip_info ipConfig_info;

	os_timer_disarm(&timer_sntpTimerAct);

	wifi_get_ip_info(STATION_IF, &ipConfig_info);
	if(wifi_station_get_connect_status() == STATION_GOT_IP && ipConfig_info.ip.addr != 0){

		devStnp_Init();
		os_printf("[Tips_timer]: sntp init complete, timerAct start!!!\n");
		
		os_timer_setfn(&timer_sntpTimerAct, timerFunCB_sntpTimerAct, NULL);
		os_timer_arm(&timer_sntpTimerAct, 1000, true);

	}else{

		os_timer_setfn(&timer_sntpTimerAct, timerFunCB_sntpTimerInit, NULL);
		os_timer_arm(&timer_sntpTimerAct, 1000, false);
	}
}

LOCAL void ICACHE_FLASH_ATTR
localSystime_logOut(void){

	os_printf(	"\n>>===时间戳===<<\n    20%d/%02d/%02d-W%01d\n        %02d:%02d:%02d\n timeZone_H:%02d.\n", 
				(int)systemTime_current.time_Year,
				(int)systemTime_current.time_Month,
				(int)systemTime_current.time_Day,
				(int)systemTime_current.time_Week,
				(int)systemTime_current.time_Hour,
				(int)systemTime_current.time_Minute,
				(int)systemTime_current.time_Second,
				(int)sysTimeZone_H);
	
   os_printf("reserve heap : %d. \n", system_get_free_heap_size());
}

void ICACHE_FLASH_ATTR
localTimerPause_sntpTimerAct(void){

	os_timer_disarm(&timer_sntpTimerAct);
}

void ICACHE_FLASH_ATTR
localTimerRecover_sntpTimerAct(void){

	sntp_timerActThread_Start();
}

LOCAL bool ICACHE_FLASH_ATTR
weekend_judge(u8 weekNum, u8 HoldNum){

	u8 loop;
	
	weekNum --;
	for(loop = 0; loop < 7; loop ++){
	
		if(HoldNum & (1 << loop)){
			
			if(loop == weekNum)return true;
		}
	}
	
	return false;
}

void ICACHE_FLASH_ATTR
timeZone_Reales(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();

	sysTimeZone_H = datsRead_Temp->timeZone_H;
	sysTimeZone_M = datsRead_Temp->timeZone_M;

	if(datsRead_Temp)os_free(datsRead_Temp);
}

void ICACHE_FLASH_ATTR
datsTiming_getRealse(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();
	u8 loop = 0;
	
	for(loop = 0; loop < 4; loop ++){ //普通开关动作定时信息更新
	
		timDatsTemp_CalibrateTab[loop].Week_Num		= (datsRead_Temp->swTimer_Tab[loop * 3 + 0] & 0x7f) >> 0;	
		timDatsTemp_CalibrateTab[loop].if_Timing 	= (datsRead_Temp->swTimer_Tab[loop * 3 + 0] & 0x80) >> 7;	
		timDatsTemp_CalibrateTab[loop].Status_Act	= (datsRead_Temp->swTimer_Tab[loop * 3 + 1] & 0xe0) >> 5;	
		timDatsTemp_CalibrateTab[loop].Hour			= (datsRead_Temp->swTimer_Tab[loop * 3 + 1] & 0x1f) >> 0;	
		timDatsTemp_CalibrateTab[loop].Minute		= (datsRead_Temp->swTimer_Tab[loop * 3 + 2] & 0xff) >> 0;	
	}

	for(loop = 0; loop < 2; loop ++){ //设备夜间模式定时信息更新
	
		nightDatsTemp_CalibrateTab[loop].Week_Num	= (datsRead_Temp->devNightModeTimer_Tab[loop * 3 + 0] & 0x7f) >> 0;	
		nightDatsTemp_CalibrateTab[loop].if_Timing 	= (datsRead_Temp->devNightModeTimer_Tab[loop * 3 + 0] & 0x80) >> 7;	
		nightDatsTemp_CalibrateTab[loop].Status_Act	= (datsRead_Temp->devNightModeTimer_Tab[loop * 3 + 1] & 0xe0) >> 5;	
		nightDatsTemp_CalibrateTab[loop].Hour		= (datsRead_Temp->devNightModeTimer_Tab[loop * 3 + 1] & 0x1f) >> 0;	
		nightDatsTemp_CalibrateTab[loop].Minute		= (datsRead_Temp->devNightModeTimer_Tab[loop * 3 + 2] & 0xff) >> 0;	
	}

	if(datsRead_Temp)os_free(datsRead_Temp);
}

void ICACHE_FLASH_ATTR
datsDelayOP_getReales(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();
	
	ifDelay_sw_running_FLAG = datsRead_Temp->swDelay_flg;
	delayPeriod_closeLoop = datsRead_Temp->swDelay_periodCloseLoop;

	ifDelay_sw_running_FLAG &= ~(1 << 1); //重启延时失效

	if(datsRead_Temp)os_free(datsRead_Temp);
}


LOCAL void ICACHE_FLASH_ATTR
timActingProcess_task(void *pvParameters){

	stt_usrDats_privateSave datsSave_Temp = {0};
	stt_usrDats_privateSave *datsRead_Temp;

	u8 loop = 0;

	for(;;){

		{ //调试代码，当前系统本地时间log输出
			
			u16 log_period = 300;
			static u16 log_Count = 0;
			
			if(log_Count < log_period)log_Count ++;
			else{
			
				log_Count = 0;
				
				localSystime_logOut();
			}
		}
		
		{ /*系统本地时间自更新*///zigbee时间查询周期之外
			
			systemTime_current.time_Minute = sysTimeKeep_counter / 60;
			systemTime_current.time_Second = sysTimeKeep_counter % 60;
			
			if(sysTimeKeep_counter < 3600){
				
			}else{
			
				sysTimeKeep_counter = 0;
				(systemTime_current.time_Hour >= 24)?(systemTime_current.time_Hour = 0):(systemTime_current.time_Hour ++);
				(systemTime_current.time_Week >   7)?(systemTime_current.time_Week = 1):(systemTime_current.time_Week ++);
			}
		}

		/*设备夜间模式定时业务*/
		if((nightDatsTemp_CalibrateTab[0].Week_Num & 0x7F) == 0x7F){ //全天判断，若定时段一周占位全满则为全天循环
		
			ifNightMode_sw_running_FLAG = true;
			
		}else{
			
			if(nightDatsTemp_CalibrateTab[0].if_Timing){ //定时使能判断
			
				if(((u16)systemTime_current.time_Hour * 60 + (u16)systemTime_current.time_Minute) >=  ((u16)nightDatsTemp_CalibrateTab[0].Hour * 60 + (u16)nightDatsTemp_CalibrateTab[0].Minute) && //定时区间判断
				   ((u16)systemTime_current.time_Hour * 60 + (u16)systemTime_current.time_Minute) < ((u16)nightDatsTemp_CalibrateTab[1].Hour * 60 + (u16)nightDatsTemp_CalibrateTab[1].Minute)){
				   
					ifNightMode_sw_running_FLAG = true;
					   
				}else{
				
					ifNightMode_sw_running_FLAG = false;
				}
				
			}else{
			
				ifNightMode_sw_running_FLAG = false;
			}
		}


		/*所有普通开关定时业务*/
		if((timDatsTemp_CalibrateTab[0].if_Timing == 0) &&	//若全关，更新标志位
		   (timDatsTemp_CalibrateTab[1].if_Timing == 0) &&
		   (timDatsTemp_CalibrateTab[2].if_Timing == 0) &&
		   (timDatsTemp_CalibrateTab[3].if_Timing == 0)
		  ){
		  
			ifTim_sw_running_FLAG = 0; 
			  
		}else{ //非全关，更新标志位，并执行逻辑
			
			ifTim_sw_running_FLAG = 1; 
		
			for(loop = 0; loop < 4; loop ++){
				
				if(true == weekend_judge(systemTime_current.time_Week, timDatsTemp_CalibrateTab[loop].Week_Num)){ //周占位对比
				
					if(timCount_ENABLE == timDatsTemp_CalibrateTab[loop].if_Timing){ //定时开启标志核对
						
						if(((u16)systemTime_current.time_Hour * 60 + (u16)systemTime_current.time_Minute) ==	\
						   ((u16)timDatsTemp_CalibrateTab[loop].Hour * 60 + (u16)timDatsTemp_CalibrateTab[loop].Minute) && //定时时刻核对，正确时刻前10s都是响应期
						   ((u16)systemTime_current.time_Second <= 10)){  //
							   
							//一次性定时判断
							if(swTim_onShoot_FLAG & (1 << loop)){ //是否为一次性定时，是则清空当前段定时信息
								
								swTim_onShoot_FLAG &= ~(1 << loop);

								datsRead_Temp = devParam_flashDataRead(); //定时表缓存更新
								memcpy(datsSave_Temp.swTimer_Tab, datsRead_Temp->swTimer_Tab, 3 * 4); 
								if(datsRead_Temp)os_free(datsRead_Temp);
								datsSave_Temp.swTimer_Tab[loop * 3] = 0; //定时表缓存局部清除
								devParam_flashDataSave(obj_swTimer_Tab, datsSave_Temp); //存储数据更新
								datsTiming_getRealse(); //本地运行数据更新
							}
						   
							//继电器动作响应
							swCommand_fromUsr.actMethod = relay_OnOff;
							swCommand_fromUsr.objRelay = timDatsTemp_CalibrateTab[loop].Status_Act;
							devStatus_pushIF = true; //开关状态数据推送
							
						}
						else
						if(((u16)systemTime_current.time_Hour * 60 + (u16)systemTime_current.time_Minute) > //定时时刻核对，大于正确时刻标志清除
						   ((u16)timDatsTemp_CalibrateTab[loop].Hour * 60 + (u16)timDatsTemp_CalibrateTab[loop].Minute)){
							   
						   //一次性定时判断
						   if(swTim_onShoot_FLAG & (1 << loop)){ //是否为一次性定时，是则清空当前段定时信息
							   
							   swTim_onShoot_FLAG &= ~(1 << loop);
						   
							   datsRead_Temp = devParam_flashDataRead(); //定时表缓存更新
							   memcpy(datsSave_Temp.swTimer_Tab, datsRead_Temp->swTimer_Tab, 3 * 4); 
							   if(datsRead_Temp)os_free(datsRead_Temp);
							   datsSave_Temp.swTimer_Tab[loop * 3] = 0; //定时表缓存局部清除
							   devParam_flashDataSave(obj_swTimer_Tab, datsSave_Temp); //存储数据更新
							   datsTiming_getRealse(); //本地运行数据更新
						   }
						}
					}
				}
			}
		}


		vTaskDelay(1);
	}

	vTaskDelete(NULL);
}

void ICACHE_FLASH_ATTR
sntp_timerActThread_Start(void){

	os_timer_disarm(&timer_sntpTimerAct);
	os_timer_setfn(&timer_sntpTimerAct, timerFunCB_sntpTimerInit, NULL);
	os_timer_arm(&timer_sntpTimerAct, 1000, false);
}

void ICACHE_FLASH_ATTR
timActing_ThreadStart(void){

	portBASE_TYPE xReturn = pdFAIL;

	xReturn = xTaskCreate(timActingProcess_task, "Process_timActing", 512, (void *)NULL, 4, &pxTaskHandle_threadtimActing);

	os_printf("\npxTaskHandle_threadTimActing is %d\n", pxTaskHandle_threadtimActing);
}














