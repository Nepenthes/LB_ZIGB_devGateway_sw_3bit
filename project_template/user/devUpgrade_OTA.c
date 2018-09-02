#include "devUpgrade_OTA.h"

#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "espressif/espconn.h"
#include "espressif/airkiss.h"
#include "upgrade.h"

#include "stdio.h"
#include "string.h"

#include "datsManage.h"

xQueueHandle xMsgQ_devUpgrade;

LOCAL xTaskHandle pxTaskHandle_upgradeOTA;

LOCAL struct client_conn_param clientUpgrade_param;
/*---------------------------------------------------------------------------------------------*/

LOCAL void ICACHE_FLASH_ATTR
funDevUpgrade_rsp(void *arg){

    struct upgrade_server_info *server = arg;

    if (server->upgrade_flag == true) {
		
        os_printf("upgarde_successfully\n");

    } else {
    
        os_printf("upgrade_failed\n");
    }

    if(server != NULL){

		if(server->url != NULL){

			free(server->url);
			server->url = NULL;
		}
		
        free(server);
        server = NULL;
    }
}

LOCAL void ICACHE_FLASH_ATTR
devUpgrade_begin(struct upgrade_server_info *infoServer){

	u8 firwareBin_info[40] = {0}; //firewareNew_test.bin
//	u8 *url_upgrade = "GET /sm/image/firewareNew_test.bin HTTP/1.1\r\nAccept:*/*\r\nHost:www.lanbonserver.com\r\nConnection:Keep-Alive\r\n\r\n\0";

	infoServer->check_cb = funDevUpgrade_rsp;
	infoServer->check_times = 20000;
	infoServer->sockaddrin.sin_addr.s_addr = inet_addr("47.52.5.108");
	infoServer->sockaddrin.sin_port = htons(80);
	infoServer->sockaddrin.sin_family = AF_INET;

	if (infoServer->url == NULL) {
		
	  infoServer->url = (uint8 *)os_zalloc(512 * sizeof(uint8));
	}

	if (system_upgrade_userbin_check() == UPGRADE_FW_BIN1) {
	  memcpy(firwareBin_info, "firewareNew_test_Z2.bin", 23);
	} else if (system_upgrade_userbin_check() == UPGRADE_FW_BIN2) {
	  memcpy(firwareBin_info, "firewareNew_test_Z1.bin", 23);
	}

	os_printf("%s\n", firwareBin_info);

	sprintf(infoServer->url, "GET /sm/image/%s HTTP/1.1\r\nAccept:*/*\r\nHost:www.lanbonserver.com\r\nConnection:Keep-Alive\r\n\r\n\0", firwareBin_info);

	if(true == system_upgrade_start(infoServer)){

		os_printf("upgrade start!!!\n", firwareBin_info);
	}
}

/***********************************************************ss*******************
 * FunctionName : user_esp_platform_upgrade_begin
 * Description  : Processing the received data from the server
 * Parameters   : pespconn -- the espconn used to connetion with the host
 *                server -- upgrade param
 * Returns      : none
*******************************************************************************/
//LOCAL void ICACHE_FLASH_ATTR
//devUpgrade_begin(struct client_conn_param *pclient_param, struct upgrade_server_info *server)
//{
//    uint8 user_bin[40] = {0};

//    server->pclient_param=(void*)pclient_param;

//    struct sockaddr iname;
//    struct sockaddr_in *piname= (struct sockaddr_in *)&iname;
//	
//    int len = sizeof(iname);
//    getpeername(pclient_param->sock_fd, &iname, (socklen_t *)&len);
//    
//    bzero(&server->sockaddrin,sizeof(struct sockaddr_in));
//    
//    server->sockaddrin.sin_family = AF_INET;
//    server->sockaddrin.sin_addr= piname->sin_addr;

//    server->sockaddrin.sin_port = htons(80);

//    server->check_cb = funDevUpgrade_rsp;
//    server->check_times = 20000;/*rsp once finished*/

//    if (server->url == NULL) {
//        server->url = (uint8 *)zalloc(512);
//    }

//    if (system_upgrade_userbin_check() == UPGRADE_FW_BIN1) {
//        memcpy(user_bin, "firewareNew_test.bin", 20);
//    } else if (system_upgrade_userbin_check() == UPGRADE_FW_BIN2) {
//        memcpy(user_bin, "firewareNew_test.bin", 20);
//    }

//    sprintf(server->url, "GET /sm/image/%s HTTP/1.1\r\nAccept:*/*\r\nHost:www.lanbonserver.com\r\nConnection:Keep-Alive\r\n\r\n\0",
//            user_bin);//  IPSTR  IP2STR(server->sockaddrin.sin_addr.s_addr)
//    os_printf("%s\n",server->url);

//    if (system_upgrade_start(server) == true) 
//    {
//        os_printf("upgrade is already started\n");
//    }
//}

LOCAL void ICACHE_FLASH_ATTR
devUpgrade_Test(void){

	if(UPGRADE_FLAG_START == system_upgrade_flag_check()){

		os_printf("upgrade already start!!!\n");
	
	}else{

		struct upgrade_server_info *server = (struct upgrade_server_info *)os_zalloc(sizeof(struct upgrade_server_info));
		
		devUpgrade_begin(server);
	}
}

LOCAL void ICACHE_FLASH_ATTR
upgradeDetecting_task(void *pvParameters){

	u8 rptr_S2Z;	//通信线程消息队列
	portBASE_TYPE xMsgQ_rcvResult = pdFALSE;

	for(;;){

		xMsgQ_rcvResult = xQueueReceive(xMsgQ_devUpgrade, (void *)&rptr_S2Z, 20);
		if(xMsgQ_rcvResult == pdTRUE){

			switch(rptr_S2Z){

				case DEVUPGRADE_PUSH:{

						devUpgrade_Test();
						
					}break;

				case DEVUPGRADE_REBOOT:{

						system_upgrade_reboot();
						
					}break;

				default:{

					}break;
			}
		}else{

			vTaskDelay(10);
		}
		vTaskDelay(100);
	}
}

void ICACHE_FLASH_ATTR
devUpgradeDetecting_Start(void){

	portBASE_TYPE xReturn = pdFAIL;

	xMsgQ_devUpgrade = xQueueCreate(2, sizeof(u8));
	
	xReturn = xTaskCreate(upgradeDetecting_task, "upgradeDetecting_task", 256, (void *)NULL, 3, &pxTaskHandle_upgradeOTA);
	os_printf("\npxTaskHandle_upgradeOTA is %d\n", pxTaskHandle_upgradeOTA);
}























