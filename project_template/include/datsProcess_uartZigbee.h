#ifndef __DATSPROCESS_UARTZIGBEE_H__
#define __DATSPROCESS_UARTZIGBEE_H__

#include "esp_common.h"
#include "freertos/queue.h"

#define uart_putDats(a,b,c)	uartZigbee_putDats(a,b,c)

#define ZIGB_UTCTIME_START	946684800UL //zigbee时间戳从unix纪元946684800<2000/01/01 00:00:00>开始

#define WIFI_FRAME_HEAD		0x7F
#define ZIGB_FRAME_HEAD		0xFE

#define DTMODEKEEPACESS_FRAMEHEAD_ONLINE	0xFA	//定时询访模式帧头-internet在线
#define DTMODEKEEPACESS_FRAMEHEAD_OFFLINE	0xFB	//定时询访模式帧头-internet离线
#define	DTMODEKEEPACESS_FRAMECMD_ASR		0xA1	//定时询访模式帧命令 - 被动应答
#define	DTMODEKEEPACESS_FRAMECMD_PST		0xA2	//定时询访模式帧命令 - 主动上传

#define ZIGB_FRAMEHEAD_CTRLLOCAL		0xAA //常规控制帧头：本地
#define ZIGB_FRAMEHEAD_CTRLREMOTE		0xCC //常规控制帧头：远程
#define ZIGB_FRAMEHEAD_HEARTBEAT		0xAB //常规控制帧头：心跳<网关internet在线>
#define ZIGB_OFFLINEFRAMEHEAD_HEARTBEAT	0xBB //常规控制帧头：心跳<网关internet离线>

#define STATUSLOCALEACTRL_VALMASKRESERVE_ON		0x0A //互控轮询更新值，开关状态操作掩码：开
#define STATUSLOCALEACTRL_VALMASKRESERVE_OFF	0x0B //互控轮询更新值，开关状态操作掩码：关

#define CTRLEATHER_PORT_NUMSTART		0x10 //互控端口起始编号
#define CTRLEATHER_PORT_NUMTAIL			0xFF //互控端口结束编号

#define CTRLSECENARIO_RESPCMD_SPECIAL	0xCE //场景控制回复专用数据（取消后摇）

#define ZIGB_ENDPOINT_CTRLSECENARIO		12 //场景集群控制专用端口
#define ZIGB_ENDPOINT_CTRLNORMAL		13 //常规数据转发专用端口
#define ZIGB_ENDPOINT_CTRLSYSZIGB		14 //zigb系统交互专用端口

#define ZIGB_CLUSTER_DEFAULT_DEVID		13
#define ZIGB_CLUSTER_DEFAULT_CULSTERID	13

#define ZIGBNWKOPENTIME_DEFAULT	30 //zigb网络开放时间 默认值 单位：ms

#define ZIGB_PANID_MAXVAL     	0x3FFF //随机产生PANID最大值

#define ZIGB_SYSCMD_NWKOPEN					0x68 //zigb系统指令，开放网络
#define ZIGB_SYSCMD_TIMESET					0x69 //zigb系统指令，对子节点进行网络时间同步设定
#define ZIGB_SYSCMD_DEVHOLD					0x6A //zigb系统指令，设备网络挂起(用于更换网关，网管本身用不到)
#define ZIGB_SYSCMD_EACHCTRL_REPORT			0x6B //zigb系统指令，子设备向网关汇报互控触发状态
#define ZIGB_SYSCMD_COLONYPARAM_REQPERIOD	0x6C //zigb系统指令，集群控制本地受控状态周期性轮询应答(包括场景和互控)
#define ZIGB_SYSCMD_DATATRANS_HOLD			0x6D //zigb系统指令，主动挂起周期性通信一段时间，之后恢复

#define zigB_remoteDataTransASY_QbuffLen 		60  //本地非阻塞远端数据请求，缓存队列
#define zigB_remoteDataTransASY_txPeriod		200 //单条数据请求周期	单位：ms
#define zigB_remoteDataTransASY_txReapt			10	//单条数据请求重复次数
#define zigB_remoteDataTransASY_txUartOnceWait	4	//异步串口通信远端数据请求单次等待时间 单位：10ms

