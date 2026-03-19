/// GBA S-3511A RTC GPIO emulation for GBARunner3.
/// Adapted from mGBA's src/gba/cart/gpio.c (MPL 2.0).
///
/// GPIO pins (all 4-bit, only bits 0-2 used for RTC):
///   bit 0 = SCK  (clock)
///   bit 1 = SIO  (serial data, bidirectional)
///   bit 2 = CS   (chip select, active high)
///   bit 3 = (unused)
///
/// RTC command byte (received LSB-first over serial):
///   bits 3:0 = magic, must be 0x6
///   bits 6:4 = command  (0=reset, 2=datetime 7B, 4=control 1B, 6=time 3B)
///   bit  7   = 1=read from RTC, 0=write to RTC

#include "common.h"
#include "GbaGpio.h"
#include "../SystemIpc.h"

// ---- constants -------------------------------------------------------

#define GPIO_REG_DATA       0x080000C4u
#define GPIO_REG_DIRECTION  0x080000C6u
#define GPIO_REG_CONTROL    0x080000C8u

#define RTC_CMD_RESET    0
#define RTC_CMD_DATETIME 2
#define RTC_CMD_CONTROL  4
#define RTC_CMD_TIME     6

static const int RTC_BYTES[8] = { 0, 0, 7, 0, 1, 0, 3, 0 };

// ---- calendar helpers (defined in GbaRtcCalendar.c) ------------------
extern u8  rtcBcdToDec(u8 bcd);
extern u8  rtcDecToBcd(u8 dec);
extern u32 rtcToSeconds(const u8 t[7]);
extern void rtcFromSeconds(u32 secs, u8 t[7]);

// ---- state (placed in DTCM for fast access) --------------------------

typedef struct
{
    // GPIO layer
    u16 gpioData;       ///< value readable at 0x080000C4
    u16 gpioDirection;  ///< value at 0x080000C6
    u16 gpioControl;    ///< bit 0 = readable

    u8 writeLatch;      ///< last written 4-bit value
    u8 pinState;        ///< current 4-bit pin state (includes RTC output)
    u8 direction;       ///< output direction mask (4-bit)

    // RTC state machine
    int  bitsRead;
    int  bits;
    int  commandActive;
    bool sckEdge;
    bool sioOutput;
    u8   rtcCommand;    ///< command byte as received (LSB-first)
    int  bytesRemaining;
    u8   rtcTime[7];    ///< BCD: year,month,day,weekDay,hour,min,sec
    u8   rtcControl;    ///< RTC control register
    bool rtcTimeSet;    ///< true if game wrote time (use offset mode on reads)
    s32  rtcOffset;     ///< seconds: game_written_time - ds_time at write moment
} GpioState;

static GpioState sGpio __attribute__((section(".dtcm")));

// 32-byte-aligned buffer for live RTC refresh via IPC
static gba_rtc_time_t sRtcRefreshBuf __attribute__((section(".ewram.bss"), aligned(32)));

/// Refresh rtcTime[] from DS RTC, applying offset if game has written time.
static __attribute__((section(".ewram"))) void rtcRefreshForRead(void)
{
    sysipc_getDatetime(&sRtcRefreshBuf);

    if (!sGpio.rtcTimeSet)
    {
        // No game write yet -- pass DS time through directly
        sGpio.rtcTime[0] = sRtcRefreshBuf.year;
        sGpio.rtcTime[1] = sRtcRefreshBuf.month;
        sGpio.rtcTime[2] = sRtcRefreshBuf.day;
        sGpio.rtcTime[3] = sRtcRefreshBuf.weekDay;
        sGpio.rtcTime[4] = sRtcRefreshBuf.hour;
        sGpio.rtcTime[5] = sRtcRefreshBuf.minute;
        sGpio.rtcTime[6] = sRtcRefreshBuf.second;
    }
    else
    {
        // Offset mode: game_time = ds_time + offset
        u8 dsBcd[7];
        dsBcd[0] = sRtcRefreshBuf.year;
        dsBcd[1] = sRtcRefreshBuf.month;
        dsBcd[2] = sRtcRefreshBuf.day;
        dsBcd[3] = sRtcRefreshBuf.weekDay;
        dsBcd[4] = sRtcRefreshBuf.hour;
        dsBcd[5] = sRtcRefreshBuf.minute;
        dsBcd[6] = sRtcRefreshBuf.second;

        s32 adjusted = (s32)rtcToSeconds(dsBcd) + sGpio.rtcOffset;
        if (adjusted < 0) adjusted = 0;
        rtcFromSeconds((u32)adjusted, sGpio.rtcTime);
        sGpio.rtcTime[3] = sRtcRefreshBuf.weekDay; // preserve DS weekday
    }
}

