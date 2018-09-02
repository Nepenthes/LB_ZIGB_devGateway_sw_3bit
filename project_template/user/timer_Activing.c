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


extern bool nwkInternetOnline_IF;
extern xQueueHandle xMsgQ_timeStampGet;

sint8 sysTimeZone_H = 8; /*ʱ����ʱ*///�ݶ�������
sint8 sysTimeZone_M = 0; /*ʱ������*/

stt_localTime systemTime_current = {0}; //ϵͳ����ʱ��
u16	sysTimeKeep_counter	= 0; //ϵͳ����ʱ��ά�ָ��¼�ʱ

timing_Dats timDatsTemp_CalibrateTab[4] = {0}; 		//��ͨʱ�̶�ʱ��
u8 			swTim_onShoot_FLAG 			= 0; 	 	//��ͨʱ�̶�ʱһ����ʹ�ܱ�־
bool 		ifTim_sw_running_FLAG 		= false; 	//��ͨʱ�̶�ʱ�������б�־

u8 	ifDelay_sw_running_FLAG	= 0;	//��ʱ����ɫģʽ�Ƿ����б�־λ bit1-��ʱ��־λ bit0-��ɫģʽ��־λ
u16	delayCnt_onoff			= 0;	//��ʱ������ʱ����
u8	delayPeriod_onoff		= 0;	//��ʱ��������
u8	delayUp_act				= 0;	//��ʱ��Ӧ���嶯��
u16	delayCnt_closeLoop		= 0;	//��ɫģʽ��ʱ����
u8	delayPeriod_closeLoop	= 0;	//��ɫģʽ��������

LOCAL os_timer_t timer_sntpTimerAct;
LOCAL xTaskHandle pxTaskHandle_threadtimActing;
/*---------------------------------------------------------------------------------------------*/

LOCAL void ICACHE_FLASH_ATTR timerFunCB_sntpTimerInit(void *para);

