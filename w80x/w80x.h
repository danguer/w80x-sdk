/*
 * w80x.h - Minimal register-level definitions for the WinnerMicro W80x (W800/W801/W806).
 *
 * Core: T-Head XT804 (C-SKY CK804 ISA, ABIv2, single-precision FPU).
 *       Build with a C-SKY toolchain.
 *
 * Source: "WM_W800 Register Manual v2.1" (the W801 and W806 share the W800 register map).
 *
 * Style: one header, plain structs + bit masks, static inline helpers.
 * Peripheral drivers live in w80x_<periph>.h and are mostly header-only.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_H
#define W80X_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Generic helpers                                                          */
/* ------------------------------------------------------------------------ */

#define __I   volatile const   /* read-only  */
#define __O   volatile         /* write-only */
#define __IO  volatile         /* read/write */

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

#define W80X_INLINE  static inline __attribute__((always_inline))

/* Puts a variable in .noinit, a fixed 1 KB block (__noinit_size) at the top
 * of RAM below the stack. Startup neither loads nor clears it, so
 * w80x_early_init() can write it before .data/.bss are set up. On a W801 it
 * keeps its value across a watchdog reset (the bootloader, classic secboot
 * or wm_iot_sdk, only overwrites low RAM); power-on and the RESET button
 * lose it (garbage, check a magic). */
#define W80X_NOINIT  __attribute__((section(".noinit")))

/* Marks interrupt handlers, for readability:  W80X_IRQ void GPIOA_IRQHandler(void) {...}
 * It expands to nothing: handlers are plain C functions called from the
 * common entry in startup.S, which saves and restores all registers. (GCC
 * 13's __attribute__((isr)) is not used: it leaves r18+ and the FPU unsaved.) */
#define W80X_IRQ

/* ------------------------------------------------------------------------ */
/* Clocks                                                                   */
/* ------------------------------------------------------------------------ */

#define W80X_XTAL_HZ   40000000UL   /* external crystal                    */
#define W80X_PLL_HZ   480000000UL   /* fixed DPLL output (manual 5.3.4)    */

/* Current CPU clock in Hz; kept up to date by clock_set_cpu_div(). */
extern uint32_t SystemCoreClock;

/* ------------------------------------------------------------------------ */
/* Memory map (manual 4.4)                                                  */
/* ------------------------------------------------------------------------ */

#define FLASH_BASE        0x08000000UL   /* 2 MB QFlash, XIP via 32 KB cache */
#define SRAM_BASE         0x20000000UL   /* 160 KB on AHB1                   */
#define MACRAM_BASE       0x20028000UL   /* 128 KB on AHB2                   */
#define SRAM_SIZE         (288UL * 1024)
#define PSRAM_BASE        0x30000000UL

#define RSA_BASE          0x40000000UL
#define GPSEC_BASE        0x40000600UL
#define DMA_BASE          0x40000800UL
#define SDIO_HOST_BASE    0x40000A00UL
#define PMU_BASE          0x40000D00UL
#define CLK_BASE          0x40000E00UL   /* clock & reset module */
#define FLASH_CTRL_BASE   0x40002000UL
#define PSRAM_CTRL_BASE   0x40002200UL

#define APB_BASE          0x40010000UL
#define I2C_BASE          0x40010000UL
#define SDADC_BASE        0x40010200UL
#define SPI_BASE          0x40010400UL
#define UART0_BASE        0x40010600UL
#define UART1_BASE        0x40010800UL
#define UART2_BASE        0x40010A00UL
#define UART3_BASE        0x40010C00UL
#define UART4_BASE        0x40010E00UL
#define UART5_BASE        0x40011000UL
#define GPIOA_BASE        0x40011200UL
#define GPIOB_BASE        0x40011400UL
#define WDG_BASE          0x40011600UL
#define TIMER_BASE        0x40011800UL
#define LCD_BASE          0x40011C00UL
#define PWM_BASE          0x40011E00UL
#define I2S_BASE          0x40012000UL
#define TOUCH_BASE        0x40012400UL

/* XT804 tightly-coupled IP */
#define TCIP_BASE         0xE000E000UL
#define CORET_BASE        (TCIP_BASE + 0x0010UL)
#define VIC_BASE          (TCIP_BASE + 0x0100UL)

/* ------------------------------------------------------------------------ */
/* Clock & reset module (manual chapter 5)                                  */
/* ------------------------------------------------------------------------ */

typedef struct {
    __IO uint32_t GATE_EN;      /* 0x00 SW_CLKG_EN   : 1 = clock running     */
    __IO uint32_t GATE_MASK;    /* 0x04 SW_CLK_MASK  : adaptive gating mask  */
         uint32_t RESERVED0;    /* 0x08                                      */
    __IO uint32_t RST_CTRL;     /* 0x0C SW_RST_CTRL  : 0 = held in reset     */
    __IO uint32_t DIV;          /* 0x10 SYS_CLK_DIV  : dividers              */
    __IO uint32_t SEL;          /* 0x14 SYS_CLK_SEL/DEBUG_CTRL               */
    __IO uint32_t I2S_CTRL;     /* 0x18 I2S_CLK_CTRL                         */
    __IO uint32_t RST_STATUS;   /* 0x1C RESET_STATUS                         */
} CLK_TypeDef;

#define CLK ((CLK_TypeDef *)CLK_BASE)

/* CLK->GATE_EN bits (1 = clock on). Reset value: all on. */
#define CLK_GATE_I2C      BIT(0)
#define CLK_GATE_UART0    BIT(1)
#define CLK_GATE_UART1    BIT(2)
#define CLK_GATE_UART2    BIT(3)
#define CLK_GATE_UART3    BIT(4)
#define CLK_GATE_UART4    BIT(5)
#define CLK_GATE_UART5    BIT(6)
#define CLK_GATE_LSPI     BIT(7)
#define CLK_GATE_DMA      BIT(8)
#define CLK_GATE_RF_CFG   BIT(9)    /* internal, do not touch */
#define CLK_GATE_TIMER    BIT(10)
#define CLK_GATE_GPIO     BIT(11)
#define CLK_GATE_SDADC    BIT(12)
#define CLK_GATE_PWM      BIT(13)
#define CLK_GATE_LCD      BIT(14)
#define CLK_GATE_I2S      BIT(15)
#define CLK_GATE_RSA      BIT(16)
#define CLK_GATE_GPSEC    BIT(17)
#define CLK_GATE_SDIO_M   BIT(18)
#define CLK_GATE_QSRAM    BIT(19)
#define CLK_GATE_BT       BIT(20)
#define CLK_GATE_TOUCH    BIT(21)

