#include "hwPeripherial_Actuator.h"

#include "datsManage.h"

extern u16	delayCnt_closeLoop;

u8 status_actuatorRelay = 0;
status_ifSave relayStatus_ifSave = statusSave_disable;

relay_Command swCommand_fromUsr	= {0, actionNull};

bool devStatus_ctrlEachO_IF = false; //互控状态组播发送使能--为使异步信号同步
u8 EACHCTRL_realesFLG = 0; //互控更新使能标志 标志（一位：bit0\二位：bit1\三位：bit2）

bool devStatus_pushIF = false; //推送使能-为使异步信号同步
relayStatus_PUSH devActionPush_IF = {0}; //推送执行数据

LOCAL xTaskHandle pxTaskHandle_threadRelayActing;
/*---------------------------------------------------------------------------------------------*/

LOCAL void ICACHE_FLASH_ATTR
relay_statusReales(void){
	
	if(DEV_actReserve & 0x01)(status_actuatorRelay & 0x01)?(PIN_RELAY_1 = 1):(PIN_RELAY_1 = 0);
	if(DEV_actReserve & 0x02)(status_actuatorRelay & 0x02)?(PIN_RELAY_2 = 1):(PIN_RELAY_2 = 0);
	if(DEV_actReserve & 0x04)(status_actuatorRelay & 0x04)?(PIN_RELAY_3 = 1):(PIN_RELAY_3 = 0);

	tips_statusChangeToNormal(); //tips响应
}

LOCAL void ICACHE_FLASH_ATTR
actuatorRelay_Init(void){

	u8 statusTemp = 0;

	if(relayStatus_ifSave = statusSave_enable){
		
		stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();
	
		statusTemp = (u8)(datsRead_Temp->rlyStaute_flg);

		status_actuatorRelay = statusTemp;
	
		if(datsRead_Temp)os_free(datsRead_Temp);
	
	}else{

		stt_usrDats_privateSave datsSave_Temp = {0};
		
		status_actuatorRelay = statusTemp;

		datsSave_Temp.rlyStaute_flg = statusTemp;
		devParam_flashDataSave(obj_rlyStaute_flg, datsSave_Temp);
	}

	relay_statusReales();
}

/*继电器动作*/
LOCAL void ICACHE_FLASH_ATTR
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
	if(		(statusTemp & 0x01) != (status_actuatorRelay & 0x01))devActionPush_IF.dats_Push |= 0x20; //更改值填装<高三位>第一位
	else if((statusTemp & 0x02) != (status_actuatorRelay & 0x02))devActionPush_IF.dats_Push |= 0x40; //更改值填装<高三位>第二位
	else if((statusTemp & 0x04) != (status_actuatorRelay & 0x04))devActionPush_IF.dats_Push |= 0x80; //更改值填装<高三位>第三位
	if(devStatus_pushIF){

		devStatus_pushIF = false;
		devActionPush_IF.push_IF = true;
	}
	
	if(status_actuatorRelay)delayCnt_closeLoop = 0; //继电器开则立即更新绿色模式计时
	if(EACHCTRL_realesFLG)devStatus_ctrlEachO_IF = true; //若有互控触发，则此时开放互控信息发送使能（为了信号变量同步，若提前发送，继电器状态还没变就发送了）
	
	if(relayStatus_ifSave == statusSave_enable){ //状态记忆更新

		stt_usrDats_privateSave datsSave_Temp = {0};

		datsSave_Temp.rlyStaute_flg = status_actuatorRelay;
		devParam_flashDataSave(obj_rlyStaute_flg, datsSave_Temp);
	}
}

LOCAL void ICACHE_FLASH_ATTR
relayActingProcess_task(void *pvParameters){

	for(;;){
	
		if(swCommand_fromUsr.actMethod != actionNull){ //请求响应
		
			actuatorRelay_Act(swCommand_fromUsr);
			
			swCommand_fromUsr.actMethod = actionNull;
			swCommand_fromUsr.objRelay = 0;
		}

		vTaskDelay(1);
	}

	vTaskDelete(NULL);
}

void ICACHE_FLASH_ATTR
relayActing_ThreadStart(void){

	portBASE_TYPE xReturn = pdFAIL;

	xReturn = xTaskCreate(relayActingProcess_task, "Process_relayActing", 512, (void *)NULL, 5, &pxTaskHandle_threadRelayActing);

	os_printf("\npxTaskHandle_threadRelayActing is %d\n", pxTaskHandle_threadRelayActing);
}



