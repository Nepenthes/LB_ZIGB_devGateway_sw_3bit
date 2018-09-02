#ifndef __DATSPROCESS_UDPLOCAL_A_H__
#define __DATSPROCESS_UDPLOCAL_A_H__

#include "esp_common.h"

void mySocketUDPlocal_A_buildInit(void);
void UDPlocalA_datsSend(u8 dats[], u16 datsLen);

#endif

