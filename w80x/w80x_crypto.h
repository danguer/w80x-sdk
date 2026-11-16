/*
 * w80x_crypto.h - GPSEC crypto engine: CRC, SHA1, MD5, AES-128, DES, 3DES,
 * RC4 and the true random number generator (manual ch. 7). Header-only.
 *
 *   crypto_init();
 *
 *   uint32_t c = crc32(buf, len);             // same result as zlib crc32()
 *
 *   uint8_t digest[SHA1_SIZE];
 *   sha1(data, len, digest);                  // or sha1_init/update/final
 *
 *   cipher_ctx_t aes;
 *   aes_setup(&aes, key, CIPHER_CBC, iv);
 *   cipher_encrypt(&aes, plain, out, 64);     // multiple of the block size
 *
 *   rng_init();                               // or rng_init_analog(): raw bits
 *   uint32_t r = rng_read();                  // hardware TRNG
 *
 * The engine copies data itself: it reads `in` and writes `out` in memory,
 * like a DMA channel. It can only read SRAM (from flash it reads zeros), so
 * input in flash (const data) is copied through RAM here, which is slower.
 * `out` must be in SRAM. Word-aligned buffers are safest (unaligned is
 * untested). `in` and `out` may be the same buffer.
 *
 * All calls wait for the engine. There is one engine for everything here, so
 * don't use it from an interrupt while the main code is using it. Contexts
 * (hash_ctx_t, cipher_ctx_t) keep their state in RAM and reload the engine
 * on every call, so several can be in use at once.
 *
 * Functions returning int give 0 on success, -1 on bad arguments or if the
 * engine never signalled completion (CRYPTO_TIMEOUT polls).
 *
 * Limits of the hardware (and of wm_iot_sdk, which this follows):
 *   - AES keys are 128-bit only. AES modes: ECB, CBC, CTR (CTR is done in
 *     software over hardware ECB; see cipher__ctr). DES/3DES: ECB, CBC.
 *   - RC4 keys are 128 or 256 bits; every rc4_crypt() call restarts the
 *     key stream.
 *   - The engine processes at most 65535 bytes per run; longer buffers are
 *     split here. CBC/CTR state is carried in software between runs and calls.
 *
 * GPSEC runs from CLK_PERI at 80 MHz by default; CLK->SEL |= CLK_SEL_GPSEC_160M
 * selects 160 MHz.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_CRYPTO_H
#define W80X_CRYPTO_H

#include <stddef.h>
#include <string.h>
#include "w80x.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRYPTO_TIMEOUT
#define CRYPTO_TIMEOUT  0x1000000u   /* status polls before giving up */
#endif

#define GPSEC_MAX_RUN   0xFFFFu      /* CFG.TOTAL is 16 bits          */

/* Clock on, and a full reset of the engine (this also stops a TRNG left
 * running by earlier code, e.g. before a soft reset). Call it once, before
 * rng_init(). */
W80X_INLINE void crypto_init(void)
{
    CLK->GATE_EN  |= CLK_GATE_GPSEC;
    CLK->RST_CTRL &= ~CLK_RST_GPSEC;
    __asm__ volatile("nop; nop; nop; nop");
    CLK->RST_CTRL |= CLK_RST_GPSEC;
    GPSEC->TRNG_CR = GPSEC_TRNG_INT_MASK;
    GPSEC->STS = GPSEC_STS_DONE;
}

/* One engine run: cfg = algorithm bits without the length.
 * Completion = START has cleared itself (manual: after the byte count is
 * done) and the done flag is set. Checking both also guards against the
 * TRNG, which raises the same done flag while its interrupt is unmasked. */
static inline int gpsec__run(const void *src, void *dst, uint32_t cfg, uint32_t len)
{
    GPSEC->SRC = (uint32_t)src;
    GPSEC->DST = (uint32_t)dst;
    GPSEC->STS = GPSEC_STS_DONE;
    GPSEC->CFG = cfg | GPSEC_CFG_INT_MASK | (len & GPSEC_CFG_TOTAL_Msk);
    GPSEC->CTRL = GPSEC_CTRL_START;

    for (uint32_t t = CRYPTO_TIMEOUT;
         (GPSEC->CTRL & GPSEC_CTRL_START) || !(GPSEC->STS & GPSEC_STS_DONE); ) {
        if (--t == 0) {
            GPSEC->CTRL = GPSEC_CTRL_STOP;
            return -1;
        }
    }
    GPSEC->STS = GPSEC_STS_DONE;
    return 0;
}

