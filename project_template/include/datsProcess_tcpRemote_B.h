#ifndef __DATSPROCESS_TCPREMOTE_B_H__
#define __DATSPROCESS_TCPREMOTE_B_H__

#include "esp_common.h"

void tcpRemote_B_connectStart(void);
STATUS TCPremoteB_datsSend(u8 dats[], u16 datsLen);

#endif

