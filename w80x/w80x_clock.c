/*
 * w80x_clock.c - CPU clock divider configuration.
 * SPDX-License-Identifier: MIT
 */
#include "w80x_clock.h"

int clock_set_cpu_div(uint32_t div)
{
    uint32_t wlan, bus2, reg;

    if ((div < 2u) || (div > 255u)) {
        return -1;
    }

    /*
     * bus2 (AHB2/APB) = PLL / (4 * wlan_div), CPU = PLL / div
     * => CPU:bus2 = 4 * wlan_div / div, which must be an integer >= 1.
     * Pick the smallest wlan_div >= 3 (WLAN root <= 160 MHz) satisfying that;
     * for div in {2,3,4,6,12} this is 3, i.e. WLAN 160 MHz / APB 40 MHz.
     */
    for (wlan = 3u; wlan <= 255u; wlan++) {
        if (((4u * wlan) % div) == 0u) {
            break;
        }
    }
    if (wlan > 255u) {
        return -1;
    }
    bus2 = (4u * wlan) / div;
    if ((bus2 == 0u) || (bus2 > 255u)) {
        return -1;
    }

    reg  = CLK->DIV & CLK_DIV_PERI_Msk;     /* keep CLK_PERI = 160 MHz */
    reg |= CLK_DIV_UPDATE
         | (bus2 << CLK_DIV_BUS2_Pos)
         | (wlan << CLK_DIV_WLAN_Pos)
         | (div  << CLK_DIV_CPU_Pos);

    uint32_t psr = irq_save();
    CLK->DIV = reg;
    while (CLK->DIV & CLK_DIV_UPDATE) {
    }
    SystemCoreClock = W80X_PLL_HZ / div;
    irq_restore(psr);

    return 0;
}
