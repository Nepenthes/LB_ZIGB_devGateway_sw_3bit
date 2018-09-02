#ifndef __DATSPROCESS_UARTZIGBEE_H__
#define __DATSPROCESS_UARTZIGBEE_H__

#include "esp_common.h"
#include "freertos/queue.h"

#define uart_putDats(a,b,c)	uartZigbee_putDats(a,b,c)

#define WIFI_FRAME_HEAD		0x7F
#define ZIGB_FRAME_HEAD		0xFE

#define ZIGB_FRAMEHEAD_CTRLLOCAL		0xAA
#define ZIGB_FRAMEHEAD_CTRLREMOTE		0xCC
#define ZIGB_FRAMEHEAD_HEARTBEAT		0xAB
#define ZIGB_OFFLINEFRAMEHEAD_HEARTBEAT	0xBB

#define ZIGB_ENDPOINT_CTRLNORMAL		13
#define ZIGB_ENDPOINT_CTRLSYSZIGB		14

#define ZIGB_CLUSTER_DEFAULT_DEVID		13
#define ZIGB_CLUSTER_DEFAULT_CULSTERID	13

#define ZIGBNWKOPENTIME_DEFAULT	5 //zigb网络开放时间 默认值

#define ZIGB_PANID_MAXVAL     	0x3FFF

#define ZIGB_SYSCMD_NWKOPEN	0x68 //zigb系统指令，开放网络
#define ZIGB_SYSCMD_TIMESET	0x69 //zigb系统指令，对子节点进行网络时间同步设定

extern xQueueHandle xMsgQ_Zigb2Socket;
extern xQueueHandle xMsgQ_zigbFunRemind;

typedef enum{

	msgFun_nwkOpen = 0,
	msgFun_nodeSystimeSynchronous,
	msgFun_localSystimeZigbAdjust,
	msgFun_portCtrlEachoRegister,
	msgFun_panidRealesNwkCreat,
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
}uartToutDatsRcv;

typedef struct{

	u8 cmdResp[2];
	u8 frameResp[96];
	u8 frameRespLen;
}datsZigb_reqGet;

/*Zigbee节点设备信息链表数据结构*/
#define DEVMAC_LEN	5
typedef struct ZigB_nwkState_Form{

	u16	nwkAddr;				//网络短地址
	u8	macAddr[DEVMAC_LEN];	//设备MAC地址
	u8	devType;				//设备类型
	u16	onlineDectect_LCount;	//心跳技术——实时更新
	
	struct ZigB_nwkState_Form *next;
}nwkStateAttr_Zigb;

typedef enum datsZigb_structType{

	zigbTP_NULL = 0,
	zigbTP_MSGCOMMING,
	zigbTP_ntCONNECT,
}datsZigb_sttType;

typedef struct ZigB_datsRXAttr_typeMSG{

	bool ifBroadcast;	//是否为广播源
	u16	 Addr_from;		//源地址，来自哪
	u8	 srcEP;			//源端点
	u8	 dstEP;			//终端点
	u16	 ClusterID;		//簇ID
	u8	 LQI;			//链接质量
	u8 	 datsLen;		//数据长度
	u8	 dats[100];		//数据
}datsAttr_ZigbRX_tpMSG;

typedef struct ZigB_datsRXAttr_typeONLINE{

	u16	 nwkAddr_fresh;		//新上线节点网络短地址
}datsAttr_ZigbRX_tpONLINE;

typedef union ZigB_datsRXAttr{

	datsAttr_ZigbRX_tpMSG 		stt_MSG;
	datsAttr_ZigbRX_tpONLINE	stt_ONLINE;
}ZigbAttr_datsZigbRX;

typedef struct ZigB_Init_datsAttr{

	u8 	 zigbInit_reqCMD[2];	//请求指令
	u8 	 zigbInit_reqDAT[96];	//请求数据
	u8	 reqDAT_num;			//请求数据长度
	u8 	 zigbInit_REPLY[96];	//响应内容
	u8 	 REPLY_num;				//响应内容长度
	u16  timeTab_waitAnsr;		//等待响应时间
}datsAttr_ZigbInit;

typedef struct ZigBAttr_datsRX{

	datsZigb_sttType datsType:4;	//数据类型，是远端消息数据，还是系统协议栈数据
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
						   u16 timeWait);
bool zigb_VALIDA_INPUT(uint8 REQ_CMD[2],		//指令
					        uint8 REQ_DATS[],	//数据
					        uint8 REQdatsLen,	//数据长度
					        uint8 ANSR_frame[],	//响应帧
					        uint8 ANSRdatsLen,	//响应帧长度
					        uint8 times,uint16 timeDelay);//循环次数，单次等待时间


#endif

