/*
 * w80x_dma.h - DMA controller, 8 channels. Header-only.
 *
 * Memory to memory:
 *
 *   dma_init();
 *   dma_memcpy(0, dst, src, 1024);          // channel 0, waits until done
 *
 *   dma_setup(1, dst, src, len, DMA_SRC_INC | DMA_DST_INC | DMA_WIDTH_32, DMA_REQ_MEM);
 *   dma_start(1);
 *   ... do other work ...
 *   dma_wait(1);
 *
 * Peripheral transfers set a request (dma_req_t): the peripheral paces the
 * channel. UART helpers are below (dma_uart_tx / dma_uart_rx).
 *
 * Rules:
 *   - len is in bytes, at most DMA_MAX_LEN (65535), and a multiple of the
 *     item width. Addresses must be aligned to the width.
 *   - Use SRAM buffers. Flash (XIP) addresses as a source are not confirmed
 *     to work.
 *   - A channel must be stopped (dma_busy() == 0) before dma_setup().
 *
 * Interrupts: dma_irq_enable(ch, DMA_IRQ_DONE) plus irq_enable(dma_irqn(ch)).
 * Channels 0-3 have one vector each; channels 4-7 share DMA_CH4_7_IRQn. The
 * handler must clear the flag with dma_irq_clear():
 *
 *   W80X_IRQ void DMA_Channel0_IRQHandler(void) { dma_irq_clear(0, DMA_IRQ_ALL); done = 1; }
 *
 * Auto-reload (DMA_AUTO_RELOAD) restarts the same transfer when it ends,
 * for continuous streams. wm_iot_sdk works around a hardware problem with
 * it by stopping and restarting the channel in the done interrupt; call
 * dma_restart(ch) there to do the same.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_DMA_H
#define W80X_DMA_H

#include "w80x.h"
#include "w80x_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Request lines (MODE[5:2]). DMA_REQ_MEM = no request: memory to memory. */
typedef enum {
    DMA_REQ_UART_RX   = 0,    /* the UART chosen with dma_uart_select() */
    DMA_REQ_UART_TX   = 1,
    DMA_REQ_PWM_CAP0  = 2,
    DMA_REQ_PWM_CAP1  = 3,
    DMA_REQ_LSPI_RX   = 4,
    DMA_REQ_LSPI_TX   = 5,
    DMA_REQ_ADC_CH0   = 6,
    DMA_REQ_ADC_CH1   = 7,
    DMA_REQ_ADC_CH2   = 8,
    DMA_REQ_ADC_CH3   = 9,
    DMA_REQ_I2S_RX    = 10,
    DMA_REQ_I2S_TX    = 11,
    DMA_REQ_SDIO_HOST = 12,
    DMA_REQ_MEM       = 0xFF,
} dma_req_t;

/* Transfer flags for dma_setup(): one per group, OR-ed together. */
#define DMA_SRC_FIXED    (0u << DMA_CTRL_SRC_Pos)    /* e.g. a peripheral data register */
#define DMA_SRC_INC      (1u << DMA_CTRL_SRC_Pos)
#define DMA_SRC_WRAP     (3u << DMA_CTRL_SRC_Pos)    /* ring, see dma_set_wrap() */
#define DMA_DST_FIXED    (0u << DMA_CTRL_DST_Pos)
#define DMA_DST_INC      (1u << DMA_CTRL_DST_Pos)
#define DMA_DST_WRAP     (3u << DMA_CTRL_DST_Pos)
#define DMA_WIDTH_8      (0u << DMA_CTRL_WIDTH_Pos)
#define DMA_WIDTH_16     (1u << DMA_CTRL_WIDTH_Pos)
#define DMA_WIDTH_32     (2u << DMA_CTRL_WIDTH_Pos)
#define DMA_BURST_4      DMA_CTRL_BURST4             /* 4 items per request (else 1) */
#define DMA_AUTO_RELOAD  DMA_CTRL_AUTO_RELOAD        /* restart when done */
#define DMA_FLAGS_Msk    0xFFu

/* Per-channel interrupt flags for dma_irq_*(). */
#define DMA_IRQ_BURST    1u      /* a burst was moved   */
#define DMA_IRQ_DONE     2u      /* transfer complete   */
#define DMA_IRQ_ALL      3u

W80X_INLINE DMA_Channel_TypeDef *dma_ch(uint32_t ch) { return &DMA->CH[ch & 7u]; }

/* Clock on, out of reset, all interrupts masked and cleared. */
W80X_INLINE void dma_init(void)
{
    CLK->GATE_EN  |= CLK_GATE_DMA;
    CLK->RST_CTRL |= CLK_RST_DMA;
    DMA->INTM = 0xFFFFu;
    DMA->INTS = 0xFFFFu;
}

/* ---- run control ------------------------------------------------------- */