LOCAL void ICACHE_FLASH_ATTR
devStnp_Init(void){

	sntp_stop();
	while(false == sntp_set_timezone(sysTimeZone_H))vTaskDelay(100); 
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
	u32_t timeStmap_temp = 0;

	if(timeLog_Cnt < timeLog_Period)timeLog_Cnt ++;
	else{

		timeLog_Cnt = 0;

		struct ip_info ipConfig_info;
		if(wifi_station_get_connect_status() == STATION_GOT_IP && ipConfig_info.ip.addr != 0){

			nwkInternetOnline_IF = true;
				
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

	os_printf(	"\n>>===ʱ���===<<\n    20%d/%02d/%02d-W%01d\n        %02d:%02d:%02d\n", 
				(int)systemTime_current.time_Year,
				(int)systemTime_current.time_Month,
				(int)systemTime_current.time_Day,
				(int)systemTime_current.time_Week,
				(int)systemTime_current.time_Hour,
				(int)systemTime_current.time_Minute,
				(int)systemTime_current.time_Second	);
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
	sysTimeZone_H = datsRead_Temp->timeZone_H;

	os_free(datsRead_Temp);
}

void ICACHE_FLASH_ATTR
datsTiming_getRealse(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();
	u8 loop = 0;
	
	for(loop = 0; loop < 4; loop ++){
	
		timDatsTemp_CalibrateTab[loop].Week_Num		= (datsRead_Temp->swTimer_Tab[loop * 3 + 0] & 0x7f) >> 0;	
		timDatsTemp_CalibrateTab[loop].if_Timing 	= (datsRead_Temp->swTimer_Tab[loop * 3 + 0] & 0x80) >> 7;	
		timDatsTemp_CalibrateTab[loop].Status_Act	= (datsRead_Temp->swTimer_Tab[loop * 3 + 1] & 0xe0) >> 5;	
		timDatsTemp_CalibrateTab[loop].Hour			= (datsRead_Temp->swTimer_Tab[loop * 3 + 1] & 0x1f) >> 0;	
		timDatsTemp_CalibrateTab[loop].Minute		= (datsRead_Temp->swTimer_Tab[loop * 3 + 2] & 0xff) >> 0;	
	}

	os_free(datsRead_Temp);
}

void ICACHE_FLASH_ATTR
datsDelayOP_getReales(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();
	
	ifDelay_sw_running_FLAG = datsRead_Temp->swDelay_flg;
	delayPeriod_closeLoop = datsRead_Temp->swDelay_periodCloseLoop;

	ifDelay_sw_running_FLAG &= ~(1 << 1); //������ʱʧЧ

	os_free(datsRead_Temp);
}


LOCAL void ICACHE_FLASH_ATTR
timActingProcess_task(void *pvParameters){

	stt_usrDats_privateSave datsSave_Temp = {0};
	stt_usrDats_privateSave *datsRead_Temp;

	u8 loop = 0;

	for(;;){

		{ //���Դ��룬��ǰϵͳ����ʱ��log���
			
			u16 log_period = 300;
			static u16 log_Count = 0;
			
			if(log_Count < log_period)log_Count ++;
			else{
			
				log_Count = 0;
				
				localSystime_logOut();
			}
		}
		
		{ /*ϵͳ����ʱ���Ը���*///zigbeeʱ���ѯ����֮��
			
			systemTime_current.time_Minute = sysTimeKeep_counter / 60;
			systemTime_current.time_Second = sysTimeKeep_counter % 60;
			
			if(sysTimeKeep_counter < 3600){
				
			}else{
			
				sysTimeKeep_counter = 0;
				(systemTime_current.time_Hour >= 24)?(systemTime_current.time_Hour = 0):(systemTime_current.time_Hour ++);
				(systemTime_current.time_Week >   7)?(systemTime_current.time_Week = 1):(systemTime_current.time_Week ++);
			}
		}

		/*������ͨ���ض�ʱҵ��*/
		if((timDatsTemp_CalibrateTab[0].if_Timing == 0) &&	//��ȫ�أ����±�־λ
		   (timDatsTemp_CalibrateTab[1].if_Timing == 0) &&
		   (timDatsTemp_CalibrateTab[2].if_Timing == 0) &&
		   (timDatsTemp_CalibrateTab[3].if_Timing == 0)
		  ){
		  
			ifTim_sw_running_FLAG = 0; 
			  
		}else{ //��ȫ�أ����±�־λ����ִ���߼�
			
			ifTim_sw_running_FLAG = 1; 
		
			for(loop = 0; loop < 4; loop ++){
				
				if(true == weekend_judge(systemTime_current.time_Week, timDatsTemp_CalibrateTab[loop].Week_Num)){ //��ռλ�Ա�
				
					if(timCount_ENABLE == timDatsTemp_CalibrateTab[loop].if_Timing){ //��ʱ������־�˶�
						
						if((systemTime_current.time_Hour * 60 + systemTime_current.time_Minute) ==	\
						   (timDatsTemp_CalibrateTab[loop].Hour * 60 + timDatsTemp_CalibrateTab[loop].Minute) && //��ʱʱ�̺˶ԣ���ȷʱ��ǰ10s������Ӧ��
						   (systemTime_current.time_Second <= 10)){  //
							   
							//һ���Զ�ʱ�ж�
							if(swTim_onShoot_FLAG & (1 << loop)){ //�Ƿ�Ϊһ���Զ�ʱ��������յ�ǰ�ζ�ʱ��Ϣ
								
								swTim_onShoot_FLAG &= ~(1 << loop);

								datsRead_Temp = devParam_flashDataRead(); //��ʱ�������
								memcpy(datsSave_Temp.swTimer_Tab, datsRead_Temp->swTimer_Tab, 3 * 4); 
								os_free(datsRead_Temp);
								datsSave_Temp.swTimer_Tab[loop * 3] = 0; //��ʱ����ֲ����
								devParam_flashDataSave(obj_swTimer_Tab, datsSave_Temp); //�洢���ݸ���
								datsTiming_getRealse(); //�����������ݸ���
							}
						   
							//�̵���������Ӧ
							swCommand_fromUsr.actMethod = relay_OnOff;
							swCommand_fromUsr.objRelay = timDatsTemp_CalibrateTab[loop].Status_Act;
							
						}
						else
						if((systemTime_current.time_Hour * 60 + systemTime_current.time_Minute) > //��ʱʱ�̺˶ԣ�������ȷʱ�̱�־���
						   (timDatsTemp_CalibrateTab[loop].Hour * 60 + timDatsTemp_CalibrateTab[loop].Minute)){
							   
						   //һ���Զ�ʱ�ж�
						   if(swTim_onShoot_FLAG & (1 << loop)){ //�Ƿ�Ϊһ���Զ�ʱ��������յ�ǰ�ζ�ʱ��Ϣ
							   
							   swTim_onShoot_FLAG &= ~(1 << loop);
						   
							   datsRead_Temp = devParam_flashDataRead(); //��ʱ�������
							   memcpy(datsSave_Temp.swTimer_Tab, datsRead_Temp->swTimer_Tab, 3 * 4); 
							   os_free(datsRead_Temp);
							   datsSave_Temp.swTimer_Tab[loop * 3] = 0; //��ʱ����ֲ����
							   devParam_flashDataSave(obj_swTimer_Tab, datsSave_Temp); //�洢���ݸ���
							   datsTiming_getRealse(); //�����������ݸ���
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














