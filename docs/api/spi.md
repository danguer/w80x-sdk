# SPI

[API index](README.md)

- **Source:** [`w80x_spi.h`](../../w80x/w80x_spi.h)
- **Example:** [`examples/spi`](../../examples/spi/main.c)

```c
spi_pins(PB2, PB3, PA7, GPIO_AF2);            // SCK, MISO, MOSI (free pins on the HLK kit)
gpio_set(PB4); gpio_mode(PB4, GPIO_OUTPUT);   // chip select as a plain GPIO
spi_init(8000000, SPI_MODE0);                 // returns the SCK actually set (<= request)

gpio_clear(PB4);
spi_write(cmd, 4);
spi_read(buf, 256);                           // sends 0xFF while reading
gpio_set(PB4);
uint8_t r = spi_xfer(0x9F);                   // one byte each way; spi_transfer(tx, rx, n) for buffers

dma_init();                                   // once
spi_transfer_dma(0, 1, tx, rx, 512);          // DMA channels 0 (TX) and 1 (RX); either buffer may be NULL
```

Master only, polled. SCK = APB / (2 * (div + 1)), at most 20 MHz. Data moves
in chunks of up to 32 bytes (the FIFO size), so SCK pauses briefly between
chunks; a GPIO chip select stays low through the whole transfer. The SPI's
own CS pin can be routed with `gpio_af()` and driven with `spi_cs(1/0)`.
Pin options are listed at the top of `w80x_spi.h`.

`spi_transfer_dma()` moves the data with two DMA channels instead, in one
run of up to 8188 bytes with no pauses and no CPU copying; it still waits
until the transfer is done. It needs word-aligned SRAM buffers: unaligned
buffers, buffers in flash and the last `len % 4` bytes go through
`spi_transfer()` instead. `examples/spi` checks it and compares the speed: at 20 MHz the FIFO copy
reaches about 1530 KB/s and DMA about 2100 KB/s, against a wire limit of
2500 KB/s.