W80X_INLINE int  dma_busy(uint32_t ch)  { return (dma_ch(ch)->CHCTRL & DMA_CHCTRL_ON) != 0; }
W80X_INLINE void dma_start(uint32_t ch) { dma_ch(ch)->CHCTRL |= DMA_CHCTRL_ON; }

/* Stops after the current burst; waits until the channel is off. */
W80X_INLINE void dma_stop(uint32_t ch)
{
    DMA_Channel_TypeDef *c = dma_ch(ch);
    if (c->CHCTRL & DMA_CHCTRL_ON) {
        c->CHCTRL |= DMA_CHCTRL_OFF;
        while (c->CHCTRL & DMA_CHCTRL_ON) {
        }
    }
}

W80X_INLINE void dma_restart(uint32_t ch) { dma_stop(ch); dma_start(ch); }

W80X_INLINE void dma_wait(uint32_t ch)
{
    while (dma_busy(ch)) {
    }
}

/* Bytes moved since the last start. */
W80X_INLINE uint32_t dma_count(uint32_t ch) { return dma_ch(ch)->STATUS & 0xFFFFu; }

/* ---- configuration ----------------------------------------------------- */

/* Program a channel (it must be stopped). Doesn't start it. */
W80X_INLINE void dma_setup(uint32_t ch, volatile void *dst, const volatile void *src,
                           uint32_t len, uint32_t flags, dma_req_t req)
{
    DMA_Channel_TypeDef *c = dma_ch(ch);

    c->MODE = (req == DMA_REQ_MEM) ? 0u
            : (DMA_MODE_HW | (((uint32_t)req << DMA_MODE_REQ_Pos) & DMA_MODE_REQ_Msk));
    c->SRC  = (uint32_t)(uintptr_t)src;
    c->DST  = (uint32_t)(uintptr_t)dst;
    c->CTRL = (flags & DMA_FLAGS_Msk) | ((len & DMA_MAX_LEN) << DMA_CTRL_LEN_Pos);
    DMA->INTS = DMA_INT_CH(ch);
}

/* Drop a pending request from a peripheral (UART_SEL bits 23:8, one per
 * request line, self-clearing; manual 6.4.4). wm_iot_sdk does this before
 * setting up a peripheral transfer, so a request left from an earlier
 * transfer doesn't move a word too early. */
W80X_INLINE void dma_req_clear(dma_req_t req)
{
    if (req != DMA_REQ_MEM) {
        DMA->UART_SEL = (DMA->UART_SEL & DMA_UART_SEL_Msk) | BIT(DMA_UART_SEL_REQCLR_Pos + (uint32_t)req);
    }
}

/* Change only the addresses and length, keeping mode and flags (the
 * channel must be stopped). Handy for ping-pong buffers. */
W80X_INLINE void dma_reload(uint32_t ch, volatile void *dst, const volatile void *src, uint32_t len)
{
    DMA_Channel_TypeDef *c = dma_ch(ch);
    c->SRC  = (uint32_t)(uintptr_t)src;
    c->DST  = (uint32_t)(uintptr_t)dst;
    c->CTRL = (c->CTRL & ~DMA_CTRL_LEN_Msk) | ((len & DMA_MAX_LEN) << DMA_CTRL_LEN_Pos);
}

/* Ring buffers for DMA_SRC_WRAP / DMA_DST_WRAP: the address runs through
 * len bytes from the ring start, then jumps back. Call after dma_setup();
 * the ring starts are the src/dst given there. 0 = side doesn't wrap.
 * (Follows wm_iot_sdk; not yet tested here.) */
W80X_INLINE void dma_set_wrap(uint32_t ch, uint32_t src_len, uint32_t dst_len)
{
    DMA_Channel_TypeDef *c = dma_ch(ch);
    c->SRC_WRAP  = c->SRC;
    c->DST_WRAP  = c->DST;
    c->WRAP_SIZE = (src_len & 0xFFFFu) | ((dst_len & 0xFFFFu) << 16);
}

/* ---- interrupts -------------------------------------------------------- */

W80X_INLINE IRQn_Type dma_irqn(uint32_t ch)
{
    return (ch < 4u) ? (IRQn_Type)(DMA_CH0_IRQn + ch) : DMA_CH4_7_IRQn;
}

W80X_INLINE void dma_irq_enable(uint32_t ch, uint32_t m)
{
    uint32_t p = irq_save();
    DMA->INTM &= ~((m & DMA_IRQ_ALL) << (2u * ch));
    irq_restore(p);
}

W80X_INLINE void dma_irq_disable(uint32_t ch, uint32_t m)
{
    uint32_t p = irq_save();
    DMA->INTM |= (m & DMA_IRQ_ALL) << (2u * ch);
    irq_restore(p);
}

/* Raw flags (DMA_IRQ_BURST / DMA_IRQ_DONE), set even while masked. */
W80X_INLINE uint32_t dma_irq_status(uint32_t ch)          { return (DMA->INTS >> (2u * ch)) & DMA_IRQ_ALL; }
W80X_INLINE void     dma_irq_clear(uint32_t ch, uint32_t m) { DMA->INTS = (m & DMA_IRQ_ALL) << (2u * ch); }

