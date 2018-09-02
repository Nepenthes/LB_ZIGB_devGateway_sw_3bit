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

extern u8 	swTim_onShoot_FLAG; 
extern bool ifTim_sw_running_FLAG; 

extern u8 tipsInsert_swLedBKG_ON;
extern u8 tipsInsert_swLedBKG_OFF;

bool nwkInternetOnline_IF = false;	//internet网络在线标志

xQueueHandle xMsgQ_Socket2Zigb;
xQueueHandle xMsgQ_datsFromSocketPort;

LOCAL xTaskHandle pxTaskHandle_threadNWK;

LOCAL internet_connFLG = false;	//STA网络在线标志

LOCAL os_timer_t timer_heartBeatKeep;
const uint32 timer_heartBeatKeep_Period = 1000;

const u8 test_Dats1[14] = { //调试数据

	0xAA, 0x0E, 0x23, 0x00, 0x20, 0x15, 0x07, 0x12, 0x36, 0x00, 0x00, 0x01, 0x01, 0x00
};

const u8 test_Dats2[14] = {

	0xAA, 0x0E, 0x22, 0x00, 0x20, 0x15, 0x07, 0x12, 0x36, 0xFF, 0xFF, 0x00, 0x00, 0x00 
};
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

	xMsgQ_Socket2Zigb = xQueueCreate(20, sizeof(stt_threadDatsPass));
	if(0 == xMsgQ_Socket2Zigb)return OK;
	else return FAIL;
}

LOCAL STATUS ICACHE_FLASH_ATTR
appMsgQueueCreat_datsFromSocket(void){

	xMsgQ_datsFromSocketPort = xQueueCreate(20, sizeof(stt_socketDats));
	if(0 == xMsgQ_Socket2Zigb)return OK;
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
			
			if(!ifSpecial_CMD)dats_Tx[10 + 12] = DEV_SWITCH_TYPE;	//开关类型填充
			
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
			
			if(!ifSpecial_CMD)dats_Tx[10] = DEV_SWITCH_TYPE;	//开关类型填充
			
			memcpy(&dats_Tx[5], &MACSTA_ID[1], 5);	//本地MAC填充
								  
			dats_Tx[4] 	= frame_Check(&dats_Tx[5], 28);

			return dataTransLength_objLOCAL;
			
		}break;
		
		default:break;
	}	
}

LOCAL STATUS ICACHE_FLASH_ATTR
sockets_datsSend(socket_OBJ sObj, u8 dats[], u16 datsLen){

	if(internet_connFLG){

		switch(sObj){
		
			case Obj_udpLocal_A:	UDPlocalA_datsSend(dats, datsLen);return OK;
			case Obj_udpRemote_B:	UDPremoteB_datsSend(dats, datsLen);return OK;
			case Obj_tcpRemote_A:	return TCPremoteA_datsSend(dats, datsLen);
			case Obj_tcpRemote_B:	return TCPremoteB_datsSend(dats, datsLen);
			default:return FAIL;
		}
	}
	else{

		return FAIL;
	}
}

LOCAL void ICACHE_FLASH_ATTR
timer_heartBeatKeep_funCB(void *para){

	struct ip_info ipConfig_info;

	LOCAL bool heartbeat_oeFLG = true;	//奇偶包标志
	LOCAL u8 heartbeat_Cnt = 0;	//心跳包周期计数
	const u8 heartbeat_Period = SOCKET_HEARTBEAT_PERIOD / timer_heartBeatKeep_Period;	//心跳包计数周期

	u8 heartBeat_Pack[dataHeartBeatLength_objSERVER] = {0};

	wifi_get_ip_info(STATION_IF, &ipConfig_info);
	(wifi_station_get_connect_status() == STATION_GOT_IP && ipConfig_info.ip.addr != 0)?(internet_connFLG = true):(internet_connFLG = false);

	if(internet_connFLG){

		if(heartbeat_Cnt < heartbeat_Period)heartbeat_Cnt ++;
		else{

			heartbeat_Cnt = 0;
			heartbeat_oeFLG = !heartbeat_oeFLG;

			heartBeat_Pack[0] = FRAME_HEAD_HEARTB;				
			heartBeat_Pack[1] = dataHeartBeatLength_objSERVER;
			memcpy(&heartBeat_Pack[4], &MACSTA_ID[1], 5);

			if(heartbeat_oeFLG){	//奇数包

				heartBeat_Pack[2] = FRAME_HEARTBEAT_cmdOdd;

				heartBeat_Pack[9] 	= 0; 
				heartBeat_Pack[10] 	= 0; 
				heartBeat_Pack[11] 	= 0; 
				heartBeat_Pack[12] 	= 0; 
				heartBeat_Pack[13] 	= 0; 
				
			}else{					//偶数包

				heartBeat_Pack[2] = FRAME_HEARTBEAT_cmdEven;

				heartBeat_Pack[9] 	= 0; 
				heartBeat_Pack[10] 	= 0; 
				heartBeat_Pack[11] 	= 0; 
				heartBeat_Pack[12] 	= 0; 
				heartBeat_Pack[13] 	= 0; 
			}

			sockets_datsSend(Obj_udpRemote_B, heartBeat_Pack, dataHeartBeatLength_objSERVER);
			
//			printf_datsHtoA("[Tips_threadNet]: hearBeat datsSend is:", heartBeat_Pack, dataHeartBeatLength_objSERVER);
//			(heartbeat_oeFLG)?(sockets_datsSend(Obj_udpRemote_B, (u8 *)test_Dats1, 14)):(sockets_datsSend(Obj_udpRemote_B, (u8 *)test_Dats2, 14));
		}
	}else{

		
	}
}

