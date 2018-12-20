#include "datsProcess_socketsNetwork.h"

#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "datsProcess_udpLocal_A.h"
#include "datsProcess_udpRemote_B.h"
#include "datsProcess_tcpRemote_A.h"
#include "datsProcess_tcpRemote_B.h"

#include "datsProcess_uartZigbee.h"

#include "timer_Activing.h"
#include "hwPeripherial_Actuator.h"
#include "usrParsingMethod.h"
#include "datsManage.h"
#include "usrInterface_Tips.h"

extern u8 	swTim_onShoot_FLAG; 
extern bool ifTim_sw_running_FLAG; 

extern u8 tipsInsert_swLedBKG_ON;
extern u8 tipsInsert_swLedBKG_OFF;

bool nwkInternetOnline_IF = false;	//internet网络在线标志

xQueueHandle xMsgQ_Socket2Zigb;
xQueueHandle xMsgQ_datsFromSocketPort;

u8 heartbeat_Period = (u8)(PERIOD_HEARTBEAT_ASR / timer_heartBeatKeep_Period);	//周期性数据传输间隔定义
u8 heartbeat_Cnt = 0;	//心跳包周期计数

u8 internetRemoteServer_portSwitchPeriod = INTERNET_SERVERPORT_SWITCHPERIOD; //远端服务器端口切换周期（网络正常情况下每收到一次心跳回复就赋值刷新，否则 既定x个 周期内未收到心跳回复，即周期为0，就切换端口）

stt_devOpreatDataPonit dev_currentDataPoint = {0}; //周期询访数据点
stt_agingDataSet_bitHold dev_agingCmd_rcvPassive = {0}; //周期询访时效占位指令缓存 --被动接收
stt_agingDataSet_bitHold dev_agingCmd_sndInitative = {0}; //周期询访时效占位指令缓存 --主动接收
u8 dtModeKeepAcess_currentCmd = DTMODEKEEPACESS_FRAMECMD_ASR; //当前传输模式，主动还是被动指示

const u8 test_Dats1[14] = { //调试数据

	0xAA, 0x0E, 0x23, 0x00, 0x20, 0x15, 0x07, 0x12, 0x36, 0x00, 0x00, 0x01, 0x01, 0x00
};

const u8 test_Dats2[14] = {

	0xAA, 0x0E, 0x22, 0x00, 0x20, 0x15, 0x07, 0x12, 0x36, 0xFF, 0xFF, 0x00, 0x00, 0x00 
};

LOCAL xTaskHandle pxTaskHandle_threadNWK;

LOCAL internet_connFLG = false;	//STA网络在线标志

LOCAL os_timer_t timer_heartBeatKeep;

LOCAL void timer_heartBeatKeep_funCB(void *para);

/*---------------------------------------------------------------------------------------------*/

/*********************网络数据自定义校验*****************************/
u8 ICACHE_FLASH_ATTR
frame_Check(unsigned char frame_temp[], u8 check_num){
  
	u8 loop 		= 0;
	u8 val_Check 	= 0;
	
	for(loop = 0; loop < check_num; loop ++){
	
		val_Check += frame_temp[loop];
	}
	
	val_Check  = ~val_Check;
	val_Check ^= 0xa7;
	
	return val_Check;
}

LOCAL STATUS ICACHE_FLASH_ATTR
appMsgQueueCreat_S2Z(void){

	xMsgQ_Socket2Zigb = xQueueCreate(30, sizeof(stt_threadDatsPass));
	if(0 == xMsgQ_Socket2Zigb)return OK;
	else return FAIL;
}

LOCAL STATUS ICACHE_FLASH_ATTR
appMsgQueueCreat_datsFromSocket(void){

	xMsgQ_datsFromSocketPort = xQueueCreate(30, sizeof(stt_socketDats));
	if(0 == xMsgQ_datsFromSocketPort)return OK;
	else return FAIL;
}

/*本函数在socket数据发送前最后调用*///提前调用将影响校验码计算
LOCAL u8 ICACHE_FLASH_ATTR
dtasTX_loadBasic_CUSTOM(socketDataTrans_obj datsObj,
							   u8 dats_Tx[45],
							   u8 frame_Type,
							   u8 frame_CMD,
							   bool ifSpecial_CMD){	
						  
	switch(datsObj){
	
		/*服务器数据包*///45字节
		case DATATRANS_objFLAG_REMOTE:{
			
			u8 datsTemp[32] = {0};
		
			dats_Tx[0] 	= FRAME_HEAD_SERVER;
			
			memcpy(&datsTemp[0], &dats_Tx[1], 32);
			memcpy(&dats_Tx[13], &datsTemp[0], 32);	//帧头向后拉开，空出12字节
			memcpy(&dats_Tx[1],  &MACDST_ID[0], 6);	//远端MAC填充
			memcpy(&dats_Tx[8],  &MACSTA_ID[1], 5);	/*dats_Tx[7] 暂填0*///源MAC填充
			
			dats_Tx[1 + 12] = dataTransLength_objREMOTE;
			dats_Tx[2 + 12] = frame_Type;
			dats_Tx[3 + 12] = frame_CMD;
			
			if(!ifSpecial_CMD)dats_Tx[10 + 12] = SWITCH_TYPE;	//开关类型填充
			
			memcpy(&dats_Tx[5 + 12], &MACSTA_ID[1], 5);	//本地MAC填充
								  
			dats_Tx[4 + 12] = frame_Check(&dats_Tx[5 + 12], 28);	

			return dataTransLength_objREMOTE;
			
		}break;
		
		/*局域网数据包*///33字节
		case DATATRANS_objFLAG_LOCAL:{
		
			dats_Tx[0] 	= FRAME_HEAD_MOBILE;
			
			dats_Tx[1] 	= dataTransLength_objLOCAL;
			dats_Tx[2] 	= frame_Type;
			dats_Tx[3] 	= frame_CMD;
			
			if(!ifSpecial_CMD)dats_Tx[10] = SWITCH_TYPE;	//开关类型填充
			
			memcpy(&dats_Tx[5], &MACSTA_ID[1], 5);	//本地MAC填充
								  
			dats_Tx[4] 	= frame_Check(&dats_Tx[5], 28);

			return dataTransLength_objLOCAL;
			
		}break;
		
		default:break;
	}	
}

LOCAL STATUS ICACHE_FLASH_ATTR
sockets_datsSend(socket_OBJ sObj, u8 dats[], u16 datsLen){

	if(internet_connFLG || (SOFTAP_MODE == wifi_get_opmode())){

		switch(sObj){
		
			case Obj_udpLocal_A:	UDPlocalA_datsSend(dats, datsLen);return OK;
			case Obj_udpRemote_B:	UDPremoteB_datsSend(dats, datsLen);return OK;
			case Obj_tcpRemote_A:	return TCPremoteA_datsSend(dats, datsLen);
			case Obj_tcpRemote_B:	return TCPremoteB_datsSend(dats, datsLen);
			default:return FAIL;
		}

		vTaskDelay(4); //socket通畅通信间隔 40ms
	}
	else{

		return FAIL;
	}
}

void ICACHE_FLASH_ATTR
timer_heartBeat_Pause(void){

	os_timer_disarm(&timer_heartBeatKeep);
}

void ICACHE_FLASH_ATTR
timer_heartBeat_Recovery(void){

	os_timer_setfn(&timer_heartBeatKeep, (os_timer_func_t *)timer_heartBeatKeep_funCB, NULL);
	os_timer_arm(&timer_heartBeatKeep, timer_heartBeatKeep_Period, true);
}

