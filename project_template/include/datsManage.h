#ifndef __DATSMANAGE_H__
#define __DATSMANAGE_H__

#include "esp_common.h"
#include "freertos/queue.h"

#include "datsProcess_uartZigbee.h"
#include "datsManage.h"

#define WIFIMODE_STA			1
#define WIFIMODE_AP				2
#define WIFIMODE_APSTA			3

#define scenarioCtrlDats_LENGTH 1000 //场景控制数据包长度限制

#define USRCLUSTERNUM_CTRLEACHOTHER		3 //互控端口数

#define DEVZIGB_DEFAULT			0x33	//链表对应设备类型默认值

#define DEV_SWITCH_TYPE			0xA3	//开关设备类型默认值
#define DEV_MAC_LEN				6

#define SMARTCONFIG_TIMEOPEN_DEFULT		180 //smartconfig启动时间限制 单位：s

#define SWITCH_TYPE_SWBIT1	 	0x01 + 0xA0 //设备类型，一位开关
#define SWITCH_TYPE_SWBIT2	 	0x02 + 0xA0 //设备类型，二位开关
#define SWITCH_TYPE_SWBIT3	 	0x03 + 0xA0 //设备类型，三位开关

#define SPI_FLASH_SEC_SIZE      4096
#define DATS_LOCATION_START		0x3F9	//32Mbit
//#define DATS_LOCATION_START		0x1F9	//16Mbit
//#define DATS_LOCATION_START		0x0F9	//08Mbit

#define DEBUG_LOGLEN			128

typedef struct{

	u8 devNode_MAC[5];
	u8 devNode_opStatus;
}scenarioOprateUnit_Attr;

typedef struct{

	bool scenarioCtrlOprate_IF; //场景集群控制使能
    scenarioOprateUnit_Attr scenarioOprate_Unit[100]; //集群节点MAC
	u8 devNode_num; //集群节点数目
}stt_scenarioOprateDats;

typedef struct{

	u8  FLG_factory_IF;

	u8 	rlyStaute_flg:3;	//三位继电器 状态

	u8  dev_lockIF:1;	//设备锁标志

	u8 	test_dats:3;	//测试数据

	u8  timeZone_H;
	u8  timeZone_M;

	u8 	serverIP_default[4];	//默认服务器IP

	u8  swTimer_Tab[3 * 8];	//定时表
	u8  swDelay_flg; //延时及绿色模式标志
	u8  swDelay_periodCloseLoop; //绿色模式周期
	u8  devNightModeTimer_Tab[3 * 2]; //夜间模式定时表

	u8  port_ctrlEachother[USRCLUSTERNUM_CTRLEACHOTHER]; //互控端口

	u16 panID_default; //PANID

	u8 	bkColor_swON; //色值索引，开
	u8 	bkColor_swOFF; //色值索引，关
}stt_usrDats_privateSave;

typedef enum{

	factory_record_IF = 0,

	obj_rlyStaute_flg,
	
	obj_serverIP_default,

	obj_dev_lockIF,

	obj_test_dats,

	obj_timeZone_H,
	obj_timeZone_M,

	obj_swTimer_Tab,
	obj_swDelay_flg,
	obj_swDelay_periodCloseLoop,
	obj_devNightModeTimer_Tab,

	obj_port_ctrlEachother,
	
	obj_panID_default,

	obj_bkColor_swON,
	obj_bkColor_swOFF,
	
}devDatsSave_Obj;

typedef enum{

	datsFrom_ctrlRemote = 0,
	datsFrom_ctrlLocal,
	datsFrom_heartBtRemote
}threadDatsPass_objDatsFrom;

typedef struct STTthreadDatsPass_conv{	//数据传输进程消息类型1：常规数据传输

	threadDatsPass_objDatsFrom datsFrom;	//数据来源
	u8	macAddr[5];
	u8	devType;
	u8	dats[100];
	u8  datsLen;
}stt_threadDatsPass_conv;

typedef struct STTthreadDatsPass_devQuery{	//数据传输进程消息类型2：设备列表请求

	u8	infoDevList[100];
	u8  infoLen;
}stt_threadDatsPass_devQuery;

typedef union STTthreadDatsPass_dats{	//数据实体（公用体），单次消息只存在一种类型数据

	stt_threadDatsPass_conv 	dats_conv;		//消息类型1数据
	stt_threadDatsPass_devQuery	dats_devQuery;	//消息类型2数据
}stt_threadDatsPass_dats;

typedef enum threadDatsPass_msgType{	//数据传输进程消息类型枚举

	listDev_query = 0,
	conventional,
}threadDP_msgType;

typedef struct STTthreadDatsPass{	//数据传输进程消息数据类型

	threadDP_msgType msgType;		//数据类型
	stt_threadDatsPass_dats dats;	//数据实体
}stt_threadDatsPass;

