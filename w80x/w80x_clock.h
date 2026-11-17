/*
 * w80x_clock.h - Clock & reset control (manual chapter 5).
 *
 * Clock tree (40 MHz XTAL -> DPLL 480 MHz, fixed):
 *
 *   PLL 480 MHz --/cpu_div (2..255)--> CPU / AHB1 / SRAM / DMA   (SystemCoreClock)
 *               --/wlan_div (>=3)----> WLAN root (must be 160 MHz for Wi-Fi/BT)
 *                                       \--/4--> AHB2 / APB peripherals
 *               --/3-----------------> CLK_PERI 160 MHz (RSA, GPSEC, PWM, I2S, flash)
 *
 * Constraint: CPU clock must be an integer multiple of the AHB2 (bus2) clock.
 * clock_set_cpu_div() picks wlan_div / bus2 ratio automatically, keeping
 * WLAN at 160 MHz (APB = 40 MHz) whenever the CPU divider allows it.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_CLOCK_H
#define W80X_CLOCK_H

#include "w80x.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Common CPU frequencies, expressed as PLL dividers. */
typedef enum {
    CLK_CPU_240MHZ = 2,
    CLK_CPU_160MHZ = 3,
    CLK_CPU_120MHZ = 4,
    CLK_CPU_96MHZ  = 5,
    CLK_CPU_80MHZ  = 6,    /* reset default */
    CLK_CPU_60MHZ  = 8,
    CLK_CPU_40MHZ  = 12,
    CLK_CPU_24MHZ  = 20,
    CLK_CPU_12MHZ  = 40,
    CLK_CPU_2MHZ   = 240,
} clk_cpu_div_t;

/* Set CPU divider (2..255). Returns 0 on success, -1 on invalid divider. */
int clock_set_cpu_div(uint32_t div);

/* Convenience: MHz must divide 480 evenly (e.g. 240, 160, 120, 80, 40...). */
W80X_INLINE int clock_set_cpu_mhz(uint32_t mhz)
{
    if ((mhz == 0) || ((480u % mhz) != 0)) {
        return -1;
    }
    return clock_set_cpu_div(480u / mhz);
}

/* Read back the current tree from hardware. */
W80X_INLINE uint32_t clock_cpu_div(void)  { return (CLK->DIV & CLK_DIV_CPU_Msk)  >> CLK_DIV_CPU_Pos; }
W80X_INLINE uint32_t clock_wlan_div(void) { return (CLK->DIV & CLK_DIV_WLAN_Msk) >> CLK_DIV_WLAN_Pos; }
W80X_INLINE uint32_t clock_cpu_hz(void)   { return W80X_PLL_HZ / clock_cpu_div(); }
W80X_INLINE uint32_t clock_wlan_hz(void)  { return W80X_PLL_HZ / clock_wlan_div(); }
W80X_INLINE uint32_t clock_bus2_div(void) { return (CLK->DIV & CLK_DIV_BUS2_Msk) >> CLK_DIV_BUS2_Pos; }
/* AHB2 + APB = CPU / bus2 ratio (same formula as wm_iot_sdk's HAL). When
 * clock_set_cpu_div() set the dividers this equals wlan / 4; reading the
 * ratio stays correct if a bootloader set them differently. */
W80X_INLINE uint32_t clock_apb_hz(void)
{
    uint32_t ratio = clock_bus2_div();
    return clock_cpu_hz() / (ratio ? ratio : 1u);
}
W80X_INLINE uint32_t clock_peri_hz(void)
{
    return W80X_PLL_HZ / ((CLK->DIV & CLK_DIV_PERI_Msk) >> CLK_DIV_PERI_Pos);
}

/* Peripheral clock gating: pass CLK_GATE_xxx masks (may be OR-ed). */
W80X_INLINE void clock_enable(uint32_t gate_mask)  { CLK->GATE_EN |=  gate_mask; }
W80X_INLINE void clock_disable(uint32_t gate_mask) { CLK->GATE_EN &= ~gate_mask; }
W80X_INLINE int  clock_is_enabled(uint32_t gate)   { return (CLK->GATE_EN & gate) == gate; }

/* Peripheral reset: pass CLK_RST_xxx masks. Does not reset CPU/watchdog. */
W80X_INLINE void periph_reset_assert(uint32_t rst_mask)  { CLK->RST_CTRL &= ~rst_mask; }
W80X_INLINE void periph_reset_release(uint32_t rst_mask) { CLK->RST_CTRL |=  rst_mask; }
W80X_INLINE void periph_reset(uint32_t rst_mask)
{
    periph_reset_assert(rst_mask);
    __nop(); __nop(); __nop(); __nop();
    periph_reset_release(rst_mask);
}

/* JTAG/SWD debug port on PA1 (CK) / PA4 (SWO). */
W80X_INLINE void clock_jtag_enable(void)  { CLK->SEL |=  CLK_SEL_JTAG_EN; }
W80X_INLINE void clock_jtag_disable(void) { CLK->SEL &= ~CLK_SEL_JTAG_EN; }

#ifdef __cplusplus
}
#endif

#endif /* W80X_CLOCK_H */
