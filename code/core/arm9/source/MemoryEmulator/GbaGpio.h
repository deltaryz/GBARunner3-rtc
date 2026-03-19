#pragma once

#include "GbaRtcTime.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Initialize GPIO emulation with the RTC datetime fetched at startup.
void gpio_init(const gba_rtc_time_t* time);

/// @brief Handle a 16-bit write to a GPIO register (0x080000C4/C6/C8).
/// @param address The GBA address (0x080000C4, 0x080000C6, or 0x080000C8).
/// @param value   The value to write.
void gpio_write16(u32 address, u16 value);

/// @brief Handle a 16-bit read from a GPIO register.
/// @param address The GBA address (0x080000C4, 0x080000C6, or 0x080000C8).
/// @return The current register value.
u16 gpio_read16(u32 address);

#ifdef __cplusplus
}
#endif
