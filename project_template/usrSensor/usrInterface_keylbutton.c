#include "usrInterface_keylbutton.h"

#include "esp_common.h"
#include "espressif/espconn.h"
#include "espressif/airkiss.h"
#include "espressif/esp_softap.h"
#include "espressif/esp_wifi.h"

#include "GPIO.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "datsProcess_uartZigbee.h"
#include "datsProcess_socketsNetwork.h"

#include "bsp_Hardware.h"
#include "hwPeripherial_Actuator.h"
#include "datsManage.h"
#include "devUpgrade_OTA.h"
#include "usrInterface_Tips.h"

extern xQueueHandle xMsgQ_zigbFunRemind;
extern xQueueHandle xMsgQ_devUpgrade;

extern void somartConfig_complete(void);

u8 val_DcodeCfm = 0; //拨码确认值

u8 timeCounter_smartConfig_start = 0; //smartconfig开启时间计时（开启时间过长影响其他定时器运行，必须有限制）
bool smartconfigOpen_flg = false; //smartconfig开启标志

u16	touchPadActCounter 	= 0;  //按下时间计时计数值（触摸按键）
u16	touchPadContinueCnt	= 0;  //连按间隔计时计数值（触摸按键）

bool usrKeyCount_EN	= false;  //按下时间计时计数值（轻触按键）
u16	 usrKeyCount	= 0;  //按下时间计时使能（轻触按键）

LOCAL xTaskHandle pxTaskHandle_threadUsrInterface;

LOCAL void usrSmartconfig_start(void);
/*---------------------------------------------------------------------------------------------*/

LOCAL void ICACHE_FLASH_ATTR
usrFunCB_pressShort(void){

	os_printf("usrKey_short.\n");

}

LOCAL void ICACHE_FLASH_ATTR
usrFunCB_pressLongA(void){

	os_printf("usrKey_Long_A.\n");

	if(WIFIMODE_STA == wifi_get_opmode()){

		usrSmartconfig_start();
		
	}else{

		
	}
}

LOCAL void ICACHE_FLASH_ATTR
usrFunCB_pressLongB(void){

	os_printf("usrKey_Long_B.\n");

	u8 mptr_upgrade = DEVUPGRADE_PUSH;
//	mptr_upgrade = 0;
	xQueueSend(xMsgQ_devUpgrade, (void *)&mptr_upgrade, 0); 	
}

LOCAL void ICACHE_FLASH_ATTR
touchFunCB_sysGetRestart(void){

	os_printf("system get restart right now! please wait.\n");
	system_restart();
}

LOCAL void ICACHE_FLASH_ATTR
usrSoftAP_Config(void){

	struct softap_config usrAP_config = {0};

	sprintf(usrAP_config.ssid, "Lanbon-H7_Mster%02X%02X", MACSTA_ID[4], MACSTA_ID[5]);
	sprintf(usrAP_config.password, "lanbon123456");
	usrAP_config.authmode = AUTH_WPA_WPA2_PSK;
	usrAP_config.ssid_len = 0;
	usrAP_config.channel = 6;
	usrAP_config.ssid_hidden = 0;
	usrAP_config.beacon_interval = 200;
	usrAP_config.max_connection = 3;

	wifi_set_opmode(SOFTAP_MODE);
	wifi_softap_set_config(&usrAP_config);
}

void ICACHE_FLASH_ATTR
usrSmartconfig_stop(void){

	if(smartconfigOpen_flg){

		smartconfigOpen_flg = false; //标志复位
		if(timeCounter_smartConfig_start)timeCounter_smartConfig_start = 0;
		if(devTips_status == status_tipsAPFind)tips_statusChangeToNormal(); //tips从配置模式恢复至正常模式
		
		smartconfig_stop();
		somartConfig_complete(); //定时器恢复

		os_printf("smartconfig stop.\n");
	}
}

void ICACHE_FLASH_ATTR
usrSmartconfig_start(void){

	os_printf("smartconfig start!!!.\n");

	tips_statusChangeToAPFind(); //tips更变

	wifi_set_opmode(STATION_MODE);	//设置为STATION模式
	smartconfig_start(smartconfig_done_tp, 0);	//开始smartlink
	
	timeCounter_smartConfig_start = SMARTCONFIG_TIMEOPEN_DEFULT;
	smartconfigOpen_flg = true;
}

