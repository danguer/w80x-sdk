# SDIO host

[API index](README.md)

- **Source:** [`w80x_sdh.h`](../../w80x/w80x_sdh.h)
- **Registers:** [SDIO host (SDH)](../mcu/sdio.md)
- **Used by:** [`examples/lib/sdcard/sdcard_sdio.c`](../../examples/lib/sdcard/sdcard_sdio.c) ([SD card](sdcard.md))

```c
sdh_pins(PA9, PA10, PA11, PA12, PA13, PA14, GPIO_AF1);   // CLK, CMD, DAT0..DAT3
sdh_init();                                   // 1-bit bus, ~400 kHz, CRCs on
uint32_t st = sdh_cmd(8, 0x1AA, SDH_IO_CMD_SHORT);       // command + 48-bit response
if (!(st & SDH_ERR_MASK)) {
    uint32_t r7 = sdh_resp();                 // response argument; sdh_resp_index() = 8
}
sdh_set_clock(20000000);                      // returns the rate set (<= request)
sdh_set_width(SDH_WIDTH_4BIT);                // after ACMD6
sdh_cmd(17, lba, SDH_IO_CMD_SHORT);
sdh_read_block(buf, 512);                     // one block through the 512-byte buffer
sdh_cmd(18, lba, SDH_IO_CMD_SHORT);
sdh_read_blocks_dma(5, buf, 16);              // multi-block by DMA channel 5, then CMD12
```

This is the bus layer only; the SD card protocol (identification,
addressing, busy waits, retries) is in the SDIO backend of the SD card
library. Everything is polled, and each call returns the controller's
`INT_SRC` flags: `SDH_ERR_MASK` covers no response, data CRC, a rejected
write and timeouts (`SDH_STATUS_STUCK` is added when an operation never
finished). R2 and R3 responses always set `SDH_INT_CMD_CRC`; ignore it for
those.

The bus clock is the CPU clock divided by 2..16, or a time base near 1 MHz
divided the same way for slow rates. `sdh_set_clock()` picks the fastest
rate not above the request (240 MHz CPU: 24 MHz for 25, 20 for 20, 937 kHz
and below for the identification clock). Call it again after changing the
CPU clock.

Multi-block transfers only work by DMA (as in both vendor SDKs): the buffer
must be word-aligned SRAM (`sdh_dma_ok()`), at most `SDH_DMA_MAX_BLOCKS`
(127) blocks, and `dma_init()` must have run. The register values follow
the manual's tables and `wm_sdio_host.c` from wm_sdk_w80x. Pins: W801/W806
PA9..PA14 on AF1; W800 PB6..PB11 on AF2.

Not yet run on hardware.
