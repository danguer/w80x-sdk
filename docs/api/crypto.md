# Crypto

[API index](README.md)

- **Source:** [`w80x_crypto.h`](../../w80x/w80x_crypto.h), [`w80x_rsa.h`](../../w80x/w80x_rsa.h)
- **Example:** [`examples/crypto`](../../examples/crypto/main.c)

```c
crypto_init();

uint32_t c = crc32(buf, len);                 // zlib-compatible; also crc16_modbus/ccitt, crc8, crc_calc()

uint8_t digest[SHA1_SIZE];
sha1(data, len, digest);                      // or sha1_init/update/final; md5() likewise

cipher_ctx_t aes;
aes_setup(&aes, key, CIPHER_CBC, iv);         // CIPHER_ECB / CBC / CTR; des_setup, des3_setup
cipher_encrypt(&aes, plain, out, len);        // len = multiple of the block size
cipher_decrypt(&aes, out, plain, len);

rng_init();                                   // rng_init_analog(): raw, unwhitened bits
uint32_t r = rng_read();                      // true random; rng_fill(buf, n)

rsa_init();
rsa_modexp(out, m, &e, 1, n, 32);             // out = m^e mod n, 1024-bit, little-endian words
```

One engine (GPSEC) does CRC, SHA1, MD5, AES-128, DES, 3DES, RC4 and the
TRNG. It reads and writes the buffers itself and can only read SRAM (from
flash it reads zeros), so const input in flash is copied through RAM. Calls
wait for it to finish. CBC/CTR state lives in the context, so a message can be
encrypted in several calls. AES keys are 128-bit only (hardware limit).
AES-CTR runs in software over hardware ECB, because the hardware CTR mode
builds its counter block in a scrambled order. The TRNG register is a shift
register that takes in one new bit per analog random clock (measured: about
every 6 us), so `rng_read()` waits
until it has changed 32 times (~0.2 ms per word, about 19 KB/s).
The RSA block is a Montgomery multiplier. `rsa_modexp()` builds RSA on top
of it without a bignum library, for moduli of 128..2048 bits. At 240 MHz,
RSA-1024 takes about 19 ms with e = 65537 and 60 ms with the private key;
RSA-2048 about 72 ms and 390 ms. It is not
constant-time, so think before using it with private keys.
