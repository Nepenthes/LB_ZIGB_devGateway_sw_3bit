#ifndef __HWPERIPHERIAL_ACTUATOR_H__
#define __HWPERIPHERIAL_ACTUATOR_H__

#include "esp_common.h"

#include "bsp_Hardware.h"

#define actRelay_ON		1
#define actRelay_OFF	0

#define CURTAIN_ORBITAL_PERIOD_INITTIME	0	//�������ʱ�������ʼֵ����λ��s

#define PIN_RELAY_1		usrDats_actuator.conDatsOut_rly_0
#define PIN_RELAY_2		usrDats_actuator.conDatsOut_rly_1
#define PIN_RELAY_3		usrDats_actuator.conDatsOut_rly_2

typedef enum{

	statusSave_enable = 1,
	statusSave_disable,
}status_ifSave;

typedef enum{

	relay_flip = 1, //��ת����
	relay_OnOff, //ֱ�ӿ���
	actionNull,
}rly_methodType;

typedef struct{

	u8 act_counter;
	u8 act_period;
	
	enum{
	
		cTact_stop = 0,
		cTact_close,
		cTact_open,
	}act:8;

}stt_motorAttr;

typedef struct{

	u8 objRelay;
	rly_methodType actMethod;
}relay_Command;

typedef struct{

	bool push_IF; //����ʹ��
	u8 dats_Push; //��������-����λ��ʾ����λ������λ��ʾ����״̬��һһ��Ӧ
}relayStatus_PUSH;

extern u8 status_actuatorRelay;
extern status_ifSave relayStatus_ifSave;
extern relay_Command swCommand_fromUsr;
extern bool devStatus_ctrlEachO_IF;
extern u8 EACHCTRL_realesFLG;
extern stt_motorAttr curtainAct_Param;
extern bool devStatus_pushIF;
extern relayStatus_PUSH devActionPush_IF;

void actuatorRelay_Init(void);	
void curtainOrbitalPeriod_Reales(void);
void relayActing_ThreadStart(void);

#endif

