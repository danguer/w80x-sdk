# Clock and reset module (CLK)

The clock and reset module (`CLK`, manual chapter 5) sets the CPU and bus
clocks, turns peripheral clocks on and off, holds peripherals in reset, and
records why the chip last reset.

- **Base address:** `0x4000_0E00`
- **SDK:** `CLK_TypeDef` / `CLK` in [`w80x.h`](../../w80x/w80x.h), driver in
  [`w80x_clock.h`](../../w80x/w80x_clock.h) and [`w80x_clock.c`](../../w80x/w80x_clock.c)
- **Source:** WM_W800 Register Manual v2.1, chapter 5, checked against the
  wm_iot_sdk register structs. v3.0 (2022-08-20, English and Chinese) has
  the same content in this chapter, including the errors listed under
  [Manual discrepancies](#manual-discrepancies). Hardware notes come from the SDK examples
  on an HLK-W801-KIT-V1.1.

## Registers

| Offset | Address | Name | SDK | Reset value | At app start ³ | Description |
|---|---|---|---|---|---|---|
| `0x00` | `0x4000_0E00` | [SW_CLKG_EN](#sw_clkg_en) | `CLK->GATE_EN` | `0x003F_FFFF` ¹ | `0x003F_FFFF` | Peripheral clock on/off |
| `0x04` | `0x4000_0E04` | [SW_CLK_MASK](#sw_clk_mask) | `CLK->GATE_MASK` | `0x0000_007E` | `0x0000_007E` | Allow automatic (adaptive) clock shutdown |
| `0x08` | `0x4000_0E08` | — | — | — | — | Reserved |
| `0x0C` | `0x4000_0E0C` | [SW_RST_CTRL](#sw_rst_ctrl) | `CLK->RST_CTRL` | `0xFFFF_FFFF` ¹ | `0xFFFF_FFFF` | Peripheral reset (0 = held in reset) |
| `0x10` | `0x4000_0E10` | [SYS_CLK_DIV](#sys_clk_div) | `CLK->DIV` | `0x0302_0306` ¹ | `0x0302_0306` | CPU, WLAN, bus and peripheral clock dividers |
| `0x14` | `0x4000_0E14` | [SYS_CLK_SEL](#sys_clk_sel) | `CLK->SEL` | `0x0000_0A00` ¹ | **`0x0001_2840`** | Crypto/flash clock select, ADC divider, JTAG enable |
| `0x18` | `0x4000_0E18` | [I2S_CLK_CTRL](#i2s_clk_ctrl) | `CLK->I2S_CTRL` | `0x0000_0000` | `0x0000_0000` | I2S clock dividers |
| `0x1C` | `0x4000_0E1C` | [RESET_STATUS](#reset_status) | `CLK->RST_STATUS` | `0x0000_0000` | `0x0000_0000` ⁴ | Cause of the last reset |

¹ The manual's register list and its bit tables give different reset values
for these registers. This table uses the bit tables, which match the
hardware. See [Manual discrepancies](#manual-discrepancies).

³ Read on a W801 (HLK-W801-KIT-V1.1) after power-on, before any
application init, with
[`examples/confirm_reset_values`](../../examples/confirm_reset_values/main.c)
*(hardware)*. A bootloader has already run, so a value can differ from the
reset value because the ROM or the bootloader changed it.

**Bootloaders used for the hardware readings:**
- the classic secboot from
  [cjacker/wm_sdk_w80x `tools/w800`](https://github.com/cjacker/wm_sdk_w80x/tree/197ad911d3b3ab988a1f4cb539184984d12e743e/tools/w800)
  (commit `197ad91`), app at `0x080D_0400` (`BOOT=secboot`);
- the bootloader of the official
  [wm_iot_sdk](https://github.com/winnermicro/wm_iot_sdk) (commit
  `e0e4a72`), app at `0x0800_F400` (`BOOT=iot`).

All CLK registers read the same with both. They differ only in which RAM
they overwrite during a watchdog reset (see [RESET_STATUS](#reset_status)).

⁴ After power-on. After a watchdog reset bit 0 is set (see
[RESET_STATUS](#reset_status)).

**Contents:** [Clock tree](#clock-tree) · [Buses](#buses) ·
[Which clock each peripheral uses](#which-clock-each-peripheral-uses) ·
[CPU clock settings](#cpu-clock-settings) ·
[Changing the clock](#changing-the-clock) · [Registers](#register-details) ·
[Manual discrepancies](#manual-discrepancies)

**Legend:** **RW** read/write, **RO** read-only, **WO** write-only. Notes
are tagged by source: *(manual)* register manual, *(SDK)* vendor SDK or this
SDK's code, *(hardware)* seen on a real chip.

---

## Clock tree

```
                        ┌──────────────┐
 40 MHz crystal ──────► │ DPLL  480 MHz│ (fixed)
                        └──────┬───────┘
                               │ pll_out 480 MHz
          ┌────────────────────┼──────────────────────────────┐
          │                    │                              │
    ÷ cpu_clk_divider    ÷ wlan_clk_divider           ÷ Peripheral_divider
      (2..255)             (3..255)                      (3..15, keep 3)
          │                    │                              │
       clk_cpu              clk_wlan                       clk_peri 160 MHz
          │                    │                              │
  CPU, AHB1 (bus1),     WLAN subsystem             ÷2 / ÷4 → 80 / 40 MHz for
  SRAM, DMA, CORET      (needs 160 MHz)            RSA, GPSEC, PWM, I2S, flash
                               │
                              ÷ 4
                               │
                       AHB2 (bus2) + APB
                       UART, TIMER, WDG, SPI, GPIO, I2C, ...
```

- The DPLL output is fixed at 480 MHz *(manual)*. The SDK assumes a 40 MHz
  crystal (`W80X_XTAL_HZ`) and a 480 MHz PLL (`W80X_PLL_HZ`). The manual also
  mentions 24 MHz crystals; the SDK has not been used with one.
- `clk_cpu` drives the CPU, the system bus (bus1/AHB1), SRAM and DMA. It is
  the clock in `SystemCoreClock` *(manual)*.
- The data bus (bus2/AHB2) and APB always run at **`clk_wlan` / 4**
  *(manual)*. With the WLAN root at the required 160 MHz, that is **40 MHz**.
- `clk_peri` must stay at 160 MHz (divider 3). Blocks take 160, 80 or 40 MHz
  from it *(manual)*.

## Buses

| Bus | Also called | Clock | Holds |
|---|---|---|---|
| bus1 | system bus, AHB1 | `clk_cpu` | CPU, ROM, 160 KB SRAM, DMA |
| bus2 | data bus, AHB2 | `clk_wlan` / 4 | 128 KB SRAM, AHB2 peripherals, APB bridge |
| APB | — | same as bus2 | UART, timers, watchdog, SPI, GPIO, I2C, … |

- The 288 KB of SRAM is split: 160 KB on bus1 and 128 KB on bus2. Bus1
  masters can reach all of it. Masters on bus2 only reach the 128 KB on bus2
  *(manual 4.4.1)*.
- **The `clk_cpu` : bus2 ratio must be a whole number (M:1, M ≥ 1)**, and
  SYS_CLK_DIV `bus2_syncdn_factor` must be set to that M. With a wrong
  factor, *"accessing the data bus will result in abnormal data"* *(manual
  5.3.4)*.

## Which clock each peripheral uses

| Peripheral | Clock | Notes |
|---|---|---|
| CPU core timer (CORET) | `clk_cpu` | `delay_us()`/`delay_ms()` follow `SystemCoreClock` *(SDK, hardware)* |
| UART0..5 | APB | Baud divider from `clock_apb_hz()`: call `uart_set_baud()` after a clock change *(SDK, hardware)* |
| TIMER0..5 | APB | 1 µs tick from APB: call `timer_init()` after a clock change *(SDK, hardware)* |
| Watchdog | APB | Call `wdg_start_ms()` again after a clock change *(SDK, hardware)* |
| SPI master (LSPI) | APB | SCK = APB / (2 × (div + 1)), at most 20 MHz at APB 40 MHz *(manual, SDK, hardware)* |
| RSA | `clk_peri` ÷ 2 or ÷ 1 | 80 MHz (reset) or 160 MHz, [SYS_CLK_SEL](#sys_clk_sel) bit 4 *(manual)* |
| GPSEC (crypto, CRC, TRNG) | `clk_peri` ÷ 2 or ÷ 1 | 80 MHz (reset) or 160 MHz, [SYS_CLK_SEL](#sys_clk_sel) bit 5 *(manual)* |
| Flash (QSPI) | `clk_peri` ÷ 4 or ÷ 2 | 40 MHz (reset) or 80 MHz, [SYS_CLK_SEL](#sys_clk_sel) bit 6 *(manual)* |
| ADC | 40 MHz ÷ `sd_adc_div` | [SYS_CLK_SEL](#sys_clk_sel) bits 15:8 *(manual)* |
| I2S | see [I2S_CLK_CTRL](#i2s_clk_ctrl) | CPU clock or an external clock *(manual)* |
| PWM | `clk_peri` | per the clock diagram *(manual)* |

## CPU clock settings

`clock_set_cpu_div(div)` sets `cpu_clk_divider` and picks the other two
dividers itself. It uses the smallest `wlan_clk_divider` ≥ 3 that keeps the
CPU : bus2 ratio a whole number. So the WLAN root stays at 160 MHz (APB
40 MHz) whenever the CPU clock allows it *(SDK)*.

| `clk_cpu` | `cpu_clk_divider` | `wlan_clk_divider` → WLAN root | `bus2_syncdn_factor` | APB / bus2 | Wi-Fi/BT usable |
|---|---|---|---|---|---|
| 240 MHz | 2 | 3 → 160 MHz | 6 | 40 MHz | yes |
| 160 MHz | 3 | 3 → 160 MHz | 4 | 40 MHz | yes |
| 120 MHz | 4 | 3 → 160 MHz | 3 | 40 MHz | yes |
| 96 MHz | 5 | 5 → 96 MHz | 4 | **24 MHz** | no |
| **80 MHz** (reset) | **6** | **3 → 160 MHz** | **2** | **40 MHz** | yes |
| 60 MHz | 8 | 4 → 120 MHz | 2 | **30 MHz** | no |
| 40 MHz | 12 | 3 → 160 MHz | 1 | 40 MHz | yes |
| 24 MHz | 20 | 5 → 96 MHz | 1 | **24 MHz** | no |
| 12 MHz | 40 | 10 → 48 MHz | 1 | **12 MHz** | no |
| 2 MHz | 240 | 60 → 8 MHz | 1 | **2 MHz** | no |

The `clk_cpu` column lists the `CLK_CPU_*` values in `w80x_clock.h`; any
divider from 2 to 255 works. The `clock_switch` example steps through
240, 160, 80, 40 and 12 MHz on hardware *(hardware)*.

## Changing the clock

1. Write all four fields of [SYS_CLK_DIV](#sys_clk_div) in one write, with
   `divide_freq_en` (bit 31) set. *"All factors must be valid"* when bit 31
   is set, so don't write a field alone *(manual)*.
2. Wait until the hardware clears bit 31: the new dividers are then in effect
   *(manual)*. The SDK does steps 1 and 2 with interrupts off *(SDK)*.
3. Update everything that depends on the clocks: `SystemCoreClock` (done by
   `clock_set_cpu_div()`), UART baud rates, timers, the watchdog and the SPI
   clock (see the [peripheral table](#which-clock-each-peripheral-uses)).
   Before the change, call `uart_flush()` so no character is sent half at
   the old rate *(SDK)*.

The application does not start from the reset values: secboot runs first and
may leave other dividers. `SystemInit()` therefore reads
`cpu_clk_divider` back from the register instead of assuming 80 MHz, and
`clock_apb_hz()` uses the `bus2_syncdn_factor` in the register *(SDK)*.

---

## Register details

### SW_CLKG_EN

Software clock gating enable. **0 stops the module's clock** (its logic and
clock tree stop, saving dynamic power); 1 lets it run.

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0E00` (base + `0x00`) | 32 bits | RW | `0x003F_FFFF` ¹ | `CLK->GATE_EN`, `clock_enable()` / `clock_disable()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:22 | — | RO | 0 | Reserved | |
| 21 | soft_touch_gate_en | RW | 1 | 0: touch sensor clock off · 1: on | `CLK_GATE_TOUCH` |
| 20 | soft_bt_gate_en | RW | 1 | 0: BT/BLE clock off · 1: on | `CLK_GATE_BT` |
| 19 | soft_qsram_gate_en | RW | 1 | 0: QSPI PSRAM controller clock off · 1: on | `CLK_GATE_QSRAM` |
| 18 | soft_sdio_m_gate_en | RW | 1 | 0: SDIO master clock off · 1: on | `CLK_GATE_SDIO_M` |
| 17 | soft_gpsec_gate_en | RW | 1 | 0: GPSEC (crypto) clock off · 1: on | `CLK_GATE_GPSEC` |
| 16 | soft_rsa_gate_en | RW | 1 | 0: RSA clock off · 1: on | `CLK_GATE_RSA` |
| 15 | soft_i2s_gate_en | RW | 1 | 0: I2S clock off · 1: on | `CLK_GATE_I2S` |
| 14 | soft_lcd_gate_en | RW | 1 | 0: LCD clock off · 1: on | `CLK_GATE_LCD` |
| 13 | soft_pwm_gate_en | RW | 1 | 0: PWM clock off · 1: on | `CLK_GATE_PWM` |
| 12 | soft_sd_adc_gate_en | RW | 1 | 0: ADC clock off · 1: on | `CLK_GATE_SDADC` |
| 11 | soft_gpio_gate_en | RW | 1 | 0: GPIO clock off · 1: on | `CLK_GATE_GPIO` |
| 10 | soft_timer_gate_en | RW | 1 | 0: timer clock off · 1: on | `CLK_GATE_TIMER` |
| 9 | soft_rf_cfg_gate_en | RW | 1 | RF configuration clock. **Internal use, do not modify** | `CLK_GATE_RF_CFG` |
| 8 | soft_dma_gate_en | RW | 1 | 0: DMA clock off · 1: on | `CLK_GATE_DMA` |
| 7 | soft_ls_spi_gate_en | RW | 1 | 0: low-speed SPI (LSPI) clock off · 1: on | `CLK_GATE_LSPI` |
| 6 | soft_uart5_gate_en | RW | 1 | 0: UART5 clock off · 1: on | `CLK_GATE_UART5` |
| 5 | soft_uart4_gate_en | RW | 1 | 0: UART4 clock off · 1: on | `CLK_GATE_UART4` |
| 4 | soft_uart3_gate_en | RW | 1 | 0: UART3 clock off · 1: on | `CLK_GATE_UART3` |
| 3 | soft_uart2_gate_en | RW | 1 | 0: UART2 clock off · 1: on | `CLK_GATE_UART2` |
| 2 | soft_uart1_gate_en | RW | 1 | 0: UART1 clock off · 1: on | `CLK_GATE_UART1` |
| 1 | soft_uart0_gate_en | RW | 1 | 0: UART0 clock off · 1: on | `CLK_GATE_UART0` |
| 0 | soft_i2c_gate_en | RW | 1 | 0: I2C clock off · 1: on | `CLK_GATE_I2C` |

Notes:
- Every module clock is on after reset, so the SDK drivers mostly just call
  `clock_enable()` to be safe *(manual, SDK)*.
- There is no gate bit for the high-speed SPI (HSPI, the SPI slave). It only
  has a reset bit in [SW_RST_CTRL](#sw_rst_ctrl) *(manual)*.

### SW_CLK_MASK

Software clock mask: whether the hardware may switch some clock domains off
by itself (adaptive shutdown). The manual warns: *"please do not change the
configuration, otherwise it may cause system abnormality when configuring
the PMU function"*. The SDK does not touch this register.

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0E04` (base + `0x04`) | 32 bits | RW | `0x0000_007E` | `CLK->GATE_MASK` |

| Bits | Name | Access | Reset | Values |
|---|---|---|---|---|
| 31:7 | — | — | 0 | Reserved |
| 6 | soft_cpu_clk_gt_mask | RW | 1 | CPU clock domain (CPU, bus1, ROM, SRAM). 0: adaptive off/on allowed · 1: not allowed. *Don't allow it when the CPU is to enter WFI (`wait`)* |
| 5:2 | — | RW | 1111 ² | Internal use, do not modify |
| 1 | soft_sdioahb_clk_gt_mask | RW | 1 | SDIO AHB clock domain. 0: adaptive off/on allowed · 1: not allowed |
| 0 | soft_pmu_clk_gt_mask | RW | 0 | Gate after the PLL output. 0: the PMU may switch it off · 1: the PMU may not |

² Not given in the bit table; `1111` is what makes the listed reset value
`0x7E`.

### SW_RST_CTRL

Software reset control. **Write 0 to hold a module in reset, 1 to release
it.** The reset does not release itself, so write 1 afterwards. It does not
reset the CPU or the watchdog *(manual)*. `periph_reset(mask)` pulses the
reset: it writes 0, waits a few cycles and writes 1 *(SDK)*.

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0E0C` (base + `0x0C`) | 32 bits | RW | `0xFFFF_FFFF` ¹ | `CLK->RST_CTRL`, `periph_reset()`, `periph_reset_assert()` / `_release()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31 | soft_touch_rst_n | RW | 1 | 0: touch sensor in reset · 1: released | `CLK_RST_TOUCH` |
| 30 | soft_rst_flash_n | RW | 1 | 0: flash controller in reset · 1: released. **Code runs from flash: don't** | `CLK_RST_FLASH` |
| 29 | soft_rst_bt_n | RW | 1 | 0: BT in reset · 1: released | `CLK_RST_BT` |
| 28 | soft_rst_qspi_ram_n | RW | 1 | 0: QSPI PSRAM controller in reset · 1: released | `CLK_RST_QSPI_RAM` |
| 27 | soft_rst_sdio_m_n | RW | 1 | 0: SDIO master in reset · 1: released | `CLK_RST_SDIO_M` |
| 26 | soft_rst_gpsec_n | RW | 1 | 0: GPSEC in reset · 1: released | `CLK_RST_GPSEC` |
| 25 | soft_rst_rsa_n | RW | 1 | 0: RSA in reset · 1: released | `CLK_RST_RSA` |
| 24 | soft_rst_i2s_n | RW | 1 | 0: I2S in reset · 1: released | `CLK_RST_I2S` |
| 23 | soft_rst_lcd_n | RW | 1 | 0: LCD in reset · 1: released | `CLK_RST_LCD` |
| 22 | soft_rst_pwm_n | RW | 1 | 0: PWM in reset · 1: released | `CLK_RST_PWM` |
| 21 | soft_rst_sar_adc_n | RW | 1 | 0: ADC in reset · 1: released | `CLK_RST_SARADC` |
| 20 | soft_rst_timer_n | RW | 1 | 0: timers in reset · 1: released | `CLK_RST_TIMER` |
| 19 | soft_rst_gpio_n | RW | 1 | 0: GPIO in reset · 1: released | `CLK_RST_GPIO` |
| 18 | soft_rst_rf_cfg_n | RW | 1 | RF configuration registers. **Internal use, do not modify** | `CLK_RST_RF_CFG` |
| 17 | soft_rst_spis_n | RW | 1 | 0: high-speed SPI (HSPI, slave) in reset · 1: released | `CLK_RST_HSPI` |
| 16 | soft_rst_spim_n | RW | 1 | 0: low-speed SPI (LSPI, master) in reset · 1: released | `CLK_RST_LSPI` |
| 15 | soft_rst_uart5_n | RW | 1 | 0: UART5 in reset · 1: released | `CLK_RST_UART5` |
| 14 | soft_rst_uart4_n | RW | 1 | 0: UART4 in reset · 1: released | `CLK_RST_UART4` |
| 13 | soft_rst_uart3_n | RW | 1 | 0: UART3 in reset · 1: released | `CLK_RST_UART3` |
| 12 | soft_rst_uart2_n | RW | 1 | 0: UART2 in reset · 1: released | `CLK_RST_UART2` |
| 11 | soft_rst_uart1_n | RW | 1 | 0: UART1 in reset · 1: released | `CLK_RST_UART1` |
| 10 | soft_rst_uart0_n | RW | 1 | 0: UART0 in reset · 1: released | `CLK_RST_UART0` |
| 9 | soft_rst_i2c_n | RW | 1 | 0: I2C in reset · 1: released | `CLK_RST_I2C` |
| 8 | soft_rst_bus2_n | RW | 1 | Data bus (bus2). **Not recommended**: breaks bus access | `CLK_RST_BUS2` |
| 7 | soft_rst_bus1_n | RW | 1 | System bus (bus1). **Not recommended**: breaks bus access | `CLK_RST_BUS1` |
| 6 | soft_rst_apb_n | RW | 1 | APB bridge. **Not recommended**: breaks bus access | `CLK_RST_APB` |
| 5 | soft_rst_mem_mng_n | RW | 1 | Memory manager. **Internal use, do not modify** | `CLK_RST_MEM_MNG` |
| 4 | soft_rst_dma_n | RW | 1 | 0: DMA in reset · 1: released | `CLK_RST_DMA` |
| 3 | soft_rst_sdio_ahb_n | RW | 1 | 0: SDIO AHB clock domain in reset · 1: released | `CLK_RST_SDIO_AHB` |
| 2 | soft_rst_sec_n | RW | 1 | Security module. **Internal use, do not modify** | `CLK_RST_SEC` |
| 1 | soft_rst_mac_n | RW | 1 | Wi-Fi MAC. **Internal use, do not modify** | `CLK_RST_MAC` |
| 0 | soft_rst_bbp_n | RW | 1 | Wi-Fi baseband. **Internal use, do not modify** | `CLK_RST_BBP` |

Notes:
- The gate bits and reset bits are in a different order: UART0 is gate bit 1
  but reset bit 10, LSPI is gate bit 7 but reset bit 16, and so on. Always
  use the `CLK_GATE_*` and `CLK_RST_*` names *(manual, SDK)*.
- `crypto_init()` resets GPSEC this way, which also stops a TRNG left running
  from before a soft reset *(SDK)*.

### SYS_CLK_DIV

Clock divider configuration. See [Changing the clock](#changing-the-clock)
for the update procedure.

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0E10` (base + `0x10`) | 32 bits | RW | `0x0302_0306` ¹ | `CLK->DIV`, `clock_set_cpu_div()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31 | divide_freq_en | RW | 0 | Write 1: load all four dividers (bits 27:0 here and `sd_adc_div` in [SYS_CLK_SEL](#sys_clk_sel)) into the clock dividers. The hardware clears it when they have taken effect. Read 0: done | `CLK_DIV_UPDATE` |
| 30:28 | — | — | 0 | Reserved | |
| 27:24 | Peripheral_divider | RW | 3 | `clk_peri` = 480 MHz / n. **Keep 3** (160 MHz) | `CLK_DIV_PERI_Msk` |
| 23:16 | bus2_syncdn_factor | RW | 2 | M in `clk_cpu` : bus2 = M : 1. Must equal the real ratio (reset: 80 MHz : 40 MHz = 2) | `CLK_DIV_BUS2_Msk` |
| 15:8 | wlan_clk_divider | RW | 3 | WLAN root = 480 MHz / n. bus2 and APB = WLAN root / 4. Keep 3 (160 MHz) for Wi-Fi/BT. Never above 160 MHz, so n ≥ 3 | `CLK_DIV_WLAN_Msk` |
| 7:0 | cpu_clk_divider | RW | 6 | `clk_cpu` = 480 MHz / n, n = 2..255 (reset: 6 = 80 MHz) | `CLK_DIV_CPU_Msk` |

### SYS_CLK_SEL

Clock select and debug control. The manual calls this register
`SYS_CLK_SEL` in the text, `DEBUG_CTRL` in the register list and "Clock
Select Register" in its table.

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0E14` (base + `0x14`) | 32 bits | RW | `0x0000_0A00` ¹ (`0x0001_2840` at app start) | `CLK->SEL`, `clock_jtag_enable()` / `_disable()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:17 | — | — | 0 | Reserved | |
| 16 | JTAG enable | RW | 0 | 0: JTAG debug off · 1: JTAG on (PA1, PA4) | `CLK_SEL_JTAG_EN` |
| 15:8 | sd_adc_div | RW | 10 | ADC clock = 40 MHz / n. Takes effect only through `divide_freq_en` in [SYS_CLK_DIV](#sys_clk_div) | `CLK_SEL_SDADC_DIV_Msk` |
| 7 | — | — | 0 | Reserved | |
| 6 | qflash_clk_sel | RW | 0 | Flash (QSPI) clock. 0: 40 MHz · 1: 80 MHz | `CLK_SEL_QFLASH_80M` |
| 5 | gpsec_sel | RW | 0 | GPSEC clock. 0: 80 MHz · 1: 160 MHz | `CLK_SEL_GPSEC_160M` |
| 4 | rsa_sel | RW | 0 | RSA clock. 0: 80 MHz · 1: 160 MHz | `CLK_SEL_RSA_160M` |
| 3:0 | — | RW | 0 | Reserved, do not modify | |

Notes:
- At application start on a W801 the register reads `0x0001_2840`, not the
  reset value `0x0000_0A00` *(hardware)*. Both bootloaders tested leave the
  same value, so the ROM (or the hardware itself) may set it rather than the
  bootloader; which one is not known:
  - **bit 16 = 1: JTAG is on.** PA1 and PA4 start as the JTAG port. Call
    `clock_jtag_disable()` before using them as GPIO;
  - **`sd_adc_div` = 40** (`0x28`): ADC clock 1 MHz, not 4 MHz;
  - **bit 6 = 1: flash at 80 MHz**, not 40 MHz;
  - bits 5:4 = 0: GPSEC and RSA at 80 MHz, as at reset.
- The SDK never writes bits 6:4, so the application keeps flash at 80 MHz
  and GPSEC/RSA at 80 MHz. The crypto and RSA speeds measured by the
  examples are at 80 MHz *(SDK, hardware)*.

### I2S_CLK_CTRL

I2S clock control. The SDK has no I2S driver yet, so this section comes
from the manual only.

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0E18` (base + `0x18`) | 32 bits | RW | `0x0000_0000` | `CLK->I2S_CTRL` (no field macros yet) |

| Bits | Name | Access | Reset | Values |
|---|---|---|---|---|
| 31:18 | — | — | 0 | Reserved |
| 17:8 | BCLKDIV | RW | 0 | F_BCLK = F_I2SCLK / BCLKDIV. BCLKDIV = round(F_I2SCLK / (Fs × W × F)): Fs sample rate, W word width in bits, F = 1 mono / 2 stereo. Example: 160 MHz, 24-bit stereo at 128 kHz → 160e6 / (128e3 × 24 × 2) ≈ 26 = `0x1A` |
| 7:2 | MCLKDIV | RW | 0 | MCLK divider, for an external clock. MCLKDIV = 0: F_MCLK = F_I2SCLK · MCLKDIV ≥ 1: F_MCLK = F_I2SCLK / (2 × MCLKDIV). F_MCLK should be 256 × Fs |
| 1 | MCLKEN | RW | 0 | 0: MCLK off · 1: MCLK on |
| 0 | EXTAL_EN | RW | 0 | I2S clock source. 0: internal: F_I2SCLK = F_CPU · 1: external clock, which must be 2 × N × 256 × Fs with N a whole number |

### RESET_STATUS

Records whether the last reset came from the watchdog or a CPU soft reset.
The flags survive those resets until cleared *(manual, SDK)*.

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0E1C` (base + `0x1C`) | 32 bits | RW (bits RO / WO) | `0x0000_0000` | `CLK->RST_STATUS`, `reset_status()` / `reset_status_clear()` (`w80x_wdg.h`) |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:18 | — | — | 0 | Reserved | |
| 17:8 | CPU soft reset state clear | WO | 0 | Write 1 to clear bit 1 (see note) | `CLK_RSTSTAT_CPU_CLR` = bit 8 |
| 7:2 | Wdog reset state clear | WO | 0 | Write 1 to clear bit 0 (see note) | `CLK_RSTSTAT_WDG_CLR` = bit 2 |
| 1 | CPU soft reset state | RO | 0 | 1: the last reset was a CPU soft reset · 0: it was not | `CLK_RSTSTAT_CPU` |
| 0 | Wdog reset state | RO | 0 | 1: the last reset came from the watchdog · 0: it did not | `CLK_RSTSTAT_WDG` |

Notes:
- The manual shows the clear controls as multi-bit fields (17:8 and 7:2)
  without saying which bit matters. wm_iot_sdk writes 1 to the field, which
  sets its lowest bit, so the SDK writes bit 8 and bit 2 *(SDK)*.
- On a W801, a watchdog reset sets bit 0 and changes no other CLK register.
  Writing bit 2 clears it, and it reads 0 after power-on *(hardware,
  [`confirm_reset_values`](../../examples/confirm_reset_values/main.c))*.
- The RESET button clears bit 0, like a power-on *(hardware)*.
- Still untested: what sets bit 1 (no CPU soft reset in the SDK yet).

What survives each reset on a W801 *(hardware,
[`confirm_reset_values`](../../examples/confirm_reset_values/main.c), one
word checked every 4 KB, so the edges are known only to ±4 KB)*:

| Reset | RESET_STATUS bit 0 | RAM |
|---|---|---|
| Power-on | 0 | lost |
| RESET button | 0 (clears it) | all lost, like a power-on |
| Watchdog | 1 | kept, except the ranges the bootloader overwrites (below) |

RAM lost across a watchdog reset, per bootloader (the rest of
`0x2000_1000..0x2004_6FFF` was kept):

| Bootloader | Lost |
|---|---|
| classic secboot (cjacker `197ad91`) | near `0x2000_1000`, and `0x2001_0000..0x2001_5FFF` |
| wm_iot_sdk bootloader (`e0e4a72`) | near `0x2000_1000`, and `0x2000_4000..0x2001_5FFF` |

Both keep everything from `0x2001_6000` up. The ranges were the same on
every watchdog reset tested. The SDK places `W80X_NOINIT` data at the top of
RAM (`0x2004_6C00`, below the stack), where it survived several watchdog
resets in a row with both bootloaders *(hardware)*.

---

## Manual discrepancies

Places where the WM_W800 Register Manual v2.1 contradicts itself. v3.0
(English and Chinese) still has every one of them.
[`examples/confirm_reset_values`](../../examples/confirm_reset_values/main.c)
reads the registers before any application init and compares them with both
manual values. A bootloader runs before every application, so these
readings are "what an application starts with": they confirm a manual value
when they match it, but a mismatch may come from the ROM or the bootloader.
Both bootloaders tested gave the same values.

| Register | Register list (table 7) | Bit table | W801 at app start *(hardware)* | This document |
|---|---|---|---|---|
| SW_CLKG_EN | `0x0000_7FFF` | bits 21:0 all reset to 1 = `0x003F_FFFF` | `0x003F_FFFF` | bit table, confirmed |
| SW_RST_CTRL | `0x01FF_FFFF` | bits 31:0 all reset to 1 = `0xFFFF_FFFF` | `0xFFFF_FFFF` | bit table, confirmed |
| SYS_CLK_DIV | `0x0000_2212` | 3 / 2 / 3 / 6 = `0x0302_0306`; the text also says CPU divider 6, WLAN divider 3 | `0x0302_0306` | bit table, confirmed |
| SYS_CLK_SEL | `0x0000_0000` | `sd_adc_div` resets to 10 = `0x0000_0A00` | `0x0001_2840` | bit table; neither matches (see [SYS_CLK_SEL](#sys_clk_sel)) |

The register list is wrong for the first three: the hardware matches the bit
tables. Its values look like an older or smaller chip (fewer gate and reset
bits, other dividers). SW_CLK_MASK, I2S_CLK_CTRL and RESET_STATUS match both
manual values. Only the W801 has been checked so far; W800 and W806
readings are still to do.

Other inconsistencies:
- **`wlan_clk_divider` minimum.** The bit table says n ≥ 2, but it also says
  the WLAN root must never be above 160 MHz, which needs n ≥ 3. The SDK uses
  n ≥ 3.
- **ADC name.** The text calls the SYS_CLK_SEL 15:8 divider the SAR ADC
  clock, and the bit table calls it the sigma-delta ADC (`sd_adc_div`). The
  gate bit is `soft_sd_adc_gate_en` and the reset bit is
  `soft_rst_sar_adc_n`. It seems to be one ADC with two names.
- **I2S clock source.** The clock diagram puts I2S on `clk_peri`, but the
  I2S_CLK_CTRL table says the internal I2S clock equals the CPU clock.
  Untested until there is an I2S driver.