// ---- GPIO / RTC helpers ----------------------------------------------

static __attribute__((section(".ewram"))) void outputPins(u8 pins)
{
    sGpio.pinState &= sGpio.direction;
    sGpio.pinState |= (pins & ~sGpio.direction & 0xF);
    if (sGpio.gpioControl & 1)
        sGpio.gpioData = sGpio.pinState;
}

static __attribute__((section(".ewram"))) u8 rtcOutput(void)
{
    u8 outByte = 0xFF;
    int cmd = (sGpio.rtcCommand >> 4) & 0x7;
    switch (cmd)
    {
        case RTC_CMD_CONTROL:
            outByte = sGpio.rtcControl;
            break;
        case RTC_CMD_DATETIME:
        case RTC_CMD_TIME:
            outByte = sGpio.rtcTime[7 - sGpio.bytesRemaining];
            break;
        default:
            break;
    }
    return (outByte >> sGpio.bitsRead) & 1;
}

static __attribute__((section(".ewram"))) void rtcBeginCommand(void)
{
    u8 cmd = (u8)sGpio.bits;
    // magic = bits 3:0, must be 0x6
    if ((cmd & 0xF) != 0x6)
        return;

    sGpio.rtcCommand = cmd;
    int command = (cmd >> 4) & 0x7;
    sGpio.bytesRemaining = RTC_BYTES[command];
    sGpio.commandActive = 1;

    // For DATETIME/TIME reads, refresh from DS RTC (with offset if applicable)
    if ((cmd & 0x80) && (command == RTC_CMD_DATETIME || command == RTC_CMD_TIME))
    {
        rtcRefreshForRead();
    }

    switch (command)
    {
        case RTC_CMD_RESET:
            sGpio.rtcControl = 0;
            sGpio.rtcTimeSet = false;
            break;
        default:
            break;
    }

    sGpio.bits = 0;
    sGpio.bitsRead = 0;
}

static __attribute__((section(".ewram"))) void rtcProcessByte(void)
{
    int command = (sGpio.rtcCommand >> 4) & 0x7;
    switch (command)
    {
        case RTC_CMD_CONTROL:
            sGpio.rtcControl = (u8)sGpio.bits;
            break;
        case RTC_CMD_DATETIME:
        case RTC_CMD_TIME:
            // Store game-written time byte
            sGpio.rtcTime[7 - sGpio.bytesRemaining] = (u8)sGpio.bits;
            // On last byte: compute offset so future reads advance from this time
            if (sGpio.bytesRemaining == 1)
            {
                sysipc_getDatetime(&sRtcRefreshBuf);
                u8 dsBcd[7];
                dsBcd[0] = sRtcRefreshBuf.year;
                dsBcd[1] = sRtcRefreshBuf.month;
                dsBcd[2] = sRtcRefreshBuf.day;
                dsBcd[3] = sRtcRefreshBuf.weekDay;
                dsBcd[4] = sRtcRefreshBuf.hour;
                dsBcd[5] = sRtcRefreshBuf.minute;
                dsBcd[6] = sRtcRefreshBuf.second;
                // For TIME writes (3 bytes = h,m,s only), fill date from DS RTC
                if (command == RTC_CMD_TIME)
                {
                    sGpio.rtcTime[0] = dsBcd[0];
                    sGpio.rtcTime[1] = dsBcd[1];
                    sGpio.rtcTime[2] = dsBcd[2];
                    sGpio.rtcTime[3] = dsBcd[3];
                }
                sGpio.rtcOffset = (s32)rtcToSeconds(sGpio.rtcTime) - (s32)rtcToSeconds(dsBcd);
                sGpio.rtcTimeSet = true;
            }
            break;
        default:
            break;
    }
    sGpio.bits = 0;
    sGpio.bitsRead = 0;
    --sGpio.bytesRemaining;
    if (sGpio.bytesRemaining <= 0)
        sGpio.bytesRemaining = RTC_BYTES[(sGpio.rtcCommand >> 4) & 0x7];
}

