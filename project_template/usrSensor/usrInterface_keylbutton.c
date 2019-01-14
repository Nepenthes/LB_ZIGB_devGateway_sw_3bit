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

u8 touchKeepCnt_record	= 1;  //连按进行时连按计数变量，连按必定从1开始，否则不叫连按

u16  combinationFunFLG_3S5S_cancel_counter  = 0;	//三段五短预触发标志_衔接时长取消计数，衔接时间过长时，将预触发标志取消
LOCAL param_combinationFunPreTrig param_combinationFunTrigger_3S1L = {0};
LOCAL param_combinationFunPreTrig param_combinationFunTrigger_3S5S = {0};

LOCAL bool relayStatusRecovery_doneIF = false; //继电器若在记忆使能情况下，需要在拨码确认后进行恢复

LOCAL xTaskHandle pxTaskHandle_threadUsrInterface;

LOCAL void usrSmartconfig_start(void);
/*---------------------------------------------------------------------------------------------*/

LOCAL void 
usrFunCB_pressShort(void){

	os_printf("usrKey_short, wait for reStart.\n");

	beeps_usrActive(3, 160, 3);
	tips_statusChangeToTouchReset();
	vTaskDelay(1000); //挂起10s后重启，等待提示
	system_restart();
}

LOCAL void 
usrFunCB_pressLongA(void){

	stt_usrDats_privateSave datsSave_Temp = {0};

	os_printf("usrKey_Long_A.\n");

	datsSave_Temp.dev_lockIF = 0; //重新配置 就解锁
	devParam_flashDataSave(obj_dev_lockIF, datsSave_Temp);
	deviceLock_flag = false;

	beeps_usrActive(3, 20, 2);
	if(WIFIMODE_STA == wifi_get_opmode()){

		usrSmartconfig_start();
		
	}else{

		
	}
}

LOCAL void 
usrFunCB_pressLongB(void){

	os_printf("usrKey_Long_B，factory recovery opreation trig.\n");

//	u8 mptr_upgrade = DEVUPGRADE_PUSH;
//	mptr_upgrade = 0;
//	xQueueSend(xMsgQ_devUpgrade, (void *)&mptr_upgrade, 0);

	beeps_usrActive(3, 20, 2);
	tips_statusChangeToFactoryRecover();
	devData_recoverFactory(); //恢复出厂挂起4s，等待提示结束
	vTaskDelay(400); 
	system_restart();
}

LOCAL void 
touchFunCB_sysGetRestart(void){

//	os_printf("system get restart right now! please wait.\n");
//	system_restart();
}

LOCAL void 
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

void 
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

void 
usrSmartconfig_start(void){

	os_printf("smartconfig start!!!.\n");

	tips_statusChangeToAPFind(); //tips更变

	wifi_set_opmode(STATION_MODE);	//设置为STATION模式
	smartconfig_start(smartconfig_done_tp, 0);	//开始smartlink
	
	timeCounter_smartConfig_start = SMARTCONFIG_TIMEOPEN_DEFULT;
	smartconfigOpen_flg = true;
}

void 
usrZigbNwkOpen_start(void){

	enum_zigbFunMsg mptr_zigbFunRm = msgFun_nwkOpen;
	xQueueSend(xMsgQ_zigbFunRemind, (void *)&mptr_zigbFunRm, 0);
	tips_statusChangeToZigbNwkOpen(ZIGBNWKOPENTIME_DEFAULT);

	os_printf("zigbNwk open start!!!.\n");
}

