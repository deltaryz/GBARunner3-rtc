# GBARunner3-rtc

This is an implementation of real-time clock support into GBARunner3 for DSi
(cache-hicode branch).

Tested with **Pokémon Ruby**, **Sapphire**, and **Emerald** on a Nintendo DSi
system running TWiLightMenu++ v27.23.0 and nds-bootstrap v2.15.0.

Creating and loading saves works, no battery error. Time will advance as
expected, including when the system is off.

_Compatibility with other games not verified._

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

Pokemon Ruby/Sapphire/Emerald access a Seiko S-3511A real-time clock via GBA
GPIO registers at 0x080000C4-0x080000C8. Without this hardware, the games
display "The internal battery has run dry" on every boot and time-based events
(berry growth, tides, etc.) do not function.

This patch implements full S-3511A GPIO emulation:

- GbaRtcTime.h: shared BCD time struct passed via IPC
- GbaGpio.h/.c: complete S-3511A serial state machine
  - GPIO state struct placed in DTCM for fast access
  - All logic in EWRAM section (avoids ITCM size constraints)
  - Handles CS/SCK/SIO edge detection, MSB-first command reception, LSB-first
    data exchange, 7-byte datetime, 3-byte time, and 1-byte control register
    commands
  - On each DATETIME or TIME read command, issues a live sysipc_getDatetime()
    call to ARM7 before clocking out the response, so the in-game clock advances
    in real time rather than serving a frozen boot-time snapshot
  - Uses a 32-byte-aligned EWRAM BSS buffer for the IPC refresh (required by the
    ARM7 pointer encoding scheme)
- SystemIpcCommand.h: add SYSTEM_IPC_CMD_GET_DATETIME
- arm7 SystemIpcService.cpp: new IPC handler reads DS hardware RTC via
  rtc_readDateTime() and writes raw BCD into the ARM9-supplied buffer (pointer
  recovered as (data>>4)<<5)
- arm9 SystemIpc.h/.cpp: sysipc_getDatetime(ptr) sends ptr>>5 as IPC payload and
  waits for ARM7 acknowledgement. SystemIpc.h gains extern "C" guards so the C++
  symbol is reachable from GbaGpio.c (a C translation unit)
- main.cpp: at boot, fetch DS time via IPC then gpio_init() before EWRAM is
  zeroed (sRtcTime is 32-byte aligned)
- MemoryStore16.s: intercept writes to 0x080000C4-C8 in memu_store16Rom; use
  register-indirect BLX since ITCM to EWRAM exceeds the 32MB BL range limit
- MemoryLoadStoreTables.s + MemoryLoadStoreWordTables.s: reroute load16 dispatch
  entry 0x08 from memu_load16Rom to memu_load16RomHi (which contains the GPIO
  read intercept). Direct modification of memu_load16Rom was not possible as the
  linker script constrains MemoryLoadRom.o to 112 bytes. The bic
  r9,r8,#0x06000000 in memu_load16RomHi is a no-op for 0x08-range addresses
  (bits 25:24 are already zero).
- MemoryLoad16.s: memu_load16RomHi intercepts reads from 0x080000C4-C8 and
  returns virtual pin state from GPIO struct

# DISCLAIMER

This code was written 100% by Claude Sonnet/Opus via Claude Code. This is not a
human-authored implementation.

I have not submitted a PR for this reason. Consider this a "proof of concept".

While it has been functional for me in my limited testing, **USE AT YOUR OWN
RISK**