void ICACHE_FLASH_ATTR
usrZigbNwkOpen_start(void){

	enum_zigbFunMsg mptr_zigbFunRm = msgFun_nwkOpen;
	xQueueSend(xMsgQ_zigbFunRemind, (void *)&mptr_zigbFunRm, 0);
	tips_statusChangeToZigbNwkOpen(ZIGBNWKOPENTIME_DEFAULT);

	os_printf("zigbNwk open start!!!.\n");
}

LOCAL void ICACHE_FLASH_ATTR
touchPad_functionTrigNormal(u8 statusPad, keyCfrm_Type statusCfm){ //普通触摸按键触发

	switch(statusCfm){
	
		case press_Short:{

			os_printf("touchShort get:%02X.\n", statusPad);
		
			switch(statusPad){
				
				case 1:
				case 2:
				case 4:{
					
					swCommand_fromUsr.actMethod = relay_flip;
					swCommand_fromUsr.objRelay = statusPad;
					EACHCTRL_realesFLG = statusPad; //非连按短按触发互控
					devStatus_pushIF = true; //开关状态数据推送
					
				}break;
					
				default:{}break;
			}
			
		}break;
		
		case press_ShortCnt:{

			os_printf("touchCnt get:%02X.\n", statusPad);
			
			switch(statusPad){
				
				case 1:
				case 2:
				case 4:{
					
					swCommand_fromUsr.actMethod = relay_flip;
					swCommand_fromUsr.objRelay = statusPad;

				}break;
					
				default:{}break;
			}
			
		}break;
		
		case press_LongA:{

			os_printf("touchLongA get:%02X.\n", statusPad);
		
			switch(statusPad){
			
				case 1:
				case 2:
				case 4:{
					

				}break;
					
				default:{}break;
			}
			
		}break;
			
		case press_LongB:{

			os_printf("touchLongB get:%02X.\n", statusPad);
		
			switch(statusPad){
			
				case 1:
				case 2:
				case 4:{

					touchFunCB_sysGetRestart();
			
				}break;
					
				default:{}break;
			}
			
		}break;
			
		default:{}break;
	}
}

LOCAL void ICACHE_FLASH_ATTR
touchPad_functionTrigContinue(u8 statusPad, u8 loopCount){	//普通触摸按键连按触发
	
	EACHCTRL_realesFLG = statusPad; //连按结束后触发互控
	devStatus_pushIF = true; //连按结束后触发开关状态数据推送

	os_printf("touchCnt over:%02X, %02dtime.\n", statusPad, loopCount);

	switch(statusPad){
	
		case 1:{
		
			switch(loopCount){
			
				case 3:{
		
				}break;
				
				case 4:{

					usrZigbNwkOpen_start();
				
				}break;
					
				default:{}break;
			}
			
		}break;
			
		case 2:{
		
			switch(loopCount){
			
				case 3:{}break;
					
				default:{}break;
			}
			
		}break;
			
		case 4:{
		
			switch(loopCount){
			
				case 3:{}break;
					
				default:{}break;
			}
			
		}break;
		
		default:{}break;
	}
}

LOCAL u8 ICACHE_FLASH_ATTR
DcodeScan_oneShoot(void){
	
	u8 val_Dcode = 0;
	
	if(!Dcode0)val_Dcode |= 1 << 0;
	else val_Dcode &= ~(1 << 0);
	
	if(!Dcode1)val_Dcode |= 1 << 1;
	else val_Dcode &= ~(1 << 1);
	
	if(!Dcode2)val_Dcode |= 1 << 2;
	else val_Dcode &= ~(1 << 2);
	
	if(!Dcode3)val_Dcode |= 1 << 3;
	else val_Dcode &= ~(1 << 3);
	
	if(0)val_Dcode |= 1 << 4;
	else val_Dcode &= ~(1 << 4);
	
	if(0)val_Dcode |= 1 << 5;
	else val_Dcode &= ~(1 << 5);
	
	return val_Dcode;
}

LOCAL bool ICACHE_FLASH_ATTR
UsrKEYScan_oneShoot(void){

	if(!usrDats_sensor.usrKeyIn_fun_0)return true;
	else return false;
}

