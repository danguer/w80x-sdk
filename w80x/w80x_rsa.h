/*
 * w80x_rsa.h - RSA coprocessor: modular exponentiation (manual ch. 8).
 * Header-only, no bignum library needed.
 *
 * The hardware only does one Montgomery multiplication, a*b/R mod M (fully
 * reduced) with R = 2^(32*N), N = 4..64 words. rsa_modexp() builds
 * out = base^exp mod mod on top of it, which is all RSA needs:
 *
 *   uint32_t n[32], m[32], c[32];             // 1024-bit, little-endian words
 *   rsa_from_bytes(n, 32, key_n, 128);        // big-endian bytes -> words
 *   rsa_from_bytes(m, 32, msg, 128);
 *   uint32_t e = 65537;
 *   rsa_init();
 *   rsa_modexp(c, m, &e, 1, n, 32);           // c = m^e mod n
 *
 * Numbers are uint32_t arrays, word 0 least significant; `words` is the
 * modulus length. Requirements: the modulus is odd (true for RSA) and
 * base < modulus. Returns 0, or -1 on bad arguments or a hardware timeout.
 *
 * Sizes: 4..64 words (128..2048 bits). Measured at 240 MHz: RSA-1024
 * public (e = 65537) ~19 ms, private ~60 ms; RSA-2048 ~72 ms, ~390 ms.
 *
 * Square-and-multiply on the exponent bits: the run time depends on the
 * exponent, so this leaks timing for private keys. Fine for verifying
 * signatures/encrypting with public keys; think before using it with
 * private keys where an attacker can time you.
 *
 * Mode mapping (X, Y, D are the hardware buffers; matches wm_iot_sdk):
 *   RSA_MODE_D_XX  D = X*X    RSA_MODE_X_DD  X = D*D
 *   RSA_MODE_D_XY  D = X*Y    RSA_MODE_X_DY  X = D*Y
 * The running value ping-pongs between X and D; Y holds the base.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_RSA_H
#define W80X_RSA_H

#include <stddef.h>
#include "w80x.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RSA_MIN_WORDS   4u     /* 128 bits */
#define RSA_MAX_WORDS   64u    /* 2048 bits */

#ifndef RSA_TIMEOUT
#define RSA_TIMEOUT     0x1000000u
#endif

W80X_INLINE void rsa_init(void)
{
    CLK->GATE_EN  |= CLK_GATE_RSA;
    CLK->RST_CTRL &= ~CLK_RST_RSA;                     /* full block reset */
    __asm__ volatile("nop; nop; nop; nop");
    CLK->RST_CTRL |= CLK_RST_RSA;
}

/* Soft reset: stops a running multiplication and zeroes MC and N. It
 * finishes asynchronously (the RESET bit self-clears, and DONE may be set),
 * so wait for it before writing N/MC or they get zeroed afterwards.
 * Returns 0, or -1 if it never finished. */
static inline int rsa__reset(void)
{
    RSA->CTRL = RSA_CTRL_RESET;
    uint32_t t = RSA_TIMEOUT;
    while ((RSA->CTRL & RSA_CTRL_RESET) && --t) {
    }
    RSA->CTRL = 0;                                     /* clear DONE */
    return t ? 0 : -1;
}

/* big-endian byte string (as in PKCS#1, PEM, test vectors) <-> words */
static inline void rsa_from_bytes(uint32_t *w, uint32_t words, const uint8_t *be, size_t len)
{
    for (uint32_t i = 0; i < words; i++) {
        w[i] = 0;
    }
    for (size_t i = 0; i < len && i < 4u * words; i++)
        w[i / 4] |= (uint32_t)be[len - 1 - i] << (8u * (i % 4));
}

static inline void rsa_to_bytes(uint8_t *be, size_t len, const uint32_t *w, uint32_t words)
{
    for (size_t i = 0; i < len; i++)
        be[len - 1 - i] = i < 4u * words ? (uint8_t)(w[i / 4] >> (8u * (i % 4))) : 0;
}

/* a >= b ? */
static inline int rsa__ge(const uint32_t *a, const uint32_t *b, uint32_t words)
{
    for (uint32_t i = words; i-- > 0; ) {
        if (a[i] != b[i]) {
            return a[i] > b[i];
        }
    }
    return 1;
}

static inline void rsa__sub(uint32_t *a, const uint32_t *b, uint32_t words)  /* a -= b */
{
    uint32_t borrow = 0;
    for (uint32_t i = 0; i < words; i++) {
        uint32_t t = a[i] - b[i] - borrow;
        borrow = (a[i] < b[i]) || ((a[i] - b[i]) < borrow);
        a[i] = t;
    }
}

