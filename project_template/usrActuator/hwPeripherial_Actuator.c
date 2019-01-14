#include "hwPeripherial_Actuator.h"

#include "datsManage.h"

extern u16	delayCnt_closeLoop;

u8 status_actuatorRelay = 0;
status_ifSave relayStatus_ifSave = statusSave_disable;

relay_Command swCommand_fromUsr	= {0, actionNull};

bool devStatus_ctrlEachO_IF = false; //互控状态组播发送使能--为使异步信号同步
u8 EACHCTRL_realesFLG = 0; //互控更新使能标志 标志（一位：bit0\二位：bit1\三位：bit2）

stt_motorAttr curtainAct_Param = {
	
	.act_counter = 0, 
	.act_period = CURTAIN_ORBITAL_PERIOD_INITTIME, 
	.act = cTact_stop
};

bool devStatus_pushIF = false; //推送使能-为使异步信号同步
relayStatus_PUSH devActionPush_IF = {0}; //推送执行数据

LOCAL xTaskHandle pxTaskHandle_threadRelayActing;
/*---------------------------------------------------------------------------------------------*/

LOCAL void 
relay_statusReales(void){

	stt_usrDats_privateSave datsSave_Temp = {0};
	
	switch(SWITCH_TYPE){

		case SWITCH_TYPE_CURTAIN:{
		
			switch(status_actuatorRelay){
			
				case 1:{
				
					PIN_RELAY_2 = 1;
					PIN_RELAY_1 = PIN_RELAY_3 = 0;
					curtainAct_Param.act = cTact_open;
					
				}break;
					
				case 4:{
				
					PIN_RELAY_1 = 1;
					PIN_RELAY_2 = PIN_RELAY_3 = 0;
					curtainAct_Param.act = cTact_close;
					
				}break;
					
				case 2:
				default:{
				
					PIN_RELAY_1 = PIN_RELAY_2 = PIN_RELAY_3 = 0;
					curtainAct_Param.act = cTact_stop;

					datsSave_Temp.devCurtain_orbitalCounter = curtainAct_Param.act_counter; //每次窗帘运动停止时，记录当前位置对应的计时变量值
					devParam_flashDataSave(obj_devCurtainOrbitalCounter, datsSave_Temp); //本地存储动作执行
					
				}break;
			}
		
		}break;
	
		case SWITCH_TYPE_SWBIT1:{ //继电器位位置调整 2对1
		
			if(DEV_actReserve & 0x02)(status_actuatorRelay & 0x01)?(PIN_RELAY_1 = 1):(PIN_RELAY_1 = 0);
			
		}break;
		
		case SWITCH_TYPE_SWBIT2:{ //继电器位位置调整 3对2
		
			if(DEV_actReserve & 0x01)(status_actuatorRelay & 0x01)?(PIN_RELAY_1 = 1):(PIN_RELAY_1 = 0);
			if(DEV_actReserve & 0x04)(status_actuatorRelay & 0x02)?(PIN_RELAY_2 = 1):(PIN_RELAY_2 = 0);
		
		}break;
		
		case SWITCH_TYPE_SWBIT3:{ //继电器位位置保持
		
			if(DEV_actReserve & 0x01)(status_actuatorRelay & 0x01)?(PIN_RELAY_1 = 1):(PIN_RELAY_1 = 0);
			if(DEV_actReserve & 0x02)(status_actuatorRelay & 0x02)?(PIN_RELAY_2 = 1):(PIN_RELAY_2 = 0);
			if(DEV_actReserve & 0x04)(status_actuatorRelay & 0x04)?(PIN_RELAY_3 = 1):(PIN_RELAY_3 = 0);
		
		}break;

		default:break;
	}

	tips_statusChangeToNormal(); //tips响应
}

