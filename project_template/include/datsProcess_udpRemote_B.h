#ifndef __DATSPROCESS_UDPREMOTE_B_H__
#define __DATSPROCESS_UDPREMOTE_B_H__

#include "esp_common.h"

void mySocketUDPremote_B_buildInit(void);
void mySocketUDPremote_B_serverChange(u8 remoteIP_toChg[4]);
void UDPremoteB_datsSend(u8 dats[], u16 datsLen);

#endif