/* CLK->RST_CTRL bits (write 0 = assert reset, 1 = release). */
#define CLK_RST_BBP       BIT(0)    /* internal */
#define CLK_RST_MAC       BIT(1)    /* internal */
#define CLK_RST_SEC       BIT(2)    /* internal */
#define CLK_RST_SDIO_AHB  BIT(3)
#define CLK_RST_DMA       BIT(4)
#define CLK_RST_MEM_MNG   BIT(5)    /* internal */
#define CLK_RST_APB       BIT(6)    /* not recommended */
#define CLK_RST_BUS1      BIT(7)    /* not recommended */
#define CLK_RST_BUS2      BIT(8)    /* not recommended */
#define CLK_RST_I2C       BIT(9)
#define CLK_RST_UART0     BIT(10)
#define CLK_RST_UART1     BIT(11)
#define CLK_RST_UART2     BIT(12)
#define CLK_RST_UART3     BIT(13)
#define CLK_RST_UART4     BIT(14)
#define CLK_RST_UART5     BIT(15)
#define CLK_RST_LSPI      BIT(16)
#define CLK_RST_HSPI      BIT(17)
#define CLK_RST_RF_CFG    BIT(18)   /* internal */
#define CLK_RST_GPIO      BIT(19)
#define CLK_RST_TIMER     BIT(20)
#define CLK_RST_SARADC    BIT(21)
#define CLK_RST_PWM       BIT(22)
#define CLK_RST_LCD       BIT(23)
#define CLK_RST_I2S       BIT(24)
#define CLK_RST_RSA       BIT(25)
#define CLK_RST_GPSEC     BIT(26)
#define CLK_RST_SDIO_M    BIT(27)
#define CLK_RST_QSPI_RAM  BIT(28)
#define CLK_RST_BT        BIT(29)
#define CLK_RST_FLASH     BIT(30)
#define CLK_RST_TOUCH     BIT(31)

/* CLK->DIV fields */
#define CLK_DIV_CPU_Pos       0
#define CLK_DIV_CPU_Msk       (0xFFUL << CLK_DIV_CPU_Pos)   /* PLL / n, n >= 2   */
#define CLK_DIV_WLAN_Pos      8
#define CLK_DIV_WLAN_Msk      (0xFFUL << CLK_DIV_WLAN_Pos)  /* PLL / n, n >= 3   */
#define CLK_DIV_BUS2_Pos      16
#define CLK_DIV_BUS2_Msk      (0xFFUL << CLK_DIV_BUS2_Pos)  /* cpu:bus2 ratio    */
#define CLK_DIV_PERI_Pos      24
#define CLK_DIV_PERI_Msk      (0xFUL  << CLK_DIV_PERI_Pos)  /* PLL / 3 = 160 MHz */
#define CLK_DIV_UPDATE        BIT(31)   /* set to latch; HW clears when done */

/* CLK->SEL fields */
#define CLK_SEL_RSA_160M      BIT(4)
#define CLK_SEL_GPSEC_160M    BIT(5)
#define CLK_SEL_QFLASH_80M    BIT(6)
#define CLK_SEL_SDADC_DIV_Pos 8
#define CLK_SEL_SDADC_DIV_Msk (0xFFUL << CLK_SEL_SDADC_DIV_Pos)
#define CLK_SEL_JTAG_EN       BIT(16)

/* CLK->RST_STATUS (manual 5.4.8). The status bits survive a watchdog reset.
 * The manual (and wm_iot_sdk's wm_rcc_reg_struct.h) list the clear controls
 * as fields [7:2] and [17:8]. wm_iot_sdk clears them by writing 1 to the
 * field, which sets the field's lowest bit, so that is what we use. */
#define CLK_RSTSTAT_WDG       BIT(0)   /* RO: last reset by watchdog        */
#define CLK_RSTSTAT_CPU       BIT(1)   /* RO: last reset by CPU soft reset  */
#define CLK_RSTSTAT_WDG_CLR   BIT(2)   /* WO: write 1 to clear CLK_RSTSTAT_WDG */
#define CLK_RSTSTAT_CPU_CLR   BIT(8)   /* WO: write 1 to clear CLK_RSTSTAT_CPU */

/* ------------------------------------------------------------------------ */
/* PMU and RTC (manual chapters 20 and 21; bit positions checked against    */
/* wm_iot_sdk wm_pmu_reg_struct.h / wm_rtc_reg_struct.h and wm_sdk_w80x,    */
/* where the manual disagrees: see docs/mcu/rtc.md)                         */
/* ------------------------------------------------------------------------ */

typedef struct {
    __IO uint32_t CTRL;      /* 0x00 PS_CR: 32K clock, standby/sleep        */
    __IO uint32_t TIMER0;    /* 0x04 PMU timer 0 (seconds)                  */
    __IO uint32_t TIMER1;    /* 0x08 PMU timer 1 (Wi-Fi listen interval)    */
    __IO uint32_t RTC_R1;    /* 0x0C RTC day, hour, minute, second, alarm   */
    __IO uint32_t RTC_R2;    /* 0x10 RTC year, month, count enable          */
    __IO uint32_t INT_SRC;   /* 0x14 interrupt flags, write 1 to clear      */
         uint32_t RESERVED0; /* 0x18                                        */
    __IO uint32_t WLAN_STTS; /* 0x1C LDO / 40 MHz / WLAN idle status        */
    __IO uint32_t BK;        /* 0x20 DPLL / crystal buffer settings         */
} PMU_TypeDef;

#define PMU ((PMU_TypeDef *)PMU_BASE)

/* PMU->CTRL (PS_CR) */
#define PMU_CTRL_STANDBY      BIT(0)   /* write 1: enter standby            */
#define PMU_CTRL_SLEEP        BIT(1)   /* write 1: enter sleep              */
#define PMU_CTRL_CAL32K       BIT(3)   /* RC 32K calibration: write 0 then 1 */
#define PMU_CTRL_BYPASS32K    BIT(4)   /* 1: 32K clock = 40 MHz crystal / divider, 0: RC */

/* PMU->INT_SRC: write 1 to clear a flag */
#define PMU_INT_TIMER0        BIT(0)
#define PMU_INT_TIMER1        BIT(1)
#define PMU_INT_IO_WAKE       BIT(2)
#define PMU_INT_IO_SLEEP      BIT(3)
#define PMU_INT_RTC           BIT(4)   /* manual table says bit 5: wrong    */
#define PMU_INT_DPLL_TIMEOUT  BIT(5)   /* RO */
#define PMU_INT_OSC_TIMEOUT   BIT(6)   /* RO */
#define PMU_INT_STANDBY_WAKE  BIT(7)   /* RO: woke up from standby          */
#define PMU_INT_SLEEP_WAKE    BIT(8)   /* woke up from sleep, write 1 to clear */

