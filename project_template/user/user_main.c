/*
 * ESPRSSIF MIT License
 *
 * Copyright (c) 2015 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP8266 only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "esp_common.h"

#include "uart.h"
#include "GPIO.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "espressif/espconn.h"
#include "espressif/airkiss.h"

#include "bsp_Hardware.h"
#include "datsProcess_tcpRemote_A.h"
#include "datsProcess_tcpRemote_B.h"
#include "datsProcess_udpLocal_A.h"
#include "datsProcess_udpRemote_B.h"

#include "datsProcess_uartZigbee.h"
#include "datsProcess_socketsNetwork.h"

#include "timer_Activing.h"
#include "usrInterface_keylbutton.h"
#include "hwPeripherial_Actuator.h"

#include "usrInterface_Tips.h"

#include "devUpgrade_OTA.h"

#include "datsManage.h"

#define TIMERPERIOD_USRTEST_FUN		800
#define TIMERPERIOD_SPECIAL1MS_FUN	1

extern bool nwkInternetOnline_IF;

extern xQueueHandle xMsgQ_zigbFunRemind;
extern xQueueHandle xMsgQ_devUpgrade;

extern stt_dataRemoteReq localZigbASYDT_bufQueueRemoteReq[zigB_remoteDataTransASY_QbuffLen];
extern stt_dataScenarioReq localZigbASYDT_bufQueueScenarioReq[zigB_ScenarioCtrlDataTransASY_QbuffLen];

extern u16 dtReqEx_counter;

extern u8 internetRemoteServer_portSwitchPeriod;

extern u8 zigbee_currentPanID_reslesCounter;

extern bool zigbNodeDevDetectManage_runningFLG;

LOCAL os_timer_t timer_Test;
LOCAL os_timer_t usrTimer_reference;

void devConnectAP_autoInit(char P_ssid[32], char P_password[64]);
void somartConfig_complete(void);

LOCAL void somartConfig_standBy(void);
/*---------------------------------------------------------------------------------------------*/

LOCAL void 
uart1Init_Debug(void){

	UART_WaitTxFifoEmpty(UART1);

	UART_ConfigTypeDef uart_config;

	uart_config.baud_rate 			= BIT_RATE_921600;
	uart_config.data_bits 			= UART_WordLength_8b;
	uart_config.flow_ctrl 			= USART_Parity_None;
	uart_config.stop_bits 			= USART_StopBits_1;
	uart_config.flow_ctrl 			= USART_HardwareFlowControl_None;
	uart_config.UART_RxFlowThresh 	= 0;
	uart_config.UART_InverseMask 	= UART_None_Inverse;
	UART_ParamConfig(UART1, &uart_config);	//UART1初始化
	UART_SetPrintPort(UART1);
}

void 
smartconfig_done_tp(sc_status status, void *pdata)
{
    switch(status) {
        case SC_STATUS_WAIT:
            os_printf("SC_STATUS_WAIT\n");
            break;
        case SC_STATUS_FIND_CHANNEL:
            os_printf("SC_STATUS_FIND_CHANNEL\n");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
			somartConfig_standBy(); //常规定时器暂停
            os_printf("SC_STATUS_GETTING_SSID_PSWD\n");
            sc_type *type = pdata;
            if (*type == SC_TYPE_ESPTOUCH) {
                os_printf("SC_TYPE:SC_TYPE_ESPTOUCH\n");
            } else {
                os_printf("SC_TYPE:SC_TYPE_AIRKISS\n");
            }
            break;
        case SC_STATUS_LINK:
            os_printf("SC_STATUS_LINK\n");
         	struct station_config *datsTemp = (struct station_config *)pdata;
		
//			struct station_config staConfig = {0}; 

//			staConfig.bssid_set = datsTemp->bssid_set;
//			strcpy(staConfig.bssid, datsTemp->bssid);
//			strcpy(staConfig.password, datsTemp->password);
//			strcpy(staConfig.ssid, datsTemp->ssid);

//			wifi_station_set_config(&staConfig);
//			wifi_station_disconnect();
//			wifi_station_connect();

			/*提前结束，避免升级段代码耦合（upgrade代码段问题暂时只有此方法解决 -.-|||）*/
			char dats_ssid[32] = {0};
			char dats_password[64] = {0};

			strcpy(dats_password, datsTemp->password);
			strcpy(dats_ssid, datsTemp->ssid);

			smartconfig_stop();
			somartConfig_complete(); //常规定时器恢复
			devConnectAP_autoInit(dats_ssid, dats_password);
	
            break;
        case SC_STATUS_LINK_OVER:
            os_printf("SC_STATUS_LINK_OVER\n");
            if (pdata != NULL) {
				//SC_TYPE_ESPTOUCH
                uint8 phone_ip[4] = {0};

                memcpy(phone_ip, (uint8*)pdata, 4);
                os_printf("Phone ip: %d.%d.%d.%d\n",phone_ip[0],phone_ip[1],phone_ip[2],phone_ip[3]);

//				wifi_set_opmode(STATIONAP_MODE);

            } else {
            	//SC_TYPE_AIRKISS - support airkiss v2.0
//				airkiss_start_discover();
			}
            smartconfig_stop();
            break;
    }
	
}

