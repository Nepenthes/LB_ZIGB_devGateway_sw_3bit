#include "datsProcess_udpRemote_B.h"

#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "espressif/espconn.h"
#include "espressif/airkiss.h"

#include "datsManage.h"

#include "datsProcess_socketsNetwork.h"

LOCAL struct espconn infoTemp_connUDP_remote_B;
LOCAL esp_udp ssdp_udp_remote_B;	
/*---------------------------------------------------------------------------------------------*/

LOCAL void ICACHE_FLASH_ATTR
myUDP_remote_BCallback(void *arg, char *pdata, unsigned short len){

	bool dataCorrect_FLG = false;

	remot_info *pcon_info = NULL;

	stt_socketDats mptr_socketDats;

	if (pdata == NULL)return;

//	os_printf("rmd rcv, num[%04d], head[%02X]<<<<.\n", len, *pdata);

//	espconn_get_connection_info(&infoTemp_connUDP_remote_B, &pcon_info, 0);
//	memcpy(infoTemp_connUDP_remote_B.proto.udp->remote_ip, pcon_info->remote_ip, 4);
//	infoTemp_connUDP_remote_B.proto.udp->remote_port = pcon_info->remote_port;

	if(!memcmp("654321", pdata, 6)){
		
		os_printf("[Tips_socketUDP_B]remote ip: %d.%d.%d.%d \r\n",pcon_info->remote_ip[0],
																  pcon_info->remote_ip[1],
																  pcon_info->remote_ip[2],
																  pcon_info->remote_ip[3]);
		os_printf("[Tips_socketUDP_B]remote port: %d \r\n",pcon_info->remote_port);
		
		espconn_sent(&infoTemp_connUDP_remote_B, "123456\n", 7);
		
	}else{

		/*数据处理*/
		if((*pdata == FRAME_HEAD_SERVER) &&
		   (*(pdata + 13) >= dataTransLength_objREMOTE) &&
		   (*(pdata + 14) >= FRAME_TYPE_MtoS_CMD) ){	//远端数据

			if(!memcmp(&MACSTA_ID[1], pdata + 2, 5)){

			   memcpy(MACDST_ID, pdata + 7, 6);  //远端MAC更新

			   mptr_socketDats.portObj = Obj_udpRemote_B;
			   mptr_socketDats.command = *(pdata + 15);
			   memcpy(pdata + 1, pdata + 13, len - 13); //数据提取远端MAC后前移十二位
			   mptr_socketDats.datsLen = len - 12;
			   memcpy(mptr_socketDats.dats, pdata, len - 12);
			   
			   mptr_socketDats.heartBeat_IF = false; //不是心跳包
			   mptr_socketDats.dstObj = obj_toWIFI;
			   dataCorrect_FLG = true;
			   
			}else{

			   mptr_socketDats.portObj = Obj_udpRemote_B;  //原封中转
			   mptr_socketDats.command = *(pdata + 15);
			   mptr_socketDats.datsLen = len;
			   memcpy(mptr_socketDats.dats, pdata, len);
			   
			   mptr_socketDats.heartBeat_IF = false;   //不是心跳包
			   mptr_socketDats.dstObj = obj_toZigB;
			   dataCorrect_FLG = true;
			}
			
		}
		else
		if( (*pdata == DTMODEKEEPACESS_FRAMEHEAD_ONLINE) && //定时询访机制帧头
		    (*(pdata + 1) == len) && 
		    (*(pdata + 8) == DTMODEKEEPACESS_FRAMECMD_ASR || *(pdata + 8) == DTMODEKEEPACESS_FRAMECMD_PST) ){

//			os_printf("pag mark.\n");

			mptr_socketDats.portObj = Obj_udpRemote_B;
			mptr_socketDats.command = *(pdata + 8);
			mptr_socketDats.datsLen = len;
			memcpy(mptr_socketDats.dats, pdata, len);

			if(!memcmp(&MACSTA_ID[1], pdata + 2, 5)){

				mptr_socketDats.heartBeat_IF = true;	
				mptr_socketDats.dstObj = obj_toWIFI;
				dataCorrect_FLG = true;
				
			}else{

				mptr_socketDats.heartBeat_IF = true;	
				mptr_socketDats.dstObj = obj_toZigB;
				dataCorrect_FLG = true;				
			}
		}
		else
		if( (*pdata == FRAME_HEAD_HEARTB) &&	//远端心跳
		    (*(pdata + 1) == dataHeartBeatLength_objSERVER) &&
			(*(pdata + 1) == len) ){   

			mptr_socketDats.portObj = Obj_udpRemote_B;
			mptr_socketDats.command = *(pdata + 2);
			mptr_socketDats.datsLen = len;
			memcpy(mptr_socketDats.dats, pdata, len);

//			printf_datsHtoA("[Tips_socketUDP_B]: get message:", pdata, len);

			if(!memcmp(&MACSTA_ID[1], pdata + (3), 5)){		//服务器帮忙往前挪了一个字字节
//			if(!memcmp(&MACSTA_ID[1], pdata + (3 + 1), 5)){
				
				mptr_socketDats.heartBeat_IF = true;    
				mptr_socketDats.dstObj = obj_toWIFI;
				dataCorrect_FLG = true;
			   
			}else{

				mptr_socketDats.heartBeat_IF = true;	
				mptr_socketDats.dstObj = obj_toZigB;
				dataCorrect_FLG = true;
			}
		}
		else
		if( (*pdata == FRAME_HEAD_MOBILE) && 
		    (*(pdata + 2) == FRAME_TYPE_MtoS_CMD) ){ //远端特殊指令

			bool specialCMD_IF = false;

			if(*(pdata + 3) == FRAME_MtoZIGBCMD_cmdCfg_scenarioCtl)specialCMD_IF = true; //指令数据甄别

			if(specialCMD_IF){ //一级非常规处理

//				os_printf("scenarioCtl cmd rcv, dataLen:%04d bytes<<<<.\n", len);

				switch(*(pdata + 3)){

					case FRAME_MtoZIGBCMD_cmdCfg_scenarioCtl:{ //>>>场景控制<<<
					
						u16 dats_Len = (u16)(*(pdata + 1)) * 6 + 10; //实际帧长（数据包帧长为操作开关个数）
						
						if((dats_Len == len) && !memcmp(&MACSTA_ID[1], pdata + 4, 5)){ //特殊指令 MAC从第五字节开始
						
							u8 loop = 0;
							u8 pointTemp = 0;
						
							scenarioOprateDats.devNode_num = *(pdata + 1); //集群数量填装
							scenarioOprateDats.scenarioCtrlOprate_IF = true; //场景集群操作使能
							for(loop = 0; loop < *(pdata + 1); loop ++){
						
								pointTemp = 9 + 6 * loop;
								memcpy(scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC, pdata + pointTemp, 5); //集群单位MAC填装
								scenarioOprateDats.scenarioOprate_Unit[loop].devNode_opStatus = *(pdata + pointTemp + 5); //集群单位操作状态填装
							}

							memset(&COLONY_DATAMANAGE_SCENE, 0, sizeof(stt_scenarioOprateDats));
							memcpy(&COLONY_DATAMANAGE_SCENE, &scenarioOprateDats, sizeof(stt_scenarioOprateDats)); //本地数据集中管理更新

							mptr_socketDats.dstObj = obj_toWIFI;
							mptr_socketDats.portObj = Obj_udpRemote_B;
							mptr_socketDats.command = *(pdata + 3);
							mptr_socketDats.datsLen = 0;
							mptr_socketDats.heartBeat_IF = false;	//不是心跳包
							
							xQueueSend(xMsgQ_datsFromSocketPort, (void *)&mptr_socketDats, 0);
						}
						else{ //打印错误分析

							os_printf("scenarioCtl parsing fail, {frameDataLen:%04d<-->actualDataLen:%04d, frameTargetMAC:%02X %02X %02X %02X %02X, actualMAC:%02X %02X %02X %02X %02X}<<<<.\n", 
									  (*(pdata + 1)) * 6 + 10,
									  len,
									  *(pdata + 4),
									  *(pdata + 5),
									  *(pdata + 6),
									  *(pdata + 7),
									  *(pdata + 8),
									  MACSTA_ID[1],
									  MACSTA_ID[2],
									  MACSTA_ID[3],
									  MACSTA_ID[4],
									  MACSTA_ID[5]);
						}
						
					}break;

					default:break;
				}
			}
		}

		if(dataCorrect_FLG){

			xQueueSend(xMsgQ_datsFromSocketPort, (void *)&mptr_socketDats, 0);
		}

//		printf_datsHtoA("[Tips_socketUDP_B]: get message:", pdata, len);
	}
}

