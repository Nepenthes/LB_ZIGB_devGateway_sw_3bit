#include "datsProcess_tcpRemote_B.h"

#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "espressif/espconn.h"
#include "espressif/airkiss.h"

#include "datsManage.h"

#define DNS_ENABLE_TCPB	
//#define NET_DOMAIN "api.yeelink.net"
#define NET_DOMAIN "www.lanbonserver.com"

LOCAL os_timer_t timer_tcpConnectDetect_B;

LOCAL struct espconn infoTemp_connTCP_remoteB;
LOCAL esp_tcp ssdp_tcp_remoteB;
ip_addr_t tcp_remoteB_serverIP;

LOCAL bool tcp_remoteB_connFLG = false;
/*---------------------------------------------------------------------------------------------*/

LOCAL void ICACHE_FLASH_ATTR
myTCP_remoteBCallback_dataRcv(void *arg, char *pdata, unsigned short len){

	remot_info *pcon_info = NULL;

	if (pdata == NULL)return;

//	printf_datsHtoA("[Tips_socketTCP_B]:", pdata, len);

	os_printf("[Tips_socketTCP_B](datsLen: %d): msgComming is : %s\n", len, pdata);
}

LOCAL void ICACHE_FLASH_ATTR
myTCP_remoteBCallback_dataSend(void *arg){

	os_printf("[Tips_socketTCP_B]: data sent successfully!!!\n");
}

LOCAL void ICACHE_FLASH_ATTR
myTCP_remoteBCallback_disConnect(void *arg){

	tcp_remoteB_connFLG = false;

	os_printf("[Tips_socketTCP_B]: disConnect from server!!!\n");
//	os_printf("[Tips_socketTCP_B]:TCP reconnect Result is :%d\n", espconn_connect(&infoTemp_connTCP_remoteB));
}

LOCAL void ICACHE_FLASH_ATTR
myTCP_remoteBCallback_Connect(void *arg){

	const u8 *dats_Test = "HEAD /sm/image/firewareNew_test.bin HTTP/1.1\r\nAccept:*/*\r\nHost:www.lanbonserver.com\r\nConnection:Keep-Alive\r\n\r\n\0";

	struct espconn *pespconn = arg;

	tcp_remoteB_connFLG = true;

	os_printf("[Tips_socketTCP_B]: Connected to server ...\n");

	espconn_regist_recvcb(pespconn, myTCP_remoteBCallback_dataRcv);
	espconn_regist_sentcb(pespconn, myTCP_remoteBCallback_dataSend);
	espconn_regist_disconcb(pespconn, myTCP_remoteBCallback_disConnect);

//	espconn_send(&infoTemp_connTCP_remoteB, (u8 *)dats_Test, strlen(dats_Test));
}

LOCAL void ICACHE_FLASH_ATTR
myTCP_remoteBCallback_reConnect(void *arg, sint8 err){

	tcp_remoteB_connFLG = false;

	switch(err){

		case ESPCONN_TIMEOUT:{
				
				os_printf("[Tips_socketTCP_B]: Connect fail cause timeout .\n");
				os_printf("[Tips_socketTCP_B]:TCP reconnect Result is :%d\n", espconn_connect(&infoTemp_connTCP_remoteB));
			}break;
		case ESPCONN_ABRT:{

				os_printf("[Tips_socketTCP_B]: Connect fail cause abnormal .\n");
				os_printf("[Tips_socketTCP_B]:TCP reconnect Result is :%d\n", espconn_connect(&infoTemp_connTCP_remoteB));
			}break;
		case ESPCONN_RST:{

				os_printf("[Tips_socketTCP_B]: Connect fail cause reset .\n");
				os_printf("[Tips_socketTCP_B]:TCP reconnect Result is :%d\n", espconn_connect(&infoTemp_connTCP_remoteB));
			}break;
		case ESPCONN_CLSD:{

				os_printf("[Tips_socketTCP_B]: Connect fail cause trying fault .\n");
			}break;
		case ESPCONN_CONN:{

				os_printf("[Tips_socketTCP_B]: Connect fail cause normal .\n");
			}break;
//		case ESPCONN_HANDSHAKE:{

//				os_printf("[Tips_socketTCP_B]: Connect fail cause sll handshake fault .\n");
//			}break;
//		case ESPCONN_PROTO_MSG:{

//				os_printf("[Tips_socketTCP_B]: Connect fail cause sll message fault .\n");
//			}break;

		default:{

				os_printf("[Tips_socketTCP_B]: Connect fail cause fault unknown: %d .\n", err);
			}break;
	}
}