#define zigB_ScenarioCtrlDataTransASY_txBatchs_EN			0	//异步串口通信远端场景控制数据请求多批次分发使能
#define zigB_ScenarioCtrlDataTransASY_opreatOnceNum			10	//场景控制逻辑业务单轮操作单位数目(大包场次分批次异步发送，单轮/单批次数目)

#define zigB_ScenarioCtrlDataTransASY_timeRoundPause		0	//数据发送轮次间距暂歇时间系数 单位：视业务调用周期而定，近似单位为 10ms/per

#define zigB_ScenarioCtrlDataTransASY_QbuffLen 				100 //本地非阻塞远端场景控制数据请求，缓存队列
#define zigB_ScenarioCtrlDataTransASY_txPeriod				200 //单条场景控制数据请求周期	单位：ms
#define zigB_ScenarioCtrlDataTransASY_txReapt				30	//单条场景控制数据请求重复次数
#define zigB_ScenarioCtrlDataTransASY_txTimeWaitOnceBasic	2	//异步串口通信远端场景控制数据请求单次保底等待时间 单位：10ms/per
#define zigB_ScenarioCtrlDataTransASY_txTimeWaitOnceStep	2	//异步串口通信远端场景控制数据请求单次等待时间延长步距 单位：10ms/per

typedef enum{

	msgFun_nwkOpen = 0, //开放网络
	msgFun_nodeSystimeSynchronous, //UTC时间及时区下发至子设备同步
	msgFun_localSystimeZigbAdjust, //本地zigb系统时间与UTC时间同步
	msgFun_portCtrlEachoRegister, //互控端点（通讯簇）注册
	msgFun_panidRealesNwkCreat, //本地zigb主机panid更新
	msgFun_scenarioCrtl, //场景集群控制
	msgFun_dtPeriodHoldPst, //使子节点设备周期性远端通信挂起
	msgFun_dtPeriodHoldCancelAdvance,  //使子节点设备周期性远端通信挂起提前结束
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

	u8 dats[128 + 25];
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

typedef struct{

	u16 respNwkAddr;
}sttUartRcv_scenarioCtrlResp;

#define dataRemote_RESPLEN 8
typedef struct{

	u8 	dataReq[96];
	u8 	dataReq_Len;
	u8 	dataResp[dataRemote_RESPLEN];
	u8 	dataResp_Len;

	u16 dataReqPeriod;	//指令下达单次发送周期
	u8 	repeat_Loop;	//指令下达发送重复次数
}stt_dataRemoteReq;

#define dataScenario_RESPLEN 8
typedef struct{

	u8 	dataReq[16];
	u8 	dataReq_Len;
	u16 dataRespNwkAddr;

	u16 dataReqPeriod;	//指令下达单次发送周期
	u8 	repeat_Loop:7;	//指令下达发送重复次数
	u8 	scenarioOpreatCmp_flg:1; //单元场景操作成功完成标志
}stt_dataScenarioReq;

typedef struct{

	u8 cmdResp[2];
	u8 frameResp[96];
	u8 frameRespLen;
}datsZigb_reqGet;

typedef struct{

	u16 keepTxUntilCmp_IF:1; // 0:非死磕（超时前只发一次）/ 1:死磕（超时前周期性发送） 
	u16 datsTxKeep_Period:15; //死磕周期
}remoteDataReq_method;

/*Zigbee节点设备信息链表数据结构*/
#define DEVMAC_LEN	5
typedef struct ZigB_nwkState_Form{

	u16	nwkAddr;				//网络短地址
	u8	macAddr[DEVMAC_LEN];	//设备MAC地址
	u8	devType;				//设备类型
	u16	onlineDectect_LCount;	//心跳计时——实时更新
	
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
	u8	 dats[128];		//数据
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

extern xQueueHandle xMsgQ_Zigb2Socket;
extern xQueueHandle xMsgQ_zigbFunRemind;

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
bool zigb_VALIDA_INPUT(uint8 REQ_CMD[2],		//指令
					        uint8 REQ_DATS[],	//数据
					        uint8 REQdatsLen,	//数据长度
					        uint8 ANSR_frame[],	//响应帧
					        uint8 ANSRdatsLen,	//响应帧长度
					        uint8 times,uint16 timeDelay);//循环次数，单次等待时间


#endif

