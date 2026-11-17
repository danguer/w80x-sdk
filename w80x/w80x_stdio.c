/*
 * w80x_stdio.c - newlib system calls: printf/scanf over a UART, malloc heap.
 *
 * Always linked; with --gc-sections none of this ends up in the image unless
 * the program uses stdio or malloc. Everything is weak, so a program can
 * provide its own _write() (for example to print to a display) instead.
 *
 * stdout is line-buffered by newlib: text shows up at "\n", or call
 * fflush(stdout). stderr is unbuffered.
 *
 * time() and gettimeofday() read the RTC (w80x_rtc.h) as UTC, to the second.
 * They fail (time() returns -1) while the RTC is stopped: call rtc_init() or
 * rtc_set_time() first.
 *
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "w80x.h"
#include "w80x_uart.h"
#include "w80x_rtc.h"

#define W80X_WEAK __attribute__((weak))

static UART_TypeDef *stdio_uart;

void stdio_set_uart(UART_TypeDef *uart)
{
    if (stdio_uart) {
        uart_flush(stdio_uart);
    }
    stdio_uart = uart;
}

/* stdout / stderr: "\n" -> "\r\n" so a plain terminal starts a new line. */
W80X_WEAK int _write(int fd, const char *buf, int len)
{
    (void)fd;
    UART_TypeDef *uart = stdio_uart;
    if (uart) {
        for (int pos = 0; pos < len; pos++) {
            if (buf[pos] == '\n') {
                uart_putc(uart, '\r');
            }
            uart_putc(uart, buf[pos]);
        }
    }
    return len;
}

/* stdin: waits for the first byte, then takes whatever else is already in
 * the FIFO. The Enter key sends "\r"; it is passed on as "\n" so fgets() and
 * scanf() see a line end. No echo: print what you read if you want one. */
W80X_WEAK int _read(int fd, char *buf, int len)
{
    (void)fd;
    UART_TypeDef *uart = stdio_uart;
    if (!uart || (len <= 0)) {
        return 0;
    }

    int count = 0;
    do {
        char ch = (char)uart_getc(uart);
        if (ch == '\r') {
            ch = '\n';
        }
        buf[count++] = ch;
        if (ch == '\n') {
            break;
        }
    } while ((count < len) && uart_rx_ready(uart));
    return count;
}

/* Heap: from the end of .bss up to the stack reserve (w80x.ld). */
extern char __heap_start[], __heap_end[];

W80X_WEAK void *_sbrk(ptrdiff_t incr)
{
    static char *brk = __heap_start;
    char *prev = brk;

    if ((incr > (__heap_end - brk)) || (incr < (__heap_start - brk))) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk += incr;
    return prev;
}

W80X_WEAK int _close(int fd)                      { (void)fd; return -1; }
W80X_WEAK int _isatty(int fd)                     { (void)fd; return 1; }
W80X_WEAK int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
W80X_WEAK int _getpid(void)                       { return 1; }

W80X_WEAK int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;               /* a terminal: line-buffered stdout */
    return 0;
}

W80X_WEAK int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

/* time(), gettimeofday(): the RTC, whole seconds (tv_usec is 0). */
W80X_WEAK int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (!rtc_is_running()) {
        errno = EAGAIN;
        return -1;
    }
    if (tv) {
        tv->tv_sec  = rtc_get_unix();
        tv->tv_usec = 0;
    }
    return 0;
}

/* exit() / abort(): flush the console and stop. */
W80X_WEAK void _exit(int status)
{
    (void)status;
    if (stdio_uart) {
        uart_flush(stdio_uart);
    }
    __disable_irq();
    for (;;) {
    }
}

/* Newlib can be built to call either name: the Xuantie-800 newlib's
 * _write_r() calls write(), others call _write(). Without these aliases the
 * linker would take libnosys's stubs and printf would print nothing. */
#define W80X_SYSCALL_ALIAS(name, target) \
    extern __typeof(target) name __attribute__((weak, alias(#target)))

W80X_SYSCALL_ALIAS(write,  _write);
W80X_SYSCALL_ALIAS(read,   _read);
W80X_SYSCALL_ALIAS(sbrk,   _sbrk);
W80X_SYSCALL_ALIAS(close,  _close);
W80X_SYSCALL_ALIAS(isatty, _isatty);
W80X_SYSCALL_ALIAS(lseek,  _lseek);
W80X_SYSCALL_ALIAS(getpid, _getpid);
W80X_SYSCALL_ALIAS(fstat,  _fstat);
W80X_SYSCALL_ALIAS(kill,   _kill);
W80X_SYSCALL_ALIAS(gettimeofday, _gettimeofday);