/* The engine only reads SRAM: from flash (const data) it reads zeros, as
 * seen on hardware. Sources outside SRAM are copied through RAM here. */
W80X_INLINE int gpsec__in_sram(const void *p, size_t len)
{
    uint32_t a = (uint32_t)p;
    return (a >= SRAM_BASE) && ((a + len) <= (SRAM_BASE + SRAM_SIZE));
}

/* Copy bytes into 32-bit registers / out of them, little-endian, any alignment. */
static inline void gpsec__put(__IO uint32_t *reg, const uint8_t *src, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++) {
        uint32_t w;
        memcpy(&w, src + (4u * i), 4);
        reg[i] = w;
    }
}

/* ======================================================================== */
/* CRC                                                                      */
/* ======================================================================== */

/* crc_calc() mode = one type | optional reflect flags */
#define CRC_8             GPSEC_CFG_SUB(GPSEC_SUB_CRC8)          /* poly 0x07   */
#define CRC_16_MODBUS     GPSEC_CFG_SUB(GPSEC_SUB_CRC16_MODBUS)  /* poly 0x8005 */
#define CRC_16_CCITT      GPSEC_CFG_SUB(GPSEC_SUB_CRC16_CCITT)   /* poly 0x1021 */
#define CRC_32            GPSEC_CFG_SUB(GPSEC_SUB_CRC32)         /* poly 0x04C11DB7 */
#define CRC_REFLECT_IN    GPSEC_CFG_CRC_REV_IN
#define CRC_REFLECT_OUT   GPSEC_CFG_CRC_REV_OUT

static inline uint32_t crc__width(uint32_t mode)
{
    switch (mode & GPSEC_CFG_SUB_Msk) {
    case CRC_8:  return 8;
    case CRC_32: return 32;
    default:     return 16;
    }
}

static inline uint32_t crc__reflect(uint32_t v, uint32_t bits)
{
    uint32_t r = 0;
    for (uint32_t i = 0; i < bits; i++, v >>= 1) {
        r = (r << 1) | (v & 1u);
    }
    return r;
}

/* Raw hardware CRC: starts from `crc` (an initial value, or the result of a
 * previous call to continue), no final XOR. With CRC_REFLECT_OUT the start
 * value is reflected before loading, as wm_iot_sdk does, so chained calls
 * give the same result as one call over all the data. */
static inline uint32_t crc_calc(uint32_t mode, uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t bits = crc__width(mode);
    uint32_t cfg = GPSEC_CFG_ALG(GPSEC_ALG_CRC) |
                   (mode & (GPSEC_CFG_SUB_Msk | CRC_REFLECT_IN | CRC_REFLECT_OUT));
    int direct = gpsec__in_sram(data, len);
    uint32_t bounce[64];                          /* for flash sources */

    while (len) {
        uint32_t max = direct ? 0xFFFCu : sizeof bounce;
        uint32_t n = (len > max) ? max : (uint32_t)len;
        const void *src = p;
        if (!direct) {
            src = memcpy(bounce, p, n);
        }
        GPSEC->DIGEST[4] = (mode & CRC_REFLECT_OUT) ? crc__reflect(crc, bits) : crc;
        gpsec__run(src, 0, cfg, n);
        crc = GPSEC->DIGEST[4];
        GPSEC->CTRL = GPSEC_CTRL_CRC_CLR;
        p += n;
        len -= n;
    }
    return (bits == 32) ? crc : crc & ((1u << bits) - 1u);
}

/* Standard CRCs ("123456789" check value in brackets). */

/* CRC-32 as in zlib/Ethernet/PNG [0xCBF43926]. Continue with
 * crc = crc32_update(crc, more, n), starting from 0. */
static inline uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
    return ~crc_calc(CRC_32 | CRC_REFLECT_IN | CRC_REFLECT_OUT, ~crc, data, len);
}
static inline uint32_t crc32(const void *data, size_t len) { return crc32_update(0, data, len); }