/* PMU->RTC_R1 / RTC_R2 (binary values, not BCD) */
#define RTC_R1_SEC_Pos        0
#define RTC_R1_SEC_Msk        (0x3FUL << RTC_R1_SEC_Pos)
#define RTC_R1_MIN_Pos        8
#define RTC_R1_MIN_Msk        (0x3FUL << RTC_R1_MIN_Pos)
#define RTC_R1_HOUR_Pos       16
#define RTC_R1_HOUR_Msk       (0x1FUL << RTC_R1_HOUR_Pos)
#define RTC_R1_DAY_Pos        24
#define RTC_R1_DAY_Msk        (0x1FUL << RTC_R1_DAY_Pos)
#define RTC_R1_ALARM_EN       BIT(31)  /* write with this set: the fields are the alarm time */
#define RTC_R2_MON_Pos        0
#define RTC_R2_MON_Msk        (0xFUL << RTC_R2_MON_Pos)
#define RTC_R2_YEAR_Pos       8        /* years since 2000 */
#define RTC_R2_YEAR_Msk       (0x7FUL << RTC_R2_YEAR_Pos)
#define RTC_R2_EN             BIT(16)  /* 1: the calendar counts */

/* ------------------------------------------------------------------------ */
/* GPIO (manual chapter 9)                                                  */
/* ------------------------------------------------------------------------ */

typedef struct {
    __IO uint32_t DATA;      /* 0x00 pin level / output value               */
    __IO uint32_t DATA_EN;   /* 0x04 per-bit access mask for DATA (1 = rw)  */
    __IO uint32_t DIR;       /* 0x08 1 = output, 0 = input                  */
    __IO uint32_t PULL_EN;   /* 0x0C pull-up, ACTIVE LOW: 0 = pull-up on    */
    __IO uint32_t AF_SEL;    /* 0x10 1 = alternate function, 0 = GPIO       */
    __IO uint32_t AF_S1;     /* 0x14 alternate function select, high bit    */
    __IO uint32_t AF_S0;     /* 0x18 alternate function select, low bit     */
    __IO uint32_t DN_EN;     /* 0x1C pull-down, active high: 1 = on         */
    __IO uint32_t IS;        /* 0x20 irq sense: 0 = edge, 1 = level         */
    __IO uint32_t IBE;       /* 0x24 irq both edges: 1 = both               */
    __IO uint32_t IEV;       /* 0x28 irq event: 0 = low/falling, 1 = high/rising */
    __IO uint32_t IE;        /* 0x2C irq enable                             */
    __I  uint32_t RIS;       /* 0x30 raw irq status                         */
    __I  uint32_t MIS;       /* 0x34 masked irq status                      */
    __O  uint32_t IC;        /* 0x38 irq clear (write 1)                    */
} GPIO_TypeDef;

#define GPIOA ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB ((GPIO_TypeDef *)GPIOB_BASE)

/* ------------------------------------------------------------------------ */
/* Watchdog (manual chapter 22)                                             */
/* ------------------------------------------------------------------------ */

typedef struct {
    __IO uint32_t LD;        /* 0x00 WD_LD    reload value, in APB clocks   */
    __I  uint32_t VAL;       /* 0x04 WD_VAL   current count (counts down)   */
    __IO uint32_t CTRL;      /* 0x08 WD_CTRL                                */
    __O  uint32_t CLR;       /* 0x0C WD_CLR   write anything: clear irq (= feed) */
    __I  uint32_t SRC;       /* 0x10 WD_SRC   raw interrupt flag            */
    __I  uint32_t STATE;     /* 0x14 WD_STATE interrupt output (masked)     */
         uint32_t RESERVED0[10];
    __IO uint32_t LOCK;      /* 0x40 not in the manual; from wm_sdk_w80x */
} WDG_TypeDef;

#define WDG ((WDG_TypeDef *)WDG_BASE)

#define WDG_CTRL_EN           BIT(0)   /* run the timer, interrupt on every timeout */
#define WDG_CTRL_RST_EN       BIT(1)   /* reset if a timeout finds the irq still set */
#define WDG_LOCK_KEY          0x1ACCE551UL  /* write to unlock; any other value locks */

/* ------------------------------------------------------------------------ */
/* UART0..UART5 (layout checked against wm_iot_sdk                         */
/* wm_uart_reg_struct.h and wm_sdk_w80x wm_regs.h)                          */
/* ------------------------------------------------------------------------ */

typedef struct {
    __IO uint32_t LC;        /* 0x00 line control                           */
    __IO uint32_t FC;        /* 0x04 automatic flow control (RTS/CTS)       */
    __IO uint32_t DMAC;      /* 0x08 DMA control, RX timeout                */
    __IO uint32_t FIFOC;     /* 0x0C FIFO reset / trigger levels            */
    __IO uint32_t BAUDR;     /* 0x10 [15:0] divider, [19:16] fraction       */
    __IO uint32_t INTM;      /* 0x14 interrupt mask: 1 = masked (off)       */
    __IO uint32_t INTS;      /* 0x18 interrupt status, write 1 to clear     */
    __I  uint32_t FIFOS;     /* 0x1C FIFO levels, CTS                       */
    __O  uint32_t TDW;       /* 0x20 TX data window (write a byte)          */
         uint32_t RESERVED0[3];
    __I  uint32_t RDW;       /* 0x30 RX data window (read a byte)           */
         uint32_t RESERVED1[3];
    __IO uint32_t GT;        /* 0x40 7816 guard time                        */
    __IO uint32_t WT;        /* 0x44 7816 wait time                         */
} UART_TypeDef;

#define UART0 ((UART_TypeDef *)UART0_BASE)
#define UART1 ((UART_TypeDef *)UART1_BASE)
#define UART2 ((UART_TypeDef *)UART2_BASE)
#define UART3 ((UART_TypeDef *)UART3_BASE)
#define UART4 ((UART_TypeDef *)UART4_BASE)
#define UART5 ((UART_TypeDef *)UART5_BASE)

/* LC */
#define UART_LC_DATA_5        0x0u
#define UART_LC_DATA_6        0x1u
#define UART_LC_DATA_7        0x2u
#define UART_LC_DATA_8        0x3u
#define UART_LC_DATA_Msk      0x3u
#define UART_LC_STOP_2        BIT(2)
#define UART_LC_PARITY_EN     BIT(3)
#define UART_LC_PARITY_ODD    BIT(4)   /* with PARITY_EN; 0 = even */
#define UART_LC_BREAK         BIT(5)   /* send a break, self-clearing */
#define UART_LC_TX_EN         BIT(6)
#define UART_LC_RX_EN         BIT(7)
#define UART_LC_MODE_7816     BIT(24)

