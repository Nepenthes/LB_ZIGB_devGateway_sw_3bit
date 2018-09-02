#ifndef __DATSPROCESS_TCPREMOTE_A_H__
#define __DATSPROCESS_TCPREMOTE_A_H__

#include "esp_common.h"

void tcpRemote_A_connectStart(void);
STATUS TCPremoteA_datsSend(u8 dats[], u16 datsLen);

#endif