/* CRC-16/MODBUS [0x4B37] */
static inline uint16_t crc16_modbus(const void *data, size_t len)
{
    return (uint16_t)crc_calc(CRC_16_MODBUS | CRC_REFLECT_IN | CRC_REFLECT_OUT, 0xFFFFu, data, len);
}

/* CRC-16/CCITT-FALSE, a.k.a. CRC-16/IBM-3740 [0x29B1] */
static inline uint16_t crc16_ccitt(const void *data, size_t len)
{
    return (uint16_t)crc_calc(CRC_16_CCITT, 0xFFFFu, data, len);
}

/* CRC-8, poly 0x07, init 0 (CRC-8/SMBUS) [0xF4] */
static inline uint8_t crc8(const void *data, size_t len)
{
    return (uint8_t)crc_calc(CRC_8, 0, data, len);
}

/* ======================================================================== */
/* SHA1 / MD5                                                               */
/* ======================================================================== */

#define SHA1_SIZE  20u
#define MD5_SIZE   16u

typedef struct {
    uint32_t state[5];
    uint32_t buf[16];        /* one 64-byte block, word-aligned for the engine */
    uint64_t bytes;          /* total length so far */
    uint32_t alg;            /* GPSEC_ALG_SHA1 or GPSEC_ALG_MD5 */
} hash_ctx_t;

/* Run the engine over the 64-byte block in ctx->buf. */
static inline int hash__block(hash_ctx_t *c)
{
    uint32_t words = (c->alg == GPSEC_ALG_SHA1) ? 5u : 4u;
    for (uint32_t i = 0; i < words; i++) {
        GPSEC->DIGEST[i] = c->state[i];
    }
    int r = gpsec__run(c->buf, 0, GPSEC_CFG_ALG(c->alg), 64);
    for (uint32_t i = 0; i < words; i++) {
        c->state[i] = GPSEC->DIGEST[i];
    }
    return r;
}

static inline void hash__init(hash_ctx_t *c, uint32_t alg)
{
    c->state[0] = 0x67452301u;    /* same start values for SHA1 and MD5 */
    c->state[1] = 0xEFCDAB89u;
    c->state[2] = 0x98BADCFEu;
    c->state[3] = 0x10325476u;
    c->state[4] = 0xC3D2E1F0u;    /* SHA1 only */
    c->bytes = 0;
    c->alg = alg;
}

static inline int hash_update(hash_ctx_t *c, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint8_t *buf = (uint8_t *)c->buf;
    int r = 0;

    while (len) {
        uint32_t used = (uint32_t)(c->bytes & 63u);
        uint32_t n = 64u - used;
        if (n > len) {
            n = (uint32_t)len;
        }
        memcpy(buf + used, p, n);
        c->bytes += n;
        p += n;
        len -= n;
        if ((c->bytes & 63u) == 0) {
            r |= hash__block(c);
        }
    }
    return r;
}

/* Pad, write the digest (SHA1_SIZE or MD5_SIZE bytes) to out. */
static inline int hash_final(hash_ctx_t *c, uint8_t *out)
{
    uint8_t *buf = (uint8_t *)c->buf;
    uint64_t bits = c->bytes * 8u;
    uint32_t used = (uint32_t)(c->bytes & 63u);
    int sha = c->alg == GPSEC_ALG_SHA1;
    int r = 0;

    buf[used++] = 0x80;
    if (used > 56) {
        memset(buf + used, 0, 64u - used);
        r |= hash__block(c);
        used = 0;
    }
    memset(buf + used, 0, 56u - used);
    for (uint32_t i = 0; i < 8; i++) {      /* SHA1: big-endian length, MD5: little */
        buf[56 + i] = (uint8_t)(bits >> (sha ? 56u - (8u * i) : 8u * i));
    }
    r |= hash__block(c);

    for (uint32_t i = 0; i < (sha ? 5u : 4u); i++) {
        for (uint32_t b = 0; b < 4; b++) {
            out[(4 * i) + b] = (uint8_t)(c->state[i] >> (sha ? 24u - (8u * b) : 8u * b));
        }
    }
    return r;
}

