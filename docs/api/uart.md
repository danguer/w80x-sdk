# UART and printf

[API index](README.md)

- **Source:** [`w80x_uart.h`](../../w80x/w80x_uart.h), [`w80x_stdio.c`](../../w80x/w80x_stdio.c)
- **Example:** [`examples/uart_printf`](../../examples/uart_printf/main.c)

```c
uart0_init(115200);            // PB19 TX / PB20 RX, the USB-serial port
stdio_set_uart(UART0);         // printf, puts, scanf, getchar use UART0
printf("CPU %lu Hz\n", SystemCoreClock);   // "\n" is sent as "\r\n"

uart_puts(UART0, "raw\r\n");   // no stdio, no newline translation
int c = uart_try_getc(UART0);  // -1 if nothing received

uart_pins(PB6, PB7, GPIO_AF1); // other UARTs: route the pins first
uart_init(UART1, 9600);
uart_set_format(UART1, UART_8E1);
```

`printf` is newlib's full version (with `%f`); it adds about 37 KB to the
image, and the first call takes a 1 KB line buffer from the heap. Output
appears at each `\n`, or call `fflush(stdout)`. Programs that don't use stdio
pay nothing. The hooks in `w80x_stdio.c` are weak, so you can supply your own
`_write()`.

The baud divider comes from the APB clock: call `uart_flush()` before and
`uart_set_baud()` after `clock_set_cpu_div()`. Watch the console with
`make monitor` (115200 baud).
