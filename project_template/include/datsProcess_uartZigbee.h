#ifndef __DATSPROCESS_UARTZIGBEE_H__
#define __DATSPROCESS_UARTZIGBEE_H__

#include "esp_common.h"
#include "freertos/queue.h"

#define uart_putDats(a,b,c)	uartZigbee_putDats(a,b,c)

#define ZIGB_UTCTIME_START	946684800UL //zigbeeʱ�����unix��Ԫ946684800<2000/01/01 00:00:00>��ʼ

#define WIFI_FRAME_HEAD		0x7F
#define ZIGB_FRAME_HEAD		0xFE

#define DTMODEKEEPACESS_FRAMEHEAD_ONLINE	0xFA	//��ʱѯ��ģʽ֡ͷ-internet����
#define DTMODEKEEPACESS_FRAMEHEAD_OFFLINE	0xFB	//��ʱѯ��ģʽ֡ͷ-internet����
#define	DTMODEKEEPACESS_FRAMECMD_ASR		0xA1	//��ʱѯ��ģʽ֡���� - ����Ӧ��
#define	DTMODEKEEPACESS_FRAMECMD_PST		0xA2	//��ʱѯ��ģʽ֡���� - �����ϴ�

#define ZIGB_FRAMEHEAD_CTRLLOCAL		0xAA //�������֡ͷ������
#define ZIGB_FRAMEHEAD_CTRLREMOTE		0xCC //�������֡ͷ��Զ��
#define ZIGB_FRAMEHEAD_HEARTBEAT		0xAB //�������֡ͷ������<����internet����>
#define ZIGB_OFFLINEFRAMEHEAD_HEARTBEAT	0xBB //�������֡ͷ������<����internet����>

#define STATUSLOCALEACTRL_VALMASKRESERVE_ON		0x0A //������ѯ����ֵ������״̬�������룺��
#define STATUSLOCALEACTRL_VALMASKRESERVE_OFF	0x0B //������ѯ����ֵ������״̬�������룺��

#define CTRLEATHER_PORT_NUMSTART		0x10 //���ض˿���ʼ���
#define CTRLEATHER_PORT_NUMTAIL			0xFF //���ض˿ڽ������

#define ZIGB_ENDPOINT_CTRLSECENARIO		12 //������Ⱥ����ר�ö˿�
#define ZIGB_ENDPOINT_CTRLNORMAL		13 //��������ת��ר�ö˿�
#define ZIGB_ENDPOINT_CTRLSYSZIGB		14 //zigbϵͳ����ר�ö˿�

#define ZIGB_CLUSTER_DEFAULT_DEVID		13
#define ZIGB_CLUSTER_DEFAULT_CULSTERID	13

#define ZIGBNWKOPENTIME_DEFAULT	30 //zigb���翪��ʱ�� Ĭ��ֵ ��λ��ms

#define ZIGB_PANID_MAXVAL     	0x3FFF //�������PANID���ֵ

#define ZIGB_SYSCMD_NWKOPEN					0x68 //zigbϵͳָ���������
#define ZIGB_SYSCMD_TIMESET					0x69 //zigbϵͳָ����ӽڵ��������ʱ��ͬ���趨
#define ZIGB_SYSCMD_DEVHOLD					0x6A //zigbϵͳָ��豸�������(���ڸ������أ����ܱ����ò���)
#define ZIGB_SYSCMD_EACHCTRL_REPORT			0x6B //zigbϵͳָ����豸�����ػ㱨���ش���״̬
#define ZIGB_SYSCMD_COLONYPARAM_REQPERIOD	0x6C //zigbϵͳָ���Ⱥ���Ʊ����ܿ�״̬��������ѯӦ��(���������ͻ���)

#define zigB_remoteDataTransASY_QbuffLen 		100 //���ط�����Զ���������󣬻������
#define zigB_remoteDataTransASY_txPeriod		300 //����������������	��λ��ms
#define zigB_remoteDataTransASY_txReapt			10	//�������������ظ�����
#define zigB_remoteDataTransASY_txUartOnceWait	4	//�첽����ͨ��Զ���������󵥴εȴ�ʱ�� ��λ��ms

extern xQueueHandle xMsgQ_Zigb2Socket;
extern xQueueHandle xMsgQ_zigbFunRemind;

typedef enum{

	msgFun_nwkOpen = 0, //��������
	msgFun_nodeSystimeSynchronous, //UTCʱ�估ʱ���·������豸ͬ��
	msgFun_localSystimeZigbAdjust, //����zigbϵͳʱ����UTCʱ��ͬ��
	msgFun_portCtrlEachoRegister, //���ض˵㣨ͨѶ�أ�ע��
	msgFun_panidRealesNwkCreat, //����zigb����panid����
	msgFun_scenarioCrtl, //������Ⱥ����
}enum_zigbFunMsg;

