# SDIO host (SDH)

The SDIO host (manual chapter 13) is the controller for SD memory cards,
SDIO cards and MMC on the native SD bus: a clock line (CLK), a command line
(CMD) and 1 or 4 data lines (DAT0..DAT3). It sends commands and receives
their responses, generates and checks the CRCs, and moves data blocks
through a 512-byte buffer, either with the CPU or by DMA.

- **Base address:** `0x4000_0A00` (AHB, so its clock `hclk` is the CPU clock).
- **SDK:** `SDH_TypeDef` / `SDH` in [`w80x.h`](../../w80x/w80x.h), driver in
  [`w80x_sdh.h`](../../w80x/w80x_sdh.h) ([API](../api/sdh.md)), SD card
  backend in [`sdcard_sdio.c`](../../examples/lib/sdcard/sdcard_sdio.c), example
  [`examples/sd_fatfs`](../../examples/sd_fatfs/main.c) with
  `make SDCARD_BUS=sdio`.
- **Source:** WM_W800 Register Manual v2.1, chapter 13, checked against
  wm_iot_sdk (`wm_sdh_reg_struct.h`, `wm_hal_sdh.c`) and wm_sdk_w80x
  (`wm_sdio_host.c`). v3.0 (2022-08-20, English) has the same content in
  this chapter. The register list is translated badly in both, and the
  operation tables (3 and 5) only make sense once the columns are lined up
  again; see [Manual discrepancies](#manual-discrepancies).

> **Status:** nothing here has been run on hardware yet. The register values
> the driver writes are the ones wm_sdk_w80x writes, which agree with the
> manual's operation tables. Every point is from the manual or the SDKs;
> there are no *(hardware)* notes yet.

## Registers

All registers are 32 bits wide, but only the low 8 or 16 bits are used.

| Offset | Address | Name | SDK | Reset value | Description |
|---|---|---|---|---|---|
| `0x00` | `0x4000_0A00` | [MMC_CTRL](#mmc_ctrl) | `SDH->CTRL` | `0x0000_0045` ¹ | Bus mode, clock divider and speed, bus width, push-pull/open-drain |
| `0x04` | `0x4000_0A04` | [MMC_IO](#mmc_io) | `SDH->IO` | `0x0000_0000` | Starts a command, response, 8 clocks or one data block |
| `0x08` | `0x4000_0A08` | [MMC_BYTECNTL](#mmc_bytecntl) | `SDH->BYTECNT` | `0x0000_0200` | Bytes in a single-block transfer |
| `0x0C` | `0x4000_0A0C` | [MMC_TR_BLOCKCNT](#mmc_tr_blockcnt) | `SDH->TR_BLOCKS` | `0x0000_0000` | Blocks done in a multi-block transfer |
| `0x10` | `0x4000_0A10` | [MMC_CRCCTL](#mmc_crcctl) | `SDH->CRCCTL` | `0x0000_0000` | CRC enables, CRC error flags |
| `0x14` | `0x4000_0A14` | [CMD_CRC](#cmd_crc-dat_crcl-dat_crch) | `SDH->CMD_CRC` | `0x0000_0000` | CRC7 of the last command or response |
| `0x18` | `0x4000_0A18` | [DAT_CRCL](#cmd_crc-dat_crcl-dat_crch) | `SDH->DAT_CRCL` | — | Data CRC16, low byte |
| `0x1C` | `0x4000_0A1C` | [DAT_CRCH](#cmd_crc-dat_crcl-dat_crch) | `SDH->DAT_CRCH` | — | Data CRC16, high byte |
| `0x20` | `0x4000_0A20` | [MM_PORT](#mm_port) | `SDH->PORT` | `0x0000_007F` | Manual line levels (SPI mode), Ncr timeout |
| `0x24` | `0x4000_0A24` | [MMC_INT_MASK](#mmc_int_mask) | `SDH->INT_MASK` | `0x0000_0000` | 1 = interrupt enabled |
| `0x28` | `0x4000_0A28` | [CLR_MMC_INT](#clr_mmc_int) | `SDH->INT_SRC` | `0x0000_0000` | Status flags (also while masked); write 1 to clear |
| `0x2C` | `0x4000_0A2C` | [MMC_CARDSEL](#mmc_cardsel) | `SDH->CARDSEL` | `0x0000_0040` | Controller enable, card clock enable, 1 MHz time base |
| `0x30` | `0x4000_0A30` | [MMC_SIG](#mmc_sig) | `SDH->SIG` | `0x0000_00FF` | Line levels, last CRC status token |
| `0x34` | `0x4000_0A34` | [MMC_IO_MBCTL](#mmc_io_mbctl) | `SDH->MBCTL` | `0x0000_0010` | Multi-block start and direction, timeout units, clock edge |
| `0x38` | `0x4000_0A38` | [MMC_BLOCKCNT](#mmc_blockcnt) | `SDH->BLOCKCNT` | `0x0000_0001` | Blocks in a multi-block transfer |
| `0x3C` | `0x4000_0A3C` | [MMC_TIMEOUTCNT](#mmc_timeoutcnt) | `SDH->TIMEOUTCNT` | `0x0000_0040` | Data and busy timeout, in MBCTL units |
| `0x40`–`0x7C` | `0x4000_0A40` | [CMD_BUF0..15](#cmd_buf0cmd_buf15) | `SDH->CMD_BUF[16]` | `0x0000_0000` | One byte each: the command going out, the response coming in |
| `0x80` | `0x4000_0A80` | [BUF_CTRL](#buf_ctrl) | `SDH->BUF_CTL` | `0x0000_0002` | Data buffer: direction, DMA, DMA level, flush, full/empty |
| `0x84`–`0xFC` | | — | `RESERVED[31]` | | Reserved |
| `0x100`–`0x2FF` | `0x4000_0B00` | [DATA_BUF](#data_buf) | `SDH->DATA_BUF[128]` | — | 512-byte data buffer (128 words) |

¹ From the bit table: bit 0 (MMC mode) = 1, bit 2 (open drain) = 1, bit 6
(high speed) = 1. The register list gives no value for the whole register.
Not yet read on a chip *(to check)*.

**Contents:** [How it works](#how-it-works) · [Clock](#clock) ·
[Operations](#operations) · [Using it](#using-it) ·
[Registers](#register-details) · [Manual discrepancies](#manual-discrepancies)

**Legend:** **RW** read/write, **RO** read-only, **W1C** write 1 to clear,
**SC** clears itself when the operation ends. Notes are tagged by source:
*(manual)*, *(SDK)* (vendor SDKs or this SDK's code), *(to check)* not yet
seen on a chip.

---

## How it works

- **Clock, reset, interrupt, DMA.** The controller's clock gate is
  `CLK_GATE_SDIO_M` (SW_CLKG_EN bit 18) and its reset `CLK_RST_SDIO_M`
  (SW_RST_CTRL bit 27); see [Clock and reset](clock.md). Interrupt
  `SDIO_HOST_IRQn` (24). DMA request `DMA_REQ_SDIO_HOST` (12) *(SDK)*.
- **Pins.** W801/W806: CLK PA9, CMD PA10, DAT0..DAT3 PA11..PA14, all on
  AF1. W800: PB6..PB11 on AF2 *(SDK)*. CMD and DAT need pull-ups (10–100 kΩ
  in the SD specification). `sdh_pins()` turns on the internal ones.
- **Mode.** MMC_CTRL bit 0 picks the SD bus (1) or SPI (0); this SDK only
  uses the SD bus (SPI cards go through the SPI controller instead, see
  [`sdcard_spi.c`](../../examples/lib/sdcard/sdcard_spi.c)). Bit 8 picks an
  SDIO card instead of an SD memory card or MMC.
- **One operation at a time.** Writing MMC_IO (or MMC_IO_MBCTL for several
  blocks) starts an operation. Its trigger bit clears when it is done, and
  CLR_MMC_INT shows how it ended. The flags are set whether or not their
  interrupt is enabled, so polling works with MMC_INT_MASK = 0 *(manual)*.
- **Commands.** The command index and argument go into CMD_BUF0..4. The
  controller adds the start bit, the CRC7 and the end bit. The response
  comes back into the same bytes *(manual, SDK)*.
- **Data.** Blocks go through the 512-byte buffer at `0x100`. Single blocks
  use MMC_BYTECNTL for the length; multi-block transfers use MMC_BLOCKCNT.
  Both vendor SDKs only do multi-block transfers by DMA *(SDK)*.
- **CRCs.** With MMC_CRCCTL bits 7 and 6 set, the controller checks the
  CRC of every response and data block, and flags errors in CLR_MMC_INT
  *(manual)*. R2 (CID/CSD) and R3 (OCR) responses carry no valid CRC7 for
  the controller to check, so they always set the command CRC flag *(SDK)*.
- **Timeouts.** A missing response gives the Ncr timeout flag (MM_PORT
  bits 4:0 set how many clocks to wait). A data block or busy period that
  goes on too long gives the data timeout flag, after
  MMC_TIMEOUTCNT × the unit chosen in MMC_IO_MBCTL *(manual)*.

## Clock

The bus clock is a base clock divided by 2, 4, … 16 (MMC_CTRL bits 5:3,
[table 2](#mmc_ctrl)). MMC_CTRL bit 6 picks the base *(manual)*:

| Bit 6 | Base clock | Bus clock |
|---|---|---|
| 1 (reset) | `hclk` (the CPU clock) | hclk / (2 × (sel + 1)) |
| 0 | `clk1m` = hclk / ((MMC_CARDSEL[5:0] + 1) × 2) | clk1m / (2 × (sel + 1)) |

- **`clk1m` is meant to be 1 MHz**, but MMC_CARDSEL[5:0] is only 6 bits, so
  hclk / 128 is the slowest it gets. Above 128 MHz it runs faster than
  1 MHz: 1.875 MHz at 240 MHz, giving a 937 kHz bus clock at sel = 0 and
  117 kHz at sel = 7 *(manual)*. Both vendor SDKs write
  `hclk_MHz / 2 - 1` there without checking that it fits (119 at
  240 MHz, which runs into bits 6 and 7) *(SDK)*. `sdh_set_clock()` caps it
  at 63.
- **The same time base sets the timeout units** in MMC_IO_MBCTL, so above
  128 MHz the timeouts are shorter than their nominal values
  (1.875 / 1 = 1.9 times shorter at 240 MHz) *(manual, to check)*.
- **Rates at 240 MHz.** High speed: 120, 60, 40, 30, 24, 20, 17.1 and
  15 MHz. Low speed: 937, 469, 312, 234, 187, 156, 134 and 117 kHz. The
  SD specification allows up to 400 kHz while the card is being identified
  and 25 MHz afterwards (50 MHz in high-speed mode); the manual says the
  controller goes up to 50 MHz *(manual)*.
- **Changing the CPU clock** changes the bus clock too: set it again.
- wm_sdk_w80x runs at hclk / 6 (40 MHz at 240 MHz) once the card is
  identified. wm_iot_sdk switches to low speed before every command and
  back to high speed for data *(SDK)*. This SDK keeps one clock for both,
  20 MHz by default *(to check)*.

## Operations

### Commands and single blocks (MMC_IO)

Writing MMC_IO starts the operation described by its bits. Bit 2 starts a
command, response or 8 clocks; bit 0 starts a data block. The bit clears
itself when the operation is done *(manual, table 3)*:

| MMC_IO | Bits 7 6 5 4 3 2 1 0 | Operation | Done flag | SDK |
|---|---|---|---|---|
| `0x24` | `0 0 1 0 0 T 0 0` | 8 clocks, CMD high | CMD_DONE | `SDH_IO_8_CLOCKS` |
| `0x04` | `0 0 0 0 0 T 0 0` | Send the command, no response (CMD0) | CMD_DONE | `SDH_IO_CMD_NORESP` |
| `0x0C` | `0 0 0 0 1 T 0 0` | Receive a 48-bit response only | CMD_DONE | |
| `0x1C` | `0 0 0 1 1 T 0 0` | Receive a 136-bit response only | CMD_DONE | |
| `0x84` | `1 0 0 0 0 T 0 0` | Command, then 8 clocks | CMD_DONE | |
| `0x44` | `0 1 0 0 0 T 0 0` | Command and 48-bit response | CMD_DONE | `SDH_IO_CMD_SHORT` |
| `0x54` | `0 1 0 1 0 T 0 0` | Command and 136-bit response (CMD2, CMD9, CMD10) | CMD_DONE | `SDH_IO_CMD_LONG` |
| `0xC4` | `1 1 0 0 0 T 0 0` | Command, 48-bit response, then 8 clocks | CMD_DONE | |
| `0x03` | `x x x x x 0 1 T` | Read one data block of MMC_BYTECNTL bytes, then 8 clocks | DATA_DONE | `SDH_IO_DATA_READ \| SDH_IO_DATA_START` |
| `0x01` | `x x x x x 0 0 T` | Write one data block of MMC_BYTECNTL bytes, then 8 clocks | DATA_DONE | `SDH_IO_DATA_START` |

`T` is the trigger bit. The response comes back in CMD_BUF, see
[CMD_BUF0..15](#cmd_buf0cmd_buf15).

- **Command, then data.** For a single-block read or write (CMD17, CMD24,
  ACMD13, CMD6), send the command with `0x44` and wait for CMD_DONE, then
  start the data with `0x03` or `0x01`. For a write, fill the buffer before
  starting *(SDK)*.
- **8 clocks after a command.** wm_iot_sdk uses `0xC4` for commands without
  data and `0x44` for the ones followed by data (CMD17/18/24/25) *(SDK)*.
  wm_sdk_w80x always uses `0x44`. The SD specification needs 8 clocks after
  the last command before the clock may stop; this controller only drives
  the clock while an operation runs, so the difference shouldn't matter
  *(to check)*.

### Several blocks (MMC_IO_MBCTL)

| MMC_IO bits 7:6 | MBCTL bits 2 1 0 | Operation | SDK |
|---|---|---|---|
| `11` | `T 0 0` | Command, response, 8 clocks, then MMC_BLOCKCNT blocks written | |
| `11` | `T 1 0` | Command, response, 8 clocks, then MMC_BLOCKCNT blocks read | |
| `xx` | `0 0 T` | Write MMC_BLOCKCNT blocks (after CMD25) | `SDH_MBCTL_START` |
| `xx` | `0 1 T` | Read MMC_BLOCKCNT blocks (after CMD18) | `SDH_MBCTL_READ \| SDH_MBCTL_START` |

- Each block gives DATA_DONE; the end of the run gives MULTI_DONE, unless it
  timed out, which gives DATA_TIMEOUT instead *(manual)*.
- Both vendor SDKs send CMD18 or CMD25 with `0x44` first, then start the
  data with MBCTL bit 0 (`0xA3` to read, `0xA1` to write: units of 10 ms
  for both timeouts), by DMA only, and send CMD12 at the end *(SDK)*.

### By DMA

The order matters *(manual note 1, SDK)*:

1. BUF_CTRL = `0x4000` (DMA request masked, DMA off).
2. Set up the DMA channel with request 12: from `&DATA_BUF[0]` to memory for
   a read, from memory to `&DATA_BUF[0]` for a write, 32-bit words, the
   buffer address fixed. Start it.
3. BUF_CTRL = `0x404` for a read (DMA on, level 1 word) or `0xC20` for a
   write (DMA on, direction write, level 8 words).
4. MMC_BLOCKCNT = number of blocks, clear CLR_MMC_INT.
5. MMC_IO_MBCTL = `0xA3` (read) or `0xA1` (write).
6. Wait for MBCTL bit 0 to clear. BUF_CTRL bit 10 also clears itself.

The DMA moves at most 65535 bytes per run in this SDK (`DMA_MAX_LEN`), so
127 blocks (`SDH_DMA_MAX_BLOCKS`).

## Using it

```c
#include "w80x_sdh.h"

sdh_pins(PA9, PA10, PA11, PA12, PA13, PA14, GPIO_AF1);
sdh_init();                                  // gate, reset, 1-bit, ~300-400 kHz
for (int i = 0; i < 10; i++) { sdh_clocks(); }   // 80 clocks at power-up
sdh_cmd(0, 0, SDH_IO_CMD_NORESP);            // CMD0
uint32_t st = sdh_cmd(8, 0x1AA, SDH_IO_CMD_SHORT);
if (!(st & SDH_ERR_MASK) && (sdh_resp_index() == 8)) {
    uint32_t r7 = sdh_resp();                // 0x1AA echoed back
}
// ... ACMD41, CMD2, CMD3, CMD7, ACMD6 ...
sdh_set_clock(20000000);
sdh_set_width(SDH_WIDTH_4BIT);
sdh_cmd(17, lba, SDH_IO_CMD_SHORT);
sdh_read_block(buf, 512);
```

- `sdh_init()`: clock gate on, controller reset, MMC_CTRL = MMC mode with
  automatic signal mode, push-pull, 1-bit; CRCs on; interrupts masked;
  MMC_TIMEOUTCNT = 255 in 10 ms units; `sdh_set_clock(400000)`.
- `sdh_cmd()`: CMD_BUF4 = `0x40 | index`, CMD_BUF3..0 = argument (MSB in
  CMD_BUF3), clear CLR_MMC_INT, write MMC_IO, wait for MMC_IO bit 2 to
  clear (100 ms). Returns CLR_MMC_INT (`SDH_STATUS_STUCK` added on a
  timeout).
- `sdh_read_block()` / `sdh_write_block()`: one block through the buffer
  with the CPU, as wm_sdk_w80x does: BUF_CTRL `0x4020` (read) / `0x4820`
  (write), MMC_BYTECNTL, then MMC_IO `0x03` / `0x01`.
- `sdh_read_blocks_dma()` / `sdh_write_blocks_dma()`: the DMA sequence
  above.
- The SD protocol (identification, RCA, busy waits, CMD12, retries) is in
  [`sdcard_sdio.c`](../../examples/lib/sdcard/sdcard_sdio.c).

---

## Register details

### MMC_CTRL

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A00` (base + `0x00`) | 32 bits | RW | `0x0000_0045` ¹ | `SDH->CTRL`, `sdh_init()`, `sdh_set_clock()`, `sdh_set_width()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:11 | — | RO | 0 | Reserved | |
| 10 | read wait | RW | 0 | 1: SDIO read wait enabled | |
| 9 | SDIO interrupt | RW | 0 | 1: SDIO card interrupt (on DAT1) enabled | |
| 8 | SDIO mode | RW | 0 | 0: SD memory card / MMC · 1: SDIO card | `SDH_CTRL_SDIO` |
| 7 | bus width | RW | 0 | 0: 1 bit (DAT0) · 1: 4 bits (DAT0..DAT3) | `SDH_CTRL_WIDTH4` |
| 6 | transfer mode | RW | 1 | Clock base. 0: low speed, `clk1m` · 1: high speed, `hclk` | `SDH_CTRL_HIGH_SPEED` |
| 5:3 | clock select | RW | 0 | Bus clock = base / (2 × (value + 1)): ½, ¼, ⅙ … 1/16 (table 2) | `SDH_CTRL_CLKSEL_Msk` |
| 2 | drive mode | RW | 1 | 0: push-pull · 1: open drain | `SDH_CTRL_OPEN_DRAIN` |
| 1 | signal mode | RW | 0 | 0: line levels from MM_PORT · 1: driven by the controller | `SDH_CTRL_AUTO_SIG` |
| 0 | port mode | RW | 1 | 0: SPI · 1: SD/MMC bus | `SDH_CTRL_MMC` |

- Both vendor SDKs use push-pull (bit 2 = 0) for SD cards, also during
  identification, where the SD specification allows push-pull *(SDK)*.
  Open drain is for MMC identification.
- wm_sdk_w80x writes `0xD3` (4-bit, high speed, ⅙, push-pull, MMC mode);
  wm_iot_sdk writes `0x93` (4-bit, low speed, ⅙) and then switches bit 6
  per operation *(SDK)*.

### MMC_IO

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A04` (base + `0x04`) | 32 bits | RW | `0x0000_0000` | `SDH->IO`, `sdh_cmd()`, `sdh_clocks()`, `sdh_read_block()`, `sdh_write_block()` |

Write the whole register at once: the write starts the operation (see
[Operations](#commands-and-single-blocks-mmc_io)).

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:10 | — | RO | 0 | Reserved | |
| 9 | abort flag | RW | 0 | 1: this command is CMD12 / IO abort (SDIO) | |
| 8 | command attribute | RW | 0 | 1: data blocks follow this command (SDIO) | |
| 7 | 8 clocks after | RW | 0 | 1: 8 clocks after the command / response / single block | `SDH_IO_AUTO_CLOCKS` |
| 6 | auto response | RW | 0 | 1: receive the response after the command | `SDH_IO_AUTO_RESP` |
| 5 | 8 clocks | RW | 0 | 1: the operation is 8 clocks · 0: command or response, per bit 3 | `SDH_IO_CLOCKS` |
| 4 | long response | RW | 0 | 1: 136-bit response (CID, CSD); bits 135:8 go to CMD_BUF | `SDH_IO_LONG_RESP` |
| 3 | response / command | RW | 0 | With bit 5 = 0. 0: send the command · 1: receive a response | `SDH_IO_RECEIVE` |
| 2 | command start | RW, SC | 0 | 1: start the command / response / clocks given by bits 7:3 | `SDH_IO_CMD_START` |
| 1 | data direction | RW | 0 | 0: write · 1: read | `SDH_IO_DATA_READ` |
| 0 | data start | RW, SC | 0 | 1: start one data block of MMC_BYTECNTL bytes | `SDH_IO_DATA_START` |

The names of bits 2 and 7 are swapped in wm_iot_sdk; see
[Manual discrepancies](#manual-discrepancies).

### MMC_BYTECNTL

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A08` (base + `0x08`) | 32 bits | RW | `0x0000_0200` | `SDH->BYTECNT` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:16 | — | RO | 0 | Reserved | |
| 15:0 | byte count | RW | 512 | Bytes in a single-block transfer (MMC_IO bit 0). The buffer holds 512 | |

The manual's feature list says blocks of up to 1024 bytes, but the buffer
is 512 bytes *(manual)*. ACMD13 (SD status) reads 64 bytes this way *(SDK)*.

### MMC_TR_BLOCKCNT

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A0C` (base + `0x0C`) | 32 bits | RO | `0x0000_0000` | `SDH->TR_BLOCKS` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:16 | — | RO | 0 | Reserved | |
| 15:0 | blocks done | RO | 0 | Blocks completed in the current multi-block transfer | |

### MMC_CRCCTL

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A10` (base + `0x10`) | 32 bits | RW | `0x0000_0000` | `SDH->CRCCTL`, `sdh_init()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:8 | — | RO | 0 | Reserved | |
| 7 | CMD CRC | RW | 0 | 1: generate and check the CRC7 on CMD | `SDH_CRCCTL_CMD_EN` |
| 6 | DAT CRC | RW | 0 | 1: generate and check the CRC16 on DAT | `SDH_CRCCTL_DAT_EN` |
| 5 | auto CRC status check | RW | 0 | 1: if the card's CRC status token is not `010`, raise CRC_TOKEN, stop the write and clear MMC_IO bit 0 / MBCTL bits 2:0 | |
| 4 | read before response | RW | 0 | 1: multi-block read data may arrive before the response | |
| 3:2 | DAT CRC select | RW | 0 | Which line's CRC DAT_CRCL/H show: 0 = DAT0 … 3 = DAT3 (table 4) | |
| 1 | CMD CRC error | RO | 0 | 1: the last response had a bad CRC7 | `SDH_CRCCTL_CMD_ERR` |
| 0 | DAT CRC error | RO | 0 | 1: the last data block had a bad CRC16 | `SDH_CRCCTL_DAT_ERR` |

Both vendor SDKs write `0xC0` *(SDK)*.

### CMD_CRC, DAT_CRCL, DAT_CRCH

| Address | Name | Access | Bits | Values |
|---|---|---|---|---|
| `0x4000_0A14` | CMD_CRC | RO | 6:0 | CRC7 of the last command or response |
| `0x4000_0A18` | DAT_CRCL | RO | 7:0 | Data CRC16 low byte, of the line chosen by MMC_CRCCTL bits 3:2 |
| `0x4000_0A1C` | DAT_CRCH | RO | 7:0 | Data CRC16 high byte |

For debugging only; neither SDK reads them.

### MM_PORT

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A20` (base + `0x20`) | 32 bits | RW | `0x0000_007F` | `SDH->PORT` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:8 | — | RO | 0 | Reserved | |
| 7 | CLK level | RW | 0 | Clock line level when MMC_CTRL bit 1 = 0 | |
| 6 | CMD level | RW | 1 | CMD line level when MMC_CTRL bit 1 = 0 | |
| 5 | DAT level | RW | 1 | Data line level when MMC_CTRL bit 1 = 0 | |
| 4 | Ncr timeout check | RW | 1 | 1: flag NCR_TIMEOUT when no response starts in time | |
| 3:0 | Ncr timeout | RW | 15 | Clocks to wait for a response to start | |

The SD specification gives Ncr as at most 64 clocks, more than this
field's 15. The driver leaves the reset value, as both vendor SDKs do
*(SDK, to check)*.

### MMC_INT_MASK

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A24` (base + `0x24`) | 32 bits | RW | `0x0000_0000` | `SDH->INT_MASK` |

1 = interrupt enabled (the manual's "do not block"), 0 = masked. Bits as in
[CLR_MMC_INT](#clr_mmc_int). The driver polls and leaves everything masked;
both vendor SDKs write `0x100` (SDIO card interrupt only) *(SDK)*.

### CLR_MMC_INT

Status flags. Read: the flag is set (whether or not it is masked). Write 1:
clear it.

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A28` (base + `0x28`) | 32 bits | W1C | `0x0000_0000` | `SDH->INT_SRC` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:9 | — | RO | 0 | Reserved | |
| 8 | SDIO interrupt | W1C | 0 | An SDIO card signalled an interrupt on DAT1 | `SDH_INT_SDIO` |
| 7 | CRC status token error | W1C | 0 | The card rejected a written block; MMC_SIG bits 6:4 hold its token | `SDH_INT_CRC_TOKEN` |
| 6 | Ncr timeout | W1C | 0 | No response; the transfer is abandoned and must be set up again | `SDH_INT_NCR_TIMEOUT` |
| 5 | data timeout | W1C | 0 | A data block or busy period took longer than MMC_TIMEOUTCNT | `SDH_INT_DATA_TIMEOUT` |
| 4 | multi-block done | W1C | 0 | A multi-block transfer finished | `SDH_INT_MULTI_DONE` |
| 3 | CMD CRC error | W1C | 0 | Bad response CRC7 (always set for R2/R3) | `SDH_INT_CMD_CRC` |
| 2 | DAT CRC error | W1C | 0 | Bad CRC16 in a received block | `SDH_INT_DATA_CRC` |
| 1 | data done | W1C | 0 | A data block finished (each block of a multi-block transfer too) | `SDH_INT_DATA_DONE` |
| 0 | command done | W1C | 0 | A command / response / 8 clocks finished | `SDH_INT_CMD_DONE` |

The manual and wm_iot_sdk call bit 5 "multi-block timeout"; it also covers
single blocks *(to check)*. wm_sdk_w80x clears with `|= 0x7FF`, which
writes 1 to every flag *(SDK)*. `SDH_INT_ALL` is `0x1FF`.

### MMC_CARDSEL

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A2C` (base + `0x2C`) | 32 bits | RW | `0x0000_0040` | `SDH->CARDSEL`, `sdh_set_clock()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:8 | — | RO | 0 | Reserved | |
| 7 | controller enable | RW | 0 | 1: controller on | `SDH_CARDSEL_ENABLE` |
| 6 | card clock | RW | 1 | 1: clock line on | `SDH_CARDSEL_CARD_CLK` |
| 5:0 | time base | RW | 0 | `clk1m` = hclk / ((value + 1) × 2); meant to be 1 MHz (see [Clock](#clock)) | `SDH_CARDSEL_TIMEBASE_Msk` |

### MMC_SIG

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A30` (base + `0x30`) | 32 bits | RW | `0x0000_00FF` | `SDH->SIG` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:8 | — | RO | 0 | Reserved | |
| 7 | CMD level | RW | 1 | CMD line level. **Reading the register sends one clock pulse** and latches CMD on its rising edge | |
| 6:4 | CRC status | RW | `111` | The card's CRC status token after a written block: `010` = accepted, `101` = CRC error, `110` = write error | |
| 3 | DAT3 level | RW | 1 | | |
| 2 | DAT2 level | RW | 1 | | |
| 1 | DAT1 level | RW | 1 | | |
| 0 | DAT0 level | RW | 1 | DAT0 level (low while the card is busy) | |

Because a read clocks the bus, don't read it while a transfer runs
*(manual)*.

### MMC_IO_MBCTL

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A34` (base + `0x34`) | 32 bits | RW | `0x0000_0010` | `SDH->MBCTL`, `sdh__blocks_dma()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:8 | — | RO | 0 | Reserved | |
| 7:6 | NAC timeout unit | RW | 0 | Unit for read data timeouts: 0 = 1 µs, 1 = 100 µs, 2 = 10 ms, 3 = 1 s (table 6) | `SDH_MBCTL_NAC_UNIT_Pos` |
| 5:4 | busy timeout unit | RW | 1 | Unit for write busy timeouts, same values (table 7) | `SDH_MBCTL_BUSY_UNIT_Pos` |
| 3 | clock polarity | RW | 0 | 0: data out on the rising edge, sampled on the falling edge · 1: the other way round | |
| 2 | auto command + blocks | RW, SC | 0 | 1: send the command, receive the response, 8 clocks, then the blocks (needs MMC_IO bits 7:6 = `11`) | |
| 1 | direction | RW | 0 | 0: write · 1: read | `SDH_MBCTL_READ` |
| 0 | blocks start | RW, SC | 0 | 1: transfer MMC_BLOCKCNT blocks | `SDH_MBCTL_START` |

The units count `clk1m` periods, so above 128 MHz they are shorter
([Clock](#clock)). This SDK uses 10 ms for both (`SDH_MBCTL_UNITS`, as
wm_sdk_w80x's `0xA0`); wm_iot_sdk uses 1 s (`| 0xF0`) *(SDK)*.

### MMC_BLOCKCNT

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A38` (base + `0x38`) | 32 bits | RW | `0x0000_0001` | `SDH->BLOCKCNT` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:16 | — | RO | 0 | Reserved | |
| 15:0 | block count | RW | 1 | Blocks in a multi-block transfer (MMC_IO_MBCTL) | |

### MMC_TIMEOUTCNT

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A3C` (base + `0x3C`) | 32 bits | RW | `0x0000_0040` | `SDH->TIMEOUTCNT`, `sdh_init()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:8 | — | RO | 0 | Reserved | |
| 7:0 | timeout count | RW | 64 | Data / busy timeout = count × unit (MMC_IO_MBCTL bits 7:6 or 5:4) | |

Both vendor SDKs write `0xFF` *(SDK)*: with 10 ms units that is 2.55 s
(1.36 s at 240 MHz, see [Clock](#clock)).

### CMD_BUF0..CMD_BUF15

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A40`–`0x4000_0A7C` (base + `0x40` + 4 × n) | 32 bits each, bits 7:0 used | RW | `0x0000_0000` | `SDH->CMD_BUF[n]`, `sdh_cmd()`, `sdh_resp()`, `sdh_resp_index()`, `sdh_resp_long()` |

CMD_BUF*n* holds bits [8n + 15 : 8n + 8] of what goes out or comes in on
CMD *(manual)*. The low 8 bits (CRC7 and end bit) are not kept.

**Command (48 bits):**

| CMD_BUF | Bits on CMD | Contents |
|---|---|---|
| 4 | 47:40 | `0x40 \| index` (start bit 0, transmission bit 1, 6-bit index) |
| 3 | 39:32 | Argument bits 31:24 |
| 2 | 31:24 | Argument bits 23:16 |
| 1 | 23:16 | Argument bits 15:8 |
| 0 | 15:8 | Argument bits 7:0 |

**48-bit response (R1, R3, R6, R7):** the same layout: CMD_BUF4 bits 5:0
are the index the card echoed (`0x3F` for R3), CMD_BUF3..0 the 32-bit
argument (card status, OCR, RCA, …).

**136-bit response (R2: CID, CSD, with MMC_IO bit 4):** response bits
135:8 fill CMD_BUF15..CMD_BUF0. CMD_BUF15 is the start bits and `0x3F`;
CMD_BUF14 down to CMD_BUF0 are register bytes 0..14 (bits 127:8, MSB
first). Byte 15 (CRC7 and the always-1 bit) is lost, so
`sdh_resp_long()` puts 0 there and the SD backend recomputes it *(SDK)*.

### BUF_CTRL

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0A80` (base + `0x80`) | 32 bits | RW | `0x0000_0002` | `SDH->BUF_CTL` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:16 | — | RO | 0 | Reserved | |
| 15 | flush | RW, SC | 0 | 1: empty the buffer (clears after one clock) | `SDH_BUF_FLUSH` |
| 14 | DMA request mask | RW | 0 | 1: DMA requests masked. Set it before turning DMA on; changing it while DMA is on has no effect | `SDH_BUF_DMA_REQ_MASK` |
| 13 | — | RW | 0 | Reserved | |
| 12 | FIFO status mask | RW | 0 | 1: ignore "full" while reading from the card, "empty" while writing to it | |
| 11 | direction | RW | 0 | 0: read (card to buffer) · 1: write (buffer to card) | `SDH_BUF_WRITE` |
| 10 | DMA | RW, SC | 0 | 1: the DMA moves the data; clears itself when the transfer ends · 0: the CPU reads/writes DATA_BUF | `SDH_BUF_DMA` |
| 9:2 | DMA level | RW | 0 | Words that trigger a DMA request (only with bit 10 = 1). At most 127 | `SDH_BUF_LEVEL(words)` |
| 1 | empty | RO | 1 | 1: buffer empty | `SDH_BUF_EMPTY` |
| 0 | full | RO | 0 | 1: buffer full | `SDH_BUF_FULL` |

Values used by both vendor SDKs *(SDK)*:

| Value | Meaning | Used for |
|---|---|---|
| `0x4000` | DMA request masked, DMA off | Before setting up a DMA transfer |
| `0x4020` | Masked, read, level 8 | Single-block read by the CPU |
| `0x4820` | Masked, write, level 8 | Single-block write by the CPU |
| `0x404` | DMA on, read, level 1 | Multi-block read by DMA |
| `0xC20` | DMA on, write, level 8 | Multi-block write by DMA |

### DATA_BUF

| Address | Size | Access | SDK |
|---|---|---|---|
| `0x4000_0B00`–`0x4000_0CFF` (base + `0x100`) | 128 × 32 bits | RW | `SDH->DATA_BUF[128]` |

The 512-byte data buffer. Bytes are in card order, the first byte in the
low byte of the word *(SDK)*.

- **Reading** works as a FIFO: wm_sdk_w80x reads `DATA_BUF[0]` 128 times
  for a block, and the DMA reads from that one address too *(SDK)*.
- **Writing:** wm_sdk_w80x writes `DATA_BUF[0..127]` in order, while the
  DMA writes to `DATA_BUF[0]` again and again. Both seem to work, so the
  write side is probably a FIFO as well *(SDK, to check)*.

---

## Manual discrepancies

All of these are in v2.1 and still in v3.0 (English).

| Where | Manual | Vendor SDKs / notes |
|---|---|---|
| MMC_IO bits 2 and 7 | Bit 2 starts the operation; bit 7 adds 8 clocks after it | wm_iot_sdk's `wm_sdh_reg_struct.h` names bit 2 `auto_8_null_clk_gen` and bit 7 the trigger. Its HAL still writes the manual's values (bit 2 last, `0xC4` / `0x44`), and wm_sdk_w80x writes `0x04` / `0x44` / `0x54`: the manual is right, the struct's names are swapped |
| Tables 3 and 5 | The translated tables have cells shifted between columns and rows | Re-aligned above against the bit descriptions and the values both SDKs write |
| MMC_IO_MBCTL bit 0 text | "when mmc_io[7:6] == 11" | Table 5 shows `xx` for the data-only operations, and wm_sdk_w80x starts them with MMC_IO left at `0x44` (bits 7:6 = `01`) |
| MMC_IO_MBCTL table references | Bits 7:6 refer to "Appendix 2 – Table 5", bits 5:4 to "Table 6" | The unit tables are tables 6 and 7 |
| Feature list | "Support blocks up to 1024 bytes" | The data buffer is 512 bytes |
| MMC_CARDSEL bits 5:0 | Builds a 1 MHz clock | Only 6 bits: at more than 128 MHz hclk it can't reach 1 MHz. Both SDKs write `hclk_MHz / 2 - 1` without checking (119 at 240 MHz overflows into bits 6 and 7, which they set anyway) |
| CLR_MMC_INT bit 5 | "Multi-block timeout" | Also the data timeout for single blocks *(to check)* |
| MMC_CTRL register list | No reset value for the whole register | The bit table gives `0x45` |