static __attribute__((section(".ewram"))) void rtcReadPins(void)
{
    // Keep output pins low (SCK, CS, unused); preserve SIO output bit only
    outputPins(sGpio.pinState & 2);

    // CS low -> reset
    if (!(sGpio.pinState & 4))
    {
        sGpio.bitsRead = 0;
        sGpio.bytesRemaining = 0;
        sGpio.commandActive = 0;
        sGpio.bits = 0;
        sGpio.sckEdge = true;
        sGpio.sioOutput = true;
        outputPins(2);
        return;
    }

    bool reading = (sGpio.rtcCommand & 0x80) != 0;

    if (!sGpio.commandActive)
    {
        outputPins(2);
        // Capture SIO on SCK low
        if (!(sGpio.pinState & 1))
        {
            sGpio.bits &= ~(1 << sGpio.bitsRead);
            sGpio.bits |= ((sGpio.pinState & 2) >> 1) << sGpio.bitsRead;
        }
        // Count bit on SCK rising edge
        if (!sGpio.sckEdge && (sGpio.pinState & 1))
        {
            ++sGpio.bitsRead;
            if (sGpio.bitsRead == 8)
                rtcBeginCommand();
        }
    }
    else if (!reading)
    {
        outputPins(2);
        if (!(sGpio.pinState & 1))
        {
            sGpio.bits &= ~(1 << sGpio.bitsRead);
            sGpio.bits |= ((sGpio.pinState & 2) >> 1) << sGpio.bitsRead;
        }
        if (!sGpio.sckEdge && (sGpio.pinState & 1))
        {
            if ((((sGpio.bits >> sGpio.bitsRead) & 1) ^ ((sGpio.pinState & 2) >> 1)))
                sGpio.bits &= ~(1 << sGpio.bitsRead);
            ++sGpio.bitsRead;
            if (sGpio.bitsRead == 8)
                rtcProcessByte();
        }
    }
    else
    {
        // Read phase: output bit on SCK falling edge
        if (sGpio.sckEdge && !(sGpio.pinState & 1))
        {
            sGpio.sioOutput = rtcOutput();
            ++sGpio.bitsRead;
            if (sGpio.bitsRead == 8)
            {
                --sGpio.bytesRemaining;
                int cmd = (sGpio.rtcCommand >> 4) & 0x7;
                if (sGpio.bytesRemaining <= 0)
                    sGpio.bytesRemaining = RTC_BYTES[cmd];
                sGpio.bitsRead = 0;
            }
        }
        outputPins((u8)(sGpio.sioOutput << 1));
    }

    sGpio.sckEdge = !!(sGpio.pinState & 1);
}

// ---- public API ------------------------------------------------------

void gpio_init(const gba_rtc_time_t* time)
{
    sGpio.gpioData      = 0;
    sGpio.gpioDirection = 0;
    sGpio.gpioControl   = 0;
    sGpio.writeLatch    = 0;
    sGpio.pinState      = 0;
    sGpio.direction     = 0;
    sGpio.bitsRead      = 0;
    sGpio.bits          = 0;
    sGpio.commandActive = 0;
    sGpio.sckEdge       = true;
    sGpio.sioOutput     = true;
    sGpio.rtcCommand    = 0;
    sGpio.bytesRemaining = 0;
    sGpio.rtcControl    = 0x40; // 24-hour mode
    sGpio.rtcTimeSet    = false;
    sGpio.rtcOffset     = 0;

    sGpio.rtcTime[0] = time->year;
    sGpio.rtcTime[1] = time->month;
    sGpio.rtcTime[2] = time->day;
    sGpio.rtcTime[3] = time->weekDay;
    sGpio.rtcTime[4] = time->hour;
    sGpio.rtcTime[5] = time->minute;
    sGpio.rtcTime[6] = time->second;
}

__attribute__((section(".ewram"))) void gpio_write16(u32 address, u16 value)
{
    switch (address)
    {
        case GPIO_REG_DATA:
            sGpio.writeLatch = (u8)(value & 0xF);
            sGpio.pinState &= ~sGpio.direction;
            sGpio.pinState |= sGpio.writeLatch & sGpio.direction;
            rtcReadPins();
            break;
        case GPIO_REG_DIRECTION:
            sGpio.direction = (u8)(value & 0xF);
            sGpio.gpioDirection = value & 0xF;
            sGpio.pinState &= ~sGpio.direction;
            sGpio.pinState |= sGpio.writeLatch & sGpio.direction;
            rtcReadPins();
            break;
        case GPIO_REG_CONTROL:
            sGpio.gpioControl = value & 0x1;
            break;
        default:
            break;
    }

    // Reflect readable state into gpioData/Direction
    if (sGpio.gpioControl & 1)
        sGpio.gpioData = sGpio.pinState;
    else
        sGpio.gpioData = 0;
}

__attribute__((section(".ewram"))) u16 gpio_read16(u32 address)
{
    if (!(sGpio.gpioControl & 1))
        return 0;  // write-only mode

    switch (address)
    {
        case GPIO_REG_DATA:      return sGpio.gpioData;
        case GPIO_REG_DIRECTION: return sGpio.gpioDirection;
        case GPIO_REG_CONTROL:   return sGpio.gpioControl;
        default:                 return 0;
    }
}
