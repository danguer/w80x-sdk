# System

[API index](README.md)

- **Source:** [`w80x.h`](../../w80x/w80x.h), [`system.c`](../../w80x/system.c), [`startup.S`](../../w80x/startup.S)
- **Example:** [`examples/confirm_reset_values`](../../examples/confirm_reset_values/main.c)

`delay_us()`, `delay_ms()` and `delay_ticks()` busy-wait on the core timer
(CORET), so they stay correct after a clock change. `irq_enable/disable/
set_priority()`, `irq_save()/irq_restore()`, `__enable_irq()`, `__wait()`.
Handlers are weak symbols: define e.g. `W80X_IRQ void TIMER_IRQHandler(void)`.
They are plain C functions (`W80X_IRQ` expands to nothing and only marks them).
A common entry in `startup.S` saves every register a C function may clobber,
FPU included, and calls the handler. GCC 13's own `__attribute__((isr))` is
not used: it leaves r18-r31 and the FPU registers unsaved, so a handler that
calls a function silently corrupts the code it interrupted.
Define `void w80x_early_init(void)` to run code right after reset, before
startup sets up `.data`/`.bss` and calls `SystemInit()`. Only `const` data and
variables marked `W80X_NOINIT` can be used there. `.noinit` variables (1 KB at the top of RAM, below the stack) are never
loaded or cleared by startup, and keep their value across a watchdog reset.
Power-on and the RESET button lose them, so guard them with a magic value
(see `examples/confirm_reset_values`).
Define `void w80x_fault_hook(uint32_t vec, uint32_t epc, uint32_t epsr)` to
report CPU exceptions (by default they just hang in `Default_Handler`).
