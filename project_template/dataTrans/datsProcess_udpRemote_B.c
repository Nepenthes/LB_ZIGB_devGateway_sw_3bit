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
			   
			   mptr_socketDats.heartBeat_IF = false; //不是心跳�?
			   mptr_socketDats.dstObj = obj_toWIFI;
			   dataCorrect_FLG = true;
			   
			}else{

			   mptr_socketDats.portObj = Obj_udpRemote_B;  //原封中转
			   mptr_socketDats.command = *(pdata + 15);
			   mptr_socketDats.datsLen = len;
			   memcpy(mptr_socketDats.dats, pdata, len);
			   
			   mptr_socketDats.heartBeat_IF = false;   //不是心跳�?
			   mptr_socketDats.dstObj = obj_toZigB;
			   dataCorrect_FLG = true;
			}
			
		}
		else
		if((*pdata == FRAME_HEAD_HEARTB) &&
			(*(pdata + 1) == dataHeartBeatLength_objSERVER)){   //远端心跳

			mptr_socketDats.portObj = Obj_udpRemote_B;
			mptr_socketDats.command = *(pdata + 2);
			mptr_socketDats.datsLen = len;
			memcpy(mptr_socketDats.dats, pdata, len);

//			printf_datsHtoA("[Tips_socketUDP_B]: get message:", pdata, len);

			if(!memcmp(&MACSTA_ID[1], pdata + (3), 5)){		//服务器帮忙往前挪了一个字�?
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

		if(dataCorrect_FLG){

			xQueueSend(xMsgQ_datsFromSocketPort, (void *)&mptr_socketDats, 0);
		}

//		printf_datsHtoA("[Tips_socketUDP_B]: get message:", pdata, len);
	}
}

void ICACHE_FLASH_ATTR
UDPremoteB_datsSend(u8 dats[], u16 datsLen){

	espconn_sent(&infoTemp_connUDP_remote_B, dats, datsLen);
	os_printf("[Tips_socketUDP_B]: msg send ok!!!\n");
}

void ICACHE_FLASH_ATTR
mySocketUDPremote_B_buildInit(u8 refRemote_IP[4]){

	memcpy(ssdp_udp_remote_B.remote_ip, refRemote_IP, 4);
	ssdp_udp_remote_B.remote_port = 80;
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
		mySocketUDPremote_B_buildInit(remoteIP_toChg);
		os_printf("[Tips_socketUDP_B]: UDP remoteB serverIP change cmp.!!!\n");
		
	}else{

		os_printf("[Tips_socketUDP_B]: ip no change.!!!\n");
	}

	os_free(datsRead_Temp);
}