/*=======================↓↓↓定时询访机制专用数据结构↓↓↓=============================*/
typedef struct agingDataSet_bitHold{ //使用指针强转时注意，agingCmd_swOpreat对应单字节最低位bit0,依此类推
	u8 agingCmd_swOpreat:1; //时效_开关状态操作 -bit0
	u8 agingCmd_devLock:1; //时效_设备锁设置 -bit1
	u8 agingCmd_delaySetOpreat:1; //时效_延时设置 -bit2
	u8 agingCmd_greenModeSetOpreat:1; //时效_绿色模式设置 -bit3
	u8 agingCmd_timerSetOpreat:1; //时效_定时设置 -bit4
	u8 agingCmd_nightModeSetOpreat:1; //时效_夜间模式设置 -bit5
	u8 agingCmd_bkLightSetOpreat:1; //时效_背光灯设置 -bit6
	u8 agingCmd_devResetOpreat:1; //时效_开关恢复出厂操作 -bit7
	
	u8 agingCmd_horsingLight:1; //时效_跑马灯设置 -bit0
	u8 statusRef_bitReserve:7; //时效_bit保留 -bit1...bit7
	
	u8 agingCmd_byteReserve[4];	//5字节占位保留
	
}stt_agingDataSet_bitHold; //standard_length = 6Bytes

typedef struct swDevStatus_reference{

	u8 statusRef_swStatus:3; //状态_设备开关状态 -bit0...bit2
	u8 statusRef_reserve:2; //状态_reserve -bit3...bit4
	u8 statusRef_swPush:3; //状态_推送占位 -bit5...bit7
	
	u8 statusRef_timer:1; //状态_定时器运行 -bit0
	u8 statusRef_devLock:1; //状态_设备锁 -bit1
	u8 statusRef_delay:1; //状态_延时运行 -bit2
	u8 statusRef_greenMode:1; //状态_绿色模式运行 -bit3
	u8 statusRef_nightMode:1; //状态_夜间模式运行 -bit4
	u8 statusRef_horsingLight:1; //状态_跑马灯运行 -bit5
	u8 statusRef_bitReserve:2;  //状态__reserve -bit6...bit7
	
	u8 statusRef_byteReserve[2];   //状态__reserve -bytes2...3
	
}stt_swDevStatusReference_bitHold; //standard_length = 4Bytes

typedef struct dataPonit{

	stt_agingDataSet_bitHold 			devAgingOpreat_agingReference; //时效操作占位, 6Bytes
	stt_swDevStatusReference_bitHold	devStatus_Reference; //设备状态占位, 4Bytes 			
	u8 						 			devData_timer[24]; //定时器数据, 24Bytes
	u8									devData_delayer; //延时数据, 1Bytes
	u8									devData_delayUpStatus; //延时到达时,开关响应状态 1Bytes
	u8									devData_greenMode; //绿色模式数据, 1Bytes
	u8									devData_nightMode[6]; //夜间模式数据, 6Bytes
	u8									devData_bkLight[2]; //背光灯颜色, 2Bytes
	u8									devData_devReset; //开关复位数据, 1Bytes
	
}stt_devOpreatDataPonit; //standard_length = 46Bytes
/*=======================↑↑↑定时询访机制专用数据结构↑↑↑=============================*/

extern const u8 debugLogOut_targetMAC[5];
extern const u8 serverRemote_IP_Lanbon[4];

extern u8 SWITCH_TYPE;
extern u8 DEV_actReserve;

extern stt_scenarioOprateDats scenarioOprateDats;
extern u8 CTRLEATHER_PORT[USRCLUSTERNUM_CTRLEACHOTHER];

extern bool deviceLock_flag;

extern u8 MACSTA_ID[6];
extern u8 MACAP_ID[6];
extern u8 MACDST_ID[6];

extern u8 COLONY_DATAMANAGE_CTRLEATHER[CTRLEATHER_PORT_NUMTAIL];
extern stt_scenarioOprateDats COLONY_DATAMANAGE_SCENE;

void smartconfig_done_tp(sc_status status, void *pdata);

void portCtrlEachOther_Reales(void);
void devMAC_Reales(void);
void devLockIF_Reales(void);

u8 switchTypeReserve_GET(void);

void printf_datsHtoA(const u8 *TipsHead, u8 *dats , u8 datsLen);
stt_usrDats_privateSave *devParam_flashDataRead(void);	//谨记读取完毕后释放内存
void devParam_flashDataSave(devDatsSave_Obj dats_obj, stt_usrDats_privateSave datsSave_Temp);
void devData_recoverFactory(void);
void devFactoryRecord_Opreat(void);

#endif