/* FC */
#define UART_FC_AFC_EN        BIT(0)
#define UART_FC_RTS_SET       BIT(1)

/* DMAC */
#define UART_DMAC_TX_EN       BIT(0)   /* raise DMA requests for TX       */
#define UART_DMAC_RX_EN       BIT(1)   /* raise DMA requests for RX       */
#define UART_DMAC_RX_TO_EN    BIT(2)   /* RX timeout while DMA is on      */
#define UART_DMAC_RX_TO_Pos   3        /* [7:3] RX timeout length         */

/* FIFOC */
#define UART_FIFOC_TX_RST     BIT(0)
#define UART_FIFOC_RX_RST     BIT(1)
#define UART_FIFOC_TXLVL_Pos  2        /* UART_INT_LEVEL_*: empty, 4, 8, 16 bytes */
#define UART_FIFOC_RXLVL_Pos  4        /* UART_INT_LEVEL_*: 1, 4, 8, 16 bytes     */

/* BAUDR */
#define UART_BAUDR_DIV_Msk    0x0000FFFFUL
#define UART_BAUDR_FRAC_Pos   16
#define UART_BAUDR_FRAC_Msk   0x000F0000UL

/* INTM / INTS */
#define UART_INT_TX_EMPTY     BIT(0)   /* TX FIFO empty                   */
#define UART_INT_TX_LEVEL     BIT(1)   /* TX FIFO at its trigger level    */
#define UART_INT_RX_LEVEL     BIT(2)   /* RX FIFO at its trigger level    */
#define UART_INT_RX_TIMEOUT   BIT(3)   /* RX data waiting, line idle      */
#define UART_INT_CTS          BIT(4)   /* CTS changed                     */
#define UART_INT_BREAK        BIT(5)
#define UART_INT_FRAME_ERR    BIT(6)
#define UART_INT_PARITY_ERR   BIT(7)
#define UART_INT_OVERRUN      BIT(8)
#define UART_INT_ALL          0x3FFUL
#define UART_INT_RX_ERRORS    (UART_INT_BREAK | UART_INT_FRAME_ERR | UART_INT_PARITY_ERR | UART_INT_OVERRUN)

/* FIFOS */
#define UART_FIFOS_TX_Msk     0x0000003FUL
#define UART_FIFOS_RX_Pos     6
#define UART_FIFOS_RX_Msk     0x00000FC0UL
#define UART_FIFOS_CTS        BIT(12)

/* ------------------------------------------------------------------------ */
/* DMA: 8 channels (layout checked against wm_iot_sdk wm_dma_reg_struct.h   */
/* and wm_sdk_w80x wm_regs.h / wm_dma.c)                                    */
/* ------------------------------------------------------------------------ */

typedef struct {
    __IO uint32_t SRC;       /* 0x00 source address                         */
    __IO uint32_t DST;       /* 0x04 destination address                    */
    __IO uint32_t SRC_WRAP;  /* 0x08 source ring start (wrap mode)          */
    __IO uint32_t DST_WRAP;  /* 0x0C destination ring start (wrap mode)     */
    __IO uint32_t WRAP_SIZE; /* 0x10 [15:0] source, [31:16] dest ring length */
    __IO uint32_t CHCTRL;    /* 0x14 start / stop                           */
    __IO uint32_t MODE;      /* 0x18 software/hardware, request, chain      */
    __IO uint32_t CTRL;      /* 0x1C increments, width, burst, length       */
    __I  uint32_t STATUS;    /* 0x20 [15:0] bytes moved since start         */
    __IO uint32_t DESC;      /* 0x24 first descriptor (chain mode)          */
    __I  uint32_t CUR_DST;   /* 0x28 current destination address            */
         uint32_t RESERVED;  /* 0x2C                                        */
} DMA_Channel_TypeDef;

typedef struct {
    __IO uint32_t INTM;      /* 0x00 interrupt mask: 1 = masked (off)       */
    __IO uint32_t INTS;      /* 0x04 interrupt status, write 1 to clear     */
    __IO uint32_t UART_SEL;  /* 0x08 [2:0] UART served by UART DMA requests,
                                     [23:8] write 1 to clear a request      */
         uint32_t RESERVED;  /* 0x0C                                        */
    DMA_Channel_TypeDef CH[8];  /* 0x10 + 0x30 * n                          */
} DMA_TypeDef;

#define DMA ((DMA_TypeDef *)DMA_BASE)
#define DMA_CHANNELS          8u

/* INTM / INTS: two bits per channel */
#define DMA_INT_BURST(ch)     BIT(2u * (ch))        /* one burst moved      */
#define DMA_INT_DONE(ch)      BIT(2u * (ch) + 1u)   /* whole transfer done  */
#define DMA_INT_CH(ch)        (3UL << (2u * (ch)))

#define DMA_UART_SEL_Msk      0x7UL
#define DMA_UART_SEL_REQCLR_Pos 8

/* CHCTRL */
#define DMA_CHCTRL_ON         BIT(0)   /* write 1 to start; reads 1 while running */
#define DMA_CHCTRL_OFF        BIT(1)   /* write 1 to stop after the current burst */

/* MODE */
#define DMA_MODE_HW           BIT(0)   /* paced by a peripheral request (else memory-to-memory) */
#define DMA_MODE_CHAIN        BIT(1)   /* take the transfer from descriptors (DESC) */
#define DMA_MODE_REQ_Pos      2        /* [5:2] request, see dma_req_t */
#define DMA_MODE_REQ_Msk      (0xFUL << DMA_MODE_REQ_Pos)
#define DMA_MODE_CHAIN_LINK   BIT(6)

/* CTRL */
#define DMA_CTRL_AUTO_RELOAD  BIT(0)
#define DMA_CTRL_SRC_Pos      1        /* [2:1] 0 fixed, 1 increment, 3 wrap */
#define DMA_CTRL_DST_Pos      3        /* [4:3] 0 fixed, 1 increment, 3 wrap */
#define DMA_CTRL_WIDTH_Pos    5        /* [6:5] 0 byte, 1 half-word, 2 word  */
#define DMA_CTRL_BURST4       BIT(7)   /* 4 items per burst (else 1)          */
#define DMA_CTRL_LEN_Pos      8        /* [23:8] total length in bytes        */
#define DMA_CTRL_LEN_Msk      (0xFFFFUL << DMA_CTRL_LEN_Pos)
#define DMA_MAX_LEN           0xFFFFu