LOCAL void normalBussiness_shortTouchTrig(u8 statusPad, bool shortPressCnt_IF){

	bool tipsBeep_IF = false;

	switch(statusPad){
		
		case 1:{
			
			if(SWITCH_TYPE == SWITCH_TYPE_SWBIT1){
			
				swCommand_fromUsr.objRelay = 0;
			}
			else if(SWITCH_TYPE == SWITCH_TYPE_CURTAIN){
			
				swCommand_fromUsr.objRelay = 4;
			}
			else{
			
				swCommand_fromUsr.objRelay = statusPad;
			}
			
			if(DEV_actReserve & 0x01)tipsBeep_IF = 1;
		
		}break;
		
		case 2:{
		
			if(SWITCH_TYPE == SWITCH_TYPE_SWBIT1){
			
				swCommand_fromUsr.objRelay = 1;
			}
			else if(SWITCH_TYPE == SWITCH_TYPE_SWBIT2){
			
				swCommand_fromUsr.objRelay = 0;
			}
			else if(SWITCH_TYPE == SWITCH_TYPE_CURTAIN){
			
				swCommand_fromUsr.objRelay = 2;
			}
			else{
			
				swCommand_fromUsr.objRelay = statusPad;
			}
			
			if(DEV_actReserve & 0x02)tipsBeep_IF = 1;
		
		}break;
		
		case 4:{
	
			if(SWITCH_TYPE == SWITCH_TYPE_SWBIT2){
			
				swCommand_fromUsr.objRelay = 2;
			}
			else if(SWITCH_TYPE == SWITCH_TYPE_CURTAIN){
			
				swCommand_fromUsr.objRelay = 1;
			}
			else{
			
				swCommand_fromUsr.objRelay = statusPad;
			}
			
			if(DEV_actReserve & 0x04)tipsBeep_IF = 1;
			
		}break;
			
		default:{
		
			switch(SWITCH_TYPE){ //针对多位开关，多个按键可同时触发
			
				case SWITCH_TYPE_SWBIT1:{
				
					if(statusPad & 0x02)swCommand_fromUsr.objRelay |= 0x01;
					if(DEV_actReserve & 0x02)tipsBeep_IF = 1;
					
				}break;
					
				case SWITCH_TYPE_SWBIT2:{
				
					if(statusPad & 0x01)swCommand_fromUsr.objRelay |= 0x01;
					if(statusPad & 0x04)swCommand_fromUsr.objRelay |= 0x02;
					
					if(DEV_actReserve & 0x05)tipsBeep_IF = 1;
					
				}break;
					
				case SWITCH_TYPE_SWBIT3:{
				
					if(statusPad & 0x01)swCommand_fromUsr.objRelay |= 0x01;
					if(statusPad & 0x02)swCommand_fromUsr.objRelay |= 0x02;
					if(statusPad & 0x04)swCommand_fromUsr.objRelay |= 0x04;
					
					if(DEV_actReserve & 0x07)tipsBeep_IF = 1;
					
				}break;
					
				default:{
				
					return; //其他类型开关不支持同时多位按键触发，若有多位按键同时触发则判定为误操作，动作不执行
				
				}break;
			}
		
		}break;
	}
	
	if(SWITCH_TYPE == SWITCH_TYPE_SWBIT1 || SWITCH_TYPE == SWITCH_TYPE_SWBIT2 || SWITCH_TYPE == SWITCH_TYPE_SWBIT3){
	
		swCommand_fromUsr.actMethod = relay_flip;
		
	}else{
	
		swCommand_fromUsr.actMethod = relay_OnOff;
	}

	if(!shortPressCnt_IF){ //非连按才触发互控
	
		if(SWITCH_TYPE == SWITCH_TYPE_SWBIT1 || SWITCH_TYPE == SWITCH_TYPE_SWBIT2 || SWITCH_TYPE == SWITCH_TYPE_SWBIT3)EACHCTRL_realesFLG |= (status_actuatorRelay ^ swCommand_fromUsr.objRelay); //有效互控触发
		else
		if(SWITCH_TYPE == SWITCH_TYPE_CURTAIN)EACHCTRL_realesFLG = 1; //有效互控触发
	}
	
	if(swCommand_fromUsr.objRelay)devActionPush_IF.push_IF = 1; //推送
	devStatus_pushIF = true; //开关状态数据推送
	if(tipsBeep_IF)beeps_usrActive(3, 25, 1); //tips

}

