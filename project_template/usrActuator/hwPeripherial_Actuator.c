#include "hwPeripherial_Actuator.h"

#include "datsManage.h"

extern u16	delayCnt_closeLoop;

u8 status_actuatorRelay = 0;
status_ifSave relayStatus_ifSave = statusSave_disable;

relay_Command swCommand_fromUsr	= {0, actionNull};

bool devStatus_ctrlEachO_IF = false; //����״̬�鲥����ʹ��--Ϊʹ�첽�ź�ͬ��
u8 EACHCTRL_realesFLG = 0; //���ظ���ʹ�ܱ�־ ��־��һλ��bit0\��λ��bit1\��λ��bit2��

bool devStatus_pushIF = false; //����ʹ��-Ϊʹ�첽�ź�ͬ��
relayStatus_PUSH devActionPush_IF = {0}; //����ִ������

LOCAL xTaskHandle pxTaskHandle_threadRelayActing;
/*---------------------------------------------------------------------------------------------*/

LOCAL void ICACHE_FLASH_ATTR
relay_statusReales(void){
	
	switch(SWITCH_TYPE){
	
		case SWITCH_TYPE_SWBIT1:{ //�̵���λλ�õ��� 2��1
		
			if(DEV_actReserve & 0x02)(status_actuatorRelay & 0x01)?(PIN_RELAY_1 = 1):(PIN_RELAY_1 = 0);
			
		}break;
		
		case SWITCH_TYPE_SWBIT2:{ //�̵���λλ�õ��� 3��2
		
			if(DEV_actReserve & 0x01)(status_actuatorRelay & 0x01)?(PIN_RELAY_1 = 1):(PIN_RELAY_1 = 0);
			if(DEV_actReserve & 0x04)(status_actuatorRelay & 0x02)?(PIN_RELAY_2 = 1):(PIN_RELAY_2 = 0);
		
		}break;
		
		case SWITCH_TYPE_SWBIT3:{ //�̵���λλ�ñ���
		
			if(DEV_actReserve & 0x01)(status_actuatorRelay & 0x01)?(PIN_RELAY_1 = 1):(PIN_RELAY_1 = 0);
			if(DEV_actReserve & 0x02)(status_actuatorRelay & 0x02)?(PIN_RELAY_2 = 1):(PIN_RELAY_2 = 0);
			if(DEV_actReserve & 0x04)(status_actuatorRelay & 0x04)?(PIN_RELAY_3 = 1):(PIN_RELAY_3 = 0);
		
		}break;
	}

	tips_statusChangeToNormal(); //tips��Ӧ
}

/*�̵�������*/
LOCAL void ICACHE_FLASH_ATTR
actuatorRelay_Act(relay_Command dats){
	
	u8 statusTemp = 0;

	statusTemp = status_actuatorRelay; //��ǰ�������ݴ�
	
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

	devActionPush_IF.dats_Push = 0;
	devActionPush_IF.dats_Push |= (status_actuatorRelay & 0x07); //��ǰ����״̬λ��װ<����λ>
//	/*���ȷ�ʽ*/
//	if(		(statusTemp & 0x01) != (status_actuatorRelay & 0x01))devActionPush_IF.dats_Push |= 0x20; //����ֵ��װ<����λ>��һλ
//	else if((statusTemp & 0x02) != (status_actuatorRelay & 0x02))devActionPush_IF.dats_Push |= 0x40; //����ֵ��װ<����λ>�ڶ�λ
//	else if((statusTemp & 0x04) != (status_actuatorRelay & 0x04))devActionPush_IF.dats_Push |= 0x80; //����ֵ��װ<����λ>����λ
	/*�����ȷ�ʽ*/
	if((statusTemp & 0x01) != (status_actuatorRelay & 0x01))devActionPush_IF.dats_Push |= 0x20; //����ֵ��װ<����λ>��һλ
	if((statusTemp & 0x02) != (status_actuatorRelay & 0x02))devActionPush_IF.dats_Push |= 0x40; //����ֵ��װ<����λ>�ڶ�λ
	if((statusTemp & 0x04) != (status_actuatorRelay & 0x04))devActionPush_IF.dats_Push |= 0x80; //����ֵ��װ<����λ>����λ

	if(devStatus_pushIF){

		devStatus_pushIF = false;
		devActionPush_IF.push_IF = true;
	}
	
	if(status_actuatorRelay)delayCnt_closeLoop = 0; //�̵�����������������ɫģʽ��ʱ
	if(EACHCTRL_realesFLG)devStatus_ctrlEachO_IF = true; //���л��ش��������ʱ���Ż�����Ϣ����ʹ�ܣ�Ϊ���źű���ͬ��������ǰ���ͣ��̵���״̬��û��ͷ����ˣ�
	
	if(relayStatus_ifSave == statusSave_enable){ //״̬�������

#if(RELAYSTATUS_REALYTIME_ENABLEIF)

		devParamDtaaSave_relayStatusRealTime(status_actuatorRelay);
#else

		stt_usrDats_privateSave datsSave_Temp = {0};

		datsSave_Temp.rlyStaute_flg = status_actuatorRelay;
		devParam_flashDataSave(obj_rlyStaute_flg, datsSave_Temp);
		
#endif
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

void ICACHE_FLASH_ATTR
relayActing_ThreadStart(void){

	portBASE_TYPE xReturn = pdFAIL;

	xReturn = xTaskCreate(relayActingProcess_task, "Process_relayActing", 512, (void *)NULL, 5, &pxTaskHandle_threadRelayActing);

	os_printf("\npxTaskHandle_threadRelayActing is %d\n", pxTaskHandle_threadRelayActing);
}