typedef struct{

	u16 deviveID;
	u8  endPoint; 
}devDatsTrans_portAttr;

typedef struct{

	u8 command;
	u8 dats[32];
	u8 datsLen;
}frame_zigbSysCtrl;

typedef struct{

	u8 dats[96];
	u8 datsLen;
}sttUartRcv_rmoteDatComming;

typedef struct{

	u8 dats[32];
	u8 datsLen;
}sttUartRcv_sysDat;

typedef struct{

	u8 dats[16];
	u8 datsLen;
}sttUartRcv_rmDatsReqResp;

#define dataRemote_RESPLEN 8
typedef struct{

	u8 	dataReq[96];
	u8 	dataReq_Len;
	u8 	dataResp[dataRemote_RESPLEN];
	u8 	dataResp_Len;

	u16 dataReqPeriod;	//ָ���´ﵥ�η�������
	u8 	repeat_Loop;	//ָ���´﷢���ظ�����
}stt_dataRemoteReq;

typedef struct{

	u8 cmdResp[2];
	u8 frameResp[96];
	u8 frameRespLen;
}datsZigb_reqGet;

typedef struct{

	u16 keepTxUntilCmp_IF:1; // 0:�����ģ���ʱǰֻ��һ�Σ�/ 1:���ģ���ʱǰ�����Է��ͣ� 
	u16 datsTxKeep_Period:15; //��������
}remoteDataReq_method;

/*Zigbee�ڵ��豸��Ϣ�������ݽṹ*/
#define DEVMAC_LEN	5
typedef struct ZigB_nwkState_Form{

	u16	nwkAddr;				//����̵�ַ
	u8	macAddr[DEVMAC_LEN];	//�豸MAC��ַ
	u8	devType;				//�豸����
	u16	onlineDectect_LCount;	//������������ʵʱ����
	
	struct ZigB_nwkState_Form *next;
}nwkStateAttr_Zigb;

typedef enum datsZigb_structType{

	zigbTP_NULL = 0,
	zigbTP_MSGCOMMING,
	zigbTP_ntCONNECT,
}datsZigb_sttType;

typedef struct ZigB_datsRXAttr_typeMSG{

	bool ifBroadcast;	//�Ƿ�Ϊ�㲥Դ
	u16	 Addr_from;		//Դ��ַ��������
	u8	 srcEP;			//Դ�˵�
	u8	 dstEP;			//�ն˵�
	u16	 ClusterID;		//��ID
	u8	 LQI;			//��������
	u8 	 datsLen;		//���ݳ���
	u8	 dats[100];		//����
}datsAttr_ZigbRX_tpMSG;

typedef struct ZigB_datsRXAttr_typeONLINE{

	u16	 nwkAddr_fresh;		//�����߽ڵ�����̵�ַ
}datsAttr_ZigbRX_tpONLINE;

typedef union ZigB_datsRXAttr{

	datsAttr_ZigbRX_tpMSG 		stt_MSG;
	datsAttr_ZigbRX_tpONLINE	stt_ONLINE;
}ZigbAttr_datsZigbRX;

typedef struct ZigB_Init_datsAttr{

	u8 	 zigbInit_reqCMD[2];	//����ָ��
	u8 	 zigbInit_reqDAT[96];	//��������
	u8	 reqDAT_num;			//�������ݳ���
	u8 	 zigbInit_REPLY[96];	//��Ӧ����
	u8 	 REPLY_num;				//��Ӧ���ݳ���
	u16  timeTab_waitAnsr;		//�ȴ���Ӧʱ��
}datsAttr_ZigbInit;

typedef struct ZigBAttr_datsRX{

	datsZigb_sttType datsType:4;	//�������ͣ���Զ����Ϣ���ݣ�����ϵͳЭ��ջ����
	ZigbAttr_datsZigbRX datsSTT;
}datsAttr_ZigbTrans;

extern u16 nwkZigb_currentPANID;
extern u16 sysZigb_random;

void uart0Init_datsTrans(void);
void zigbee_mainThreadStart(void);
bool zigb_datsRequest(u8 frameREQ[],
						   u8 frameREQ_Len,
					  	   u8 resp_cmd[2],
						   datsZigb_reqGet *datsRX,
						   u16 timeWait,
						   remoteDataReq_method method);
bool zigb_VALIDA_INPUT(uint8 REQ_CMD[2],		//ָ��
					        uint8 REQ_DATS[],	//����
					        uint8 REQdatsLen,	//���ݳ���
					        uint8 ANSR_frame[],	//��Ӧ֡
					        uint8 ANSRdatsLen,	//��Ӧ֡����
					        uint8 times,uint16 timeDelay);//ѭ�����������εȴ�ʱ��


#endif

