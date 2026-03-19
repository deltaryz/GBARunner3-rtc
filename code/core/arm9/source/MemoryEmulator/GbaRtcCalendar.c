// GbaRtcCalendar.c -- BCD/calendar helpers for RTC offset arithmetic.
// Placed in a separate translation unit so that GbaGpio.c's object file
// stays close to its baseline size, avoiding the link-order shift that
// pushes JitThumb.o earlier and overflows vrama.
//
// All functions go to .ewram.  sDaysInMonth goes to .rodata (12 bytes).

#include <nds.h>

// Days in each month (non-leap).  In .rodata (12 bytes, safe for vrama).
const u8 rtcDaysInMonth[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

__attribute__((section(".ewram"))) u8 rtcBcdToDec(u8 bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0xF);
}

__attribute__((section(".ewram"))) u8 rtcDecToBcd(u8 dec)
{
    return ((dec / 10) << 4) | (dec % 10);
}

__attribute__((section(".ewram"))) u32 rtcToSeconds(const u8 t[7])
{
    u32 y   = rtcBcdToDec(t[0]);
    u32 m   = rtcBcdToDec(t[1]);
    u32 d   = rtcBcdToDec(t[2]);
    u32 h   = rtcBcdToDec(t[4]);
    u32 min = rtcBcdToDec(t[5]);
    u32 sec = rtcBcdToDec(t[6]);

    u32 days = 0;
    for (u32 i = 0; i < y; i++)
        days += (i % 4 == 0) ? 366 : 365;
    for (u32 i = 1; i < m; i++)
    {
        days += rtcDaysInMonth[i - 1];
        if (i == 2 && (y % 4 == 0))
            days += 1;
    }
    days += d - 1;

    return days * 86400u + h * 3600u + min * 60u + sec;
}

__attribute__((section(".ewram"))) void rtcFromSeconds(u32 secs, u8 t[7])
{
    u32 days = secs / 86400u;
    u32 rem  = secs % 86400u;

    u32 y = 0;
    for (;;)
    {
        u32 diy = (y % 4 == 0) ? 366 : 365;
        if (days < diy) break;
        days -= diy;
        y++;
    }

    u32 m = 1;
    for (;;)
    {
        u32 dim = rtcDaysInMonth[m - 1];
        if (m == 2 && (y % 4 == 0))
            dim++;
        if (days < dim) break;
        days -= dim;
        m++;
    }

    t[0] = rtcDecToBcd((u8)y);
    t[1] = rtcDecToBcd((u8)m);
    t[2] = rtcDecToBcd((u8)(days + 1));
    t[4] = rtcDecToBcd((u8)(rem / 3600u));
    t[5] = rtcDecToBcd((u8)((rem % 3600u) / 60u));
    t[6] = rtcDecToBcd((u8)(rem % 60u));
}
