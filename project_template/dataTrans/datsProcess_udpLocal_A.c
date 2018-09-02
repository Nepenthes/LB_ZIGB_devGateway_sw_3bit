#include "datsProcess_udpLocal_A.h"

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

LOCAL struct espconn infoTemp_connUDP_local_A;
LOCAL esp_udp ssdp_udp_local_A;		
/*---------------------------------------------------------------------------------------------*/

LOCAL void ICACHE_FLASH_ATTR
myUDP_local_ACallback(void *arg, char *pdata, unsigned short len){

	bool dataCorrect_FLG = false;

	remot_info *pcon_info = NULL;

	stt_socketDats mptr_socketDats;

	if (pdata == NULL)return;

	espconn_get_connection_info(&infoTemp_connUDP_local_A, &pcon_info, 0);
	memcpy(infoTemp_connUDP_local_A.proto.udp->remote_ip, pcon_info->remote_ip, 4);
	infoTemp_connUDP_local_A.proto.udp->remote_port = pcon_info->remote_port;

	if(!memcmp("654321", pdata, 6)){

		os_printf("[Tips_socketUDP_A]remote ip: %d.%d.%d.%d \r\n",pcon_info->remote_ip[0],
																  pcon_info->remote_ip[1],
																  pcon_info->remote_ip[2],
																  pcon_info->remote_ip[3]);
		os_printf("[Tips_socketUDP_A]remote port: %d \r\n",pcon_info->remote_port);

		espconn_sent(&infoTemp_connUDP_local_A, "123456\n", 7);

	}else{

		/*数据处理*/
		if((*pdata == FRAME_HEAD_MOBILE) && 
		   ((u16)*(pdata + 1) == len) &&
		   *(pdata + 2) == FRAME_TYPE_MtoS_CMD){

			bool frameCodeCheck_PASS = false; // 校验码通过标志
			bool frameMacCheck_PASS  = false; // MAC通过标志

			if(*(pdata + 4) == frame_Check(pdata + 5, *(pdata + 1) - 5))frameCodeCheck_PASS = true;
			if(!memcmp(&MACSTA_ID[1], pdata + 5, 5))frameMacCheck_PASS = true;

			if( (*(pdata + 3) == FRAME_MtoSCMD_cmdConfigSearch) ){ //特殊指令不进行MAC地址校验	

				frameMacCheck_PASS = true;
			
			}
			if( (*(pdata + 3) == FRAME_MtoSCMD_cmdCfg_swTim) || //特殊指令不进行校验码校验
				(*(pdata + 3) == FRAME_MtoSCMD_cmdswTimQuery) ){

				frameCodeCheck_PASS = true;
			}

			if(frameMacCheck_PASS == true && frameCodeCheck_PASS)dataCorrect_FLG = true;

			if(dataCorrect_FLG == true){

				if( (*(pdata + 3) == FRAME_MtoSCMD_cmdConfigSearch) ){

					mptr_socketDats.dstObj = obj_toALL;
					
				}else{

					mptr_socketDats.dstObj = obj_toWIFI;
				}
			
			}else{

				mptr_socketDats.dstObj = obj_toZigB;
			}

			mptr_socketDats.portObj = Obj_udpLocal_A;
			mptr_socketDats.command = *(pdata + 3);
			mptr_socketDats.datsLen = len;
			memcpy(mptr_socketDats.dats, pdata, len);
			mptr_socketDats.heartBeat_IF = false;	//不是心跳包
			
			xQueueSend(xMsgQ_datsFromSocketPort, (void *)&mptr_socketDats, 0);
		}
		   
		/*数据输出log_Debug*/
//		printf_datsHtoA("[Tips_socketUDP_A]: get message:", pdata, len);
	}
}

void ICACHE_FLASH_ATTR
UDPlocalA_datsSend(u8 dats[], u16 datsLen){

	espconn_sent(&infoTemp_connUDP_local_A, dats, datsLen);
	os_printf("[Tips_socketUDP_A]: msg send ok!!!\n");
}

void ICACHE_FLASH_ATTR
mySocketUDPlocal_A_buildInit(void){

	ssdp_udp_local_A.local_port = 8866; 			//建立本地udp链接
	infoTemp_connUDP_local_A.type = ESPCONN_UDP;
	
	infoTemp_connUDP_local_A.proto.udp = &(ssdp_udp_local_A);
	espconn_regist_recvcb(&infoTemp_connUDP_local_A, myUDP_local_ACallback);
	espconn_create(&infoTemp_connUDP_local_A);

	printf("\n[Tips_socketUDP_A]: socket build compeled!!!\n");
}




