LOCAL void ICACHE_FLASH_ATTR
socketsDataTransProcess_task(void *pvParameters){

//	spi_flash_write(uint32 des_addr, uint32 * src_addr, uint32 size)

	stt_threadDatsPass 	mptr_S2Z;
	stt_threadDatsPass 	rptr_Z2S;
	stt_socketDats 		rptr_socketDats;
	portBASE_TYPE 		xMsgQ_rcvResult = pdFALSE;

#define socketsData_repeatTxLen	45
	u8 repeatTX_buff[socketsData_repeatTxLen] = {0};
	u8 repeatTX_Len = 0;

#define socketsData_datsTxLen	64
	u8 datsTX_buff[socketsData_datsTxLen] = {0};
	u8 datsTX_Len = 0;

	stt_usrDats_privateSave datsSave_Temp = {0};
	stt_usrDats_privateSave *datsRead_Temp;
		
	bool socketRespond_IF = false;
	bool specialCMD_IF = false;

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

							swCommand_fromUsr.objRelay = rptr_socketDats.dats[11];
							swCommand_fromUsr.actMethod = relay_OnOff;

							repeatTX_buff[11] = rptr_socketDats.dats[11];
		
							specialCMD_IF		= false;
							socketRespond_IF	= true;
		
						}break;
		
						case FRAME_MtoSCMD_cmdConfigSearch:{

							datsSave_Temp.timeZone_H = rptr_socketDats.dats[12];
							datsSave_Temp.timeZone_M = rptr_socketDats.dats[13];
							devParam_flashDataSave(obj_timeZone_H, datsSave_Temp);
							devParam_flashDataSave(obj_timeZone_M, datsSave_Temp);

							mySocketUDPremote_B_serverChange(&rptr_socketDats.dats[6]);

							repeatTX_buff[14] = (u8)(nwkZigb_currentPANID >> 8);
							repeatTX_buff[15] = (u8)(nwkZigb_currentPANID >> 0);
		
							specialCMD_IF		= false;
							socketRespond_IF	= true;
						
						}break;
		
						case FRAME_MtoSCMD_cmdQuery:{

							
							
						}break;
		
						case FRAME_MtoSCMD_cmdInterface:{

							
							
						}break;
		
						case FRAME_MtoSCMD_cmdReset:{
		
							
						}break;
		
						case FRAME_MtoSCMD_cmdLockON:{
		
							
						}break;
		
						case FRAME_MtoSCMD_cmdLockOFF:{
		
							
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
								
								default:break;
							}

							socketRespond_IF = true;
							
						}break;
		
						case FRAME_MtoSCMD_cmdConfigAP:{
		
							
						
						}break;
		
						case FRAME_MtoSCMD_cmdBeepsON:{
		
							
						}break;
		
						case FRAME_MtoSCMD_cmdBeepsOFF:{
		
							
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
											rptr_socketDats.dats[14 + loop * 3] |= (1 << (rptr_socketDats.dats[31] - 1)); //强制进行周占位，执行完毕后清除
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
							
							for(loop = 0; loop < clusterNum_usr; loop ++){
							
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

							memcpy(&repeatTX_buff[14], CTRLEATHER_PORT, clusterNum_usr);

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

					case obj_toWIFI:{

						os_printf("[Tips_NWK-SKdats]: netGate_hb rvc-<mac:%02X %02X %02X %02X %02X>,<cmd:%02X>.\n", 
								  rptr_socketDats.dats[3],
								  rptr_socketDats.dats[4],
								  rptr_socketDats.dats[5],
								  rptr_socketDats.dats[6],
								  rptr_socketDats.dats[7],
								  rptr_socketDats.command);
	
					}break;

					case obj_toZigB:{

						memset(&mptr_S2Z, 0, sizeof(stt_threadDatsPass));
						
						mptr_S2Z.msgType = conventional;
						memcpy(mptr_S2Z.dats.dats_conv.dats, rptr_socketDats.dats, rptr_socketDats.datsLen);
						mptr_S2Z.dats.dats_conv.datsLen = rptr_socketDats.datsLen;
						memcpy(mptr_S2Z.dats.dats_conv.macAddr, &rptr_socketDats.dats[3], DEV_MAC_LEN);
//						memcpy(mptr_S2Z.dats.dats_conv.macAddr, &rptr_socketDats.dats[3 + 1], DEV_MAC_LEN);	//服务器帮忙往前挪了一个字节
						mptr_S2Z.dats.dats_conv.devType = rptr_socketDats.dats[rptr_socketDats.datsLen - 3];
						mptr_S2Z.dats.dats_conv.dats[0] = ZIGB_FRAMEHEAD_HEARTBEAT;
						mptr_S2Z.dats.dats_conv.datsFrom = datsFrom_heartBtRemote;

						os_printf("[Tips_NWK-SKdats]: node_hb rvc-<mac:%02X %02X %02X %02X %02X>,<cmd:%02X>.\n", 
								  mptr_S2Z.dats.dats_conv.macAddr[0],
								  mptr_S2Z.dats.dats_conv.macAddr[1],
								  mptr_S2Z.dats.dats_conv.macAddr[2],
								  mptr_S2Z.dats.dats_conv.macAddr[3],
								  mptr_S2Z.dats.dats_conv.macAddr[4],
								  rptr_socketDats.command);

						xQueueSend(xMsgQ_Socket2Zigb, (void *)&mptr_S2Z, 0);
						
					}break;

					default:{


					}break;
				}
			}
		}