static inline void sha1_init(hash_ctx_t *c) { hash__init(c, GPSEC_ALG_SHA1); }
static inline void md5_init(hash_ctx_t *c)  { hash__init(c, GPSEC_ALG_MD5); }
#define sha1_update hash_update
#define md5_update  hash_update
#define sha1_final  hash_final
#define md5_final   hash_final

/* One-shot digests. */
static inline int sha1(const void *data, size_t len, uint8_t out[SHA1_SIZE])
{
    hash_ctx_t c;
    sha1_init(&c);
    return hash_update(&c, data, len) | hash_final(&c, out);
}
static inline int md5(const void *data, size_t len, uint8_t out[MD5_SIZE])
{
    hash_ctx_t c;
    md5_init(&c);
    return hash_update(&c, data, len) | hash_final(&c, out);
}

/* ======================================================================== */
/* AES-128 / DES / 3DES                                                     */
/* ======================================================================== */

#define CIPHER_ECB   GPSEC_SUB_ECB
#define CIPHER_CBC   GPSEC_SUB_CBC
#define CIPHER_CTR   GPSEC_SUB_CTR     /* AES only */

#define AES_BLOCK    16u
#define DES_BLOCK    8u

typedef struct {
    uint8_t key[24];         /* AES 16, DES 8, 3DES 24 bytes */
    uint8_t iv[16];          /* CBC: next IV; CTR: next counter block */
    uint8_t alg;             /* GPSEC_ALG_AES / DES / 3DES */
    uint8_t mode;            /* CIPHER_ECB / CBC / CTR */
    uint8_t block;           /* 16 or 8 */
} cipher_ctx_t;

static inline int cipher__setup(cipher_ctx_t *c, uint32_t alg, const uint8_t *key, uint32_t keylen,
                                uint32_t mode, const uint8_t *iv, uint32_t block)
{
    if ((mode > ((alg == GPSEC_ALG_AES) ? CIPHER_CTR : CIPHER_CBC)) || ((mode != CIPHER_ECB) && !iv)) {
        return -1;
    }
    memset(c, 0, sizeof(*c));
    memcpy(c->key, key, keylen);
    if (iv) {
        memcpy(c->iv, iv, block);
    }
    c->alg = (uint8_t)alg;
    c->mode = (uint8_t)mode;
    c->block = (uint8_t)block;
    return 0;
}

/* iv: 16 bytes (CBC IV or CTR initial counter block), NULL for ECB.
 * CTR counts the whole block as a 128-bit big-endian number (NIST SP 800-38A). */
static inline int aes_setup(cipher_ctx_t *c, const uint8_t key[16], uint32_t mode, const uint8_t *iv)
{
    return cipher__setup(c, GPSEC_ALG_AES, key, 16, mode, iv, AES_BLOCK);
}
/* iv: 8 bytes, NULL for ECB. Mode ECB or CBC. */
static inline int des_setup(cipher_ctx_t *c, const uint8_t key[8], uint32_t mode, const uint8_t *iv)
{
    return cipher__setup(c, GPSEC_ALG_DES, key, 8, mode, iv, DES_BLOCK);
}
/* key = K1 K2 K3 (24 bytes, EDE). For 2-key 3DES pass K1 K2 K1. */
static inline int des3_setup(cipher_ctx_t *c, const uint8_t key[24], uint32_t mode, const uint8_t *iv)
{
    return cipher__setup(c, GPSEC_ALG_3DES, key, 24, mode, iv, DES_BLOCK);
}

/* Add n to a 128-bit big-endian counter block. */
static inline void cipher__ctr_add(uint8_t ctr[16], uint32_t n)
{
    for (int i = 15; (i >= 0) && n; i--) {
        uint32_t s = ctr[i] + (n & 0xFFu);
        ctr[i] = (uint8_t)s;
        n = (n >> 8) + (s >> 8);
    }
}

/* Load key and IV registers (wm_iot_sdk layout) and soft-reset the
 * algorithm so it starts from the loaded IV. */