LOCAL u8 ICACHE_FLASH_ATTR
touchPadScan_oneShoot(void){

	u8 valKey_Temp = 0;
	
	if(!usrDats_sensor.usrKeyIn_rly_0)valKey_Temp |= 0x01;
	if(!usrDats_sensor.usrKeyIn_rly_1)valKey_Temp |= 0x02;
	if(!usrDats_sensor.usrKeyIn_rly_2)valKey_Temp |= 0x04;
	
	return valKey_Temp;
}

LOCAL void ICACHE_FLASH_ATTR
DcodeScan(void){

	static u8 	val_Dcode_Local 	= 0,
				comfirm_Cnt			= 0;
	const  u8 	comfirm_Period		= 200;	//拨码值消抖确认周期-取决于当前线程调度周期
		
		   u8 	val_Dcode_differ	= 0;
	
		   bool	val_CHG				= false;
	
	val_DcodeCfm = DcodeScan_oneShoot();
	
	DEV_actReserve = switchTypeReserve_GET(); //当前开关类型对应有效操作位刷新
	
	if(val_Dcode_Local != val_DcodeCfm){
	
		if(comfirm_Cnt < comfirm_Period)comfirm_Cnt ++;
		else{
		
			comfirm_Cnt = 0;
			val_CHG		= 1;
		}
	}
	
	if(val_CHG){
		
		val_CHG				= 0;
	
		val_Dcode_differ 	= val_Dcode_Local ^ val_DcodeCfm;
		val_Dcode_Local		= val_DcodeCfm;

		os_printf("Dcode chg: %02X.\n", val_Dcode_Local);

		if(val_Dcode_differ & Dcode_FLG_ifAP){
		
			if(val_Dcode_Local & Dcode_FLG_ifAP){
			
				usrSoftAP_Config();
			
			}else{
			
				wifi_set_opmode(STATION_MODE);
			}
		}
		
		if(val_Dcode_differ & Dcode_FLG_ifMemory){
		
			if(val_Dcode_Local & Dcode_FLG_ifMemory){

				relayStatus_ifSave = statusSave_enable;
				
			}else{
			
				relayStatus_ifSave = statusSave_disable;
			}
		}
		
		if(val_Dcode_differ & Dcode_FLG_bitReserve){
		
			switch(Dcode_bitReserve(val_Dcode_Local)){
			
				case 0:{
				
					SWITCH_TYPE = SWITCH_TYPE_SWBIT3;	
					
				}break;
					
				case 1:{
				
					SWITCH_TYPE = SWITCH_TYPE_SWBIT1;	

				}break;
					
				case 2:{
				
					SWITCH_TYPE = SWITCH_TYPE_SWBIT2;	

				}break;
					
				case 3:{
					
					SWITCH_TYPE = SWITCH_TYPE_SWBIT3;	

				}break;
					
				default:break;
			}
		}
	}
}

LOCAL void ICACHE_FLASH_ATTR
UsrKEYScan(funKey_Callback funCB_Short, funKey_Callback funCB_LongA, funKey_Callback funCB_LongB){
	
	const  u16 	keyCfrmLoop_Short 	= 20,		//消抖时间 单位：ms
			   	keyCfrmLoop_LongA 	= 3000,		//长按A时间  单位：ms
			   	keyCfrmLoop_LongB 	= 10000,	//长按B时间  单位：ms
			   	keyCfrmLoop_MAX		= 60000;	//计时封顶

	static bool LongA_FLG = 0;
	static bool LongB_FLG = 0;
	
	static bool keyPress_FLG = 0;

	if(true == UsrKEYScan_oneShoot()){		
		
		keyPress_FLG = 1;
		
//		tips_statusChangeToNormal();
	
		if(!usrKeyCount_EN) usrKeyCount_EN= 1;	//计时
		
		if((usrKeyCount >= keyCfrmLoop_LongA) && (usrKeyCount <= keyCfrmLoop_LongB) && !LongA_FLG){
		
			funCB_LongA();
			
			LongA_FLG = 1;
		}	
		
		if((usrKeyCount >= keyCfrmLoop_LongB) && (usrKeyCount <= keyCfrmLoop_MAX) && !LongB_FLG){
		
			funCB_LongB();
			
			LongB_FLG = 1;
		}
		
	}
	else{		
		
		usrKeyCount_EN = 0;
		
		if(keyPress_FLG){
		
			keyPress_FLG = 0;
			
			if(usrKeyCount < keyCfrmLoop_LongA && usrKeyCount > keyCfrmLoop_Short){
				
				funCB_Short();
			}
			
			usrKeyCount = 0;
			LongA_FLG 	= 0;
			LongB_FLG 	= 0;
		}
	}
}