void ICACHE_FLASH_ATTR
UDPremoteB_datsSend(u8 dats[], u16 datsLen){

	sint8 res = espconn_sent(&infoTemp_connUDP_remote_B, dats, datsLen);

	if(res){

		os_printf("[Tips_socketUDP_B]: msg send fail, res:%d, socketInfoptr:%X, dataPtr:%X, dataLen:%d\n", res, &infoTemp_connUDP_remote_B, dats, datsLen);
	}

//	espconn_sent(&infoTemp_connUDP_remote_B, dats, datsLen);
}

void ICACHE_FLASH_ATTR
mySocketUDPremote_B_buildAct(u8 refRemote_IP[4]){

	memcpy(ssdp_udp_remote_B.remote_ip, refRemote_IP, 4);
//	ssdp_udp_remote_B.remote_port = 80;
	ssdp_udp_remote_B.remote_port = 4000;
	ssdp_udp_remote_B.local_port = espconn_port(); //建立远端udp链接
	infoTemp_connUDP_remote_B.type = ESPCONN_UDP;
	
	infoTemp_connUDP_remote_B.proto.udp = &(ssdp_udp_remote_B);
	espconn_regist_recvcb(&infoTemp_connUDP_remote_B, myUDP_remote_BCallback);
	espconn_create(&infoTemp_connUDP_remote_B);

	os_printf("[Tips_socketUDP_B]: socket build compeled!!!\n");
}