void 
devConnectAP_autoInit(char P_ssid[32], char P_password[64]){

//	char ssid[32] = "LANBON_SHOWROOM";
//	char password[64] = "Lanbon9999";

//	char ssid[32] = "LANBON_DEVLOPE";
//	char password[64] = "12345678";

//	char ssid[32] = "TEST";
//	char password[64] = "12345abc";

	struct station_config stationCfg;

	wifi_set_opmode(STATION_MODE);

	bzero(&stationCfg, sizeof(struct station_config));

	stationCfg.bssid_set = 0;
	memcpy(stationCfg.ssid, P_ssid, 32);
	memcpy(stationCfg.password, P_password, 64);
	wifi_station_set_config(&stationCfg);
	wifi_station_disconnect();
	if(!wifi_station_dhcpc_status())wifi_station_dhcpc_start();
	os_printf("AP connect result is %d.\n", wifi_station_connect());
}

/*测试线程*/
void 
myProcess_task(void *pvParameters)
{

	const u8 colorDats[3][3] = {

		{10, 0, 0},
		{0, 10, 0},
		{0, 0, 10},
	};
	u8 color_ptr[3] = {0};

	const u8 keyShakeFight_period = 3; 

	bool FLG_keyReales_usrKeyIn_fun_0 = true;
	
	bool FLG_keyReales_usrKeyIn_rly_0 = true;
	bool FLG_keyPress_usrKeyIn_rly_0 = false;
	u8	 cfrCount_usrKeyIn_rly_0 = 0;
	
	bool FLG_keyReales_usrKeyIn_rly_1 = true;
	bool FLG_keyPress_usrKeyIn_rly_1 = false;
	u8	 cfrCount_usrKeyIn_rly_1 = 0;
	
	bool FLG_keyReales_usrKeyIn_rly_2 = true;
	bool FLG_keyPress_usrKeyIn_rly_2 = false;
	u8	 cfrCount_usrKeyIn_rly_2 = 0;
	
	bool connect_Compeled = false;
	
	stt_threadDatsPass mptr_S2Z;
	u8 mptr_upgrade;

	mptr_S2Z.msgType = conventional;
	mptr_S2Z.dats.dats_conv.macAddr[0] = 0xfe;

//	devConnectAP_autoInit();

	for(;;){

//		xQueueSend(xMsgQ_Zigb2Socket, (void *)&mptr_S2Z, portMAX_DELAY);

//		if(false == connect_Compeled){
//		
//			if(wifi_station_connect()){
//				
//				connect_Compeled = true;

//			}
//		}

		if(!usrDats_sensor.usrKeyIn_fun_0){

			if(FLG_keyReales_usrKeyIn_fun_0){

				FLG_keyReales_usrKeyIn_fun_0 = false;

				os_printf("[Tips_usrInterface]: key usrKeyIn_fun_0!!!\n");

				wifi_set_opmode(STATION_MODE);	//设置为STATION模式
				smartconfig_start(smartconfig_done_tp, 0);	//开始smartlink       
			}
		}else{

			FLG_keyReales_usrKeyIn_fun_0 = true;
		}

		if(!usrDats_sensor.usrKeyIn_rly_0){

			if(!FLG_keyPress_usrKeyIn_rly_0){
			
				FLG_keyPress_usrKeyIn_rly_0 = true;
			}

			if(FLG_keyPress_usrKeyIn_rly_0){
			
				cfrCount_usrKeyIn_rly_0 ++;
			
				if(cfrCount_usrKeyIn_rly_0 > keyShakeFight_period){

					if(FLG_keyReales_usrKeyIn_rly_0){
			
						FLG_keyReales_usrKeyIn_rly_0 = false;
			
						tipsLED_rgbColorSet(0, colorDats[color_ptr[0]][0], colorDats[color_ptr[0]][1], colorDats[color_ptr[0]][2]);
						color_ptr[0] ++;
						if(color_ptr[0] > 2)color_ptr[0] = 0;			
					}
				}
			}
		}
		else{

			FLG_keyReales_usrKeyIn_rly_0 = true;
			FLG_keyPress_usrKeyIn_rly_0 = false;
			cfrCount_usrKeyIn_rly_0 = 0;
		}

		if(!usrDats_sensor.usrKeyIn_rly_1){

			if(!FLG_keyPress_usrKeyIn_rly_1){
			
				FLG_keyPress_usrKeyIn_rly_1 = true;
			}

			if(FLG_keyPress_usrKeyIn_rly_1){
			
				cfrCount_usrKeyIn_rly_1 ++;
			
				if(cfrCount_usrKeyIn_rly_1 > keyShakeFight_period){
			
					if(FLG_keyReales_usrKeyIn_rly_1){
			
						FLG_keyReales_usrKeyIn_rly_1 = false;
			
						enum_zigbFunMsg mptr_zigbFunRm = msgFun_nwkOpen;
						xQueueSend(xMsgQ_zigbFunRemind, (void *)&mptr_zigbFunRm, 0);
			
						os_printf("[Tips_usrInterface]: key usrKeyIn_fun_1, nwkZigb openning now.\n");
						
						tipsLED_rgbColorSet(1, colorDats[color_ptr[1]][0], colorDats[color_ptr[1]][1], colorDats[color_ptr[1]][2]);
						color_ptr[1] ++;
						if(color_ptr[1] > 2)color_ptr[1] = 0;		
					}
				}
			}
		}
		else{

			FLG_keyReales_usrKeyIn_rly_1 = true;
			FLG_keyPress_usrKeyIn_rly_1 = false;
			cfrCount_usrKeyIn_rly_1 = 0;
		}

		if(!usrDats_sensor.usrKeyIn_rly_2){

			if(!FLG_keyPress_usrKeyIn_rly_2){
			
				FLG_keyPress_usrKeyIn_rly_2 = true;
			}

			if(FLG_keyPress_usrKeyIn_rly_2){
		
				cfrCount_usrKeyIn_rly_2 ++;
		
				if(cfrCount_usrKeyIn_rly_2 > keyShakeFight_period){
		
					if(FLG_keyReales_usrKeyIn_rly_2){
		
						FLG_keyReales_usrKeyIn_rly_2 = false;
		
						stt_usrDats_privateSave dats_Temp;

						os_printf("[Tips_usrInterface]: key usrKeyIn_rly_2 press!!!\n");

						tipsLED_rgbColorSet(2, colorDats[color_ptr[2]][0], colorDats[color_ptr[2]][1], colorDats[color_ptr[2]][2]);
						color_ptr[2] ++;
						if(color_ptr[2] > 2)color_ptr[2] = 0;

//							dats_Temp.test_dats += 1;
//							devParam_flashDataSave(obj_test_dats, dats_Temp);

//							if(!dats_Temp.test_dats){

//								dats_Temp.serverIP_default[0] = 0xcc;
//								devParam_flashDataSave(obj_serverIP_default, dats_Temp);
//							}

						mptr_upgrade = DEVUPGRADE_PUSH;
//						mptr_upgrade = 0;
						xQueueSend(xMsgQ_devUpgrade, (void *)&mptr_upgrade, 0); 	
					}
				}
			}
		}
		else{

			FLG_keyReales_usrKeyIn_rly_2 = true;
			FLG_keyPress_usrKeyIn_rly_2 = false;
			cfrCount_usrKeyIn_rly_2 = 0;
		}

		vTaskDelay(1);
	}
    
    vTaskDelete(NULL);
}

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32 user_rf_cal_sector_set(void)
{
    flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;
        case FLASH_SIZE_64M_MAP_1024_1024:
            rf_cal_sec = 2048 - 5;
            break;
        case FLASH_SIZE_128M_MAP_1024_1024:
            rf_cal_sec = 4096 - 5;
            break;
        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}

