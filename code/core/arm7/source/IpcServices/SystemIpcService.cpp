#include "common.h"
#include <libtwl/spi/spiPmic.h>
#include <libtwl/sio/sioRtc.h>
#include "GbaRtcTime.h"
#include "SystemIpcService.h"

static u8 rtcBcd(u32 value)
{
    return (u8)((((value / 10) % 10) << 4) | (value % 10));
}

void SystemIpcService::HandleMessage(u32 data)
{
    switch (data & 0xF)
    {
        case SYSTEM_IPC_CMD_SET_TOP_BACKLIGHT:
        {
            pmic_setTopBacklightEnable((data >> 4) & 1);
            SendResponseMessage(0);
            break;
        }
        case SYSTEM_IPC_CMD_SET_BOTTOM_BACKLIGHT:
        {
            pmic_setBottomBacklightEnable((data >> 4) & 1);
            SendResponseMessage(0);
            break;
        }
        case SYSTEM_IPC_CMD_GET_DATETIME:
        {
            // ARM9 passes a 32-byte-aligned pointer in upper bits
            gba_rtc_time_t* dst = (gba_rtc_time_t*)((data >> 4) << 5);

            rtc_datetime_t dt;
            rtc_readDateTime(&dt);

            // RTC time is already in BCD (the DS RTC stores values in BCD).
            // hour field: bits 5:0 = BCD hour, bits 7:6 = AM/PM. Strip flags.
            dst->year    = dt.date.year;
            dst->month   = dt.date.month;
            dst->day     = dt.date.monthDay;
            dst->weekDay = dt.date.weekDay;
            dst->hour    = dt.time.hour & 0x3F;
            dst->minute  = dt.time.minute;
            dst->second  = dt.time.second;

            SendResponseMessage(0);
            break;
        }
    }
}