/* ------------------------------------------------------------------------ */
/* Timers: 6 x 32-bit (manual chapter 19). The manual numbers them 1..6;    */
/* this SDK uses 0..5, like both vendor SDKs.                               */
/* ------------------------------------------------------------------------ */

typedef struct {
    __IO uint32_t CFG;       /* 0x00 TMR_CONFIG prescaler: APB MHz - 1 -> 1 us tick */
    __IO uint32_t CSR;       /* 0x04 TMR_CSR    5 control bits per timer           */
    __IO uint32_t PRD[6];    /* 0x08 TMRn_PRD   timing value, in us or ms          */
    __I  uint32_t CNT[6];    /* 0x20 TMRn_CNT   current count                      */
} TIMER_TypeDef;

#define TIMER ((TIMER_TypeDef *)TIMER_BASE)
#define TIMER_NUM             6u

/* Timer ids for w80x_timer.h and the TIMER_CSR_*(n) macros. The manual
 * numbers the timers 1..6, so TIMER0 is the manual's timer 1. In
 * timer_irq_status() masks, timer n is BIT(TIMERn). */
#define TIMER0                0u
#define TIMER1                1u
#define TIMER2                2u
#define TIMER3                3u
#define TIMER4                4u
#define TIMER5                5u

#define TIMER_CFG_Msk         0x7FUL

/* TMR_CSR: bits [5n+4 : 5n] belong to timer n */
#define TIMER_CSR_MS(n)       BIT(5u * (n))        /* unit: 1 = ms, 0 = us                 */
#define TIMER_CSR_ONESHOT(n)  BIT(5u * (n) + 1u)   /* 1 = one-shot, stops itself; 0 = repeat */
#define TIMER_CSR_EN(n)       BIT(5u * (n) + 2u)   /* running                              */
#define TIMER_CSR_IE(n)       BIT(5u * (n) + 3u)   /* interrupt enable                     */
#define TIMER_CSR_IF(n)       BIT(5u * (n) + 4u)   /* interrupt flag, write 1 to clear     */
#define TIMER_CSR_IF_ALL      (TIMER_CSR_IF(0) | TIMER_CSR_IF(1) | TIMER_CSR_IF(2) | \
                               TIMER_CSR_IF(3) | TIMER_CSR_IF(4) | TIMER_CSR_IF(5))
#define TIMER_CSR_FIELD(n)    (0x1FUL << (5u * (n)))

/* ------------------------------------------------------------------------ */
/* LSPI: SPI master/slave (manual ch. 14). 8-word (32-byte) TX and RX FIFOs. */
/* ------------------------------------------------------------------------ */

typedef struct {
    __IO uint32_t CH_CFG;    /* 0x00 channel config: start, length, channels       */
    __IO uint32_t CFG;       /* 0x04 SPI config: mode, role, byte order, timing     */
    __IO uint32_t CLK_CFG;   /* 0x08 SCK = APB / (2 * (div + 1))                    */
    __IO uint32_t MODE_CFG;  /* 0x0C DMA enables and FIFO trigger levels            */
    __IO uint32_t INT_MASK;  /* 0x10 1 = masked (reset: all masked)                 */
    __IO uint32_t INT_SRC;   /* 0x14 interrupt status, write 1 to clear             */
    __I  uint32_t STATUS;    /* 0x18 FIFO levels (bytes), busy                      */
    __IO uint32_t TIMEOUT;   /* 0x1C RX timeout                                     */
    __IO uint32_t TXDATA;    /* 0x20 TX FIFO window (32-bit words)                  */
    __IO uint32_t TRANS_MODE;/* 0x24 TI / microwire frame settings                  */
    __I  uint32_t SLV_LEN;   /* 0x28 slave: RX length [15:0], TX length [31:16]     */
    uint32_t      RESERVED;  /* 0x2C                                                */
    __IO uint32_t RXDATA;    /* 0x30 RX FIFO window (32-bit words)                  */
} SPI_TypeDef;

#define SPI ((SPI_TypeDef *)SPI_BASE)
#define SPI_FIFO_SIZE           32u                /* bytes, per direction */

/* CH_CFG */
#define SPI_CH_START            BIT(0)             /* self-clearing */
#define SPI_CH_CS_FORCE         BIT(1)             /* 1 = CS pin level = SPI_CH_CS_LEVEL */
#define SPI_CH_CS_LEVEL         BIT(2)             /* active CS level, or forced level */
#define SPI_CH_LEN_Pos          3                  /* transfer length in bits, 1..65535 */
#define SPI_CH_LEN_Msk          (0xFFFFUL << SPI_CH_LEN_Pos)
#define SPI_CH_LEN(bits)        (((uint32_t)(bits) << SPI_CH_LEN_Pos) & SPI_CH_LEN_Msk)
#define SPI_CH_TX_ON            BIT(19)
#define SPI_CH_RX_ON            BIT(20)
#define SPI_CH_CONTINUE         BIT(21)            /* keep clocking with TX FIFO empty */
#define SPI_CH_CLEAR_FIFO       BIT(22)            /* clears FIFOs + resets the engine; self-clearing */
#define SPI_CH_RX_SKIP_Pos      23                 /* first N received bits are dropped */
#define SPI_CH_RX_SKIP_Msk      (0xFFUL << SPI_CH_RX_SKIP_Pos)

/* CFG */
#define SPI_CFG_CPOL            BIT(0)             /* SCK idles high */
#define SPI_CFG_CPHA            BIT(1)             /* sample on the second edge */
#define SPI_CFG_MASTER          BIT(2)             /* reset value: master */
#define SPI_CFG_BIG_ENDIAN      BIT(3)             /* 1 = high byte of a FIFO word first */
#define SPI_CFG_FRAME_DELAY_Pos 4
#define SPI_CFG_OUT_DELAY_Pos   7                  /* MOSI delay vs SCK, APB cycles 0..3 */
#define SPI_CFG_OUT_DELAY_Msk   (3u << SPI_CFG_OUT_DELAY_Pos)
#define SPI_CFG_CS_SETUP_Pos    9                  /* 2^n APB cycles, n = 0..7 */
#define SPI_CFG_CS_HOLD_Pos     12
#define SPI_CFG_TX_ALWAYS       BIT(16)            /* drive MOSI even with CS inactive */
#define SPI_CFG_FORMAT_Pos      17                 /* 0 Motorola, 1 TI, 2 microwire */
#define SPI_CFG_FORMAT_Msk      (3UL << SPI_CFG_FORMAT_Pos)

