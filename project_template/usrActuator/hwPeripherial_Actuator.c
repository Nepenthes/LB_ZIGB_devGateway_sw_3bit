#include "hwPeripherial_Actuator.h"

#include "datsManage.h"

extern u16	delayCnt_closeLoop;

u8 status_actuatorRelay = 0;
status_ifSave relayStatus_ifSave = statusSave_disable;

relay_Command swCommand_fromUsr	= {0, actionNull};

u8 EACHCTRL_realesFLG = 0; //���ظ���ʹ�ܱ�־ ��־��һλ��bit0\��λ��bit1\��λ��bit2��

LOCAL xTaskHandle pxTaskHandle_threadRelayActing;
/*---------------------------------------------------------------------------------------------*/

LOCAL void ICACHE_FLASH_ATTR
relay_statusReales(void){
	
	if(DEV_actReserve & 0x01)(status_actuatorRelay & 0x01)?(PIN_RELAY_1 = 1):(PIN_RELAY_1 = 0);
	if(DEV_actReserve & 0x02)(status_actuatorRelay & 0x02)?(PIN_RELAY_2 = 1):(PIN_RELAY_2 = 0);
	if(DEV_actReserve & 0x04)(status_actuatorRelay & 0x04)?(PIN_RELAY_3 = 1):(PIN_RELAY_3 = 0);

	tips_statusChangeToNormal(); //tips��Ӧ
}

LOCAL void ICACHE_FLASH_ATTR
actuatorRelay_Init(void){

	u8 statusTemp = 0;

	if(relayStatus_ifSave = statusSave_enable){
		
		stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();
	
		statusTemp = (u8)(datsRead_Temp->rlyStaute_flg);

		status_actuatorRelay = statusTemp;
	
		os_free(datsRead_Temp);
	
	}else{

		stt_usrDats_privateSave datsSave_Temp = {0};
		
		status_actuatorRelay = statusTemp;

		datsSave_Temp.rlyStaute_flg = statusTemp;
		devParam_flashDataSave(obj_rlyStaute_flg, datsSave_Temp);
	}

	relay_statusReales();
}

/*�̵�������*/
LOCAL void ICACHE_FLASH_ATTR
actuatorRelay_Act(relay_Command dats){
	
	u8 statusTemp = 0;
	
	switch(dats.actMethod){ //���ݶ���������Ӧ����
	
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
	relay_statusReales(); //Ӳ����Ӧ����
	
	if(status_actuatorRelay)delayCnt_closeLoop = 0; //�̵�����������������ɫģʽ��ʱ
	
	if(relayStatus_ifSave == statusSave_enable){ //״̬�������

		stt_usrDats_privateSave datsSave_Temp = {0};

		datsSave_Temp.rlyStaute_flg = status_actuatorRelay;
		devParam_flashDataSave(obj_rlyStaute_flg, datsSave_Temp);
	}
}

LOCAL void ICACHE_FLASH_ATTR
relayActingProcess_task(void *pvParameters){

	for(;;){
	
		if(swCommand_fromUsr.actMethod != actionNull){ //������Ӧ
		
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



