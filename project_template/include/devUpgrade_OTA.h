#ifndef __DEVUPGRADE_OTA_H__
#define __DEVUPGRADE_OTA_H__

#include "esp_common.h"

#include "freertos/queue.h"

/* NOTICE---this is for 512KB spi flash.
 * you can change to other sector if you use other size spi flash. */
#define ESP_PARAM_START_SEC     0x7D

#define packet_size   (2 * 1024)

#define token_size 41

#define DEVUPGRADE_PUSH			0x11
#define DEVUPGRADE_REBOOT		0xA0

struct esp_platform_saved_param {
    uint8 devkey[40];
    uint8 token[40];
    uint8 activeflag;
    uint8 tokenrdy;
    uint8 pad[2];
};

enum {
    DEVICE_GOT_IP=39,
    DEVICE_CONNECTING,
    DEVICE_ACTIVE_DONE,
    DEVICE_ACTIVE_FAIL,
    DEVICE_CONNECT_SERVER_FAIL
};

struct dhcp_client_info {
    ip_addr_t ip_addr;
    ip_addr_t netmask;
    ip_addr_t gw;
    uint8 flag;
    uint8 pad[3];
};

enum{
    AP_DISCONNECTED = 0,
    AP_CONNECTED,
    DNS_SUCESSES,
    DNS_FAIL,
};

struct client_conn_param {
	
    int32 sock_fd;
};

void devUpgradeDetecting_Start(void);

#endif
