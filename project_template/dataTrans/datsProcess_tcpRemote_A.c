#include "datsProcess_tcpRemote_A.h"

#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "espressif/espconn.h"
#include "espressif/airkiss.h"

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

	remot_info *pcon_info = NULL;

	if (pdata == NULL)return;
	
	printf_datsHtoA("[Tips_socketTCP_A]:", pdata, len);
}

LOCAL void ICACHE_FLASH_ATTR
myTCP_remoteACallback_dataSend(void *arg){

	os_printf("[Tips_socketTCP_A]: data sent successfully!!!\n");
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



