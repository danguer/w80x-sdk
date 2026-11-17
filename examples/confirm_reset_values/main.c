/*
 * confirm_reset_values - read registers before any init and compare them
 * with the reset values in the register manual (docs/mcu/clock.md lists
 * where the manual contradicts itself).
 *
 * Board: HLK-W801-KIT-V1.1. Output on UART0, 115200 baud (`make monitor`).
 *
 * How: w80x_early_init() runs right after reset, before startup copies
 * .data, clears .bss or calls SystemInit(), and saves every register in
 * regs[] into a W80X_NOINIT array. main() prints them later. Values
 * different from both manual columns were most likely set by secboot,
 * which runs before any application; the ROM and secboot can't be skipped,
 * so this shows "what an application starts with", not the raw silicon
 * reset. Run it on each chip (W800, W801, W806) to compare.
 *
 * Try each kind of reset and compare RESET_STATUS:
 *   - power-on (unplug USB),  - RESET button,
 *   - 'w' key: watchdog reset,  'c' key: clear RESET_STATUS,
 *   - 'p' key: print the table again (values now, after init).
 *
 * It also shows which RAM keeps its content across a reset (secboot runs
 * in between and uses some RAM), to find where W80X_NOINIT data can live.
 *
 * To check more registers, add lines to regs[] (name, address, and the
 * two manual values: 0 if the manual gives only one, use it twice).
 */
#include <stdio.h>
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_uart.h"
#include "w80x_wdg.h"
#include "boards/hlk_w801_kit.h"

typedef struct {
    const char *name;
    uint32_t addr;
    uint32_t list;      /* reset value in the register list (manual table 7) */
    uint32_t bits;      /* reset value built from the per-bit tables */
} reg_t;

/* const: in flash, readable in w80x_early_init() before .data is set up */
static const reg_t regs[] = {
    { "SW_CLKG_EN",   CLK_BASE + 0x00u, 0x00007FFFu, 0x003FFFFFu },
    { "SW_CLK_MASK",  CLK_BASE + 0x04u, 0x0000007Eu, 0x0000007Eu },
    { "SW_RST_CTRL",  CLK_BASE + 0x0Cu, 0x01FFFFFFu, 0xFFFFFFFFu },
    { "SYS_CLK_DIV",  CLK_BASE + 0x10u, 0x00002212u, 0x03020306u },
    { "SYS_CLK_SEL",  CLK_BASE + 0x14u, 0x00000000u, 0x00000A00u },
    { "I2S_CLK_CTRL", CLK_BASE + 0x18u, 0x00000000u, 0x00000000u },
    { "RESET_STATUS", CLK_BASE + 0x1Cu, 0x00000000u, 0x00000000u },
};
#define NREGS (sizeof regs / sizeof regs[0])

#define EARLY_MAGIC 0x52535456u     /* "RSTV" */

/* Startup leaves these alone, so values written before .bss is cleared
 * reach main(). .noinit sits at the top of RAM, which a watchdog reset
 * keeps on a W801, so `boots` counts watchdog resets; power-on and the
 * RESET button lose all RAM and restart it at 0. */
static uint32_t early[NREGS] W80X_NOINIT;
static uint32_t early_magic W80X_NOINIT;
static uint32_t boots W80X_NOINIT;      /* resets without power loss */
static uint32_t boots_check W80X_NOINIT;

/*
 * RAM probe: which RAM keeps its content across a reset. main() writes a
 * pattern every PROBE_STEP bytes in the free RAM between the heap start and
 * the stack; w80x_early_init() checks it after the next reset (secboot has
 * run in between). The result is a bit per probe in probe_ok[].
 */
#define PROBE_STEP   0x1000u
#define PROBE_MAX    96u
#define PROBE_MAGIC  0xA5C3961Eu
extern char __heap_start[], __heap_end[];
extern const uint32_t __Vectors[];     /* start of the code, from startup.S */
static uint32_t probe_ok[(PROBE_MAX + 31u) / 32u] W80X_NOINIT;
static uint32_t probe_count W80X_NOINIT;

static uint32_t probe_first(void)
{
    return ((uint32_t)__heap_start + PROBE_STEP - 1u) & ~(PROBE_STEP - 1u);
}

