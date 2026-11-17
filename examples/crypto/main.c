/*
 * crypto - hardware crypto self-test: CRC, SHA1, MD5, AES, DES, 3DES, RC4,
 * TRNG and RSA, checked against published / reference test vectors.
 *
 * Board: HLK-W801-KIT-V1.1. Output on UART0, 115200 baud (`make monitor`).
 * Every line says PASS or FAIL; a FAIL prints what the hardware returned.
 * LED1 lights if everything passed, LED2 if anything failed.
 *
 * Expected values come from the standards (FIPS-197, NIST SP 800-38A,
 * RFC 6229, CRC catalogue check values) and were cross-checked with
 * Python's hashlib / zlib / 'cryptography' package.
 */
#include <stdio.h>
#include <string.h>
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_utils.h"
#include "w80x_gpio.h"
#include "w80x_uart.h"
#include "w80x_timer.h"
#include "w80x_crypto.h"
#include "w80x_rsa.h"
#include "boards/hlk_w801_kit.h"
#include "rsa_vectors.h"

static int fails;

/* ---- helpers ------------------------------------------------------------- */

static size_t unhex(uint8_t *out, const char *hex)
{
    size_t count = 0;
    for (; hex[0] && hex[1]; hex += 2) {
        uint8_t byte = 0;
        for (int nibble = 0; nibble < 2; nibble++) {
            char ch = hex[nibble];
            byte = (uint8_t)((byte << 4) | ((ch <= '9') ? ch - '0' : (ch | 0x20) - 'a' + 10));
        }
        out[count++] = byte;
    }
    return count;
}

static void dump(const uint8_t *data, size_t len)
{
    for (size_t pos = 0; pos < len; pos++) {
        printf("%02x", data[pos]);
    }
}

static void check_bytes(const char *name, const uint8_t *got, const char *exp_hex)
{
    static uint8_t exp[64];
    size_t len = unhex(exp, exp_hex);
    int ok = memcmp(got, exp, len) == 0;
    printf("%-28s %s", name, ok ? "PASS" : "FAIL got ");
    if (!ok) {
        dump(got, len);
        fails++;
    }
    printf("\n");
}

static void check_u32(const char *name, uint32_t got, uint32_t exp)
{
    int ok = got == exp;
    printf("%-28s %s", name, ok ? "PASS" : "FAIL");
    if (!ok) {
        printf(" got 0x%08lx, want 0x%08lx", (unsigned long)got, (unsigned long)exp);
        fails++;
    }
    printf("\n");
}

static void check_ok(const char *name, int ok)
{
    printf("%-28s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) {
        fails++;
    }
}

/* CORET: 24-bit down counter at the CPU clock. */
static volatile uint32_t ms_ticks;
W80X_IRQ void TIMER_IRQHandler(void)
{
    timer_irq_clear(timer_irq_status());
    ms_ticks++;
}

/* Word-aligned scratch buffers: the engine reads and writes them directly. */
static uint8_t in[64]  __attribute__((aligned(4)));
static uint8_t out[64] __attribute__((aligned(4)));
static uint8_t key[32] __attribute__((aligned(4)));
static uint8_t iv[16]  __attribute__((aligned(4)));
static uint8_t big[16384] __attribute__((aligned(4)));

/* ---- tests --------------------------------------------------------------- */

static void test_crc(void)
{
    static const char check[] __attribute__((aligned(4))) = "123456789";

    check_u32("CRC-32", crc32(check, 9), 0xCBF43926u);
    check_u32("CRC-32 in two parts", crc32_update(crc32(check, 4), check + 4, 5), 0xCBF43926u);
    check_u32("CRC-16/MODBUS", crc16_modbus(check, 9), 0x4B37u);
    check_u32("CRC-16/CCITT-FALSE", crc16_ccitt(check, 9), 0x29B1u);
    check_u32("CRC-8", crc8(check, 9), 0xF4u);
}