/* r = R^2 mod m = 2^(64*words) mod m, by doubling (a few ms for 2048
 * bits). */
static inline void rsa__r2(uint32_t *r, const uint32_t *m, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++) {
        r[i] = 0;
    }
    r[0] = 1;
    for (uint32_t k = 0; k < (64u * words); k++) {
        uint32_t carry = 0;
        for (uint32_t i = 0; i < words; i++) {
            uint32_t v = r[i];
            r[i] = (v << 1) | carry;
            carry = v >> 31;
        }
        if (carry || rsa__ge(r, m, words)) {
            rsa__sub(r, m, words);
        }
    }
}

/* -m^-1 mod 2^32 (m odd), Newton iteration. */
static inline uint32_t rsa__mc(uint32_t m0)
{
    uint32_t inv = m0;                 /* correct to 3 bits for odd m0 */
    for (int i = 0; i < 4; i++) {
        inv *= 2u - (m0 * inv);        /* doubles the correct bits */
    }
    return 0u - inv;
}

static inline void rsa__write(__IO uint32_t *buf, const uint32_t *v, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++) {
        buf[i] = v[i];
    }
}

static inline void rsa__write_one(__IO uint32_t *buf, uint32_t words)
{
    buf[0] = 1;
    for (uint32_t i = 1; i < words; i++) {
        buf[i] = 0;
    }
}

static inline void rsa__read(uint32_t *v, __IO uint32_t *buf, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++) {
        v[i] = buf[i];
    }
}

/* One Montgomery multiplication in the given mode. */
static inline int rsa__mul(uint32_t mode)
{
    RSA->CTRL = RSA_CTRL_MODE(mode);                   /* also clears DONE */
    RSA->CTRL = RSA_CTRL_MODE(mode) | RSA_CTRL_START;
    for (uint32_t t = RSA_TIMEOUT; !(RSA->CTRL & RSA_CTRL_DONE); ) {
        if (--t == 0) {
            return -1;
        }
    }
    RSA->CTRL = RSA_CTRL_MODE(mode);
    return 0;
}

/* out = base^exp mod mod. out may alias base. exp has exp_words words. */
static inline int rsa_modexp(uint32_t *out, const uint32_t *base,
                             const uint32_t *exp, uint32_t exp_words,
                             const uint32_t *mod, uint32_t words)
{
    uint32_t tmp[RSA_MAX_WORDS];
    int err = 0;

    if ((words < RSA_MIN_WORDS) || (words > RSA_MAX_WORDS) || !(mod[0] & 1u) ||
        rsa__ge(base, mod, words)) {
        return -1;
    }

    if (rsa__reset()) {
        return -1;
    }
    RSA->N  = words;
    if (RSA->N != words) {
        return -1;
    }
    RSA->MC = rsa__mc(mod[0]);
    rsa__write(RSA->M, mod, words);

    /* base' = base * R mod M  (Montgomery form) -> Y */
    rsa__r2(tmp, mod, words);
    rsa__write(RSA->Y, tmp, words);
    rsa__write(RSA->X, base, words);
    err |= rsa__mul(RSA_MODE_D_XY);                    /* D = base * R^2 / R */
    rsa__read(tmp, RSA->D, words);

    /* acc = 1 * R mod M  (Montgomery one) -> D */
    rsa__write_one(RSA->X, words);
    err |= rsa__mul(RSA_MODE_D_XY);                    /* D = 1 * R^2 / R */
    rsa__write(RSA->Y, tmp, words);

    /* left-to-right square and multiply; acc_in_d says where acc is */
    int acc_in_d = 1;
    for (uint32_t i = 32u * exp_words; i-- > 0; ) {
        err |= rsa__mul(acc_in_d ? RSA_MODE_X_DD : RSA_MODE_D_XX);
        acc_in_d = !acc_in_d;
        if (exp[i / 32] & BIT(i % 32)) {
            err |= rsa__mul(acc_in_d ? RSA_MODE_X_DY : RSA_MODE_D_XY);
            acc_in_d = !acc_in_d;
        }
        if (err) {
            break;
        }
    }

    /* leave Montgomery form: acc * 1 / R */
    rsa__write_one(RSA->Y, words);
    err |= rsa__mul(acc_in_d ? RSA_MODE_X_DY : RSA_MODE_D_XY);
    rsa__read(out, acc_in_d ? RSA->X : RSA->D, words);

    rsa__reset();
    return err ? -1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* W80X_RSA_H */