/* Probe addresses: from the heap start to 256 bytes below the stack. */
static uint32_t probe_num(void)
{
    uint32_t first = probe_first(), end = (uint32_t)__heap_end - 0x100u;
    uint32_t count = (end > first) ? (((end - first) / PROBE_STEP) + 1u) : 0u;
    return (count > PROBE_MAX) ? PROBE_MAX : count;
}

static volatile uint32_t *probe_ptr(uint32_t probe)
{
    return (volatile uint32_t *)(probe_first() + (probe * PROBE_STEP));
}

static void probe_write(void)
{
    for (uint32_t probe = 0; probe < probe_num(); probe++) {
        *probe_ptr(probe) = PROBE_MAGIC ^ (uint32_t)probe_ptr(probe);
    }
}

/* Runs before .data/.bss: only NOINIT variables and const data here. */
void w80x_early_init(void)
{
    for (uint32_t reg = 0; reg < NREGS; reg++) {
        early[reg] = *(volatile const uint32_t *)regs[reg].addr;
    }
    early_magic = EARLY_MAGIC;

    probe_count = probe_num();
    for (uint32_t word = 0; word < ((PROBE_MAX + 31u) / 32u); word++) {
        probe_ok[word] = 0;
    }
    for (uint32_t probe = 0; probe < probe_count; probe++) {
        if (*probe_ptr(probe) == (PROBE_MAGIC ^ (uint32_t)probe_ptr(probe))) {
            probe_ok[probe / 32u] |= 1u << (probe % 32u);
        }
    }

    if (boots_check == ~boots) {
        boots++;                        /* RAM survived the last reset */
    } else {
        boots = 0;                      /* power-on (or RAM was overwritten) */
    }
    boots_check = ~boots;
}

static uint32_t reg_now(uint32_t reg)
{
    return *(volatile const uint32_t *)regs[reg].addr;
}

/* How a value compares with the manual. */
static const char *verdict(uint32_t reg, uint32_t value)
{
    if ((value == regs[reg].list) && (value == regs[reg].bits)) {
        return "= manual";
    }
    if (value == regs[reg].list) {
        return "= register list";
    }
    if (value == regs[reg].bits) {
        return "= bit tables";
    }
    return "differs";
}

static void print_table(const uint32_t *values, const char *when)
{
    printf("%-13s %-10s  %-10s  %-10s  %-10s  %s\n", "register", "address", when,
           "list", "bit table", "result");
    for (uint32_t reg = 0; reg < NREGS; reg++) {
        printf("%-13s 0x%08lx  0x%08lx  0x%08lx  0x%08lx  %s", regs[reg].name,
               (unsigned long)regs[reg].addr, (unsigned long)values[reg],
               (unsigned long)regs[reg].list, (unsigned long)regs[reg].bits,
               verdict(reg, values[reg]));
        if (values[reg] != regs[reg].bits) {
            printf(" (bits differing from the bit table: 0x%08lx)",
                   (unsigned long)(values[reg] ^ regs[reg].bits));
        }
        printf("\n");
    }
}

/* Field-by-field view of the clock registers. */
static void decode(const uint32_t *values)
{
    uint32_t div = values[3], sel = values[4], status = values[6];
    uint32_t cpu = div & 0xFFu, wlan = (div >> 8) & 0xFFu;
    uint32_t bus2 = (div >> 16) & 0xFFu, peri = (div >> 24) & 0xFu;

    printf("\nSYS_CLK_DIV: cpu_clk_divider %lu (%lu MHz), wlan_clk_divider %lu (%lu MHz),\n",
           (unsigned long)cpu, (unsigned long)(cpu ? (480u / cpu) : 0u),
           (unsigned long)wlan, (unsigned long)(wlan ? (480u / wlan) : 0u));
    printf("             bus2_syncdn_factor %lu (APB %lu MHz), Peripheral_divider %lu (%lu MHz),"
           " divide_freq_en %lu\n",
           (unsigned long)bus2, (unsigned long)((cpu && bus2) ? (480u / cpu / bus2) : 0u),
           (unsigned long)peri, (unsigned long)(peri ? (480u / peri) : 0u),
           (unsigned long)(div >> 31));
    printf("SYS_CLK_SEL: JTAG %s, sd_adc_div %lu, flash %s MHz, GPSEC %s MHz, RSA %s MHz,"
           " bits 3:0 = %lu\n",
           (sel & BIT(16)) ? "on" : "off", (unsigned long)((sel >> 8) & 0xFFu),
           (sel & BIT(6)) ? "80" : "40", (sel & BIT(5)) ? "160" : "80",
           (sel & BIT(4)) ? "160" : "80", (unsigned long)(sel & 0xFu));
    printf("RESET_STATUS: watchdog reset %lu, CPU soft reset %lu\n",
           (unsigned long)(status & 1u), (unsigned long)((status >> 1) & 1u));
}