/*毫秒定时器*/
LOCAL void 
timerFunCB_usrReference(void *para){ 

	const u8 period1s_usrTest = 40; //定时测试业务 计时周期
	static u8 counter1s_usrTest = 0;  //定时测试业务 计时值
	static u8 internetServerPort_switchInsert = 0; //端口切换索引

	const u16 period_1second = 1000;
	static u16 counter_1second = 0;

	const u8 period_nodeSystimeSynchronous = 10; //子节点系统时间同步周期 单位：s
	static u8 counter_nodeSystimeSynchronous = 0;

	const u8 period_localSystimeZigbAdjust = 20; //本地系统时间同步周期 单位：s
	static u8 counter_localSystimeZigbAdjust = 0;

	u8 loop = 0;

	// 1s专用定时
	if(counter_1second < period_1second)counter_1second ++;
	else{

		counter_1second = 0;

		/*子节点系统时间周期性同步（时间戳下发）*///消息通知
		if(counter_nodeSystimeSynchronous < period_nodeSystimeSynchronous)counter_nodeSystimeSynchronous ++;
		else{

			counter_nodeSystimeSynchronous = 0;

			enum_zigbFunMsg mptr_zigbFunRm = msgFun_nodeSystimeSynchronous;
			xQueueSend(xMsgQ_zigbFunRemind, (void *)&mptr_zigbFunRm, 0);
		}

		/*本地系统时间周期性与zigbee时间同步*///消息通知
		if(counter_localSystimeZigbAdjust < period_localSystimeZigbAdjust)counter_localSystimeZigbAdjust ++;
		else{

			counter_localSystimeZigbAdjust = 0;

			enum_zigbFunMsg mptr_zigbFunRm = msgFun_localSystimeZigbAdjust;
			xQueueSend(xMsgQ_zigbFunRemind, (void *)&mptr_zigbFunRm, 0);
		}

		/*zigbee当前PANID读取周期计时计数*/
		if(zigbee_currentPanID_reslesCounter)zigbee_currentPanID_reslesCounter --;

		/*延时计时业务*///到点动作响应
		if(ifDelay_sw_running_FLAG & (1 << 1)){
		
			if(delayCnt_onoff < ((u16)delayPeriod_onoff * 60))delayCnt_onoff ++;
			else{
			
				delayPeriod_onoff = delayCnt_onoff = 0;
				
				ifDelay_sw_running_FLAG &= ~(1 << 1);	//标志清除
				
				swCommand_fromUsr.actMethod = relay_OnOff; //动作响应
				swCommand_fromUsr.objRelay = delayUp_act;
				devStatus_pushIF = true; //开关状态数据推送
				dev_agingCmd_sndInitative.agingCmd_delaySetOpreat = 1;
				if(SWITCH_TYPE == SWITCH_TYPE_SWBIT1 || SWITCH_TYPE == SWITCH_TYPE_SWBIT2 || SWITCH_TYPE == SWITCH_TYPE_SWBIT3)EACHCTRL_realesFLG |= (status_actuatorRelay ^ swCommand_fromUsr.objRelay); //有效互控位触发
				else
				if(SWITCH_TYPE == SWITCH_TYPE_CURTAIN)EACHCTRL_realesFLG = 1; //有效互控触发
				
				os_printf(">>>>>>>>delayAct opreation up.\n");
			}
		}	

		/*绿色模式计时业务*///只要开关打开就倒计时关闭
		if((ifDelay_sw_running_FLAG & (1 << 0)) && status_actuatorRelay){
		
			if(delayCnt_closeLoop < ((u16)delayPeriod_closeLoop * 60))delayCnt_closeLoop ++;
			else{
				
				delayCnt_closeLoop = 0;
			
				swCommand_fromUsr.actMethod = relay_OnOff; //动作响应
				swCommand_fromUsr.objRelay = 0;
				devStatus_pushIF = true; //开关状态数据推送
				dev_agingCmd_sndInitative.agingCmd_delaySetOpreat = 1;
				if(SWITCH_TYPE == SWITCH_TYPE_SWBIT1 || SWITCH_TYPE == SWITCH_TYPE_SWBIT2 || SWITCH_TYPE == SWITCH_TYPE_SWBIT3)EACHCTRL_realesFLG |= (status_actuatorRelay ^ swCommand_fromUsr.objRelay); //有效互控位触发
				else
				if(SWITCH_TYPE == SWITCH_TYPE_CURTAIN)EACHCTRL_realesFLG = 1; //有效互控触发
			}
		}

		//用户操作闲置释放计时
		if(counter_ifTipsFree)counter_ifTipsFree --;

		//zigb网络开放倒计时
		if(timeCount_zigNwkOpen)timeCount_zigNwkOpen --;

		//zigb设备链表监管周期更新，1s一次
		if(!zigbNodeDevDetectManage_runningFLG)zigbNodeDevDetectManage_runningFLG = true;

//		//定时测试业务[服务器网络端口切换]
//		if(counter1s_usrTest < period1s_usrTest)counter1s_usrTest ++; 
//		else{

//			counter1s_usrTest = 0;

//			internetServerPort_switchInsert ++;
//			if(internetServerPort_switchInsert >= INTERNET_REMOTESERVER_PORTTAB_LEN)internetServerPort_switchInsert = 0;
//			if(nwkInternetOnline_IF)mySocketUDPremote_B_portChange(remoteServerPort_switchTab[internetServerPort_switchInsert]); //端口切换操作
//		}

		//网络在线情况下，通信不正常切换端口
		if(internetRemoteServer_portSwitchPeriod)internetRemoteServer_portSwitchPeriod --;
		else{

			internetRemoteServer_portSwitchPeriod = INTERNET_SERVERPORT_SWITCHPERIOD;
			(internetServerPort_switchInsert >= INTERNET_REMOTESERVER_PORTTAB_LEN - 1)?(internetServerPort_switchInsert = 0):(internetServerPort_switchInsert ++);
			if(nwkInternetOnline_IF)mySocketUDPremote_B_portChange(remoteServerPort_switchTab[internetServerPort_switchInsert]);
		}
	}

	{ //100ms专用计时，<窗帘>

		const u16 period_100ms 	= 200;
		static u16 counter_100ms = 0; 
		const u8 period5_200ms = 5;
		static u8 counter5_200ms = 0; 

		if(counter_100ms < period_100ms)counter_100ms ++;
		else{
			
			counter_100ms = 0;
			counter5_200ms ++;

//			os_printf("%d\n", curtainAct_Param.act);
		
			/*窗帘逻辑业务，按照轨道时间动作*/
			if(SWITCH_TYPE == SWITCH_TYPE_CURTAIN){

				switch(curtainAct_Param.act){
				
					case cTact_open:{
						
						if(curtainAct_Param.act_period){ //轨道周期时间非零时才进行有效轨道时间计时业务
						
							if(curtainAct_Param.act_counter < curtainAct_Param.act_period){
							
								if(counter5_200ms >= period5_200ms)curtainAct_Param.act_counter ++;
								
							}else{
							
								curtainAct_Param.act = cTact_stop;
							}	
						}
						
					}break;
						
					case cTact_close:{
						
						if(curtainAct_Param.act_period){ //轨道周期时间非零时才进行有效轨道时间计时业务
						
							if(curtainAct_Param.act_counter > 0){
							
								if(counter5_200ms >= period5_200ms)curtainAct_Param.act_counter --;
								
							}else{
							
								curtainAct_Param.act = cTact_stop;
							}						
						}
					
					}break;
						
					case cTact_stop:{
					
						if(status_actuatorRelay != 2){
						
							swCommand_fromUsr.objRelay = 2;
							swCommand_fromUsr.actMethod = relay_OnOff;
							devActionPush_IF.push_IF = 1; //推送使能
						}
						
					}break;
						
					default:{}break;

				}
			}
			
			if(counter5_200ms >= period5_200ms)counter5_200ms = 0;
		}
	}

	/*其它1ms定时业务*/
	for(loop = 0; loop < zigB_remoteDataTransASY_QbuffLen; loop ++){ //普通远端数据请求异步发送缓存周期判断

		if(localZigbASYDT_bufQueueRemoteReq[loop].dataReqPeriod)localZigbASYDT_bufQueueRemoteReq[loop].dataReqPeriod --;
	}
	
	for(loop = 0; loop < zigB_ScenarioCtrlDataTransASY_QbuffLen; loop ++){

		if(localZigbASYDT_bufQueueScenarioReq[loop].dataReqPeriod)localZigbASYDT_bufQueueScenarioReq[loop].dataReqPeriod --;
	}

	if(dtReqEx_counter)dtReqEx_counter --;
}