/* MODE_CFG */
#define SPI_MODECFG_TX_DMA         BIT(0)
#define SPI_MODECFG_RX_DMA         BIT(1)
#define SPI_MODECFG_TX_TRIG_Pos    2                  /* words, 0..7 */
#define SPI_MODECFG_RX_TRIG_Pos    6

/* INT_MASK / INT_SRC */
#define SPI_INT_TX_READY        BIT(0)
#define SPI_INT_RX_READY        BIT(1)
#define SPI_INT_TX_UNDERRUN     BIT(2)
#define SPI_INT_TX_OVERRUN      BIT(3)
#define SPI_INT_RX_UNDERRUN     BIT(4)
#define SPI_INT_RX_OVERRUN      BIT(5)
#define SPI_INT_DONE            BIT(6)
#define SPI_INT_TIMEOUT         BIT(7)
#define SPI_INT_ALL             0xFFUL

/* STATUS */
#define SPI_STATUS_TX_LEVEL_Msk 0x3FUL             /* bytes in TX FIFO */
#define SPI_STATUS_RX_LEVEL_Pos 6
#define SPI_STATUS_RX_LEVEL_Msk (0x3FUL << SPI_STATUS_RX_LEVEL_Pos)
#define SPI_STATUS_BUSY         BIT(12)

/* ------------------------------------------------------------------------ */
/* GPSEC general crypto engine (manual ch. 7): SHA1/MD5/AES/DES/3DES/RC4/   */
/* CRC/RNG. It DMAs SRC_ADDR -> DEST_ADDR for CFG.TOTAL bytes by itself.    */
/* Bit layout cross-checked with wm_iot_sdk wm_gpsec_reg_struct.h.          */
/* ------------------------------------------------------------------------ */

typedef struct {
    __IO uint32_t SRC;       /* 0x00 source address                                 */
    __IO uint32_t DST;       /* 0x04 destination (RC4/AES/DES/3DES)                 */
    __IO uint32_t CFG;       /* 0x08 algorithm, mode, length                        */
    __IO uint32_t CTRL;      /* 0x0C start / stop / CRC FIFO clear                  */
    __IO uint32_t KEY0[2];   /* 0x10 key 0 (lo, hi); KEY0[0] = PRNG seed            */
    __IO uint32_t KEY1[2];   /* 0x18 key 1                                          */
    __IO uint32_t KEY2[2];   /* 0x20 key 2 (3DES) / IV bytes 8..15 (AES)            */
    __IO uint32_t IV0[2];    /* 0x28 IV bytes 0..7 (AES/DES/3DES)                   */
    __IO uint32_t STS;       /* 0x30 done flag, byte count                          */
    __IO uint32_t DIGEST[5]; /* 0x34 SHA1/MD5 state; DIGEST[4] = CRC in/out         */
    __IO uint32_t RNG;       /* 0x48 PRNG / TRNG result                             */
    __IO uint32_t KEY3[2];   /* 0x4C key 3 (RC4 256-bit)                            */
    __IO uint32_t TRNG_CR;   /* 0x54 TRNG control                                   */
} GPSEC_TypeDef;

#define GPSEC ((GPSEC_TypeDef *)GPSEC_BASE)

#define GPSEC_CFG_TOTAL_Msk     0xFFFFUL           /* bytes to process, max 65535      */
#define GPSEC_CFG_ALG_Pos       16
#define GPSEC_CFG_ALG_Msk       (7UL << GPSEC_CFG_ALG_Pos)
#define GPSEC_CFG_ALG(a)        ((uint32_t)(a) << GPSEC_CFG_ALG_Pos)
#define GPSEC_ALG_RC4           1u
#define GPSEC_ALG_SHA1          2u
#define GPSEC_ALG_AES           3u
#define GPSEC_ALG_DES           4u
#define GPSEC_ALG_3DES          5u
#define GPSEC_ALG_CRC           6u
#define GPSEC_ALG_MD5           7u
#define GPSEC_CFG_INT_MASK      BIT(19)            /* 1 = no done interrupt (flag still sets) */
#define GPSEC_CFG_DECRYPT       BIT(20)            /* 0 = encrypt                       */
#define GPSEC_CFG_SUB_Pos       21                 /* ECB/CBC/CTR, or CRC type          */
#define GPSEC_CFG_SUB_Msk       (3UL << GPSEC_CFG_SUB_Pos)
#define GPSEC_CFG_SUB(s)        ((uint32_t)(s) << GPSEC_CFG_SUB_Pos)
#define GPSEC_SUB_ECB           0u
#define GPSEC_SUB_CBC           1u
#define GPSEC_SUB_CTR           2u                 /* AES only                          */
#define GPSEC_SUB_CRC8          0u
#define GPSEC_SUB_CRC16_MODBUS  1u                 /* poly 0x8005                       */
#define GPSEC_SUB_CRC16_CCITT   2u                 /* poly 0x1021                       */
#define GPSEC_SUB_CRC32         3u
#define GPSEC_CFG_CRC_REV_OUT   BIT(23)            /* reflect the CRC result            */
#define GPSEC_CFG_CRC_REV_IN    BIT(24)            /* reflect each input byte           */
#define GPSEC_CFG_RC4_RESET     BIT(25)            /* self-clearing soft resets         */
#define GPSEC_CFG_AES_RESET     BIT(26)
#define GPSEC_CFG_DES_RESET     BIT(27)
#define GPSEC_CFG_PRNG_32BIT    BIT(28)            /* 0 = 16-bit results                */
#define GPSEC_CFG_PRNG_LOAD     BIT(29)            /* load seed from KEY0[0]            */
#define GPSEC_CFG_PRNG_START    BIT(30)
#define GPSEC_CFG_RC4_256       BIT(31)            /* 0 = 128-bit key                   */

#define GPSEC_CTRL_START        BIT(0)             /* self-clearing                     */
#define GPSEC_CTRL_STOP         BIT(1)
#define GPSEC_CTRL_CRC_CLR      BIT(2)             /* clear CRC FIFO (from wm_iot_sdk)  */

#define GPSEC_STS_COUNT_Msk     0xFFFFUL           /* bytes done so far                 */
#define GPSEC_STS_DONE          BIT(16)            /* write 1 to clear                  */

#define GPSEC_TRNG_EN           BIT(0)
#define GPSEC_TRNG_SEL          BIT(1)             /* RNG register shows TRNG, not PRNG */
#define GPSEC_TRNG_BYPASS       BIT(2)             /* skip digital post-processing      */
#define GPSEC_TRNG_CP_Pos       3                  /* charge pump, wm_iot_sdk uses 4    */
#define GPSEC_TRNG_CP_Msk       (7UL << GPSEC_TRNG_CP_Pos)
#define GPSEC_TRNG_INT_MASK     BIT(6)             /* 0 = each new word sets STS DONE   */