static inline void cipher__load(const cipher_ctx_t *c, uint32_t cfg)
{
    if (c->alg == GPSEC_ALG_AES) {
        gpsec__put(GPSEC->KEY0, c->key, 4);              /* KEY0, KEY1 */
        gpsec__put(GPSEC->IV0, c->iv, 2);
        gpsec__put(GPSEC->KEY2, c->iv + 8, 2);
    } else {
        gpsec__put(GPSEC->KEY0, c->key, (c->alg == GPSEC_ALG_3DES) ? 6u : 2u);
        gpsec__put(GPSEC->IV0, c->iv, 2);
    }
    GPSEC->CFG = cfg | GPSEC_CFG_INT_MASK |
                 ((c->alg == GPSEC_ALG_AES) ? GPSEC_CFG_AES_RESET : GPSEC_CFG_DES_RESET);
}

/* CTR in software over hardware ECB. The hardware CTR mode builds its first
 * counter block from the IV registers in a scrambled order (seen on
 * hardware with the SP 800-38A vector), so it is not used. */
static inline int cipher__ctr(cipher_ctx_t *c, const uint8_t *src, uint8_t *dst, size_t len)
{
    uint32_t ks[64];                                     /* 256 bytes of key stream */
    uint8_t *k = (uint8_t *)ks;
    uint32_t cfg = GPSEC_CFG_ALG(GPSEC_ALG_AES) | GPSEC_CFG_SUB(GPSEC_SUB_ECB);

    while (len) {
        uint32_t n = (len > sizeof ks) ? sizeof ks : (uint32_t)len;
        for (uint32_t i = 0; i < n; i += AES_BLOCK) {
            memcpy(k + i, c->iv, AES_BLOCK);
            cipher__ctr_add(c->iv, 1);
        }
        cipher__load(c, cfg);
        if (gpsec__run(k, k, cfg, n)) {
            return -1;
        }
        for (uint32_t i = 0; i < n; i++) {
            dst[i] = src[i] ^ k[i];
        }
        src += n;
        dst += n;
        len -= n;
    }
    return 0;
}

static inline int cipher__run(cipher_ctx_t *c, const void *in, void *out, size_t len, uint32_t decrypt)
{
    const uint8_t *src = (const uint8_t *)in;
    uint8_t *dst = (uint8_t *)out;
    uint32_t bs = c->block;
    uint32_t cfg = GPSEC_CFG_ALG(c->alg) | GPSEC_CFG_SUB(c->mode) | (decrypt ? GPSEC_CFG_DECRYPT : 0);

    if (len % bs) {
        return -1;
    }
    if (c->mode == CIPHER_CTR) {
        return cipher__ctr(c, src, dst, len);
    }
    if (!gpsec__in_sram(out, len)) {
        return -1;
    }
    int direct = gpsec__in_sram(in, len);

    while (len) {
        uint32_t n = (len > (GPSEC_MAX_RUN & ~15u)) ? (GPSEC_MAX_RUN & ~15u) : (uint32_t)len;
        uint8_t next_iv[16];

        if ((c->mode == CIPHER_CBC) && decrypt) {        /* save before in-place overwrite */
            memcpy(next_iv, src + n - bs, bs);
        }
        if (!direct) {                                   /* flash source: work in place in out */
            memcpy(dst, src, n);
        }

        cipher__load(c, cfg);
        if (gpsec__run(direct ? src : dst, dst, cfg, n)) {
            return -1;
        }

        if (c->mode == CIPHER_CBC) {
            memcpy(c->iv, decrypt ? next_iv : dst + n - bs, bs);
        }

        src += n;
        dst += n;
        len -= n;
    }
    return 0;
}

/* len must be a multiple of the block size (16 AES, 8 DES/3DES). CBC and CTR
 * continue where the previous call on this context stopped. */
static inline int cipher_encrypt(cipher_ctx_t *c, const void *in, void *out, size_t len)
{
    return cipher__run(c, in, out, len, 0);
}
static inline int cipher_decrypt(cipher_ctx_t *c, const void *in, void *out, size_t len)
{
    /* CTR decrypts by encrypting */
    return cipher__run(c, in, out, len, c->mode != CIPHER_CTR);
}

/* ======================================================================== */
/* RC4 (legacy; broken as a cipher, kept for old protocols)                 */
/* ======================================================================== */

