#pragma once
#include "GbaRtcTime.h"

#ifdef __cplusplus
extern "C" {
#endif

void sysipc_setTopBacklight(bool enabled);
void sysipc_setBottomBacklight(bool enabled);
void sysipc_getDatetime(gba_rtc_time_t* dst);

#ifdef __cplusplus
}
#endif