/* ------------------------------------------------------------------------ */
/* RSA Montgomery modular multiplier (manual ch. 8), 128..2048 bits in      */
/* 32-bit steps. Numbers are little-endian word arrays: word 0 = bits 31:0. */
/* ------------------------------------------------------------------------ */

typedef struct {
    __IO uint32_t X[64];     /* 0x000 */
    __IO uint32_t Y[64];     /* 0x100 */
    __IO uint32_t M[64];     /* 0x200 modulus */
    __IO uint32_t D[64];     /* 0x300 */
    __IO uint32_t CTRL;      /* 0x400 */
    __O  uint32_t MC;        /* 0x404 -M^-1 mod 2^32, write-only */
    __IO uint32_t N;         /* 0x408 length in 32-bit words, 4..64 */
} RSA_TypeDef;

#define RSA ((RSA_TypeDef *)RSA_BASE)

#define RSA_CTRL_DONE         BIT(0)    /* set by HW, write 0 to clear      */
#define RSA_CTRL_MODE_Pos     2
#define RSA_CTRL_MODE(m)      ((uint32_t)(m) << RSA_CTRL_MODE_Pos)
#define RSA_MODE_X_DD         0u        /* X = D*D / R mod M                */
#define RSA_MODE_D_XY         1u        /* D = X*Y / R mod M                */
#define RSA_MODE_X_DY         2u        /* X = D*Y / R mod M                */
#define RSA_MODE_D_XX         3u        /* D = X*X / R mod M                */
#define RSA_CTRL_RESET        BIT(4)    /* self-clearing; zeroes MC and N   */
#define RSA_CTRL_START        BIT(5)    /* self-clearing                    */

/* ------------------------------------------------------------------------ */
/* XT804 core timer (CORET, SysTick-like 24-bit down counter)               */
/* ------------------------------------------------------------------------ */

typedef struct {
    __IO uint32_t CTRL;      /* 0x00 */
    __IO uint32_t LOAD;      /* 0x04 */
    __IO uint32_t VAL;       /* 0x08 */
    __I  uint32_t CALIB;     /* 0x0C */
} CORET_TypeDef;

#define CORET ((CORET_TypeDef *)CORET_BASE)

#define CORET_CTRL_ENABLE     BIT(0)
#define CORET_CTRL_TICKINT    BIT(1)
#define CORET_CTRL_CLKSOURCE  BIT(2)   /* 1 = CPU clock */
#define CORET_CTRL_COUNTFLAG  BIT(16)
#define CORET_MAX             0x00FFFFFFUL

/* Current CPU cycle count. SystemInit() runs CORET free at the CPU clock as
 * a 24-bit DOWN counter, so it wraps every 2^24 cycles (70 ms at 240 MHz)
 * and later readings are smaller: measure with cpu_cycles_since()
 * (w80x_utils.h) rather than subtracting. */
#define CPU_TICKS             (CORET->VAL)

/* ------------------------------------------------------------------------ */
/* XT804 vectored interrupt controller (VIC)                                */
/* ------------------------------------------------------------------------ */
/* Part of the CK804 core, so it isn't in the W800 register manual. It is
 * documented in T-Head's CK804 User Manual (VIC chapter), and the CSI header
 * core_804.h in wm_sdk_w80x has the same layout. The enable/pending/active/
 * priority registers work like the ARMv7-M NVIC. */

typedef struct {
    __IO uint32_t ISER[4];  uint32_t RESERVED0[12];   /* 0x000 set enable    */
    __IO uint32_t IWER[4];  uint32_t RESERVED1[12];   /* 0x040 wake-up set   */
    __IO uint32_t ICER[4];  uint32_t RESERVED2[12];   /* 0x080 clear enable  */
    __IO uint32_t IWDR[4];  uint32_t RESERVED3[12];   /* 0x0C0 wake-up clear */
    __IO uint32_t ISPR[4];  uint32_t RESERVED4[12];   /* 0x100 set pending   */
    __IO uint32_t ISSR[4];  uint32_t RESERVED5[12];   /* 0x140 security set  */
    __IO uint32_t ICPR[4];  uint32_t RESERVED6[12];   /* 0x180 clear pending */
    __IO uint32_t ICSR[4];  uint32_t RESERVED7[12];   /* 0x1C0 security clr  */
    __IO uint32_t IABR[4];  uint32_t RESERVED8[60];   /* 0x200 active        */
    __IO uint32_t IPR[32];  uint32_t RESERVED9[480];  /* 0x300 priority      */
    __I  uint32_t ISR;                                /* 0xB00 */
    __IO uint32_t IPTR;                               /* 0xB04 */
    __IO uint32_t TSPEND;                             /* 0xB08 */
    __IO uint32_t TSABR;                              /* 0xB0C */
    __IO uint32_t TSPR;                               /* 0xB10 */
} VIC_TypeDef;

#define VIC ((VIC_TypeDef *)VIC_BASE)

/* External interrupt numbers (vector = 32 + IRQn). */
typedef enum {
    SDIO_IRQn        = 0,
    MAC_IRQn         = 1,
    RF_CFG_IRQn      = 2,
    SEC_IRQn         = 3,
    DMA_CH0_IRQn     = 4,
    DMA_CH1_IRQn     = 5,
    DMA_CH2_IRQn     = 6,
    DMA_CH3_IRQn     = 7,
    DMA_CH4_7_IRQn   = 8,
    DMA_BURST_IRQn   = 9,
    I2C_IRQn         = 10,
    ADC_IRQn         = 11,
    SPI_LS_IRQn      = 12,
    SPI_HS_IRQn      = 13,
    GPIOA_IRQn       = 14,
    GPIOB_IRQn       = 15,
    UART0_IRQn       = 16,
    UART1_IRQn       = 17,
    UART2_IRQn       = 18,   /* UART2 & 7816 */
    UART3_5_IRQn     = 19,
    BLE_IRQn         = 20,
    BT_IRQn          = 21,
    PWM_IRQn         = 22,
    I2S_IRQn         = 23,
    SDIO_HOST_IRQn   = 24,
    CORET_IRQn       = 25,
    RSA_IRQn         = 26,
    GPSEC_IRQn       = 27,
    FLASH_IRQn       = 28,
    PMU_IRQn         = 29,
    TIMER_IRQn       = 30,
    WDG_IRQn         = 31,
} IRQn_Type;