LOCAL void 
somartConfig_standBy(void){ //smartConfig时中断所有定时器

	os_timer_disarm(&timer_Test);
	os_timer_disarm(&usrTimer_reference);
//	timer_heartBeat_Pause();
	localTimerPause_sntpTimerAct();
}

void 
somartConfig_complete(void){ //smartConfig结束时恢复所有被中断的定时器

	os_timer_arm(&timer_Test, TIMERPERIOD_USRTEST_FUN, true);
	os_timer_arm(&usrTimer_reference, TIMERPERIOD_SPECIAL1MS_FUN, true);
//	timer_heartBeat_Recovery();
	localTimerRecover_sntpTimerAct();
}

LOCAL void 
timerFunCB_Test(void *para){

//	stt_usrDats_privateSave *dats_Temp = devParam_flashDataRead();;
//	os_printf("[TEST]: data read from flash is %d.\n", dats_Temp->test_dats);
//	os_printf("[TEST]: data read from flash is %02X.\n", dats_Temp->serverIP_default[0]);
//	os_free(dats_Temp);

//	usrDats_actuator.conDatsOut_ZigbeeRst = 1;

//	os_printf("[TEST]: val test is : %02X\n", (u8)usrDats_sensor.usrKeyIn_rly_0);

//	os_printf("[TEST]: val usr_KeyIn is : %02X\n", (u8)usrDats_sensor.usrKeyIn_rly_0);
//	os_printf("reserve heap : %d. \n", system_get_free_heap_size());

//	os_printf("i'm new fireware (Z)1, hellow world!!!\n");
}

