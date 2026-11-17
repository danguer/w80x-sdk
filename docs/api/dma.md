# DMA

[API index](README.md)

- **Source:** [`w80x_dma.h`](../../w80x/w80x_dma.h)
- **Example:** [`examples/dma`](../../examples/dma/main.c)

```c
dma_init();
dma_memcpy(0, dst, src, 1024);               // channel 0, waits

dma_setup(1, dst, src, len, DMA_SRC_INC | DMA_DST_INC | DMA_WIDTH_32, DMA_REQ_MEM);
dma_irq_enable(1, DMA_IRQ_DONE);             // DMA_Channel1_IRQHandler, clear with dma_irq_clear()
irq_enable(dma_irqn(1));
dma_start(1);

dma_uart_tx(2, UART0, buf, n);               // background UART output
```

Eight channels; channels 0-3 have their own interrupt, 4-7 share one.
Transfers are at most 65535 bytes. Use SRAM buffers: DMA from flash isn't
confirmed. Only one UART at a time can use DMA. Wrap (ring) mode and
descriptor chains are included, following the vendor SDKs, but haven't been tried yet.