/* ------------------------------------------------------------------------ */
/* CPU intrinsics                                                           */
/* ------------------------------------------------------------------------ */

W80X_INLINE void     __enable_irq(void)   { __asm__ volatile("psrset ie" ::: "memory"); }
W80X_INLINE void     __disable_irq(void)  { __asm__ volatile("psrclr ie" ::: "memory"); }
W80X_INLINE void     __enable_excp(void)  { __asm__ volatile("psrset ee" ::: "memory"); }
W80X_INLINE void     __nop(void)          { __asm__ volatile("nop"); }
/* Low-power wait until an interrupt. Only IRQs enabled with irq_wakeup_enable()
 * (VIC IWER) wake the core from `wait`; others stay pending. */
W80X_INLINE void     __wait(void)         { __asm__ volatile("wait"); }
W80X_INLINE void     __dsb(void)          { __asm__ volatile("sync" ::: "memory"); }

W80X_INLINE uint32_t __get_PSR(void)      { uint32_t r; __asm__ volatile("mfcr %0, psr" : "=r"(r)); return r; }
W80X_INLINE void     __set_PSR(uint32_t v){ __asm__ volatile("mtcr %0, psr" :: "r"(v) : "memory"); }
W80X_INLINE void     __set_VBR(uint32_t v){ __asm__ volatile("mtcr %0, vbr" :: "r"(v) : "memory"); }
W80X_INLINE uint32_t __get_VBR(void)      { uint32_t r; __asm__ volatile("mfcr %0, vbr" : "=r"(r)); return r; }
W80X_INLINE uint32_t __get_CHR(void)      { uint32_t r; __asm__ volatile("mfcr %0, cr<31, 0>" : "=r"(r)); return r; }
W80X_INLINE void     __set_CHR(uint32_t v){ __asm__ volatile("mtcr %0, cr<31, 0>" :: "r"(v) : "memory"); }

#define PSR_IE   BIT(6)
#define CHR_IAE  BIT(4)   /* interrupt stack alignment enable */

/* Critical sections: save PSR, disable IRQs, restore later. */
W80X_INLINE uint32_t irq_save(void)          { uint32_t p = __get_PSR(); __disable_irq(); return p; }
W80X_INLINE void     irq_restore(uint32_t p) { if (p & PSR_IE) { __enable_irq(); } }

/* Per-IRQ control */
W80X_INLINE void irq_enable(IRQn_Type n)        { VIC->ISER[0] = BIT(n); }
W80X_INLINE void irq_disable(IRQn_Type n)       { VIC->ICER[0] = BIT(n); }
W80X_INLINE void irq_set_pending(IRQn_Type n)   { VIC->ISPR[0] = BIT(n); }
W80X_INLINE void irq_clear_pending(IRQn_Type n) { VIC->ICPR[0] = BIT(n); }
/* Let this IRQ wake the core from __wait() (VIC IWER/IWDR). */
W80X_INLINE void irq_wakeup_enable(IRQn_Type n)  { VIC->IWER[0] = BIT(n); }
W80X_INLINE void irq_wakeup_disable(IRQn_Type n) { VIC->IWDR[0] = BIT(n); }
/* prio 0 (highest) .. 3 (lowest); stored in the top 2 bits of each byte. */
W80X_INLINE void irq_set_priority(IRQn_Type n, uint32_t prio)
{
    uint32_t sh = (n & 3u) * 8u;
    VIC->IPR[n >> 2] = (VIC->IPR[n >> 2] & ~(0xFFUL << sh)) | (((prio << 6) & 0xFFUL) << sh);
}

/* ------------------------------------------------------------------------ */
/* System (system.c)                                                        */
/* ------------------------------------------------------------------------ */

void SystemInit(void);                 /* called from startup before main()  */
void delay_ticks(uint32_t cpu_cycles); /* busy-wait on CORET                 */
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);

/* Weak hook, called first thing after reset, before the vector copy,
 * .data/.bss setup and SystemInit(): the registers still hold what the
 * bootloader left. Only W80X_NOINIT variables and const data may be used
 * here (initialized and zeroed globals are not set up yet). */
void w80x_early_init(void);

/* Interrupt / exception handlers (weak, override in your code with W80X_IRQ). */
void Default_Handler(void);
#define W80X_DECLARE_IRQ(name) W80X_IRQ void name(void)
W80X_DECLARE_IRQ(SDIO_IRQHandler);
W80X_DECLARE_IRQ(MAC_IRQHandler);
W80X_DECLARE_IRQ(RF_Cfg_IRQHandler);
W80X_DECLARE_IRQ(SEC_IRQHandler);
W80X_DECLARE_IRQ(DMA_Channel0_IRQHandler);
W80X_DECLARE_IRQ(DMA_Channel1_IRQHandler);
W80X_DECLARE_IRQ(DMA_Channel2_IRQHandler);
W80X_DECLARE_IRQ(DMA_Channel3_IRQHandler);
W80X_DECLARE_IRQ(DMA_Channel4_7_IRQHandler);
W80X_DECLARE_IRQ(DMA_BURST_IRQHandler);
W80X_DECLARE_IRQ(I2C_IRQHandler);
W80X_DECLARE_IRQ(ADC_IRQHandler);
W80X_DECLARE_IRQ(SPI_LS_IRQHandler);
W80X_DECLARE_IRQ(SPI_HS_IRQHandler);
W80X_DECLARE_IRQ(GPIOA_IRQHandler);
W80X_DECLARE_IRQ(GPIOB_IRQHandler);
W80X_DECLARE_IRQ(UART0_IRQHandler);
W80X_DECLARE_IRQ(UART1_IRQHandler);
W80X_DECLARE_IRQ(UART2_IRQHandler);
W80X_DECLARE_IRQ(UART3_5_IRQHandler);
W80X_DECLARE_IRQ(BLE_IRQHandler);
W80X_DECLARE_IRQ(BT_IRQHandler);
W80X_DECLARE_IRQ(PWM_IRQHandler);
W80X_DECLARE_IRQ(I2S_IRQHandler);
W80X_DECLARE_IRQ(SDIO_HOST_IRQHandler);
W80X_DECLARE_IRQ(CORET_IRQHandler);
W80X_DECLARE_IRQ(RSA_IRQHandler);
W80X_DECLARE_IRQ(GPSEC_IRQHandler);
W80X_DECLARE_IRQ(FLASH_IRQHandler);
W80X_DECLARE_IRQ(PMU_IRQHandler);
W80X_DECLARE_IRQ(TIMER_IRQHandler);
W80X_DECLARE_IRQ(WDG_IRQHandler);

#ifdef __cplusplus
}
#endif

#endif /* W80X_H */
