#pragma once

/// @brief Packed RTC datetime, all fields in BCD except weekDay.
///        Matches the 7-byte layout of the S-3511A chip used in GBA carts.
typedef struct
{
    u8 year;     ///< Year 20xx, last 2 digits BCD  (e.g. 0x26 = 2026)
    u8 month;    ///< Month 01-12 BCD
    u8 day;      ///< Day   01-31 BCD
    u8 weekDay;  ///< Day of week 0-6 (counter, not tied to a specific day)
    u8 hour;     ///< Hour 00-23 BCD (24-hour mode)
    u8 minute;   ///< Minute 00-59 BCD
    u8 second;   ///< Second 00-59 BCD
} gba_rtc_time_t;