LOCAL void ICACHE_FLASH_ATTR
touchPad_Scan(void){

	static u8   touchPad_temp = 0;
	static bool keyPress_FLG = 0;

	static bool	funTrigFLG_LongA = 0;
	static bool funTrigFLG_LongB = 0;

	const  u16 	touchCfrmLoop_Short 	= 20,		//消抖时间 单位：ms
			   	touchCfrmLoop_LongA 	= 3000,		//长按A时间  单位：ms
			   	touchCfrmLoop_LongB 	= 10000,	//长按B时间  单位：ms
			   	touchCfrmLoop_MAX		= 60000;	//计时封顶

	const  u16  timeDef_touchPressContinue = 400;

	static u8 	pressContinueGet = 0;
		   u8 	pressContinueCfm = 0;

	u16 conterTemp = 0; //

	if(touchPadScan_oneShoot()){
		
		if(!keyPress_FLG){
		
			keyPress_FLG = true;
			touchPadActCounter 	= touchCfrmLoop_MAX;
			touchPadContinueCnt = timeDef_touchPressContinue;  //连按计时初始化
			touchPad_temp = touchPadScan_oneShoot();
		}
		else{
			
			if(touchPad_temp == touchPadScan_oneShoot()){
				
				conterTemp = touchCfrmLoop_MAX - touchPadActCounter;
			
				if(conterTemp > touchCfrmLoop_LongA && conterTemp <= touchCfrmLoop_LongB){
				
					if(false == funTrigFLG_LongA){
					
						funTrigFLG_LongA = true;
						touchPad_functionTrigNormal(touchPad_temp, press_LongA);
					}
				}
				if(conterTemp > touchCfrmLoop_LongB && conterTemp <= touchCfrmLoop_MAX){
				
					if(false == funTrigFLG_LongB){
					
						funTrigFLG_LongB = true;
						touchPad_functionTrigNormal(touchPad_temp, press_LongB);
					}
				}
			}
		}
	}
	else{
		
		if(true == keyPress_FLG){
		
			conterTemp = touchCfrmLoop_MAX - touchPadActCounter;
			if(conterTemp > touchCfrmLoop_Short && conterTemp <= touchCfrmLoop_LongA){
			
				if(touchPadContinueCnt)pressContinueGet ++;
				
				if(pressContinueGet <= 1)touchPad_functionTrigNormal(touchPad_temp, press_Short); //非连按短按触发
				else touchPad_functionTrigNormal(touchPad_temp, press_ShortCnt); //连按短按触发
					
			}
		}
	
		if(!touchPadContinueCnt && pressContinueGet){
		
			pressContinueCfm = pressContinueGet;
			pressContinueGet = 0;
			
			if(pressContinueCfm >= 2){
			
				touchPad_functionTrigContinue(touchPad_temp, pressContinueCfm); //连按结束触发
				pressContinueCfm = 0;
			}

			touchPad_temp = 0;
		}

		funTrigFLG_LongA = 0;
		funTrigFLG_LongB = 0;
			
		touchPadActCounter = 0;
		keyPress_FLG = 0;
	}

}

LOCAL void ICACHE_FLASH_ATTR
usrInterfaceProcess_task(void *pvParameters){

	for(;;){

		touchPad_Scan();
		UsrKEYScan(usrFunCB_pressShort, usrFunCB_pressLongA, usrFunCB_pressLongB);
		DcodeScan();
		
		vTaskDelay(1);
	}

	vTaskDelete(NULL);
}

void ICACHE_FLASH_ATTR
usrInterface_ThreadStart(void){

	portBASE_TYPE xReturn = pdFAIL;

	xReturn = xTaskCreate(usrInterfaceProcess_task, "Process_keyAndBtn", 512, (void *)NULL, 5, &pxTaskHandle_threadUsrInterface);

	os_printf("\npxTaskHandle_threadKeyAndBtn is %d\n", pxTaskHandle_threadUsrInterface);
}