/* keylen 16 or 32, len up to 65532 (the wm_iot_sdk limit). The key stream
 * starts over on every call. Encrypt and decrypt are the same operation. */
static inline int rc4_crypt(const uint8_t *key, uint32_t keylen, const void *in, void *out, size_t len)
{
    if (((keylen != 16) && (keylen != 32)) || (len == 0) || (len > 65532u) || !gpsec__in_sram(out, len)) {
        return -1;
    }
    if (!gpsec__in_sram(in, len)) {                      /* flash source: work in place in out */
        in = memcpy(out, in, len);
    }
    gpsec__put(GPSEC->KEY0, key, (keylen == 32) ? 6u : 4u);
    if (keylen == 32) {
        gpsec__put(GPSEC->KEY3, key + 24, 2);
    }

    uint32_t cfg = GPSEC_CFG_ALG(GPSEC_ALG_RC4) | ((keylen == 32) ? GPSEC_CFG_RC4_256 : 0);
    GPSEC->CFG = cfg | GPSEC_CFG_INT_MASK | GPSEC_CFG_RC4_RESET;
    return gpsec__run(in, out, cfg, (uint32_t)len);
}

/* ======================================================================== */
/* True random number generator                                             */
/* ======================================================================== */

/* Start the TRNG. digital = 1: the raw analog bits go through the digital
 * post-processing (XOR chain + LFSR) that removes bias and correlation.
 * digital = 0 sets TRNG_DIG_BYPASS: the RNG register gets the raw analog
 * samples (manual 7.3), useful to look at the noise source itself. Settings
 * and the 1 ms settle time follow wm_iot_sdk. Needs crypto_init() first. */
static inline void rng__init(int digital)
{
    GPSEC->TRNG_CR = GPSEC_TRNG_INT_MASK | (4u << GPSEC_TRNG_CP_Pos) |
                     (digital ? 0 : GPSEC_TRNG_BYPASS) |
                     GPSEC_TRNG_SEL | GPSEC_TRNG_EN;
    delay_ms(1);
}

/* Normal use: post-processed output. */
static inline void rng_init(void) { rng__init(1); }

/* Raw analog output, no post-processing; not for keys unless you whiten
 * it yourself. */
static inline void rng_init_analog(void) { rng__init(0); }

/* Changes of the RNG register to wait per word; each change is one new bit.
 * wm_iot_sdk reads after a single done event (one new bit), so its
 * consecutive words overlap by 31 bits. */
#ifndef RNG_CHANGES_PER_WORD
#define RNG_CHANGES_PER_WORD 32u
#endif

/* Next 32-bit random word.
 * RNG is a shift register: on every random clock from the analog module
 * (ad_trng_clks) one new sample is shifted in (manual 7.3). Measured on an
 * HLK-W801 (CP = 4, room temperature): 244 of 256 changes were a 1-bit
 * shift, about 1450 CPU cycles apart at 240 MHz (~6 us). The rate follows
 * the analog clock, so it is not fixed. That is why this counts changes,
 * not time: after RNG_CHANGES_PER_WORD = 32 changes none of the previous
 * word is left (~0.2 ms per word). A shift that leaves the value unchanged
 * (all bits equal) is not seen, which only makes it wait longer.
 * The TRNG interrupt stays masked: the TRNG runs either way, and unmasked
 * it sets the GPSEC done flag once per bit, which the other algorithms
 * use for their own completion. */
static inline uint32_t rng_read(void)
{
    uint32_t last = GPSEC->RNG;
    for (uint32_t n = 0, t = CRYPTO_TIMEOUT; (n < RNG_CHANGES_PER_WORD) && --t; ) {
        uint32_t v = GPSEC->RNG;
        if (v != last) {
            last = v;
            n++;
        }
    }
    return last;
}

static inline void rng_fill(void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    while (len) {
        uint32_t w = rng_read();
        uint32_t n = (len < 4) ? (uint32_t)len : 4u;
        memcpy(p, &w, n);
        p += n;
        len -= n;
    }
}

static inline void rng_stop(void) { GPSEC->TRNG_CR = GPSEC_TRNG_INT_MASK; }

#ifdef __cplusplus
}
#endif

#endif /* W80X_CRYPTO_H */