LOCAL void ICACHE_FLASH_ATTR
myTCP_DNSfound_funCB(const char *name, ip_addr_t *ipaddr, void *arg){

	struct espconn *pespconn = (struct espconn*)arg;

	if(ipaddr == NULL){

		os_printf("[Tips_socketTCP_B]: user dns found NULL\n");
	}

	os_printf("[Tips_socketTCP_B]: dns found is: %d.%d.%d.%d\n", *((uint8 *)&ipaddr->addr + 0), 
															  *((uint8 *)&ipaddr->addr + 1),
															  *((uint8 *)&ipaddr->addr + 2),
															  *((uint8 *)&ipaddr->addr + 3));
	
	if(tcp_remoteB_serverIP.addr == 0 && ipaddr->addr != 0){

		os_timer_disarm(&timer_tcpConnectDetect_B);
		
		tcp_remoteB_serverIP.addr = ipaddr->addr;
		memcpy(infoTemp_connTCP_remoteB.proto.tcp->remote_ip, &ipaddr->addr, 4);
		infoTemp_connTCP_remoteB.proto.tcp->remote_port = 80;
		infoTemp_connTCP_remoteB.proto.tcp->local_port = espconn_port();
		
		espconn_regist_connectcb(&infoTemp_connTCP_remoteB, myTCP_remoteBCallback_Connect);
		espconn_regist_reconcb(&infoTemp_connTCP_remoteB, myTCP_remoteBCallback_reConnect);

		os_printf("[Tips_socketTCP_B]: DNS connect Result is :%d\n", espconn_connect(&infoTemp_connTCP_remoteB));
	}
}

LOCAL void ICACHE_FLASH_ATTR
tcpConnectDetect_B_funCB(void *para){

	const uint8 myServer_IPaddr[4] = {10,0,0,218};
	struct ip_info ipConfig_info;

	os_timer_disarm(&timer_tcpConnectDetect_B);

	wifi_get_ip_info(STATION_IF, &ipConfig_info);
	if(wifi_station_get_connect_status() == STATION_GOT_IP && ipConfig_info.ip.addr != 0){

		os_printf("[Tips_socketTCP_B]: connect to router and got IP!!! \n");
		
		infoTemp_connTCP_remoteB.proto.tcp = &ssdp_tcp_remoteB;	//建立服务器TCP链接
		infoTemp_connTCP_remoteB.type = ESPCONN_TCP;
		infoTemp_connTCP_remoteB.state = ESPCONN_NONE;
		
#ifdef DNS_ENABLE_TCPB		

		tcp_remoteB_serverIP.addr = 0;
		espconn_gethostbyname(&infoTemp_connTCP_remoteB, NET_DOMAIN, &tcp_remoteB_serverIP, myTCP_DNSfound_funCB);

		os_timer_setfn(&timer_tcpConnectDetect_B, (os_timer_func_t *)myTCP_DNSfound_funCB, &infoTemp_connTCP_remoteB);
		os_timer_arm(&timer_tcpConnectDetect_B, 100, 0);
		
#else

		memcpy(infoTemp_connTCP_remoteB.proto.tcp->remote_ip, myServer_IPaddr, 4);
		infoTemp_connTCP_remoteB.proto.tcp->remote_port = 6000;		
		infoTemp_connTCP_remoteB.proto.tcp->local_port = espconn_port();

		espconn_regist_connectcb(&infoTemp_connTCP_remoteB, myTCP_remoteBCallback_Connect);
		espconn_regist_reconcb(&infoTemp_connTCP_remoteB, myTCP_remoteBCallback_reConnect);

		os_printf("[Tips_socketTCP_B]:TCP connect Result is :%d\n", espconn_connect(&infoTemp_connTCP_remoteB));
#endif
	}
	else{

		os_timer_setfn(&timer_tcpConnectDetect_B, tcpConnectDetect_B_funCB, NULL);
		os_timer_arm(&timer_tcpConnectDetect_B, 100, 0);
	}
}

STATUS ICACHE_FLASH_ATTR
TCPremoteB_datsSend(u8 dats[], u16 datsLen){

	if(tcp_remoteB_connFLG){

		espconn_sent(&infoTemp_connTCP_remoteB, dats, datsLen);
		return OK;
	}
	else{

		return FAIL;
	}
}

void ICACHE_FLASH_ATTR
tcpRemote_B_connectStart(void){

	os_timer_disarm(&timer_tcpConnectDetect_B);
	os_timer_setfn(&timer_tcpConnectDetect_B, (os_timer_func_t *)tcpConnectDetect_B_funCB, NULL);
	os_timer_arm(&timer_tcpConnectDetect_B, 100, 0);
}




