/*继电器动作*/
LOCAL void 
actuatorRelay_Act(relay_Command dats){
	
	u8 statusTemp = 0;

	statusTemp = status_actuatorRelay; //当前开关至暂存
	
	switch(dats.actMethod){ //根据动作类型响应动作
	
		case relay_flip:{
			
			if(dats.objRelay & 0x01)status_actuatorRelay ^= 1 << 0;
			if(dats.objRelay & 0x02)status_actuatorRelay ^= 1 << 1;
			if(dats.objRelay & 0x04)status_actuatorRelay ^= 1 << 2;
				
		}break;
		
		case relay_OnOff:{
			
			(dats.objRelay & 0x01)?(status_actuatorRelay |= 1 << 0):(status_actuatorRelay &= ~(1 << 0));
			(dats.objRelay & 0x02)?(status_actuatorRelay |= 1 << 1):(status_actuatorRelay &= ~(1 << 1));
			(dats.objRelay & 0x04)?(status_actuatorRelay |= 1 << 2):(status_actuatorRelay &= ~(1 << 2));
			
		}break;
		
		default:break;
		
	}
	relay_statusReales(); //硬件响应加载

	devActionPush_IF.dats_Push = 0;
	devActionPush_IF.dats_Push |= (status_actuatorRelay & 0x07); //当前开关状态位填装<低三位>
//	/*优先方式*/
//	if(		(statusTemp & 0x01) != (status_actuatorRelay & 0x01))devActionPush_IF.dats_Push |= 0x20; //更改值填装<高三位>第一位
//	else if((statusTemp & 0x02) != (status_actuatorRelay & 0x02))devActionPush_IF.dats_Push |= 0x40; //更改值填装<高三位>第二位
//	else if((statusTemp & 0x04) != (status_actuatorRelay & 0x04))devActionPush_IF.dats_Push |= 0x80; //更改值填装<高三位>第三位
	/*非优先方式*/
	if((statusTemp & 0x01) != (status_actuatorRelay & 0x01))devActionPush_IF.dats_Push |= 0x20; //更改值填装<高三位>第一位
	if((statusTemp & 0x02) != (status_actuatorRelay & 0x02))devActionPush_IF.dats_Push |= 0x40; //更改值填装<高三位>第二位
	if((statusTemp & 0x04) != (status_actuatorRelay & 0x04))devActionPush_IF.dats_Push |= 0x80; //更改值填装<高三位>第三位

	if(devStatus_pushIF){

		devStatus_pushIF = false;
		devActionPush_IF.push_IF = true;
	}
	
	if(status_actuatorRelay)delayCnt_closeLoop = 0; //继电器开则立即更新绿色模式计时
	if(EACHCTRL_realesFLG)devStatus_ctrlEachO_IF = true; //若有互控触发，则此时开放互控信息发送使能（为了信号变量同步，若提前发送，继电器状态还没变就发送了）
	
	if(relayStatus_ifSave == statusSave_enable){ //状态记忆更新

#if(RELAYSTATUS_REALYTIME_ENABLEIF)

		devParamDtaaSave_relayStatusRealTime(status_actuatorRelay);
#else

		stt_usrDats_privateSave datsSave_Temp = {0};

		datsSave_Temp.rlyStaute_flg = status_actuatorRelay;
		devParam_flashDataSave(obj_rlyStaute_flg, datsSave_Temp);
		
#endif
	}
}

LOCAL void 
relayActingProcess_task(void *pvParameters){

	u16 log_Counter = 0;
	const u16 log_Period = 200;

	os_printf(">>>curtain orbital period recover val:%d\n", curtainAct_Param.act_period);

	for(;;){

//		{ //打印

//			if(log_Counter < log_Period)log_Counter ++;
//			else{

//				log_Counter = 0;
//				os_printf(">>>devType: %02X, act:%d, actCounter: %d, actPeriod: %d.\n", SWITCH_TYPE, curtainAct_Param.act, curtainAct_Param.act_counter, curtainAct_Param.act_period);
//			}
//		}
	
		if(swCommand_fromUsr.actMethod != actionNull){ //请求响应
		
			actuatorRelay_Act(swCommand_fromUsr);
			
			swCommand_fromUsr.actMethod = actionNull;
			swCommand_fromUsr.objRelay = 0;
		}

		vTaskDelay(1);
	}

	vTaskDelete(NULL);
}

void 
curtainOrbitalPeriod_Reales(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();
	
	curtainAct_Param.act_period = datsRead_Temp->devCurtain_orbitalPeriod; //窗帘轨道时间初始化更新
	curtainAct_Param.act_period = datsRead_Temp->devCurtain_orbitalPeriod; //窗帘轨道位置计时值初始化更新
	if(curtainAct_Param.act_period == 0xff)curtainAct_Param.act_period = CURTAIN_ORBITAL_PERIOD_INITTIME; //限值
	if(curtainAct_Param.act_counter == 0xff)curtainAct_Param.act_counter = 0; //限制
	
	if(datsRead_Temp)os_free(datsRead_Temp);
}

void 
actuatorRelay_Init(void){

	if(relayStatus_ifSave = statusSave_enable){

#if(RELAYSTATUS_REALYTIME_ENABLEIF)

		swCommand_fromUsr.objRelay = (u8)devDataRecovery_relayStatus();
		swCommand_fromUsr.actMethod = relay_OnOff;

#else

		stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();

		swCommand_fromUsr.objRelay = (u8)(datsRead_Temp->rlyStaute_flg);
		swCommand_fromUsr.actMethod = relay_OnOff;

		if(datsRead_Temp)os_free(datsRead_Temp);
#endif

	}

	os_printf("status relay recovery data read is: %02X.\n", swCommand_fromUsr.objRelay);
}

void 
relayActing_ThreadStart(void){

	portBASE_TYPE xReturn = pdFAIL;

	xReturn = xTaskCreate(relayActingProcess_task, "Process_relayActing", 512, (void *)NULL, 5, &pxTaskHandle_threadRelayActing);

	os_printf("\npxTaskHandle_threadRelayActing is %d\n", pxTaskHandle_threadRelayActing);
}