/*>>>>>>zigbee消息数据处理<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<*/
		xMsgQ_rcvResult = xQueueReceive(xMsgQ_Zigb2Socket, (void *)&rptr_Z2S, 0);
		if(xMsgQ_rcvResult == pdTRUE){

			memset(datsTX_buff, 0, socketsData_datsTxLen * sizeof(u8));	//清缓存

			switch(rptr_Z2S.msgType){ //消息类型判断

				/*常规数据中转*/
				case conventional:{

					os_printf("[Tips_NWK-ZBmsg]: msgRcv from zigb <len: %d>\n", rptr_Z2S.dats.dats_conv.datsLen);

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

								memcpy(datsTX_buff, rptr_Z2S.dats.dats_conv.dats, rptr_Z2S.dats.dats_conv.datsLen);
								datsTX_buff[0] = FRAME_HEAD_HEARTB;
								datsTX_Len = dataHeartBeatLength_objSERVER;
								sockets_datsSend(Obj_udpRemote_B, datsTX_buff, datsTX_Len);
//								printf_datsHtoA("[Tips_NWK-ZBmsg]: zb_HB datsSend is:", datsTX_buff, datsTX_Len);

							}else{ //网络离线则由网关自行回复给子设备心跳

								stt_threadDatsPass *mptrTemp_S2Z = &rptr_Z2S;

								if(rptr_Z2S.dats.dats_conv.dats[2] == FRAME_HEARTBEAT_cmdEven){ //奇数心跳包

									mptrTemp_S2Z->dats.dats_conv.dats[0] = ZIGB_OFFLINEFRAMEHEAD_HEARTBEAT;
									xQueueSend(xMsgQ_Socket2Zigb, (void *)mptrTemp_S2Z, 0);
								
								}else
								if(rptr_Z2S.dats.dats_conv.dats[2] == FRAME_HEARTBEAT_cmdOdd){ //偶数心跳包


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













