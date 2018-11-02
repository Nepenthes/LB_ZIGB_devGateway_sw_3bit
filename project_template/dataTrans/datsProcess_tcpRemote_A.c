#include "datsProcess_tcpRemote_A.h"

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

//#define DNS_ENABLE_TCPA
#define NET_DOMAIN "api.yeelink.net"
//#define NET_DOMAIN "www.lanbonserver.com"

LOCAL os_timer_t timer_tcpConnectDetect_A;

LOCAL struct espconn infoTemp_connTCP_remoteA;
LOCAL esp_tcp ssdp_tcp_remoteA;
ip_addr_t tcp_remoteA_serverIP;

LOCAL bool tcp_remoteA_connFLG = false;
/*---------------------------------------------------------------------------------------------*/

LOCAL void ICACHE_FLASH_ATTR
myTCP_remoteACallback_dataRcv(void *arg, char *pdata, unsigned short len){

	bool dataCorrect_FLG = false;

	remot_info *pcon_info = NULL;

	stt_socketDats mptr_socketDats;

	if (pdata == NULL)return;

//	os_printf("rmd rcv, num[%04d], head[%02X]<<<<.\n", len, *pdata);

//	espconn_get_connection_info(&infoTemp_connUDP_remote_B, &pcon_info, 0);
//	memcpy(infoTemp_connUDP_remote_B.proto.udp->remote_ip, pcon_info->remote_ip, 4);
//	infoTemp_connUDP_remote_B.proto.udp->remote_port = pcon_info->remote_port;

	if(!memcmp("654321", pdata, 6)){
		
		os_printf("[Tips_socketUDP_B]remote ip: %d.%d.%d.%d \r\n", infoTemp_connTCP_remoteA.proto.tcp->remote_ip[0],
																   infoTemp_connTCP_remoteA.proto.tcp->remote_ip[1],
																   infoTemp_connTCP_remoteA.proto.tcp->remote_ip[2],
																   infoTemp_connTCP_remoteA.proto.tcp->remote_ip[3]);
		os_printf("[Tips_socketUDP_B]remote port: %d \r\n", infoTemp_connTCP_remoteA.proto.tcp->remote_port);
		
		espconn_sent(&infoTemp_connTCP_remoteA, "123456\n", 7);
		
	}else{

		/*数据处理*/
		if((*pdata == FRAME_HEAD_SERVER) &&
		   (*(pdata + 13) >= dataTransLength_objREMOTE) &&
		   (*(pdata + 14) >= FRAME_TYPE_MtoS_CMD) ){	//远端数据

			if(!memcmp(&MACSTA_ID[1], pdata + 2, 5)){

			   memcpy(MACDST_ID, pdata + 7, 6);  //远端MAC更新

			   mptr_socketDats.portObj = Obj_tcpRemote_A;
			   mptr_socketDats.command = *(pdata + 15);
			   memcpy(pdata + 1, pdata + 13, len - 13); //数据提取远端MAC后前移十二位
			   mptr_socketDats.datsLen = len - 12;
			   memcpy(mptr_socketDats.dats, pdata, len - 12);
			   
			   mptr_socketDats.heartBeat_IF = false; //不是心跳包
			   mptr_socketDats.dstObj = obj_toWIFI;
			   dataCorrect_FLG = true;
			   
			}else{

			   mptr_socketDats.portObj = Obj_tcpRemote_A;  //原封中转
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

			mptr_socketDats.portObj = Obj_tcpRemote_A;
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

			mptr_socketDats.portObj = Obj_tcpRemote_A;
			mptr_socketDats.command = *(pdata + 2);
			mptr_socketDats.datsLen = len;
			memcpy(mptr_socketDats.dats, pdata, len);

//			printf_datsHtoA("[Tips_socketUDP_B]: get message:", pdata, len);

			if(!memcmp(&MACSTA_ID[1], pdata + (3), 5)){ 	//服务器帮忙往前挪了一个字字节
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
							mptr_socketDats.portObj = Obj_tcpRemote_A;
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

LOCAL void ICACHE_FLASH_ATTR
myTCP_remoteACallback_dataSend(void *arg){

	os_printf("[Tips_socketTCP_A]: data sent cmp.\n");
}

LOCAL void ICACHE_FLASH_ATTR
myTCP_remoteACallback_disConnect(void *arg){

	tcp_remoteA_connFLG = false;

	os_printf("[Tips_socketTCP_A]: disConnect from server!!!\n");
}

LOCAL void ICACHE_FLASH_ATTR
myTCP_remoteACallback_Connect(void *arg){

	struct espconn *pespconn = arg;

	tcp_remoteA_connFLG = true;

	os_printf("[Tips_socketTCP_A]: Connected to server ...\n");

	espconn_regist_recvcb(pespconn, myTCP_remoteACallback_dataRcv);
	espconn_regist_sentcb(pespconn, myTCP_remoteACallback_dataSend);
	espconn_regist_disconcb(pespconn, myTCP_remoteACallback_disConnect);

	espconn_send(&infoTemp_connTCP_remoteA, "[DBG_DATA]: hellow world!!!\n", 26);
}

LOCAL void ICACHE_FLASH_ATTR
myTCP_remoteACallback_reConnect(void *arg, sint8 err){

	tcp_remoteA_connFLG = false;

	switch(err){

		case ESPCONN_TIMEOUT:{
				
				os_printf("[Tips_socketTCP_A]: Connect fail cause timeout .\n");
				os_printf("[Tips_socketTCP_A]:TCP reconnect Result is :%d\n", espconn_connect(&infoTemp_connTCP_remoteA));
			}break;
		case ESPCONN_ABRT:{

				os_printf("[Tips_socketTCP_A]: Connect fail cause abnormal .\n");
				os_printf("[Tips_socketTCP_A]:TCP reconnect Result is :%d\n", espconn_connect(&infoTemp_connTCP_remoteA));
			}break;
		case ESPCONN_RST:{

				os_printf("[Tips_socketTCP_A]: Connect fail cause reset .\n");
				os_printf("[Tips_socketTCP_A]:TCP reconnect Result is :%d\n", espconn_connect(&infoTemp_connTCP_remoteA));
			}break;
		case ESPCONN_CLSD:{

				os_printf("[Tips_socketTCP_A]: Connect fail cause trying fault .\n");
			}break;
		case ESPCONN_CONN:{

				os_printf("[Tips_socketTCP_A]: Connect fail cause normal .\n");
//				os_printf("[Tips_socketTCP_A]:TCP reconnect Result is :%d\n", espconn_connect(&infoTemp_connTCP_remoteA));
			}break;
//		case ESPCONN_HANDSHAKE:{

//				os_printf("[Tips_socketTCP_A]: Connect fail cause sll handshake fault .\n");
//			}break;
//		case ESPCONN_PROTO_MSG:{

//				os_printf("[Tips_socketTCP_A]: Connect fail cause sll message fault .\n");
//			}break;

		default:{

				os_printf("[Tips_socketTCP_A]: Connect fail cause fault unknown: %d .\n", err);
			}break;
	}
}

LOCAL void ICACHE_FLASH_ATTR
myTCP_DNSfound_funCB(const char *name, ip_addr_t *ipaddr, void *arg){

	struct espconn *pespconn = (struct espconn*)arg;

	if(ipaddr == NULL){

		os_printf("[Tips_socketTCP_A]: user dns found NULL\n");
	}

	os_printf("[Tips_socketTCP_A]: dns found is: %d.%d.%d.%d\n", *((uint8 *)&ipaddr->addr + 0), 
															  *((uint8 *)&ipaddr->addr + 1),
															  *((uint8 *)&ipaddr->addr + 2),
															  *((uint8 *)&ipaddr->addr + 3));
	
	if(tcp_remoteA_serverIP.addr == 0 && ipaddr->addr != 0){

		os_timer_disarm(&timer_tcpConnectDetect_A);
		
		tcp_remoteA_serverIP.addr = ipaddr->addr;
		memcpy(infoTemp_connTCP_remoteA.proto.tcp->remote_ip, &ipaddr->addr, 4);
		infoTemp_connTCP_remoteA.proto.tcp->remote_port = 80;
		infoTemp_connTCP_remoteA.proto.tcp->local_port = espconn_port();
		
		espconn_regist_connectcb(&infoTemp_connTCP_remoteA, myTCP_remoteACallback_Connect);
		espconn_regist_reconcb(&infoTemp_connTCP_remoteA, myTCP_remoteACallback_reConnect);

		os_printf("[Tips_socketTCP_A]: DNS connect Result is :%d\n", espconn_connect(&infoTemp_connTCP_remoteA));
	}
}

LOCAL void ICACHE_FLASH_ATTR
tcpConnectDetect_A_funCB(void *para){

	const uint8 myServer_IPaddr[4] = {10,0,0,218};
	struct ip_info ipConfig_info;

	os_timer_disarm(&timer_tcpConnectDetect_A);

	wifi_get_ip_info(STATION_IF, &ipConfig_info);
	if(wifi_station_get_connect_status() == STATION_GOT_IP && ipConfig_info.ip.addr != 0){

		os_printf("[Tips_socketTCP_A]: connect to router and got IP!!! \n");
		
		infoTemp_connTCP_remoteA.proto.tcp = &ssdp_tcp_remoteA;	//建立服务器TCP链接
		infoTemp_connTCP_remoteA.type = ESPCONN_TCP;
		infoTemp_connTCP_remoteA.state = ESPCONN_NONE;
		
#ifdef DNS_ENABLE_TCPA

		tcp_remoteA_serverIP.addr = 0;
		espconn_gethostbyname(&infoTemp_connTCP_remoteA, NET_DOMAIN, &tcp_remoteA_serverIP, myTCP_DNSfound_funCB);

		os_timer_setfn(&timer_tcpConnectDetect_A, (os_timer_func_t *)myTCP_DNSfound_funCB, &infoTemp_connTCP_remoteA);
		os_timer_arm(&timer_tcpConnectDetect_A, 100, 0);
#else

		memcpy(infoTemp_connTCP_remoteA.proto.tcp->remote_ip, myServer_IPaddr, 4);
		infoTemp_connTCP_remoteA.proto.tcp->remote_port = 6000;		
		infoTemp_connTCP_remoteA.proto.tcp->local_port = espconn_port();

		espconn_regist_connectcb(&infoTemp_connTCP_remoteA, myTCP_remoteACallback_Connect);
		espconn_regist_reconcb(&infoTemp_connTCP_remoteA, myTCP_remoteACallback_reConnect);

		os_printf("[Tips_socketTCP_A]:TCP connect Result is :%d\n", espconn_connect(&infoTemp_connTCP_remoteA));
#endif
	}
	else{

		os_timer_setfn(&timer_tcpConnectDetect_A, tcpConnectDetect_A_funCB, NULL);
		os_timer_arm(&timer_tcpConnectDetect_A, 100, 0);
	}
}

STATUS ICACHE_FLASH_ATTR
TCPremoteA_datsSend(u8 dats[], u16 datsLen){

	if(tcp_remoteA_connFLG){

		espconn_sent(&infoTemp_connTCP_remoteA, dats, datsLen);
		return OK;
	}
	else{

		return FAIL;
	}
}

void ICACHE_FLASH_ATTR
tcpRemote_A_connectStart(void){

	os_timer_disarm(&timer_tcpConnectDetect_A);
	os_timer_setfn(&timer_tcpConnectDetect_A, (os_timer_func_t *)tcpConnectDetect_A_funCB, NULL);
	os_timer_arm(&timer_tcpConnectDetect_A, 100, 0);
}