static void test_hash(void)
{
    static const char abc[] __attribute__((aligned(4))) = "abc";
    static const char m56[] __attribute__((aligned(4))) =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";   /* 56 bytes: 2-block padding */
    hash_ctx_t ctx;

    sha1(abc, 3, out);
    check_bytes("SHA1(\"abc\")", out, "a9993e364706816aba3e25717850c26c9cd0d89d");
    sha1(m56, 56, out);
    check_bytes("SHA1(56 bytes)", out, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    sha1_init(&ctx);                                   /* same, fed 1 byte at a time */
    for (int pos = 0; pos < 56; pos++) {
        sha1_update(&ctx, m56 + pos, 1);
    }
    sha1_final(&ctx, out);
    check_bytes("SHA1(56 bytes) bytewise", out, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    md5(abc, 3, out);
    check_bytes("MD5(\"abc\")", out, "900150983cd24fb0d6963f7d28e17f72");
    md5(m56, 56, out);
    check_bytes("MD5(56 bytes)", out, "8215ef0796a20bcaaae116d3876c664a");
}

static void test_aes(void)
{
    cipher_ctx_t ctx;

    /* FIPS-197 C.1 */
    unhex(key, "000102030405060708090a0b0c0d0e0f");
    unhex(in, "00112233445566778899aabbccddeeff");
    aes_setup(&ctx, key, CIPHER_ECB, NULL);
    cipher_encrypt(&ctx, in, out, 16);
    check_bytes("AES-128-ECB encrypt", out, "69c4e0d86a7b0430d8cdb78070b4c55a");
    cipher_decrypt(&ctx, out, out, 16);
    check_bytes("AES-128-ECB decrypt", out, "00112233445566778899aabbccddeeff");

    /* SP 800-38A F.2.1, first two blocks */
    unhex(key, "2b7e151628aed2a6abf7158809cf4f3c");
    unhex(iv, "000102030405060708090a0b0c0d0e0f");
    unhex(in, "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
    static const char cbc[] = "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2";

    aes_setup(&ctx, key, CIPHER_CBC, iv);
    cipher_encrypt(&ctx, in, out, 32);
    check_bytes("AES-128-CBC encrypt", out, cbc);

    aes_setup(&ctx, key, CIPHER_CBC, iv);              /* IV carried between calls */
    cipher_encrypt(&ctx, in, out, 16);
    cipher_encrypt(&ctx, in + 16, out + 16, 16);
    check_bytes("AES-128-CBC in two calls", out, cbc);

    aes_setup(&ctx, key, CIPHER_CBC, iv);
    cipher_decrypt(&ctx, out, out, 32);
    check_bytes("AES-128-CBC decrypt", out,
                "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");

    /* SP 800-38A F.5.1, first two blocks */
    unhex(iv, "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    static const char ctr[] = "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff";

    aes_setup(&ctx, key, CIPHER_CTR, iv);
    cipher_encrypt(&ctx, in, out, 32);
    check_bytes("AES-128-CTR encrypt", out, ctr);

    aes_setup(&ctx, key, CIPHER_CTR, iv);              /* counter carried between calls */
    cipher_encrypt(&ctx, in, out, 16);
    cipher_encrypt(&ctx, in + 16, out + 16, 16);
    check_bytes("AES-128-CTR in two calls", out, ctr);
}

static void test_des(void)
{
    cipher_ctx_t ctx;

    unhex(key, "133457799bbcdff1");
    unhex(in, "0123456789abcdef");
    des_setup(&ctx, key, CIPHER_ECB, NULL);
    cipher_encrypt(&ctx, in, out, 8);
    check_bytes("DES-ECB encrypt", out, "85e813540f0ab405");

    unhex(key, "0123456789abcdef23456789abcdef01456789abcdef0123");
    unhex(iv, "0001020304050607");
    memcpy(in, "The quick brown fox jump", 24);
    des3_setup(&ctx, key, CIPHER_CBC, iv);
    cipher_encrypt(&ctx, in, out, 24);
    check_bytes("3DES-CBC encrypt", out, "29b01b011b9ebb6f10308a42938279068782e8bec97fe03f");
    des3_setup(&ctx, key, CIPHER_CBC, iv);
    cipher_decrypt(&ctx, out, out, 24);
    check_ok("3DES-CBC decrypt", memcmp(out, "The quick brown fox jump", 24) == 0);
}

static void test_rc4(void)
{
    /* RFC 6229, 128-bit key, key stream bytes 0..31 (= encrypting zeros) */
    unhex(key, "0102030405060708090a0b0c0d0e0f10");
    memset(in, 0, 32);
    rc4_crypt(key, 16, in, out, 32);
    check_bytes("RC4-128 key stream", out,
                "9ac7cc9a609d1ef7b2932899cde41b975248c4959014126a6e8a84f11d1a9e1c");
}

/* Statistics over 256 words from the current TRNG mode: bits set (ideal
 * 4096 of 8192), adjacent bits that differ (ideal ~50%, low = runs of equal
 * bits), repeated words and words that are the previous one shifted left by
 * 1..16 bits (overlapping reads), plus the time per word. Returns 1 if all
 * look sane. */
static int rng_stats(const char *name)
{
    uint32_t ones = 0, flips = 0, prev = rng_read(), repeats = 0, shifted = 0;
    uint32_t t0 = CPU_TICKS;
    for (int sample = 0; sample < 256; sample++) {
        uint32_t word = rng_read();
        ones += (uint32_t)__builtin_popcount(word);
        flips += (uint32_t)__builtin_popcount((word ^ (word >> 1)) & 0x7FFFFFFFu);
        repeats += (word == prev);
        for (uint32_t shift = 1; shift <= 16; shift++) {       /* >= 16 overlapping bits */
            if ((prev << shift) == (word & ~((1u << shift) - 1u))) {
                shifted++;
                break;
            }
        }
        prev = word;
    }
    uint32_t cyc = cpu_cycles_since(t0);
    printf("%s: %lu/8192 bits set, %lu/7936 bit flips, %lu repeats, %lu shifted, "
           "%lu cycles/word\n", name, (unsigned long)ones, (unsigned long)flips,
           (unsigned long)repeats, (unsigned long)shifted, (unsigned long)(cyc / 256));
    return (ones > 3800) && (ones < 4392) && (flips > 3680) && (flips < 4256) &&
           (repeats == 0) && (shifted == 0);
}

static void test_rng(void)
{
    rng_init_analog();
    printf("TRNG analog:");
    for (int word = 0; word < 4; word++) {
        printf(" %08lx", (unsigned long)rng_read());
    }
    printf("\n");
    printf("  (raw, no post-processing; informational, %s)\n",
           rng_stats("  analog ") ? "looks balanced" : "biased/correlated");

    rng_init();
    printf("TRNG digital:");
    for (int word = 0; word < 4; word++) {
        printf(" %08lx", (unsigned long)rng_read());
    }
    printf("\n");
    check_ok("TRNG digital statistics", rng_stats("  digital"));
}

/* Reports CPU exceptions instead of hanging silently (see system.c). */
void w80x_fault_hook(uint32_t vec, uint32_t epc, uint32_t epsr)
{
    printf("\n*** CPU exception %lu at PC %08lx (PSR %08lx)\n",
           (unsigned long)vec, (unsigned long)epc, (unsigned long)epsr);
}

static void test_rsa_key(const char *name, uint32_t words, const uint32_t *n,
                         const uint32_t *d, const uint32_t *m, const uint32_t *c)
{
    static uint32_t result[RSA_MAX_WORDS];
    static const uint32_t e = 65537;
    uint32_t t0 = ms_ticks;
    int err = rsa_modexp(result, m, &e, 1, n, words);
    uint32_t t_pub = ms_ticks - t0;
    char label[32];
    strcpy(label, name);
    strcat(label, " public (m^e)");
    check_ok(label, !err && (memcmp(result, c, words * 4) == 0));

    t0 = ms_ticks;
    err = rsa_modexp(result, c, d, words, n, words);
    uint32_t t_priv = ms_ticks - t0;
    strcpy(label, name);
    strcat(label, " private (c^d)");
    check_ok(label, !err && (memcmp(result, m, words * 4) == 0));

    printf("%s: public %lu ms, private %lu ms\n", name,
           (unsigned long)t_pub, (unsigned long)t_priv);
}

static void test_rsa(void)
{
    rsa_init();
    test_rsa_key("RSA-1024", RSA_TEST_WORDS, rsa_n, rsa_d, rsa_m, rsa_c);
    test_rsa_key("RSA-2048", RSA2048_WORDS, rsa2_n, rsa2_d, rsa2_m, rsa2_c);
}

static void speed(const char *name, uint32_t cycles, uint32_t bytes)
{
    uint32_t us = cpu_cycles_to_us(cycles);
    printf("  %-14s %5lu us  %5lu KB/s\n", name, (unsigned long)us,
           (unsigned long)(us ? (uint64_t)bytes * 1000000u / 1024u / us : 0));
}

static void test_speed(void)
{
    cipher_ctx_t ctx;
    uint32_t t0;

    for (uint32_t pos = 0; pos < sizeof big; pos++) {
        big[pos] = (uint8_t)pos;
    }
    printf("Throughput, %u bytes:\n", (unsigned)sizeof big);

    t0 = CPU_TICKS; crc32(big, sizeof big);           speed("CRC-32", cpu_cycles_since(t0), sizeof big);
    t0 = CPU_TICKS; sha1(big, sizeof big, out);       speed("SHA1", cpu_cycles_since(t0), sizeof big);
    t0 = CPU_TICKS; md5(big, sizeof big, out);        speed("MD5", cpu_cycles_since(t0), sizeof big);
    aes_setup(&ctx, key, CIPHER_CBC, iv);
    t0 = CPU_TICKS; cipher_encrypt(&ctx, big, big, sizeof big); speed("AES-128-CBC", cpu_cycles_since(t0), sizeof big);
    des3_setup(&ctx, key, CIPHER_CBC, iv);
    t0 = CPU_TICKS; cipher_encrypt(&ctx, big, big, sizeof big); speed("3DES-CBC", cpu_cycles_since(t0), sizeof big);
}

int main(void)
{
    clock_set_cpu_div(CLK_CPU_240MHZ);
    led_init(LED1);
    led_init(LED2);
    uart0_init(115200);
    stdio_set_uart(UART0);

    timer_init();
    timer_start(TIMER0, 1, TIMER_MS | TIMER_PERIODIC | TIMER_IRQ);
    irq_enable(TIMER_IRQn);
    __enable_irq();

    printf("\n\nCrypto self-test on the %s, CPU %lu MHz\n\n",
           BOARD_NAME, (unsigned long)(clock_cpu_hz() / 1000000u));

    crypto_init();
    test_crc();
    test_hash();
    test_aes();
    test_des();
    test_rc4();
    test_rng();
    test_rsa();
    test_speed();

    printf("\n%d failed\n", fails);
    led_on(fails ? LED2 : LED1);
    for (;;) {
        __wait();
    }
}