static int probe_kept(uint32_t probe)
{
    return (probe_ok[probe / 32u] >> (probe % 32u)) & 1u;
}

/* Prints the probed RAM as ranges that kept or lost the pattern. */
static void print_probe(void)
{
    uint32_t kept = 0;

    for (uint32_t probe = 0; probe < probe_count; probe++) {
        kept += probe_kept(probe);
    }
    printf("\nRAM kept across the last reset: %lu of %lu probes (one every %lu bytes,"
           " 0x%08lx..0x%08lx)\n", (unsigned long)kept, (unsigned long)probe_count,
           (unsigned long)PROBE_STEP, (unsigned long)probe_ptr(0),
           (unsigned long)probe_ptr(probe_count - 1u));
    printf("(after power-on everything reads as lost: that is expected)\n");
    for (uint32_t probe = 0; probe < probe_count;) {
        uint32_t run_end = probe;
        while ((run_end < probe_count) && (probe_kept(run_end) == probe_kept(probe))) {
            run_end++;
        }
        printf("  0x%08lx..0x%08lx  %s\n", (unsigned long)probe_ptr(probe),
               (unsigned long)probe_ptr(run_end - 1u) + 3u, probe_kept(probe) ? "kept" : "lost");
        probe = run_end;
    }
}

static void print_now(void)
{
    uint32_t now[NREGS];

    for (uint32_t reg = 0; reg < NREGS; reg++) {
        now[reg] = reg_now(reg);
    }
    printf("\nValues now, after init:\n");
    print_table(now, "now");
    decode(now);
}

int main(void)
{
    uart0_init(115200);
    stdio_set_uart(UART0);

    printf("\n\nconfirm_reset_values on the %s\n", BOARD_NAME);
    /* An image only runs if a bootloader jumped to the address it was linked
     * for: 0x080D0400 = classic secboot, 0x0800F400 = wm_iot_sdk bootloader. */
    printf("Image linked at 0x%08lx (%s)\n", (unsigned long)__Vectors,
           ((uint32_t)__Vectors == 0x0800F400u) ? "BOOT=iot, wm_iot_sdk bootloader" :
           ((uint32_t)__Vectors == 0x080D0400u) ? "BOOT=secboot, classic secboot" : "custom layout");
    printf("Resets that kept .noinit RAM (0x%08lx) in a row: %lu"
           " (0 = power-on, RESET button, or the RAM was overwritten)\n\n",
           (unsigned long)&boots, (unsigned long)boots);

    if (early_magic != EARLY_MAGIC) {
        printf("w80x_early_init() did not run: no early values\n");
    } else {
        printf("Values at reset (before any init; the bootloader has already run):\n");
        print_table(early, "at reset");
        decode(early);
        early_magic = 0;
        print_probe();
    }
    probe_write();          /* checked again after the next reset */

    printf("\nKeys: w = watchdog reset, c = clear RESET_STATUS, p = print values now\n");
    for (;;) {
        int key = uart_getc(UART0);
        if (key == 'w') {
            printf("watchdog reset...\n");
            uart_flush(UART0);
            wdg_system_reset();
        } else if (key == 'c') {
            reset_status_clear();
            printf("RESET_STATUS cleared, now 0x%08lx\n", (unsigned long)CLK->RST_STATUS);
        } else if (key == 'p') {
            print_now();
        }
    }
}