LOCAL void ICACHE_FLASH_ATTR
timer_heartBeatKeep_funCB(void *para){

	struct ip_info ipConfig_info;

	LOCAL bool heartbeat_oeFLG = true;	//奇偶包标志

	u8 frameTx_temp[dataHeartBeatLength_objSERVER] = {0};
	u8 frameTx_dataLen = 0;

	stt_usrDats_privateSave *datsRead_Temp;

	wifi_get_ip_info(STATION_IF, &ipConfig_info);
	(wifi_station_get_connect_status() == STATION_GOT_IP && ipConfig_info.ip.addr != 0)?(internet_connFLG = true):(internet_connFLG = false);

	if(internet_connFLG){

		if(heartbeat_Cnt < heartbeat_Period)heartbeat_Cnt ++;
		else{

			heartbeat_Cnt = 0;
			
#if(ZIGB_DATATRANS_WORKMODE == DATATRANS_WORKMODE_HEARTBEAT)

			heartbeat_oeFLG = !heartbeat_oeFLG;

			frameTx_temp[0] = FRAME_HEAD_HEARTB;				
			frameTx_temp[1] = dataHeartBeatLength_objSERVER;
			memcpy(&frameTx_temp[4], &MACSTA_ID[1], 5);

			if(heartbeat_oeFLG){	//奇数包

				frameTx_temp[2] = FRAME_HEARTBEAT_cmdOdd;

				frameTx_temp[9] 	= 0; 
				frameTx_temp[10] 	= 0; 
				frameTx_temp[11] 	= 0; 
				frameTx_temp[12] 	= 0; 
				frameTx_temp[13] 	= 0; 
				
			}else{					//偶数包

				frameTx_temp[2] = FRAME_HEARTBEAT_cmdEven;

				frameTx_temp[9] 	= 0; 
				frameTx_temp[10] 	= 0; 
				frameTx_temp[11] 	= 0; 
				frameTx_temp[12] 	= 0; 
				frameTx_temp[13] 	= 0; 
			}

			frameTx_dataLen = dataHeartBeatLength_objSERVER;
			
#elif(ZIGB_DATATRANS_WORKMODE == DATATRANS_WORKMODE_KEEPACESS)

			datsRead_Temp = devParam_flashDataRead();

			//状态填装 -实时值
			dev_currentDataPoint.devStatus_Reference.statusRef_swStatus 	= status_actuatorRelay; //周期询访本地数据点 开关状态更新
			if(devActionPush_IF.push_IF){ //推送数据加载

				devActionPush_IF.push_IF = 0;
				dev_currentDataPoint.devStatus_Reference.statusRef_swPush = 0;
				dev_currentDataPoint.devStatus_Reference.statusRef_swPush |= devActionPush_IF.dats_Push;
			}
			dev_currentDataPoint.devStatus_Reference.statusRef_timer		= ifTim_sw_running_FLAG; //周期询访本地数据点 定时器状态更新
			dev_currentDataPoint.devStatus_Reference.statusRef_devLock		= deviceLock_flag;
			dev_currentDataPoint.devStatus_Reference.statusRef_delay		= (ifDelay_sw_running_FLAG & 0x02) >> 1;
			dev_currentDataPoint.devStatus_Reference.statusRef_greenMode	= (ifDelay_sw_running_FLAG & 0x01) >> 0;
			dev_currentDataPoint.devStatus_Reference.statusRef_nightMode	= ifNightMode_sw_running_FLAG;
			dev_currentDataPoint.devStatus_Reference.statusRef_horsingLight	= ifHorsingLight_running_FLAG;

			//属性值填装 -实时值
			memcpy(&dev_currentDataPoint.devData_timer, datsRead_Temp->swTimer_Tab, 3 * 8);
			{ //普通定时数据一次性周占位回复特殊处理

				u8 loop = 0;
				for(loop = 0; loop < TIMEER_TABLENGTH; loop ++){

					if(swTim_onShoot_FLAG & (1 << loop))dev_currentDataPoint.devData_timer[3 * loop] = 0x80; //针对一次性定时查询回复数据，周占位清空恢复
				}
			}
			dev_currentDataPoint.devData_delayer		= delayPeriod_onoff - (delayCnt_onoff / 60);
			dev_currentDataPoint.devData_delayUpStatus	= delayUp_act;
			dev_currentDataPoint.devData_greenMode		= delayPeriod_closeLoop;
			{ //夜间模式数据特殊转换

				((datsRead_Temp->devNightModeTimer_Tab[0] & 0x7F) == 0x7F)?(dev_currentDataPoint.devData_nightMode[0] = 1):(dev_currentDataPoint.devData_nightMode[0] = 0);
				((datsRead_Temp->devNightModeTimer_Tab[0] & 0x80) == 0x80)?(dev_currentDataPoint.devData_nightMode[1] = 1):(dev_currentDataPoint.devData_nightMode[1] = 0);
				dev_currentDataPoint.devData_nightMode[2] = datsRead_Temp->devNightModeTimer_Tab[1];
				dev_currentDataPoint.devData_nightMode[3] = datsRead_Temp->devNightModeTimer_Tab[2];
				dev_currentDataPoint.devData_nightMode[4] = datsRead_Temp->devNightModeTimer_Tab[4];
				dev_currentDataPoint.devData_nightMode[5] = datsRead_Temp->devNightModeTimer_Tab[5];

//				printf_datsHtoA("btMode data upload:", dev_currentDataPoint.devData_nightMode, 6);
			}
			memcpy(&dev_currentDataPoint.devData_bkLight[0], &(datsRead_Temp->bkColor_swON), 1);
			memcpy(&dev_currentDataPoint.devData_bkLight[1], &(datsRead_Temp->bkColor_swOFF), 1);
			dev_currentDataPoint.devData_devReset = 0;
			memcpy(dev_currentDataPoint.devData_switchBitBind, CTRLEATHER_PORT, USRCLUSTERNUM_CTRLEACHOTHER);

			//时效操作占位指令填装
			switch(dtModeKeepAcess_currentCmd){

				case DTMODEKEEPACESS_FRAMECMD_ASR:{
				
					memcpy(&dev_currentDataPoint.devAgingOpreat_agingReference, &dev_agingCmd_rcvPassive, sizeof(stt_agingDataSet_bitHold));
				
				}break;
					
				case DTMODEKEEPACESS_FRAMECMD_PST:{
				
					memcpy(&dev_currentDataPoint.devAgingOpreat_agingReference, &dev_agingCmd_sndInitative, sizeof(stt_agingDataSet_bitHold));
				
				}break;
					
				default:{}break;
			}

			//数据帧总数据最后填装
			frameTx_dataLen						= 0;
			frameTx_temp[frameTx_dataLen ++]	= DTMODEKEEPACESS_FRAMEHEAD_ONLINE; //帧头
			frameTx_temp[frameTx_dataLen ++]	= 0;  //帧长暂填0，最后更新
			memcpy(&frameTx_temp[frameTx_dataLen], &MACSTA_ID[1], 5); //MAC
			frameTx_dataLen += 6; //空出1Byte MAC
			frameTx_temp[frameTx_dataLen ++]	= dtModeKeepAcess_currentCmd; //命令
			frameTx_temp[frameTx_dataLen ++]	= SWITCH_TYPE; //开关信息
			frameTx_temp[frameTx_dataLen ++]	= (u8)(nwkZigb_currentPANID >> 8); //PANID填装
			frameTx_temp[frameTx_dataLen ++]	= (u8)(nwkZigb_currentPANID >> 0); 
			frameTx_temp[frameTx_dataLen ++]	= DEVICE_VERSION_NUM; //设备版本号填装
			frameTx_temp[frameTx_dataLen ++]	= sysTimeZone_H; //时区_时
			frameTx_temp[frameTx_dataLen ++]	= sysTimeZone_M; //时区_分
			memcpy(&frameTx_temp[frameTx_dataLen], &dev_currentDataPoint, sizeof(stt_devOpreatDataPonit)); //直接数据指针对齐，数据点直接数据帧待发缓存进行数据结构强怼
			frameTx_dataLen += sizeof(stt_devOpreatDataPonit);
			frameTx_dataLen ++;
			frameTx_temp[1] 					= frameTx_dataLen;
			frameTx_temp[frameTx_dataLen - 1] = frame_Check(&frameTx_temp[1], frameTx_dataLen - 2); //校验码最后算

			if(datsRead_Temp)os_free(datsRead_Temp);

#endif

			sockets_datsSend(Obj_udpRemote_B, frameTx_temp, frameTx_dataLen);
			
//			printf_datsHtoA("[Tips_threadNet]: hearBeat datsSend is:", heartBeat_Pack, dataHeartBeatLength_objSERVER);
//			(heartbeat_oeFLG)?(sockets_datsSend(Obj_udpRemote_B, (u8 *)test_Dats1, 14)):(sockets_datsSend(Obj_udpRemote_B, (u8 *)test_Dats2, 14));
		}
	}
	else{

		
	}
}