LOCAL void 
touchPad_functionTrigNormal(u8 statusPad, keyCfrm_Type statusCfm){ //普通触摸按键触发

	switch(statusCfm){
	
		case press_Short:{

			os_printf("touchShort get:%02X.\n", statusPad);
		
			normalBussiness_shortTouchTrig(statusPad, false); //普通短按业务触发
			
		}break;
		
		case press_ShortCnt:{

			os_printf("touchCnt get:%02X.\n", statusPad);

			touchKeepCnt_record ++; //连按进行时计数变量更新
		
			if(touchKeepCnt_record == 3){
			
				param_combinationFunTrigger_3S1L.param_combinationFunPreTrig_standBy_FLG = true; //特殊组合动作预触发标志置位<3短1长>
				param_combinationFunTrigger_3S1L.param_combinationFunPreTrig_standBy_keyVal = statusPad; //特殊组合动作预触发按键键值对比缓存更新<3短1长>
			
			}else{
			
				memset(&param_combinationFunTrigger_3S1L, 0, sizeof(param_combinationFunPreTrig));
			} 

			normalBussiness_shortTouchTrig(statusPad, true); //普通短按业务触发

		}break;
		
		case press_LongA:{

			if(param_combinationFunTrigger_3S1L.param_combinationFunPreTrig_standBy_FLG && (statusPad == param_combinationFunTrigger_3S1L.param_combinationFunPreTrig_standBy_keyVal)){ //特殊组合按键动作业务触发<3短1长>

				memset(&param_combinationFunTrigger_3S1L, 0, sizeof(param_combinationFunPreTrig)); //预触发标志清零、参数清空

				os_printf("combination fun<3S1L> trig!\n");

				usrFunCB_pressLongA();

			}else{

				os_printf("touchLongA get:%02X.\n", statusPad);
			
				switch(statusPad){
				
					case 1:
					case 2:
					case 4:{
						

					}break;
						
					default:{}break;
				}				
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

	{ //按键特殊组合动作相关标志及变量清零
	
		if(statusCfm != press_ShortCnt){

			touchKeepCnt_record = 1; //连按进行时计数变量复原
			memset(&param_combinationFunTrigger_3S1L, 0, sizeof(param_combinationFunPreTrig)); //特殊组合动作预触发标志复位、参数清空<3短1长>
			
			if(statusCfm != press_Short)memset(&param_combinationFunTrigger_3S5S, 0, sizeof(param_combinationFunPreTrig)); //非短按及非连续短按，预触发标志复位、参数清空<3短5短>
		}
	}
}

LOCAL void 
touchPad_functionTrigContinue(u8 statusPad, u8 loopCount){	//普通触摸按键连按触发
	
	if(SWITCH_TYPE == SWITCH_TYPE_SWBIT1 || SWITCH_TYPE == SWITCH_TYPE_SWBIT2 || SWITCH_TYPE == SWITCH_TYPE_SWBIT3)EACHCTRL_realesFLG = statusPad; //连按最后一次触发有效互控
	else
	if(SWITCH_TYPE == SWITCH_TYPE_CURTAIN)EACHCTRL_realesFLG = 1; //有效互控触发
	devStatus_pushIF = true; //连按结束后触发开关状态数据推送

	os_printf("touchCnt over:%02X, %02dtime.\n", statusPad, loopCount);

	switch(loopCount){

		case 2:{

			switch(statusPad){
			
				case 1:{
				
					
				}break;
					
				case 2:{
				
					
				}break;
					
				case 4:{
			
					
				}break;
					
				default:{}break;
			}
			
		}break;

		case 3:{

			param_combinationFunTrigger_3S5S.param_combinationFunPreTrig_standBy_FLG = true; //特殊组合动作按键预触发标志置位<3短5短>
			param_combinationFunTrigger_3S5S.param_combinationFunPreTrig_standBy_keyVal = statusPad; //特殊组合动作按键预触发按键对比键值更新<3短5短>
			combinationFunFLG_3S5S_cancel_counter = 3000;  //特殊组合动作按键预触衔接时间计时开始<3短5短>
		
		}break;

		case 5:{

			if(param_combinationFunTrigger_3S5S.param_combinationFunPreTrig_standBy_FLG && (statusPad == param_combinationFunTrigger_3S5S.param_combinationFunPreTrig_standBy_keyVal)){ //特殊组合按键动作业务触发<3短5短>
			
				memset(&param_combinationFunTrigger_3S5S, 0, sizeof(param_combinationFunPreTrig));

				os_printf("combination fun<3S5S> trig!\n");
				
				usrZigbNwkOpen_start();
			}

		}break;
		
		case 6:{}break;

		case 10:{}break;
	
		default:{}break;
	}

	{ //按键特殊组合动作相关标志及变量清零
	
		touchKeepCnt_record = 1; //连按进行时计数变量复位
		memset(&param_combinationFunTrigger_3S1L, 0, sizeof(param_combinationFunPreTrig)); //特殊组合动作预触发标志复位、参数清空<3短1长>
		if(loopCount != 3){ //非3短
		
			memset(&param_combinationFunTrigger_3S5S, 0, sizeof(param_combinationFunPreTrig)); //特殊组合动作预触发标志复位、参数清空<3短5长>
		}
	}
}

LOCAL u8 
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

LOCAL bool 
UsrKEYScan_oneShoot(void){

	if(!usrDats_sensor.usrKeyIn_fun_0)return true;
	else return false;
}

LOCAL u8 
touchPadScan_oneShoot(void){

	u8 valKey_Temp = 0;
	
	if(!usrDats_sensor.usrKeyIn_rly_0)valKey_Temp |= 0x01;
	if(!usrDats_sensor.usrKeyIn_rly_1)valKey_Temp |= 0x02;
	if(!usrDats_sensor.usrKeyIn_rly_2)valKey_Temp |= 0x04;
	
	return valKey_Temp;
}

LOCAL void 
DcodeScan(void){

	static u8 	val_Dcode_Local 	= 0x00, //溢出赋值 提前触发首次检测
				comfirm_Cnt			= 200;  //溢出赋值 提前触发首次检测
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

		beeps_usrActive(3, 20, 2);
		tips_statusChangeToNormal();

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
				if(!relayStatusRecovery_doneIF){ //首次触发继电器状态恢复

					relayStatusRecovery_doneIF = true;
					
					actuatorRelay_Init();
				}
				
			}else{
			
				relayStatus_ifSave = statusSave_disable;
			}
		}
		
		if(val_Dcode_differ & Dcode_FLG_bitReserve){
		
			switch(Dcode_bitReserve(val_Dcode_Local)){
			
				case 0:{
				
					SWITCH_TYPE = SWITCH_TYPE_CURTAIN;	
					
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

LOCAL void 
UsrKEYScan(funKey_Callback funCB_Short, funKey_Callback funCB_LongA, funKey_Callback funCB_LongB){
	
	const  u16 	keyCfrmLoop_Short 	= 20,		//消抖时间 单位：ms
			   	keyCfrmLoop_LongA 	= 3000,		//长按A时间  单位：ms
			   	keyCfrmLoop_LongB 	= 12000,	//长按B时间  单位：ms
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

LOCAL void 
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

	if(!combinationFunFLG_3S5S_cancel_counter)memset(&param_combinationFunTrigger_3S5S, 0, sizeof(param_combinationFunPreTrig)); //<3短5短>特殊组合按键衔接时间超时检测业务，超时则将对应预触发标志复位、参数清空

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
			else{

				if((touchCfrmLoop_MAX - touchPadActCounter) < touchCfrmLoop_Short){ //短按消抖时间内随时可做键值变更，否则禁止
				
					touchPadActCounter = touchCfrmLoop_MAX;
					touchPadContinueCnt = timeDef_touchPressContinue;  //连按间隔时间判断
					touchPad_temp = touchPadScan_oneShoot();
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

LOCAL void 
usrInterfaceProcess_task(void *pvParameters){

	for(;;){

		touchPad_Scan();
		UsrKEYScan(usrFunCB_pressShort, usrFunCB_pressLongA, usrFunCB_pressLongB);
		DcodeScan();
		
		vTaskDelay(1);
	}

	vTaskDelete(NULL);
}

void 
usrInterface_ThreadStart(void){

	portBASE_TYPE xReturn = pdFAIL;

	xReturn = xTaskCreate(usrInterfaceProcess_task, "Process_keyAndBtn", 512, (void *)NULL, 5, &pxTaskHandle_threadUsrInterface);

	os_printf("\npxTaskHandle_threadKeyAndBtn is %d\n", pxTaskHandle_threadUsrInterface);
}




