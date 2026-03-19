# GBARunner3-rtc

This is an implementation of real-time clock support into GBARunner3 for DSi
(cache-hicode branch).

Tested with **Pokémon Ruby**, **Sapphire**, and **Emerald** on a Nintendo DSi
system running TWiLightMenu++ v27.23.0 and nds-bootstrap v2.15.0.

Creating and loading saves works, no battery error. Time will advance as
expected, including when the system is off.

_Compatibility with other games has not been verified_, but most should be
supported.

## Build Instructions

```bash
git checkout feature/cache-hicode
git submodule update --init

docker run --rm -v $(pwd):/gba devkitpro/devkitarm:20230928 bash -c "
  cd /gba/code
  make -C libs/libtwl
  make -C bootstrap
"
```

Output will be at: `code/bootstrap/GBARunner3.nds`

## SD Card Setup

Add the following files to your SD:

- GBARunner3.nds
  - `_nds/TWiLightMenu/emulators/GBARunner3.nds`
- GBA BIOS (bios.bin)
  - `_gba/bios.bin`
- configs folder from repository
  - `_gba/configs`

Edit `_nds/TWiLightMenu/settings.ini`, add the following line to the bottom of
the `[SRLOADER]` section:

- `GBARUNNER3_TEST = 1`

TWiLightMenu++ should now be able to run .gba roms directly with working RTC.

## Technical Details

GBA games that use real-time hardware access a Seiko S-3511A clock via GPIO
registers at 0x080000C4-0x080000C8. Without this hardware, time-dependent games
display errors like "The internal battery has run dry" and time-based events
(berry growth, tides, day/night cycles) do not function.

This patch implements full S-3511A GPIO emulation with two-mode time tracking:

**Files added/modified:**

- `GbaRtcTime.h`: shared packed BCD datetime struct for ARM7/ARM9 IPC
- `GbaRtcCalendar.c`: BCD↔decimal conversion and calendar math helpers (split
  into a separate translation unit to avoid a link-order issue that causes vrama
  overflow when added to GbaGpio.c directly)
- `GbaGpio.h/.c`: complete S-3511A serial state machine
  - GPIO state struct placed in DTCM for fast access
  - All logic in EWRAM section (avoids ITCM size constraints)
  - Handles CS/SCK/SIO edge detection, command reception, and LSB-first data
    exchange for 7-byte datetime, 3-byte time, and 1-byte control register
    commands
  - **Two-mode time model:**
    - _Passthrough_ (default, `rtcTimeSet = false`): reads forward DS system
      clock directly — works for Pokémon RSE and any read-only RTC game
    - _Offset mode_ (activated on first write): records
      `rtcOffset = game_written_seconds − ds_seconds` at write time; every
      subsequent read returns `ds_seconds + rtcOffset` so the clock advances in
      real time from whatever the game set — handles games like Rockman EXE that
      write time at startup (see mGBA #240)
  - Uses a 32-byte-aligned EWRAM BSS buffer for IPC (required by the ARM7
    pointer encoding scheme)
- `SystemIpcCommand.h`: adds `SYSTEM_IPC_CMD_GET_DATETIME`
- `arm7 SystemIpcService.cpp`: IPC handler reads DS RTC via `rtc_readDateTime()`
  and writes BCD into the ARM9-supplied buffer (pointer recovered as
  `(data>>4)<<5`)
- `arm9 SystemIpc.h/.cpp`: `sysipc_getDatetime(ptr)` sends `ptr>>5` as IPC
  payload and waits for ARM7 acknowledgement; `extern "C"` guards allow the C++
  symbol to be linked from GbaGpio.c
- `main.cpp`: fetches DS time via IPC then calls `gpio_init()` at boot
- `MemoryStore16.s`: intercepts writes to 0x080000C4-C8; uses register-indirect
  BLX (ITCM→EWRAM exceeds the 32 MB BL range limit)
- `MemoryLoadStoreTables.s` + `MemoryLoadStoreWordTables.s`: reroutes load16
  dispatch entry 0x08 to `memu_load16RomHi` (which contains the GPIO read
  intercept)
- `MemoryLoad16.s`: `memu_load16RomHi` intercepts reads from 0x080000C4-C8 and
  returns virtual pin state from the GPIO struct

# DISCLAIMER

This code was written 100% by Claude Sonnet/Opus via Claude Code. This is not a
human-authored implementation.

I have not submitted a PR for this reason. Consider this a "proof of concept".

While it has been functional for me in my limited testing, **USE AT YOUR OWN
RISK**