/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
/*!!!attention!!!*///串口设置后所有信息打印必须使用os_printf()!!!,printf废除。
void user_init(void)
{
  	os_printf("SDK version:%s\n", system_get_sdk_version());
	os_printf("hellow world!!!\n");

	espconn_init();

	smartconfig_stop();

	wifi_set_opmode(STATION_MODE);

	devFactoryRecord_Opreat(); //出厂判断
	
	devMAC_Reales(); //MAC更新
	ledBKGColorSw_Reales(); //背景灯色索引更新
	portCtrlEachOther_Reales(); //互控端口更新
	datsTiming_getRealse(); //定时数据更新
	timeZone_Reales(); //时区数据更新
	datsDelayOP_getReales(); //延时操作数据更新
	devLockIF_Reales(); //设备锁数据更新
	timeZone_Reales(); //时区更新
	curtainOrbitalPeriod_Reales(); //窗帘轨道周期更新
	
//	wifi_station_disconnect();
	devConnectAP_autoInit("LANBON_DEVELOP002", "Lanbon22*#"); //调试时直接连wifi
//	wifi_station_set_auto_connect(1); 

	uart1Init_Debug();
	uart0Init_datsTrans();

	zigbee_mainThreadStart(); //zigbee主线程激活
	network_mainThreadStart(); //网络通信主线程激活
	sntp_timerActThread_Start(); //stnp网络授时线程激活
	relayActing_ThreadStart(); //继电器动作线程激活
	usrInterface_ThreadStart(); //触摸及按键检测线程激活
	timActing_ThreadStart(); //用户业务定时线程激活
	dats595and597_keepRealesingStart(); //硬件外设驱动响应线程激活
	usrTips_ThreadStart(); //tips线程激活

	/*Sockets建立不能进行二次封装，否则影响smartlink功能，导致重启*/
	mySocketUDPlocal_A_buildInit();	//Socket建立_本地UDP_A
	mySocketUDPremote_B_buildInit();//Socket建立_远端服务器UDP_B
	devUpgradeDetecting_Start(); //OTA升级检测线程激活
//	tcpRemote_A_connectStart();	//Socket建立_远端TCP_A
//	tcpRemote_B_connectStart();	//Socket建立_远端TCP_B

	tipsLED_rgbColorSet(3, 31, 	0, 	0);
	tipsLED_rgbColorSet(2, 31, 	0, 	0);
	tipsLED_rgbColorSet(1, 31,  0, 	0);
	tipsLED_rgbColorSet(0, 31, 	0,  0);

	os_timer_disarm(&timer_Test); //用户测试定时器
	os_timer_setfn(&timer_Test, (os_timer_func_t *)timerFunCB_Test, NULL);
	os_timer_arm(&timer_Test, TIMERPERIOD_USRTEST_FUN, true);

	os_timer_disarm(&usrTimer_reference); //其它任务协助定时器
	os_timer_setfn(&usrTimer_reference, (os_timer_func_t *)timerFunCB_usrReference, NULL);
	os_timer_arm(&usrTimer_reference, TIMERPERIOD_SPECIAL1MS_FUN, true);

//	xTaskCreate(myProcess_task, "myProcess_task", 1024, NULL, 3, NULL);

	os_printf("Hellow world!!!\n");
//	os_printf("reserve heap : %d. \n", system_get_free_heap_size());
}

