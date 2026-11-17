# Clocks

[API index](README.md)

- **Source:** [`w80x_clock.h`](../../w80x/w80x_clock.h), [`w80x_clock.c`](../../w80x/w80x_clock.c)
- **Registers:** [Clock and reset (CLK)](../mcu/clock.md)
- **Example:** [`examples/clock_switch`](../../examples/clock_switch/main.c)

```c
clock_set_cpu_div(CLK_CPU_240MHZ);  // PLL 480 MHz / 2; also 160, 120, 96, 80, 60, 40, 24, 12, 2 MHz
clock_set_cpu_mhz(160);             // any MHz value that divides 480
clock_cpu_hz(); clock_apb_hz(); clock_wlan_hz(); clock_peri_hz();
clock_enable(CLK_GATE_GPIO | CLK_GATE_UART0);
clock_disable(CLK_GATE_I2S);
periph_reset(CLK_RST_UART1);
clock_jtag_enable();                // PA1/PA4 debug port
```

The CPU clock must be an integer multiple of the AHB2/APB clock (manual
§5.3.4). `clock_set_cpu_div()` picks the WLAN divider and the bus ratio for
you. It keeps WLAN at 160 MHz (APB 40 MHz) whenever the CPU divider allows it,
and lowers WLAN otherwise (for example 96 MHz CPU → APB 24 MHz).
`SystemCoreClock` is updated automatically.