/* ---- convenience ------------------------------------------------------- */

/* Copy len bytes and wait. Uses 32-bit items when both pointers and len
 * are word-aligned, bytes otherwise. */
W80X_INLINE void dma_memcpy(uint32_t ch, void *dst, const void *src, uint32_t len)
{
    uint32_t w = ((uint32_t)(uintptr_t)dst | (uint32_t)(uintptr_t)src | len) & 3u;
    dma_setup(ch, dst, src, len,
              DMA_SRC_INC | DMA_DST_INC | (w ? DMA_WIDTH_8 : (DMA_WIDTH_32 | DMA_BURST_4)),
              DMA_REQ_MEM);
    dma_start(ch);
    dma_wait(ch);
}

/* ---- UART -------------------------------------------------------------- */

/* The UART DMA requests come from one UART at a time; this picks it. */
W80X_INLINE void dma_uart_select(UART_TypeDef *u)
{
    DMA->UART_SEL = (DMA->UART_SEL & ~DMA_UART_SEL_Msk) | uart_index(u);
}

/* Send len bytes from buf in the background (sequence from wm_sdk_w80x).
 * "Done" means the last byte is in the UART FIFO: uart_flush() to wait for
 * the wire. Call dma_uart_tx_end() when done to stop the UART's requests. */
W80X_INLINE void dma_uart_tx(uint32_t ch, UART_TypeDef *u, const void *buf, uint32_t len)
{
    dma_uart_select(u);
    u->DMAC &= ~UART_DMAC_TX_EN;
    dma_setup(ch, &u->TDW, buf, len, DMA_SRC_INC | DMA_DST_FIXED | DMA_WIDTH_8, DMA_REQ_UART_TX);
    dma_start(ch);
    u->DMAC |= UART_DMAC_TX_EN;
}

W80X_INLINE void dma_uart_tx_end(UART_TypeDef *u) { u->DMAC &= ~UART_DMAC_TX_EN; }

/* Receive exactly len bytes into buf in the background; dma_count() says
 * how many have arrived so far. */
W80X_INLINE void dma_uart_rx(uint32_t ch, UART_TypeDef *u, void *buf, uint32_t len)
{
    dma_uart_select(u);
    u->DMAC &= ~UART_DMAC_RX_EN;
    dma_setup(ch, buf, &u->RDW, len, DMA_SRC_FIXED | DMA_DST_INC | DMA_WIDTH_8, DMA_REQ_UART_RX);
    dma_start(ch);
    u->DMAC |= UART_DMAC_RX_EN;
}

W80X_INLINE void dma_uart_rx_end(UART_TypeDef *u) { u->DMAC &= ~UART_DMAC_RX_EN; }

/* ---- chain mode (descriptor lists) ---------------------------------------
 * Layout and the CTRL >> 1 encoding come from wm_sdk_w80x (tls_dma_start)
 * and wm_iot_sdk ("HW moves desc.ctrl[22:0] to reg.ctrl[23:1]"). Not yet
 * tested here. Descriptors must stay valid (static or global) until the
 * channel is done.
 */
typedef struct dma_desc {
    volatile uint32_t valid;     /* DMA_DESC_VALID                     */
    uint32_t ctrl;               /* CTRL[23:1], i.e. CTRL >> 1         */
    uint32_t src;
    uint32_t dst;
    struct dma_desc *next;       /* NULL ends the list                 */
} dma_desc_t;

#define DMA_DESC_VALID  BIT(31)

W80X_INLINE void dma_desc_init(dma_desc_t *d, volatile void *dst, const volatile void *src,
                               uint32_t len, uint32_t flags, dma_desc_t *next)
{
    d->ctrl  = (((flags & DMA_FLAGS_Msk) & ~DMA_CTRL_AUTO_RELOAD)
               | ((len & DMA_MAX_LEN) << DMA_CTRL_LEN_Pos)) >> 1;
    d->src   = (uint32_t)(uintptr_t)src;
    d->dst   = (uint32_t)(uintptr_t)dst;
    d->next  = next;
    d->valid = DMA_DESC_VALID;
}

/* Run a descriptor list on a stopped channel; dma_start() starts it. */
W80X_INLINE void dma_setup_chain(uint32_t ch, dma_desc_t *first, dma_req_t req)
{
    DMA_Channel_TypeDef *c = dma_ch(ch);
    c->MODE = DMA_MODE_CHAIN
            | ((req == DMA_REQ_MEM) ? 0u
               : (DMA_MODE_HW | (((uint32_t)req << DMA_MODE_REQ_Pos) & DMA_MODE_REQ_Msk)));
    c->DESC = (uint32_t)(uintptr_t)first;
    DMA->INTS = DMA_INT_CH(ch);
}

#ifdef __cplusplus
}
#endif

#endif /* W80X_DMA_H */