LOCAL void ICACHE_FLASH_ATTR
socketsDataTransProcess_task(void *pvParameters){

//	spi_flash_write(uint32 des_addr, uint32 * src_addr, uint32 size)

	stt_threadDatsPass 	mptr_S2Z;
	stt_threadDatsPass 	rptr_Z2S;
	stt_socketDats 		rptr_socketDats;
	portBASE_TYPE 		xMsgQ_rcvResult = pdFALSE;

#define socketsData_repeatTxLen	128
	u8 repeatTX_buff[socketsData_repeatTxLen] = {0};
	u8 repeatTX_Len = 0;

#define socketsData_datsTxLen	128
	u8 datsTX_buff[socketsData_datsTxLen] = {0};
	u8 datsTX_Len = 0;

	stt_usrDats_privateSave datsSave_Temp = {0};
	stt_usrDats_privateSave *datsRead_Temp;
		
	bool socketRespond_IF = false;
	bool specialCMD_IF = false;

	bool local_ftyRecover_standbyFLG = false; //恢复出厂设置预动作标志

	u8 loop = 0;

	for(;;){

/*>>>>>>sockets数据处理<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<*/
		xMsgQ_rcvResult = xQueueReceive(xMsgQ_datsFromSocketPort, (void *)&rptr_socketDats, 0);
		if(xMsgQ_rcvResult == pdTRUE){

			if(!rptr_socketDats.heartBeat_IF){	//常规数据处理<<---------------------------------------------------------------//

				os_printf("[Tips_NWK-SKdats]: receive msg with cmd: %02X !!!\n", rptr_socketDats.command);

				memset(repeatTX_buff, 0, socketsData_repeatTxLen * sizeof(u8));	//清缓存

				if((rptr_socketDats.dstObj == obj_toWIFI) || //数据是给网关的，则直接解析数据，远端数据已做缩减处理为和本地数据格式相同，直接解析数据即可
				   (rptr_socketDats.dstObj == obj_toALL)){ 

					switch(rptr_socketDats.command){
		
						case FRAME_MtoSCMD_cmdControl:{

							repeatTX_buff[11] = 0;
							repeatTX_buff[11] |= (rptr_socketDats.dats[11] & 0x07); //当前开关状态位填装<低三位>
							if( 	(rptr_socketDats.dats[11] & 0x01) != (status_actuatorRelay & 0x01))repeatTX_buff[11] |= 0x20; //更改值填装<高三位>第一位
							else if((rptr_socketDats.dats[11] & 0x02) != (status_actuatorRelay & 0x02))repeatTX_buff[11] |= 0x40; //更改值填装<高三位>第二位
							else if((rptr_socketDats.dats[11] & 0x04) != (status_actuatorRelay & 0x04))repeatTX_buff[11] |= 0x80; //更改值填装<高三位>第三位

							swCommand_fromUsr.objRelay = rptr_socketDats.dats[11];
							swCommand_fromUsr.actMethod = relay_OnOff;

							os_printf("ctrl data: %02X, pushData: %02X.\n", rptr_socketDats.dats[11], repeatTX_buff[11]);
		
							specialCMD_IF		= false;
							socketRespond_IF	= true;
		
						}break;
		
						case FRAME_MtoSCMD_cmdConfigSearch:{

							sysTimeZone_H = rptr_socketDats.dats[12];
							sysTimeZone_M = rptr_socketDats.dats[13];

							datsSave_Temp.timeZone_H = rptr_socketDats.dats[12];
							datsSave_Temp.timeZone_M = rptr_socketDats.dats[13];
							devParam_flashDataSave(obj_timeZone_H, datsSave_Temp);
							devParam_flashDataSave(obj_timeZone_M, datsSave_Temp);

							mySocketUDPremote_B_serverChange(&rptr_socketDats.dats[6]);

							repeatTX_buff[11] = status_actuatorRelay;
							repeatTX_buff[12] = DEVICE_VERSION_NUM; 

							repeatTX_buff[14] = (u8)(nwkZigb_currentPANID >> 8);
							repeatTX_buff[15] = (u8)(nwkZigb_currentPANID >> 0);

							repeatTX_buff[20] = 0x00;  //协调器网络短地址标记 <调试专用>
							repeatTX_buff[21] = 0x00;  //协调器网络短地址标记 <调试专用>

							specialCMD_IF		= false;
							socketRespond_IF	= !deviceLock_flag; //上锁不应答
						
						}break;
		
						case FRAME_MtoSCMD_cmdQuery:{

							repeatTX_buff[11] = status_actuatorRelay;
							repeatTX_buff[12] = 0;
							(deviceLock_flag)?(repeatTX_buff[12] |= 0x01):(repeatTX_buff[12] &= ~0x01);
							(ifNightMode_sw_running_FLAG)?(repeatTX_buff[12] |= 0x02):(repeatTX_buff[12] &= ~0x02);

							specialCMD_IF		= false;
							socketRespond_IF	= true;

						}break;
		
						case FRAME_MtoSCMD_cmdInterface:{

							
							
						}break;
		
						case FRAME_MtoSCMD_cmdReset:{

							
							
						}break;
		
						case FRAME_MtoSCMD_cmdLockON:{

							datsSave_Temp.dev_lockIF = 1;
							devParam_flashDataSave(obj_dev_lockIF, datsSave_Temp);
						
							deviceLock_flag = true;
							
						}break;
		
						case FRAME_MtoSCMD_cmdLockOFF:{
							
							datsSave_Temp.dev_lockIF = 0;
							devParam_flashDataSave(obj_dev_lockIF, datsSave_Temp);

							deviceLock_flag = false;
							
						}break;
		
						case FRAME_MtoSCMD_cmdswTimQuery:{

							//分类回复
							switch(rptr_socketDats.dats[13]){

								/*普通时刻定时*/
								case 0: /*上位机查定时的时候这个字节给0*/
								case cmdConfigTim_normalSwConfig:{
								
									u8 loop = 0;
								
									//数据响应及回复
									datsRead_Temp = devParam_flashDataRead();
									memcpy(&repeatTX_buff[14], datsRead_Temp->swTimer_Tab, 4 * 3);
									
									//回复数据二次处理（针对一次性定时）
									for(loop = 0; loop < 4; loop ++){
									
										if(swTim_onShoot_FLAG & (1 << loop)){
											
											repeatTX_buff[14 + loop * 3] &= 0x80;
										}
									}
											
									specialCMD_IF = true; //特殊占位指令

									if(datsRead_Temp)os_free(datsRead_Temp);
									
								}break;

								/*普通延时*/
								case cmdConfigTim_onoffDelaySwConfig:{
								
									if(!delayCnt_onoff)repeatTX_buff[14] = 0;
									else repeatTX_buff[14] = delayPeriod_onoff - (u8)(delayCnt_onoff / 60);
									repeatTX_buff[15] = delayUp_act;

								}break;

								/*普通绿色模式（循环关闭）*/
								case cmdConfigTim_closeLoopSwConfig:{
								
									repeatTX_buff[14] = delayPeriod_closeLoop;
									
								}break;

								/*设备夜间模式*/
								case cmdConfigTim_nightModeSwConfig:{

									datsRead_Temp = devParam_flashDataRead();

									memcpy(&repeatTX_buff[14], datsRead_Temp->devNightModeTimer_Tab, 3 * 2);
									(ifNightMode_sw_running_FLAG)?(repeatTX_buff[12] |= 0x02):(repeatTX_buff[12] &= ~0x02);

									if(datsRead_Temp)os_free(datsRead_Temp);
								
								}break;

								default:break;
							}

							if(datsRead_Temp)os_free(datsRead_Temp);

							repeatTX_buff[13] = rptr_socketDats.dats[13]; //子命令回复

							socketRespond_IF = true;
							
						}break;
		
						case FRAME_MtoSCMD_cmdConfigAP:{
		
							
						
						}break;
		
						case FRAME_MtoSCMD_cmdBeepsON:{ //蜂鸣器开命令代表夜间模式关闭

							datsRead_Temp = devParam_flashDataRead();
							
							datsSave_Temp.devNightModeTimer_Tab[0]	= datsRead_Temp->devNightModeTimer_Tab[0]; //存储数据更新
							datsSave_Temp.devNightModeTimer_Tab[0] &= ~0x7F; //全天夜间失能
							devParam_flashDataSave(obj_devNightModeTimer_Tab, datsSave_Temp);

							datsTiming_getRealse(); //本地运行数据更新
							
							if(datsRead_Temp)os_free(datsRead_Temp);

							socketRespond_IF = true;

						}break;
		
						case FRAME_MtoSCMD_cmdBeepsOFF:{ //蜂鸣器关命令代表夜间模式开启

							datsRead_Temp = devParam_flashDataRead();
							
							datsSave_Temp.devNightModeTimer_Tab[0]	= datsRead_Temp->devNightModeTimer_Tab[0]; //存储数据更新
							datsSave_Temp.devNightModeTimer_Tab[0] |= 0x7F; //全天夜间使能
							devParam_flashDataSave(obj_devNightModeTimer_Tab, datsSave_Temp);

							datsTiming_getRealse(); //本地运行数据更新
							
							if(datsRead_Temp)os_free(datsRead_Temp);

							socketRespond_IF = true;
							
						}break;
		
						case FRAME_MtoSCMD_cmdftRecoverRQ:{
		
							socketRespond_IF = true; //支持恢复·
						
						}break;
		
						case FRAME_MtoSCMD_cmdRecoverFactory:{
		
							devData_recoverFactory(); //flash清空

							//关键缓存清空
							nwkZigb_currentPANID = 0; //PANID
	
							socketRespond_IF = true;
						
						}break;
		
						case FRAME_MtoSCMD_cmdCfg_swTim:{
		
							switch(rptr_socketDats.dats[13]){ //分类处理

								/*普通时刻定时*/
								case cmdConfigTim_normalSwConfig:{	
									
									for(loop = 0; loop < 4; loop ++){
									
										if(rptr_socketDats.dats[14 + loop * 3] == 0x80){ /*一次性定时判断*///周占位为空，而定时器使能被打开，说明是一次性
										
											swTim_onShoot_FLAG 	|= (1 << loop); //一次性定时标志开启
											rptr_socketDats.dats[14 + loop * 3] |= (1 << (systemTime_current.time_Week - 1)); //强制进行周占位，执行完毕后清除
											
										}else{

											swTim_onShoot_FLAG	&= ~(1 << loop); //一次标志关闭
										}
									}

									memcpy(datsSave_Temp.swTimer_Tab, &(rptr_socketDats.dats[14]), 4 * 3);
									devParam_flashDataSave(obj_swTimer_Tab, datsSave_Temp); //存储数据更新
									
									datsTiming_getRealse(); //本地运行数据更新
				
								}break;

								/*普通延时*/
								case cmdConfigTim_onoffDelaySwConfig:{	
								
									if(rptr_socketDats.dats[14]){
									
										ifDelay_sw_running_FLAG |= (1 << 1);
										delayPeriod_onoff 		= rptr_socketDats.dats[14];
										
										delayUp_act		  		= rptr_socketDats.dats[15];
										
										delayCnt_onoff			= 0;
										
									}else{
									
										ifDelay_sw_running_FLAG &= ~(1 << 1);
										delayPeriod_onoff 		= 0;
										delayCnt_onoff			= 0;
									}
									
								}break;

								/*普通绿色模式（循环关闭）*/
								case cmdConfigTim_closeLoopSwConfig:{	
								
									if(rptr_socketDats.dats[14]){
									
										ifDelay_sw_running_FLAG |= (1 << 0);
										delayPeriod_closeLoop	= rptr_socketDats.dats[14];
										delayCnt_closeLoop		= 0;
										
									}else{
									
										ifDelay_sw_running_FLAG &= ~(1 << 0);
										delayPeriod_closeLoop	= 0;
										delayCnt_closeLoop		= 0;
									}

									datsSave_Temp.swDelay_flg = ifDelay_sw_running_FLAG;
									datsSave_Temp.swDelay_periodCloseLoop = delayPeriod_closeLoop;
									devParam_flashDataSave(obj_swDelay_flg, datsSave_Temp);
									devParam_flashDataSave(obj_swDelay_periodCloseLoop, datsSave_Temp);
									
								}break;			

								/*设备夜间模式*/
								case cmdConfigTim_nightModeSwConfig:{

									memcpy(datsSave_Temp.devNightModeTimer_Tab, &rptr_socketDats.dats[14], 3 * 2); //存储数据更新
									devParam_flashDataSave(obj_devNightModeTimer_Tab, datsSave_Temp);

									datsTiming_getRealse(); //本地运行数据更新
								
								}break;
								
								default:break;
							}

							specialCMD_IF		= false;
							socketRespond_IF	= true;

						}break;

						case FRAME_MtoZIGBCMD_cmdCfg_PANID:{

							bool PANID_changeRightNow = false;

							os_printf("PANID_GET: 0x%02X%02X.\n", rptr_socketDats.dats[14], rptr_socketDats.dats[15]);
							if(rptr_socketDats.dats[16] == 0x01){ //强制设置

								PANID_changeRightNow = true;
							}
							else
							if(rptr_socketDats.dats[16] == 0x00){ //非强制设置

								if(nwkZigb_currentPANID > 0 && nwkZigb_currentPANID <= ZIGB_PANID_MAXVAL){ //合法

									PANID_changeRightNow = false;

								}else{ //非法

									PANID_changeRightNow = true;
								}
							}

							if(PANID_changeRightNow){ //立即更改并创建对应PANID网络

								datsSave_Temp.panID_default = 0; //缓存清空
								datsSave_Temp.panID_default |= ((u16)rptr_socketDats.dats[14] << 8);
								datsSave_Temp.panID_default |= ((u16)rptr_socketDats.dats[15] << 0);
								devParam_flashDataSave(obj_panID_default, datsSave_Temp); //存储数据更新
								
								repeatTX_buff[14] = rptr_socketDats.dats[14];
								repeatTX_buff[15] = rptr_socketDats.dats[15];

								enum_zigbFunMsg mptr_zigbFunRm = msgFun_panidRealesNwkCreat;
								xQueueSend(xMsgQ_zigbFunRemind, (void *)&mptr_zigbFunRm, 0);
								
							}else{

								repeatTX_buff[14] = (u8)(nwkZigb_currentPANID >> 8);
								repeatTX_buff[15] = (u8)(nwkZigb_currentPANID >> 0);
							}
							
							socketRespond_IF = true;
						
						}break;

						case FRAME_MtoZIGBCMD_cmdCfg_ctrlEachO:{

							u8 loop = 0;
							u8 effective_oprate = rptr_socketDats.dats[12]; //有效数据操作占位获取
							
							for(loop = 0; loop < USRCLUSTERNUM_CTRLEACHOTHER; loop ++){
							
								if((effective_oprate >> loop) & 0x01){ //有效数据判断
								
									CTRLEATHER_PORT[loop] = rptr_socketDats.dats[14 + loop]; //运行数据加载
									datsSave_Temp.port_ctrlEachother[loop] =  CTRLEATHER_PORT[loop]; //存储数据更新
								}
							}

							devParam_flashDataSave(obj_port_ctrlEachother, datsSave_Temp);

							enum_zigbFunMsg mptr_zigbFunRm = msgFun_portCtrlEachoRegister; //即刻注册互控端口
							xQueueSend(xMsgQ_zigbFunRemind, (void *)&mptr_zigbFunRm, 0);

							socketRespond_IF = true;

						}break;

						case FRAME_MtoZIGBCMD_cmdQue_ctrlEachO:{

							memcpy(&repeatTX_buff[14], CTRLEATHER_PORT, USRCLUSTERNUM_CTRLEACHOTHER);

							socketRespond_IF = true;

						}break;

						case FRAME_MtoZIGBCMD_cmdCfg_ledBackSet:{

							tipsInsert_swLedBKG_ON 	= rptr_socketDats.dats[14]; //运行数据加载
							tipsInsert_swLedBKG_OFF = rptr_socketDats.dats[15];

							datsSave_Temp.bkColor_swON 	= tipsInsert_swLedBKG_ON; //存储数据更新
							datsSave_Temp.bkColor_swOFF = tipsInsert_swLedBKG_OFF;
							devParam_flashDataSave(obj_bkColor_swON, datsSave_Temp);
							devParam_flashDataSave(obj_bkColor_swOFF, datsSave_Temp);

							socketRespond_IF = true;

						}break;

						case FRAME_MtoZIGBCMD_cmdQue_ledBackSet:{

							repeatTX_buff[14] = tipsInsert_swLedBKG_ON;
							repeatTX_buff[15] = tipsInsert_swLedBKG_OFF;
								
							socketRespond_IF = true;
						
						}break;

						case FRAME_MtoZIGBCMD_cmdCfg_scenarioCtl:{

							if(scenarioOprateDats.scenarioCtrlOprate_IF){

								u8 loop = 0;

								for(loop = 0; loop < scenarioOprateDats.devNode_num; loop ++){ //查找网关自身MAC是否在内

									if(!memcmp(&MACSTA_ID[1], scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC, 5)){ //本身存在于场景就响应动作

										swCommand_fromUsr.objRelay = scenarioOprateDats.scenarioOprate_Unit[loop].devNode_opStatus;
										swCommand_fromUsr.actMethod = relay_OnOff;

										memset(&scenarioOprateDats.scenarioOprate_Unit[loop], 0, 1 * sizeof(scenarioOprateUnit_Attr)); //同时从场景控制表中剔除网关自身
										memcpy(&scenarioOprateDats.scenarioOprate_Unit[loop], &scenarioOprateDats.scenarioOprate_Unit[loop + 1], (zigB_ScenarioCtrlDataTransASY_QbuffLen - loop - 1) * sizeof(scenarioOprateUnit_Attr));
										scenarioOprateDats.devNode_num --;

										break;
									}
								}

								enum_zigbFunMsg mptr_zigbFunRm = msgFun_scenarioCrtl; //消息即刻转发至zigb线程进行场景集群控制下发
								xQueueSend(xMsgQ_zigbFunRemind, (void *)&mptr_zigbFunRm, 0);
								
//								for(loop = 0; loop < scenarioOprateDats.devNode_num; loop ++){ //log打印

//									os_printf("devNode<%02d>MAC:[%02X %02X %02X %02X %02X] --opStatus:{%02X}.\n",
//										      loop,
//										      scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC[0],
//										      scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC[1],
//										      scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC[2],
//										      scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC[3],
//										      scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC[4],
//										      scenarioOprateDats.scenarioOprate_Unit[loop].devNode_opStatus);
//								}
							}

						}break;

						default:{
		
							
						
						}break;
					}
		
					if(socketRespond_IF){
		
						u8 dats_Log[DEBUG_LOGLEN] = {0};
						u8 loop;
		
						socketDataTrans_obj dats_obj = DATATRANS_objFLAG_LOCAL;
		
						if(rptr_socketDats.portObj == Obj_udpLocal_A){	//数据传输对象判断
		
							dats_obj = DATATRANS_objFLAG_LOCAL;
							
						}else{
		
							dats_obj = DATATRANS_objFLAG_REMOTE;
						}
		
						repeatTX_Len = dtasTX_loadBasic_CUSTOM( dats_obj,		//发送前最后填装
																repeatTX_buff,				
																FRAME_TYPE_StoM_RCVsuccess,
																rptr_socketDats.command,
																specialCMD_IF);
		
						sockets_datsSend(rptr_socketDats.portObj, repeatTX_buff, repeatTX_Len);

//						printf_datsHtoA("[Tips_threadNet]: respondDats:", repeatTX_buff, repeatTX_Len);
					}

				}

				if((rptr_socketDats.dstObj == obj_toZigB) || //数据是给zigbee的，转发处理
				   (rptr_socketDats.dstObj == obj_toALL)){

					memset(&mptr_S2Z, 0, sizeof(stt_threadDatsPass));

					mptr_S2Z.msgType = conventional;
					memcpy(mptr_S2Z.dats.dats_conv.dats, rptr_socketDats.dats, rptr_socketDats.datsLen);
					mptr_S2Z.dats.dats_conv.datsLen = rptr_socketDats.datsLen;
					switch(rptr_socketDats.portObj){

						case Obj_udpLocal_A:{ //本地类型数据处理

							memcpy(mptr_S2Z.dats.dats_conv.macAddr, &rptr_socketDats.dats[5], DEV_MAC_LEN);
							mptr_S2Z.dats.dats_conv.devType = rptr_socketDats.dats[10];
							mptr_S2Z.dats.dats_conv.dats[0] = ZIGB_FRAMEHEAD_CTRLLOCAL;
							mptr_S2Z.dats.dats_conv.datsFrom = datsFrom_ctrlLocal;
							   
						}break;

						case Obj_udpRemote_B:{ //远端服务器数据处理

							memcpy(mptr_S2Z.dats.dats_conv.macAddr, &rptr_socketDats.dats[17], DEV_MAC_LEN);
							mptr_S2Z.dats.dats_conv.devType = rptr_socketDats.dats[22];
							mptr_S2Z.dats.dats_conv.dats[0] = ZIGB_FRAMEHEAD_CTRLREMOTE;
							mptr_S2Z.dats.dats_conv.datsFrom = datsFrom_ctrlRemote;

//							os_printf("[Tips_NWK-SKdats]: mark: %02X %02X %02X %02X %02X.\n", 
//							mptr_S2Z.dats.dats_conv.macAddr[0],
//							mptr_S2Z.dats.dats_conv.macAddr[1],
//							mptr_S2Z.dats.dats_conv.macAddr[2],
//							mptr_S2Z.dats.dats_conv.macAddr[3],
//							mptr_S2Z.dats.dats_conv.macAddr[4]);

						}break;

						default:{

							mptr_S2Z.dats.dats_conv.dats[0] = ZIGB_FRAMEHEAD_CTRLREMOTE;
							mptr_S2Z.dats.dats_conv.datsFrom = datsFrom_ctrlRemote;

						}break;
					}

				   xQueueSend(xMsgQ_Socket2Zigb, (void *)&mptr_S2Z, 0);

				}

				/*标志复位*/
				socketRespond_IF = false;
				specialCMD_IF = false;

			}
			else{ //心跳包处理<<---------------------------------------------------------------//

				switch(rptr_socketDats.dstObj){

					case obj_toWIFI:{ //给网关主机的

						stt_usrDats_privateSave datsSave_Temp = {0};

						stt_agingDataSet_bitHold agingCmd_Temp = {0};
						stt_devOpreatDataPonit dev_dataPointTemp = {0};
						
						bool frameCodeCheck_PASS = false; //校验码检查通过标志
						bool frameMacCheck_PASS  = false; //MAC资质检查通过标志

						internetRemoteServer_portSwitchPeriod = INTERNET_SERVERPORT_SWITCHPERIOD; //远端服务器端口切换周期更新

						os_printf("[Tips_NWK-SKdats]: netGate_hb rvc-<mac:%02X %02X %02X %02X %02X>,<cmd:%02X>.\n", 
								  rptr_socketDats.dats[2],
								  rptr_socketDats.dats[3],
								  rptr_socketDats.dats[4],
								  rptr_socketDats.dats[5],
								  rptr_socketDats.dats[6],
								  rptr_socketDats.command);

						if(rptr_socketDats.dats[rptr_socketDats.dats[1] - 1] == frame_Check(&rptr_socketDats.dats[1], rptr_socketDats.dats[1] - 2))frameCodeCheck_PASS = true;
						if(!memcmp(&rptr_socketDats.dats[2], &MACSTA_ID[1], 5))frameMacCheck_PASS = 1;

						if(frameCodeCheck_PASS && frameMacCheck_PASS){ 

							memcpy(&dev_dataPointTemp, &rptr_socketDats.dats[15], sizeof(stt_devOpreatDataPonit));
							switch(rptr_socketDats.dats[8]){ //帧命令

								case DTMODEKEEPACESS_FRAMECMD_ASR:{

									/*时效命令判断*///进行时效动作后，清空时效操作位
									if(memcmp(&agingCmd_Temp, &dev_dataPointTemp.devAgingOpreat_agingReference, sizeof(stt_agingDataSet_bitHold))){

										heartbeat_Cnt = heartbeat_Period; //有时效控制，强行提前定时通讯，即时回码

//										printf_datsHtoA(">>>>>>>>agingCMD bytes:", &rptr_socketDats.dats[15], 6);

										beeps_usrActive(3, 25, 1);
										
										memcpy(&dev_agingCmd_rcvPassive, &dev_dataPointTemp.devAgingOpreat_agingReference, sizeof(stt_agingDataSet_bitHold)); //本地被动时效操作缓存同步更新，用于原位回发
										
										if(dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_swOpreat){ //开关状态操作，需要时效

											if((status_actuatorRelay & 0x07) != dev_dataPointTemp.devStatus_Reference.statusRef_swStatus){ 

												swCommand_fromUsr.objRelay = dev_dataPointTemp.devStatus_Reference.statusRef_swStatus;
												swCommand_fromUsr.actMethod = relay_OnOff;

												if((SWITCH_TYPE == SWITCH_TYPE_SWBIT1) || //多位开关才存在推送
												   (SWITCH_TYPE == SWITCH_TYPE_SWBIT2) ||
												   (SWITCH_TYPE == SWITCH_TYPE_SWBIT3)){

														devActionPush_IF.push_IF = 1; //推送使能
												}

												os_printf(">>>>>>>>relayStatus reales:%02X, currentVal:%02X.\n", (int)status_actuatorRelay & 0x07, (int)dev_dataPointTemp.devStatus_Reference.statusRef_swStatus);
											}
										}

										if(dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_delaySetOpreat){ //延时设置操作，需要时效

											if(dev_dataPointTemp.devData_delayer){ //延时时间大于0就是开启操作
											
												ifDelay_sw_running_FLAG |= (1 << 1); //延时标志更新，启动
												delayPeriod_onoff		= dev_dataPointTemp.devData_delayer; //延时时间
												delayUp_act 			= dev_dataPointTemp.devData_delayUpStatus; //延时到达时，开关响应状态
												delayCnt_onoff			= 0; //延时计数清零
												
											}else{ //否则为关闭操作

												ifDelay_sw_running_FLAG &= ~(1 << 0); //延时标志更新，关闭
												delayPeriod_onoff		= 0; 
												delayCnt_onoff			= 0; 
											}

											os_printf(">>>>>>>>delayPeriod:[%d], delayUpAct:[%02X].\n", (int)delayPeriod_onoff, (int)delayUp_act);
										}

										if(dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_devResetOpreat){ //恢复出厂重启操作，需要时效

											local_ftyRecover_standbyFLG = 1; //恢复出厂预动作标志置位，先做就绪态，等待时效标志清零后进行实际动作

											os_printf(">>>>>>>>factory recover standBy!.\n");
										}

										if(dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_devLock){  //设备上锁设置操作，需要时效

											if(dev_dataPointTemp.devStatus_Reference.statusRef_devLock){ //数据放在状态里
											
												datsSave_Temp.dev_lockIF = deviceLock_flag = 1; //运行缓存及本地存储更新
												
											}else{
											
												datsSave_Temp.dev_lockIF = deviceLock_flag = 0; //运行缓存及本地存储更新
											}
											devParam_flashDataSave(obj_dev_lockIF, datsSave_Temp); //本地存储动作执行

											os_printf(">>>>>>>>agingCmd devLock coming, lockIf:[%d].\n", (int)dev_dataPointTemp.devStatus_Reference.statusRef_devLock);
										}

										if(dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_timerSetOpreat){  //定时设置操作，需要时效

											for(loop = 0; loop < TIMEER_TABLENGTH; loop ++){ //运行缓存更新
											
												if(dev_dataPointTemp.devData_timer[loop * 3] == 0x80){	/*一次性定时判断*///周占位为空而定时器被打开，说明是一次性
												
													swTim_onShoot_FLAG	|= (1 << loop); //一次标志开启
													dev_dataPointTemp.devData_timer[loop * 3] |= (1 << (systemTime_current.time_Week - 1)); //强行进行当前周占位，当次执行完毕后清除
													
												}else{

													swTim_onShoot_FLAG	&= ~(1 << loop); //一次标志关闭
												}
											}
											memcpy(datsSave_Temp.swTimer_Tab, dev_dataPointTemp.devData_timer, 8 * 3); //本地存储更新
											devParam_flashDataSave(obj_swTimer_Tab, datsSave_Temp); //本地存储动作执行
											
											datsTiming_getRealse(); //本地运行数据更新

											os_printf(">>>>>>>>agingCmd timer coming, dataTab1:[%02X-%02X-%02X].\n", (int)dev_dataPointTemp.devData_timer[0],
																													 (int)dev_dataPointTemp.devData_timer[1],
																													 (int)dev_dataPointTemp.devData_timer[2]);
										}

										if(dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_greenModeSetOpreat){  //绿色模式设置操作，需要时效

											datsSave_Temp.swDelay_periodCloseLoop = dev_dataPointTemp.devData_greenMode; //本地存储更新
											(dev_dataPointTemp.devData_greenMode)?(ifDelay_sw_running_FLAG |= (1 << 0)):(ifDelay_sw_running_FLAG &= ~(1 << 0)); //运行缓存更新
											delayPeriod_closeLoop = dev_dataPointTemp.devData_greenMode;
											devParam_flashDataSave(obj_swDelay_periodCloseLoop, datsSave_Temp); //本地存储动作执行

											os_printf(">>>>>>>>agingCmd greenMode coming, timeSet:%d.\n", dev_dataPointTemp.devData_greenMode);
										}

										if(dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_nightModeSetOpreat){  //夜间模式设置操作，需要时效

											u8 dataTemp[3 * 2] = {0};
											
											(dev_dataPointTemp.devData_nightMode[0])?(dataTemp[0] |= 0x7f):(dataTemp[0] &= ~0x7f);
											(dev_dataPointTemp.devData_nightMode[1])?(dataTemp[0] |= 0x80):(dataTemp[0] &= ~0x80);
											dataTemp[1] = dev_dataPointTemp.devData_nightMode[2]; //下标2， 低5位， 时
											dataTemp[2] = dev_dataPointTemp.devData_nightMode[3]; //下标3， 全8位， 分
											dataTemp[4] = dev_dataPointTemp.devData_nightMode[4]; //下标4， 低5位， 时
											dataTemp[5] = dev_dataPointTemp.devData_nightMode[5]; //下标5， 全8位，	分
											
											memcpy(datsSave_Temp.devNightModeTimer_Tab, dataTemp, 3 * 2); //本地存储更新
											devParam_flashDataSave(obj_devNightModeTimer_Tab, datsSave_Temp); //本地存储动作执
											vTaskDelay(2);

											datsTiming_getRealse(); //本地运行数据更新

											os_printf(">>>>>>>>agingCmd nightMode coming.\n");

//											printf_datsHtoA("btMode data download:", dev_dataPointTemp.devData_nightMode, 6);
										}

										if(dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_bkLightSetOpreat){  //背光灯设置操作，需要时效

											datsSave_Temp.bkColor_swON = tipsInsert_swLedBKG_ON	= dev_dataPointTemp.devData_bkLight[0]; //运行缓存及本地存储更新
											datsSave_Temp.bkColor_swOFF = tipsInsert_swLedBKG_OFF = dev_dataPointTemp.devData_bkLight[1]; //运行缓存及本地存储更新
											devParam_flashDataSave(obj_bkColor_swON, datsSave_Temp); //本地存储动作执行
											devParam_flashDataSave(obj_bkColor_swOFF, datsSave_Temp); //本地存储动作执行

											os_printf(">>>>>>>>agingCmd bkLight coming, on-Isrt:%02d, off-Isrt:%02d.\n", dev_dataPointTemp.devData_bkLight[0], dev_dataPointTemp.devData_bkLight[1]);
										}

										if(dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_horsingLight){  //跑马灯设置操作，需要时效

											os_printf(">>>>>>>>agingCmd horsingLight coming, opreatData:%02X.\n", (int)dev_dataPointTemp.devStatus_Reference.statusRef_horsingLight);
											ifHorsingLight_running_FLAG = dev_dataPointTemp.devStatus_Reference.statusRef_horsingLight;
											if(ifHorsingLight_running_FLAG)counter_ifTipsFree = 0;
											else tips_statusChangeToNormal(); //tips立即恢复
										}

										if(dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_switchBitBindSetOpreat){ //开关位互控绑定设置操作，需要时效

											u8 loop = 0;
											
											for(loop = 0; loop < 3; loop ++){
											
												if(dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_switchBitBindSetOpreat & (1 << loop)){
												
													CTRLEATHER_PORT[loop] = dev_dataPointTemp.devData_switchBitBind[loop]; //本地运行缓存更新
												}
											}
											
											memcpy(datsSave_Temp.port_ctrlEachother, CTRLEATHER_PORT, USRCLUSTERNUM_CTRLEACHOTHER); //本地存储更新
											devParam_flashDataSave(obj_port_ctrlEachother, datsSave_Temp); //本地存储动作执行
											enum_zigbFunMsg mptr_zigbFunRm = msgFun_portCtrlEachoRegister; //即刻注册互控端口
											xQueueSend(xMsgQ_zigbFunRemind, (void *)&mptr_zigbFunRm, 0);

											os_printf(">>>>>>>>agingCmd switchBindSet coming, opreatBitHold:%02X bindData:%02X %02X %02X.\n", (int)dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_switchBitBindSetOpreat,
																																			  (int)dev_dataPointTemp.devData_switchBitBind[0], 
																																			  (int)dev_dataPointTemp.devData_switchBitBind[1], 
																																			  (int)dev_dataPointTemp.devData_switchBitBind[2]);
										}

										if(dev_dataPointTemp.devAgingOpreat_agingReference.agingCmd_curtainOpPeriodSetOpreat){ //窗帘轨道时间设置操作，需要时效

											curtainAct_Param.act_period = dev_dataPointTemp.union_devParam.curtain_param.orbital_Period; //本地运行缓存更新
											datsSave_Temp.devCurtain_orbitalPeriod = dev_dataPointTemp.union_devParam.curtain_param.orbital_Period; //本地存储更新
											devParam_flashDataSave(obi_devCurtainOrbitalPeriod, datsSave_Temp); //本地存储动作执行
											
											if(!curtainAct_Param.act_period)curtainAct_Param.act_period = 240;
											else
											if(curtainAct_Param.act_period == 0xff)curtainAct_Param.act_period = 10;

											os_printf(">>>>>>>>agingCmd curtainOrbitalSet coming, valSet:%d .\n", (int)dev_dataPointTemp.union_devParam.curtain_param.orbital_Period);
										}
									}
									else{

										memset(&dev_agingCmd_rcvPassive, 0, sizeof(stt_agingDataSet_bitHold));
										if(local_ftyRecover_standbyFLG){

											local_ftyRecover_standbyFLG = 0;
											os_printf(">>>>>>>>factory recover doing now!.\n");

											devData_recoverFactory();
											vTaskDelay(10);
											system_restart();
										}
									}

								}break;

								case DTMODEKEEPACESS_FRAMECMD_PST:{

									if(memcmp(&dev_agingCmd_sndInitative, &dev_dataPointTemp.devAgingOpreat_agingReference, sizeof(stt_agingDataSet_bitHold))){ //判断本地时效是否被响应，否则不动作
									
										dtModeKeepAcess_currentCmd = DTMODEKEEPACESS_FRAMECMD_PST;
										
									}else{

										bool heartbeat_Period_recoveryToASR_EN = true; //从主动回复到被动使能

										if(heartbeat_Period_recoveryToASR_EN){

											heartbeat_Period = (u8)(PERIOD_HEARTBEAT_ASR / timer_heartBeatKeep_Period); //定时周期切换为被动
											memset(&dev_agingCmd_sndInitative, 0, sizeof(stt_agingDataSet_bitHold)); //主动发送动作时效清零
											dtModeKeepAcess_currentCmd = DTMODEKEEPACESS_FRAMECMD_ASR;
										}
									}

								}break;

								default:break;
							}
						}
						
					}break;

					case obj_toZigB:{ //给zigb子设备的

						memset(&mptr_S2Z, 0, sizeof(stt_threadDatsPass));
						
						mptr_S2Z.msgType = conventional;
						memcpy(mptr_S2Z.dats.dats_conv.dats, rptr_socketDats.dats, rptr_socketDats.datsLen);
						mptr_S2Z.dats.dats_conv.datsLen = rptr_socketDats.datsLen;
						mptr_S2Z.dats.dats_conv.datsFrom = datsFrom_heartBtRemote;

						switch(rptr_socketDats.dats[0]){

							case FRAME_HEAD_HEARTB:{

								memcpy(mptr_S2Z.dats.dats_conv.macAddr, &rptr_socketDats.dats[3], DEV_MAC_LEN);
//								memcpy(mptr_S2Z.dats.dats_conv.macAddr, &rptr_socketDats.dats[3 + 1], DEV_MAC_LEN); //服务器帮忙往前挪了一个字节
								mptr_S2Z.dats.dats_conv.devType = rptr_socketDats.dats[rptr_socketDats.datsLen - 3];
								mptr_S2Z.dats.dats_conv.dats[0] = ZIGB_FRAMEHEAD_HEARTBEAT;
								
							}break;

							case DTMODEKEEPACESS_FRAMEHEAD_ONLINE:{

								memcpy(mptr_S2Z.dats.dats_conv.macAddr, &rptr_socketDats.dats[2], DEV_MAC_LEN);
								mptr_S2Z.dats.dats_conv.devType = DEVZIGB_DEFAULT;

							}break;

							default:{}break;
						}

						if(!memcmp(debugLogOut_targetMAC ,mptr_S2Z.dats.dats_conv.macAddr, 5)){

							os_printf("[Tips_NWK-SKdats]: node_hb rvc-<mac:%02X %02X %02X %02X %02X>,<cmd:%02X>,<dataLen:%d>.\n", 
									  mptr_S2Z.dats.dats_conv.macAddr[0],
									  mptr_S2Z.dats.dats_conv.macAddr[1],
									  mptr_S2Z.dats.dats_conv.macAddr[2],
									  mptr_S2Z.dats.dats_conv.macAddr[3],
									  mptr_S2Z.dats.dats_conv.macAddr[4],
									  rptr_socketDats.command,
									  rptr_socketDats.datsLen);							
						}

//						os_printf("[Tips_NWK-SKdats]: node_hb rvc-<mac:%02X %02X %02X %02X %02X>,<cmd:%02X>.\n", 
//								  mptr_S2Z.dats.dats_conv.macAddr[0],
//								  mptr_S2Z.dats.dats_conv.macAddr[1],
//								  mptr_S2Z.dats.dats_conv.macAddr[2],
//								  mptr_S2Z.dats.dats_conv.macAddr[3],
//								  mptr_S2Z.dats.dats_conv.macAddr[4],
//								  rptr_socketDats.command); 

						xQueueSend(xMsgQ_Socket2Zigb, (void *)&mptr_S2Z, 0);
						
					}break;

					default:{


					}break;
				}
			}
		}

/*>>>>>>开关状态改变，推送响应处理<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<*/
		if(devActionPush_IF.push_IF){

			devActionPush_IF.push_IF = false;

#if(ZIGB_DATATRANS_WORKMODE == DATATRANS_WORKMODE_HEARTBEAT) //原定时通讯机制：心跳

			const bool specialCMD_IF = false;

#define swActionPush_repeatTxLen	45
			u8 repeatTX_buff[swActionPush_repeatTxLen] = {0};
			u8 repeatTX_Len = 0;	

			repeatTX_buff[11] = devActionPush_IF.dats_Push; //推送数据信息填装

			repeatTX_Len = dtasTX_loadBasic_CUSTOM( DATATRANS_objFLAG_REMOTE,		//发送前最后填装
													repeatTX_buff,				
													FRAME_TYPE_StoM_RCVsuccess,
													FRAME_MtoSCMD_cmdControl,
													specialCMD_IF);
			
			sockets_datsSend(Obj_udpRemote_B, repeatTX_buff, repeatTX_Len);

			os_printf("swData push:%02X.\n", devActionPush_IF.dats_Push);

#elif(ZIGB_DATATRANS_WORKMODE == DATATRANS_WORKMODE_KEEPACESS) //定时通讯机制：周期询访

			dev_currentDataPoint.devStatus_Reference.statusRef_swPush = (devActionPush_IF.dats_Push & 0xE0) >> 5; //属性值填装
			dev_agingCmd_sndInitative.agingCmd_swOpreat = 1; //主动控制时效置位
			dtModeKeepAcess_currentCmd = DTMODEKEEPACESS_FRAMECMD_PST; //命令改为主动上传

			heartbeat_Period = (u8)(PERIOD_HEARTBEAT_PST / timer_heartBeatKeep_Period); //定时周期改为主动
			heartbeat_Cnt = heartbeat_Period;  //立即生效
#endif

		}

/*>>>>>>zigbee消息数据处理<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<*/
		xMsgQ_rcvResult = xQueueReceive(xMsgQ_Zigb2Socket, (void *)&rptr_Z2S, 0);
		if(xMsgQ_rcvResult == pdTRUE){

			memset(datsTX_buff, 0, socketsData_datsTxLen * sizeof(u8));	//清缓存

			switch(rptr_Z2S.msgType){ //消息类型判断

				/*常规数据中转*/
				case conventional:{

//					os_printf("[Tips_NWK-ZBmsg]: msgRcv from zigb <len: %d>\n", rptr_Z2S.dats.dats_conv.datsLen);

					switch(rptr_Z2S.dats.dats_conv.datsFrom){

						case datsFrom_ctrlLocal:{

							memcpy(datsTX_buff, rptr_Z2S.dats.dats_conv.dats, rptr_Z2S.dats.dats_conv.datsLen);
							datsTX_buff[0] = FRAME_HEAD_MOBILE;
							datsTX_Len = dataTransLength_objLOCAL;
							sockets_datsSend(Obj_udpLocal_A, datsTX_buff, datsTX_Len);
							
						}break;

						case datsFrom_ctrlRemote:{

							memcpy(datsTX_buff, rptr_Z2S.dats.dats_conv.dats, rptr_Z2S.dats.dats_conv.datsLen);
							datsTX_buff[0] = FRAME_HEAD_SERVER;
							datsTX_Len = dataTransLength_objREMOTE;
							sockets_datsSend(Obj_udpRemote_B, datsTX_buff, datsTX_Len);
							
						}break;

						case datsFrom_heartBtRemote:{

							if(nwkInternetOnline_IF){ //网关正常则向远端服务器发送心跳

								switch(rptr_Z2S.dats.dats_conv.dats[0]){
									
									case FRAME_HEAD_HEARTB:{
										
										memcpy(datsTX_buff, rptr_Z2S.dats.dats_conv.dats, rptr_Z2S.dats.dats_conv.datsLen);
										datsTX_buff[0] = FRAME_HEAD_HEARTB;
										datsTX_Len = dataHeartBeatLength_objSERVER;
										sockets_datsSend(Obj_udpRemote_B, datsTX_buff, datsTX_Len);
//										printf_datsHtoA("[Tips_NWK-ZBmsg]: zb_HB datsSend is:", datsTX_buff, datsTX_Len);
		
									}break;

									case DTMODEKEEPACESS_FRAMEHEAD_ONLINE:{

										memcpy(datsTX_buff, rptr_Z2S.dats.dats_conv.dats, rptr_Z2S.dats.dats_conv.datsLen);
										datsTX_buff[0] = DTMODEKEEPACESS_FRAMEHEAD_ONLINE;
										datsTX_Len = rptr_Z2S.dats.dats_conv.datsLen;
//										os_printf("[Tips_socketUDP_B]: resTX:%d.\n", sockets_datsSend(Obj_udpRemote_B, datsTX_buff, datsTX_Len));
										sockets_datsSend(Obj_udpRemote_B, datsTX_buff, datsTX_Len);

									}break;

									default:{}break;
								}

							}
							else{ //网络离线则由网关自行回复给子设备心跳

								stt_threadDatsPass *mptrTemp_S2Z = &rptr_Z2S;

								switch(rptr_Z2S.dats.dats_conv.dats[0]){
									
									case FRAME_HEAD_HEARTB:{

										if(rptr_Z2S.dats.dats_conv.dats[2] == FRAME_HEARTBEAT_cmdEven){ //奇数心跳包
										
											mptrTemp_S2Z->dats.dats_conv.dats[0] = ZIGB_OFFLINEFRAMEHEAD_HEARTBEAT;
											xQueueSend(xMsgQ_Socket2Zigb, (void *)mptrTemp_S2Z, 0);
										
										}else
										if(rptr_Z2S.dats.dats_conv.dats[2] == FRAME_HEARTBEAT_cmdOdd){ //偶数心跳包
										
										
										}
										
									}break;
								
									case DTMODEKEEPACESS_FRAMEHEAD_ONLINE:{

										mptrTemp_S2Z->dats.dats_conv.dats[0] = DTMODEKEEPACESS_FRAMEHEAD_OFFLINE;
										xQueueSend(xMsgQ_Socket2Zigb, (void *)mptrTemp_S2Z, 0);

									}break;
								
									default:{}break;
								}
							}

						}break;

						default:{

							
						}break;
					}
				}break;

				/*子设备列表请求数据中转*/
				case listDev_query:{

					

				}break;

				default:break;
			}
		}

		vTaskDelay(1);
	}

	vTaskDelete(NULL);
}

void ICACHE_FLASH_ATTR
network_mainThreadStart(void){

	portBASE_TYPE xReturn = pdFAIL;

	appMsgQueueCreat_S2Z();
	appMsgQueueCreat_datsFromSocket();
	
	os_timer_disarm(&timer_heartBeatKeep);
	os_timer_setfn(&timer_heartBeatKeep, (os_timer_func_t *)timer_heartBeatKeep_funCB, NULL);
	os_timer_arm(&timer_heartBeatKeep, timer_heartBeatKeep_Period, true);
	
	xReturn = xTaskCreate(socketsDataTransProcess_task, "Process_Sockets", 1024, (void *)NULL, 4, &pxTaskHandle_threadNWK);

	os_printf("\npxTaskHandle_threadNWK is %d\n", pxTaskHandle_threadNWK);
}













