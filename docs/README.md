# W80x documentation

Register-level documentation for the WinnerMicro W80x family (W800, W801,
W806), in the style of a datasheet. It is written because the official
documents are mostly in Chinese, and the English register manual is a
machine translation that is often hard to follow.

Each chapter combines:

- **the register manual** (WM_W800 Register Manual v2.1), rewritten and
  checked against the register definitions in WinnerMicro's wm_iot_sdk;
- **what was found on hardware** (an HLK-W801-KIT-V1.1), where the manual is
  unclear, incomplete or wrong.

Notes are tagged by source: *(manual)*, *(SDK)* and *(hardware)*. Where the
manual contradicts itself, the chapter says so and shows both values.

## Contents

### MCU

| Chapter | Covers |
|---|---|
| [Clock and reset (CLK)](mcu/clock.md) | Clock tree, buses, CPU clock settings, peripheral clock gating and reset, reset cause |

## Conventions

- **Addresses** are absolute, written with a `_` between the 16-bit halves
  (`0x4000_0E00`). Registers list both the absolute address and the offset
  from the module base.
- **Bits** are written `high:low` (`23:16`), counted from bit 0 (the LSB).
- **Access:** RW read/write, RO read-only, WO write-only.
- **Reset** is the value after a chip reset. The application doesn't
  start from these values: secboot runs first and may change some registers.
- **SDK** columns name the matching `w80x.h` register field or bit macro.