void ICACHE_FLASH_ATTR
mySocketUDPremote_B_serverChange(u8 remoteIP_toChg[4]){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();
	stt_usrDats_privateSave datsSave_Temp = {0};

	if(memcmp(remoteIP_toChg, datsRead_Temp->serverIP_default, 4)){

		memcpy(datsSave_Temp.serverIP_default, remoteIP_toChg, 4);
		devParam_flashDataSave(obj_serverIP_default, datsSave_Temp);
		espconn_disconnect(&infoTemp_connUDP_remote_B);
		espconn_delete(&infoTemp_connUDP_remote_B);
		mySocketUDPremote_B_buildAct(remoteIP_toChg);
		os_printf("[Tips_socketUDP_B]: UDP remoteB serverIP change to %d.%d.%d.%d cmp.!!!\n", 	remoteIP_toChg[0],
																								remoteIP_toChg[1],
																								remoteIP_toChg[2],
																								remoteIP_toChg[3]);
									
	}else{

		os_printf("[Tips_socketUDP_B]: ip no change.!!!\n");
	}

	if(datsRead_Temp)os_free(datsRead_Temp);
}

void ICACHE_FLASH_ATTR
mySocketUDPremote_B_buildInit(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();
	mySocketUDPremote_B_buildAct(datsRead_Temp->serverIP_default);
//	mySocketUDPremote_B_buildAct((u8 *)serverRemote_IP_Lanbon);
}















