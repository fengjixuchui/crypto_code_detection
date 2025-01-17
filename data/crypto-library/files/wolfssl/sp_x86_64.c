/* sp.c
 *
 * Copyright (C) 2006-2019 wolfSSL Inc.
 *
 * This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

/* Implementation by Sean Parkinson. */

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssl/wolfcrypt/cpuid.h>
#ifdef NO_INLINE
    #include <wolfssl/wolfcrypt/misc.h>
#else
    #define WOLFSSL_MISC_INCLUDED
    #include <wolfcrypt/src/misc.c>
#endif

#if defined(WOLFSSL_HAVE_SP_RSA) || defined(WOLFSSL_HAVE_SP_DH) || \
                                    defined(WOLFSSL_HAVE_SP_ECC)

#ifdef RSA_LOW_MEM
#ifndef WOLFSSL_SP_SMALL
#define WOLFSSL_SP_SMALL
#endif
#endif

#include <wolfssl/wolfcrypt/sp.h>

#ifdef WOLFSSL_SP_X86_64_ASM
#if defined(WOLFSSL_HAVE_SP_RSA) || defined(WOLFSSL_HAVE_SP_DH)
#ifndef WOLFSSL_SP_NO_2048
/* Read big endian unsigned byte aray into r.
 *
 * r  A single precision integer.
 * a  Byte array.
 * n  Number of bytes in array to read.
 */
static void sp_2048_from_bin(sp_digit* r, int max, const byte* a, int n)
{
    int i, j = 0, s = 0;

    r[0] = 0;
    for (i = n-1; i >= 0; i--) {
        r[j] |= ((sp_digit)a[i]) << s;
        if (s >= 56) {
            r[j] &= 0xffffffffffffffffl;
            s = 64 - s;
            if (j + 1 >= max)
                break;
            r[++j] = a[i] >> s;
            s = 8 - s;
        }
        else
            s += 8;
    }

    for (j++; j < max; j++)
        r[j] = 0;
}

/* Convert an mp_int to an array of sp_digit.
 *
 * r  A single precision integer.
 * a  A multi-precision integer.
 */
static void sp_2048_from_mp(sp_digit* r, int max, const mp_int* a)
{
#if DIGIT_BIT == 64
    int j;

    XMEMCPY(r, a->dp, sizeof(sp_digit) * a->used);

    for (j = a->used; j < max; j++)
        r[j] = 0;
#elif DIGIT_BIT > 64
    int i, j = 0, s = 0;

    r[0] = 0;
    for (i = 0; i < a->used && j < max; i++) {
        r[j] |= a->dp[i] << s;
        r[j] &= 0xffffffffffffffffl;
        s = 64 - s;
        if (j + 1 >= max)
            break;
        r[++j] = (sp_digit)(a->dp[i] >> s);
        while (s + 64 <= DIGIT_BIT) {
            s += 64;
            r[j] &= 0xffffffffffffffffl;
            if (j + 1 >= max)
                break;
            if (s < DIGIT_BIT)
                r[++j] = (sp_digit)(a->dp[i] >> s);
            else
                r[++j] = 0;
        }
        s = DIGIT_BIT - s;
    }

    for (j++; j < max; j++)
        r[j] = 0;
#else
    int i, j = 0, s = 0;

    r[0] = 0;
    for (i = 0; i < a->used && j < max; i++) {
        r[j] |= ((sp_digit)a->dp[i]) << s;
        if (s + DIGIT_BIT >= 64) {
            r[j] &= 0xffffffffffffffffl;
            if (j + 1 >= max)
                break;
            s = 64 - s;
            if (s == DIGIT_BIT) {
                r[++j] = 0;
                s = 0;
            }
            else {
                r[++j] = a->dp[i] >> s;
                s = DIGIT_BIT - s;
            }
        }
        else
            s += DIGIT_BIT;
    }

    for (j++; j < max; j++)
        r[j] = 0;
#endif
}

/* Write r as big endian to byte aray.
 * Fixed length number of bytes written: 256
 *
 * r  A single precision integer.
 * a  Byte array.
 */
static void sp_2048_to_bin(sp_digit* r, byte* a)
{
    int i, j, s = 0, b;

    j = 2048 / 8 - 1;
    a[j] = 0;
    for (i=0; i<32 && j>=0; i++) {
        b = 0;
        a[j--] |= r[i] << s; b += 8 - s;
        if (j < 0)
            break;
        while (b < 64) {
            a[j--] = r[i] >> b; b += 8;
            if (j < 0)
                break;
        }
        s = 8 - (b - 64);
        if (j >= 0)
            a[j] = 0;
        if (s != 0)
            j++;
    }
}

extern void sp_2048_mul_16(sp_digit* r, const sp_digit* a, const sp_digit* b);
extern void sp_2048_sqr_16(sp_digit* r, const sp_digit* a);
extern void sp_2048_mul_avx2_16(sp_digit* r, const sp_digit* a, const sp_digit* b);
extern void sp_2048_sqr_avx2_16(sp_digit* r, const sp_digit* a);
extern sp_digit sp_2048_add_16(sp_digit* r, const sp_digit* a, const sp_digit* b);
extern sp_digit sp_2048_sub_in_place_32(sp_digit* a, const sp_digit* b);
extern sp_digit sp_2048_add_32(sp_digit* r, const sp_digit* a, const sp_digit* b);
/* AND m into each word of a and store in r.
 *
 * r  A single precision integer.
 * a  A single precision integer.
 * m  Mask to AND against each digit.
 */
static void sp_2048_mask_16(sp_digit* r, const sp_digit* a, sp_digit m)
{
#ifdef WOLFSSL_SP_SMALL
    int i;

    for (i=0; i<16; i++)
        r[i] = a[i] & m;
#else
    int i;

    for (i = 0; i < 16; i += 8) {
        r[i+0] = a[i+0] & m;
        r[i+1] = a[i+1] & m;
        r[i+2] = a[i+2] & m;
        r[i+3] = a[i+3] & m;
        r[i+4] = a[i+4] & m;
        r[i+5] = a[i+5] & m;
        r[i+6] = a[i+6] & m;
        r[i+7] = a[i+7] & m;
    }
#endif
}

/* Multiply a and b into r. (r = a * b)
 *
 * r  A single precision integer.
 * a  A single precision integer.
 * b  A single precision integer.
 */
SP_NOINLINE static void sp_2048_mul_32(sp_digit* r, const sp_digit* a,
        const sp_digit* b)
{
    sp_digit* z0 = r;
    sp_digit z1[32];
    sp_digit a1[16];
    sp_digit b1[16];
    sp_digit z2[32];
    sp_digit u, ca, cb;

    ca = sp_2048_add_16(a1, a, &a[16]);
    cb = sp_2048_add_16(b1, b, &b[16]);
    u  = ca & cb;
    sp_2048_mul_16(z1, a1, b1);
    sp_2048_mul_16(z2, &a[16], &b[16]);
    sp_2048_mul_16(z0, a, b);
    sp_2048_mask_16(r + 32, a1, 0 - cb);
    sp_2048_mask_16(b1, b1, 0 - ca);
    u += sp_2048_add_16(r + 32, r + 32, b1);
    u += sp_2048_sub_in_place_32(z1, z2);
    u += sp_2048_sub_in_place_32(z1, z0);
    u += sp_2048_add_32(r + 16, r + 16, z1);
    r[48] = u;
    XMEMSET(r + 48 + 1, 0, sizeof(sp_digit) * (16 - 1));
    sp_2048_add_32(r + 32, r + 32, z2);
}

/* Square a and put result in r. (r = a * a)
 *
 * r  A single precision integer.
 * a  A single precision integer.
 */
SP_NOINLINE static void sp_2048_sqr_32(sp_digit* r, const sp_digit* a)
{
    sp_digit* z0 = r;
    sp_digit z2[32];
    sp_digit z1[32];
    sp_digit a1[16];
    sp_digit u;

    u = sp_2048_add_16(a1, a, &a[16]);
    sp_2048_sqr_16(z1, a1);
    sp_2048_sqr_16(z2, &a[16]);
    sp_2048_sqr_16(z0, a);
    sp_2048_mask_16(r + 32, a1, 0 - u);
    u += sp_2048_add_16(r + 32, r + 32, r + 32);
    u += sp_2048_sub_in_place_32(z1, z2);
    u += sp_2048_sub_in_place_32(z1, z0);
    u += sp_2048_add_32(r + 16, r + 16, z1);
    r[48] = u;
    XMEMSET(r + 48 + 1, 0, sizeof(sp_digit) * (16 - 1));
    sp_2048_add_32(r + 32, r + 32, z2);
}

#ifdef HAVE_INTEL_AVX2
/* Multiply a and b into r. (r = a * b)
 *
 * r  A single precision integer.
 * a  A single precision integer.
 * b  A single precision integer.
 */
SP_NOINLINE static void sp_2048_mul_avx2_32(sp_digit* r, const sp_digit* a,
        const sp_digit* b)
{
    sp_digit* z0 = r;
    sp_digit z1[32];
    sp_digit a1[16];
    sp_digit b1[16];
    sp_digit z2[32];
    sp_digit u, ca, cb;

    ca = sp_2048_add_16(a1, a, &a[16]);
    cb = sp_2048_add_16(b1, b, &b[16]);
    u  = ca & cb;
    sp_2048_mul_avx2_16(z1, a1, b1);
    sp_2048_mul_avx2_16(z2, &a[16], &b[16]);
    sp_2048_mul_avx2_16(z0, a, b);
    sp_2048_mask_16(r + 32, a1, 0 - cb);
    sp_2048_mask_16(b1, b1, 0 - ca);
    u += sp_2048_add_16(r + 32, r + 32, b1);
    u += sp_2048_sub_in_place_32(z1, z2);
    u += sp_2048_sub_in_place_32(z1, z0);
    u += sp_2048_add_32(r + 16, r + 16, z1);
    r[48] = u;
    XMEMSET(r + 48 + 1, 0, sizeof(sp_digit) * (16 - 1));
    sp_2048_add_32(r + 32, r + 32, z2);
}
#endif /* HAVE_INTEL_AVX2 */

#ifdef HAVE_INTEL_AVX2
/* Square a and put result in r. (r = a * a)
 *
 * r  A single precision integer.
 * a  A single precision integer.
 */
SP_NOINLINE static void sp_2048_sqr_avx2_32(sp_digit* r, const sp_digit* a)
{
    sp_digit* z0 = r;
    sp_digit z2[32];
    sp_digit z1[32];
    sp_digit a1[16];
    sp_digit u;

    u = sp_2048_add_16(a1, a, &a[16]);
    sp_2048_sqr_avx2_16(z1, a1);
    sp_2048_sqr_avx2_16(z2, &a[16]);
    sp_2048_sqr_avx2_16(z0, a);
    sp_2048_mask_16(r + 32, a1, 0 - u);
    u += sp_2048_add_16(r + 32, r + 32, r + 32);
    u += sp_2048_sub_in_place_32(z1, z2);
    u += sp_2048_sub_in_place_32(z1, z0);
    u += sp_2048_add_32(r + 16, r + 16, z1);
    r[48] = u;
    XMEMSET(r + 48 + 1, 0, sizeof(sp_digit) * (16 - 1));
    sp_2048_add_32(r + 32, r + 32, z2);
}
#endif /* HAVE_INTEL_AVX2 */

#if (defined(WOLFSSL_HAVE_SP_RSA) || defined(WOLFSSL_HAVE_SP_DH)) && !defined(WOLFSSL_RSA_PUBLIC_ONLY)
#endif /* (WOLFSSL_HAVE_SP_RSA || WOLFSSL_HAVE_SP_DH) && !WOLFSSL_RSA_PUBLIC_ONLY */

/* Caclulate the bottom digit of -1/a mod 2^n.
 *
 * a    A single precision number.
 * rho  Bottom word of inverse.
 */
static void sp_2048_mont_setup(const sp_digit* a, sp_digit* rho)
{
    sp_digit x, b;

    b = a[0];
    x = (((b + 2) & 4) << 1) + b; /* here x*a==1 mod 2**4 */
    x *= 2 - b * x;               /* here x*a==1 mod 2**8 */
    x *= 2 - b * x;               /* here x*a==1 mod 2**16 */
    x *= 2 - b * x;               /* here x*a==1 mod 2**32 */
    x *= 2 - b * x;               /* here x*a==1 mod 2**64 */

    /* rho = -1/m mod b */
    *rho = -x;
}

extern void sp_2048_mul_d_32(sp_digit* r, const sp_digit* a, sp_digit b);
#if (defined(WOLFSSL_HAVE_SP_RSA) || defined(WOLFSSL_HAVE_SP_DH)) && !defined(WOLFSSL_RSA_PUBLIC_ONLY)
extern sp_digit sp_2048_sub_in_place_16(sp_digit* a, const sp_digit* b);
/* r = 2^n mod m where n is the number of bits to reduce by.
 * Given m must be 2048 bits, just need to subtract.
 *
 * r  A single precision number.
 * m  A signle precision number.
 */
static void sp_2048_mont_norm_16(sp_digit* r, const sp_digit* m)
{
    XMEMSET(r, 0, sizeof(sp_digit) * 16);

    /* r = 2^n mod m */
    sp_2048_sub_in_place_16(r, m);
}

extern sp_digit sp_2048_cond_sub_16(sp_digit* r, const sp_digit* a, const sp_digit* b, sp_digit m);
extern void sp_2048_mont_reduce_16(sp_digit* a, const sp_digit* m, sp_digit mp);
/* Multiply two Montogmery form numbers mod the modulus (prime).
 * (r = a * b mod m)
 *
 * r   Result of multiplication.
 * a   First number to multiply in Montogmery form.
 * b   Second number to multiply in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_2048_mont_mul_16(sp_digit* r, const sp_digit* a, const sp_digit* b,
        const sp_digit* m, sp_digit mp)
{
    sp_2048_mul_16(r, a, b);
    sp_2048_mont_reduce_16(r, m, mp);
}

/* Square the Montgomery form number. (r = a * a mod m)
 *
 * r   Result of squaring.
 * a   Number to square in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_2048_mont_sqr_16(sp_digit* r, const sp_digit* a, const sp_digit* m,
        sp_digit mp)
{
    sp_2048_sqr_16(r, a);
    sp_2048_mont_reduce_16(r, m, mp);
}

extern void sp_2048_mul_d_16(sp_digit* r, const sp_digit* a, sp_digit b);
extern void sp_2048_mul_d_avx2_16(sp_digit* r, const sp_digit* a, const sp_digit b);
/* Divide the double width number (d1|d0) by the dividend. (d1|d0 / div)
 *
 * d1   The high order half of the number to divide.
 * d0   The low order half of the number to divide.
 * div  The dividend.
 * returns the result of the division.
 */
static WC_INLINE sp_digit div_2048_word_16(sp_digit d1, sp_digit d0,
        sp_digit div)
{
    register sp_digit r asm("rax");
    __asm__ __volatile__ (
        "divq %3"
        : "=a" (r)
        : "d" (d1), "a" (d0), "r" (div)
        :
    );
    return r;
}
extern int64_t sp_2048_cmp_16(const sp_digit* a, const sp_digit* b);
/* Divide d in a and put remainder into r (m*d + r = a)
 * m is not calculated as it is not needed at this time.
 *
 * a  Nmber to be divided.
 * d  Number to divide with.
 * m  Multiplier result.
 * r  Remainder from the division.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_2048_div_16(const sp_digit* a, const sp_digit* d, sp_digit* m,
        sp_digit* r)
{
    sp_digit t1[32], t2[17];
    sp_digit div, r1;
    int i;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    (void)m;

    div = d[15];
    XMEMCPY(t1, a, sizeof(*t1) * 2 * 16);
    for (i=15; i>=0; i--) {
        r1 = div_2048_word_16(t1[16 + i], t1[16 + i - 1], div);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_2048_mul_d_avx2_16(t2, d, r1);
        else
#endif
            sp_2048_mul_d_16(t2, d, r1);
        t1[16 + i] += sp_2048_sub_in_place_16(&t1[i], t2);
        t1[16 + i] -= t2[16];
        sp_2048_mask_16(t2, d, t1[16 + i]);
        t1[16 + i] += sp_2048_add_16(&t1[i], &t1[i], t2);
        sp_2048_mask_16(t2, d, t1[16 + i]);
        t1[16 + i] += sp_2048_add_16(&t1[i], &t1[i], t2);
    }

    r1 = sp_2048_cmp_16(t1, d) >= 0;
    sp_2048_cond_sub_16(r, t1, d, (sp_digit)0 - r1);

    return MP_OKAY;
}

/* Reduce a modulo m into r. (r = a mod m)
 *
 * r  A single precision number that is the reduced result.
 * a  A single precision number that is to be reduced.
 * m  A single precision number that is the modulus to reduce with.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_2048_mod_16(sp_digit* r, const sp_digit* a, const sp_digit* m)
{
    return sp_2048_div_16(a, m, NULL, r);
}

/* Modular exponentiate a to the e mod m. (r = a^e mod m)
 *
 * r     A single precision number that is the result of the operation.
 * a     A single precision number being exponentiated.
 * e     A single precision number that is the exponent.
 * bits  The number of bits in the exponent.
 * m     A single precision number that is the modulus.
 * returns 0 on success and MEMORY_E on dynamic memory allocation failure.
 */
static int sp_2048_mod_exp_16(sp_digit* r, const sp_digit* a, const sp_digit* e,
        int bits, const sp_digit* m, int reduceA)
{
#ifndef WOLFSSL_SMALL_STACK
    sp_digit t[32][32];
#else
    sp_digit* t[32];
    sp_digit* td;
#endif
    sp_digit* norm;
    sp_digit mp = 1;
    sp_digit n;
    sp_digit mask;
    int i;
    int c, y;
    int err = MP_OKAY;

#ifdef WOLFSSL_SMALL_STACK
    td = (sp_digit*)XMALLOC(sizeof(sp_digit) * 32 * 32, NULL,
                            DYNAMIC_TYPE_TMP_BUFFER);
    if (td == NULL)
        err = MEMORY_E;

    if (err == MP_OKAY) {
        for (i=0; i<32; i++)
            t[i] = td + i * 32;
        norm = t[0];
    }
#else
    norm = t[0];
#endif

    if (err == MP_OKAY) {
        sp_2048_mont_setup(m, &mp);
        sp_2048_mont_norm_16(norm, m);

        XMEMSET(t[1], 0, sizeof(sp_digit) * 16);
        if (reduceA) {
            err = sp_2048_mod_16(t[1] + 16, a, m);
            if (err == MP_OKAY)
                err = sp_2048_mod_16(t[1], t[1], m);
        }
        else {
            XMEMCPY(t[1] + 16, a, sizeof(sp_digit) * 16);
            err = sp_2048_mod_16(t[1], t[1], m);
        }
    }

    if (err == MP_OKAY) {
        sp_2048_mont_sqr_16(t[ 2], t[ 1], m, mp);
        sp_2048_mont_mul_16(t[ 3], t[ 2], t[ 1], m, mp);
        sp_2048_mont_sqr_16(t[ 4], t[ 2], m, mp);
        sp_2048_mont_mul_16(t[ 5], t[ 3], t[ 2], m, mp);
        sp_2048_mont_sqr_16(t[ 6], t[ 3], m, mp);
        sp_2048_mont_mul_16(t[ 7], t[ 4], t[ 3], m, mp);
        sp_2048_mont_sqr_16(t[ 8], t[ 4], m, mp);
        sp_2048_mont_mul_16(t[ 9], t[ 5], t[ 4], m, mp);
        sp_2048_mont_sqr_16(t[10], t[ 5], m, mp);
        sp_2048_mont_mul_16(t[11], t[ 6], t[ 5], m, mp);
        sp_2048_mont_sqr_16(t[12], t[ 6], m, mp);
        sp_2048_mont_mul_16(t[13], t[ 7], t[ 6], m, mp);
        sp_2048_mont_sqr_16(t[14], t[ 7], m, mp);
        sp_2048_mont_mul_16(t[15], t[ 8], t[ 7], m, mp);
        sp_2048_mont_sqr_16(t[16], t[ 8], m, mp);
        sp_2048_mont_mul_16(t[17], t[ 9], t[ 8], m, mp);
        sp_2048_mont_sqr_16(t[18], t[ 9], m, mp);
        sp_2048_mont_mul_16(t[19], t[10], t[ 9], m, mp);
        sp_2048_mont_sqr_16(t[20], t[10], m, mp);
        sp_2048_mont_mul_16(t[21], t[11], t[10], m, mp);
        sp_2048_mont_sqr_16(t[22], t[11], m, mp);
        sp_2048_mont_mul_16(t[23], t[12], t[11], m, mp);
        sp_2048_mont_sqr_16(t[24], t[12], m, mp);
        sp_2048_mont_mul_16(t[25], t[13], t[12], m, mp);
        sp_2048_mont_sqr_16(t[26], t[13], m, mp);
        sp_2048_mont_mul_16(t[27], t[14], t[13], m, mp);
        sp_2048_mont_sqr_16(t[28], t[14], m, mp);
        sp_2048_mont_mul_16(t[29], t[15], t[14], m, mp);
        sp_2048_mont_sqr_16(t[30], t[15], m, mp);
        sp_2048_mont_mul_16(t[31], t[16], t[15], m, mp);

        i = (bits - 1) / 64;
        n = e[i--];
        c = bits & 63;
        if (c == 0)
            c = 64;
        c -= bits % 5;
        y = (int)(n >> c);
        n <<= 64 - c;
        XMEMCPY(r, t[y], sizeof(sp_digit) * 16);
        for (; i>=0 || c>=5; ) {
            if (c == 0) {
                n = e[i--];
                y = (int)(n >> 59);
                n <<= 5;
                c = 59;
            }
            else if (c < 5) {
                y = (int)(n >> 59);
                n = e[i--];
                c = 5 - c;
                y |= n >> (64 - c);
                n <<= c;
                c = 64 - c;
            }
            else {
                y = (n >> 59) & 0x1f;
                n <<= 5;
                c -= 5;
            }

            sp_2048_mont_sqr_16(r, r, m, mp);
            sp_2048_mont_sqr_16(r, r, m, mp);
            sp_2048_mont_sqr_16(r, r, m, mp);
            sp_2048_mont_sqr_16(r, r, m, mp);
            sp_2048_mont_sqr_16(r, r, m, mp);

            sp_2048_mont_mul_16(r, r, t[y], m, mp);
        }

        XMEMSET(&r[16], 0, sizeof(sp_digit) * 16);
        sp_2048_mont_reduce_16(r, m, mp);

        mask = 0 - (sp_2048_cmp_16(r, m) >= 0);
        sp_2048_cond_sub_16(r, r, m, mask);
    }

#ifdef WOLFSSL_SMALL_STACK
    if (td != NULL)
        XFREE(td, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return err;
}

extern void sp_2048_mont_reduce_avx2_16(sp_digit* a, const sp_digit* m, sp_digit mp);
#ifdef HAVE_INTEL_AVX2
/* Multiply two Montogmery form numbers mod the modulus (prime).
 * (r = a * b mod m)
 *
 * r   Result of multiplication.
 * a   First number to multiply in Montogmery form.
 * b   Second number to multiply in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_2048_mont_mul_avx2_16(sp_digit* r, const sp_digit* a, const sp_digit* b,
        const sp_digit* m, sp_digit mp)
{
    sp_2048_mul_avx2_16(r, a, b);
    sp_2048_mont_reduce_avx2_16(r, m, mp);
}

#endif /* HAVE_INTEL_AVX2 */
#ifdef HAVE_INTEL_AVX2
/* Square the Montgomery form number. (r = a * a mod m)
 *
 * r   Result of squaring.
 * a   Number to square in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_2048_mont_sqr_avx2_16(sp_digit* r, const sp_digit* a, const sp_digit* m,
        sp_digit mp)
{
    sp_2048_sqr_avx2_16(r, a);
    sp_2048_mont_reduce_avx2_16(r, m, mp);
}

#endif /* HAVE_INTEL_AVX2 */
#ifdef HAVE_INTEL_AVX2
/* Modular exponentiate a to the e mod m. (r = a^e mod m)
 *
 * r     A single precision number that is the result of the operation.
 * a     A single precision number being exponentiated.
 * e     A single precision number that is the exponent.
 * bits  The number of bits in the exponent.
 * m     A single precision number that is the modulus.
 * returns 0 on success and MEMORY_E on dynamic memory allocation failure.
 */
static int sp_2048_mod_exp_avx2_16(sp_digit* r, const sp_digit* a, const sp_digit* e,
        int bits, const sp_digit* m, int reduceA)
{
#ifndef WOLFSSL_SMALL_STACK
    sp_digit t[32][32];
#else
    sp_digit* t[32];
    sp_digit* td;
#endif
    sp_digit* norm;
    sp_digit mp = 1;
    sp_digit n;
    sp_digit mask;
    int i;
    int c, y;
    int err = MP_OKAY;

#ifdef WOLFSSL_SMALL_STACK
    td = (sp_digit*)XMALLOC(sizeof(sp_digit) * 32 * 32, NULL,
                            DYNAMIC_TYPE_TMP_BUFFER);
    if (td == NULL)
        err = MEMORY_E;

    if (err == MP_OKAY) {
        for (i=0; i<32; i++)
            t[i] = td + i * 32;
        norm = t[0];
    }
#else
    norm = t[0];
#endif

    if (err == MP_OKAY) {
        sp_2048_mont_setup(m, &mp);
        sp_2048_mont_norm_16(norm, m);

        XMEMSET(t[1], 0, sizeof(sp_digit) * 16);
        if (reduceA) {
            err = sp_2048_mod_16(t[1] + 16, a, m);
            if (err == MP_OKAY)
                err = sp_2048_mod_16(t[1], t[1], m);
        }
        else {
            XMEMCPY(t[1] + 16, a, sizeof(sp_digit) * 16);
            err = sp_2048_mod_16(t[1], t[1], m);
        }
    }

    if (err == MP_OKAY) {
        sp_2048_mont_sqr_avx2_16(t[ 2], t[ 1], m, mp);
        sp_2048_mont_mul_avx2_16(t[ 3], t[ 2], t[ 1], m, mp);
        sp_2048_mont_sqr_avx2_16(t[ 4], t[ 2], m, mp);
        sp_2048_mont_mul_avx2_16(t[ 5], t[ 3], t[ 2], m, mp);
        sp_2048_mont_sqr_avx2_16(t[ 6], t[ 3], m, mp);
        sp_2048_mont_mul_avx2_16(t[ 7], t[ 4], t[ 3], m, mp);
        sp_2048_mont_sqr_avx2_16(t[ 8], t[ 4], m, mp);
        sp_2048_mont_mul_avx2_16(t[ 9], t[ 5], t[ 4], m, mp);
        sp_2048_mont_sqr_avx2_16(t[10], t[ 5], m, mp);
        sp_2048_mont_mul_avx2_16(t[11], t[ 6], t[ 5], m, mp);
        sp_2048_mont_sqr_avx2_16(t[12], t[ 6], m, mp);
        sp_2048_mont_mul_avx2_16(t[13], t[ 7], t[ 6], m, mp);
        sp_2048_mont_sqr_avx2_16(t[14], t[ 7], m, mp);
        sp_2048_mont_mul_avx2_16(t[15], t[ 8], t[ 7], m, mp);
        sp_2048_mont_sqr_avx2_16(t[16], t[ 8], m, mp);
        sp_2048_mont_mul_avx2_16(t[17], t[ 9], t[ 8], m, mp);
        sp_2048_mont_sqr_avx2_16(t[18], t[ 9], m, mp);
        sp_2048_mont_mul_avx2_16(t[19], t[10], t[ 9], m, mp);
        sp_2048_mont_sqr_avx2_16(t[20], t[10], m, mp);
        sp_2048_mont_mul_avx2_16(t[21], t[11], t[10], m, mp);
        sp_2048_mont_sqr_avx2_16(t[22], t[11], m, mp);
        sp_2048_mont_mul_avx2_16(t[23], t[12], t[11], m, mp);
        sp_2048_mont_sqr_avx2_16(t[24], t[12], m, mp);
        sp_2048_mont_mul_avx2_16(t[25], t[13], t[12], m, mp);
        sp_2048_mont_sqr_avx2_16(t[26], t[13], m, mp);
        sp_2048_mont_mul_avx2_16(t[27], t[14], t[13], m, mp);
        sp_2048_mont_sqr_avx2_16(t[28], t[14], m, mp);
        sp_2048_mont_mul_avx2_16(t[29], t[15], t[14], m, mp);
        sp_2048_mont_sqr_avx2_16(t[30], t[15], m, mp);
        sp_2048_mont_mul_avx2_16(t[31], t[16], t[15], m, mp);

        i = (bits - 1) / 64;
        n = e[i--];
        c = bits & 63;
        if (c == 0)
            c = 64;
        c -= bits % 5;
        y = (int)(n >> c);
        n <<= 64 - c;
        XMEMCPY(r, t[y], sizeof(sp_digit) * 16);
        for (; i>=0 || c>=5; ) {
            if (c == 0) {
                n = e[i--];
                y = (int)(n >> 59);
                n <<= 5;
                c = 59;
            }
            else if (c < 5) {
                y = (int)(n >> 59);
                n = e[i--];
                c = 5 - c;
                y |= n >> (64 - c);
                n <<= c;
                c = 64 - c;
            }
            else {
                y = (n >> 59) & 0x1f;
                n <<= 5;
                c -= 5;
            }

            sp_2048_mont_sqr_avx2_16(r, r, m, mp);
            sp_2048_mont_sqr_avx2_16(r, r, m, mp);
            sp_2048_mont_sqr_avx2_16(r, r, m, mp);
            sp_2048_mont_sqr_avx2_16(r, r, m, mp);
            sp_2048_mont_sqr_avx2_16(r, r, m, mp);

            sp_2048_mont_mul_avx2_16(r, r, t[y], m, mp);
        }

        XMEMSET(&r[16], 0, sizeof(sp_digit) * 16);
        sp_2048_mont_reduce_avx2_16(r, m, mp);

        mask = 0 - (sp_2048_cmp_16(r, m) >= 0);
        sp_2048_cond_sub_16(r, r, m, mask);
    }

#ifdef WOLFSSL_SMALL_STACK
    if (td != NULL)
        XFREE(td, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return err;
}
#endif /* HAVE_INTEL_AVX2 */

#endif /* (WOLFSSL_HAVE_SP_RSA || WOLFSSL_HAVE_SP_DH) && !WOLFSSL_RSA_PUBLIC_ONLY */

#if defined(WOLFSSL_HAVE_SP_RSA) || defined(WOLFSSL_HAVE_SP_DH)
/* r = 2^n mod m where n is the number of bits to reduce by.
 * Given m must be 2048 bits, just need to subtract.
 *
 * r  A single precision number.
 * m  A signle precision number.
 */
static void sp_2048_mont_norm_32(sp_digit* r, const sp_digit* m)
{
    XMEMSET(r, 0, sizeof(sp_digit) * 32);

    /* r = 2^n mod m */
    sp_2048_sub_in_place_32(r, m);
}

#endif /* WOLFSSL_HAVE_SP_RSA || WOLFSSL_HAVE_SP_DH */
extern sp_digit sp_2048_cond_sub_32(sp_digit* r, const sp_digit* a, const sp_digit* b, sp_digit m);
extern void sp_2048_mont_reduce_32(sp_digit* a, const sp_digit* m, sp_digit mp);
/* Multiply two Montogmery form numbers mod the modulus (prime).
 * (r = a * b mod m)
 *
 * r   Result of multiplication.
 * a   First number to multiply in Montogmery form.
 * b   Second number to multiply in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_2048_mont_mul_32(sp_digit* r, const sp_digit* a, const sp_digit* b,
        const sp_digit* m, sp_digit mp)
{
    sp_2048_mul_32(r, a, b);
    sp_2048_mont_reduce_32(r, m, mp);
}

/* Square the Montgomery form number. (r = a * a mod m)
 *
 * r   Result of squaring.
 * a   Number to square in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_2048_mont_sqr_32(sp_digit* r, const sp_digit* a, const sp_digit* m,
        sp_digit mp)
{
    sp_2048_sqr_32(r, a);
    sp_2048_mont_reduce_32(r, m, mp);
}

#ifndef WOLFSSL_RSA_PUBLIC_ONLY
extern void sp_2048_mul_d_avx2_32(sp_digit* r, const sp_digit* a, const sp_digit b);
/* Divide the double width number (d1|d0) by the dividend. (d1|d0 / div)
 *
 * d1   The high order half of the number to divide.
 * d0   The low order half of the number to divide.
 * div  The dividend.
 * returns the result of the division.
 */
static WC_INLINE sp_digit div_2048_word_32(sp_digit d1, sp_digit d0,
        sp_digit div)
{
    register sp_digit r asm("rax");
    __asm__ __volatile__ (
        "divq %3"
        : "=a" (r)
        : "d" (d1), "a" (d0), "r" (div)
        :
    );
    return r;
}
/* AND m into each word of a and store in r.
 *
 * r  A single precision integer.
 * a  A single precision integer.
 * m  Mask to AND against each digit.
 */
static void sp_2048_mask_32(sp_digit* r, const sp_digit* a, sp_digit m)
{
#ifdef WOLFSSL_SP_SMALL
    int i;

    for (i=0; i<32; i++)
        r[i] = a[i] & m;
#else
    int i;

    for (i = 0; i < 32; i += 8) {
        r[i+0] = a[i+0] & m;
        r[i+1] = a[i+1] & m;
        r[i+2] = a[i+2] & m;
        r[i+3] = a[i+3] & m;
        r[i+4] = a[i+4] & m;
        r[i+5] = a[i+5] & m;
        r[i+6] = a[i+6] & m;
        r[i+7] = a[i+7] & m;
    }
#endif
}

extern int64_t sp_2048_cmp_32(const sp_digit* a, const sp_digit* b);
/* Divide d in a and put remainder into r (m*d + r = a)
 * m is not calculated as it is not needed at this time.
 *
 * a  Nmber to be divided.
 * d  Number to divide with.
 * m  Multiplier result.
 * r  Remainder from the division.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_2048_div_32(const sp_digit* a, const sp_digit* d, sp_digit* m,
        sp_digit* r)
{
    sp_digit t1[64], t2[33];
    sp_digit div, r1;
    int i;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    (void)m;

    div = d[31];
    XMEMCPY(t1, a, sizeof(*t1) * 2 * 32);
    for (i=31; i>=0; i--) {
        r1 = div_2048_word_32(t1[32 + i], t1[32 + i - 1], div);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_2048_mul_d_avx2_32(t2, d, r1);
        else
#endif
            sp_2048_mul_d_32(t2, d, r1);
        t1[32 + i] += sp_2048_sub_in_place_32(&t1[i], t2);
        t1[32 + i] -= t2[32];
        sp_2048_mask_32(t2, d, t1[32 + i]);
        t1[32 + i] += sp_2048_add_32(&t1[i], &t1[i], t2);
        sp_2048_mask_32(t2, d, t1[32 + i]);
        t1[32 + i] += sp_2048_add_32(&t1[i], &t1[i], t2);
    }

    r1 = sp_2048_cmp_32(t1, d) >= 0;
    sp_2048_cond_sub_32(r, t1, d, (sp_digit)0 - r1);

    return MP_OKAY;
}

/* Reduce a modulo m into r. (r = a mod m)
 *
 * r  A single precision number that is the reduced result.
 * a  A single precision number that is to be reduced.
 * m  A single precision number that is the modulus to reduce with.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_2048_mod_32(sp_digit* r, const sp_digit* a, const sp_digit* m)
{
    return sp_2048_div_32(a, m, NULL, r);
}

#endif /* WOLFSSL_RSA_PUBLIC_ONLY */
/* Divide d in a and put remainder into r (m*d + r = a)
 * m is not calculated as it is not needed at this time.
 *
 * a  Nmber to be divided.
 * d  Number to divide with.
 * m  Multiplier result.
 * r  Remainder from the division.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_2048_div_32_cond(const sp_digit* a, const sp_digit* d, sp_digit* m,
        sp_digit* r)
{
    sp_digit t1[64], t2[33];
    sp_digit div, r1;
    int i;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    (void)m;

    div = d[31];
    XMEMCPY(t1, a, sizeof(*t1) * 2 * 32);
    for (i=31; i>=0; i--) {
        r1 = div_2048_word_32(t1[32 + i], t1[32 + i - 1], div);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_2048_mul_d_avx2_32(t2, d, r1);
        else
#endif
            sp_2048_mul_d_32(t2, d, r1);
        t1[32 + i] += sp_2048_sub_in_place_32(&t1[i], t2);
        t1[32 + i] -= t2[32];
        if (t1[32 + i] != 0) {
            t1[32 + i] += sp_2048_add_32(&t1[i], &t1[i], d);
            if (t1[32 + i] != 0)
                t1[32 + i] += sp_2048_add_32(&t1[i], &t1[i], d);
        }
    }

    r1 = sp_2048_cmp_32(t1, d) >= 0;
    sp_2048_cond_sub_32(r, t1, d, (sp_digit)0 - r1);

    return MP_OKAY;
}

/* Reduce a modulo m into r. (r = a mod m)
 *
 * r  A single precision number that is the reduced result.
 * a  A single precision number that is to be reduced.
 * m  A single precision number that is the modulus to reduce with.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_2048_mod_32_cond(sp_digit* r, const sp_digit* a, const sp_digit* m)
{
    return sp_2048_div_32_cond(a, m, NULL, r);
}

#if (defined(WOLFSSL_HAVE_SP_RSA) && !defined(WOLFSSL_RSA_PUBLIC_ONLY)) || defined(WOLFSSL_HAVE_SP_DH)
/* Modular exponentiate a to the e mod m. (r = a^e mod m)
 *
 * r     A single precision number that is the result of the operation.
 * a     A single precision number being exponentiated.
 * e     A single precision number that is the exponent.
 * bits  The number of bits in the exponent.
 * m     A single precision number that is the modulus.
 * returns 0 on success and MEMORY_E on dynamic memory allocation failure.
 */
static int sp_2048_mod_exp_32(sp_digit* r, const sp_digit* a, const sp_digit* e,
        int bits, const sp_digit* m, int reduceA)
{
#ifndef WOLFSSL_SMALL_STACK
    sp_digit t[32][64];
#else
    sp_digit* t[32];
    sp_digit* td;
#endif
    sp_digit* norm;
    sp_digit mp = 1;
    sp_digit n;
    sp_digit mask;
    int i;
    int c, y;
    int err = MP_OKAY;

#ifdef WOLFSSL_SMALL_STACK
    td = (sp_digit*)XMALLOC(sizeof(sp_digit) * 32 * 64, NULL,
                            DYNAMIC_TYPE_TMP_BUFFER);
    if (td == NULL)
        err = MEMORY_E;

    if (err == MP_OKAY) {
        for (i=0; i<32; i++)
            t[i] = td + i * 64;
        norm = t[0];
    }
#else
    norm = t[0];
#endif

    if (err == MP_OKAY) {
        sp_2048_mont_setup(m, &mp);
        sp_2048_mont_norm_32(norm, m);

        XMEMSET(t[1], 0, sizeof(sp_digit) * 32);
        if (reduceA) {
            err = sp_2048_mod_32(t[1] + 32, a, m);
            if (err == MP_OKAY)
                err = sp_2048_mod_32(t[1], t[1], m);
        }
        else {
            XMEMCPY(t[1] + 32, a, sizeof(sp_digit) * 32);
            err = sp_2048_mod_32(t[1], t[1], m);
        }
    }

    if (err == MP_OKAY) {
        sp_2048_mont_sqr_32(t[ 2], t[ 1], m, mp);
        sp_2048_mont_mul_32(t[ 3], t[ 2], t[ 1], m, mp);
        sp_2048_mont_sqr_32(t[ 4], t[ 2], m, mp);
        sp_2048_mont_mul_32(t[ 5], t[ 3], t[ 2], m, mp);
        sp_2048_mont_sqr_32(t[ 6], t[ 3], m, mp);
        sp_2048_mont_mul_32(t[ 7], t[ 4], t[ 3], m, mp);
        sp_2048_mont_sqr_32(t[ 8], t[ 4], m, mp);
        sp_2048_mont_mul_32(t[ 9], t[ 5], t[ 4], m, mp);
        sp_2048_mont_sqr_32(t[10], t[ 5], m, mp);
        sp_2048_mont_mul_32(t[11], t[ 6], t[ 5], m, mp);
        sp_2048_mont_sqr_32(t[12], t[ 6], m, mp);
        sp_2048_mont_mul_32(t[13], t[ 7], t[ 6], m, mp);
        sp_2048_mont_sqr_32(t[14], t[ 7], m, mp);
        sp_2048_mont_mul_32(t[15], t[ 8], t[ 7], m, mp);
        sp_2048_mont_sqr_32(t[16], t[ 8], m, mp);
        sp_2048_mont_mul_32(t[17], t[ 9], t[ 8], m, mp);
        sp_2048_mont_sqr_32(t[18], t[ 9], m, mp);
        sp_2048_mont_mul_32(t[19], t[10], t[ 9], m, mp);
        sp_2048_mont_sqr_32(t[20], t[10], m, mp);
        sp_2048_mont_mul_32(t[21], t[11], t[10], m, mp);
        sp_2048_mont_sqr_32(t[22], t[11], m, mp);
        sp_2048_mont_mul_32(t[23], t[12], t[11], m, mp);
        sp_2048_mont_sqr_32(t[24], t[12], m, mp);
        sp_2048_mont_mul_32(t[25], t[13], t[12], m, mp);
        sp_2048_mont_sqr_32(t[26], t[13], m, mp);
        sp_2048_mont_mul_32(t[27], t[14], t[13], m, mp);
        sp_2048_mont_sqr_32(t[28], t[14], m, mp);
        sp_2048_mont_mul_32(t[29], t[15], t[14], m, mp);
        sp_2048_mont_sqr_32(t[30], t[15], m, mp);
        sp_2048_mont_mul_32(t[31], t[16], t[15], m, mp);

        i = (bits - 1) / 64;
        n = e[i--];
        c = bits & 63;
        if (c == 0)
            c = 64;
        c -= bits % 5;
        y = (int)(n >> c);
        n <<= 64 - c;
        XMEMCPY(r, t[y], sizeof(sp_digit) * 32);
        for (; i>=0 || c>=5; ) {
            if (c == 0) {
                n = e[i--];
                y = (int)(n >> 59);
                n <<= 5;
                c = 59;
            }
            else if (c < 5) {
                y = (int)(n >> 59);
                n = e[i--];
                c = 5 - c;
                y |= n >> (64 - c);
                n <<= c;
                c = 64 - c;
            }
            else {
                y = (n >> 59) & 0x1f;
                n <<= 5;
                c -= 5;
            }

            sp_2048_mont_sqr_32(r, r, m, mp);
            sp_2048_mont_sqr_32(r, r, m, mp);
            sp_2048_mont_sqr_32(r, r, m, mp);
            sp_2048_mont_sqr_32(r, r, m, mp);
            sp_2048_mont_sqr_32(r, r, m, mp);

            sp_2048_mont_mul_32(r, r, t[y], m, mp);
        }

        XMEMSET(&r[32], 0, sizeof(sp_digit) * 32);
        sp_2048_mont_reduce_32(r, m, mp);

        mask = 0 - (sp_2048_cmp_32(r, m) >= 0);
        sp_2048_cond_sub_32(r, r, m, mask);
    }

#ifdef WOLFSSL_SMALL_STACK
    if (td != NULL)
        XFREE(td, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return err;
}
#endif /* (WOLFSSL_HAVE_SP_RSA && !WOLFSSL_RSA_PUBLIC_ONLY) || WOLFSSL_HAVE_SP_DH */

extern void sp_2048_mont_reduce_avx2_32(sp_digit* a, const sp_digit* m, sp_digit mp);
#ifdef HAVE_INTEL_AVX2
/* Multiply two Montogmery form numbers mod the modulus (prime).
 * (r = a * b mod m)
 *
 * r   Result of multiplication.
 * a   First number to multiply in Montogmery form.
 * b   Second number to multiply in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_2048_mont_mul_avx2_32(sp_digit* r, const sp_digit* a, const sp_digit* b,
        const sp_digit* m, sp_digit mp)
{
    sp_2048_mul_avx2_32(r, a, b);
    sp_2048_mont_reduce_avx2_32(r, m, mp);
}

#endif /* HAVE_INTEL_AVX2 */
#ifdef HAVE_INTEL_AVX2
/* Square the Montgomery form number. (r = a * a mod m)
 *
 * r   Result of squaring.
 * a   Number to square in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_2048_mont_sqr_avx2_32(sp_digit* r, const sp_digit* a, const sp_digit* m,
        sp_digit mp)
{
    sp_2048_sqr_avx2_32(r, a);
    sp_2048_mont_reduce_avx2_32(r, m, mp);
}

#endif /* HAVE_INTEL_AVX2 */
#if (defined(WOLFSSL_HAVE_SP_RSA) && !defined(WOLFSSL_RSA_PUBLIC_ONLY)) || defined(WOLFSSL_HAVE_SP_DH)
#ifdef HAVE_INTEL_AVX2
/* Modular exponentiate a to the e mod m. (r = a^e mod m)
 *
 * r     A single precision number that is the result of the operation.
 * a     A single precision number being exponentiated.
 * e     A single precision number that is the exponent.
 * bits  The number of bits in the exponent.
 * m     A single precision number that is the modulus.
 * returns 0 on success and MEMORY_E on dynamic memory allocation failure.
 */
static int sp_2048_mod_exp_avx2_32(sp_digit* r, const sp_digit* a, const sp_digit* e,
        int bits, const sp_digit* m, int reduceA)
{
#ifndef WOLFSSL_SMALL_STACK
    sp_digit t[32][64];
#else
    sp_digit* t[32];
    sp_digit* td;
#endif
    sp_digit* norm;
    sp_digit mp = 1;
    sp_digit n;
    sp_digit mask;
    int i;
    int c, y;
    int err = MP_OKAY;

#ifdef WOLFSSL_SMALL_STACK
    td = (sp_digit*)XMALLOC(sizeof(sp_digit) * 32 * 64, NULL,
                            DYNAMIC_TYPE_TMP_BUFFER);
    if (td == NULL)
        err = MEMORY_E;

    if (err == MP_OKAY) {
        for (i=0; i<32; i++)
            t[i] = td + i * 64;
        norm = t[0];
    }
#else
    norm = t[0];
#endif

    if (err == MP_OKAY) {
        sp_2048_mont_setup(m, &mp);
        sp_2048_mont_norm_32(norm, m);

        XMEMSET(t[1], 0, sizeof(sp_digit) * 32);
        if (reduceA) {
            err = sp_2048_mod_32(t[1] + 32, a, m);
            if (err == MP_OKAY)
                err = sp_2048_mod_32(t[1], t[1], m);
        }
        else {
            XMEMCPY(t[1] + 32, a, sizeof(sp_digit) * 32);
            err = sp_2048_mod_32(t[1], t[1], m);
        }
    }

    if (err == MP_OKAY) {
        sp_2048_mont_sqr_avx2_32(t[ 2], t[ 1], m, mp);
        sp_2048_mont_mul_avx2_32(t[ 3], t[ 2], t[ 1], m, mp);
        sp_2048_mont_sqr_avx2_32(t[ 4], t[ 2], m, mp);
        sp_2048_mont_mul_avx2_32(t[ 5], t[ 3], t[ 2], m, mp);
        sp_2048_mont_sqr_avx2_32(t[ 6], t[ 3], m, mp);
        sp_2048_mont_mul_avx2_32(t[ 7], t[ 4], t[ 3], m, mp);
        sp_2048_mont_sqr_avx2_32(t[ 8], t[ 4], m, mp);
        sp_2048_mont_mul_avx2_32(t[ 9], t[ 5], t[ 4], m, mp);
        sp_2048_mont_sqr_avx2_32(t[10], t[ 5], m, mp);
        sp_2048_mont_mul_avx2_32(t[11], t[ 6], t[ 5], m, mp);
        sp_2048_mont_sqr_avx2_32(t[12], t[ 6], m, mp);
        sp_2048_mont_mul_avx2_32(t[13], t[ 7], t[ 6], m, mp);
        sp_2048_mont_sqr_avx2_32(t[14], t[ 7], m, mp);
        sp_2048_mont_mul_avx2_32(t[15], t[ 8], t[ 7], m, mp);
        sp_2048_mont_sqr_avx2_32(t[16], t[ 8], m, mp);
        sp_2048_mont_mul_avx2_32(t[17], t[ 9], t[ 8], m, mp);
        sp_2048_mont_sqr_avx2_32(t[18], t[ 9], m, mp);
        sp_2048_mont_mul_avx2_32(t[19], t[10], t[ 9], m, mp);
        sp_2048_mont_sqr_avx2_32(t[20], t[10], m, mp);
        sp_2048_mont_mul_avx2_32(t[21], t[11], t[10], m, mp);
        sp_2048_mont_sqr_avx2_32(t[22], t[11], m, mp);
        sp_2048_mont_mul_avx2_32(t[23], t[12], t[11], m, mp);
        sp_2048_mont_sqr_avx2_32(t[24], t[12], m, mp);
        sp_2048_mont_mul_avx2_32(t[25], t[13], t[12], m, mp);
        sp_2048_mont_sqr_avx2_32(t[26], t[13], m, mp);
        sp_2048_mont_mul_avx2_32(t[27], t[14], t[13], m, mp);
        sp_2048_mont_sqr_avx2_32(t[28], t[14], m, mp);
        sp_2048_mont_mul_avx2_32(t[29], t[15], t[14], m, mp);
        sp_2048_mont_sqr_avx2_32(t[30], t[15], m, mp);
        sp_2048_mont_mul_avx2_32(t[31], t[16], t[15], m, mp);

        i = (bits - 1) / 64;
        n = e[i--];
        c = bits & 63;
        if (c == 0)
            c = 64;
        c -= bits % 5;
        y = (int)(n >> c);
        n <<= 64 - c;
        XMEMCPY(r, t[y], sizeof(sp_digit) * 32);
        for (; i>=0 || c>=5; ) {
            if (c == 0) {
                n = e[i--];
                y = (int)(n >> 59);
                n <<= 5;
                c = 59;
            }
            else if (c < 5) {
                y = (int)(n >> 59);
                n = e[i--];
                c = 5 - c;
                y |= n >> (64 - c);
                n <<= c;
                c = 64 - c;
            }
            else {
                y = (n >> 59) & 0x1f;
                n <<= 5;
                c -= 5;
            }

            sp_2048_mont_sqr_avx2_32(r, r, m, mp);
            sp_2048_mont_sqr_avx2_32(r, r, m, mp);
            sp_2048_mont_sqr_avx2_32(r, r, m, mp);
            sp_2048_mont_sqr_avx2_32(r, r, m, mp);
            sp_2048_mont_sqr_avx2_32(r, r, m, mp);

            sp_2048_mont_mul_avx2_32(r, r, t[y], m, mp);
        }

        XMEMSET(&r[32], 0, sizeof(sp_digit) * 32);
        sp_2048_mont_reduce_avx2_32(r, m, mp);

        mask = 0 - (sp_2048_cmp_32(r, m) >= 0);
        sp_2048_cond_sub_32(r, r, m, mask);
    }

#ifdef WOLFSSL_SMALL_STACK
    if (td != NULL)
        XFREE(td, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return err;
}
#endif /* HAVE_INTEL_AVX2 */
#endif /* (WOLFSSL_HAVE_SP_RSA && !WOLFSSL_RSA_PUBLIC_ONLY) || WOLFSSL_HAVE_SP_DH */

#ifdef WOLFSSL_HAVE_SP_RSA
/* RSA public key operation.
 *
 * in      Array of bytes representing the number to exponentiate, base.
 * inLen   Number of bytes in base.
 * em      Public exponent.
 * mm      Modulus.
 * out     Buffer to hold big-endian bytes of exponentiation result.
 *         Must be at least 256 bytes long.
 * outLen  Number of bytes in result.
 * returns 0 on success, MP_TO_E when the outLen is too small, MP_READ_E when
 * an array is too long and MEMORY_E when dynamic memory allocation fails.
 */
int sp_RsaPublic_2048(const byte* in, word32 inLen, mp_int* em, mp_int* mm,
    byte* out, word32* outLen)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_digit ad[64], md[32], rd[64];
#else
    sp_digit* d = NULL;
#endif
    sp_digit* a;
    sp_digit *ah;
    sp_digit* m;
    sp_digit* r;
    sp_digit e[1];
    int err = MP_OKAY;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    if (*outLen < 256)
        err = MP_TO_E;
    if (err == MP_OKAY && (mp_count_bits(em) > 64 || inLen > 256 ||
                                                     mp_count_bits(mm) != 2048))
        err = MP_READ_E;

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        d = (sp_digit*)XMALLOC(sizeof(sp_digit) * 32 * 5, NULL,
                                                              DYNAMIC_TYPE_RSA);
        if (d == NULL)
            err = MEMORY_E;
    }

    if (err == MP_OKAY) {
        a = d;
        r = a + 32 * 2;
        m = r + 32 * 2;
        ah = a + 32;
    }
#else
    a = ad;
    m = md;
    r = rd;
    ah = a + 32;
#endif

    if (err == MP_OKAY) {
        sp_2048_from_bin(ah, 32, in, inLen);
#if DIGIT_BIT >= 64
        e[0] = em->dp[0];
#else
        e[0] = em->dp[0];
        if (em->used > 1)
            e[0] |= ((sp_digit)em->dp[1]) << DIGIT_BIT;
#endif
        if (e[0] == 0)
            err = MP_EXPTMOD_E;
    }
    if (err == MP_OKAY) {
        sp_2048_from_mp(m, 32, mm);

        if (e[0] == 0x3) {
#ifdef HAVE_INTEL_AVX2
            if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags)) {
                if (err == MP_OKAY) {
                    sp_2048_sqr_avx2_32(r, ah);
                    err = sp_2048_mod_32_cond(r, r, m);
                }
                if (err == MP_OKAY) {
                    sp_2048_mul_avx2_32(r, ah, r);
                    err = sp_2048_mod_32_cond(r, r, m);
                }
            }
            else
#endif
            {
                if (err == MP_OKAY) {
                    sp_2048_sqr_32(r, ah);
                    err = sp_2048_mod_32_cond(r, r, m);
                }
                if (err == MP_OKAY) {
                    sp_2048_mul_32(r, ah, r);
                    err = sp_2048_mod_32_cond(r, r, m);
                }
            }
        }
        else {
            int i;
            sp_digit mp;

            sp_2048_mont_setup(m, &mp);

            /* Convert to Montgomery form. */
            XMEMSET(a, 0, sizeof(sp_digit) * 32);
            err = sp_2048_mod_32_cond(a, a, m);

            if (err == MP_OKAY) {
                for (i=63; i>=0; i--)
                    if (e[0] >> i)
                        break;

                XMEMCPY(r, a, sizeof(sp_digit) * 32);
#ifdef HAVE_INTEL_AVX2
                if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags)) {
                    for (i--; i>=0; i--) {
                        sp_2048_mont_sqr_avx2_32(r, r, m, mp);
                        if (((e[0] >> i) & 1) == 1)
                            sp_2048_mont_mul_avx2_32(r, r, a, m, mp);
                    }
                    XMEMSET(&r[32], 0, sizeof(sp_digit) * 32);
                    sp_2048_mont_reduce_avx2_32(r, m, mp);
                }
                else
#endif
                {
                    for (i--; i>=0; i--) {
                        sp_2048_mont_sqr_32(r, r, m, mp);
                        if (((e[0] >> i) & 1) == 1)
                            sp_2048_mont_mul_32(r, r, a, m, mp);
                    }
                    XMEMSET(&r[32], 0, sizeof(sp_digit) * 32);
                    sp_2048_mont_reduce_32(r, m, mp);
                }

                for (i = 31; i > 0; i--) {
                    if (r[i] != m[i])
                        break;
                }
                if (r[i] >= m[i])
                    sp_2048_sub_in_place_32(r, m);
            }
        }
    }

    if (err == MP_OKAY) {
        sp_2048_to_bin(r, out);
        *outLen = 256;
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (d != NULL)
        XFREE(d, NULL, DYNAMIC_TYPE_RSA);
#endif

    return err;
}

/* RSA private key operation.
 *
 * in      Array of bytes representing the number to exponentiate, base.
 * inLen   Number of bytes in base.
 * dm      Private exponent.
 * pm      First prime.
 * qm      Second prime.
 * dpm     First prime's CRT exponent.
 * dqm     Second prime's CRT exponent.
 * qim     Inverse of second prime mod p.
 * mm      Modulus.
 * out     Buffer to hold big-endian bytes of exponentiation result.
 *         Must be at least 256 bytes long.
 * outLen  Number of bytes in result.
 * returns 0 on success, MP_TO_E when the outLen is too small, MP_READ_E when
 * an array is too long and MEMORY_E when dynamic memory allocation fails.
 */
int sp_RsaPrivate_2048(const byte* in, word32 inLen, mp_int* dm,
    mp_int* pm, mp_int* qm, mp_int* dpm, mp_int* dqm, mp_int* qim, mp_int* mm,
    byte* out, word32* outLen)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_digit ad[32 * 2];
    sp_digit pd[16], qd[16], dpd[16];
    sp_digit tmpad[32], tmpbd[32];
#else
    sp_digit* t = NULL;
#endif
    sp_digit* a;
    sp_digit* p;
    sp_digit* q;
    sp_digit* dp;
    sp_digit* dq;
    sp_digit* qi;
    sp_digit* tmp;
    sp_digit* tmpa;
    sp_digit* tmpb;
    sp_digit* r;
    sp_digit c;
    int err = MP_OKAY;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    (void)dm;
    (void)mm;

    if (*outLen < 256)
        err = MP_TO_E;
    if (err == MP_OKAY && (inLen > 256 || mp_count_bits(mm) != 2048))
        err = MP_READ_E;

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        t = (sp_digit*)XMALLOC(sizeof(sp_digit) * 16 * 11, NULL,
                                                              DYNAMIC_TYPE_RSA);
        if (t == NULL)
            err = MEMORY_E;
    }
    if (err == MP_OKAY) {
        a = t;
        p = a + 32 * 2;
        q = p + 16;
        qi = dq = dp = q + 16;
        tmpa = qi + 16;
        tmpb = tmpa + 32;

        tmp = t;
        r = tmp + 32;
    }
#else
    r = a = ad;
    p = pd;
    q = qd;
    qi = dq = dp = dpd;
    tmpa = tmpad;
    tmpb = tmpbd;
    tmp = a + 32;
#endif

    if (err == MP_OKAY) {
        sp_2048_from_bin(a, 32, in, inLen);
        sp_2048_from_mp(p, 16, pm);
        sp_2048_from_mp(q, 16, qm);
        sp_2048_from_mp(dp, 16, dpm);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_2048_mod_exp_avx2_16(tmpa, a, dp, 1024, p, 1);
        else
#endif
            err = sp_2048_mod_exp_16(tmpa, a, dp, 1024, p, 1);
    }
    if (err == MP_OKAY) {
        sp_2048_from_mp(dq, 16, dqm);
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_2048_mod_exp_avx2_16(tmpb, a, dq, 1024, q, 1);
       else
#endif
            err = sp_2048_mod_exp_16(tmpb, a, dq, 1024, q, 1);
    }

    if (err == MP_OKAY) {
        c = sp_2048_sub_in_place_16(tmpa, tmpb);
        sp_2048_mask_16(tmp, p, c);
        sp_2048_add_16(tmpa, tmpa, tmp);

        sp_2048_from_mp(qi, 16, qim);
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_2048_mul_avx2_16(tmpa, tmpa, qi);
        else
#endif
            sp_2048_mul_16(tmpa, tmpa, qi);
        err = sp_2048_mod_16(tmpa, tmpa, p);
    }

    if (err == MP_OKAY) {
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_2048_mul_avx2_16(tmpa, q, tmpa);
        else
#endif
            sp_2048_mul_16(tmpa, q, tmpa);
        XMEMSET(&tmpb[16], 0, sizeof(sp_digit) * 16);
        sp_2048_add_32(r, tmpb, tmpa);

        sp_2048_to_bin(r, out);
        *outLen = 256;
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (t != NULL) {
        XMEMSET(t, 0, sizeof(sp_digit) * 16 * 11);
        XFREE(t, NULL, DYNAMIC_TYPE_RSA);
    }
#else
    XMEMSET(tmpad, 0, sizeof(tmpad));
    XMEMSET(tmpbd, 0, sizeof(tmpbd));
    XMEMSET(pd, 0, sizeof(pd));
    XMEMSET(qd, 0, sizeof(qd));
    XMEMSET(dpd, 0, sizeof(dpd));
#endif

    return err;
}
#endif /* WOLFSSL_HAVE_SP_RSA */
#if defined(WOLFSSL_HAVE_SP_DH) || (defined(WOLFSSL_HAVE_SP_RSA) && \
                                              !defined(WOLFSSL_RSA_PUBLIC_ONLY))
/* Convert an array of sp_digit to an mp_int.
 *
 * a  A single precision integer.
 * r  A multi-precision integer.
 */
static int sp_2048_to_mp(const sp_digit* a, mp_int* r)
{
    int err;

    err = mp_grow(r, (2048 + DIGIT_BIT - 1) / DIGIT_BIT);
    if (err == MP_OKAY) {
#if DIGIT_BIT == 64
        XMEMCPY(r->dp, a, sizeof(sp_digit) * 32);
        r->used = 32;
        mp_clamp(r);
#elif DIGIT_BIT < 64
        int i, j = 0, s = 0;

        r->dp[0] = 0;
        for (i = 0; i < 32; i++) {
            r->dp[j] |= a[i] << s;
            r->dp[j] &= (1l << DIGIT_BIT) - 1;
            s = DIGIT_BIT - s;
            r->dp[++j] = a[i] >> s;
            while (s + DIGIT_BIT <= 64) {
                s += DIGIT_BIT;
                r->dp[j++] &= (1l << DIGIT_BIT) - 1;
                if (s == SP_WORD_SIZE)
                    r->dp[j] = 0;
                else
                    r->dp[j] = a[i] >> s;
            }
            s = 64 - s;
        }
        r->used = (2048 + DIGIT_BIT - 1) / DIGIT_BIT;
        mp_clamp(r);
#else
        int i, j = 0, s = 0;

        r->dp[0] = 0;
        for (i = 0; i < 32; i++) {
            r->dp[j] |= ((mp_digit)a[i]) << s;
            if (s + 64 >= DIGIT_BIT) {
    #if DIGIT_BIT != 32 && DIGIT_BIT != 64
                r->dp[j] &= (1l << DIGIT_BIT) - 1;
    #endif
                s = DIGIT_BIT - s;
                r->dp[++j] = a[i] >> s;
                s = 64 - s;
            }
            else
                s += 64;
        }
        r->used = (2048 + DIGIT_BIT - 1) / DIGIT_BIT;
        mp_clamp(r);
#endif
    }

    return err;
}

/* Perform the modular exponentiation for Diffie-Hellman.
 *
 * base  Base. MP integer.
 * exp   Exponent. MP integer.
 * mod   Modulus. MP integer.
 * res   Result. MP integer.
 * returs 0 on success, MP_READ_E if there are too many bytes in an array
 * and MEMORY_E if memory allocation fails.
 */
int sp_ModExp_2048(mp_int* base, mp_int* exp, mp_int* mod, mp_int* res)
{
    int err = MP_OKAY;
    sp_digit b[64], e[32], m[32];
    sp_digit* r = b;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif
    int expBits = mp_count_bits(exp);

    if (mp_count_bits(base) > 2048 || expBits > 2048 ||
                                                   mp_count_bits(mod) != 2048) {
        err = MP_READ_E;
    }

    if (err == MP_OKAY) {
        sp_2048_from_mp(b, 32, base);
        sp_2048_from_mp(e, 32, exp);
        sp_2048_from_mp(m, 32, mod);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_2048_mod_exp_avx2_32(r, b, e, expBits, m, 0);
        else
#endif
            err = sp_2048_mod_exp_32(r, b, e, expBits, m, 0);
    }

    if (err == MP_OKAY) {
        err = sp_2048_to_mp(r, res);
    }

    XMEMSET(e, 0, sizeof(e));

    return err;
}

#ifdef WOLFSSL_HAVE_SP_DH
#ifdef HAVE_FFDHE_2048
extern void sp_2048_lshift_32(sp_digit* r, const sp_digit* a, int n);
#ifdef HAVE_INTEL_AVX2
/* Modular exponentiate 2 to the e mod m. (r = 2^e mod m)
 *
 * r     A single precision number that is the result of the operation.
 * e     A single precision number that is the exponent.
 * bits  The number of bits in the exponent.
 * m     A single precision number that is the modulus.
 * returns 0 on success and MEMORY_E on dynamic memory allocation failure.
 */
static int sp_2048_mod_exp_2_avx2_32(sp_digit* r, const sp_digit* e, int bits,
        const sp_digit* m)
{
#ifndef WOLFSSL_SMALL_STACK
    sp_digit nd[64];
    sp_digit td[33];
#else
    sp_digit* td;
#endif
    sp_digit* norm;
    sp_digit* tmp;
    sp_digit mp = 1;
    sp_digit n, o;
    sp_digit mask;
    int i;
    int c, y;
    int err = MP_OKAY;

#ifdef WOLFSSL_SMALL_STACK
    td = (sp_digit*)XMALLOC(sizeof(sp_digit) * 97, NULL,
                            DYNAMIC_TYPE_TMP_BUFFER);
    if (td == NULL)
        err = MEMORY_E;

    if (err == MP_OKAY) {
        norm = td;
        tmp  = td + 64;
    }
#else
    norm = nd;
    tmp  = td;
#endif

    if (err == MP_OKAY) {
        sp_2048_mont_setup(m, &mp);
        sp_2048_mont_norm_32(norm, m);

        i = (bits - 1) / 64;
        n = e[i--];
        c = bits & 63;
        if (c == 0)
            c = 64;
        c -= bits % 6;
        y = (int)(n >> c);
        n <<= 64 - c;
        sp_2048_lshift_32(r, norm, y);
        for (; i>=0 || c>=6; ) {
            if (c == 0) {
                n = e[i--];
                y = (int)(n >> 58);
                n <<= 6;
                c = 58;
            }
            else if (c < 6) {
                y = (int)(n >> 58);
                n = e[i--];
                c = 6 - c;
                y |= n >> (64 - c);
                n <<= c;
                c = 64 - c;
            }
            else {
                y = (n >> 58) & 0x3f;
                n <<= 6;
                c -= 6;
            }

            sp_2048_mont_sqr_avx2_32(r, r, m, mp);
            sp_2048_mont_sqr_avx2_32(r, r, m, mp);
            sp_2048_mont_sqr_avx2_32(r, r, m, mp);
            sp_2048_mont_sqr_avx2_32(r, r, m, mp);
            sp_2048_mont_sqr_avx2_32(r, r, m, mp);
            sp_2048_mont_sqr_avx2_32(r, r, m, mp);

            sp_2048_lshift_32(r, r, y);
            sp_2048_mul_d_avx2_32(tmp, norm, r[32]);
            r[32] = 0;
            o = sp_2048_add_32(r, r, tmp);
            sp_2048_cond_sub_32(r, r, m, (sp_digit)0 - o);
        }

        XMEMSET(&r[32], 0, sizeof(sp_digit) * 32);
        sp_2048_mont_reduce_avx2_32(r, m, mp);

        mask = 0 - (sp_2048_cmp_32(r, m) >= 0);
        sp_2048_cond_sub_32(r, r, m, mask);
    }

#ifdef WOLFSSL_SMALL_STACK
    if (td != NULL)
        XFREE(td, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return err;
}
#endif /* HAVE_INTEL_AVX2 */

/* Modular exponentiate 2 to the e mod m. (r = 2^e mod m)
 *
 * r     A single precision number that is the result of the operation.
 * e     A single precision number that is the exponent.
 * bits  The number of bits in the exponent.
 * m     A single precision number that is the modulus.
 * returns 0 on success and MEMORY_E on dynamic memory allocation failure.
 */
static int sp_2048_mod_exp_2_32(sp_digit* r, const sp_digit* e, int bits,
        const sp_digit* m)
{
#ifndef WOLFSSL_SMALL_STACK
    sp_digit nd[64];
    sp_digit td[33];
#else
    sp_digit* td;
#endif
    sp_digit* norm;
    sp_digit* tmp;
    sp_digit mp = 1;
    sp_digit n, o;
    sp_digit mask;
    int i;
    int c, y;
    int err = MP_OKAY;

#ifdef WOLFSSL_SMALL_STACK
    td = (sp_digit*)XMALLOC(sizeof(sp_digit) * 97, NULL,
                            DYNAMIC_TYPE_TMP_BUFFER);
    if (td == NULL)
        err = MEMORY_E;

    if (err == MP_OKAY) {
        norm = td;
        tmp  = td + 64;
    }
#else
    norm = nd;
    tmp  = td;
#endif

    if (err == MP_OKAY) {
        sp_2048_mont_setup(m, &mp);
        sp_2048_mont_norm_32(norm, m);

        i = (bits - 1) / 64;
        n = e[i--];
        c = bits & 63;
        if (c == 0)
            c = 64;
        c -= bits % 6;
        y = (int)(n >> c);
        n <<= 64 - c;
        sp_2048_lshift_32(r, norm, y);
        for (; i>=0 || c>=6; ) {
            if (c == 0) {
                n = e[i--];
                y = (int)(n >> 58);
                n <<= 6;
                c = 58;
            }
            else if (c < 6) {
                y = (int)(n >> 58);
                n = e[i--];
                c = 6 - c;
                y |= n >> (64 - c);
                n <<= c;
                c = 64 - c;
            }
            else {
                y = (n >> 58) & 0x3f;
                n <<= 6;
                c -= 6;
            }

            sp_2048_mont_sqr_32(r, r, m, mp);
            sp_2048_mont_sqr_32(r, r, m, mp);
            sp_2048_mont_sqr_32(r, r, m, mp);
            sp_2048_mont_sqr_32(r, r, m, mp);
            sp_2048_mont_sqr_32(r, r, m, mp);
            sp_2048_mont_sqr_32(r, r, m, mp);

            sp_2048_lshift_32(r, r, y);
            sp_2048_mul_d_32(tmp, norm, r[32]);
            r[32] = 0;
            o = sp_2048_add_32(r, r, tmp);
            sp_2048_cond_sub_32(r, r, m, (sp_digit)0 - o);
        }

        XMEMSET(&r[32], 0, sizeof(sp_digit) * 32);
        sp_2048_mont_reduce_32(r, m, mp);

        mask = 0 - (sp_2048_cmp_32(r, m) >= 0);
        sp_2048_cond_sub_32(r, r, m, mask);
    }

#ifdef WOLFSSL_SMALL_STACK
    if (td != NULL)
        XFREE(td, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return err;
}

#endif /* HAVE_FFDHE_2048 */

/* Perform the modular exponentiation for Diffie-Hellman.
 *
 * base     Base.
 * exp      Array of bytes that is the exponent.
 * expLen   Length of data, in bytes, in exponent.
 * mod      Modulus.
 * out      Buffer to hold big-endian bytes of exponentiation result.
 *          Must be at least 256 bytes long.
 * outLen   Length, in bytes, of exponentiation result.
 * returs 0 on success, MP_READ_E if there are too many bytes in an array
 * and MEMORY_E if memory allocation fails.
 */
int sp_DhExp_2048(mp_int* base, const byte* exp, word32 expLen,
    mp_int* mod, byte* out, word32* outLen)
{
    int err = MP_OKAY;
    sp_digit b[64], e[32], m[32];
    sp_digit* r = b;
    word32 i;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    if (mp_count_bits(base) > 2048 || expLen > 256 ||
                                                   mp_count_bits(mod) != 2048) {
        err = MP_READ_E;
    }

    if (err == MP_OKAY) {
        sp_2048_from_mp(b, 32, base);
        sp_2048_from_bin(e, 32, exp, expLen);
        sp_2048_from_mp(m, 32, mod);

    #ifdef HAVE_FFDHE_2048
        if (base->used == 1 && base->dp[0] == 2 && m[31] == (sp_digit)-1) {
#ifdef HAVE_INTEL_AVX2
            if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
                err = sp_2048_mod_exp_2_avx2_32(r, e, expLen * 8, m);
            else
#endif
                err = sp_2048_mod_exp_2_32(r, e, expLen * 8, m);
        }
        else
    #endif
        {
#ifdef HAVE_INTEL_AVX2
            if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
                err = sp_2048_mod_exp_avx2_32(r, b, e, expLen * 8, m, 0);
            else
#endif
                err = sp_2048_mod_exp_32(r, b, e, expLen * 8, m, 0);
        }
    }

    if (err == MP_OKAY) {
        sp_2048_to_bin(r, out);
        *outLen = 256;
        for (i=0; i<256 && out[i] == 0; i++) {
        }
        *outLen -= i;
        XMEMMOVE(out, out + i, *outLen);
    }

    XMEMSET(e, 0, sizeof(e));

    return err;
}
#endif
/* Perform the modular exponentiation for Diffie-Hellman.
 *
 * base  Base. MP integer.
 * exp   Exponent. MP integer.
 * mod   Modulus. MP integer.
 * res   Result. MP integer.
 * returs 0 on success, MP_READ_E if there are too many bytes in an array
 * and MEMORY_E if memory allocation fails.
 */
int sp_ModExp_1024(mp_int* base, mp_int* exp, mp_int* mod, mp_int* res)
{
    int err = MP_OKAY;
    sp_digit b[32], e[16], m[16];
    sp_digit* r = b;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif
    int expBits = mp_count_bits(exp);

    if (mp_count_bits(base) > 1024 || expBits > 1024 ||
                                                   mp_count_bits(mod) != 1024) {
        err = MP_READ_E;
    }

    if (err == MP_OKAY) {
        sp_2048_from_mp(b, 16, base);
        sp_2048_from_mp(e, 16, exp);
        sp_2048_from_mp(m, 16, mod);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_2048_mod_exp_avx2_16(r, b, e, expBits, m, 0);
        else
#endif
            err = sp_2048_mod_exp_16(r, b, e, expBits, m, 0);
    }

    if (err == MP_OKAY) {
        XMEMSET(r + 16, 0, sizeof(*r) * 16);
        err = sp_2048_to_mp(r, res);
    }

    XMEMSET(e, 0, sizeof(e));

    return err;
}

#endif /* WOLFSSL_HAVE_SP_DH || (WOLFSSL_HAVE_SP_RSA && !WOLFSSL_RSA_PUBLIC_ONLY) */

#endif /* !WOLFSSL_SP_NO_2048 */

#ifndef WOLFSSL_SP_NO_3072
/* Read big endian unsigned byte aray into r.
 *
 * r  A single precision integer.
 * a  Byte array.
 * n  Number of bytes in array to read.
 */
static void sp_3072_from_bin(sp_digit* r, int max, const byte* a, int n)
{
    int i, j = 0, s = 0;

    r[0] = 0;
    for (i = n-1; i >= 0; i--) {
        r[j] |= ((sp_digit)a[i]) << s;
        if (s >= 56) {
            r[j] &= 0xffffffffffffffffl;
            s = 64 - s;
            if (j + 1 >= max)
                break;
            r[++j] = a[i] >> s;
            s = 8 - s;
        }
        else
            s += 8;
    }

    for (j++; j < max; j++)
        r[j] = 0;
}

/* Convert an mp_int to an array of sp_digit.
 *
 * r  A single precision integer.
 * a  A multi-precision integer.
 */
static void sp_3072_from_mp(sp_digit* r, int max, const mp_int* a)
{
#if DIGIT_BIT == 64
    int j;

    XMEMCPY(r, a->dp, sizeof(sp_digit) * a->used);

    for (j = a->used; j < max; j++)
        r[j] = 0;
#elif DIGIT_BIT > 64
    int i, j = 0, s = 0;

    r[0] = 0;
    for (i = 0; i < a->used && j < max; i++) {
        r[j] |= a->dp[i] << s;
        r[j] &= 0xffffffffffffffffl;
        s = 64 - s;
        if (j + 1 >= max)
            break;
        r[++j] = (sp_digit)(a->dp[i] >> s);
        while (s + 64 <= DIGIT_BIT) {
            s += 64;
            r[j] &= 0xffffffffffffffffl;
            if (j + 1 >= max)
                break;
            if (s < DIGIT_BIT)
                r[++j] = (sp_digit)(a->dp[i] >> s);
            else
                r[++j] = 0;
        }
        s = DIGIT_BIT - s;
    }

    for (j++; j < max; j++)
        r[j] = 0;
#else
    int i, j = 0, s = 0;

    r[0] = 0;
    for (i = 0; i < a->used && j < max; i++) {
        r[j] |= ((sp_digit)a->dp[i]) << s;
        if (s + DIGIT_BIT >= 64) {
            r[j] &= 0xffffffffffffffffl;
            if (j + 1 >= max)
                break;
            s = 64 - s;
            if (s == DIGIT_BIT) {
                r[++j] = 0;
                s = 0;
            }
            else {
                r[++j] = a->dp[i] >> s;
                s = DIGIT_BIT - s;
            }
        }
        else
            s += DIGIT_BIT;
    }

    for (j++; j < max; j++)
        r[j] = 0;
#endif
}

/* Write r as big endian to byte aray.
 * Fixed length number of bytes written: 384
 *
 * r  A single precision integer.
 * a  Byte array.
 */
static void sp_3072_to_bin(sp_digit* r, byte* a)
{
    int i, j, s = 0, b;

    j = 3072 / 8 - 1;
    a[j] = 0;
    for (i=0; i<48 && j>=0; i++) {
        b = 0;
        a[j--] |= r[i] << s; b += 8 - s;
        if (j < 0)
            break;
        while (b < 64) {
            a[j--] = r[i] >> b; b += 8;
            if (j < 0)
                break;
        }
        s = 8 - (b - 64);
        if (j >= 0)
            a[j] = 0;
        if (s != 0)
            j++;
    }
}

extern void sp_3072_mul_24(sp_digit* r, const sp_digit* a, const sp_digit* b);
extern void sp_3072_sqr_24(sp_digit* r, const sp_digit* a);
extern void sp_3072_mul_avx2_24(sp_digit* r, const sp_digit* a, const sp_digit* b);
extern void sp_3072_sqr_avx2_24(sp_digit* r, const sp_digit* a);
extern sp_digit sp_3072_add_24(sp_digit* r, const sp_digit* a, const sp_digit* b);
extern sp_digit sp_3072_sub_in_place_48(sp_digit* a, const sp_digit* b);
extern sp_digit sp_3072_add_48(sp_digit* r, const sp_digit* a, const sp_digit* b);
/* AND m into each word of a and store in r.
 *
 * r  A single precision integer.
 * a  A single precision integer.
 * m  Mask to AND against each digit.
 */
static void sp_3072_mask_24(sp_digit* r, const sp_digit* a, sp_digit m)
{
#ifdef WOLFSSL_SP_SMALL
    int i;

    for (i=0; i<24; i++)
        r[i] = a[i] & m;
#else
    int i;

    for (i = 0; i < 24; i += 8) {
        r[i+0] = a[i+0] & m;
        r[i+1] = a[i+1] & m;
        r[i+2] = a[i+2] & m;
        r[i+3] = a[i+3] & m;
        r[i+4] = a[i+4] & m;
        r[i+5] = a[i+5] & m;
        r[i+6] = a[i+6] & m;
        r[i+7] = a[i+7] & m;
    }
#endif
}

/* Multiply a and b into r. (r = a * b)
 *
 * r  A single precision integer.
 * a  A single precision integer.
 * b  A single precision integer.
 */
SP_NOINLINE static void sp_3072_mul_48(sp_digit* r, const sp_digit* a,
        const sp_digit* b)
{
    sp_digit* z0 = r;
    sp_digit z1[48];
    sp_digit a1[24];
    sp_digit b1[24];
    sp_digit z2[48];
    sp_digit u, ca, cb;

    ca = sp_3072_add_24(a1, a, &a[24]);
    cb = sp_3072_add_24(b1, b, &b[24]);
    u  = ca & cb;
    sp_3072_mul_24(z1, a1, b1);
    sp_3072_mul_24(z2, &a[24], &b[24]);
    sp_3072_mul_24(z0, a, b);
    sp_3072_mask_24(r + 48, a1, 0 - cb);
    sp_3072_mask_24(b1, b1, 0 - ca);
    u += sp_3072_add_24(r + 48, r + 48, b1);
    u += sp_3072_sub_in_place_48(z1, z2);
    u += sp_3072_sub_in_place_48(z1, z0);
    u += sp_3072_add_48(r + 24, r + 24, z1);
    r[72] = u;
    XMEMSET(r + 72 + 1, 0, sizeof(sp_digit) * (24 - 1));
    sp_3072_add_48(r + 48, r + 48, z2);
}

/* Square a and put result in r. (r = a * a)
 *
 * r  A single precision integer.
 * a  A single precision integer.
 */
SP_NOINLINE static void sp_3072_sqr_48(sp_digit* r, const sp_digit* a)
{
    sp_digit* z0 = r;
    sp_digit z2[48];
    sp_digit z1[48];
    sp_digit a1[24];
    sp_digit u;

    u = sp_3072_add_24(a1, a, &a[24]);
    sp_3072_sqr_24(z1, a1);
    sp_3072_sqr_24(z2, &a[24]);
    sp_3072_sqr_24(z0, a);
    sp_3072_mask_24(r + 48, a1, 0 - u);
    u += sp_3072_add_24(r + 48, r + 48, r + 48);
    u += sp_3072_sub_in_place_48(z1, z2);
    u += sp_3072_sub_in_place_48(z1, z0);
    u += sp_3072_add_48(r + 24, r + 24, z1);
    r[72] = u;
    XMEMSET(r + 72 + 1, 0, sizeof(sp_digit) * (24 - 1));
    sp_3072_add_48(r + 48, r + 48, z2);
}

#ifdef HAVE_INTEL_AVX2
/* Multiply a and b into r. (r = a * b)
 *
 * r  A single precision integer.
 * a  A single precision integer.
 * b  A single precision integer.
 */
SP_NOINLINE static void sp_3072_mul_avx2_48(sp_digit* r, const sp_digit* a,
        const sp_digit* b)
{
    sp_digit* z0 = r;
    sp_digit z1[48];
    sp_digit a1[24];
    sp_digit b1[24];
    sp_digit z2[48];
    sp_digit u, ca, cb;

    ca = sp_3072_add_24(a1, a, &a[24]);
    cb = sp_3072_add_24(b1, b, &b[24]);
    u  = ca & cb;
    sp_3072_mul_avx2_24(z1, a1, b1);
    sp_3072_mul_avx2_24(z2, &a[24], &b[24]);
    sp_3072_mul_avx2_24(z0, a, b);
    sp_3072_mask_24(r + 48, a1, 0 - cb);
    sp_3072_mask_24(b1, b1, 0 - ca);
    u += sp_3072_add_24(r + 48, r + 48, b1);
    u += sp_3072_sub_in_place_48(z1, z2);
    u += sp_3072_sub_in_place_48(z1, z0);
    u += sp_3072_add_48(r + 24, r + 24, z1);
    r[72] = u;
    XMEMSET(r + 72 + 1, 0, sizeof(sp_digit) * (24 - 1));
    sp_3072_add_48(r + 48, r + 48, z2);
}
#endif /* HAVE_INTEL_AVX2 */

#ifdef HAVE_INTEL_AVX2
/* Square a and put result in r. (r = a * a)
 *
 * r  A single precision integer.
 * a  A single precision integer.
 */
SP_NOINLINE static void sp_3072_sqr_avx2_48(sp_digit* r, const sp_digit* a)
{
    sp_digit* z0 = r;
    sp_digit z2[48];
    sp_digit z1[48];
    sp_digit a1[24];
    sp_digit u;

    u = sp_3072_add_24(a1, a, &a[24]);
    sp_3072_sqr_avx2_24(z1, a1);
    sp_3072_sqr_avx2_24(z2, &a[24]);
    sp_3072_sqr_avx2_24(z0, a);
    sp_3072_mask_24(r + 48, a1, 0 - u);
    u += sp_3072_add_24(r + 48, r + 48, r + 48);
    u += sp_3072_sub_in_place_48(z1, z2);
    u += sp_3072_sub_in_place_48(z1, z0);
    u += sp_3072_add_48(r + 24, r + 24, z1);
    r[72] = u;
    XMEMSET(r + 72 + 1, 0, sizeof(sp_digit) * (24 - 1));
    sp_3072_add_48(r + 48, r + 48, z2);
}
#endif /* HAVE_INTEL_AVX2 */

#if (defined(WOLFSSL_HAVE_SP_RSA) || defined(WOLFSSL_HAVE_SP_DH)) && !defined(WOLFSSL_RSA_PUBLIC_ONLY)
#endif /* (WOLFSSL_HAVE_SP_RSA || WOLFSSL_HAVE_SP_DH) && !WOLFSSL_RSA_PUBLIC_ONLY */

/* Caclulate the bottom digit of -1/a mod 2^n.
 *
 * a    A single precision number.
 * rho  Bottom word of inverse.
 */
static void sp_3072_mont_setup(const sp_digit* a, sp_digit* rho)
{
    sp_digit x, b;

    b = a[0];
    x = (((b + 2) & 4) << 1) + b; /* here x*a==1 mod 2**4 */
    x *= 2 - b * x;               /* here x*a==1 mod 2**8 */
    x *= 2 - b * x;               /* here x*a==1 mod 2**16 */
    x *= 2 - b * x;               /* here x*a==1 mod 2**32 */
    x *= 2 - b * x;               /* here x*a==1 mod 2**64 */

    /* rho = -1/m mod b */
    *rho = -x;
}

extern void sp_3072_mul_d_48(sp_digit* r, const sp_digit* a, sp_digit b);
#if (defined(WOLFSSL_HAVE_SP_RSA) || defined(WOLFSSL_HAVE_SP_DH)) && !defined(WOLFSSL_RSA_PUBLIC_ONLY)
extern sp_digit sp_3072_sub_in_place_24(sp_digit* a, const sp_digit* b);
/* r = 2^n mod m where n is the number of bits to reduce by.
 * Given m must be 3072 bits, just need to subtract.
 *
 * r  A single precision number.
 * m  A signle precision number.
 */
static void sp_3072_mont_norm_24(sp_digit* r, const sp_digit* m)
{
    XMEMSET(r, 0, sizeof(sp_digit) * 24);

    /* r = 2^n mod m */
    sp_3072_sub_in_place_24(r, m);
}

extern sp_digit sp_3072_cond_sub_24(sp_digit* r, const sp_digit* a, const sp_digit* b, sp_digit m);
extern void sp_3072_mont_reduce_24(sp_digit* a, const sp_digit* m, sp_digit mp);
/* Multiply two Montogmery form numbers mod the modulus (prime).
 * (r = a * b mod m)
 *
 * r   Result of multiplication.
 * a   First number to multiply in Montogmery form.
 * b   Second number to multiply in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_3072_mont_mul_24(sp_digit* r, const sp_digit* a, const sp_digit* b,
        const sp_digit* m, sp_digit mp)
{
    sp_3072_mul_24(r, a, b);
    sp_3072_mont_reduce_24(r, m, mp);
}

/* Square the Montgomery form number. (r = a * a mod m)
 *
 * r   Result of squaring.
 * a   Number to square in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_3072_mont_sqr_24(sp_digit* r, const sp_digit* a, const sp_digit* m,
        sp_digit mp)
{
    sp_3072_sqr_24(r, a);
    sp_3072_mont_reduce_24(r, m, mp);
}

extern void sp_3072_mul_d_24(sp_digit* r, const sp_digit* a, sp_digit b);
extern void sp_3072_mul_d_avx2_24(sp_digit* r, const sp_digit* a, const sp_digit b);
/* Divide the double width number (d1|d0) by the dividend. (d1|d0 / div)
 *
 * d1   The high order half of the number to divide.
 * d0   The low order half of the number to divide.
 * div  The dividend.
 * returns the result of the division.
 */
static WC_INLINE sp_digit div_3072_word_24(sp_digit d1, sp_digit d0,
        sp_digit div)
{
    register sp_digit r asm("rax");
    __asm__ __volatile__ (
        "divq %3"
        : "=a" (r)
        : "d" (d1), "a" (d0), "r" (div)
        :
    );
    return r;
}
extern int64_t sp_3072_cmp_24(const sp_digit* a, const sp_digit* b);
/* Divide d in a and put remainder into r (m*d + r = a)
 * m is not calculated as it is not needed at this time.
 *
 * a  Nmber to be divided.
 * d  Number to divide with.
 * m  Multiplier result.
 * r  Remainder from the division.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_3072_div_24(const sp_digit* a, const sp_digit* d, sp_digit* m,
        sp_digit* r)
{
    sp_digit t1[48], t2[25];
    sp_digit div, r1;
    int i;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    (void)m;

    div = d[23];
    XMEMCPY(t1, a, sizeof(*t1) * 2 * 24);
    for (i=23; i>=0; i--) {
        r1 = div_3072_word_24(t1[24 + i], t1[24 + i - 1], div);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_3072_mul_d_avx2_24(t2, d, r1);
        else
#endif
            sp_3072_mul_d_24(t2, d, r1);
        t1[24 + i] += sp_3072_sub_in_place_24(&t1[i], t2);
        t1[24 + i] -= t2[24];
        sp_3072_mask_24(t2, d, t1[24 + i]);
        t1[24 + i] += sp_3072_add_24(&t1[i], &t1[i], t2);
        sp_3072_mask_24(t2, d, t1[24 + i]);
        t1[24 + i] += sp_3072_add_24(&t1[i], &t1[i], t2);
    }

    r1 = sp_3072_cmp_24(t1, d) >= 0;
    sp_3072_cond_sub_24(r, t1, d, (sp_digit)0 - r1);

    return MP_OKAY;
}

/* Reduce a modulo m into r. (r = a mod m)
 *
 * r  A single precision number that is the reduced result.
 * a  A single precision number that is to be reduced.
 * m  A single precision number that is the modulus to reduce with.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_3072_mod_24(sp_digit* r, const sp_digit* a, const sp_digit* m)
{
    return sp_3072_div_24(a, m, NULL, r);
}

/* Modular exponentiate a to the e mod m. (r = a^e mod m)
 *
 * r     A single precision number that is the result of the operation.
 * a     A single precision number being exponentiated.
 * e     A single precision number that is the exponent.
 * bits  The number of bits in the exponent.
 * m     A single precision number that is the modulus.
 * returns 0 on success and MEMORY_E on dynamic memory allocation failure.
 */
static int sp_3072_mod_exp_24(sp_digit* r, const sp_digit* a, const sp_digit* e,
        int bits, const sp_digit* m, int reduceA)
{
#ifndef WOLFSSL_SMALL_STACK
    sp_digit t[32][48];
#else
    sp_digit* t[32];
    sp_digit* td;
#endif
    sp_digit* norm;
    sp_digit mp = 1;
    sp_digit n;
    sp_digit mask;
    int i;
    int c, y;
    int err = MP_OKAY;

#ifdef WOLFSSL_SMALL_STACK
    td = (sp_digit*)XMALLOC(sizeof(sp_digit) * 32 * 48, NULL,
                            DYNAMIC_TYPE_TMP_BUFFER);
    if (td == NULL)
        err = MEMORY_E;

    if (err == MP_OKAY) {
        for (i=0; i<32; i++)
            t[i] = td + i * 48;
        norm = t[0];
    }
#else
    norm = t[0];
#endif

    if (err == MP_OKAY) {
        sp_3072_mont_setup(m, &mp);
        sp_3072_mont_norm_24(norm, m);

        XMEMSET(t[1], 0, sizeof(sp_digit) * 24);
        if (reduceA) {
            err = sp_3072_mod_24(t[1] + 24, a, m);
            if (err == MP_OKAY)
                err = sp_3072_mod_24(t[1], t[1], m);
        }
        else {
            XMEMCPY(t[1] + 24, a, sizeof(sp_digit) * 24);
            err = sp_3072_mod_24(t[1], t[1], m);
        }
    }

    if (err == MP_OKAY) {
        sp_3072_mont_sqr_24(t[ 2], t[ 1], m, mp);
        sp_3072_mont_mul_24(t[ 3], t[ 2], t[ 1], m, mp);
        sp_3072_mont_sqr_24(t[ 4], t[ 2], m, mp);
        sp_3072_mont_mul_24(t[ 5], t[ 3], t[ 2], m, mp);
        sp_3072_mont_sqr_24(t[ 6], t[ 3], m, mp);
        sp_3072_mont_mul_24(t[ 7], t[ 4], t[ 3], m, mp);
        sp_3072_mont_sqr_24(t[ 8], t[ 4], m, mp);
        sp_3072_mont_mul_24(t[ 9], t[ 5], t[ 4], m, mp);
        sp_3072_mont_sqr_24(t[10], t[ 5], m, mp);
        sp_3072_mont_mul_24(t[11], t[ 6], t[ 5], m, mp);
        sp_3072_mont_sqr_24(t[12], t[ 6], m, mp);
        sp_3072_mont_mul_24(t[13], t[ 7], t[ 6], m, mp);
        sp_3072_mont_sqr_24(t[14], t[ 7], m, mp);
        sp_3072_mont_mul_24(t[15], t[ 8], t[ 7], m, mp);
        sp_3072_mont_sqr_24(t[16], t[ 8], m, mp);
        sp_3072_mont_mul_24(t[17], t[ 9], t[ 8], m, mp);
        sp_3072_mont_sqr_24(t[18], t[ 9], m, mp);
        sp_3072_mont_mul_24(t[19], t[10], t[ 9], m, mp);
        sp_3072_mont_sqr_24(t[20], t[10], m, mp);
        sp_3072_mont_mul_24(t[21], t[11], t[10], m, mp);
        sp_3072_mont_sqr_24(t[22], t[11], m, mp);
        sp_3072_mont_mul_24(t[23], t[12], t[11], m, mp);
        sp_3072_mont_sqr_24(t[24], t[12], m, mp);
        sp_3072_mont_mul_24(t[25], t[13], t[12], m, mp);
        sp_3072_mont_sqr_24(t[26], t[13], m, mp);
        sp_3072_mont_mul_24(t[27], t[14], t[13], m, mp);
        sp_3072_mont_sqr_24(t[28], t[14], m, mp);
        sp_3072_mont_mul_24(t[29], t[15], t[14], m, mp);
        sp_3072_mont_sqr_24(t[30], t[15], m, mp);
        sp_3072_mont_mul_24(t[31], t[16], t[15], m, mp);

        i = (bits - 1) / 64;
        n = e[i--];
        c = bits & 63;
        if (c == 0)
            c = 64;
        c -= bits % 5;
        y = (int)(n >> c);
        n <<= 64 - c;
        XMEMCPY(r, t[y], sizeof(sp_digit) * 24);
        for (; i>=0 || c>=5; ) {
            if (c == 0) {
                n = e[i--];
                y = (int)(n >> 59);
                n <<= 5;
                c = 59;
            }
            else if (c < 5) {
                y = (int)(n >> 59);
                n = e[i--];
                c = 5 - c;
                y |= n >> (64 - c);
                n <<= c;
                c = 64 - c;
            }
            else {
                y = (n >> 59) & 0x1f;
                n <<= 5;
                c -= 5;
            }

            sp_3072_mont_sqr_24(r, r, m, mp);
            sp_3072_mont_sqr_24(r, r, m, mp);
            sp_3072_mont_sqr_24(r, r, m, mp);
            sp_3072_mont_sqr_24(r, r, m, mp);
            sp_3072_mont_sqr_24(r, r, m, mp);

            sp_3072_mont_mul_24(r, r, t[y], m, mp);
        }

        XMEMSET(&r[24], 0, sizeof(sp_digit) * 24);
        sp_3072_mont_reduce_24(r, m, mp);

        mask = 0 - (sp_3072_cmp_24(r, m) >= 0);
        sp_3072_cond_sub_24(r, r, m, mask);
    }

#ifdef WOLFSSL_SMALL_STACK
    if (td != NULL)
        XFREE(td, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return err;
}

extern void sp_3072_mont_reduce_avx2_24(sp_digit* a, const sp_digit* m, sp_digit mp);
#ifdef HAVE_INTEL_AVX2
/* Multiply two Montogmery form numbers mod the modulus (prime).
 * (r = a * b mod m)
 *
 * r   Result of multiplication.
 * a   First number to multiply in Montogmery form.
 * b   Second number to multiply in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_3072_mont_mul_avx2_24(sp_digit* r, const sp_digit* a, const sp_digit* b,
        const sp_digit* m, sp_digit mp)
{
    sp_3072_mul_avx2_24(r, a, b);
    sp_3072_mont_reduce_avx2_24(r, m, mp);
}

#endif /* HAVE_INTEL_AVX2 */
#ifdef HAVE_INTEL_AVX2
/* Square the Montgomery form number. (r = a * a mod m)
 *
 * r   Result of squaring.
 * a   Number to square in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_3072_mont_sqr_avx2_24(sp_digit* r, const sp_digit* a, const sp_digit* m,
        sp_digit mp)
{
    sp_3072_sqr_avx2_24(r, a);
    sp_3072_mont_reduce_avx2_24(r, m, mp);
}

#endif /* HAVE_INTEL_AVX2 */
#ifdef HAVE_INTEL_AVX2
/* Modular exponentiate a to the e mod m. (r = a^e mod m)
 *
 * r     A single precision number that is the result of the operation.
 * a     A single precision number being exponentiated.
 * e     A single precision number that is the exponent.
 * bits  The number of bits in the exponent.
 * m     A single precision number that is the modulus.
 * returns 0 on success and MEMORY_E on dynamic memory allocation failure.
 */
static int sp_3072_mod_exp_avx2_24(sp_digit* r, const sp_digit* a, const sp_digit* e,
        int bits, const sp_digit* m, int reduceA)
{
#ifndef WOLFSSL_SMALL_STACK
    sp_digit t[32][48];
#else
    sp_digit* t[32];
    sp_digit* td;
#endif
    sp_digit* norm;
    sp_digit mp = 1;
    sp_digit n;
    sp_digit mask;
    int i;
    int c, y;
    int err = MP_OKAY;

#ifdef WOLFSSL_SMALL_STACK
    td = (sp_digit*)XMALLOC(sizeof(sp_digit) * 32 * 48, NULL,
                            DYNAMIC_TYPE_TMP_BUFFER);
    if (td == NULL)
        err = MEMORY_E;

    if (err == MP_OKAY) {
        for (i=0; i<32; i++)
            t[i] = td + i * 48;
        norm = t[0];
    }
#else
    norm = t[0];
#endif

    if (err == MP_OKAY) {
        sp_3072_mont_setup(m, &mp);
        sp_3072_mont_norm_24(norm, m);

        XMEMSET(t[1], 0, sizeof(sp_digit) * 24);
        if (reduceA) {
            err = sp_3072_mod_24(t[1] + 24, a, m);
            if (err == MP_OKAY)
                err = sp_3072_mod_24(t[1], t[1], m);
        }
        else {
            XMEMCPY(t[1] + 24, a, sizeof(sp_digit) * 24);
            err = sp_3072_mod_24(t[1], t[1], m);
        }
    }

    if (err == MP_OKAY) {
        sp_3072_mont_sqr_avx2_24(t[ 2], t[ 1], m, mp);
        sp_3072_mont_mul_avx2_24(t[ 3], t[ 2], t[ 1], m, mp);
        sp_3072_mont_sqr_avx2_24(t[ 4], t[ 2], m, mp);
        sp_3072_mont_mul_avx2_24(t[ 5], t[ 3], t[ 2], m, mp);
        sp_3072_mont_sqr_avx2_24(t[ 6], t[ 3], m, mp);
        sp_3072_mont_mul_avx2_24(t[ 7], t[ 4], t[ 3], m, mp);
        sp_3072_mont_sqr_avx2_24(t[ 8], t[ 4], m, mp);
        sp_3072_mont_mul_avx2_24(t[ 9], t[ 5], t[ 4], m, mp);
        sp_3072_mont_sqr_avx2_24(t[10], t[ 5], m, mp);
        sp_3072_mont_mul_avx2_24(t[11], t[ 6], t[ 5], m, mp);
        sp_3072_mont_sqr_avx2_24(t[12], t[ 6], m, mp);
        sp_3072_mont_mul_avx2_24(t[13], t[ 7], t[ 6], m, mp);
        sp_3072_mont_sqr_avx2_24(t[14], t[ 7], m, mp);
        sp_3072_mont_mul_avx2_24(t[15], t[ 8], t[ 7], m, mp);
        sp_3072_mont_sqr_avx2_24(t[16], t[ 8], m, mp);
        sp_3072_mont_mul_avx2_24(t[17], t[ 9], t[ 8], m, mp);
        sp_3072_mont_sqr_avx2_24(t[18], t[ 9], m, mp);
        sp_3072_mont_mul_avx2_24(t[19], t[10], t[ 9], m, mp);
        sp_3072_mont_sqr_avx2_24(t[20], t[10], m, mp);
        sp_3072_mont_mul_avx2_24(t[21], t[11], t[10], m, mp);
        sp_3072_mont_sqr_avx2_24(t[22], t[11], m, mp);
        sp_3072_mont_mul_avx2_24(t[23], t[12], t[11], m, mp);
        sp_3072_mont_sqr_avx2_24(t[24], t[12], m, mp);
        sp_3072_mont_mul_avx2_24(t[25], t[13], t[12], m, mp);
        sp_3072_mont_sqr_avx2_24(t[26], t[13], m, mp);
        sp_3072_mont_mul_avx2_24(t[27], t[14], t[13], m, mp);
        sp_3072_mont_sqr_avx2_24(t[28], t[14], m, mp);
        sp_3072_mont_mul_avx2_24(t[29], t[15], t[14], m, mp);
        sp_3072_mont_sqr_avx2_24(t[30], t[15], m, mp);
        sp_3072_mont_mul_avx2_24(t[31], t[16], t[15], m, mp);

        i = (bits - 1) / 64;
        n = e[i--];
        c = bits & 63;
        if (c == 0)
            c = 64;
        c -= bits % 5;
        y = (int)(n >> c);
        n <<= 64 - c;
        XMEMCPY(r, t[y], sizeof(sp_digit) * 24);
        for (; i>=0 || c>=5; ) {
            if (c == 0) {
                n = e[i--];
                y = (int)(n >> 59);
                n <<= 5;
                c = 59;
            }
            else if (c < 5) {
                y = (int)(n >> 59);
                n = e[i--];
                c = 5 - c;
                y |= n >> (64 - c);
                n <<= c;
                c = 64 - c;
            }
            else {
                y = (n >> 59) & 0x1f;
                n <<= 5;
                c -= 5;
            }

            sp_3072_mont_sqr_avx2_24(r, r, m, mp);
            sp_3072_mont_sqr_avx2_24(r, r, m, mp);
            sp_3072_mont_sqr_avx2_24(r, r, m, mp);
            sp_3072_mont_sqr_avx2_24(r, r, m, mp);
            sp_3072_mont_sqr_avx2_24(r, r, m, mp);

            sp_3072_mont_mul_avx2_24(r, r, t[y], m, mp);
        }

        XMEMSET(&r[24], 0, sizeof(sp_digit) * 24);
        sp_3072_mont_reduce_avx2_24(r, m, mp);

        mask = 0 - (sp_3072_cmp_24(r, m) >= 0);
        sp_3072_cond_sub_24(r, r, m, mask);
    }

#ifdef WOLFSSL_SMALL_STACK
    if (td != NULL)
        XFREE(td, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return err;
}
#endif /* HAVE_INTEL_AVX2 */

#endif /* (WOLFSSL_HAVE_SP_RSA || WOLFSSL_HAVE_SP_DH) && !WOLFSSL_RSA_PUBLIC_ONLY */

#if defined(WOLFSSL_HAVE_SP_RSA) || defined(WOLFSSL_HAVE_SP_DH)
/* r = 2^n mod m where n is the number of bits to reduce by.
 * Given m must be 3072 bits, just need to subtract.
 *
 * r  A single precision number.
 * m  A signle precision number.
 */
static void sp_3072_mont_norm_48(sp_digit* r, const sp_digit* m)
{
    XMEMSET(r, 0, sizeof(sp_digit) * 48);

    /* r = 2^n mod m */
    sp_3072_sub_in_place_48(r, m);
}

#endif /* WOLFSSL_HAVE_SP_RSA || WOLFSSL_HAVE_SP_DH */
extern sp_digit sp_3072_cond_sub_48(sp_digit* r, const sp_digit* a, const sp_digit* b, sp_digit m);
extern void sp_3072_mont_reduce_48(sp_digit* a, const sp_digit* m, sp_digit mp);
/* Multiply two Montogmery form numbers mod the modulus (prime).
 * (r = a * b mod m)
 *
 * r   Result of multiplication.
 * a   First number to multiply in Montogmery form.
 * b   Second number to multiply in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_3072_mont_mul_48(sp_digit* r, const sp_digit* a, const sp_digit* b,
        const sp_digit* m, sp_digit mp)
{
    sp_3072_mul_48(r, a, b);
    sp_3072_mont_reduce_48(r, m, mp);
}

/* Square the Montgomery form number. (r = a * a mod m)
 *
 * r   Result of squaring.
 * a   Number to square in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_3072_mont_sqr_48(sp_digit* r, const sp_digit* a, const sp_digit* m,
        sp_digit mp)
{
    sp_3072_sqr_48(r, a);
    sp_3072_mont_reduce_48(r, m, mp);
}

#ifndef WOLFSSL_RSA_PUBLIC_ONLY
extern void sp_3072_mul_d_avx2_48(sp_digit* r, const sp_digit* a, const sp_digit b);
/* Divide the double width number (d1|d0) by the dividend. (d1|d0 / div)
 *
 * d1   The high order half of the number to divide.
 * d0   The low order half of the number to divide.
 * div  The dividend.
 * returns the result of the division.
 */
static WC_INLINE sp_digit div_3072_word_48(sp_digit d1, sp_digit d0,
        sp_digit div)
{
    register sp_digit r asm("rax");
    __asm__ __volatile__ (
        "divq %3"
        : "=a" (r)
        : "d" (d1), "a" (d0), "r" (div)
        :
    );
    return r;
}
/* AND m into each word of a and store in r.
 *
 * r  A single precision integer.
 * a  A single precision integer.
 * m  Mask to AND against each digit.
 */
static void sp_3072_mask_48(sp_digit* r, const sp_digit* a, sp_digit m)
{
#ifdef WOLFSSL_SP_SMALL
    int i;

    for (i=0; i<48; i++)
        r[i] = a[i] & m;
#else
    int i;

    for (i = 0; i < 48; i += 8) {
        r[i+0] = a[i+0] & m;
        r[i+1] = a[i+1] & m;
        r[i+2] = a[i+2] & m;
        r[i+3] = a[i+3] & m;
        r[i+4] = a[i+4] & m;
        r[i+5] = a[i+5] & m;
        r[i+6] = a[i+6] & m;
        r[i+7] = a[i+7] & m;
    }
#endif
}

extern int64_t sp_3072_cmp_48(const sp_digit* a, const sp_digit* b);
/* Divide d in a and put remainder into r (m*d + r = a)
 * m is not calculated as it is not needed at this time.
 *
 * a  Nmber to be divided.
 * d  Number to divide with.
 * m  Multiplier result.
 * r  Remainder from the division.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_3072_div_48(const sp_digit* a, const sp_digit* d, sp_digit* m,
        sp_digit* r)
{
    sp_digit t1[96], t2[49];
    sp_digit div, r1;
    int i;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    (void)m;

    div = d[47];
    XMEMCPY(t1, a, sizeof(*t1) * 2 * 48);
    for (i=47; i>=0; i--) {
        r1 = div_3072_word_48(t1[48 + i], t1[48 + i - 1], div);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_3072_mul_d_avx2_48(t2, d, r1);
        else
#endif
            sp_3072_mul_d_48(t2, d, r1);
        t1[48 + i] += sp_3072_sub_in_place_48(&t1[i], t2);
        t1[48 + i] -= t2[48];
        sp_3072_mask_48(t2, d, t1[48 + i]);
        t1[48 + i] += sp_3072_add_48(&t1[i], &t1[i], t2);
        sp_3072_mask_48(t2, d, t1[48 + i]);
        t1[48 + i] += sp_3072_add_48(&t1[i], &t1[i], t2);
    }

    r1 = sp_3072_cmp_48(t1, d) >= 0;
    sp_3072_cond_sub_48(r, t1, d, (sp_digit)0 - r1);

    return MP_OKAY;
}

/* Reduce a modulo m into r. (r = a mod m)
 *
 * r  A single precision number that is the reduced result.
 * a  A single precision number that is to be reduced.
 * m  A single precision number that is the modulus to reduce with.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_3072_mod_48(sp_digit* r, const sp_digit* a, const sp_digit* m)
{
    return sp_3072_div_48(a, m, NULL, r);
}

#endif /* WOLFSSL_RSA_PUBLIC_ONLY */
/* Divide d in a and put remainder into r (m*d + r = a)
 * m is not calculated as it is not needed at this time.
 *
 * a  Nmber to be divided.
 * d  Number to divide with.
 * m  Multiplier result.
 * r  Remainder from the division.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_3072_div_48_cond(const sp_digit* a, const sp_digit* d, sp_digit* m,
        sp_digit* r)
{
    sp_digit t1[96], t2[49];
    sp_digit div, r1;
    int i;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    (void)m;

    div = d[47];
    XMEMCPY(t1, a, sizeof(*t1) * 2 * 48);
    for (i=47; i>=0; i--) {
        r1 = div_3072_word_48(t1[48 + i], t1[48 + i - 1], div);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_3072_mul_d_avx2_48(t2, d, r1);
        else
#endif
            sp_3072_mul_d_48(t2, d, r1);
        t1[48 + i] += sp_3072_sub_in_place_48(&t1[i], t2);
        t1[48 + i] -= t2[48];
        if (t1[48 + i] != 0) {
            t1[48 + i] += sp_3072_add_48(&t1[i], &t1[i], d);
            if (t1[48 + i] != 0)
                t1[48 + i] += sp_3072_add_48(&t1[i], &t1[i], d);
        }
    }

    r1 = sp_3072_cmp_48(t1, d) >= 0;
    sp_3072_cond_sub_48(r, t1, d, (sp_digit)0 - r1);

    return MP_OKAY;
}

/* Reduce a modulo m into r. (r = a mod m)
 *
 * r  A single precision number that is the reduced result.
 * a  A single precision number that is to be reduced.
 * m  A single precision number that is the modulus to reduce with.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_3072_mod_48_cond(sp_digit* r, const sp_digit* a, const sp_digit* m)
{
    return sp_3072_div_48_cond(a, m, NULL, r);
}

#if (defined(WOLFSSL_HAVE_SP_RSA) && !defined(WOLFSSL_RSA_PUBLIC_ONLY)) || defined(WOLFSSL_HAVE_SP_DH)
/* Modular exponentiate a to the e mod m. (r = a^e mod m)
 *
 * r     A single precision number that is the result of the operation.
 * a     A single precision number being exponentiated.
 * e     A single precision number that is the exponent.
 * bits  The number of bits in the exponent.
 * m     A single precision number that is the modulus.
 * returns 0 on success and MEMORY_E on dynamic memory allocation failure.
 */
static int sp_3072_mod_exp_48(sp_digit* r, const sp_digit* a, const sp_digit* e,
        int bits, const sp_digit* m, int reduceA)
{
#ifndef WOLFSSL_SMALL_STACK
    sp_digit t[32][96];
#else
    sp_digit* t[32];
    sp_digit* td;
#endif
    sp_digit* norm;
    sp_digit mp = 1;
    sp_digit n;
    sp_digit mask;
    int i;
    int c, y;
    int err = MP_OKAY;

#ifdef WOLFSSL_SMALL_STACK
    td = (sp_digit*)XMALLOC(sizeof(sp_digit) * 32 * 96, NULL,
                            DYNAMIC_TYPE_TMP_BUFFER);
    if (td == NULL)
        err = MEMORY_E;

    if (err == MP_OKAY) {
        for (i=0; i<32; i++)
            t[i] = td + i * 96;
        norm = t[0];
    }
#else
    norm = t[0];
#endif

    if (err == MP_OKAY) {
        sp_3072_mont_setup(m, &mp);
        sp_3072_mont_norm_48(norm, m);

        XMEMSET(t[1], 0, sizeof(sp_digit) * 48);
        if (reduceA) {
            err = sp_3072_mod_48(t[1] + 48, a, m);
            if (err == MP_OKAY)
                err = sp_3072_mod_48(t[1], t[1], m);
        }
        else {
            XMEMCPY(t[1] + 48, a, sizeof(sp_digit) * 48);
            err = sp_3072_mod_48(t[1], t[1], m);
        }
    }

    if (err == MP_OKAY) {
        sp_3072_mont_sqr_48(t[ 2], t[ 1], m, mp);
        sp_3072_mont_mul_48(t[ 3], t[ 2], t[ 1], m, mp);
        sp_3072_mont_sqr_48(t[ 4], t[ 2], m, mp);
        sp_3072_mont_mul_48(t[ 5], t[ 3], t[ 2], m, mp);
        sp_3072_mont_sqr_48(t[ 6], t[ 3], m, mp);
        sp_3072_mont_mul_48(t[ 7], t[ 4], t[ 3], m, mp);
        sp_3072_mont_sqr_48(t[ 8], t[ 4], m, mp);
        sp_3072_mont_mul_48(t[ 9], t[ 5], t[ 4], m, mp);
        sp_3072_mont_sqr_48(t[10], t[ 5], m, mp);
        sp_3072_mont_mul_48(t[11], t[ 6], t[ 5], m, mp);
        sp_3072_mont_sqr_48(t[12], t[ 6], m, mp);
        sp_3072_mont_mul_48(t[13], t[ 7], t[ 6], m, mp);
        sp_3072_mont_sqr_48(t[14], t[ 7], m, mp);
        sp_3072_mont_mul_48(t[15], t[ 8], t[ 7], m, mp);
        sp_3072_mont_sqr_48(t[16], t[ 8], m, mp);
        sp_3072_mont_mul_48(t[17], t[ 9], t[ 8], m, mp);
        sp_3072_mont_sqr_48(t[18], t[ 9], m, mp);
        sp_3072_mont_mul_48(t[19], t[10], t[ 9], m, mp);
        sp_3072_mont_sqr_48(t[20], t[10], m, mp);
        sp_3072_mont_mul_48(t[21], t[11], t[10], m, mp);
        sp_3072_mont_sqr_48(t[22], t[11], m, mp);
        sp_3072_mont_mul_48(t[23], t[12], t[11], m, mp);
        sp_3072_mont_sqr_48(t[24], t[12], m, mp);
        sp_3072_mont_mul_48(t[25], t[13], t[12], m, mp);
        sp_3072_mont_sqr_48(t[26], t[13], m, mp);
        sp_3072_mont_mul_48(t[27], t[14], t[13], m, mp);
        sp_3072_mont_sqr_48(t[28], t[14], m, mp);
        sp_3072_mont_mul_48(t[29], t[15], t[14], m, mp);
        sp_3072_mont_sqr_48(t[30], t[15], m, mp);
        sp_3072_mont_mul_48(t[31], t[16], t[15], m, mp);

        i = (bits - 1) / 64;
        n = e[i--];
        c = bits & 63;
        if (c == 0)
            c = 64;
        c -= bits % 5;
        y = (int)(n >> c);
        n <<= 64 - c;
        XMEMCPY(r, t[y], sizeof(sp_digit) * 48);
        for (; i>=0 || c>=5; ) {
            if (c == 0) {
                n = e[i--];
                y = (int)(n >> 59);
                n <<= 5;
                c = 59;
            }
            else if (c < 5) {
                y = (int)(n >> 59);
                n = e[i--];
                c = 5 - c;
                y |= n >> (64 - c);
                n <<= c;
                c = 64 - c;
            }
            else {
                y = (n >> 59) & 0x1f;
                n <<= 5;
                c -= 5;
            }

            sp_3072_mont_sqr_48(r, r, m, mp);
            sp_3072_mont_sqr_48(r, r, m, mp);
            sp_3072_mont_sqr_48(r, r, m, mp);
            sp_3072_mont_sqr_48(r, r, m, mp);
            sp_3072_mont_sqr_48(r, r, m, mp);

            sp_3072_mont_mul_48(r, r, t[y], m, mp);
        }

        XMEMSET(&r[48], 0, sizeof(sp_digit) * 48);
        sp_3072_mont_reduce_48(r, m, mp);

        mask = 0 - (sp_3072_cmp_48(r, m) >= 0);
        sp_3072_cond_sub_48(r, r, m, mask);
    }

#ifdef WOLFSSL_SMALL_STACK
    if (td != NULL)
        XFREE(td, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return err;
}
#endif /* (WOLFSSL_HAVE_SP_RSA && !WOLFSSL_RSA_PUBLIC_ONLY) || WOLFSSL_HAVE_SP_DH */

extern void sp_3072_mont_reduce_avx2_48(sp_digit* a, const sp_digit* m, sp_digit mp);
#ifdef HAVE_INTEL_AVX2
/* Multiply two Montogmery form numbers mod the modulus (prime).
 * (r = a * b mod m)
 *
 * r   Result of multiplication.
 * a   First number to multiply in Montogmery form.
 * b   Second number to multiply in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_3072_mont_mul_avx2_48(sp_digit* r, const sp_digit* a, const sp_digit* b,
        const sp_digit* m, sp_digit mp)
{
    sp_3072_mul_avx2_48(r, a, b);
    sp_3072_mont_reduce_avx2_48(r, m, mp);
}

#endif /* HAVE_INTEL_AVX2 */
#ifdef HAVE_INTEL_AVX2
/* Square the Montgomery form number. (r = a * a mod m)
 *
 * r   Result of squaring.
 * a   Number to square in Montogmery form.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_3072_mont_sqr_avx2_48(sp_digit* r, const sp_digit* a, const sp_digit* m,
        sp_digit mp)
{
    sp_3072_sqr_avx2_48(r, a);
    sp_3072_mont_reduce_avx2_48(r, m, mp);
}

#endif /* HAVE_INTEL_AVX2 */
#if (defined(WOLFSSL_HAVE_SP_RSA) && !defined(WOLFSSL_RSA_PUBLIC_ONLY)) || defined(WOLFSSL_HAVE_SP_DH)
#ifdef HAVE_INTEL_AVX2
/* Modular exponentiate a to the e mod m. (r = a^e mod m)
 *
 * r     A single precision number that is the result of the operation.
 * a     A single precision number being exponentiated.
 * e     A single precision number that is the exponent.
 * bits  The number of bits in the exponent.
 * m     A single precision number that is the modulus.
 * returns 0 on success and MEMORY_E on dynamic memory allocation failure.
 */
static int sp_3072_mod_exp_avx2_48(sp_digit* r, const sp_digit* a, const sp_digit* e,
        int bits, const sp_digit* m, int reduceA)
{
#ifndef WOLFSSL_SMALL_STACK
    sp_digit t[32][96];
#else
    sp_digit* t[32];
    sp_digit* td;
#endif
    sp_digit* norm;
    sp_digit mp = 1;
    sp_digit n;
    sp_digit mask;
    int i;
    int c, y;
    int err = MP_OKAY;

#ifdef WOLFSSL_SMALL_STACK
    td = (sp_digit*)XMALLOC(sizeof(sp_digit) * 32 * 96, NULL,
                            DYNAMIC_TYPE_TMP_BUFFER);
    if (td == NULL)
        err = MEMORY_E;

    if (err == MP_OKAY) {
        for (i=0; i<32; i++)
            t[i] = td + i * 96;
        norm = t[0];
    }
#else
    norm = t[0];
#endif

    if (err == MP_OKAY) {
        sp_3072_mont_setup(m, &mp);
        sp_3072_mont_norm_48(norm, m);

        XMEMSET(t[1], 0, sizeof(sp_digit) * 48);
        if (reduceA) {
            err = sp_3072_mod_48(t[1] + 48, a, m);
            if (err == MP_OKAY)
                err = sp_3072_mod_48(t[1], t[1], m);
        }
        else {
            XMEMCPY(t[1] + 48, a, sizeof(sp_digit) * 48);
            err = sp_3072_mod_48(t[1], t[1], m);
        }
    }

    if (err == MP_OKAY) {
        sp_3072_mont_sqr_avx2_48(t[ 2], t[ 1], m, mp);
        sp_3072_mont_mul_avx2_48(t[ 3], t[ 2], t[ 1], m, mp);
        sp_3072_mont_sqr_avx2_48(t[ 4], t[ 2], m, mp);
        sp_3072_mont_mul_avx2_48(t[ 5], t[ 3], t[ 2], m, mp);
        sp_3072_mont_sqr_avx2_48(t[ 6], t[ 3], m, mp);
        sp_3072_mont_mul_avx2_48(t[ 7], t[ 4], t[ 3], m, mp);
        sp_3072_mont_sqr_avx2_48(t[ 8], t[ 4], m, mp);
        sp_3072_mont_mul_avx2_48(t[ 9], t[ 5], t[ 4], m, mp);
        sp_3072_mont_sqr_avx2_48(t[10], t[ 5], m, mp);
        sp_3072_mont_mul_avx2_48(t[11], t[ 6], t[ 5], m, mp);
        sp_3072_mont_sqr_avx2_48(t[12], t[ 6], m, mp);
        sp_3072_mont_mul_avx2_48(t[13], t[ 7], t[ 6], m, mp);
        sp_3072_mont_sqr_avx2_48(t[14], t[ 7], m, mp);
        sp_3072_mont_mul_avx2_48(t[15], t[ 8], t[ 7], m, mp);
        sp_3072_mont_sqr_avx2_48(t[16], t[ 8], m, mp);
        sp_3072_mont_mul_avx2_48(t[17], t[ 9], t[ 8], m, mp);
        sp_3072_mont_sqr_avx2_48(t[18], t[ 9], m, mp);
        sp_3072_mont_mul_avx2_48(t[19], t[10], t[ 9], m, mp);
        sp_3072_mont_sqr_avx2_48(t[20], t[10], m, mp);
        sp_3072_mont_mul_avx2_48(t[21], t[11], t[10], m, mp);
        sp_3072_mont_sqr_avx2_48(t[22], t[11], m, mp);
        sp_3072_mont_mul_avx2_48(t[23], t[12], t[11], m, mp);
        sp_3072_mont_sqr_avx2_48(t[24], t[12], m, mp);
        sp_3072_mont_mul_avx2_48(t[25], t[13], t[12], m, mp);
        sp_3072_mont_sqr_avx2_48(t[26], t[13], m, mp);
        sp_3072_mont_mul_avx2_48(t[27], t[14], t[13], m, mp);
        sp_3072_mont_sqr_avx2_48(t[28], t[14], m, mp);
        sp_3072_mont_mul_avx2_48(t[29], t[15], t[14], m, mp);
        sp_3072_mont_sqr_avx2_48(t[30], t[15], m, mp);
        sp_3072_mont_mul_avx2_48(t[31], t[16], t[15], m, mp);

        i = (bits - 1) / 64;
        n = e[i--];
        c = bits & 63;
        if (c == 0)
            c = 64;
        c -= bits % 5;
        y = (int)(n >> c);
        n <<= 64 - c;
        XMEMCPY(r, t[y], sizeof(sp_digit) * 48);
        for (; i>=0 || c>=5; ) {
            if (c == 0) {
                n = e[i--];
                y = (int)(n >> 59);
                n <<= 5;
                c = 59;
            }
            else if (c < 5) {
                y = (int)(n >> 59);
                n = e[i--];
                c = 5 - c;
                y |= n >> (64 - c);
                n <<= c;
                c = 64 - c;
            }
            else {
                y = (n >> 59) & 0x1f;
                n <<= 5;
                c -= 5;
            }

            sp_3072_mont_sqr_avx2_48(r, r, m, mp);
            sp_3072_mont_sqr_avx2_48(r, r, m, mp);
            sp_3072_mont_sqr_avx2_48(r, r, m, mp);
            sp_3072_mont_sqr_avx2_48(r, r, m, mp);
            sp_3072_mont_sqr_avx2_48(r, r, m, mp);

            sp_3072_mont_mul_avx2_48(r, r, t[y], m, mp);
        }

        XMEMSET(&r[48], 0, sizeof(sp_digit) * 48);
        sp_3072_mont_reduce_avx2_48(r, m, mp);

        mask = 0 - (sp_3072_cmp_48(r, m) >= 0);
        sp_3072_cond_sub_48(r, r, m, mask);
    }

#ifdef WOLFSSL_SMALL_STACK
    if (td != NULL)
        XFREE(td, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return err;
}
#endif /* HAVE_INTEL_AVX2 */
#endif /* (WOLFSSL_HAVE_SP_RSA && !WOLFSSL_RSA_PUBLIC_ONLY) || WOLFSSL_HAVE_SP_DH */

#ifdef WOLFSSL_HAVE_SP_RSA
/* RSA public key operation.
 *
 * in      Array of bytes representing the number to exponentiate, base.
 * inLen   Number of bytes in base.
 * em      Public exponent.
 * mm      Modulus.
 * out     Buffer to hold big-endian bytes of exponentiation result.
 *         Must be at least 384 bytes long.
 * outLen  Number of bytes in result.
 * returns 0 on success, MP_TO_E when the outLen is too small, MP_READ_E when
 * an array is too long and MEMORY_E when dynamic memory allocation fails.
 */
int sp_RsaPublic_3072(const byte* in, word32 inLen, mp_int* em, mp_int* mm,
    byte* out, word32* outLen)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_digit ad[96], md[48], rd[96];
#else
    sp_digit* d = NULL;
#endif
    sp_digit* a;
    sp_digit *ah;
    sp_digit* m;
    sp_digit* r;
    sp_digit e[1];
    int err = MP_OKAY;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    if (*outLen < 384)
        err = MP_TO_E;
    if (err == MP_OKAY && (mp_count_bits(em) > 64 || inLen > 384 ||
                                                     mp_count_bits(mm) != 3072))
        err = MP_READ_E;

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        d = (sp_digit*)XMALLOC(sizeof(sp_digit) * 48 * 5, NULL,
                                                              DYNAMIC_TYPE_RSA);
        if (d == NULL)
            err = MEMORY_E;
    }

    if (err == MP_OKAY) {
        a = d;
        r = a + 48 * 2;
        m = r + 48 * 2;
        ah = a + 48;
    }
#else
    a = ad;
    m = md;
    r = rd;
    ah = a + 48;
#endif

    if (err == MP_OKAY) {
        sp_3072_from_bin(ah, 48, in, inLen);
#if DIGIT_BIT >= 64
        e[0] = em->dp[0];
#else
        e[0] = em->dp[0];
        if (em->used > 1)
            e[0] |= ((sp_digit)em->dp[1]) << DIGIT_BIT;
#endif
        if (e[0] == 0)
            err = MP_EXPTMOD_E;
    }
    if (err == MP_OKAY) {
        sp_3072_from_mp(m, 48, mm);

        if (e[0] == 0x3) {
#ifdef HAVE_INTEL_AVX2
            if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags)) {
                if (err == MP_OKAY) {
                    sp_3072_sqr_avx2_48(r, ah);
                    err = sp_3072_mod_48_cond(r, r, m);
                }
                if (err == MP_OKAY) {
                    sp_3072_mul_avx2_48(r, ah, r);
                    err = sp_3072_mod_48_cond(r, r, m);
                }
            }
            else
#endif
            {
                if (err == MP_OKAY) {
                    sp_3072_sqr_48(r, ah);
                    err = sp_3072_mod_48_cond(r, r, m);
                }
                if (err == MP_OKAY) {
                    sp_3072_mul_48(r, ah, r);
                    err = sp_3072_mod_48_cond(r, r, m);
                }
            }
        }
        else {
            int i;
            sp_digit mp;

            sp_3072_mont_setup(m, &mp);

            /* Convert to Montgomery form. */
            XMEMSET(a, 0, sizeof(sp_digit) * 48);
            err = sp_3072_mod_48_cond(a, a, m);

            if (err == MP_OKAY) {
                for (i=63; i>=0; i--)
                    if (e[0] >> i)
                        break;

                XMEMCPY(r, a, sizeof(sp_digit) * 48);
#ifdef HAVE_INTEL_AVX2
                if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags)) {
                    for (i--; i>=0; i--) {
                        sp_3072_mont_sqr_avx2_48(r, r, m, mp);
                        if (((e[0] >> i) & 1) == 1)
                            sp_3072_mont_mul_avx2_48(r, r, a, m, mp);
                    }
                    XMEMSET(&r[48], 0, sizeof(sp_digit) * 48);
                    sp_3072_mont_reduce_avx2_48(r, m, mp);
                }
                else
#endif
                {
                    for (i--; i>=0; i--) {
                        sp_3072_mont_sqr_48(r, r, m, mp);
                        if (((e[0] >> i) & 1) == 1)
                            sp_3072_mont_mul_48(r, r, a, m, mp);
                    }
                    XMEMSET(&r[48], 0, sizeof(sp_digit) * 48);
                    sp_3072_mont_reduce_48(r, m, mp);
                }

                for (i = 47; i > 0; i--) {
                    if (r[i] != m[i])
                        break;
                }
                if (r[i] >= m[i])
                    sp_3072_sub_in_place_48(r, m);
            }
        }
    }

    if (err == MP_OKAY) {
        sp_3072_to_bin(r, out);
        *outLen = 384;
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (d != NULL)
        XFREE(d, NULL, DYNAMIC_TYPE_RSA);
#endif

    return err;
}

/* RSA private key operation.
 *
 * in      Array of bytes representing the number to exponentiate, base.
 * inLen   Number of bytes in base.
 * dm      Private exponent.
 * pm      First prime.
 * qm      Second prime.
 * dpm     First prime's CRT exponent.
 * dqm     Second prime's CRT exponent.
 * qim     Inverse of second prime mod p.
 * mm      Modulus.
 * out     Buffer to hold big-endian bytes of exponentiation result.
 *         Must be at least 384 bytes long.
 * outLen  Number of bytes in result.
 * returns 0 on success, MP_TO_E when the outLen is too small, MP_READ_E when
 * an array is too long and MEMORY_E when dynamic memory allocation fails.
 */
int sp_RsaPrivate_3072(const byte* in, word32 inLen, mp_int* dm,
    mp_int* pm, mp_int* qm, mp_int* dpm, mp_int* dqm, mp_int* qim, mp_int* mm,
    byte* out, word32* outLen)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_digit ad[48 * 2];
    sp_digit pd[24], qd[24], dpd[24];
    sp_digit tmpad[48], tmpbd[48];
#else
    sp_digit* t = NULL;
#endif
    sp_digit* a;
    sp_digit* p;
    sp_digit* q;
    sp_digit* dp;
    sp_digit* dq;
    sp_digit* qi;
    sp_digit* tmp;
    sp_digit* tmpa;
    sp_digit* tmpb;
    sp_digit* r;
    sp_digit c;
    int err = MP_OKAY;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    (void)dm;
    (void)mm;

    if (*outLen < 384)
        err = MP_TO_E;
    if (err == MP_OKAY && (inLen > 384 || mp_count_bits(mm) != 3072))
        err = MP_READ_E;

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        t = (sp_digit*)XMALLOC(sizeof(sp_digit) * 24 * 11, NULL,
                                                              DYNAMIC_TYPE_RSA);
        if (t == NULL)
            err = MEMORY_E;
    }
    if (err == MP_OKAY) {
        a = t;
        p = a + 48 * 2;
        q = p + 24;
        qi = dq = dp = q + 24;
        tmpa = qi + 24;
        tmpb = tmpa + 48;

        tmp = t;
        r = tmp + 48;
    }
#else
    r = a = ad;
    p = pd;
    q = qd;
    qi = dq = dp = dpd;
    tmpa = tmpad;
    tmpb = tmpbd;
    tmp = a + 48;
#endif

    if (err == MP_OKAY) {
        sp_3072_from_bin(a, 48, in, inLen);
        sp_3072_from_mp(p, 24, pm);
        sp_3072_from_mp(q, 24, qm);
        sp_3072_from_mp(dp, 24, dpm);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_3072_mod_exp_avx2_24(tmpa, a, dp, 1536, p, 1);
        else
#endif
            err = sp_3072_mod_exp_24(tmpa, a, dp, 1536, p, 1);
    }
    if (err == MP_OKAY) {
        sp_3072_from_mp(dq, 24, dqm);
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_3072_mod_exp_avx2_24(tmpb, a, dq, 1536, q, 1);
       else
#endif
            err = sp_3072_mod_exp_24(tmpb, a, dq, 1536, q, 1);
    }

    if (err == MP_OKAY) {
        c = sp_3072_sub_in_place_24(tmpa, tmpb);
        sp_3072_mask_24(tmp, p, c);
        sp_3072_add_24(tmpa, tmpa, tmp);

        sp_3072_from_mp(qi, 24, qim);
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_3072_mul_avx2_24(tmpa, tmpa, qi);
        else
#endif
            sp_3072_mul_24(tmpa, tmpa, qi);
        err = sp_3072_mod_24(tmpa, tmpa, p);
    }

    if (err == MP_OKAY) {
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_3072_mul_avx2_24(tmpa, q, tmpa);
        else
#endif
            sp_3072_mul_24(tmpa, q, tmpa);
        XMEMSET(&tmpb[24], 0, sizeof(sp_digit) * 24);
        sp_3072_add_48(r, tmpb, tmpa);

        sp_3072_to_bin(r, out);
        *outLen = 384;
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (t != NULL) {
        XMEMSET(t, 0, sizeof(sp_digit) * 24 * 11);
        XFREE(t, NULL, DYNAMIC_TYPE_RSA);
    }
#else
    XMEMSET(tmpad, 0, sizeof(tmpad));
    XMEMSET(tmpbd, 0, sizeof(tmpbd));
    XMEMSET(pd, 0, sizeof(pd));
    XMEMSET(qd, 0, sizeof(qd));
    XMEMSET(dpd, 0, sizeof(dpd));
#endif

    return err;
}
#endif /* WOLFSSL_HAVE_SP_RSA */
#if defined(WOLFSSL_HAVE_SP_DH) || (defined(WOLFSSL_HAVE_SP_RSA) && \
                                              !defined(WOLFSSL_RSA_PUBLIC_ONLY))
/* Convert an array of sp_digit to an mp_int.
 *
 * a  A single precision integer.
 * r  A multi-precision integer.
 */
static int sp_3072_to_mp(const sp_digit* a, mp_int* r)
{
    int err;

    err = mp_grow(r, (3072 + DIGIT_BIT - 1) / DIGIT_BIT);
    if (err == MP_OKAY) {
#if DIGIT_BIT == 64
        XMEMCPY(r->dp, a, sizeof(sp_digit) * 48);
        r->used = 48;
        mp_clamp(r);
#elif DIGIT_BIT < 64
        int i, j = 0, s = 0;

        r->dp[0] = 0;
        for (i = 0; i < 48; i++) {
            r->dp[j] |= a[i] << s;
            r->dp[j] &= (1l << DIGIT_BIT) - 1;
            s = DIGIT_BIT - s;
            r->dp[++j] = a[i] >> s;
            while (s + DIGIT_BIT <= 64) {
                s += DIGIT_BIT;
                r->dp[j++] &= (1l << DIGIT_BIT) - 1;
                if (s == SP_WORD_SIZE)
                    r->dp[j] = 0;
                else
                    r->dp[j] = a[i] >> s;
            }
            s = 64 - s;
        }
        r->used = (3072 + DIGIT_BIT - 1) / DIGIT_BIT;
        mp_clamp(r);
#else
        int i, j = 0, s = 0;

        r->dp[0] = 0;
        for (i = 0; i < 48; i++) {
            r->dp[j] |= ((mp_digit)a[i]) << s;
            if (s + 64 >= DIGIT_BIT) {
    #if DIGIT_BIT != 32 && DIGIT_BIT != 64
                r->dp[j] &= (1l << DIGIT_BIT) - 1;
    #endif
                s = DIGIT_BIT - s;
                r->dp[++j] = a[i] >> s;
                s = 64 - s;
            }
            else
                s += 64;
        }
        r->used = (3072 + DIGIT_BIT - 1) / DIGIT_BIT;
        mp_clamp(r);
#endif
    }

    return err;
}

/* Perform the modular exponentiation for Diffie-Hellman.
 *
 * base  Base. MP integer.
 * exp   Exponent. MP integer.
 * mod   Modulus. MP integer.
 * res   Result. MP integer.
 * returs 0 on success, MP_READ_E if there are too many bytes in an array
 * and MEMORY_E if memory allocation fails.
 */
int sp_ModExp_3072(mp_int* base, mp_int* exp, mp_int* mod, mp_int* res)
{
    int err = MP_OKAY;
    sp_digit b[96], e[48], m[48];
    sp_digit* r = b;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif
    int expBits = mp_count_bits(exp);

    if (mp_count_bits(base) > 3072 || expBits > 3072 ||
                                                   mp_count_bits(mod) != 3072) {
        err = MP_READ_E;
    }

    if (err == MP_OKAY) {
        sp_3072_from_mp(b, 48, base);
        sp_3072_from_mp(e, 48, exp);
        sp_3072_from_mp(m, 48, mod);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_3072_mod_exp_avx2_48(r, b, e, expBits, m, 0);
        else
#endif
            err = sp_3072_mod_exp_48(r, b, e, expBits, m, 0);
    }

    if (err == MP_OKAY) {
        err = sp_3072_to_mp(r, res);
    }

    XMEMSET(e, 0, sizeof(e));

    return err;
}

#ifdef WOLFSSL_HAVE_SP_DH
#ifdef HAVE_FFDHE_3072
extern void sp_3072_lshift_48(sp_digit* r, const sp_digit* a, int n);
#ifdef HAVE_INTEL_AVX2
/* Modular exponentiate 2 to the e mod m. (r = 2^e mod m)
 *
 * r     A single precision number that is the result of the operation.
 * e     A single precision number that is the exponent.
 * bits  The number of bits in the exponent.
 * m     A single precision number that is the modulus.
 * returns 0 on success and MEMORY_E on dynamic memory allocation failure.
 */
static int sp_3072_mod_exp_2_avx2_48(sp_digit* r, const sp_digit* e, int bits,
        const sp_digit* m)
{
#ifndef WOLFSSL_SMALL_STACK
    sp_digit nd[96];
    sp_digit td[49];
#else
    sp_digit* td;
#endif
    sp_digit* norm;
    sp_digit* tmp;
    sp_digit mp = 1;
    sp_digit n, o;
    sp_digit mask;
    int i;
    int c, y;
    int err = MP_OKAY;

#ifdef WOLFSSL_SMALL_STACK
    td = (sp_digit*)XMALLOC(sizeof(sp_digit) * 145, NULL,
                            DYNAMIC_TYPE_TMP_BUFFER);
    if (td == NULL)
        err = MEMORY_E;

    if (err == MP_OKAY) {
        norm = td;
        tmp  = td + 96;
    }
#else
    norm = nd;
    tmp  = td;
#endif

    if (err == MP_OKAY) {
        sp_3072_mont_setup(m, &mp);
        sp_3072_mont_norm_48(norm, m);

        i = (bits - 1) / 64;
        n = e[i--];
        c = bits & 63;
        if (c == 0)
            c = 64;
        c -= bits % 6;
        y = (int)(n >> c);
        n <<= 64 - c;
        sp_3072_lshift_48(r, norm, y);
        for (; i>=0 || c>=6; ) {
            if (c == 0) {
                n = e[i--];
                y = (int)(n >> 58);
                n <<= 6;
                c = 58;
            }
            else if (c < 6) {
                y = (int)(n >> 58);
                n = e[i--];
                c = 6 - c;
                y |= n >> (64 - c);
                n <<= c;
                c = 64 - c;
            }
            else {
                y = (n >> 58) & 0x3f;
                n <<= 6;
                c -= 6;
            }

            sp_3072_mont_sqr_avx2_48(r, r, m, mp);
            sp_3072_mont_sqr_avx2_48(r, r, m, mp);
            sp_3072_mont_sqr_avx2_48(r, r, m, mp);
            sp_3072_mont_sqr_avx2_48(r, r, m, mp);
            sp_3072_mont_sqr_avx2_48(r, r, m, mp);
            sp_3072_mont_sqr_avx2_48(r, r, m, mp);

            sp_3072_lshift_48(r, r, y);
            sp_3072_mul_d_avx2_48(tmp, norm, r[48]);
            r[48] = 0;
            o = sp_3072_add_48(r, r, tmp);
            sp_3072_cond_sub_48(r, r, m, (sp_digit)0 - o);
        }

        XMEMSET(&r[48], 0, sizeof(sp_digit) * 48);
        sp_3072_mont_reduce_avx2_48(r, m, mp);

        mask = 0 - (sp_3072_cmp_48(r, m) >= 0);
        sp_3072_cond_sub_48(r, r, m, mask);
    }

#ifdef WOLFSSL_SMALL_STACK
    if (td != NULL)
        XFREE(td, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return err;
}
#endif /* HAVE_INTEL_AVX2 */

/* Modular exponentiate 2 to the e mod m. (r = 2^e mod m)
 *
 * r     A single precision number that is the result of the operation.
 * e     A single precision number that is the exponent.
 * bits  The number of bits in the exponent.
 * m     A single precision number that is the modulus.
 * returns 0 on success and MEMORY_E on dynamic memory allocation failure.
 */
static int sp_3072_mod_exp_2_48(sp_digit* r, const sp_digit* e, int bits,
        const sp_digit* m)
{
#ifndef WOLFSSL_SMALL_STACK
    sp_digit nd[96];
    sp_digit td[49];
#else
    sp_digit* td;
#endif
    sp_digit* norm;
    sp_digit* tmp;
    sp_digit mp = 1;
    sp_digit n, o;
    sp_digit mask;
    int i;
    int c, y;
    int err = MP_OKAY;

#ifdef WOLFSSL_SMALL_STACK
    td = (sp_digit*)XMALLOC(sizeof(sp_digit) * 145, NULL,
                            DYNAMIC_TYPE_TMP_BUFFER);
    if (td == NULL)
        err = MEMORY_E;

    if (err == MP_OKAY) {
        norm = td;
        tmp  = td + 96;
    }
#else
    norm = nd;
    tmp  = td;
#endif

    if (err == MP_OKAY) {
        sp_3072_mont_setup(m, &mp);
        sp_3072_mont_norm_48(norm, m);

        i = (bits - 1) / 64;
        n = e[i--];
        c = bits & 63;
        if (c == 0)
            c = 64;
        c -= bits % 6;
        y = (int)(n >> c);
        n <<= 64 - c;
        sp_3072_lshift_48(r, norm, y);
        for (; i>=0 || c>=6; ) {
            if (c == 0) {
                n = e[i--];
                y = (int)(n >> 58);
                n <<= 6;
                c = 58;
            }
            else if (c < 6) {
                y = (int)(n >> 58);
                n = e[i--];
                c = 6 - c;
                y |= n >> (64 - c);
                n <<= c;
                c = 64 - c;
            }
            else {
                y = (n >> 58) & 0x3f;
                n <<= 6;
                c -= 6;
            }

            sp_3072_mont_sqr_48(r, r, m, mp);
            sp_3072_mont_sqr_48(r, r, m, mp);
            sp_3072_mont_sqr_48(r, r, m, mp);
            sp_3072_mont_sqr_48(r, r, m, mp);
            sp_3072_mont_sqr_48(r, r, m, mp);
            sp_3072_mont_sqr_48(r, r, m, mp);

            sp_3072_lshift_48(r, r, y);
            sp_3072_mul_d_48(tmp, norm, r[48]);
            r[48] = 0;
            o = sp_3072_add_48(r, r, tmp);
            sp_3072_cond_sub_48(r, r, m, (sp_digit)0 - o);
        }

        XMEMSET(&r[48], 0, sizeof(sp_digit) * 48);
        sp_3072_mont_reduce_48(r, m, mp);

        mask = 0 - (sp_3072_cmp_48(r, m) >= 0);
        sp_3072_cond_sub_48(r, r, m, mask);
    }

#ifdef WOLFSSL_SMALL_STACK
    if (td != NULL)
        XFREE(td, NULL, DYNAMIC_TYPE_TMP_BUFFER);
#endif

    return err;
}

#endif /* HAVE_FFDHE_3072 */

/* Perform the modular exponentiation for Diffie-Hellman.
 *
 * base     Base.
 * exp      Array of bytes that is the exponent.
 * expLen   Length of data, in bytes, in exponent.
 * mod      Modulus.
 * out      Buffer to hold big-endian bytes of exponentiation result.
 *          Must be at least 384 bytes long.
 * outLen   Length, in bytes, of exponentiation result.
 * returs 0 on success, MP_READ_E if there are too many bytes in an array
 * and MEMORY_E if memory allocation fails.
 */
int sp_DhExp_3072(mp_int* base, const byte* exp, word32 expLen,
    mp_int* mod, byte* out, word32* outLen)
{
    int err = MP_OKAY;
    sp_digit b[96], e[48], m[48];
    sp_digit* r = b;
    word32 i;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    if (mp_count_bits(base) > 3072 || expLen > 384 ||
                                                   mp_count_bits(mod) != 3072) {
        err = MP_READ_E;
    }

    if (err == MP_OKAY) {
        sp_3072_from_mp(b, 48, base);
        sp_3072_from_bin(e, 48, exp, expLen);
        sp_3072_from_mp(m, 48, mod);

    #ifdef HAVE_FFDHE_3072
        if (base->used == 1 && base->dp[0] == 2 && m[47] == (sp_digit)-1) {
#ifdef HAVE_INTEL_AVX2
            if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
                err = sp_3072_mod_exp_2_avx2_48(r, e, expLen * 8, m);
            else
#endif
                err = sp_3072_mod_exp_2_48(r, e, expLen * 8, m);
        }
        else
    #endif
        {
#ifdef HAVE_INTEL_AVX2
            if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
                err = sp_3072_mod_exp_avx2_48(r, b, e, expLen * 8, m, 0);
            else
#endif
                err = sp_3072_mod_exp_48(r, b, e, expLen * 8, m, 0);
        }
    }

    if (err == MP_OKAY) {
        sp_3072_to_bin(r, out);
        *outLen = 384;
        for (i=0; i<384 && out[i] == 0; i++) {
        }
        *outLen -= i;
        XMEMMOVE(out, out + i, *outLen);
    }

    XMEMSET(e, 0, sizeof(e));

    return err;
}
#endif
/* Perform the modular exponentiation for Diffie-Hellman.
 *
 * base  Base. MP integer.
 * exp   Exponent. MP integer.
 * mod   Modulus. MP integer.
 * res   Result. MP integer.
 * returs 0 on success, MP_READ_E if there are too many bytes in an array
 * and MEMORY_E if memory allocation fails.
 */
int sp_ModExp_1536(mp_int* base, mp_int* exp, mp_int* mod, mp_int* res)
{
    int err = MP_OKAY;
    sp_digit b[48], e[24], m[24];
    sp_digit* r = b;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif
    int expBits = mp_count_bits(exp);

    if (mp_count_bits(base) > 1536 || expBits > 1536 ||
                                                   mp_count_bits(mod) != 1536) {
        err = MP_READ_E;
    }

    if (err == MP_OKAY) {
        sp_3072_from_mp(b, 24, base);
        sp_3072_from_mp(e, 24, exp);
        sp_3072_from_mp(m, 24, mod);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_3072_mod_exp_avx2_24(r, b, e, expBits, m, 0);
        else
#endif
            err = sp_3072_mod_exp_24(r, b, e, expBits, m, 0);
    }

    if (err == MP_OKAY) {
        XMEMSET(r + 24, 0, sizeof(*r) * 24);
        err = sp_3072_to_mp(r, res);
    }

    XMEMSET(e, 0, sizeof(e));

    return err;
}

#endif /* WOLFSSL_HAVE_SP_DH || (WOLFSSL_HAVE_SP_RSA && !WOLFSSL_RSA_PUBLIC_ONLY) */

#endif /* !WOLFSSL_SP_NO_3072 */

#endif /* WOLFSSL_HAVE_SP_RSA || WOLFSSL_HAVE_SP_DH */
#ifdef WOLFSSL_HAVE_SP_ECC
#ifndef WOLFSSL_SP_NO_256

/* Point structure to use. */
typedef struct sp_point {
    sp_digit x[2 * 4];
    sp_digit y[2 * 4];
    sp_digit z[2 * 4];
    int infinity;
} sp_point;

/* The modulus (prime) of the curve P256. */
static const sp_digit p256_mod[4] = {
    0xffffffffffffffffl,0x00000000ffffffffl,0x0000000000000000l,
    0xffffffff00000001l
};
/* The Montogmery normalizer for modulus of the curve P256. */
static const sp_digit p256_norm_mod[4] = {
    0x0000000000000001l,0xffffffff00000000l,0xffffffffffffffffl,
    0x00000000fffffffel
};
/* The Montogmery multiplier for modulus of the curve P256. */
static const sp_digit p256_mp_mod = 0x0000000000000001;
#if defined(WOLFSSL_VALIDATE_ECC_KEYGEN) || defined(HAVE_ECC_SIGN) || \
                                            defined(HAVE_ECC_VERIFY)
/* The order of the curve P256. */
static const sp_digit p256_order[4] = {
    0xf3b9cac2fc632551l,0xbce6faada7179e84l,0xffffffffffffffffl,
    0xffffffff00000000l
};
#endif
/* The order of the curve P256 minus 2. */
static const sp_digit p256_order2[4] = {
    0xf3b9cac2fc63254fl,0xbce6faada7179e84l,0xffffffffffffffffl,
    0xffffffff00000000l
};
#if defined(HAVE_ECC_SIGN) || defined(HAVE_ECC_VERIFY)
/* The Montogmery normalizer for order of the curve P256. */
static const sp_digit p256_norm_order[4] = {
    0x0c46353d039cdaafl,0x4319055258e8617bl,0x0000000000000000l,
    0x00000000ffffffffl
};
#endif
#if defined(HAVE_ECC_SIGN) || defined(HAVE_ECC_VERIFY)
/* The Montogmery multiplier for order of the curve P256. */
static const sp_digit p256_mp_order = 0xccd1c8aaee00bc4fl;
#endif
#ifdef WOLFSSL_SP_SMALL
/* The base point of curve P256. */
static const sp_point p256_base = {
    /* X ordinate */
    {
        0xf4a13945d898c296l,0x77037d812deb33a0l,0xf8bce6e563a440f2l,
        0x6b17d1f2e12c4247l
    },
    /* Y ordinate */
    {
        0xcbb6406837bf51f5l,0x2bce33576b315ecel,0x8ee7eb4a7c0f9e16l,
        0x4fe342e2fe1a7f9bl
    },
    /* Z ordinate */
    {
        0x0000000000000001l,0x0000000000000000l,0x0000000000000000l,
        0x0000000000000000l
    },
    /* infinity */
    0
};
#endif /* WOLFSSL_SP_SMALL */
#if defined(HAVE_ECC_CHECK_KEY) || defined(HAVE_COMP_KEY)
static const sp_digit p256_b[4] = {
    0x3bce3c3e27d2604bl,0x651d06b0cc53b0f6l,0xb3ebbd55769886bcl,
    0x5ac635d8aa3a93e7l
};
#endif

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
/* Allocate memory for point and return error. */
#define sp_ecc_point_new(heap, sp, p)                                         \
    ((p = (sp_point*)XMALLOC(sizeof(sp_point), heap, DYNAMIC_TYPE_ECC))       \
                                                 == NULL) ? MEMORY_E : MP_OKAY
#else
/* Set pointer to data and return no error. */
#define sp_ecc_point_new(heap, sp, p)   ((p = &sp) == NULL) ? MEMORY_E : MP_OKAY
#endif

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
/* If valid pointer then clear point data if requested and free data. */
#define sp_ecc_point_free(p, clear, heap)     \
    do {                                      \
        if (p != NULL) {                      \
            if (clear)                        \
                XMEMSET(p, 0, sizeof(*p));    \
            XFREE(p, heap, DYNAMIC_TYPE_ECC); \
        }                                     \
    }                                         \
    while (0)
#else
/* Clear point data if requested. */
#define sp_ecc_point_free(p, clear, heap) \
    do {                                  \
        if (clear)                        \
            XMEMSET(p, 0, sizeof(*p));    \
    }                                     \
    while (0)
#endif

/* Multiply a number by Montogmery normalizer mod modulus (prime).
 *
 * r  The resulting Montgomery form number.
 * a  The number to convert.
 * m  The modulus (prime).
 */
static int sp_256_mod_mul_norm_4(sp_digit* r, const sp_digit* a, const sp_digit* m)
{
    int64_t t[8];
    int64_t a32[8];
    int64_t o;

    (void)m;

    a32[0] = a[0] & 0xffffffff;
    a32[1] = a[0] >> 32;
    a32[2] = a[1] & 0xffffffff;
    a32[3] = a[1] >> 32;
    a32[4] = a[2] & 0xffffffff;
    a32[5] = a[2] >> 32;
    a32[6] = a[3] & 0xffffffff;
    a32[7] = a[3] >> 32;

    /*  1  1  0 -1 -1 -1 -1  0 */
    t[0] = 0 + a32[0] + a32[1] - a32[3] - a32[4] - a32[5] - a32[6];
    /*  0  1  1  0 -1 -1 -1 -1 */
    t[1] = 0 + a32[1] + a32[2] - a32[4] - a32[5] - a32[6] - a32[7];
    /*  0  0  1  1  0 -1 -1 -1 */
    t[2] = 0 + a32[2] + a32[3] - a32[5] - a32[6] - a32[7];
    /* -1 -1  0  2  2  1  0 -1 */
    t[3] = 0 - a32[0] - a32[1] + 2 * a32[3] + 2 * a32[4] + a32[5] - a32[7];
    /*  0 -1 -1  0  2  2  1  0 */
    t[4] = 0 - a32[1] - a32[2] + 2 * a32[4] + 2 * a32[5] + a32[6];
    /*  0  0 -1 -1  0  2  2  1 */
    t[5] = 0 - a32[2] - a32[3] + 2 * a32[5] + 2 * a32[6] + a32[7];
    /* -1 -1  0  0  0  1  3  2 */
    t[6] = 0 - a32[0] - a32[1] + a32[5] + 3 * a32[6] + 2 * a32[7];
    /*  1  0 -1 -1 -1 -1  0  3 */
    t[7] = 0 + a32[0] - a32[2] - a32[3] - a32[4] - a32[5] + 3 * a32[7];

    t[1] += t[0] >> 32; t[0] &= 0xffffffff;
    t[2] += t[1] >> 32; t[1] &= 0xffffffff;
    t[3] += t[2] >> 32; t[2] &= 0xffffffff;
    t[4] += t[3] >> 32; t[3] &= 0xffffffff;
    t[5] += t[4] >> 32; t[4] &= 0xffffffff;
    t[6] += t[5] >> 32; t[5] &= 0xffffffff;
    t[7] += t[6] >> 32; t[6] &= 0xffffffff;
    o     = t[7] >> 32; t[7] &= 0xffffffff;
    t[0] += o;
    t[3] -= o;
    t[6] -= o;
    t[7] += o;
    t[1] += t[0] >> 32; t[0] &= 0xffffffff;
    t[2] += t[1] >> 32; t[1] &= 0xffffffff;
    t[3] += t[2] >> 32; t[2] &= 0xffffffff;
    t[4] += t[3] >> 32; t[3] &= 0xffffffff;
    t[5] += t[4] >> 32; t[4] &= 0xffffffff;
    t[6] += t[5] >> 32; t[5] &= 0xffffffff;
    t[7] += t[6] >> 32; t[6] &= 0xffffffff;
    r[0] = (t[1] << 32) | t[0];
    r[1] = (t[3] << 32) | t[2];
    r[2] = (t[5] << 32) | t[4];
    r[3] = (t[7] << 32) | t[6];

    return MP_OKAY;
}

/* Convert an mp_int to an array of sp_digit.
 *
 * r  A single precision integer.
 * a  A multi-precision integer.
 */
static void sp_256_from_mp(sp_digit* r, int max, const mp_int* a)
{
#if DIGIT_BIT == 64
    int j;

    XMEMCPY(r, a->dp, sizeof(sp_digit) * a->used);

    for (j = a->used; j < max; j++)
        r[j] = 0;
#elif DIGIT_BIT > 64
    int i, j = 0, s = 0;

    r[0] = 0;
    for (i = 0; i < a->used && j < max; i++) {
        r[j] |= a->dp[i] << s;
        r[j] &= 0xffffffffffffffffl;
        s = 64 - s;
        if (j + 1 >= max)
            break;
        r[++j] = (sp_digit)(a->dp[i] >> s);
        while (s + 64 <= DIGIT_BIT) {
            s += 64;
            r[j] &= 0xffffffffffffffffl;
            if (j + 1 >= max)
                break;
            if (s < DIGIT_BIT)
                r[++j] = (sp_digit)(a->dp[i] >> s);
            else
                r[++j] = 0;
        }
        s = DIGIT_BIT - s;
    }

    for (j++; j < max; j++)
        r[j] = 0;
#else
    int i, j = 0, s = 0;

    r[0] = 0;
    for (i = 0; i < a->used && j < max; i++) {
        r[j] |= ((sp_digit)a->dp[i]) << s;
        if (s + DIGIT_BIT >= 64) {
            r[j] &= 0xffffffffffffffffl;
            if (j + 1 >= max)
                break;
            s = 64 - s;
            if (s == DIGIT_BIT) {
                r[++j] = 0;
                s = 0;
            }
            else {
                r[++j] = a->dp[i] >> s;
                s = DIGIT_BIT - s;
            }
        }
        else
            s += DIGIT_BIT;
    }

    for (j++; j < max; j++)
        r[j] = 0;
#endif
}

/* Convert a point of type ecc_point to type sp_point.
 *
 * p   Point of type sp_point (result).
 * pm  Point of type ecc_point.
 */
static void sp_256_point_from_ecc_point_4(sp_point* p, const ecc_point* pm)
{
    XMEMSET(p->x, 0, sizeof(p->x));
    XMEMSET(p->y, 0, sizeof(p->y));
    XMEMSET(p->z, 0, sizeof(p->z));
    sp_256_from_mp(p->x, 4, pm->x);
    sp_256_from_mp(p->y, 4, pm->y);
    sp_256_from_mp(p->z, 4, pm->z);
    p->infinity = 0;
}

/* Convert an array of sp_digit to an mp_int.
 *
 * a  A single precision integer.
 * r  A multi-precision integer.
 */
static int sp_256_to_mp(const sp_digit* a, mp_int* r)
{
    int err;

    err = mp_grow(r, (256 + DIGIT_BIT - 1) / DIGIT_BIT);
    if (err == MP_OKAY) {
#if DIGIT_BIT == 64
        XMEMCPY(r->dp, a, sizeof(sp_digit) * 4);
        r->used = 4;
        mp_clamp(r);
#elif DIGIT_BIT < 64
        int i, j = 0, s = 0;

        r->dp[0] = 0;
        for (i = 0; i < 4; i++) {
            r->dp[j] |= a[i] << s;
            r->dp[j] &= (1l << DIGIT_BIT) - 1;
            s = DIGIT_BIT - s;
            r->dp[++j] = a[i] >> s;
            while (s + DIGIT_BIT <= 64) {
                s += DIGIT_BIT;
                r->dp[j++] &= (1l << DIGIT_BIT) - 1;
                if (s == SP_WORD_SIZE)
                    r->dp[j] = 0;
                else
                    r->dp[j] = a[i] >> s;
            }
            s = 64 - s;
        }
        r->used = (256 + DIGIT_BIT - 1) / DIGIT_BIT;
        mp_clamp(r);
#else
        int i, j = 0, s = 0;

        r->dp[0] = 0;
        for (i = 0; i < 4; i++) {
            r->dp[j] |= ((mp_digit)a[i]) << s;
            if (s + 64 >= DIGIT_BIT) {
    #if DIGIT_BIT != 32 && DIGIT_BIT != 64
                r->dp[j] &= (1l << DIGIT_BIT) - 1;
    #endif
                s = DIGIT_BIT - s;
                r->dp[++j] = a[i] >> s;
                s = 64 - s;
            }
            else
                s += 64;
        }
        r->used = (256 + DIGIT_BIT - 1) / DIGIT_BIT;
        mp_clamp(r);
#endif
    }

    return err;
}

/* Convert a point of type sp_point to type ecc_point.
 *
 * p   Point of type sp_point.
 * pm  Point of type ecc_point (result).
 * returns MEMORY_E when allocation of memory in ecc_point fails otherwise
 * MP_OKAY.
 */
static int sp_256_point_to_ecc_point_4(const sp_point* p, ecc_point* pm)
{
    int err;

    err = sp_256_to_mp(p->x, pm->x);
    if (err == MP_OKAY)
        err = sp_256_to_mp(p->y, pm->y);
    if (err == MP_OKAY)
        err = sp_256_to_mp(p->z, pm->z);

    return err;
}

extern void sp_256_cond_copy_4(sp_digit* r, const sp_digit* a, sp_digit m);
extern int64_t sp_256_cmp_4(const sp_digit* a, const sp_digit* b);
/* Normalize the values in each word to 64.
 *
 * a  Array of sp_digit to normalize.
 */
#define sp_256_norm_4(a)

extern sp_digit sp_256_cond_sub_4(sp_digit* r, const sp_digit* a, const sp_digit* b, sp_digit m);
extern sp_digit sp_256_sub_4(sp_digit* r, const sp_digit* a, const sp_digit* b);
#define sp_256_mont_reduce_order_4         sp_256_mont_reduce_4

extern void sp_256_mont_reduce_4(sp_digit* a, const sp_digit* m, sp_digit mp);
extern void sp_256_mont_mul_4(sp_digit* r, const sp_digit* a, const sp_digit* b, const sp_digit* m, sp_digit mp);
extern void sp_256_mont_sqr_4(sp_digit* r, const sp_digit* a, const sp_digit* m, sp_digit mp);
#if !defined(WOLFSSL_SP_SMALL) || defined(HAVE_COMP_KEY)
/* Square the Montgomery form number a number of times. (r = a ^ n mod m)
 *
 * r   Result of squaring.
 * a   Number to square in Montogmery form.
 * n   Number of times to square.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_256_mont_sqr_n_4(sp_digit* r, const sp_digit* a, int n,
        const sp_digit* m, sp_digit mp)
{
    sp_256_mont_sqr_4(r, a, m, mp);
    for (; n > 1; n--)
        sp_256_mont_sqr_4(r, r, m, mp);
}

#endif /* !WOLFSSL_SP_SMALL || HAVE_COMP_KEY */
#ifdef WOLFSSL_SP_SMALL
/* Mod-2 for the P256 curve. */
static const uint64_t p256_mod_2[4] = {
    0xfffffffffffffffd,0x00000000ffffffff,0x0000000000000000,
    0xffffffff00000001
};
#endif /* !WOLFSSL_SP_SMALL */

/* Invert the number, in Montgomery form, modulo the modulus (prime) of the
 * P256 curve. (r = 1 / a mod m)
 *
 * r   Inverse result.
 * a   Number to invert.
 * td  Temporary data.
 */
static void sp_256_mont_inv_4(sp_digit* r, const sp_digit* a, sp_digit* td)
{
#ifdef WOLFSSL_SP_SMALL
    sp_digit* t = td;
    int i;

    XMEMCPY(t, a, sizeof(sp_digit) * 4);
    for (i=254; i>=0; i--) {
        sp_256_mont_sqr_4(t, t, p256_mod, p256_mp_mod);
        if (p256_mod_2[i / 64] & ((sp_digit)1 << (i % 64)))
            sp_256_mont_mul_4(t, t, a, p256_mod, p256_mp_mod);
    }
    XMEMCPY(r, t, sizeof(sp_digit) * 4);
#else
    sp_digit* t = td;
    sp_digit* t2 = td + 2 * 4;
    sp_digit* t3 = td + 4 * 4;

    /* t = a^2 */
    sp_256_mont_sqr_4(t, a, p256_mod, p256_mp_mod);
    /* t = a^3 = t * a */
    sp_256_mont_mul_4(t, t, a, p256_mod, p256_mp_mod);
    /* t2= a^c = t ^ 2 ^ 2 */
    sp_256_mont_sqr_n_4(t2, t, 2, p256_mod, p256_mp_mod);
    /* t3= a^d = t2 * a */
    sp_256_mont_mul_4(t3, t2, a, p256_mod, p256_mp_mod);
    /* t = a^f = t2 * t */
    sp_256_mont_mul_4(t, t2, t, p256_mod, p256_mp_mod);
    /* t2= a^f0 = t ^ 2 ^ 4 */
    sp_256_mont_sqr_n_4(t2, t, 4, p256_mod, p256_mp_mod);
    /* t3= a^fd = t2 * t3 */
    sp_256_mont_mul_4(t3, t2, t3, p256_mod, p256_mp_mod);
    /* t = a^ff = t2 * t */
    sp_256_mont_mul_4(t, t2, t, p256_mod, p256_mp_mod);
    /* t2= a^ff00 = t ^ 2 ^ 8 */
    sp_256_mont_sqr_n_4(t2, t, 8, p256_mod, p256_mp_mod);
    /* t3= a^fffd = t2 * t3 */
    sp_256_mont_mul_4(t3, t2, t3, p256_mod, p256_mp_mod);
    /* t = a^ffff = t2 * t */
    sp_256_mont_mul_4(t, t2, t, p256_mod, p256_mp_mod);
    /* t2= a^ffff0000 = t ^ 2 ^ 16 */
    sp_256_mont_sqr_n_4(t2, t, 16, p256_mod, p256_mp_mod);
    /* t3= a^fffffffd = t2 * t3 */
    sp_256_mont_mul_4(t3, t2, t3, p256_mod, p256_mp_mod);
    /* t = a^ffffffff = t2 * t */
    sp_256_mont_mul_4(t, t2, t, p256_mod, p256_mp_mod);
    /* t = a^ffffffff00000000 = t ^ 2 ^ 32  */
    sp_256_mont_sqr_n_4(t2, t, 32, p256_mod, p256_mp_mod);
    /* t2= a^ffffffffffffffff = t2 * t */
    sp_256_mont_mul_4(t, t2, t, p256_mod, p256_mp_mod);
    /* t2= a^ffffffff00000001 = t2 * a */
    sp_256_mont_mul_4(t2, t2, a, p256_mod, p256_mp_mod);
    /* t2= a^ffffffff000000010000000000000000000000000000000000000000
     *   = t2 ^ 2 ^ 160 */
    sp_256_mont_sqr_n_4(t2, t2, 160, p256_mod, p256_mp_mod);
    /* t2= a^ffffffff00000001000000000000000000000000ffffffffffffffff
     *   = t2 * t */
    sp_256_mont_mul_4(t2, t2, t, p256_mod, p256_mp_mod);
    /* t2= a^ffffffff00000001000000000000000000000000ffffffffffffffff00000000
     *   = t2 ^ 2 ^ 32 */
    sp_256_mont_sqr_n_4(t2, t2, 32, p256_mod, p256_mp_mod);
    /* r = a^ffffffff00000001000000000000000000000000fffffffffffffffffffffffd
     *   = t2 * t3 */
    sp_256_mont_mul_4(r, t2, t3, p256_mod, p256_mp_mod);
#endif /* WOLFSSL_SP_SMALL */
}

/* Map the Montgomery form projective co-ordinate point to an affine point.
 *
 * r  Resulting affine co-ordinate point.
 * p  Montgomery form projective co-ordinate point.
 * t  Temporary ordinate data.
 */
static void sp_256_map_4(sp_point* r, const sp_point* p, sp_digit* t)
{
    sp_digit* t1 = t;
    sp_digit* t2 = t + 2*4;
    int64_t n;

    sp_256_mont_inv_4(t1, p->z, t + 2*4);

    sp_256_mont_sqr_4(t2, t1, p256_mod, p256_mp_mod);
    sp_256_mont_mul_4(t1, t2, t1, p256_mod, p256_mp_mod);

    /* x /= z^2 */
    sp_256_mont_mul_4(r->x, p->x, t2, p256_mod, p256_mp_mod);
    XMEMSET(r->x + 4, 0, sizeof(r->x) / 2);
    sp_256_mont_reduce_4(r->x, p256_mod, p256_mp_mod);
    /* Reduce x to less than modulus */
    n = sp_256_cmp_4(r->x, p256_mod);
    sp_256_cond_sub_4(r->x, r->x, p256_mod, 0 - (n >= 0));
    sp_256_norm_4(r->x);

    /* y /= z^3 */
    sp_256_mont_mul_4(r->y, p->y, t1, p256_mod, p256_mp_mod);
    XMEMSET(r->y + 4, 0, sizeof(r->y) / 2);
    sp_256_mont_reduce_4(r->y, p256_mod, p256_mp_mod);
    /* Reduce y to less than modulus */
    n = sp_256_cmp_4(r->y, p256_mod);
    sp_256_cond_sub_4(r->y, r->y, p256_mod, 0 - (n >= 0));
    sp_256_norm_4(r->y);

    XMEMSET(r->z, 0, sizeof(r->z));
    r->z[0] = 1;

}

extern void sp_256_mont_add_4(sp_digit* r, const sp_digit* a, const sp_digit* b, const sp_digit* m);
extern void sp_256_mont_dbl_4(const sp_digit* r, const sp_digit* a, const sp_digit* m);
extern void sp_256_mont_tpl_4(sp_digit* r, const sp_digit* a, const sp_digit* m);
extern void sp_256_mont_sub_4(sp_digit* r, const sp_digit* a, const sp_digit* b, const sp_digit* m);
extern void sp_256_div2_4(sp_digit* r, const sp_digit* a, const sp_digit* m);
/* Double the Montgomery form projective point p.
 *
 * r  Result of doubling point.
 * p  Point to double.
 * t  Temporary ordinate data.
 */
static void sp_256_proj_point_dbl_4(sp_point* r, const sp_point* p, sp_digit* t)
{
    sp_point* rp[2];
    sp_digit* t1 = t;
    sp_digit* t2 = t + 2*4;
    sp_digit* x;
    sp_digit* y;
    sp_digit* z;
    int i;

    /* When infinity don't double point passed in - constant time. */
    rp[0] = r;
    rp[1] = (sp_point*)t;
    XMEMSET(rp[1], 0, sizeof(sp_point));
    x = rp[p->infinity]->x;
    y = rp[p->infinity]->y;
    z = rp[p->infinity]->z;
    /* Put point to double into result - good for infinty. */
    if (r != p) {
        for (i=0; i<4; i++)
            r->x[i] = p->x[i];
        for (i=0; i<4; i++)
            r->y[i] = p->y[i];
        for (i=0; i<4; i++)
            r->z[i] = p->z[i];
        r->infinity = p->infinity;
    }

    /* T1 = Z * Z */
    sp_256_mont_sqr_4(t1, z, p256_mod, p256_mp_mod);
    /* Z = Y * Z */
    sp_256_mont_mul_4(z, y, z, p256_mod, p256_mp_mod);
    /* Z = 2Z */
    sp_256_mont_dbl_4(z, z, p256_mod);
    /* T2 = X - T1 */
    sp_256_mont_sub_4(t2, x, t1, p256_mod);
    /* T1 = X + T1 */
    sp_256_mont_add_4(t1, x, t1, p256_mod);
    /* T2 = T1 * T2 */
    sp_256_mont_mul_4(t2, t1, t2, p256_mod, p256_mp_mod);
    /* T1 = 3T2 */
    sp_256_mont_tpl_4(t1, t2, p256_mod);
    /* Y = 2Y */
    sp_256_mont_dbl_4(y, y, p256_mod);
    /* Y = Y * Y */
    sp_256_mont_sqr_4(y, y, p256_mod, p256_mp_mod);
    /* T2 = Y * Y */
    sp_256_mont_sqr_4(t2, y, p256_mod, p256_mp_mod);
    /* T2 = T2/2 */
    sp_256_div2_4(t2, t2, p256_mod);
    /* Y = Y * X */
    sp_256_mont_mul_4(y, y, x, p256_mod, p256_mp_mod);
    /* X = T1 * T1 */
    sp_256_mont_mul_4(x, t1, t1, p256_mod, p256_mp_mod);
    /* X = X - Y */
    sp_256_mont_sub_4(x, x, y, p256_mod);
    /* X = X - Y */
    sp_256_mont_sub_4(x, x, y, p256_mod);
    /* Y = Y - X */
    sp_256_mont_sub_4(y, y, x, p256_mod);
    /* Y = Y * T1 */
    sp_256_mont_mul_4(y, y, t1, p256_mod, p256_mp_mod);
    /* Y = Y - T2 */
    sp_256_mont_sub_4(y, y, t2, p256_mod);

}

/* Double the Montgomery form projective point p a number of times.
 *
 * r  Result of repeated doubling of point.
 * p  Point to double.
 * n  Number of times to double
 * t  Temporary ordinate data.
 */
static void sp_256_proj_point_dbl_n_4(sp_point* r, const sp_point* p, int n,
        sp_digit* t)
{
    sp_point* rp[2];
    sp_digit* w = t;
    sp_digit* a = t + 2*4;
    sp_digit* b = t + 4*4;
    sp_digit* t1 = t + 6*4;
    sp_digit* t2 = t + 8*4;
    sp_digit* x;
    sp_digit* y;
    sp_digit* z;
    int i;

    rp[0] = r;
    rp[1] = (sp_point*)t;
    XMEMSET(rp[1], 0, sizeof(sp_point));
    x = rp[p->infinity]->x;
    y = rp[p->infinity]->y;
    z = rp[p->infinity]->z;
    if (r != p) {
        for (i=0; i<4; i++)
            r->x[i] = p->x[i];
        for (i=0; i<4; i++)
            r->y[i] = p->y[i];
        for (i=0; i<4; i++)
            r->z[i] = p->z[i];
        r->infinity = p->infinity;
    }

    /* Y = 2*Y */
    sp_256_mont_dbl_4(y, y, p256_mod);
    /* W = Z^4 */
    sp_256_mont_sqr_4(w, z, p256_mod, p256_mp_mod);
    sp_256_mont_sqr_4(w, w, p256_mod, p256_mp_mod);
    while (n--) {
        /* A = 3*(X^2 - W) */
        sp_256_mont_sqr_4(t1, x, p256_mod, p256_mp_mod);
        sp_256_mont_sub_4(t1, t1, w, p256_mod);
        sp_256_mont_tpl_4(a, t1, p256_mod);
        /* B = X*Y^2 */
        sp_256_mont_sqr_4(t2, y, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(b, t2, x, p256_mod, p256_mp_mod);
        /* X = A^2 - 2B */
        sp_256_mont_sqr_4(x, a, p256_mod, p256_mp_mod);
        sp_256_mont_dbl_4(t1, b, p256_mod);
        sp_256_mont_sub_4(x, x, t1, p256_mod);
        /* Z = Z*Y */
        sp_256_mont_mul_4(z, z, y, p256_mod, p256_mp_mod);
        /* t2 = Y^4 */
        sp_256_mont_sqr_4(t2, t2, p256_mod, p256_mp_mod);
        if (n) {
            /* W = W*Y^4 */
            sp_256_mont_mul_4(w, w, t2, p256_mod, p256_mp_mod);
        }
        /* y = 2*A*(B - X) - Y^4 */
        sp_256_mont_sub_4(y, b, x, p256_mod);
        sp_256_mont_mul_4(y, y, a, p256_mod, p256_mp_mod);
        sp_256_mont_dbl_4(y, y, p256_mod);
        sp_256_mont_sub_4(y, y, t2, p256_mod);
    }
    /* Y = Y/2 */
    sp_256_div2_4(y, y, p256_mod);
}

/* Compare two numbers to determine if they are equal.
 * Constant time implementation.
 *
 * a  First number to compare.
 * b  Second number to compare.
 * returns 1 when equal and 0 otherwise.
 */
static int sp_256_cmp_equal_4(const sp_digit* a, const sp_digit* b)
{
    return ((a[0] ^ b[0]) | (a[1] ^ b[1]) | (a[2] ^ b[2]) | (a[3] ^ b[3])) == 0;
}

/* Add two Montgomery form projective points.
 *
 * r  Result of addition.
 * p  Frist point to add.
 * q  Second point to add.
 * t  Temporary ordinate data.
 */
static void sp_256_proj_point_add_4(sp_point* r, const sp_point* p, const sp_point* q,
        sp_digit* t)
{
    const sp_point* ap[2];
    sp_point* rp[2];
    sp_digit* t1 = t;
    sp_digit* t2 = t + 2*4;
    sp_digit* t3 = t + 4*4;
    sp_digit* t4 = t + 6*4;
    sp_digit* t5 = t + 8*4;
    sp_digit* x;
    sp_digit* y;
    sp_digit* z;
    int i;

    /* Ensure only the first point is the same as the result. */
    if (q == r) {
        const sp_point* a = p;
        p = q;
        q = a;
    }

    /* Check double */
    sp_256_sub_4(t1, p256_mod, q->y);
    sp_256_norm_4(t1);
    if (sp_256_cmp_equal_4(p->x, q->x) & sp_256_cmp_equal_4(p->z, q->z) &
        (sp_256_cmp_equal_4(p->y, q->y) | sp_256_cmp_equal_4(p->y, t1))) {
        sp_256_proj_point_dbl_4(r, p, t);
    }
    else {
        rp[0] = r;
        rp[1] = (sp_point*)t;
        XMEMSET(rp[1], 0, sizeof(sp_point));
        x = rp[p->infinity | q->infinity]->x;
        y = rp[p->infinity | q->infinity]->y;
        z = rp[p->infinity | q->infinity]->z;

        ap[0] = p;
        ap[1] = q;
        for (i=0; i<4; i++)
            r->x[i] = ap[p->infinity]->x[i];
        for (i=0; i<4; i++)
            r->y[i] = ap[p->infinity]->y[i];
        for (i=0; i<4; i++)
            r->z[i] = ap[p->infinity]->z[i];
        r->infinity = ap[p->infinity]->infinity;

        /* U1 = X1*Z2^2 */
        sp_256_mont_sqr_4(t1, q->z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(t3, t1, q->z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(t1, t1, x, p256_mod, p256_mp_mod);
        /* U2 = X2*Z1^2 */
        sp_256_mont_sqr_4(t2, z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(t4, t2, z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(t2, t2, q->x, p256_mod, p256_mp_mod);
        /* S1 = Y1*Z2^3 */
        sp_256_mont_mul_4(t3, t3, y, p256_mod, p256_mp_mod);
        /* S2 = Y2*Z1^3 */
        sp_256_mont_mul_4(t4, t4, q->y, p256_mod, p256_mp_mod);
        /* H = U2 - U1 */
        sp_256_mont_sub_4(t2, t2, t1, p256_mod);
        /* R = S2 - S1 */
        sp_256_mont_sub_4(t4, t4, t3, p256_mod);
        /* Z3 = H*Z1*Z2 */
        sp_256_mont_mul_4(z, z, q->z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(z, z, t2, p256_mod, p256_mp_mod);
        /* X3 = R^2 - H^3 - 2*U1*H^2 */
        sp_256_mont_sqr_4(x, t4, p256_mod, p256_mp_mod);
        sp_256_mont_sqr_4(t5, t2, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(y, t1, t5, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(t5, t5, t2, p256_mod, p256_mp_mod);
        sp_256_mont_sub_4(x, x, t5, p256_mod);
        sp_256_mont_dbl_4(t1, y, p256_mod);
        sp_256_mont_sub_4(x, x, t1, p256_mod);
        /* Y3 = R*(U1*H^2 - X3) - S1*H^3 */
        sp_256_mont_sub_4(y, y, x, p256_mod);
        sp_256_mont_mul_4(y, y, t4, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(t5, t5, t3, p256_mod, p256_mp_mod);
        sp_256_mont_sub_4(y, y, t5, p256_mod);
    }
}

/* Double the Montgomery form projective point p a number of times.
 *
 * r  Result of repeated doubling of point.
 * p  Point to double.
 * n  Number of times to double
 * t  Temporary ordinate data.
 */
static void sp_256_proj_point_dbl_n_store_4(sp_point* r, const sp_point* p,
        int n, int m, sp_digit* t)
{
    sp_digit* w = t;
    sp_digit* a = t + 2*4;
    sp_digit* b = t + 4*4;
    sp_digit* t1 = t + 6*4;
    sp_digit* t2 = t + 8*4;
    sp_digit* x = r[2*m].x;
    sp_digit* y = r[(1<<n)*m].y;
    sp_digit* z = r[2*m].z;
    int i;

    for (i=0; i<4; i++)
        x[i] = p->x[i];
    for (i=0; i<4; i++)
        y[i] = p->y[i];
    for (i=0; i<4; i++)
        z[i] = p->z[i];

    /* Y = 2*Y */
    sp_256_mont_dbl_4(y, y, p256_mod);
    /* W = Z^4 */
    sp_256_mont_sqr_4(w, z, p256_mod, p256_mp_mod);
    sp_256_mont_sqr_4(w, w, p256_mod, p256_mp_mod);
    for (i=1; i<=n; i++) {
        /* A = 3*(X^2 - W) */
        sp_256_mont_sqr_4(t1, x, p256_mod, p256_mp_mod);
        sp_256_mont_sub_4(t1, t1, w, p256_mod);
        sp_256_mont_tpl_4(a, t1, p256_mod);
        /* B = X*Y^2 */
        sp_256_mont_sqr_4(t2, y, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(b, t2, x, p256_mod, p256_mp_mod);
        x = r[(1<<i)*m].x;
        /* X = A^2 - 2B */
        sp_256_mont_sqr_4(x, a, p256_mod, p256_mp_mod);
        sp_256_mont_dbl_4(t1, b, p256_mod);
        sp_256_mont_sub_4(x, x, t1, p256_mod);
        /* Z = Z*Y */
        sp_256_mont_mul_4(r[(1<<i)*m].z, z, y, p256_mod, p256_mp_mod);
        z = r[(1<<i)*m].z;
        /* t2 = Y^4 */
        sp_256_mont_sqr_4(t2, t2, p256_mod, p256_mp_mod);
        if (i != n) {
            /* W = W*Y^4 */
            sp_256_mont_mul_4(w, w, t2, p256_mod, p256_mp_mod);
        }
        /* y = 2*A*(B - X) - Y^4 */
        sp_256_mont_sub_4(y, b, x, p256_mod);
        sp_256_mont_mul_4(y, y, a, p256_mod, p256_mp_mod);
        sp_256_mont_dbl_4(y, y, p256_mod);
        sp_256_mont_sub_4(y, y, t2, p256_mod);

        /* Y = Y/2 */
        sp_256_div2_4(r[(1<<i)*m].y, y, p256_mod);
        r[(1<<i)*m].infinity = 0;
    }
}

/* Add two Montgomery form projective points.
 *
 * ra  Result of addition.
 * rs  Result of subtraction.
 * p   Frist point to add.
 * q   Second point to add.
 * t   Temporary ordinate data.
 */
static void sp_256_proj_point_add_sub_4(sp_point* ra, sp_point* rs,
        const sp_point* p, const sp_point* q, sp_digit* t)
{
    sp_digit* t1 = t;
    sp_digit* t2 = t + 2*4;
    sp_digit* t3 = t + 4*4;
    sp_digit* t4 = t + 6*4;
    sp_digit* t5 = t + 8*4;
    sp_digit* t6 = t + 10*4;
    sp_digit* x = ra->x;
    sp_digit* y = ra->y;
    sp_digit* z = ra->z;
    sp_digit* xs = rs->x;
    sp_digit* ys = rs->y;
    sp_digit* zs = rs->z;


    XMEMCPY(x, p->x, sizeof(p->x) / 2);
    XMEMCPY(y, p->y, sizeof(p->y) / 2);
    XMEMCPY(z, p->z, sizeof(p->z) / 2);
    ra->infinity = 0;
    rs->infinity = 0;

    /* U1 = X1*Z2^2 */
    sp_256_mont_sqr_4(t1, q->z, p256_mod, p256_mp_mod);
    sp_256_mont_mul_4(t3, t1, q->z, p256_mod, p256_mp_mod);
    sp_256_mont_mul_4(t1, t1, x, p256_mod, p256_mp_mod);
    /* U2 = X2*Z1^2 */
    sp_256_mont_sqr_4(t2, z, p256_mod, p256_mp_mod);
    sp_256_mont_mul_4(t4, t2, z, p256_mod, p256_mp_mod);
    sp_256_mont_mul_4(t2, t2, q->x, p256_mod, p256_mp_mod);
    /* S1 = Y1*Z2^3 */
    sp_256_mont_mul_4(t3, t3, y, p256_mod, p256_mp_mod);
    /* S2 = Y2*Z1^3 */
    sp_256_mont_mul_4(t4, t4, q->y, p256_mod, p256_mp_mod);
    /* H = U2 - U1 */
    sp_256_mont_sub_4(t2, t2, t1, p256_mod);
    /* RS = S2 + S1 */
    sp_256_mont_add_4(t6, t4, t3, p256_mod);
    /* R = S2 - S1 */
    sp_256_mont_sub_4(t4, t4, t3, p256_mod);
    /* Z3 = H*Z1*Z2 */
    /* ZS = H*Z1*Z2 */
    sp_256_mont_mul_4(z, z, q->z, p256_mod, p256_mp_mod);
    sp_256_mont_mul_4(z, z, t2, p256_mod, p256_mp_mod);
    XMEMCPY(zs, z, sizeof(p->z)/2);
    /* X3 = R^2 - H^3 - 2*U1*H^2 */
    /* XS = RS^2 - H^3 - 2*U1*H^2 */
    sp_256_mont_sqr_4(x, t4, p256_mod, p256_mp_mod);
    sp_256_mont_sqr_4(xs, t6, p256_mod, p256_mp_mod);
    sp_256_mont_sqr_4(t5, t2, p256_mod, p256_mp_mod);
    sp_256_mont_mul_4(y, t1, t5, p256_mod, p256_mp_mod);
    sp_256_mont_mul_4(t5, t5, t2, p256_mod, p256_mp_mod);
    sp_256_mont_sub_4(x, x, t5, p256_mod);
    sp_256_mont_sub_4(xs, xs, t5, p256_mod);
    sp_256_mont_dbl_4(t1, y, p256_mod);
    sp_256_mont_sub_4(x, x, t1, p256_mod);
    sp_256_mont_sub_4(xs, xs, t1, p256_mod);
    /* Y3 = R*(U1*H^2 - X3) - S1*H^3 */
    /* YS = -RS*(U1*H^2 - XS) - S1*H^3 */
    sp_256_mont_sub_4(ys, y, xs, p256_mod);
    sp_256_mont_sub_4(y, y, x, p256_mod);
    sp_256_mont_mul_4(y, y, t4, p256_mod, p256_mp_mod);
    sp_256_sub_4(t6, p256_mod, t6);
    sp_256_mont_mul_4(ys, ys, t6, p256_mod, p256_mp_mod);
    sp_256_mont_mul_4(t5, t5, t3, p256_mod, p256_mp_mod);
    sp_256_mont_sub_4(y, y, t5, p256_mod);
    sp_256_mont_sub_4(ys, ys, t5, p256_mod);
}

/* Structure used to describe recoding of scalar multiplication. */
typedef struct ecc_recode {
    /* Index into pre-computation table. */
    uint8_t i;
    /* Use the negative of the point. */
    uint8_t neg;
} ecc_recode;

/* The index into pre-computation table to use. */
static const uint8_t recode_index_4_6[66] = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17,
    16, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,
     0,  1,
};

/* Whether to negate y-ordinate. */
static const uint8_t recode_neg_4_6[66] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     0,  0,
};

/* Recode the scalar for multiplication using pre-computed values and
 * subtraction.
 *
 * k  Scalar to multiply by.
 * v  Vector of operations to peform.
 */
static void sp_256_ecc_recode_6_4(const sp_digit* k, ecc_recode* v)
{
    int i, j;
    uint8_t y;
    int carry = 0;
    int o;
    sp_digit n;

    j = 0;
    n = k[j];
    o = 0;
    for (i=0; i<43; i++) {
        y = n;
        if (o + 6 < 64) {
            y &= 0x3f;
            n >>= 6;
            o += 6;
        }
        else if (o + 6 == 64) {
            n >>= 6;
            if (++j < 4)
                n = k[j];
            o = 0;
        }
        else if (++j < 4) {
            n = k[j];
            y |= (n << (64 - o)) & 0x3f;
            o -= 58;
            n >>= o;
        }

        y += carry;
        v[i].i = recode_index_4_6[y];
        v[i].neg = recode_neg_4_6[y];
        carry = (y >> 6) + v[i].neg;
    }
}

/* Multiply the point by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * r     Resulting point.
 * g     Point to multiply.
 * k     Scalar to multiply by.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
static int sp_256_ecc_mulmod_win_add_sub_4(sp_point* r, const sp_point* g,
        const sp_digit* k, int map, void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point td[33];
    sp_point rtd, pd;
    sp_digit tmpd[2 * 4 * 6];
#endif
    sp_point* t;
    sp_point* rt;
    sp_point* p = NULL;
    sp_digit* tmp;
    sp_digit* negy;
    int i;
    ecc_recode v[43];
    int err;

    (void)heap;

    err = sp_ecc_point_new(heap, rtd, rt);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, pd, p);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    t = (sp_point*)XMALLOC(sizeof(sp_point) * 33, heap, DYNAMIC_TYPE_ECC);
    if (t == NULL)
        err = MEMORY_E;
    tmp = (sp_digit*)XMALLOC(sizeof(sp_digit) * 2 * 4 * 6, heap,
                             DYNAMIC_TYPE_ECC);
    if (tmp == NULL)
        err = MEMORY_E;
#else
    t = td;
    tmp = tmpd;
#endif


    if (err == MP_OKAY) {
        /* t[0] = {0, 0, 1} * norm */
        XMEMSET(&t[0], 0, sizeof(t[0]));
        t[0].infinity = 1;
        /* t[1] = {g->x, g->y, g->z} * norm */
        err = sp_256_mod_mul_norm_4(t[1].x, g->x, p256_mod);
    }
    if (err == MP_OKAY)
        err = sp_256_mod_mul_norm_4(t[1].y, g->y, p256_mod);
    if (err == MP_OKAY)
        err = sp_256_mod_mul_norm_4(t[1].z, g->z, p256_mod);

    if (err == MP_OKAY) {
        t[1].infinity = 0;
        /* t[2] ... t[32]  */
    sp_256_proj_point_dbl_n_store_4(t, &t[ 1], 5, 1, tmp);
    sp_256_proj_point_add_4(&t[ 3], &t[ 2], &t[ 1], tmp);
    sp_256_proj_point_dbl_4(&t[ 6], &t[ 3], tmp);
    sp_256_proj_point_add_sub_4(&t[ 7], &t[ 5], &t[ 6], &t[ 1], tmp);
    sp_256_proj_point_dbl_4(&t[10], &t[ 5], tmp);
    sp_256_proj_point_add_sub_4(&t[11], &t[ 9], &t[10], &t[ 1], tmp);
    sp_256_proj_point_dbl_4(&t[12], &t[ 6], tmp);
    sp_256_proj_point_dbl_4(&t[14], &t[ 7], tmp);
    sp_256_proj_point_add_sub_4(&t[15], &t[13], &t[14], &t[ 1], tmp);
    sp_256_proj_point_dbl_4(&t[18], &t[ 9], tmp);
    sp_256_proj_point_add_sub_4(&t[19], &t[17], &t[18], &t[ 1], tmp);
    sp_256_proj_point_dbl_4(&t[20], &t[10], tmp);
    sp_256_proj_point_dbl_4(&t[22], &t[11], tmp);
    sp_256_proj_point_add_sub_4(&t[23], &t[21], &t[22], &t[ 1], tmp);
    sp_256_proj_point_dbl_4(&t[24], &t[12], tmp);
    sp_256_proj_point_dbl_4(&t[26], &t[13], tmp);
    sp_256_proj_point_add_sub_4(&t[27], &t[25], &t[26], &t[ 1], tmp);
    sp_256_proj_point_dbl_4(&t[28], &t[14], tmp);
    sp_256_proj_point_dbl_4(&t[30], &t[15], tmp);
    sp_256_proj_point_add_sub_4(&t[31], &t[29], &t[30], &t[ 1], tmp);

        negy = t[0].y;

        sp_256_ecc_recode_6_4(k, v);

        i = 42;
        XMEMCPY(rt, &t[v[i].i], sizeof(sp_point));
        for (--i; i>=0; i--) {
            sp_256_proj_point_dbl_n_4(rt, rt, 6, tmp);

            XMEMCPY(p, &t[v[i].i], sizeof(sp_point));
            sp_256_sub_4(negy, p256_mod, p->y);
            sp_256_cond_copy_4(p->y, negy, (sp_digit)0 - v[i].neg);
            sp_256_proj_point_add_4(rt, rt, p, tmp);
        }

        if (map)
            sp_256_map_4(r, rt, tmp);
        else
            XMEMCPY(r, rt, sizeof(sp_point));
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (t != NULL)
        XFREE(t, heap, DYNAMIC_TYPE_ECC);
    if (tmp != NULL)
        XFREE(tmp, heap, DYNAMIC_TYPE_ECC);
#endif
    sp_ecc_point_free(p, 0, heap);
    sp_ecc_point_free(rt, 0, heap);

    return err;
}

#ifdef HAVE_INTEL_AVX2
extern void sp_256_mont_mul_avx2_4(sp_digit* r, const sp_digit* a, const sp_digit* b, const sp_digit* m, sp_digit mp);
extern void sp_256_mont_sqr_avx2_4(sp_digit* r, const sp_digit* a, const sp_digit* m, sp_digit mp);
#if !defined(WOLFSSL_SP_SMALL) || defined(HAVE_COMP_KEY)
/* Square the Montgomery form number a number of times. (r = a ^ n mod m)
 *
 * r   Result of squaring.
 * a   Number to square in Montogmery form.
 * n   Number of times to square.
 * m   Modulus (prime).
 * mp  Montogmery mulitplier.
 */
static void sp_256_mont_sqr_n_avx2_4(sp_digit* r, const sp_digit* a, int n,
        const sp_digit* m, sp_digit mp)
{
    sp_256_mont_sqr_avx2_4(r, a, m, mp);
    for (; n > 1; n--)
        sp_256_mont_sqr_avx2_4(r, r, m, mp);
}

#endif /* !WOLFSSL_SP_SMALL || HAVE_COMP_KEY */

/* Invert the number, in Montgomery form, modulo the modulus (prime) of the
 * P256 curve. (r = 1 / a mod m)
 *
 * r   Inverse result.
 * a   Number to invert.
 * td  Temporary data.
 */
static void sp_256_mont_inv_avx2_4(sp_digit* r, const sp_digit* a, sp_digit* td)
{
#ifdef WOLFSSL_SP_SMALL
    sp_digit* t = td;
    int i;

    XMEMCPY(t, a, sizeof(sp_digit) * 4);
    for (i=254; i>=0; i--) {
        sp_256_mont_sqr_avx2_4(t, t, p256_mod, p256_mp_mod);
        if (p256_mod_2[i / 64] & ((sp_digit)1 << (i % 64)))
            sp_256_mont_mul_avx2_4(t, t, a, p256_mod, p256_mp_mod);
    }
    XMEMCPY(r, t, sizeof(sp_digit) * 4);
#else
    sp_digit* t = td;
    sp_digit* t2 = td + 2 * 4;
    sp_digit* t3 = td + 4 * 4;

    /* t = a^2 */
    sp_256_mont_sqr_avx2_4(t, a, p256_mod, p256_mp_mod);
    /* t = a^3 = t * a */
    sp_256_mont_mul_avx2_4(t, t, a, p256_mod, p256_mp_mod);
    /* t2= a^c = t ^ 2 ^ 2 */
    sp_256_mont_sqr_n_avx2_4(t2, t, 2, p256_mod, p256_mp_mod);
    /* t3= a^d = t2 * a */
    sp_256_mont_mul_avx2_4(t3, t2, a, p256_mod, p256_mp_mod);
    /* t = a^f = t2 * t */
    sp_256_mont_mul_avx2_4(t, t2, t, p256_mod, p256_mp_mod);
    /* t2= a^f0 = t ^ 2 ^ 4 */
    sp_256_mont_sqr_n_avx2_4(t2, t, 4, p256_mod, p256_mp_mod);
    /* t3= a^fd = t2 * t3 */
    sp_256_mont_mul_avx2_4(t3, t2, t3, p256_mod, p256_mp_mod);
    /* t = a^ff = t2 * t */
    sp_256_mont_mul_avx2_4(t, t2, t, p256_mod, p256_mp_mod);
    /* t2= a^ff00 = t ^ 2 ^ 8 */
    sp_256_mont_sqr_n_avx2_4(t2, t, 8, p256_mod, p256_mp_mod);
    /* t3= a^fffd = t2 * t3 */
    sp_256_mont_mul_avx2_4(t3, t2, t3, p256_mod, p256_mp_mod);
    /* t = a^ffff = t2 * t */
    sp_256_mont_mul_avx2_4(t, t2, t, p256_mod, p256_mp_mod);
    /* t2= a^ffff0000 = t ^ 2 ^ 16 */
    sp_256_mont_sqr_n_avx2_4(t2, t, 16, p256_mod, p256_mp_mod);
    /* t3= a^fffffffd = t2 * t3 */
    sp_256_mont_mul_avx2_4(t3, t2, t3, p256_mod, p256_mp_mod);
    /* t = a^ffffffff = t2 * t */
    sp_256_mont_mul_avx2_4(t, t2, t, p256_mod, p256_mp_mod);
    /* t = a^ffffffff00000000 = t ^ 2 ^ 32  */
    sp_256_mont_sqr_n_avx2_4(t2, t, 32, p256_mod, p256_mp_mod);
    /* t2= a^ffffffffffffffff = t2 * t */
    sp_256_mont_mul_avx2_4(t, t2, t, p256_mod, p256_mp_mod);
    /* t2= a^ffffffff00000001 = t2 * a */
    sp_256_mont_mul_avx2_4(t2, t2, a, p256_mod, p256_mp_mod);
    /* t2= a^ffffffff000000010000000000000000000000000000000000000000
     *   = t2 ^ 2 ^ 160 */
    sp_256_mont_sqr_n_avx2_4(t2, t2, 160, p256_mod, p256_mp_mod);
    /* t2= a^ffffffff00000001000000000000000000000000ffffffffffffffff
     *   = t2 * t */
    sp_256_mont_mul_avx2_4(t2, t2, t, p256_mod, p256_mp_mod);
    /* t2= a^ffffffff00000001000000000000000000000000ffffffffffffffff00000000
     *   = t2 ^ 2 ^ 32 */
    sp_256_mont_sqr_n_avx2_4(t2, t2, 32, p256_mod, p256_mp_mod);
    /* r = a^ffffffff00000001000000000000000000000000fffffffffffffffffffffffd
     *   = t2 * t3 */
    sp_256_mont_mul_avx2_4(r, t2, t3, p256_mod, p256_mp_mod);
#endif /* WOLFSSL_SP_SMALL */
}

/* Map the Montgomery form projective co-ordinate point to an affine point.
 *
 * r  Resulting affine co-ordinate point.
 * p  Montgomery form projective co-ordinate point.
 * t  Temporary ordinate data.
 */
static void sp_256_map_avx2_4(sp_point* r, const sp_point* p, sp_digit* t)
{
    sp_digit* t1 = t;
    sp_digit* t2 = t + 2*4;
    int64_t n;

    sp_256_mont_inv_avx2_4(t1, p->z, t + 2*4);

    sp_256_mont_sqr_avx2_4(t2, t1, p256_mod, p256_mp_mod);
    sp_256_mont_mul_avx2_4(t1, t2, t1, p256_mod, p256_mp_mod);

    /* x /= z^2 */
    sp_256_mont_mul_avx2_4(r->x, p->x, t2, p256_mod, p256_mp_mod);
    XMEMSET(r->x + 4, 0, sizeof(r->x) / 2);
    sp_256_mont_reduce_4(r->x, p256_mod, p256_mp_mod);
    /* Reduce x to less than modulus */
    n = sp_256_cmp_4(r->x, p256_mod);
    sp_256_cond_sub_4(r->x, r->x, p256_mod, 0 - (n >= 0));
    sp_256_norm_4(r->x);

    /* y /= z^3 */
    sp_256_mont_mul_avx2_4(r->y, p->y, t1, p256_mod, p256_mp_mod);
    XMEMSET(r->y + 4, 0, sizeof(r->y) / 2);
    sp_256_mont_reduce_4(r->y, p256_mod, p256_mp_mod);
    /* Reduce y to less than modulus */
    n = sp_256_cmp_4(r->y, p256_mod);
    sp_256_cond_sub_4(r->y, r->y, p256_mod, 0 - (n >= 0));
    sp_256_norm_4(r->y);

    XMEMSET(r->z, 0, sizeof(r->z));
    r->z[0] = 1;

}

/* Double the Montgomery form projective point p.
 *
 * r  Result of doubling point.
 * p  Point to double.
 * t  Temporary ordinate data.
 */
static void sp_256_proj_point_dbl_avx2_4(sp_point* r, const sp_point* p, sp_digit* t)
{
    sp_point* rp[2];
    sp_digit* t1 = t;
    sp_digit* t2 = t + 2*4;
    sp_digit* x;
    sp_digit* y;
    sp_digit* z;
    int i;

    /* When infinity don't double point passed in - constant time. */
    rp[0] = r;
    rp[1] = (sp_point*)t;
    XMEMSET(rp[1], 0, sizeof(sp_point));
    x = rp[p->infinity]->x;
    y = rp[p->infinity]->y;
    z = rp[p->infinity]->z;
    /* Put point to double into result - good for infinty. */
    if (r != p) {
        for (i=0; i<4; i++)
            r->x[i] = p->x[i];
        for (i=0; i<4; i++)
            r->y[i] = p->y[i];
        for (i=0; i<4; i++)
            r->z[i] = p->z[i];
        r->infinity = p->infinity;
    }

    /* T1 = Z * Z */
    sp_256_mont_sqr_avx2_4(t1, z, p256_mod, p256_mp_mod);
    /* Z = Y * Z */
    sp_256_mont_mul_avx2_4(z, y, z, p256_mod, p256_mp_mod);
    /* Z = 2Z */
    sp_256_mont_dbl_4(z, z, p256_mod);
    /* T2 = X - T1 */
    sp_256_mont_sub_4(t2, x, t1, p256_mod);
    /* T1 = X + T1 */
    sp_256_mont_add_4(t1, x, t1, p256_mod);
    /* T2 = T1 * T2 */
    sp_256_mont_mul_avx2_4(t2, t1, t2, p256_mod, p256_mp_mod);
    /* T1 = 3T2 */
    sp_256_mont_tpl_4(t1, t2, p256_mod);
    /* Y = 2Y */
    sp_256_mont_dbl_4(y, y, p256_mod);
    /* Y = Y * Y */
    sp_256_mont_sqr_avx2_4(y, y, p256_mod, p256_mp_mod);
    /* T2 = Y * Y */
    sp_256_mont_sqr_avx2_4(t2, y, p256_mod, p256_mp_mod);
    /* T2 = T2/2 */
    sp_256_div2_4(t2, t2, p256_mod);
    /* Y = Y * X */
    sp_256_mont_mul_avx2_4(y, y, x, p256_mod, p256_mp_mod);
    /* X = T1 * T1 */
    sp_256_mont_mul_avx2_4(x, t1, t1, p256_mod, p256_mp_mod);
    /* X = X - Y */
    sp_256_mont_sub_4(x, x, y, p256_mod);
    /* X = X - Y */
    sp_256_mont_sub_4(x, x, y, p256_mod);
    /* Y = Y - X */
    sp_256_mont_sub_4(y, y, x, p256_mod);
    /* Y = Y * T1 */
    sp_256_mont_mul_avx2_4(y, y, t1, p256_mod, p256_mp_mod);
    /* Y = Y - T2 */
    sp_256_mont_sub_4(y, y, t2, p256_mod);

}

/* Double the Montgomery form projective point p a number of times.
 *
 * r  Result of repeated doubling of point.
 * p  Point to double.
 * n  Number of times to double
 * t  Temporary ordinate data.
 */
static void sp_256_proj_point_dbl_n_avx2_4(sp_point* r, const sp_point* p, int n,
        sp_digit* t)
{
    sp_point* rp[2];
    sp_digit* w = t;
    sp_digit* a = t + 2*4;
    sp_digit* b = t + 4*4;
    sp_digit* t1 = t + 6*4;
    sp_digit* t2 = t + 8*4;
    sp_digit* x;
    sp_digit* y;
    sp_digit* z;
    int i;

    rp[0] = r;
    rp[1] = (sp_point*)t;
    XMEMSET(rp[1], 0, sizeof(sp_point));
    x = rp[p->infinity]->x;
    y = rp[p->infinity]->y;
    z = rp[p->infinity]->z;
    if (r != p) {
        for (i=0; i<4; i++)
            r->x[i] = p->x[i];
        for (i=0; i<4; i++)
            r->y[i] = p->y[i];
        for (i=0; i<4; i++)
            r->z[i] = p->z[i];
        r->infinity = p->infinity;
    }

    /* Y = 2*Y */
    sp_256_mont_dbl_4(y, y, p256_mod);
    /* W = Z^4 */
    sp_256_mont_sqr_avx2_4(w, z, p256_mod, p256_mp_mod);
    sp_256_mont_sqr_avx2_4(w, w, p256_mod, p256_mp_mod);
    while (n--) {
        /* A = 3*(X^2 - W) */
        sp_256_mont_sqr_avx2_4(t1, x, p256_mod, p256_mp_mod);
        sp_256_mont_sub_4(t1, t1, w, p256_mod);
        sp_256_mont_tpl_4(a, t1, p256_mod);
        /* B = X*Y^2 */
        sp_256_mont_sqr_avx2_4(t2, y, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(b, t2, x, p256_mod, p256_mp_mod);
        /* X = A^2 - 2B */
        sp_256_mont_sqr_avx2_4(x, a, p256_mod, p256_mp_mod);
        sp_256_mont_dbl_4(t1, b, p256_mod);
        sp_256_mont_sub_4(x, x, t1, p256_mod);
        /* Z = Z*Y */
        sp_256_mont_mul_avx2_4(z, z, y, p256_mod, p256_mp_mod);
        /* t2 = Y^4 */
        sp_256_mont_sqr_avx2_4(t2, t2, p256_mod, p256_mp_mod);
        if (n) {
            /* W = W*Y^4 */
            sp_256_mont_mul_avx2_4(w, w, t2, p256_mod, p256_mp_mod);
        }
        /* y = 2*A*(B - X) - Y^4 */
        sp_256_mont_sub_4(y, b, x, p256_mod);
        sp_256_mont_mul_avx2_4(y, y, a, p256_mod, p256_mp_mod);
        sp_256_mont_dbl_4(y, y, p256_mod);
        sp_256_mont_sub_4(y, y, t2, p256_mod);
    }
    /* Y = Y/2 */
    sp_256_div2_4(y, y, p256_mod);
}

/* Add two Montgomery form projective points.
 *
 * r  Result of addition.
 * p  Frist point to add.
 * q  Second point to add.
 * t  Temporary ordinate data.
 */
static void sp_256_proj_point_add_avx2_4(sp_point* r, const sp_point* p, const sp_point* q,
        sp_digit* t)
{
    const sp_point* ap[2];
    sp_point* rp[2];
    sp_digit* t1 = t;
    sp_digit* t2 = t + 2*4;
    sp_digit* t3 = t + 4*4;
    sp_digit* t4 = t + 6*4;
    sp_digit* t5 = t + 8*4;
    sp_digit* x;
    sp_digit* y;
    sp_digit* z;
    int i;

    /* Ensure only the first point is the same as the result. */
    if (q == r) {
        const sp_point* a = p;
        p = q;
        q = a;
    }

    /* Check double */
    sp_256_sub_4(t1, p256_mod, q->y);
    sp_256_norm_4(t1);
    if (sp_256_cmp_equal_4(p->x, q->x) & sp_256_cmp_equal_4(p->z, q->z) &
        (sp_256_cmp_equal_4(p->y, q->y) | sp_256_cmp_equal_4(p->y, t1))) {
        sp_256_proj_point_dbl_4(r, p, t);
    }
    else {
        rp[0] = r;
        rp[1] = (sp_point*)t;
        XMEMSET(rp[1], 0, sizeof(sp_point));
        x = rp[p->infinity | q->infinity]->x;
        y = rp[p->infinity | q->infinity]->y;
        z = rp[p->infinity | q->infinity]->z;

        ap[0] = p;
        ap[1] = q;
        for (i=0; i<4; i++)
            r->x[i] = ap[p->infinity]->x[i];
        for (i=0; i<4; i++)
            r->y[i] = ap[p->infinity]->y[i];
        for (i=0; i<4; i++)
            r->z[i] = ap[p->infinity]->z[i];
        r->infinity = ap[p->infinity]->infinity;

        /* U1 = X1*Z2^2 */
        sp_256_mont_sqr_avx2_4(t1, q->z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(t3, t1, q->z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(t1, t1, x, p256_mod, p256_mp_mod);
        /* U2 = X2*Z1^2 */
        sp_256_mont_sqr_avx2_4(t2, z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(t4, t2, z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(t2, t2, q->x, p256_mod, p256_mp_mod);
        /* S1 = Y1*Z2^3 */
        sp_256_mont_mul_avx2_4(t3, t3, y, p256_mod, p256_mp_mod);
        /* S2 = Y2*Z1^3 */
        sp_256_mont_mul_avx2_4(t4, t4, q->y, p256_mod, p256_mp_mod);
        /* H = U2 - U1 */
        sp_256_mont_sub_4(t2, t2, t1, p256_mod);
        /* R = S2 - S1 */
        sp_256_mont_sub_4(t4, t4, t3, p256_mod);
        /* Z3 = H*Z1*Z2 */
        sp_256_mont_mul_avx2_4(z, z, q->z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(z, z, t2, p256_mod, p256_mp_mod);
        /* X3 = R^2 - H^3 - 2*U1*H^2 */
        sp_256_mont_sqr_avx2_4(x, t4, p256_mod, p256_mp_mod);
        sp_256_mont_sqr_avx2_4(t5, t2, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(y, t1, t5, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(t5, t5, t2, p256_mod, p256_mp_mod);
        sp_256_mont_sub_4(x, x, t5, p256_mod);
        sp_256_mont_dbl_4(t1, y, p256_mod);
        sp_256_mont_sub_4(x, x, t1, p256_mod);
        /* Y3 = R*(U1*H^2 - X3) - S1*H^3 */
        sp_256_mont_sub_4(y, y, x, p256_mod);
        sp_256_mont_mul_avx2_4(y, y, t4, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(t5, t5, t3, p256_mod, p256_mp_mod);
        sp_256_mont_sub_4(y, y, t5, p256_mod);
    }
}

/* Double the Montgomery form projective point p a number of times.
 *
 * r  Result of repeated doubling of point.
 * p  Point to double.
 * n  Number of times to double
 * t  Temporary ordinate data.
 */
static void sp_256_proj_point_dbl_n_store_avx2_4(sp_point* r, const sp_point* p,
        int n, int m, sp_digit* t)
{
    sp_digit* w = t;
    sp_digit* a = t + 2*4;
    sp_digit* b = t + 4*4;
    sp_digit* t1 = t + 6*4;
    sp_digit* t2 = t + 8*4;
    sp_digit* x = r[2*m].x;
    sp_digit* y = r[(1<<n)*m].y;
    sp_digit* z = r[2*m].z;
    int i;

    for (i=0; i<4; i++)
        x[i] = p->x[i];
    for (i=0; i<4; i++)
        y[i] = p->y[i];
    for (i=0; i<4; i++)
        z[i] = p->z[i];

    /* Y = 2*Y */
    sp_256_mont_dbl_4(y, y, p256_mod);
    /* W = Z^4 */
    sp_256_mont_sqr_avx2_4(w, z, p256_mod, p256_mp_mod);
    sp_256_mont_sqr_avx2_4(w, w, p256_mod, p256_mp_mod);
    for (i=1; i<=n; i++) {
        /* A = 3*(X^2 - W) */
        sp_256_mont_sqr_avx2_4(t1, x, p256_mod, p256_mp_mod);
        sp_256_mont_sub_4(t1, t1, w, p256_mod);
        sp_256_mont_tpl_4(a, t1, p256_mod);
        /* B = X*Y^2 */
        sp_256_mont_sqr_avx2_4(t2, y, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(b, t2, x, p256_mod, p256_mp_mod);
        x = r[(1<<i)*m].x;
        /* X = A^2 - 2B */
        sp_256_mont_sqr_avx2_4(x, a, p256_mod, p256_mp_mod);
        sp_256_mont_dbl_4(t1, b, p256_mod);
        sp_256_mont_sub_4(x, x, t1, p256_mod);
        /* Z = Z*Y */
        sp_256_mont_mul_avx2_4(r[(1<<i)*m].z, z, y, p256_mod, p256_mp_mod);
        z = r[(1<<i)*m].z;
        /* t2 = Y^4 */
        sp_256_mont_sqr_avx2_4(t2, t2, p256_mod, p256_mp_mod);
        if (i != n) {
            /* W = W*Y^4 */
            sp_256_mont_mul_avx2_4(w, w, t2, p256_mod, p256_mp_mod);
        }
        /* y = 2*A*(B - X) - Y^4 */
        sp_256_mont_sub_4(y, b, x, p256_mod);
        sp_256_mont_mul_avx2_4(y, y, a, p256_mod, p256_mp_mod);
        sp_256_mont_dbl_4(y, y, p256_mod);
        sp_256_mont_sub_4(y, y, t2, p256_mod);

        /* Y = Y/2 */
        sp_256_div2_4(r[(1<<i)*m].y, y, p256_mod);
        r[(1<<i)*m].infinity = 0;
    }
}

/* Add two Montgomery form projective points.
 *
 * ra  Result of addition.
 * rs  Result of subtraction.
 * p   Frist point to add.
 * q   Second point to add.
 * t   Temporary ordinate data.
 */
static void sp_256_proj_point_add_sub_avx2_4(sp_point* ra, sp_point* rs,
        const sp_point* p, const sp_point* q, sp_digit* t)
{
    sp_digit* t1 = t;
    sp_digit* t2 = t + 2*4;
    sp_digit* t3 = t + 4*4;
    sp_digit* t4 = t + 6*4;
    sp_digit* t5 = t + 8*4;
    sp_digit* t6 = t + 10*4;
    sp_digit* x = ra->x;
    sp_digit* y = ra->y;
    sp_digit* z = ra->z;
    sp_digit* xs = rs->x;
    sp_digit* ys = rs->y;
    sp_digit* zs = rs->z;


    XMEMCPY(x, p->x, sizeof(p->x) / 2);
    XMEMCPY(y, p->y, sizeof(p->y) / 2);
    XMEMCPY(z, p->z, sizeof(p->z) / 2);
    ra->infinity = 0;
    rs->infinity = 0;

    /* U1 = X1*Z2^2 */
    sp_256_mont_sqr_avx2_4(t1, q->z, p256_mod, p256_mp_mod);
    sp_256_mont_mul_avx2_4(t3, t1, q->z, p256_mod, p256_mp_mod);
    sp_256_mont_mul_avx2_4(t1, t1, x, p256_mod, p256_mp_mod);
    /* U2 = X2*Z1^2 */
    sp_256_mont_sqr_avx2_4(t2, z, p256_mod, p256_mp_mod);
    sp_256_mont_mul_avx2_4(t4, t2, z, p256_mod, p256_mp_mod);
    sp_256_mont_mul_avx2_4(t2, t2, q->x, p256_mod, p256_mp_mod);
    /* S1 = Y1*Z2^3 */
    sp_256_mont_mul_avx2_4(t3, t3, y, p256_mod, p256_mp_mod);
    /* S2 = Y2*Z1^3 */
    sp_256_mont_mul_avx2_4(t4, t4, q->y, p256_mod, p256_mp_mod);
    /* H = U2 - U1 */
    sp_256_mont_sub_4(t2, t2, t1, p256_mod);
    /* RS = S2 + S1 */
    sp_256_mont_add_4(t6, t4, t3, p256_mod);
    /* R = S2 - S1 */
    sp_256_mont_sub_4(t4, t4, t3, p256_mod);
    /* Z3 = H*Z1*Z2 */
    /* ZS = H*Z1*Z2 */
    sp_256_mont_mul_avx2_4(z, z, q->z, p256_mod, p256_mp_mod);
    sp_256_mont_mul_avx2_4(z, z, t2, p256_mod, p256_mp_mod);
    XMEMCPY(zs, z, sizeof(p->z)/2);
    /* X3 = R^2 - H^3 - 2*U1*H^2 */
    /* XS = RS^2 - H^3 - 2*U1*H^2 */
    sp_256_mont_sqr_avx2_4(x, t4, p256_mod, p256_mp_mod);
    sp_256_mont_sqr_avx2_4(xs, t6, p256_mod, p256_mp_mod);
    sp_256_mont_sqr_avx2_4(t5, t2, p256_mod, p256_mp_mod);
    sp_256_mont_mul_avx2_4(y, t1, t5, p256_mod, p256_mp_mod);
    sp_256_mont_mul_avx2_4(t5, t5, t2, p256_mod, p256_mp_mod);
    sp_256_mont_sub_4(x, x, t5, p256_mod);
    sp_256_mont_sub_4(xs, xs, t5, p256_mod);
    sp_256_mont_dbl_4(t1, y, p256_mod);
    sp_256_mont_sub_4(x, x, t1, p256_mod);
    sp_256_mont_sub_4(xs, xs, t1, p256_mod);
    /* Y3 = R*(U1*H^2 - X3) - S1*H^3 */
    /* YS = -RS*(U1*H^2 - XS) - S1*H^3 */
    sp_256_mont_sub_4(ys, y, xs, p256_mod);
    sp_256_mont_sub_4(y, y, x, p256_mod);
    sp_256_mont_mul_avx2_4(y, y, t4, p256_mod, p256_mp_mod);
    sp_256_sub_4(t6, p256_mod, t6);
    sp_256_mont_mul_avx2_4(ys, ys, t6, p256_mod, p256_mp_mod);
    sp_256_mont_mul_avx2_4(t5, t5, t3, p256_mod, p256_mp_mod);
    sp_256_mont_sub_4(y, y, t5, p256_mod);
    sp_256_mont_sub_4(ys, ys, t5, p256_mod);
}

/* Multiply the point by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * r     Resulting point.
 * g     Point to multiply.
 * k     Scalar to multiply by.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
static int sp_256_ecc_mulmod_win_add_sub_avx2_4(sp_point* r, const sp_point* g,
        const sp_digit* k, int map, void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point td[33];
    sp_point rtd, pd;
    sp_digit tmpd[2 * 4 * 6];
#endif
    sp_point* t;
    sp_point* rt;
    sp_point* p = NULL;
    sp_digit* tmp;
    sp_digit* negy;
    int i;
    ecc_recode v[43];
    int err;

    (void)heap;

    err = sp_ecc_point_new(heap, rtd, rt);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, pd, p);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    t = (sp_point*)XMALLOC(sizeof(sp_point) * 33, heap, DYNAMIC_TYPE_ECC);
    if (t == NULL)
        err = MEMORY_E;
    tmp = (sp_digit*)XMALLOC(sizeof(sp_digit) * 2 * 4 * 6, heap,
                             DYNAMIC_TYPE_ECC);
    if (tmp == NULL)
        err = MEMORY_E;
#else
    t = td;
    tmp = tmpd;
#endif


    if (err == MP_OKAY) {
        /* t[0] = {0, 0, 1} * norm */
        XMEMSET(&t[0], 0, sizeof(t[0]));
        t[0].infinity = 1;
        /* t[1] = {g->x, g->y, g->z} * norm */
        err = sp_256_mod_mul_norm_4(t[1].x, g->x, p256_mod);
    }
    if (err == MP_OKAY)
        err = sp_256_mod_mul_norm_4(t[1].y, g->y, p256_mod);
    if (err == MP_OKAY)
        err = sp_256_mod_mul_norm_4(t[1].z, g->z, p256_mod);

    if (err == MP_OKAY) {
        t[1].infinity = 0;
        /* t[2] ... t[32]  */
    sp_256_proj_point_dbl_n_store_avx2_4(t, &t[ 1], 5, 1, tmp);
    sp_256_proj_point_add_avx2_4(&t[ 3], &t[ 2], &t[ 1], tmp);
    sp_256_proj_point_dbl_avx2_4(&t[ 6], &t[ 3], tmp);
    sp_256_proj_point_add_sub_avx2_4(&t[ 7], &t[ 5], &t[ 6], &t[ 1], tmp);
    sp_256_proj_point_dbl_avx2_4(&t[10], &t[ 5], tmp);
    sp_256_proj_point_add_sub_avx2_4(&t[11], &t[ 9], &t[10], &t[ 1], tmp);
    sp_256_proj_point_dbl_avx2_4(&t[12], &t[ 6], tmp);
    sp_256_proj_point_dbl_avx2_4(&t[14], &t[ 7], tmp);
    sp_256_proj_point_add_sub_avx2_4(&t[15], &t[13], &t[14], &t[ 1], tmp);
    sp_256_proj_point_dbl_avx2_4(&t[18], &t[ 9], tmp);
    sp_256_proj_point_add_sub_avx2_4(&t[19], &t[17], &t[18], &t[ 1], tmp);
    sp_256_proj_point_dbl_avx2_4(&t[20], &t[10], tmp);
    sp_256_proj_point_dbl_avx2_4(&t[22], &t[11], tmp);
    sp_256_proj_point_add_sub_avx2_4(&t[23], &t[21], &t[22], &t[ 1], tmp);
    sp_256_proj_point_dbl_avx2_4(&t[24], &t[12], tmp);
    sp_256_proj_point_dbl_avx2_4(&t[26], &t[13], tmp);
    sp_256_proj_point_add_sub_avx2_4(&t[27], &t[25], &t[26], &t[ 1], tmp);
    sp_256_proj_point_dbl_avx2_4(&t[28], &t[14], tmp);
    sp_256_proj_point_dbl_avx2_4(&t[30], &t[15], tmp);
    sp_256_proj_point_add_sub_avx2_4(&t[31], &t[29], &t[30], &t[ 1], tmp);

        negy = t[0].y;

        sp_256_ecc_recode_6_4(k, v);

        i = 42;
        XMEMCPY(rt, &t[v[i].i], sizeof(sp_point));
        for (--i; i>=0; i--) {
            sp_256_proj_point_dbl_n_avx2_4(rt, rt, 6, tmp);

            XMEMCPY(p, &t[v[i].i], sizeof(sp_point));
            sp_256_sub_4(negy, p256_mod, p->y);
            sp_256_cond_copy_4(p->y, negy, (sp_digit)0 - v[i].neg);
            sp_256_proj_point_add_avx2_4(rt, rt, p, tmp);
        }

        if (map)
            sp_256_map_avx2_4(r, rt, tmp);
        else
            XMEMCPY(r, rt, sizeof(sp_point));
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (t != NULL)
        XFREE(t, heap, DYNAMIC_TYPE_ECC);
    if (tmp != NULL)
        XFREE(tmp, heap, DYNAMIC_TYPE_ECC);
#endif
    sp_ecc_point_free(p, 0, heap);
    sp_ecc_point_free(rt, 0, heap);

    return err;
}

#endif /* HAVE_INTEL_AVX2 */
/* A table entry for pre-computed points. */
typedef struct sp_table_entry {
    sp_digit x[4];
    sp_digit y[4];
} sp_table_entry;

#if defined(FP_ECC) || defined(WOLFSSL_SP_SMALL)
#endif /* FP_ECC || WOLFSSL_SP_SMALL */
/* Add two Montgomery form projective points. The second point has a q value of
 * one.
 * Only the first point can be the same pointer as the result point.
 *
 * r  Result of addition.
 * p  Frist point to add.
 * q  Second point to add.
 * t  Temporary ordinate data.
 */
static void sp_256_proj_point_add_qz1_4(sp_point* r, const sp_point* p,
        const sp_point* q, sp_digit* t)
{
    const sp_point* ap[2];
    sp_point* rp[2];
    sp_digit* t1 = t;
    sp_digit* t2 = t + 2*4;
    sp_digit* t3 = t + 4*4;
    sp_digit* t4 = t + 6*4;
    sp_digit* t5 = t + 8*4;
    sp_digit* x;
    sp_digit* y;
    sp_digit* z;
    int i;

    /* Check double */
    sp_256_sub_4(t1, p256_mod, q->y);
    sp_256_norm_4(t1);
    if (sp_256_cmp_equal_4(p->x, q->x) & sp_256_cmp_equal_4(p->z, q->z) &
        (sp_256_cmp_equal_4(p->y, q->y) | sp_256_cmp_equal_4(p->y, t1))) {
        sp_256_proj_point_dbl_4(r, p, t);
    }
    else {
        rp[0] = r;
        rp[1] = (sp_point*)t;
        XMEMSET(rp[1], 0, sizeof(sp_point));
        x = rp[p->infinity | q->infinity]->x;
        y = rp[p->infinity | q->infinity]->y;
        z = rp[p->infinity | q->infinity]->z;

        ap[0] = p;
        ap[1] = q;
        for (i=0; i<4; i++)
            r->x[i] = ap[p->infinity]->x[i];
        for (i=0; i<4; i++)
            r->y[i] = ap[p->infinity]->y[i];
        for (i=0; i<4; i++)
            r->z[i] = ap[p->infinity]->z[i];
        r->infinity = ap[p->infinity]->infinity;

        /* U2 = X2*Z1^2 */
        sp_256_mont_sqr_4(t2, z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(t4, t2, z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(t2, t2, q->x, p256_mod, p256_mp_mod);
        /* S2 = Y2*Z1^3 */
        sp_256_mont_mul_4(t4, t4, q->y, p256_mod, p256_mp_mod);
        /* H = U2 - X1 */
        sp_256_mont_sub_4(t2, t2, x, p256_mod);
        /* R = S2 - Y1 */
        sp_256_mont_sub_4(t4, t4, y, p256_mod);
        /* Z3 = H*Z1 */
        sp_256_mont_mul_4(z, z, t2, p256_mod, p256_mp_mod);
        /* X3 = R^2 - H^3 - 2*X1*H^2 */
        sp_256_mont_sqr_4(t1, t4, p256_mod, p256_mp_mod);
        sp_256_mont_sqr_4(t5, t2, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(t3, x, t5, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(t5, t5, t2, p256_mod, p256_mp_mod);
        sp_256_mont_sub_4(x, t1, t5, p256_mod);
        sp_256_mont_dbl_4(t1, t3, p256_mod);
        sp_256_mont_sub_4(x, x, t1, p256_mod);
        /* Y3 = R*(X1*H^2 - X3) - Y1*H^3 */
        sp_256_mont_sub_4(t3, t3, x, p256_mod);
        sp_256_mont_mul_4(t3, t3, t4, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(t5, t5, y, p256_mod, p256_mp_mod);
        sp_256_mont_sub_4(y, t3, t5, p256_mod);
    }
}

#ifdef FP_ECC
/* Convert the projective point to affine.
 * Ordinates are in Montgomery form.
 *
 * a  Point to convert.
 * t  Temprorary data.
 */
static void sp_256_proj_to_affine_4(sp_point* a, sp_digit* t)
{
    sp_digit* t1 = t;
    sp_digit* t2 = t + 2 * 4;
    sp_digit* tmp = t + 4 * 4;

    sp_256_mont_inv_4(t1, a->z, tmp);

    sp_256_mont_sqr_4(t2, t1, p256_mod, p256_mp_mod);
    sp_256_mont_mul_4(t1, t2, t1, p256_mod, p256_mp_mod);

    sp_256_mont_mul_4(a->x, a->x, t2, p256_mod, p256_mp_mod);
    sp_256_mont_mul_4(a->y, a->y, t1, p256_mod, p256_mp_mod);
    XMEMCPY(a->z, p256_norm_mod, sizeof(p256_norm_mod));
}

/* Generate the pre-computed table of points for the base point.
 *
 * a      The base point.
 * table  Place to store generated point data.
 * tmp    Temprorary data.
 * heap  Heap to use for allocation.
 */
static int sp_256_gen_stripe_table_4(const sp_point* a,
        sp_table_entry* table, sp_digit* tmp, void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point td, s1d, s2d;
#endif
    sp_point* t;
    sp_point* s1 = NULL;
    sp_point* s2 = NULL;
    int i, j;
    int err;

    (void)heap;

    err = sp_ecc_point_new(heap, td, t);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, s1d, s1);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, s2d, s2);

    if (err == MP_OKAY)
        err = sp_256_mod_mul_norm_4(t->x, a->x, p256_mod);
    if (err == MP_OKAY)
        err = sp_256_mod_mul_norm_4(t->y, a->y, p256_mod);
    if (err == MP_OKAY)
        err = sp_256_mod_mul_norm_4(t->z, a->z, p256_mod);
    if (err == MP_OKAY) {
        t->infinity = 0;
        sp_256_proj_to_affine_4(t, tmp);

        XMEMCPY(s1->z, p256_norm_mod, sizeof(p256_norm_mod));
        s1->infinity = 0;
        XMEMCPY(s2->z, p256_norm_mod, sizeof(p256_norm_mod));
        s2->infinity = 0;

        /* table[0] = {0, 0, infinity} */
        XMEMSET(&table[0], 0, sizeof(sp_table_entry));
        /* table[1] = Affine version of 'a' in Montgomery form */
        XMEMCPY(table[1].x, t->x, sizeof(table->x));
        XMEMCPY(table[1].y, t->y, sizeof(table->y));

        for (i=1; i<8; i++) {
            sp_256_proj_point_dbl_n_4(t, t, 32, tmp);
            sp_256_proj_to_affine_4(t, tmp);
            XMEMCPY(table[1<<i].x, t->x, sizeof(table->x));
            XMEMCPY(table[1<<i].y, t->y, sizeof(table->y));
        }

        for (i=1; i<8; i++) {
            XMEMCPY(s1->x, table[1<<i].x, sizeof(table->x));
            XMEMCPY(s1->y, table[1<<i].y, sizeof(table->y));
            for (j=(1<<i)+1; j<(1<<(i+1)); j++) {
                XMEMCPY(s2->x, table[j-(1<<i)].x, sizeof(table->x));
                XMEMCPY(s2->y, table[j-(1<<i)].y, sizeof(table->y));
                sp_256_proj_point_add_qz1_4(t, s1, s2, tmp);
                sp_256_proj_to_affine_4(t, tmp);
                XMEMCPY(table[j].x, t->x, sizeof(table->x));
                XMEMCPY(table[j].y, t->y, sizeof(table->y));
            }
        }
    }

    sp_ecc_point_free(s2, 0, heap);
    sp_ecc_point_free(s1, 0, heap);
    sp_ecc_point_free( t, 0, heap);

    return err;
}

#endif /* FP_ECC */
#if defined(FP_ECC) || defined(WOLFSSL_SP_SMALL)
/* Multiply the point by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * r     Resulting point.
 * k     Scalar to multiply by.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
static int sp_256_ecc_mulmod_stripe_4(sp_point* r, const sp_point* g,
        const sp_table_entry* table, const sp_digit* k, int map, void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point rtd;
    sp_point pd;
    sp_digit td[2 * 4 * 5];
#endif
    sp_point* rt;
    sp_point* p = NULL;
    sp_digit* t;
    int i, j;
    int y, x;
    int err;

    (void)g;
    (void)heap;

    err = sp_ecc_point_new(heap, rtd, rt);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, pd, p);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    t = (sp_digit*)XMALLOC(sizeof(sp_digit) * 2 * 4 * 5, heap,
                           DYNAMIC_TYPE_ECC);
    if (t == NULL)
        err = MEMORY_E;
#else
    t = td;
#endif

    if (err == MP_OKAY) {
        XMEMCPY(p->z, p256_norm_mod, sizeof(p256_norm_mod));
        XMEMCPY(rt->z, p256_norm_mod, sizeof(p256_norm_mod));

        y = 0;
        for (j=0,x=31; j<8; j++,x+=32)
            y |= ((k[x / 64] >> (x % 64)) & 1) << j;
        XMEMCPY(rt->x, table[y].x, sizeof(table[y].x));
        XMEMCPY(rt->y, table[y].y, sizeof(table[y].y));
        rt->infinity = !y;
        for (i=30; i>=0; i--) {
            y = 0;
            for (j=0,x=i; j<8; j++,x+=32)
                y |= ((k[x / 64] >> (x % 64)) & 1) << j;

            sp_256_proj_point_dbl_4(rt, rt, t);
            XMEMCPY(p->x, table[y].x, sizeof(table[y].x));
            XMEMCPY(p->y, table[y].y, sizeof(table[y].y));
            p->infinity = !y;
            sp_256_proj_point_add_qz1_4(rt, rt, p, t);
        }

        if (map)
            sp_256_map_4(r, rt, t);
        else
            XMEMCPY(r, rt, sizeof(sp_point));
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (t != NULL)
        XFREE(t, heap, DYNAMIC_TYPE_ECC);
#endif
    sp_ecc_point_free(p, 0, heap);
    sp_ecc_point_free(rt, 0, heap);

    return err;
}

#endif /* FP_ECC || WOLFSSL_SP_SMALL */
#ifdef FP_ECC
#ifndef FP_ENTRIES
    #define FP_ENTRIES 16
#endif

typedef struct sp_cache_t {
    sp_digit x[4];
    sp_digit y[4];
    sp_table_entry table[256];
    uint32_t cnt;
    int set;
} sp_cache_t;

static THREAD_LS_T sp_cache_t sp_cache[FP_ENTRIES];
static THREAD_LS_T int sp_cache_last = -1;
static THREAD_LS_T int sp_cache_inited = 0;

#ifndef HAVE_THREAD_LS
    static volatile int initCacheMutex = 0;
    static wolfSSL_Mutex sp_cache_lock;
#endif

static void sp_ecc_get_cache(const sp_point* g, sp_cache_t** cache)
{
    int i, j;
    uint32_t least;

    if (sp_cache_inited == 0) {
        for (i=0; i<FP_ENTRIES; i++) {
            sp_cache[i].set = 0;
        }
        sp_cache_inited = 1;
    }

    /* Compare point with those in cache. */
    for (i=0; i<FP_ENTRIES; i++) {
        if (!sp_cache[i].set)
            continue;

        if (sp_256_cmp_equal_4(g->x, sp_cache[i].x) &
                           sp_256_cmp_equal_4(g->y, sp_cache[i].y)) {
            sp_cache[i].cnt++;
            break;
        }
    }

    /* No match. */
    if (i == FP_ENTRIES) {
        /* Find empty entry. */
        i = (sp_cache_last + 1) % FP_ENTRIES;
        for (; i != sp_cache_last; i=(i+1)%FP_ENTRIES) {
            if (!sp_cache[i].set) {
                break;
            }
        }

        /* Evict least used. */
        if (i == sp_cache_last) {
            least = sp_cache[0].cnt;
            for (j=1; j<FP_ENTRIES; j++) {
                if (sp_cache[j].cnt < least) {
                    i = j;
                    least = sp_cache[i].cnt;
                }
            }
        }

        XMEMCPY(sp_cache[i].x, g->x, sizeof(sp_cache[i].x));
        XMEMCPY(sp_cache[i].y, g->y, sizeof(sp_cache[i].y));
        sp_cache[i].set = 1;
        sp_cache[i].cnt = 1;
    }

    *cache = &sp_cache[i];
    sp_cache_last = i;
}
#endif /* FP_ECC */

/* Multiply the base point of P256 by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * r     Resulting point.
 * g     Point to multiply.
 * k     Scalar to multiply by.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
static int sp_256_ecc_mulmod_4(sp_point* r, const sp_point* g, const sp_digit* k,
        int map, void* heap)
{
#ifndef FP_ECC
    return sp_256_ecc_mulmod_win_add_sub_4(r, g, k, map, heap);
#else
    sp_digit tmp[2 * 4 * 5];
    sp_cache_t* cache;
    int err = MP_OKAY;

#ifndef HAVE_THREAD_LS
    if (initCacheMutex == 0) {
         wc_InitMutex(&sp_cache_lock);
         initCacheMutex = 1;
    }
    if (wc_LockMutex(&sp_cache_lock) != 0)
       err = BAD_MUTEX_E;
#endif /* HAVE_THREAD_LS */

    if (err == MP_OKAY) {
        sp_ecc_get_cache(g, &cache);
        if (cache->cnt == 2)
            sp_256_gen_stripe_table_4(g, cache->table, tmp, heap);

#ifndef HAVE_THREAD_LS
        wc_UnLockMutex(&sp_cache_lock);
#endif /* HAVE_THREAD_LS */

        if (cache->cnt < 2) {
            err = sp_256_ecc_mulmod_win_add_sub_4(r, g, k, map, heap);
        }
        else {
            err = sp_256_ecc_mulmod_stripe_4(r, g, cache->table, k,
                    map, heap);
        }
    }

    return err;
#endif
}

#ifdef HAVE_INTEL_AVX2
#if defined(FP_ECC) || defined(WOLFSSL_SP_SMALL)
#endif /* FP_ECC || WOLFSSL_SP_SMALL */
/* Add two Montgomery form projective points. The second point has a q value of
 * one.
 * Only the first point can be the same pointer as the result point.
 *
 * r  Result of addition.
 * p  Frist point to add.
 * q  Second point to add.
 * t  Temporary ordinate data.
 */
static void sp_256_proj_point_add_qz1_avx2_4(sp_point* r, const sp_point* p,
        const sp_point* q, sp_digit* t)
{
    const sp_point* ap[2];
    sp_point* rp[2];
    sp_digit* t1 = t;
    sp_digit* t2 = t + 2*4;
    sp_digit* t3 = t + 4*4;
    sp_digit* t4 = t + 6*4;
    sp_digit* t5 = t + 8*4;
    sp_digit* x;
    sp_digit* y;
    sp_digit* z;
    int i;

    /* Check double */
    sp_256_sub_4(t1, p256_mod, q->y);
    sp_256_norm_4(t1);
    if (sp_256_cmp_equal_4(p->x, q->x) & sp_256_cmp_equal_4(p->z, q->z) &
        (sp_256_cmp_equal_4(p->y, q->y) | sp_256_cmp_equal_4(p->y, t1))) {
        sp_256_proj_point_dbl_4(r, p, t);
    }
    else {
        rp[0] = r;
        rp[1] = (sp_point*)t;
        XMEMSET(rp[1], 0, sizeof(sp_point));
        x = rp[p->infinity | q->infinity]->x;
        y = rp[p->infinity | q->infinity]->y;
        z = rp[p->infinity | q->infinity]->z;

        ap[0] = p;
        ap[1] = q;
        for (i=0; i<4; i++)
            r->x[i] = ap[p->infinity]->x[i];
        for (i=0; i<4; i++)
            r->y[i] = ap[p->infinity]->y[i];
        for (i=0; i<4; i++)
            r->z[i] = ap[p->infinity]->z[i];
        r->infinity = ap[p->infinity]->infinity;

        /* U2 = X2*Z1^2 */
        sp_256_mont_sqr_avx2_4(t2, z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(t4, t2, z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(t2, t2, q->x, p256_mod, p256_mp_mod);
        /* S2 = Y2*Z1^3 */
        sp_256_mont_mul_avx2_4(t4, t4, q->y, p256_mod, p256_mp_mod);
        /* H = U2 - X1 */
        sp_256_mont_sub_4(t2, t2, x, p256_mod);
        /* R = S2 - Y1 */
        sp_256_mont_sub_4(t4, t4, y, p256_mod);
        /* Z3 = H*Z1 */
        sp_256_mont_mul_avx2_4(z, z, t2, p256_mod, p256_mp_mod);
        /* X3 = R^2 - H^3 - 2*X1*H^2 */
        sp_256_mont_sqr_avx2_4(t1, t4, p256_mod, p256_mp_mod);
        sp_256_mont_sqr_avx2_4(t5, t2, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(t3, x, t5, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(t5, t5, t2, p256_mod, p256_mp_mod);
        sp_256_mont_sub_4(x, t1, t5, p256_mod);
        sp_256_mont_dbl_4(t1, t3, p256_mod);
        sp_256_mont_sub_4(x, x, t1, p256_mod);
        /* Y3 = R*(X1*H^2 - X3) - Y1*H^3 */
        sp_256_mont_sub_4(t3, t3, x, p256_mod);
        sp_256_mont_mul_avx2_4(t3, t3, t4, p256_mod, p256_mp_mod);
        sp_256_mont_mul_avx2_4(t5, t5, y, p256_mod, p256_mp_mod);
        sp_256_mont_sub_4(y, t3, t5, p256_mod);
    }
}

#ifdef FP_ECC
/* Convert the projective point to affine.
 * Ordinates are in Montgomery form.
 *
 * a  Point to convert.
 * t  Temprorary data.
 */
static void sp_256_proj_to_affine_avx2_4(sp_point* a, sp_digit* t)
{
    sp_digit* t1 = t;
    sp_digit* t2 = t + 2 * 4;
    sp_digit* tmp = t + 4 * 4;

    sp_256_mont_inv_avx2_4(t1, a->z, tmp);

    sp_256_mont_sqr_avx2_4(t2, t1, p256_mod, p256_mp_mod);
    sp_256_mont_mul_avx2_4(t1, t2, t1, p256_mod, p256_mp_mod);

    sp_256_mont_mul_avx2_4(a->x, a->x, t2, p256_mod, p256_mp_mod);
    sp_256_mont_mul_avx2_4(a->y, a->y, t1, p256_mod, p256_mp_mod);
    XMEMCPY(a->z, p256_norm_mod, sizeof(p256_norm_mod));
}

/* Generate the pre-computed table of points for the base point.
 *
 * a      The base point.
 * table  Place to store generated point data.
 * tmp    Temprorary data.
 * heap  Heap to use for allocation.
 */
static int sp_256_gen_stripe_table_avx2_4(const sp_point* a,
        sp_table_entry* table, sp_digit* tmp, void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point td, s1d, s2d;
#endif
    sp_point* t;
    sp_point* s1 = NULL;
    sp_point* s2 = NULL;
    int i, j;
    int err;

    (void)heap;

    err = sp_ecc_point_new(heap, td, t);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, s1d, s1);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, s2d, s2);

    if (err == MP_OKAY)
        err = sp_256_mod_mul_norm_4(t->x, a->x, p256_mod);
    if (err == MP_OKAY)
        err = sp_256_mod_mul_norm_4(t->y, a->y, p256_mod);
    if (err == MP_OKAY)
        err = sp_256_mod_mul_norm_4(t->z, a->z, p256_mod);
    if (err == MP_OKAY) {
        t->infinity = 0;
        sp_256_proj_to_affine_avx2_4(t, tmp);

        XMEMCPY(s1->z, p256_norm_mod, sizeof(p256_norm_mod));
        s1->infinity = 0;
        XMEMCPY(s2->z, p256_norm_mod, sizeof(p256_norm_mod));
        s2->infinity = 0;

        /* table[0] = {0, 0, infinity} */
        XMEMSET(&table[0], 0, sizeof(sp_table_entry));
        /* table[1] = Affine version of 'a' in Montgomery form */
        XMEMCPY(table[1].x, t->x, sizeof(table->x));
        XMEMCPY(table[1].y, t->y, sizeof(table->y));

        for (i=1; i<8; i++) {
            sp_256_proj_point_dbl_n_avx2_4(t, t, 32, tmp);
            sp_256_proj_to_affine_avx2_4(t, tmp);
            XMEMCPY(table[1<<i].x, t->x, sizeof(table->x));
            XMEMCPY(table[1<<i].y, t->y, sizeof(table->y));
        }

        for (i=1; i<8; i++) {
            XMEMCPY(s1->x, table[1<<i].x, sizeof(table->x));
            XMEMCPY(s1->y, table[1<<i].y, sizeof(table->y));
            for (j=(1<<i)+1; j<(1<<(i+1)); j++) {
                XMEMCPY(s2->x, table[j-(1<<i)].x, sizeof(table->x));
                XMEMCPY(s2->y, table[j-(1<<i)].y, sizeof(table->y));
                sp_256_proj_point_add_qz1_avx2_4(t, s1, s2, tmp);
                sp_256_proj_to_affine_avx2_4(t, tmp);
                XMEMCPY(table[j].x, t->x, sizeof(table->x));
                XMEMCPY(table[j].y, t->y, sizeof(table->y));
            }
        }
    }

    sp_ecc_point_free(s2, 0, heap);
    sp_ecc_point_free(s1, 0, heap);
    sp_ecc_point_free( t, 0, heap);

    return err;
}

#endif /* FP_ECC */
#if defined(FP_ECC) || defined(WOLFSSL_SP_SMALL)
/* Multiply the point by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * r     Resulting point.
 * k     Scalar to multiply by.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
static int sp_256_ecc_mulmod_stripe_avx2_4(sp_point* r, const sp_point* g,
        const sp_table_entry* table, const sp_digit* k, int map, void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point rtd;
    sp_point pd;
    sp_digit td[2 * 4 * 5];
#endif
    sp_point* rt;
    sp_point* p = NULL;
    sp_digit* t;
    int i, j;
    int y, x;
    int err;

    (void)g;
    (void)heap;

    err = sp_ecc_point_new(heap, rtd, rt);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, pd, p);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    t = (sp_digit*)XMALLOC(sizeof(sp_digit) * 2 * 4 * 5, heap,
                           DYNAMIC_TYPE_ECC);
    if (t == NULL)
        err = MEMORY_E;
#else
    t = td;
#endif

    if (err == MP_OKAY) {
        XMEMCPY(p->z, p256_norm_mod, sizeof(p256_norm_mod));
        XMEMCPY(rt->z, p256_norm_mod, sizeof(p256_norm_mod));

        y = 0;
        for (j=0,x=31; j<8; j++,x+=32)
            y |= ((k[x / 64] >> (x % 64)) & 1) << j;
        XMEMCPY(rt->x, table[y].x, sizeof(table[y].x));
        XMEMCPY(rt->y, table[y].y, sizeof(table[y].y));
        rt->infinity = !y;
        for (i=30; i>=0; i--) {
            y = 0;
            for (j=0,x=i; j<8; j++,x+=32)
                y |= ((k[x / 64] >> (x % 64)) & 1) << j;

            sp_256_proj_point_dbl_avx2_4(rt, rt, t);
            XMEMCPY(p->x, table[y].x, sizeof(table[y].x));
            XMEMCPY(p->y, table[y].y, sizeof(table[y].y));
            p->infinity = !y;
            sp_256_proj_point_add_qz1_avx2_4(rt, rt, p, t);
        }

        if (map)
            sp_256_map_avx2_4(r, rt, t);
        else
            XMEMCPY(r, rt, sizeof(sp_point));
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (t != NULL)
        XFREE(t, heap, DYNAMIC_TYPE_ECC);
#endif
    sp_ecc_point_free(p, 0, heap);
    sp_ecc_point_free(rt, 0, heap);

    return err;
}

#endif /* FP_ECC || WOLFSSL_SP_SMALL */
/* Multiply the base point of P256 by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * r     Resulting point.
 * g     Point to multiply.
 * k     Scalar to multiply by.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
static int sp_256_ecc_mulmod_avx2_4(sp_point* r, const sp_point* g, const sp_digit* k,
        int map, void* heap)
{
#ifndef FP_ECC
    return sp_256_ecc_mulmod_win_add_sub_avx2_4(r, g, k, map, heap);
#else
    sp_digit tmp[2 * 4 * 5];
    sp_cache_t* cache;
    int err = MP_OKAY;

#ifndef HAVE_THREAD_LS
    if (initCacheMutex == 0) {
         wc_InitMutex(&sp_cache_lock);
         initCacheMutex = 1;
    }
    if (wc_LockMutex(&sp_cache_lock) != 0)
       err = BAD_MUTEX_E;
#endif /* HAVE_THREAD_LS */

    if (err == MP_OKAY) {
        sp_ecc_get_cache(g, &cache);
        if (cache->cnt == 2)
            sp_256_gen_stripe_table_avx2_4(g, cache->table, tmp, heap);

#ifndef HAVE_THREAD_LS
        wc_UnLockMutex(&sp_cache_lock);
#endif /* HAVE_THREAD_LS */

        if (cache->cnt < 2) {
            err = sp_256_ecc_mulmod_win_add_sub_avx2_4(r, g, k, map, heap);
        }
        else {
            err = sp_256_ecc_mulmod_stripe_avx2_4(r, g, cache->table, k,
                    map, heap);
        }
    }

    return err;
#endif
}

#endif /* HAVE_INTEL_AVX2 */
/* Multiply the point by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * km    Scalar to multiply by.
 * p     Point to multiply.
 * r     Resulting point.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
int sp_ecc_mulmod_256(mp_int* km, ecc_point* gm, ecc_point* r, int map,
        void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point p;
    sp_digit kd[4];
#endif
    sp_point* point;
    sp_digit* k = NULL;
    int err = MP_OKAY;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    err = sp_ecc_point_new(heap, p, point);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        k = (sp_digit*)XMALLOC(sizeof(sp_digit) * 4, heap,
                                                              DYNAMIC_TYPE_ECC);
        if (k == NULL)
            err = MEMORY_E;
    }
#else
    k = kd;
#endif
    if (err == MP_OKAY) {
        sp_256_from_mp(k, 4, km);
        sp_256_point_from_ecc_point_4(point, gm);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_256_ecc_mulmod_avx2_4(point, point, k, map, heap);
        else
#endif
            err = sp_256_ecc_mulmod_4(point, point, k, map, heap);
    }
    if (err == MP_OKAY)
        err = sp_256_point_to_ecc_point_4(point, r);

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (k != NULL)
        XFREE(k, heap, DYNAMIC_TYPE_ECC);
#endif
    sp_ecc_point_free(point, 0, heap);

    return err;
}

#ifdef WOLFSSL_SP_SMALL
static const sp_table_entry p256_table[256] = {
    /* 0 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 */
    { { 0x79e730d418a9143cl,0x75ba95fc5fedb601l,0x79fb732b77622510l,
        0x18905f76a53755c6l },
      { 0xddf25357ce95560al,0x8b4ab8e4ba19e45cl,0xd2e88688dd21f325l,
        0x8571ff1825885d85l } },
    /* 2 */
    { { 0x202886024147519al,0xd0981eac26b372f0l,0xa9d4a7caa785ebc8l,
        0xd953c50ddbdf58e9l },
      { 0x9d6361ccfd590f8fl,0x72e9626b44e6c917l,0x7fd9611022eb64cfl,
        0x863ebb7e9eb288f3l } },
    /* 3 */
    { { 0x7856b6235cdb6485l,0x808f0ea22f0a2f97l,0x3e68d9544f7e300bl,
        0x00076055b5ff80a0l },
      { 0x7634eb9b838d2010l,0x54014fbb3243708al,0xe0e47d39842a6606l,
        0x8308776134373ee0l } },
    /* 4 */
    { { 0x4f922fc516a0d2bbl,0x0d5cc16c1a623499l,0x9241cf3a57c62c8bl,
        0x2f5e6961fd1b667fl },
      { 0x5c15c70bf5a01797l,0x3d20b44d60956192l,0x04911b37071fdb52l,
        0xf648f9168d6f0f7bl } },
    /* 5 */
    { { 0x9e566847e137bbbcl,0xe434469e8a6a0becl,0xb1c4276179d73463l,
        0x5abe0285133d0015l },
      { 0x92aa837cc04c7dabl,0x573d9f4c43260c07l,0x0c93156278e6cc37l,
        0x94bb725b6b6f7383l } },
    /* 6 */
    { { 0xbbf9b48f720f141cl,0x6199b3cd2df5bc74l,0xdc3f6129411045c4l,
        0xcdd6bbcb2f7dc4efl },
      { 0xcca6700beaf436fdl,0x6f647f6db99326bel,0x0c0fa792014f2522l,
        0xa361bebd4bdae5f6l } },
    /* 7 */
    { { 0x28aa2558597c13c7l,0xc38d635f50b7c3e1l,0x07039aecf3c09d1dl,
        0xba12ca09c4b5292cl },
      { 0x9e408fa459f91dfdl,0x3af43b66ceea07fbl,0x1eceb0899d780b29l,
        0x53ebb99d701fef4bl } },
    /* 8 */
    { { 0x4fe7ee31b0e63d34l,0xf4600572a9e54fabl,0xc0493334d5e7b5a4l,
        0x8589fb9206d54831l },
      { 0xaa70f5cc6583553al,0x0879094ae25649e5l,0xcc90450710044652l,
        0xebb0696d02541c4fl } },
    /* 9 */
    { { 0x4616ca15ac1647c5l,0xb8127d47c4cf5799l,0xdc666aa3764dfbacl,
        0xeb2820cbd1b27da3l },
      { 0x9406f8d86a87e008l,0xd87dfa9d922378f3l,0x56ed2e4280ccecb2l,
        0x1f28289b55a7da1dl } },
    /* 10 */
    { { 0xabbaa0c03b89da99l,0xa6f2d79eb8284022l,0x27847862b81c05e8l,
        0x337a4b5905e54d63l },
      { 0x3c67500d21f7794al,0x207005b77d6d7f61l,0x0a5a378104cfd6e8l,
        0x0d65e0d5f4c2fbd6l } },
    /* 11 */
    { { 0xd9d09bbeb5275d38l,0x4268a7450be0a358l,0xf0762ff4973eb265l,
        0xc23da24252f4a232l },
      { 0x5da1b84f0b94520cl,0x09666763b05bd78el,0x3a4dcb8694d29ea1l,
        0x19de3b8cc790cff1l } },
    /* 12 */
    { { 0x183a716c26c5fe04l,0x3b28de0b3bba1bdbl,0x7432c586a4cb712cl,
        0xe34dcbd491fccbfdl },
      { 0xb408d46baaa58403l,0x9a69748682e97a53l,0x9e39012736aaa8afl,
        0xe7641f447b4e0f7fl } },
    /* 13 */
    { { 0x7d753941df64ba59l,0xd33f10ec0b0242fcl,0x4f06dfc6a1581859l,
        0x4a12df57052a57bfl },
      { 0xbfa6338f9439dbd0l,0xd3c24bd4bde53e1fl,0xfd5e4ffa21f1b314l,
        0x6af5aa93bb5bea46l } },
    /* 14 */
    { { 0xda10b69910c91999l,0x0a24b4402a580491l,0x3e0094b4b8cc2090l,
        0x5fe3475a66a44013l },
      { 0xb0f8cabdf93e7b4bl,0x292b501a7c23f91al,0x42e889aecd1e6263l,
        0xb544e308ecfea916l } },
    /* 15 */
    { { 0x6478c6e916ddfdcel,0x2c329166f89179e6l,0x4e8d6e764d4e67e1l,
        0xe0b6b2bda6b0c20bl },
      { 0x0d312df2bb7efb57l,0x1aac0dde790c4007l,0xf90336ad679bc944l,
        0x71c023de25a63774l } },
    /* 16 */
    { { 0x62a8c244bfe20925l,0x91c19ac38fdce867l,0x5a96a5d5dd387063l,
        0x61d587d421d324f6l },
      { 0xe87673a2a37173eal,0x2384800853778b65l,0x10f8441e05bab43el,
        0xfa11fe124621efbel } },
    /* 17 */
    { { 0x1c891f2b2cb19ffdl,0x01ba8d5bb1923c23l,0xb6d03d678ac5ca8el,
        0x586eb04c1f13bedcl },
      { 0x0c35c6e527e8ed09l,0x1e81a33c1819ede2l,0x278fd6c056c652fal,
        0x19d5ac0870864f11l } },
    /* 18 */
    { { 0x1e99f581309a4e1fl,0xab7de71be9270074l,0x26a5ef0befd28d20l,
        0xe7c0073f7f9c563fl },
      { 0x1f6d663a0ef59f76l,0x669b3b5420fcb050l,0xc08c1f7a7a6602d4l,
        0xe08504fec65b3c0al } },
    /* 19 */
    { { 0xf098f68da031b3cal,0x6d1cab9ee6da6d66l,0x5bfd81fa94f246e8l,
        0x78f018825b0996b4l },
      { 0xb7eefde43a25787fl,0x8016f80d1dccac9bl,0x0cea4877b35bfc36l,
        0x43a773b87e94747al } },
    /* 20 */
    { { 0x62577734d2b533d5l,0x673b8af6a1bdddc0l,0x577e7c9aa79ec293l,
        0xbb6de651c3b266b1l },
      { 0xe7e9303ab65259b3l,0xd6a0afd3d03a7480l,0xc5ac83d19b3cfc27l,
        0x60b4619a5d18b99bl } },
    /* 21 */
    { { 0xbd6a38e11ae5aa1cl,0xb8b7652b49e73658l,0x0b130014ee5f87edl,
        0x9d0f27b2aeebffcdl },
      { 0xca9246317a730a55l,0x9c955b2fddbbc83al,0x07c1dfe0ac019a71l,
        0x244a566d356ec48dl } },
    /* 22 */
    { { 0x6db0394aeacf1f96l,0x9f2122a9024c271cl,0x2626ac1b82cbd3b9l,
        0x45e58c873581ef69l },
      { 0xd3ff479da38f9dbcl,0xa8aaf146e888a040l,0x945adfb246e0bed7l,
        0xc040e21cc1e4b7a4l } },
    /* 23 */
    { { 0x847af0006f8117b6l,0x651969ff73a35433l,0x482b35761d9475ebl,
        0x1cdf5c97682c6ec7l },
      { 0x7db775b411f04839l,0x7dbeacf448de1698l,0xb2921dd1b70b3219l,
        0x046755f8a92dff3dl } },
    /* 24 */
    { { 0xcc8ac5d2bce8ffcdl,0x0d53c48b2fe61a82l,0xf6f161727202d6c7l,
        0x046e5e113b83a5f3l },
      { 0xe7b8ff64d8007f01l,0x7fb1ef125af43183l,0x045c5ea635e1a03cl,
        0x6e0106c3303d005bl } },
    /* 25 */
    { { 0x48c7358488dd73b1l,0x7670708f995ed0d9l,0x38385ea8c56a2ab7l,
        0x442594ede901cf1fl },
      { 0xf8faa2c912d4b65bl,0x94c2343b96c90c37l,0xd326e4a15e978d1fl,
        0xa796fa514c2ee68el } },
    /* 26 */
    { { 0x359fb604823addd7l,0x9e2a6183e56693b3l,0xf885b78e3cbf3c80l,
        0xe4ad2da9c69766e9l },
      { 0x357f7f428e048a61l,0x082d198cc092d9a0l,0xfc3a1af4c03ed8efl,
        0xc5e94046c37b5143l } },
    /* 27 */
    { { 0x476a538c2be75f9el,0x6fd1a9e8cb123a78l,0xd85e4df0b109c04bl,
        0x63283dafdb464747l },
      { 0xce728cf7baf2df15l,0xe592c4550ad9a7f4l,0xfab226ade834bcc3l,
        0x68bd19ab1981a938l } },
    /* 28 */
    { { 0xc08ead511887d659l,0x3374d5f4b359305al,0x96986981cfe74fe3l,
        0x495292f53c6fdfd6l },
      { 0x4a878c9e1acec896l,0xd964b210ec5b4484l,0x6696f7e2664d60a7l,
        0x0ec7530d26036837l } },
    /* 29 */
    { { 0x2da13a05ad2687bbl,0xa1f83b6af32e21fal,0x390f5ef51dd4607bl,
        0x0f6207a664863f0bl },
      { 0xbd67e3bb0f138233l,0xdd66b96c272aa718l,0x8ed0040726ec88ael,
        0xff0db07208ed6dcfl } },
    /* 30 */
    { { 0x749fa1014c95d553l,0xa44052fd5d680a8al,0x183b4317ff3b566fl,
        0x313b513c88740ea3l },
      { 0xb402e2ac08d11549l,0x071ee10bb4dee21cl,0x26b987dd47f2320el,
        0x2d3abcf986f19f81l } },
    /* 31 */
    { { 0x4c288501815581a2l,0x9a0a6d56632211afl,0x19ba7a0f0cab2e99l,
        0xc036fa10ded98cdfl },
      { 0x29ae08bac1fbd009l,0x0b68b19006d15816l,0xc2eb32779b9e0d8fl,
        0xa6b2a2c4b6d40194l } },
    /* 32 */
    { { 0xd433e50f6d3549cfl,0x6f33696ffacd665el,0x695bfdacce11fcb4l,
        0x810ee252af7c9860l },
      { 0x65450fe17159bb2cl,0xf7dfbebe758b357bl,0x2b057e74d69fea72l,
        0xd485717a92731745l } },
    /* 33 */
    { { 0x11741a8af0cb5a98l,0xd3da8f931f3110bfl,0x1994e2cbab382adfl,
        0x6a6045a72f9a604el },
      { 0x170c0d3fa2b2411dl,0xbe0eb83e510e96e0l,0x3bcc9f738865b3ccl,
        0xd3e45cfaf9e15790l } },
    /* 34 */
    { { 0xce1f69bbe83f7669l,0x09f8ae8272877d6bl,0x9548ae543244278dl,
        0x207755dee3c2c19cl },
      { 0x87bd61d96fef1945l,0x18813cefb12d28c3l,0x9fbcd1d672df64aal,
        0x48dc5ee57154b00dl } },
    /* 35 */
    { { 0x123790bff7e5a199l,0xe0efb8cf989ccbb7l,0xc27a2bfe0a519c79l,
        0xf2fb0aeddff6f445l },
      { 0x41c09575f0b5025fl,0x550543d740fa9f22l,0x8fa3c8ad380bfbd0l,
        0xa13e9015db28d525l } },
    /* 36 */
    { { 0xf9f7a350a2b65cbcl,0x0b04b9722a464226l,0x265ce241e23f07a1l,
        0x2bf0d6b01497526fl },
      { 0xd3d4dd3f4b216fb7l,0xf7d7b867fbdda26al,0xaeb7b83f6708505cl,
        0x42a94a5a162fe89fl } },
    /* 37 */
    { { 0x5846ad0beaadf191l,0x0f8a489025a268d7l,0xe8603050494dc1f6l,
        0x2c2dd969c65ede3dl },
      { 0x6d02171d93849c17l,0x460488ba1da250ddl,0x4810c7063c3a5485l,
        0xf437fa1f42c56dbcl } },
    /* 38 */
    { { 0x6aa0d7144a0f7dabl,0x0f0497931776e9acl,0x52c0a050f5f39786l,
        0xaaf45b3354707aa8l },
      { 0x85e37c33c18d364al,0xd40b9b063e497165l,0xf417168115ec5444l,
        0xcdf6310df4f272bcl } },
    /* 39 */
    { { 0x7473c6238ea8b7efl,0x08e9351885bc2287l,0x419567722bda8e34l,
        0xf0d008bada9e2ff2l },
      { 0x2912671d2414d3b1l,0xb3754985b019ea76l,0x5c61b96d453bcbdbl,
        0x5bd5c2f5ca887b8bl } },
    /* 40 */
    { { 0xef0f469ef49a3154l,0x3e85a5956e2b2e9al,0x45aaec1eaa924a9cl,
        0xaa12dfc8a09e4719l },
      { 0x26f272274df69f1dl,0xe0e4c82ca2ff5e73l,0xb9d8ce73b7a9dd44l,
        0x6c036e73e48ca901l } },
    /* 41 */
    { { 0x5cfae12a0f6e3138l,0x6966ef0025ad345al,0x8993c64b45672bc5l,
        0x292ff65896afbe24l },
      { 0xd5250d445e213402l,0xf6580e274392c9fel,0x097b397fda1c72e8l,
        0x644e0c90311b7276l } },
    /* 42 */
    { { 0xe1e421e1a47153f0l,0xb86c3b79920418c9l,0x93bdce87705d7672l,
        0xf25ae793cab79a77l },
      { 0x1f3194a36d869d0cl,0x9d55c8824986c264l,0x49fb5ea3096e945el,
        0x39b8e65313db0a3el } },
    /* 43 */
    { { 0x37754200b6fd2e59l,0x35e2c0669255c98fl,0xd9dab21a0e2a5739l,
        0x39122f2f0f19db06l },
      { 0xcfbce1e003cad53cl,0x225b2c0fe65c17e3l,0x72baf1d29aa13877l,
        0x8de80af8ce80ff8dl } },
    /* 44 */
    { { 0xafbea8d9207bbb76l,0x921c7e7c21782758l,0xdfa2b74b1c0436b1l,
        0x871949062e368c04l },
      { 0xb5f928bba3993df5l,0x639d75b5f3b3d26al,0x011aa78a85b55050l,
        0xfc315e6a5b74fde1l } },
    /* 45 */
    { { 0x561fd41ae8d6ecfal,0x5f8c44f61aec7f86l,0x98452a7b4924741dl,
        0xe6d4a7adee389088l },
      { 0x60552ed14593c75dl,0x70a70da4dd271162l,0xd2aede937ba2c7dbl,
        0x35dfaf9a9be2ae57l } },
    /* 46 */
    { { 0x6b956fcdaa736636l,0x09f51d97ae2cab7el,0xfb10bf410f349966l,
        0x1da5c7d71c830d2bl },
      { 0x5c41e4833cce6825l,0x15ad118ff9573c3bl,0xa28552c7f23036b8l,
        0x7077c0fddbf4b9d6l } },
    /* 47 */
    { { 0xbf63ff8d46b9661cl,0xa1dfd36b0d2cfd71l,0x0373e140a847f8f7l,
        0x53a8632ee50efe44l },
      { 0x0976ff68696d8051l,0xdaec0c95c74f468al,0x62994dc35e4e26bdl,
        0x028ca76d34e1fcc1l } },
    /* 48 */
    { { 0xd11d47dcfc9877eel,0xc8b36210801d0002l,0xd002c11754c260b6l,
        0x04c17cd86962f046l },
      { 0x6d9bd094b0daddf5l,0xbea2357524ce55c0l,0x663356e672da03b5l,
        0xf7ba4de9fed97474l } },
    /* 49 */
    { { 0xd0dbfa34ebe1263fl,0x5576373571ae7ce6l,0xd244055382a6f523l,
        0xe31f960052131c41l },
      { 0xd1bb9216ea6b6ec6l,0x37a1d12e73c2fc44l,0xc10e7eac89d0a294l,
        0xaa3a6259ce34d47bl } },
    /* 50 */
    { { 0xfbcf9df536f3dcd3l,0x6ceded50d2bf7360l,0x491710fadf504f5bl,
        0x2398dd627e79daeel },
      { 0xcf4705a36d09569el,0xea0619bb5149f769l,0xff9c037735f6034cl,
        0x5717f5b21c046210l } },
    /* 51 */
    { { 0x9fe229c921dd895el,0x8e51850040c28451l,0xfa13d2391d637ecdl,
        0x660a2c560e3c28del },
      { 0x9cca88aed67fcbd0l,0xc84724780ea9f096l,0x32b2f48172e92b4dl,
        0x624ee54c4f522453l } },
    /* 52 */
    { { 0x09549ce4d897ecccl,0x4d49d1d93f9880aal,0x723c2423043a7c20l,
        0x4f392afb92bdfbc0l },
      { 0x6969f8fa7de44fd9l,0xb66cfbe457b32156l,0xdb2fa803368ebc3cl,
        0x8a3e7977ccdb399cl } },
    /* 53 */
    { { 0xdde1881f06c4b125l,0xae34e300f6e3ca8cl,0xef6999de5c7a13e9l,
        0x3888d02370c24404l },
      { 0x7628035644f91081l,0x3d9fcf615f015504l,0x1827edc8632cd36el,
        0xa5e62e4718102336l } },
    /* 54 */
    { { 0x1a825ee32facd6c8l,0x699c635454bcbc66l,0x0ce3edf798df9931l,
        0x2c4768e6466a5adcl },
      { 0xb346ff8c90a64bc9l,0x630a6020e4779f5cl,0xd949d064bc05e884l,
        0x7b5e6441f9e652a0l } },
    /* 55 */
    { { 0x2169422c1d28444al,0xe996c5d8be136a39l,0x2387afe5fb0c7fcel,
        0xb8af73cb0c8d744al },
      { 0x5fde83aa338b86fdl,0xfee3f158a58a5cffl,0xc9ee8f6f20ac9433l,
        0xa036395f7f3f0895l } },
    /* 56 */
    { { 0x8c73c6bba10f7770l,0xa6f16d81a12a0e24l,0x100df68251bc2b9fl,
        0x4be36b01875fb533l },
      { 0x9226086e9fb56dbbl,0x306fef8b07e7a4f8l,0xeeaccc0566d52f20l,
        0x8cbc9a871bdc00c0l } },
    /* 57 */
    { { 0xe131895cc0dac4abl,0xa874a440712ff112l,0x6332ae7c6a1cee57l,
        0x44e7553e0c0835f8l },
      { 0x6d503fff7734002dl,0x9d35cb8b0b34425cl,0x95f702760e8738b5l,
        0x470a683a5eb8fc18l } },
    /* 58 */
    { { 0x81b761dc90513482l,0x0287202a01e9276al,0xcda441ee0ce73083l,
        0x16410690c63dc6efl },
      { 0xf5034a066d06a2edl,0xdd4d7745189b100bl,0xd914ae72ab8218c9l,
        0xd73479fd7abcbb4fl } },
    /* 59 */
    { { 0x7edefb165ad4c6e5l,0x262cf08f5b06d04dl,0x12ed5bb18575cb14l,
        0x816469e30771666bl },
      { 0xd7ab9d79561e291el,0xeb9daf22c1de1661l,0xf49827eb135e0513l,
        0x0a36dd23f0dd3f9cl } },
    /* 60 */
    { { 0x098d32c741d5533cl,0x7c5f5a9e8684628fl,0x39a228ade349bd11l,
        0xe331dfd6fdbab118l },
      { 0x5100ab686bcc6ed8l,0x7160c3bdef7a260el,0x9063d9a7bce850d7l,
        0xd3b4782a492e3389l } },
    /* 61 */
    { { 0xa149b6e8f3821f90l,0x92edd9ed66eb7aadl,0x0bb669531a013116l,
        0x7281275a4c86a5bdl },
      { 0x503858f7d3ff47e5l,0x5e1616bc61016441l,0x62b0f11a7dfd9bb1l,
        0x2c062e7ece145059l } },
    /* 62 */
    { { 0xa76f996f0159ac2el,0x281e7736cbdb2713l,0x2ad6d28808e46047l,
        0x282a35f92c4e7ef1l },
      { 0x9c354b1ec0ce5cd2l,0xcf99efc91379c229l,0x992caf383e82c11el,
        0xc71cd513554d2abdl } },
    /* 63 */
    { { 0x4885de9c09b578f4l,0x1884e258e3affa7al,0x8f76b1b759182f1fl,
        0xc50f6740cf47f3a3l },
      { 0xa9c4adf3374b68eal,0xa406f32369965fe2l,0x2f86a22285a53050l,
        0xb9ecb3a7212958dcl } },
    /* 64 */
    { { 0x56f8410ef4f8b16al,0x97241afec47b266al,0x0a406b8e6d9c87c1l,
        0x803f3e02cd42ab1bl },
      { 0x7f0309a804dbec69l,0xa83b85f73bbad05fl,0xc6097273ad8e197fl,
        0xc097440e5067adc1l } },
    /* 65 */
    { { 0x846a56f2c379ab34l,0xa8ee068b841df8d1l,0x20314459176c68efl,
        0xf1af32d5915f1f30l },
      { 0x99c375315d75bd50l,0x837cffbaf72f67bcl,0x0613a41848d7723fl,
        0x23d0f130e2d41c8bl } },
    /* 66 */
    { { 0x857ab6edf41500d9l,0x0d890ae5fcbeada8l,0x52fe864889725951l,
        0xb0288dd6c0a3faddl },
      { 0x85320f30650bcb08l,0x71af6313695d6e16l,0x31f520a7b989aa76l,
        0xffd3724ff408c8d2l } },
    /* 67 */
    { { 0x53968e64b458e6cbl,0x992dad20317a5d28l,0x3814ae0b7aa75f56l,
        0xf5590f4ad78c26dfl },
      { 0x0fc24bd3cf0ba55al,0x0fc4724a0c778bael,0x1ce9864f683b674al,
        0x18d6da54f6f74a20l } },
    /* 68 */
    { { 0xed93e225d5be5a2bl,0x6fe799835934f3c6l,0x4314092622626ffcl,
        0x50bbb4d97990216al },
      { 0x378191c6e57ec63el,0x65422c40181dcdb2l,0x41a8099b0236e0f6l,
        0x2b10011801fe49c3l } },
    /* 69 */
    { { 0xfc68b5c59b391593l,0xc385f5a2598270fcl,0x7144f3aad19adcbbl,
        0xdd55899983fbae0cl },
      { 0x93b88b8e74b82ff4l,0xd2e03c4071e734c9l,0x9a7a9eaf43c0322al,
        0xe6e4c551149d6041l } },
    /* 70 */
    { { 0x55f655bb1e9af288l,0x647e1a64f7ada931l,0x43697e4bcb2820e5l,
        0x51e00db107ed56ffl },
      { 0x43d169b8771c327el,0x29cdb20b4a96c2adl,0xc07d51f53deb4779l,
        0xe22f424149829177l } },
    /* 71 */
    { { 0xcd45e8f4635f1abbl,0x7edc0cb568538874l,0xc9472c1fb5a8034dl,
        0xf709373d52dc48c9l },
      { 0x401966bba8af30d6l,0x95bf5f4af137b69cl,0x3966162a9361c47el,
        0xbd52d288e7275b11l } },
    /* 72 */
    { { 0xab155c7a9c5fa877l,0x17dad6727d3a3d48l,0x43f43f9e73d189d8l,
        0xa0d0f8e4c8aa77a6l },
      { 0x0bbeafd8cc94f92dl,0xd818c8be0c4ddb3al,0x22cc65f8b82eba14l,
        0xa56c78c7946d6a00l } },
    /* 73 */
    { { 0x2962391b0dd09529l,0x803e0ea63daddfcfl,0x2c77351f5b5bf481l,
        0xd8befdf8731a367al },
      { 0xab919d42fc0157f4l,0xf51caed7fec8e650l,0xcdf9cb4002d48b0al,
        0x854a68a5ce9f6478l } },
    /* 74 */
    { { 0xdc35f67b63506ea5l,0x9286c489a4fe0d66l,0x3f101d3bfe95cd4dl,
        0x5cacea0b98846a95l },
      { 0xa90df60c9ceac44dl,0x3db29af4354d1c3al,0x08dd3de8ad5dbabel,
        0xe4982d1235e4efa9l } },
    /* 75 */
    { { 0x23104a22c34cd55el,0x58695bb32680d132l,0xfb345afa1fa1d943l,
        0x8046b7f616b20499l },
      { 0xb533581e38e7d098l,0xd7f61e8df46f0b70l,0x30dea9ea44cb78c4l,
        0xeb17ca7b9082af55l } },
    /* 76 */
    { { 0x1751b59876a145b9l,0xa5cf6b0fc1bc71ecl,0xd3e03565392715bbl,
        0x097b00bafab5e131l },
      { 0xaa66c8e9565f69e1l,0x77e8f75ab5be5199l,0x6033ba11da4fd984l,
        0xf95c747bafdbcc9el } },
    /* 77 */
    { { 0x558f01d3bebae45el,0xa8ebe9f0c4bc6955l,0xaeb705b1dbc64fc6l,
        0x3512601e566ed837l },
      { 0x9336f1e1fa1161cdl,0x328ab8d54c65ef87l,0x4757eee2724f21e5l,
        0x0ef971236068ab6bl } },
    /* 78 */
    { { 0x02598cf754ca4226l,0x5eede138f8642c8el,0x48963f74468e1790l,
        0xfc16d9333b4fbc95l },
      { 0xbe96fb31e7c800cal,0x138063312678adaal,0x3d6244976ff3e8b5l,
        0x14ca4af1b95d7a17l } },
    /* 79 */
    { { 0x7a4771babd2f81d5l,0x1a5f9d6901f7d196l,0xd898bef7cad9c907l,
        0x4057b063f59c231dl },
      { 0xbffd82fe89c05c0al,0xe4911c6f1dc0df85l,0x3befccaea35a16dbl,
        0x1c3b5d64f1330b13l } },
    /* 80 */
    { { 0x5fe14bfe80ec21fel,0xf6ce116ac255be82l,0x98bc5a072f4a5d67l,
        0xfad27148db7e63afl },
      { 0x90c0b6ac29ab05b3l,0x37a9a83c4e251ae6l,0x0a7dc875c2aade7dl,
        0x77387de39f0e1a84l } },
    /* 81 */
    { { 0x1e9ecc49a56c0dd7l,0xa5cffcd846086c74l,0x8f7a1408f505aecel,
        0xb37b85c0bef0c47el },
      { 0x3596b6e4cc0e6a8fl,0xfd6d4bbf6b388f23l,0xaba453fac39cef4el,
        0x9c135ac8f9f628d5l } },
    /* 82 */
    { { 0x32aa320284e35743l,0x320d6ab185a3cdefl,0xb821b1761df19819l,
        0x5721361fc433851fl },
      { 0x1f0db36a71fc9168l,0x5f98ba735e5c403cl,0xf64ca87e37bcd8f5l,
        0xdcbac3c9e6bb11bdl } },
    /* 83 */
    { { 0xf01d99684518cbe2l,0xd242fc189c9eb04el,0x727663c7e47feebfl,
        0xb8c1c89e2d626862l },
      { 0x51a58bddc8e1d569l,0x563809c8b7d88cd0l,0x26c27fd9f11f31ebl,
        0x5d23bbda2f9422d4l } },
    /* 84 */
    { { 0x0a1c729495c8f8bel,0x2961c4803bf362bfl,0x9e418403df63d4acl,
        0xc109f9cb91ece900l },
      { 0xc2d095d058945705l,0xb9083d96ddeb85c0l,0x84692b8d7a40449bl,
        0x9bc3344f2eee1ee1l } },
    /* 85 */
    { { 0x0d5ae35642913074l,0x55491b2748a542b1l,0x469ca665b310732al,
        0x29591d525f1a4cc1l },
      { 0xe76f5b6bb84f983fl,0xbe7eef419f5f84e1l,0x1200d49680baa189l,
        0x6376551f18ef332cl } },
    /* 86 */
    { { 0xbda5f14e562976ccl,0x22bca3e60ef12c38l,0xbbfa30646cca9852l,
        0xbdb79dc808e2987al },
      { 0xfd2cb5c9cb06a772l,0x38f475aafe536dcel,0xc2a3e0227c2b5db8l,
        0x8ee86001add3c14al } },
    /* 87 */
    { { 0xcbe96981a4ade873l,0x7ee9aa4dc4fba48cl,0x2cee28995a054ba5l,
        0x92e51d7a6f77aa4bl },
      { 0x948bafa87190a34dl,0xd698f75bf6bd1ed1l,0xd00ee6e30caf1144l,
        0x5182f86f0a56aaaal } },
    /* 88 */
    { { 0xfba6212c7a4cc99cl,0xff609b683e6d9ca1l,0x5dbb27cb5ac98c5al,
        0x91dcab5d4073a6f2l },
      { 0x01b6cc3d5f575a70l,0x0cb361396f8d87fal,0x165d4e8c89981736l,
        0x17a0cedb97974f2bl } },
    /* 89 */
    { { 0x38861e2a076c8d3al,0x701aad39210f924bl,0x94d0eae413a835d9l,
        0x2e8ce36c7f4cdf41l },
      { 0x91273dab037a862bl,0x01ba9bb760e4c8fal,0xf964538833baf2ddl,
        0xf4ccc6cb34f668f3l } },
    /* 90 */
    { { 0x44ef525cf1f79687l,0x7c59549592efa815l,0xe1231741a5c78d29l,
        0xac0db4889a0df3c9l },
      { 0x86bfc711df01747fl,0x592b9358ef17df13l,0xe5880e4f5ccb6bb5l,
        0x95a64a6194c974a2l } },
    /* 91 */
    { { 0x72c1efdac15a4c93l,0x40269b7382585141l,0x6a8dfb1c16cb0badl,
        0x231e54ba29210677l },
      { 0xa70df9178ae6d2dcl,0x4d6aa63f39112918l,0xf627726b5e5b7223l,
        0xab0be032d8a731e1l } },
    /* 92 */
    { { 0x097ad0e98d131f2dl,0x637f09e33b04f101l,0x1ac86196d5e9a748l,
        0xf1bcc8802cf6a679l },
      { 0x25c69140e8daacb4l,0x3c4e405560f65009l,0x591cc8fc477937a6l,
        0x851694695aebb271l } },
    /* 93 */
    { { 0xde35c143f1dcf593l,0x78202b29b018be3bl,0xe9cdadc29bdd9d3dl,
        0x8f67d9d2daad55d8l },
      { 0x841116567481ea5fl,0xe7d2dde9e34c590cl,0xffdd43f405053fa8l,
        0xf84572b9c0728b5dl } },
    /* 94 */
    { { 0x5e1a7a7197af71c9l,0xa14494447a736565l,0xa1b4ae070e1d5063l,
        0xedee2710616b2c19l },
      { 0xb2f034f511734121l,0x1cac6e554a25e9f0l,0x8dc148f3a40c2ecfl,
        0x9fd27e9b44ebd7f4l } },
    /* 95 */
    { { 0x3cc7658af6e2cb16l,0xe3eb7d2cfe5919b6l,0x5a8c5816168d5583l,
        0xa40c2fb6958ff387l },
      { 0x8c9ec560fedcc158l,0x7ad804c655f23056l,0xd93967049a307e12l,
        0x99bc9bb87dc6decfl } },
    /* 96 */
    { { 0x84a9521d927dafc6l,0x52c1fb695c09cd19l,0x9d9581a0f9366ddel,
        0x9abe210ba16d7e64l },
      { 0x480af84a48915220l,0xfa73176a4dd816c6l,0xc7d539871681ca5al,
        0x7881c25787f344b0l } },
    /* 97 */
    { { 0x93399b51e0bcf3ffl,0x0d02cbc5127f74f6l,0x8fb465a2dd01d968l,
        0x15e6e319a30e8940l },
      { 0x646d6e0d3e0e05f4l,0xfad7bddc43588404l,0xbe61c7d1c4f850d3l,
        0x0e55facf191172cel } },
    /* 98 */
    { { 0x7e9d9806f8787564l,0x1a33172131e85ce6l,0x6b0158cab819e8d6l,
        0xd73d09766fe96577l },
      { 0x424834251eb7206el,0xa519290fc618bb42l,0x5dcbb8595e30a520l,
        0x9250a3748f15a50bl } },
    /* 99 */
    { { 0xcaff08f8be577410l,0xfd408a035077a8c6l,0xf1f63289ec0a63a4l,
        0x77414082c1cc8c0bl },
      { 0x05a40fa6eb0991cdl,0xc1ca086649fdc296l,0x3a68a3c7b324fd40l,
        0x8cb04f4d12eb20b9l } },
    /* 100 */
    { { 0xb1c2d0556906171cl,0x9073e9cdb0240c3fl,0xdb8e6b4fd8906841l,
        0xe4e429ef47123b51l },
      { 0x0b8dd53c38ec36f4l,0xf9d2dc01ff4b6a27l,0x5d066e07879a9a48l,
        0x37bca2ff3c6e6552l } },
    /* 101 */
    { { 0x4cd2e3c7df562470l,0x44f272a2c0964ac9l,0x7c6d5df980c793bel,
        0x59913edc3002b22al },
      { 0x7a139a835750592al,0x99e01d80e783de02l,0xcf8c0375ea05d64fl,
        0x43786e4ab013e226l } },
    /* 102 */
    { { 0xff32b0ed9e56b5a6l,0x0750d9a6d9fc68f9l,0xec15e845597846a7l,
        0x8638ca98b7e79e7al },
      { 0x2f5ae0960afc24b2l,0x05398eaf4dace8f2l,0x3b765dd0aecba78fl,
        0x1ecdd36a7b3aa6f0l } },
    /* 103 */
    { { 0x5d3acd626c5ff2f3l,0xa2d516c02873a978l,0xad94c9fad2110d54l,
        0xd85d0f85d459f32dl },
      { 0x9f700b8d10b11da3l,0xd2c22c30a78318c4l,0x556988f49208decdl,
        0xa04f19c3b4ed3c62l } },
    /* 104 */
    { { 0x087924c8ed7f93bdl,0xcb64ac5d392f51f6l,0x7cae330a821b71afl,
        0x92b2eeea5c0950b0l },
      { 0x85ac4c9485b6e235l,0xab2ca4a92936c0f0l,0x80faa6b3e0508891l,
        0x1ee782215834276cl } },
    /* 105 */
    { { 0xa60a2e00e63e79f7l,0xf590e7b2f399d906l,0x9021054a6607c09dl,
        0xf3f2ced857a6e150l },
      { 0x200510f3f10d9b55l,0x9d2fcfacd8642648l,0xe5631aa7e8bd0e7cl,
        0x0f56a4543da3e210l } },
    /* 106 */
    { { 0x5b21bffa1043e0dfl,0x6c74b6cc9c007e6dl,0x1a656ec0d4a8517al,
        0xbd8f17411969e263l },
      { 0x8a9bbb86beb7494al,0x1567d46f45f3b838l,0xdf7a12a7a4e5a79al,
        0x2d1a1c3530ccfa09l } },
    /* 107 */
    { { 0x192e3813506508dal,0x336180c4a1d795a7l,0xcddb59497a9944b3l,
        0xa107a65eb91fba46l },
      { 0xe6d1d1c50f94d639l,0x8b4af3758a58b7d7l,0x1a7c5584bd37ca1cl,
        0x183d760af87a9af2l } },
    /* 108 */
    { { 0x29d697110dde59a4l,0xf1ad8d070e8bef87l,0x229b49634f2ebe78l,
        0x1d44179dc269d754l },
      { 0xb32dc0cf8390d30el,0x0a3b27530de8110cl,0x31af1dc52bc0339al,
        0x771f9cc29606d262l } },
    /* 109 */
    { { 0x99993e7785040739l,0x44539db98026a939l,0xcf40f6f2f5f8fc26l,
        0x64427a310362718el },
      { 0x4f4f2d8785428aa8l,0x7b7adc3febfb49a8l,0x201b2c6df23d01acl,
        0x49d9b7496ae90d6dl } },
    /* 110 */
    { { 0xcc78d8bc435d1099l,0x2adbcd4e8e8d1a08l,0x02c2e2a02cb68a41l,
        0x9037d81b3f605445l },
      { 0x7cdbac27074c7b61l,0xfe2031ab57bfd72el,0x61ccec96596d5352l,
        0x08c3de6a7cc0639cl } },
    /* 111 */
    { { 0x20fdd020f6d552abl,0x56baff9805cd81f1l,0x06fb7c3e91351291l,
        0xc690944245796b2fl },
      { 0x17b3ae9c41231bd1l,0x1eac6e875cc58205l,0x208837abf9d6a122l,
        0x3fa3db02cafe3ac0l } },
    /* 112 */
    { { 0xd75a3e6505058880l,0x7da365ef643943f2l,0x4147861cfab24925l,
        0xc5c4bdb0fdb808ffl },
      { 0x73513e34b272b56bl,0xc8327e9511b9043al,0xfd8ce37df8844969l,
        0x2d56db9446c2b6b5l } },
    /* 113 */
    { { 0x2461782fff46ac6bl,0xd19f792607a2e425l,0xfafea3c409a48de1l,
        0x0f56bd9de503ba42l },
      { 0x137d4ed1345cda49l,0x821158fc816f299dl,0xe7c6a54aaeb43402l,
        0x4003bb9d1173b5f1l } },
    /* 114 */
    { { 0x3b8e8189a0803387l,0xece115f539cbd404l,0x4297208dd2877f21l,
        0x53765522a07f2f9el },
      { 0xa4980a21a8a4182dl,0xa2bbd07a3219df79l,0x674d0a2e1a19a2d4l,
        0x7a056f586c5d4549l } },
    /* 115 */
    { { 0x646b25589d8a2a47l,0x5b582948c3df2773l,0x51ec000eabf0d539l,
        0x77d482f17a1a2675l },
      { 0xb8a1bd9587853948l,0xa6f817bd6cfbffeel,0xab6ec05780681e47l,
        0x4115012b2b38b0e4l } },
    /* 116 */
    { { 0x3c73f0f46de28cedl,0x1d5da7609b13ec47l,0x61b8ce9e6e5c6392l,
        0xcdf04572fbea0946l },
      { 0x1cb3c58b6c53c3b0l,0x97fe3c10447b843cl,0xfb2b8ae12cb9780el,
        0xee703dda97383109l } },
    /* 117 */
    { { 0x34515140ff57e43al,0xd44660d3b1b811b8l,0x2b3b5dff8f42b986l,
        0x2a0ad89da162ce21l },
      { 0x64e4a6946bc277bal,0xc788c954c141c276l,0x141aa64ccabf6274l,
        0xd62d0b67ac2b4659l } },
    /* 118 */
    { { 0x39c5d87b2c054ac4l,0x57005859f27df788l,0xedf7cbf3b18128d6l,
        0xb39a23f2991c2426l },
      { 0x95284a15f0b16ae5l,0x0c6a05b1a136f51bl,0x1d63c137f2700783l,
        0x04ed0092c0674cc5l } },
    /* 119 */
    { { 0x1f4185d19ae90393l,0x3047b4294a3d64e6l,0xae0001a69854fc14l,
        0xa0a91fc10177c387l },
      { 0xff0a3f01ae2c831el,0xbb76ae822b727e16l,0x8f12c8a15a3075b4l,
        0x084cf9889ed20c41l } },
    /* 120 */
    { { 0xd98509defca6becfl,0x2fceae807dffb328l,0x5d8a15c44778e8b9l,
        0xd57955b273abf77el },
      { 0x210da79e31b5d4f1l,0xaa52f04b3cfa7a1cl,0xd4d12089dc27c20bl,
        0x8e14ea4202d141f1l } },
    /* 121 */
    { { 0xeed50345f2897042l,0x8d05331f43402c4al,0xc8d9c194c8bdfb21l,
        0x597e1a372aa4d158l },
      { 0x0327ec1acf0bd68cl,0x6d4be0dcab024945l,0x5b9c8d7ac9fe3e84l,
        0xca3f0236199b4deal } },
    /* 122 */
    { { 0x592a10b56170bd20l,0x0ea897f16d3f5de7l,0xa3363ff144b2ade2l,
        0xbde7fd7e309c07e4l },
      { 0x516bb6d2b8f5432cl,0x210dc1cbe043444bl,0x3db01e6ff8f95b5al,
        0xb623ad0e0a7dd198l } },
    /* 123 */
    { { 0xa75bd67560c7b65bl,0xab8c559023a4a289l,0xf8220fd0d7b26795l,
        0xd6aa2e4658ec137bl },
      { 0x10abc00b5138bb85l,0x8c31d121d833a95cl,0xb24ff00b1702a32el,
        0x111662e02dcc513al } },
    /* 124 */
    { { 0x78114015efb42b87l,0xbd9f5d701b6c4dffl,0x66ecccd7a7d7c129l,
        0xdb3ee1cb94b750f8l },
      { 0xb26f3db0f34837cfl,0xe7eed18bb9578d4fl,0x5d2cdf937c56657dl,
        0x886a644252206a59l } },
    /* 125 */
    { { 0x3c234cfb65b569eal,0x20011141f72119c1l,0x8badc85da15a619el,
        0xa70cf4eb018a17bcl },
      { 0x224f97ae8c4a6a65l,0x36e5cf270134378fl,0xbe3a609e4f7e0960l,
        0xaa4772abd1747b77l } },
    /* 126 */
    { { 0x676761317aa60cc0l,0xc79163610368115fl,0xded98bb4bbc1bb5al,
        0x611a6ddc30faf974l },
      { 0x30e78cbcc15ee47al,0x2e8962824e0d96a5l,0x36f35adf3dd9ed88l,
        0x5cfffaf816429c88l } },
    /* 127 */
    { { 0xc0d54cff9b7a99cdl,0x7bf3b99d843c45a1l,0x038a908f62c739e1l,
        0x6e5a6b237dc1994cl },
      { 0xef8b454e0ba5db77l,0xb7b8807facf60d63l,0xe591c0c676608378l,
        0x481a238d242dabccl } },
    /* 128 */
    { { 0xe3417bc035d0b34al,0x440b386b8327c0a7l,0x8fb7262dac0362d1l,
        0x2c41114ce0cdf943l },
      { 0x2ba5cef1ad95a0b1l,0xc09b37a867d54362l,0x26d6cdd201e486c9l,
        0x20477abf42ff9297l } },
    /* 129 */
    { { 0x2f75173c18d65dbfl,0x77bf940e339edad8l,0x7022d26bdcf1001cl,
        0xac66409ac77396b6l },
      { 0x8b0bb36fc6261cc3l,0x213f7bc9190e7e90l,0x6541cebaa45e6c10l,
        0xce8e6975cc122f85l } },
    /* 130 */
    { { 0x0f121b41bc0a67d2l,0x62d4760a444d248al,0x0e044f1d659b4737l,
        0x08fde365250bb4a8l },
      { 0xaceec3da848bf287l,0xc2a62182d3369d6el,0x3582dfdc92449482l,
        0x2f7e2fd2565d6cd7l } },
    /* 131 */
    { { 0xae4b92dbc3770fa7l,0x095e8d5c379043f9l,0x54f34e9d17761171l,
        0xc65be92e907702ael },
      { 0x2758a303f6fd0a40l,0xe7d822e3bcce784bl,0x7ae4f5854f9767bfl,
        0x4bff8e47d1193b3al } },
    /* 132 */
    { { 0xcd41d21f00ff1480l,0x2ab8fb7d0754db16l,0xac81d2efbbe0f3eal,
        0x3e4e4ae65772967dl },
      { 0x7e18f36d3c5303e6l,0x3bd9994b92262397l,0x9ed70e261324c3c0l,
        0x5388aefd58ec6028l } },
    /* 133 */
    { { 0xad1317eb5e5d7713l,0x09b985ee75de49dal,0x32f5bc4fc74fb261l,
        0x5cf908d14f75be0el },
      { 0x760435108e657b12l,0xbfd421a5b96ed9e6l,0x0e29f51f8970ccc2l,
        0xa698ba4060f00ce2l } },
    /* 134 */
    { { 0x73db1686ef748fecl,0xe6e755a27e9d2cf9l,0x630b6544ce265effl,
        0xb142ef8a7aebad8dl },
      { 0xad31af9f17d5770al,0x66af3b672cb3412fl,0x6bd60d1bdf3359del,
        0xd1896a9658515075l } },
    /* 135 */
    { { 0xec5957ab33c41c08l,0x87de94ac5468e2e1l,0x18816b73ac472f6cl,
        0x267b0e0b7981da39l },
      { 0x6e554e5d8e62b988l,0xd8ddc755116d21e7l,0x4610faf03d2a6f99l,
        0xb54e287aa1119393l } },
    /* 136 */
    { { 0x0a0122b5178a876bl,0x51ff96ff085104b4l,0x050b31ab14f29f76l,
        0x84abb28b5f87d4e6l },
      { 0xd5ed439f8270790al,0x2d6cb59d85e3f46bl,0x75f55c1b6c1e2212l,
        0xe5436f6717655640l } },
    /* 137 */
    { { 0x53f9025e2286e8d5l,0x353c95b4864453bel,0xd832f5bde408e3a0l,
        0x0404f68b5b9ce99el },
      { 0xcad33bdea781e8e5l,0x3cdf5018163c2f5bl,0x575769600119caa3l,
        0x3a4263df0ac1c701l } },
    /* 138 */
    { { 0xc2965ecc9aeb596dl,0x01ea03e7023c92b4l,0x4704b4b62e013961l,
        0x0ca8fd3f905ea367l },
      { 0x92523a42551b2b61l,0x1eb7a89c390fcd06l,0xe7f1d2be0392a63el,
        0x96dca2644ddb0c33l } },
    /* 139 */
    { { 0x203bb43a387510afl,0x846feaa8a9a36a01l,0xd23a57702f950378l,
        0x4363e2123aad59dcl },
      { 0xca43a1c740246a47l,0xb362b8d2e55dd24dl,0xf9b086045d8faf96l,
        0x840e115cd8bb98c4l } },
    /* 140 */
    { { 0xf12205e21023e8a7l,0xc808a8cdd8dc7a0bl,0xe292a272163a5ddfl,
        0x5e0d6abd30ded6d4l },
      { 0x07a721c27cfc0f64l,0x42eec01d0e55ed88l,0x26a7bef91d1f9db2l,
        0x7dea48f42945a25al } },
    /* 141 */
    { { 0xabdf6f1ce5060a81l,0xe79f9c72f8f95615l,0xcfd36c5406ac268bl,
        0xabc2a2beebfd16d1l },
      { 0x8ac66f91d3e2eac7l,0x6f10ba63d2dd0466l,0x6790e3770282d31bl,
        0x4ea353946c7eefc1l } },
    /* 142 */
    { { 0xed8a2f8d5266309dl,0x0a51c6c081945a3el,0xcecaf45a578c5dc1l,
        0x3a76e6891c94ffc3l },
      { 0x9aace8a47d7b0d0fl,0x963ace968f584a5fl,0x51a30c724e697fbel,
        0x8212a10a465e6464l } },
    /* 143 */
    { { 0xef7c61c3cfab8caal,0x18eb8e840e142390l,0xcd1dff677e9733cal,
        0xaa7cab71599cb164l },
      { 0x02fc9273bc837bd1l,0xc06407d0c36af5d7l,0x17621292f423da49l,
        0x40e38073fe0617c3l } },
    /* 144 */
    { { 0xf4f80824a7bf9b7cl,0x365d23203fbe30d0l,0xbfbe532097cf9ce3l,
        0xe3604700b3055526l },
      { 0x4dcb99116cc6c2c7l,0x72683708ba4cbee6l,0xdcded434637ad9ecl,
        0x6542d677a3dee15fl } },
    /* 145 */
    { { 0x3f32b6d07b6c377al,0x6cb03847903448bel,0xd6fdd3a820da8af7l,
        0xa6534aee09bb6f21l },
      { 0x30a1780d1035facfl,0x35e55a339dcb47e6l,0x6ea50fe1c447f393l,
        0xf3cb672fdc9aef22l } },
    /* 146 */
    { { 0xeb3719fe3b55fd83l,0xe0d7a46c875ddd10l,0x33ac9fa905cea784l,
        0x7cafaa2eaae870e7l },
      { 0x9b814d041d53b338l,0xe0acc0a0ef87e6c6l,0xfb93d10811672b0fl,
        0x0aab13c1b9bd522el } },
    /* 147 */
    { { 0xddcce278d2681297l,0xcb350eb1b509546al,0x2dc431737661aaf2l,
        0x4b91a602847012e9l },
      { 0xdcff109572f8ddcfl,0x08ebf61e9a911af4l,0x48f4360ac372430el,
        0x49534c5372321cabl } },
    /* 148 */
    { { 0x83df7d71f07b7e9dl,0xa478efa313cd516fl,0x78ef264b6c047ee3l,
        0xcaf46c4fd65ac5eel },
      { 0xa04d0c7792aa8266l,0xedf45466913684bbl,0x56e65168ae4b16b0l,
        0x14ce9e5704c6770fl } },
    /* 149 */
    { { 0x99445e3e965e8f91l,0xd3aca1bacb0f2492l,0xd31cc70f90c8a0a0l,
        0x1bb708a53e4c9a71l },
      { 0xd5ca9e69558bdd7al,0x734a0508018a26b1l,0xb093aa714c9cf1ecl,
        0xf9d126f2da300102l } },
    /* 150 */
    { { 0x749bca7aaff9563el,0xdd077afeb49914a0l,0xe27a0311bf5f1671l,
        0x807afcb9729ecc69l },
      { 0x7f8a9337c9b08b77l,0x86c3a785443c7e38l,0x85fafa59476fd8bal,
        0x751adcd16568cd8cl } },
    /* 151 */
    { { 0x8aea38b410715c0dl,0xd113ea718f7697f7l,0x665eab1493fbf06dl,
        0x29ec44682537743fl },
      { 0x3d94719cb50bebbcl,0x399ee5bfe4505422l,0x90cd5b3a8d2dedb1l,
        0xff9370e392a4077dl } },
    /* 152 */
    { { 0x59a2d69bc6b75b65l,0x4188f8d5266651c5l,0x28a9f33e3de9d7d2l,
        0x9776478ba2a9d01al },
      { 0x8852622d929af2c7l,0x334f5d6d4e690923l,0xce6cc7e5a89a51e9l,
        0x74a6313fac2f82fal } },
    /* 153 */
    { { 0xb2f4dfddb75f079cl,0x85b07c9518e36fbbl,0x1b6cfcf0e7cd36ddl,
        0xab75be150ff4863dl },
      { 0x81b367c0173fc9b7l,0xb90a7420d2594fd0l,0x15fdbf03c4091236l,
        0x4ebeac2e0b4459f6l } },
    /* 154 */
    { { 0xeb6c5fe75c9f2c53l,0xd25220118eae9411l,0xc8887633f95ac5d8l,
        0xdf99887b2c1baffcl },
      { 0xbb78eed2850aaecbl,0x9d49181b01d6a272l,0x978dd511b1cdbcacl,
        0x27b040a7779f4058l } },
    /* 155 */
    { { 0x90405db7f73b2eb2l,0xe0df85088e1b2118l,0x501b71525962327el,
        0xb393dd37e4cfa3f5l },
      { 0xa1230e7b3fd75165l,0xd66344c2bcd33554l,0x6c36f1be0f7b5022l,
        0x09588c12d0463419l } },
    /* 156 */
    { { 0xe086093f02601c3bl,0xfb0252f8cf5c335fl,0x955cf280894aff28l,
        0x81c879a9db9f648bl },
      { 0x040e687cc6f56c51l,0xfed471693f17618cl,0x44f88a419059353bl,
        0xfa0d48f55fc11bc4l } },
    /* 157 */
    { { 0xbc6e1c9de1608e4dl,0x010dda113582822cl,0xf6b7ddc1157ec2d7l,
        0x8ea0e156b6a367d6l },
      { 0xa354e02f2383b3b4l,0x69966b943f01f53cl,0x4ff6632b2de03ca5l,
        0x3f5ab924fa00b5acl } },
    /* 158 */
    { { 0x337bb0d959739efbl,0xc751b0f4e7ebec0dl,0x2da52dd6411a67d1l,
        0x8bc768872b74256el },
      { 0xa5be3b7282d3d253l,0xa9f679a1f58d779fl,0xa1cac168e16767bbl,
        0xb386f19060fcf34fl } },
    /* 159 */
    { { 0x31f3c1352fedcfc2l,0x5396bf6262f8af0dl,0x9a02b4eae57288c2l,
        0x4cb460f71b069c4dl },
      { 0xae67b4d35b8095eal,0x92bbf8596fc07603l,0xe1475f66b614a165l,
        0x52c0d50895ef5223l } },
    /* 160 */
    { { 0x231c210e15339848l,0xe87a28e870778c8dl,0x9d1de6616956e170l,
        0x4ac3c9382bb09c0bl },
      { 0x19be05516998987dl,0x8b2376c4ae09f4d6l,0x1de0b7651a3f933dl,
        0x380d94c7e39705f4l } },
    /* 161 */
    { { 0x01a355aa81542e75l,0x96c724a1ee01b9b7l,0x6b3a2977624d7087l,
        0x2ce3e171de2637afl },
      { 0xcfefeb49f5d5bc1al,0xa655607e2777e2b5l,0x4feaac2f9513756cl,
        0x2e6cd8520b624e4dl } },
    /* 162 */
    { { 0x3685954b8c31c31dl,0x68533d005bf21a0cl,0x0bd7626e75c79ec9l,
        0xca17754742c69d54l },
      { 0xcc6edafff6d2dbb2l,0xfd0d8cbd174a9d18l,0x875e8793aa4578e8l,
        0xa976a7139cab2ce6l } },
    /* 163 */
    { { 0x0a651f1b93fb353dl,0xd75cab8b57fcfa72l,0xaa88cfa731b15281l,
        0x8720a7170a1f4999l },
      { 0x8c3e8d37693e1b90l,0xd345dc0b16f6dfc3l,0x8ea8d00ab52a8742l,
        0x9719ef29c769893cl } },
    /* 164 */
    { { 0x820eed8d58e35909l,0x9366d8dc33ddc116l,0xd7f999d06e205026l,
        0xa5072976e15704c1l },
      { 0x002a37eac4e70b2el,0x84dcf6576890aa8al,0xcd71bf18645b2a5cl,
        0x99389c9df7b77725l } },
    /* 165 */
    { { 0x238c08f27ada7a4bl,0x3abe9d03fd389366l,0x6b672e89766f512cl,
        0xa88806aa202c82e4l },
      { 0x6602044ad380184el,0xa8cb78c4126a8b85l,0x79d670c0ad844f17l,
        0x0043bffb4738dcfel } },
    /* 166 */
    { { 0x8d59b5dc36d5192el,0xacf885d34590b2afl,0x83566d0a11601781l,
        0x52f3ef01ba6c4866l },
      { 0x3986732a0edcb64dl,0x0a482c238068379fl,0x16cbe5fa7040f309l,
        0x3296bd899ef27e75l } },
    /* 167 */
    { { 0x476aba89454d81d7l,0x9eade7ef51eb9b3cl,0x619a21cd81c57986l,
        0x3b90febfaee571e9l },
      { 0x9393023e5496f7cbl,0x55be41d87fb51bc4l,0x03f1dd4899beb5cel,
        0x6e88069d9f810b18l } },
    /* 168 */
    { { 0xce37ab11b43ea1dbl,0x0a7ff1a95259d292l,0x851b02218f84f186l,
        0xa7222beadefaad13l },
      { 0xa2ac78ec2b0a9144l,0x5a024051f2fa59c5l,0x91d1eca56147ce38l,
        0xbe94d523bc2ac690l } },
    /* 169 */
    { { 0x72f4945e0b226ce7l,0xb8afd747967e8b70l,0xedea46f185a6c63el,
        0x7782defe9be8c766l },
      { 0x760d2aa43db38626l,0x460ae78776f67ad1l,0x341b86fc54499cdbl,
        0x03838567a2892e4bl } },
    /* 170 */
    { { 0x2d8daefd79ec1a0fl,0x3bbcd6fdceb39c97l,0xf5575ffc58f61a95l,
        0xdbd986c4adf7b420l },
      { 0x81aa881415f39eb7l,0x6ee2fcf5b98d976cl,0x5465475dcf2f717dl,
        0x8e24d3c46860bbd0l } },
    /* 171 */
    { { 0x749d8e549a587390l,0x12bb194f0cbec588l,0x46e07da4b25983c6l,
        0x541a99c4407bafc8l },
      { 0xdb241692624c8842l,0x6044c12ad86c05ffl,0xc59d14b44f7fcf62l,
        0xc0092c49f57d35d1l } },
    /* 172 */
    { { 0xd3cc75c3df2e61efl,0x7e8841c82e1b35cal,0xc62d30d1909f29f4l,
        0x75e406347286944dl },
      { 0xe7d41fc5bbc237d0l,0xc9537bf0ec4f01c9l,0x91c51a16282bd534l,
        0x5b7cb658c7848586l } },
    /* 173 */
    { { 0x964a70848a28ead1l,0x802dc508fd3b47f6l,0x9ae4bfd1767e5b39l,
        0x7ae13eba8df097a1l },
      { 0xfd216ef8eadd384el,0x0361a2d9b6b2ff06l,0x204b98784bcdb5f3l,
        0x787d8074e2a8e3fdl } },
    /* 174 */
    { { 0xc5e25d6b757fbb1cl,0xe47bddb2ca201debl,0x4a55e9a36d2233ffl,
        0x5c2228199ef28484l },
      { 0x773d4a8588315250l,0x21b21a2b827097c1l,0xab7c4ea1def5d33fl,
        0xe45d37abbaf0f2b0l } },
    /* 175 */
    { { 0xd2df1e3428511c8al,0xebb229c8bdca6cd3l,0x578a71a7627c39a7l,
        0xed7bc12284dfb9d3l },
      { 0xcf22a6df93dea561l,0x5443f18dd48f0ed1l,0xd8b861405bad23e8l,
        0xaac97cc945ca6d27l } },
    /* 176 */
    { { 0xeb54ea74a16bd00al,0xd839e9adf5c0bcc1l,0x092bb7f11f9bfc06l,
        0x318f97b31163dc4el },
      { 0xecc0c5bec30d7138l,0x44e8df23abc30220l,0x2bb7972fb0223606l,
        0xfa41faa19a84ff4dl } },
    /* 177 */
    { { 0x4402d974a6642269l,0xc81814ce9bb783bdl,0x398d38e47941e60bl,
        0x38bb6b2c1d26e9e2l },
      { 0xc64e4a256a577f87l,0x8b52d253dc11fe1cl,0xff336abf62280728l,
        0x94dd0905ce7601a5l } },
    /* 178 */
    { { 0x156cf7dcde93f92al,0xa01333cb89b5f315l,0x02404df9c995e750l,
        0x92077867d25c2ae9l },
      { 0xe2471e010bf39d44l,0x5f2c902096bb53d7l,0x4c44b7b35c9c3d8fl,
        0x81e8428bd29beb51l } },
    /* 179 */
    { { 0x6dd9c2bac477199fl,0x8cb8eeee6b5ecdd9l,0x8af7db3fee40fd0el,
        0x1b94ab62dbbfa4b1l },
      { 0x44f0d8b3ce47f143l,0x51e623fc63f46163l,0xf18f270fcc599383l,
        0x06a38e28055590eel } },
    /* 180 */
    { { 0x2e5b0139b3355b49l,0x20e26560b4ebf99bl,0xc08ffa6bd269f3dcl,
        0xa7b36c2083d9d4f8l },
      { 0x64d15c3a1b3e8830l,0xd5fceae1a89f9c0bl,0xcfeee4a2e2d16930l,
        0xbe54c6b4a2822a20l } },
    /* 181 */
    { { 0xd6cdb3df8d91167cl,0x517c3f79e7a6625el,0x7105648f346ac7f4l,
        0xbf30a5abeae022bbl },
      { 0x8e7785be93828a68l,0x5161c3327f3ef036l,0xe11b5feb592146b2l,
        0xd1c820de2732d13al } },
    /* 182 */
    { { 0x043e13479038b363l,0x58c11f546b05e519l,0x4fe57abe6026cad1l,
        0xb7d17bed68a18da3l },
      { 0x44ca5891e29c2559l,0x4f7a03765bfffd84l,0x498de4af74e46948l,
        0x3997fd5e6412cc64l } },
    /* 183 */
    { { 0xf20746828bd61507l,0x29e132d534a64d2al,0xffeddfb08a8a15e3l,
        0x0eeb89293c6c13e8l },
      { 0xe9b69a3ea7e259f8l,0xce1db7e6d13e7e67l,0x277318f6ad1fa685l,
        0x228916f8c922b6efl } },
    /* 184 */
    { { 0x959ae25b0a12ab5bl,0xcc11171f957bc136l,0x8058429ed16e2b0cl,
        0xec05ad1d6e93097el },
      { 0x157ba5beac3f3708l,0x31baf93530b59d77l,0x47b55237118234e5l,
        0x7d3141567ff11b37l } },
    /* 185 */
    { { 0x7bd9c05cf6dfefabl,0xbe2f2268dcb37707l,0xe53ead973a38bb95l,
        0xe9ce66fc9bc1d7a3l },
      { 0x75aa15766f6a02a1l,0x38c087df60e600edl,0xf8947f3468cdc1b9l,
        0xd9650b0172280651l } },
    /* 186 */
    { { 0x504b4c4a5a057e60l,0xcbccc3be8def25e4l,0xa635320817c1ccbdl,
        0x14d6699a804eb7a2l },
      { 0x2c8a8415db1f411al,0x09fbaf0bf80d769cl,0xb4deef901c2f77adl,
        0x6f4c68410d43598al } },
    /* 187 */
    { { 0x8726df4e96c24a96l,0x534dbc85fcbd99a3l,0x3c466ef28b2ae30al,
        0x4c4350fd61189abbl },
      { 0x2967f716f855b8dal,0x41a42394463c38a1l,0xc37e1413eae93343l,
        0xa726d2425a3118b5l } },
    /* 188 */
    { { 0xdae6b3ee948c1086l,0xf1de503dcbd3a2e1l,0x3f35ed3f03d022f3l,
        0x13639e82cc6cf392l },
      { 0x9ac938fbcdafaa86l,0xf45bc5fb2654a258l,0x1963b26e45051329l,
        0xca9365e1c1a335a3l } },
    /* 189 */
    { { 0x3615ac754c3b2d20l,0x742a5417904e241bl,0xb08521c4cc9d071dl,
        0x9ce29c34970b72a5l },
      { 0x8cc81f736d3e0ad6l,0x8060da9ef2f8434cl,0x35ed1d1a6ce862d9l,
        0x48c4abd7ab42af98l } },
    /* 190 */
    { { 0xd221b0cc40c7485al,0xead455bbe5274dbfl,0x493c76989263d2e8l,
        0x78017c32f67b33cbl },
      { 0xb9d35769930cb5eel,0xc0d14e940c408ed2l,0xf8b7bf55272f1a4dl,
        0x53cd0454de5c1c04l } },
    /* 191 */
    { { 0xbcd585fa5d28ccacl,0x5f823e56005b746el,0x7c79f0a1cd0123aal,
        0xeea465c1d3d7fa8fl },
      { 0x7810659f0551803bl,0x6c0b599f7ce6af70l,0x4195a77029288e70l,
        0x1b6e42a47ae69193l } },
    /* 192 */
    { { 0x2e80937cf67d04c3l,0x1e312be289eeb811l,0x56b5d88792594d60l,
        0x0224da14187fbd3dl },
      { 0x87abb8630c5fe36fl,0x580f3c604ef51f5fl,0x964fb1bfb3b429ecl,
        0x60838ef042bfff33l } },
    /* 193 */
    { { 0x432cb2f27e0bbe99l,0x7bda44f304aa39eel,0x5f497c7a9fa93903l,
        0x636eb2022d331643l },
      { 0xfcfd0e6193ae00aal,0x875a00fe31ae6d2fl,0xf43658a29f93901cl,
        0x8844eeb639218bacl } },
    /* 194 */
    { { 0x114171d26b3bae58l,0x7db3df7117e39f3el,0xcd37bc7f81a8eadal,
        0x27ba83dc51fb789el },
      { 0xa7df439ffbf54de5l,0x7277030bb5fe1a71l,0x42ee8e35db297a48l,
        0xadb62d3487f3a4abl } },
    /* 195 */
    { { 0x9b1168a2a175df2al,0x082aa04f618c32e9l,0xc9e4f2e7146b0916l,
        0xb990fd7675e7c8b2l },
      { 0x0829d96b4df37313l,0x1c205579d0b40789l,0x66c9ae4a78087711l,
        0x81707ef94d10d18dl } },
    /* 196 */
    { { 0x97d7cab203d6ff96l,0x5b851bfc0d843360l,0x268823c4d042db4bl,
        0x3792daead5a8aa5cl },
      { 0x52818865941afa0bl,0xf3e9e74142d83671l,0x17c825275be4e0a7l,
        0x5abd635e94b001bal } },
    /* 197 */
    { { 0x727fa84e0ac4927cl,0xe3886035a7c8cf23l,0xa4bcd5ea4adca0dfl,
        0x5995bf21846ab610l },
      { 0xe90f860b829dfa33l,0xcaafe2ae958fc18bl,0x9b3baf4478630366l,
        0x44c32ca2d483411el } },
    /* 198 */
    { { 0xa74a97f1e40ed80cl,0x5f938cb131d2ca82l,0x53f2124b7c2d6ad9l,
        0x1f2162fb8082a54cl },
      { 0x7e467cc5720b173el,0x40e8a666085f12f9l,0x8cebc20e4c9d65dcl,
        0x8f1d402bc3e907c9l } },
    /* 199 */
    { { 0x4f592f9cfbc4058al,0xb15e14b6292f5670l,0xc55cfe37bc1d8c57l,
        0xb1980f43926edbf9l },
      { 0x98c33e0932c76b09l,0x1df5279d33b07f78l,0x6f08ead4863bb461l,
        0x2828ad9b37448e45l } },
    /* 200 */
    { { 0x696722c4c4cf4ac5l,0xf5ac1a3fdde64afbl,0x0551baa2e0890832l,
        0x4973f1275a14b390l },
      { 0xe59d8335322eac5dl,0x5e07eef50bd9b568l,0xab36720fa2588393l,
        0x6dac8ed0db168ac7l } },
    /* 201 */
    { { 0xf7b545aeeda835efl,0x4aa113d21d10ed51l,0x035a65e013741b09l,
        0x4b23ef5920b9de4cl },
      { 0xe82bb6803c4c7341l,0xd457706d3f58bc37l,0x73527863a51e3ee8l,
        0x4dd71534ddf49a4el } },
    /* 202 */
    { { 0xbf94467295476cd9l,0x648d072fe31a725bl,0x1441c8b8fc4b67e0l,
        0xfd3170002f4a4dbbl },
      { 0x1cb43ff48995d0e1l,0x76e695d10ef729aal,0xe0d5f97641798982l,
        0x14fac58c9569f365l } },
    /* 203 */
    { { 0xad9a0065f312ae18l,0x51958dc0fcc93fc9l,0xd9a142408a7d2846l,
        0xed7c765136abda50l },
      { 0x46270f1a25d4abbcl,0x9b5dd8f3f1a113eal,0xc609b0755b51952fl,
        0xfefcb7f74d2e9f53l } },
    /* 204 */
    { { 0xbd09497aba119185l,0xd54e8c30aac45ba4l,0x492479deaa521179l,
        0x1801a57e87e0d80bl },
      { 0x073d3f8dfcafffb0l,0x6cf33c0bae255240l,0x781d763b5b5fdfbcl,
        0x9f8fc11e1ead1064l } },
    /* 205 */
    { { 0x1583a1715e69544cl,0x0eaf8567f04b7813l,0x1e22a8fd278a4c32l,
        0xa9d3809d3d3a69a9l },
      { 0x936c2c2c59a2da3bl,0x38ccbcf61895c847l,0x5e65244e63d50869l,
        0x3006b9aee1178ef7l } },
    /* 206 */
    { { 0x0bb1f2b0c9eead28l,0x7eef635d89f4dfbcl,0x074757fdb2ce8939l,
        0x0ab85fd745f8f761l },
      { 0xecda7c933e5b4549l,0x4be2bb5c97922f21l,0x261a1274b43b8040l,
        0xb122d67511e942c2l } },
    /* 207 */
    { { 0x3be607be66a5ae7al,0x01e703fa76adcbe3l,0xaf9043014eb6e5c5l,
        0x9f599dc1097dbaecl },
      { 0x6d75b7180ff250edl,0x8eb91574349a20dcl,0x425605a410b227a3l,
        0x7d5528e08a294b78l } },
    /* 208 */
    { { 0xf0f58f6620c26defl,0x025585ea582b2d1el,0xfbe7d79b01ce3881l,
        0x28ccea01303f1730l },
      { 0xd1dabcd179644ba5l,0x1fc643e806fff0b8l,0xa60a76fc66b3e17bl,
        0xc18baf48a1d013bfl } },
    /* 209 */
    { { 0x34e638c85dc4216dl,0x00c01067206142acl,0xd453a17195f5064al,
        0x9def809db7a9596bl },
      { 0x41e8642e67ab8d2cl,0xb42404336237a2b6l,0x7d506a6d64c4218bl,
        0x0357f8b068808ce5l } },
    /* 210 */
    { { 0x8e9dbe644cd2cc88l,0xcc61c28df0b8f39dl,0x4a309874cd30a0c8l,
        0xe4a01add1b489887l },
      { 0x2ed1eeacf57cd8f9l,0x1b767d3ebd594c48l,0xa7295c717bd2f787l,
        0x466d7d79ce10cc30l } },
    /* 211 */
    { { 0x47d318929dada2c7l,0x4fa0a6c38f9aa27dl,0x90e4fd28820a59e1l,
        0xc672a522451ead1al },
      { 0x30607cc85d86b655l,0xf0235d3bf9ad4af1l,0x99a08680571172a6l,
        0x5e3d64faf2a67513l } },
    /* 212 */
    { { 0xaa6410c79b3b4416l,0xcd8fcf85eab26d99l,0x5ebff74adb656a74l,
        0x6c8a7a95eb8e42fcl },
      { 0x10c60ba7b02a63bdl,0x6b2f23038b8f0047l,0x8c6c3738312d90b0l,
        0x348ae422ad82ca91l } },
    /* 213 */
    { { 0x7f4746635ccda2fbl,0x22accaa18e0726d2l,0x85adf782492b1f20l,
        0xc1074de0d9ef2d2el },
      { 0xfcf3ce44ae9a65b3l,0xfd71e4ac05d7151bl,0xd4711f50ce6a9788l,
        0xfbadfbdbc9e54ffcl } },
    /* 214 */
    { { 0x1713f1cd20a99363l,0xb915658f6cf22775l,0x968175cd24d359b2l,
        0xb7f976b483716fcdl },
      { 0x5758e24d5d6dbf74l,0x8d23bafd71c3af36l,0x48f477600243dfe3l,
        0xf4d41b2ecafcc805l } },
    /* 215 */
    { { 0x51f1cf28fdabd48dl,0xce81be3632c078a4l,0x6ace2974117146e9l,
        0x180824eae0160f10l },
      { 0x0387698b66e58358l,0x63568752ce6ca358l,0x82380e345e41e6c5l,
        0x67e5f63983cf6d25l } },
    /* 216 */
    { { 0xf89ccb8dcf4899efl,0x949015f09ebb44c0l,0x546f9276b2598ec9l,
        0x9fef789a04c11fc6l },
      { 0x6d367ecf53d2a071l,0xb10e1a7fa4519b09l,0xca6b3fb0611e2eefl,
        0xbc80c181a99c4e20l } },
    /* 217 */
    { { 0x972536f8e5eb82e6l,0x1a484fc7f56cb920l,0xc78e217150b5da5el,
        0x49270e629f8cdf10l },
      { 0x1a39b7bbea6b50adl,0x9a0284c1a2388ffcl,0x5403eb178107197bl,
        0xd2ee52f961372f7fl } },
    /* 218 */
    { { 0xd37cd28588e0362al,0x442fa8a78fa5d94dl,0xaff836e5a434a526l,
        0xdfb478bee5abb733l },
      { 0xa91f1ce7673eede6l,0xa5390ad42b5b2f04l,0x5e66f7bf5530da2fl,
        0xd9a140b408df473al } },
    /* 219 */
    { { 0x0e0221b56e8ea498l,0x623478293563ee09l,0xe06b8391335d2adel,
        0x760c058d623f4b1al },
      { 0x0b89b58cc198aa79l,0xf74890d2f07aba7fl,0x4e204110fde2556al,
        0x7141982d8f190409l } },
    /* 220 */
    { { 0x6f0a0e334d4b0f45l,0xd9280b38392a94e1l,0x3af324c6b3c61d5el,
        0x3af9d1ce89d54e47l },
      { 0xfd8f798120930371l,0xeda2664c21c17097l,0x0e9545dcdc42309bl,
        0xb1f815c373957dd6l } },
    /* 221 */
    { { 0x84faa78e89fec44al,0xc8c2ae473caa4cafl,0x691c807dc1b6a624l,
        0xa41aed141543f052l },
      { 0x424353997d5ffe04l,0x8bacb2df625b6e20l,0x85d660be87817775l,
        0xd6e9c1dd86fb60efl } },
    /* 222 */
    { { 0x3aa2e97ec6853264l,0x771533b7e2304a0bl,0x1b912bb7b8eae9bel,
        0x9c9c6e10ae9bf8c2l },
      { 0xa2309a59e030b74cl,0x4ed7494d6a631e90l,0x89f44b23a49b79f2l,
        0x566bd59640fa61b6l } },
    /* 223 */
    { { 0x066c0118c18061f3l,0x190b25d37c83fc70l,0xf05fc8e027273245l,
        0xcf2c7390f525345el },
      { 0xa09bceb410eb30cfl,0xcfd2ebba0d77703al,0xe842c43a150ff255l,
        0x02f517558aa20979l } },
    /* 224 */
    { { 0x396ef794addb7d07l,0x0b4fc74224455500l,0xfaff8eacc78aa3cel,
        0x14e9ada5e8d4d97dl },
      { 0xdaa480a12f7079e2l,0x45baa3cde4b0800el,0x01765e2d7838157dl,
        0xa0ad4fab8e9d9ae8l } },
    /* 225 */
    { { 0x0bfb76214a653618l,0x1872813c31eaaa5fl,0x1553e73744949d5el,
        0xbcd530b86e56ed1el },
      { 0x169be85332e9c47bl,0xdc2776feb50059abl,0xcdba9761192bfbb4l,
        0x909283cf6979341dl } },
    /* 226 */
    { { 0x67b0032476e81a13l,0x9bee1a9962171239l,0x08ed361bd32e19d6l,
        0x35eeb7c9ace1549al },
      { 0x1280ae5a7e4e5bdcl,0x2dcd2cd3b6ceec6el,0x52e4224c6e266bc1l,
        0x9a8b2cf4448ae864l } },
    /* 227 */
    { { 0xf6471bf209d03b59l,0xc90e62a3b65af2abl,0xff7ff168ebd5eec9l,
        0x6bdb60f4d4491379l },
      { 0xdadafebc8a55bc30l,0xc79ead1610097fe0l,0x42e197414c1e3bddl,
        0x01ec3cfd94ba08a9l } },
    /* 228 */
    { { 0xba6277ebdc9485c2l,0x48cc9a7922fb10c7l,0x4f61d60f70a28d8al,
        0xd1acb1c0475464f6l },
      { 0xd26902b126f36612l,0x59c3a44ee0618d8bl,0x4df8a813308357eel,
        0x7dcd079d405626c2l } },
    /* 229 */
    { { 0x5ce7d4d3f05a4b48l,0xadcd295237230772l,0xd18f7971812a915al,
        0x0bf53589377d19b8l },
      { 0x35ecd95a6c68ea73l,0xc7f3bbca823a584dl,0x9fb674c6f473a723l,
        0xd28be4d9e16686fcl } },
    /* 230 */
    { { 0x5d2b990638fa8e4bl,0x559f186e893fd8fcl,0x3a6de2aa436fb6fcl,
        0xd76007aa510f88cel },
      { 0x2d10aab6523a4988l,0xb455cf4474dd0273l,0x7f467082a3407278l,
        0xf2b52f68b303bb01l } },
    /* 231 */
    { { 0x0d57eafa9835b4cal,0x2d2232fcbb669cbcl,0x8eeeb680c6643198l,
        0xd8dbe98ecc5aed3al },
      { 0xcba9be3fc5a02709l,0x30be68e5f5ba1fa8l,0xfebd43cdf10ea852l,
        0xe01593a3ee559705l } },
    /* 232 */
    { { 0xd3e5af50ea75a0a6l,0x512226ac57858033l,0x6fe6d50fd0176406l,
        0xafec07b1aeb8ef06l },
      { 0x7fb9956780bb0a31l,0x6f1af3cc37309aael,0x9153a15a01abf389l,
        0xa71b93546e2dbfddl } },
    /* 233 */
    { { 0xbf8e12e018f593d2l,0xd1a90428a078122bl,0x150505db0ba4f2adl,
        0x53a2005c628523d9l },
      { 0x07c8b639e7f2b935l,0x2bff975ac182961al,0x86bceea77518ca2cl,
        0xbf47d19b3d588e3dl } },
    /* 234 */
    { { 0x672967a7dd7665d5l,0x4e3030572f2f4de5l,0x144005ae80d4903fl,
        0x001c2c7f39c9a1b6l },
      { 0x143a801469efc6d6l,0xc810bdaa7bc7a724l,0x5f65670ba78150a4l,
        0xfdadf8e786ffb99bl } },
    /* 235 */
    { { 0xfd38cb88ffc00785l,0x77fa75913b48eb67l,0x0454d055bf368fbcl,
        0x3a838e4d5aa43c94l },
      { 0x561663293e97bb9al,0x9eb93363441d94d9l,0x515591a60adb2a83l,
        0x3cdb8257873e1da3l } },
    /* 236 */
    { { 0x137140a97de77eabl,0xf7e1c50d41648109l,0x762dcad2ceb1d0dfl,
        0x5a60cc89f1f57fbal },
      { 0x80b3638240d45673l,0x1b82be195913c655l,0x057284b8dd64b741l,
        0x922ff56fdbfd8fc0l } },
    /* 237 */
    { { 0x1b265deec9a129a1l,0xa5b1ce57cc284e04l,0x04380c46cebfbe3cl,
        0x72919a7df6c5cd62l },
      { 0x298f453a8fb90f9al,0xd719c00b88e4031bl,0xe32c0e77796f1856l,
        0x5e7917803624089al } },
    /* 238 */
    { { 0x5c16ec557f63cdfbl,0x8e6a3571f1cae4fdl,0xfce26bea560597cal,
        0x4e0a5371e24c2fabl },
      { 0x276a40d3a5765357l,0x3c89af440d73a2b4l,0xb8f370ae41d11a32l,
        0xf5ff7818d56604eel } },
    /* 239 */
    { { 0xfbf3e3fe1a09df21l,0x26d5d28ee66e8e47l,0x2096bd0a29c89015l,
        0xe41df0e9533f5e64l },
      { 0x305fda40b3ba9e3fl,0xf2340ceb2604d895l,0x0866e1927f0367c7l,
        0x8edd7d6eac4f155fl } },
    /* 240 */
    { { 0xc9a1dc0e0bfc8ff3l,0x14efd82be936f42fl,0x67016f7ccca381efl,
        0x1432c1caed8aee96l },
      { 0xec68482970b23c26l,0xa64fe8730735b273l,0xe389f6e5eaef0f5al,
        0xcaef480b5ac8d2c6l } },
    /* 241 */
    { { 0x5245c97875315922l,0xd82951713063cca5l,0xf3ce60d0b64ef2cbl,
        0xd0ba177e8efae236l },
      { 0x53a9ae8fb1b3af60l,0x1a796ae53d2da20el,0x01d63605df9eef28l,
        0xf31c957c1c54ae16l } },
    /* 242 */
    { { 0xc0f58d5249cc4597l,0xdc5015b0bae0a028l,0xefc5fc55734a814al,
        0x013404cb96e17c3al },
      { 0xb29e2585c9a824bfl,0xd593185e001eaed7l,0x8d6ee68261ef68acl,
        0x6f377c4b91933e6cl } },
    /* 243 */
    { { 0x9f93bad1a8333fd2l,0xa89302025a2a95b8l,0x211e5037eaf75acel,
        0x6dba3e4ed2d09506l },
      { 0xa48ef98cd04399cdl,0x1811c66ee6b73adel,0x72f60752c17ecaf3l,
        0xf13cf3423becf4a7l } },
    /* 244 */
    { { 0xceeb9ec0a919e2ebl,0x83a9a195f62c0f68l,0xcfba3bb67aba2299l,
        0xc83fa9a9274bbad3l },
      { 0x0d7d1b0b62fa1ce0l,0xe58b60f53418efbfl,0xbfa8ef9e52706f04l,
        0xb49d70f45d702683l } },
    /* 245 */
    { { 0x914c7510fad5513bl,0x05f32eecb1751e2dl,0x6d850418d9fb9d59l,
        0x59cfadbb0c30f1cfl },
      { 0xe167ac2355cb7fd6l,0x249367b8820426a3l,0xeaeec58c90a78864l,
        0x5babf362354a4b67l } },
    /* 246 */
    { { 0x37c981d1ee424865l,0x8b002878f2e5577fl,0x702970f1b9e0c058l,
        0x6188c6a79026c8f0l },
      { 0x06f9a19bd0f244dal,0x1ecced5cfb080873l,0x35470f9b9f213637l,
        0x993fe475df50b9d9l } },
    /* 247 */
    { { 0x68e31cdf9b2c3609l,0x84eb19c02c46d4eal,0x7ac9ec1a9a775101l,
        0x81f764664c80616bl },
      { 0x1d7c2a5a75fbe978l,0x6743fed3f183b356l,0x838d1f04501dd2bfl,
        0x564a812a5fe9060dl } },
    /* 248 */
    { { 0x7a5a64f4fa817d1dl,0x55f96844bea82e0fl,0xb5ff5a0fcd57f9aal,
        0x226bf3cf00e51d6cl },
      { 0xd6d1a9f92f2833cfl,0x20a0a35a4f4f89a8l,0x11536c498f3f7f77l,
        0x68779f47ff257836l } },
    /* 249 */
    { { 0x79b0c1c173043d08l,0xa54467741fc020fal,0xd3767e289a6d26d0l,
        0x97bcb0d1eb092e0bl },
      { 0x2ab6eaa8f32ed3c3l,0xc8a4f151b281bc48l,0x4d1bf4f3bfa178f3l,
        0xa872ffe80a784655l } },
    /* 250 */
    { { 0xb1ab7935a32b2086l,0xe1eb710e8160f486l,0x9bd0cd913b6ae6bel,
        0x02812bfcb732a36al },
      { 0xa63fd7cacf605318l,0x646e5d50fdfd6d1dl,0xa1d683982102d619l,
        0x07391cc9fe5396afl } },
    /* 251 */
    { { 0xc50157f08b80d02bl,0x6b8333d162877f7fl,0x7aca1af878d542ael,
        0x355d2adc7e6d2a08l },
      { 0xb41f335a287386e1l,0xfd272a94f8e43275l,0x286ca2cde79989eal,
        0x3dc2b1e37c2a3a79l } },
    /* 252 */
    { { 0xd689d21c04581352l,0x0a00c825376782bel,0x203bd5909fed701fl,
        0xc47869103ccd846bl },
      { 0x5dba770824c768edl,0x72feea026841f657l,0x73313ed56accce0el,
        0xccc42968d5bb4d32l } },
    /* 253 */
    { { 0x94e50de13d7620b9l,0xd89a5c8a5992a56al,0xdc007640675487c9l,
        0xe147eb42aa4871cfl },
      { 0x274ab4eeacf3ae46l,0xfd4936fb50350fbel,0xdf2afe4748c840eal,
        0x239ac047080e96e3l } },
    /* 254 */
    { { 0x481d1f352bfee8d4l,0xce80b5cffa7b0fecl,0x105c4c9e2ce9af3cl,
        0xc55fa1a3f5f7e59dl },
      { 0x3186f14e8257c227l,0xc5b1653f342be00bl,0x09afc998aa904fb2l,
        0x094cd99cd4f4b699l } },
    /* 255 */
    { { 0x8a981c84d703bebal,0x8631d15032ceb291l,0xa445f2c9e3bd49ecl,
        0xb90a30b642abad33l },
      { 0xb465404fb4a5abf9l,0x004750c375db7603l,0x6f9a42ccca35d89fl,
        0x019f8b9a1b7924f7l } },
};

/* Multiply the base point of P256 by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * r     Resulting point.
 * k     Scalar to multiply by.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
static int sp_256_ecc_mulmod_base_4(sp_point* r, const sp_digit* k,
        int map, void* heap)
{
    return sp_256_ecc_mulmod_stripe_4(r, &p256_base, p256_table,
                                      k, map, heap);
}

#ifdef HAVE_INTEL_AVX2
/* Multiply the base point of P256 by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * r     Resulting point.
 * k     Scalar to multiply by.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
static int sp_256_ecc_mulmod_base_avx2_4(sp_point* r, const sp_digit* k,
        int map, void* heap)
{
    return sp_256_ecc_mulmod_stripe_avx2_4(r, &p256_base, p256_table,
                                      k, map, heap);
}

#endif /* HAVE_INTEL_AVX2 */
#else /* WOLFSSL_SP_SMALL */
/* The index into pre-computation table to use. */
static const uint8_t recode_index_4_7[130] = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49,
    48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33,
    32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17,
    16, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,
     0,  1,
};

/* Whether to negate y-ordinate. */
static const uint8_t recode_neg_4_7[130] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     0,  0,
};

/* Recode the scalar for multiplication using pre-computed values and
 * subtraction.
 *
 * k  Scalar to multiply by.
 * v  Vector of operations to peform.
 */
static void sp_256_ecc_recode_7_4(const sp_digit* k, ecc_recode* v)
{
    int i, j;
    uint8_t y;
    int carry = 0;
    int o;
    sp_digit n;

    j = 0;
    n = k[j];
    o = 0;
    for (i=0; i<37; i++) {
        y = n;
        if (o + 7 < 64) {
            y &= 0x7f;
            n >>= 7;
            o += 7;
        }
        else if (o + 7 == 64) {
            n >>= 7;
            if (++j < 4)
                n = k[j];
            o = 0;
        }
        else if (++j < 4) {
            n = k[j];
            y |= (n << (64 - o)) & 0x7f;
            o -= 57;
            n >>= o;
        }

        y += carry;
        v[i].i = recode_index_4_7[y];
        v[i].neg = recode_neg_4_7[y];
        carry = (y >> 7) + v[i].neg;
    }
}

static const sp_table_entry p256_table[2405] = {
    /* 0 << 0 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 0 */
    { { 0x79e730d418a9143cl,0x75ba95fc5fedb601l,0x79fb732b77622510l,
        0x18905f76a53755c6l },
      { 0xddf25357ce95560al,0x8b4ab8e4ba19e45cl,0xd2e88688dd21f325l,
        0x8571ff1825885d85l } },
    /* 2 << 0 */
    { { 0x850046d410ddd64dl,0xaa6ae3c1a433827dl,0x732205038d1490d9l,
        0xf6bb32e43dcf3a3bl },
      { 0x2f3648d361bee1a5l,0x152cd7cbeb236ff8l,0x19a8fb0e92042dbel,
        0x78c577510a5b8a3bl } },
    /* 3 << 0 */
    { { 0xffac3f904eebc127l,0xb027f84a087d81fbl,0x66ad77dd87cbbc98l,
        0x26936a3fb6ff747el },
      { 0xb04c5c1fc983a7ebl,0x583e47ad0861fe1al,0x788208311a2ee98el,
        0xd5f06a29e587cc07l } },
    /* 4 << 0 */
    { { 0x74b0b50d46918dccl,0x4650a6edc623c173l,0x0cdaacace8100af2l,
        0x577362f541b0176bl },
      { 0x2d96f24ce4cbaba6l,0x17628471fad6f447l,0x6b6c36dee5ddd22el,
        0x84b14c394c5ab863l } },
    /* 5 << 0 */
    { { 0xbe1b8aaec45c61f5l,0x90ec649a94b9537dl,0x941cb5aad076c20cl,
        0xc9079605890523c8l },
      { 0xeb309b4ae7ba4f10l,0x73c568efe5eb882bl,0x3540a9877e7a1f68l,
        0x73a076bb2dd1e916l } },
    /* 6 << 0 */
    { { 0x403947373e77664al,0x55ae744f346cee3el,0xd50a961a5b17a3adl,
        0x13074b5954213673l },
      { 0x93d36220d377e44bl,0x299c2b53adff14b5l,0xf424d44cef639f11l,
        0xa4c9916d4a07f75fl } },
    /* 7 << 0 */
    { { 0x0746354ea0173b4fl,0x2bd20213d23c00f7l,0xf43eaab50c23bb08l,
        0x13ba5119c3123e03l },
      { 0x2847d0303f5b9d4dl,0x6742f2f25da67bddl,0xef933bdc77c94195l,
        0xeaedd9156e240867l } },
    /* 8 << 0 */
    { { 0x27f14cd19499a78fl,0x462ab5c56f9b3455l,0x8f90f02af02cfc6bl,
        0xb763891eb265230dl },
      { 0xf59da3a9532d4977l,0x21e3327dcf9eba15l,0x123c7b84be60bbf0l,
        0x56ec12f27706df76l } },
    /* 9 << 0 */
    { { 0x75c96e8f264e20e8l,0xabe6bfed59a7a841l,0x2cc09c0444c8eb00l,
        0xe05b3080f0c4e16bl },
      { 0x1eb7777aa45f3314l,0x56af7bedce5d45e3l,0x2b6e019a88b12f1al,
        0x086659cdfd835f9bl } },
    /* 10 << 0 */
    { { 0x2c18dbd19dc21ec8l,0x98f9868a0fcf8139l,0x737d2cd648250b49l,
        0xcc61c94724b3428fl },
      { 0x0c2b407880dd9e76l,0xc43a8991383fbe08l,0x5f7d2d65779be5d2l,
        0x78719a54eb3b4ab5l } },
    /* 11 << 0 */
    { { 0xea7d260a6245e404l,0x9de407956e7fdfe0l,0x1ff3a4158dac1ab5l,
        0x3e7090f1649c9073l },
      { 0x1a7685612b944e88l,0x250f939ee57f61c8l,0x0c0daa891ead643dl,
        0x68930023e125b88el } },
    /* 12 << 0 */
    { { 0x04b71aa7d2697768l,0xabdedef5ca345a33l,0x2409d29dee37385el,
        0x4ee1df77cb83e156l },
      { 0x0cac12d91cbb5b43l,0x170ed2f6ca895637l,0x28228cfa8ade6d66l,
        0x7ff57c9553238acal } },
    /* 13 << 0 */
    { { 0xccc425634b2ed709l,0x0e356769856fd30dl,0xbcbcd43f559e9811l,
        0x738477ac5395b759l },
      { 0x35752b90c00ee17fl,0x68748390742ed2e3l,0x7cd06422bd1f5bc1l,
        0xfbc08769c9e7b797l } },
    /* 14 << 0 */
    { { 0xa242a35bb0cf664al,0x126e48f77f9707e3l,0x1717bf54c6832660l,
        0xfaae7332fd12c72el },
      { 0x27b52db7995d586bl,0xbe29569e832237c2l,0xe8e4193e2a65e7dbl,
        0x152706dc2eaa1bbbl } },
    /* 15 << 0 */
    { { 0x72bcd8b7bc60055bl,0x03cc23ee56e27e4bl,0xee337424e4819370l,
        0xe2aa0e430ad3da09l },
      { 0x40b8524f6383c45dl,0xd766355442a41b25l,0x64efa6de778a4797l,
        0x2042170a7079adf4l } },
    /* 16 << 0 */
    { { 0x808b0b650bc6fb80l,0x5882e0753ffe2e6bl,0xd5ef2f7c2c83f549l,
        0x54d63c809103b723l },
      { 0xf2f11bd652a23f9bl,0x3670c3194b0b6587l,0x55c4623bb1580e9el,
        0x64edf7b201efe220l } },
    /* 17 << 0 */
    { { 0x97091dcbd53c5c9dl,0xf17624b6ac0a177bl,0xb0f139752cfe2dffl,
        0xc1a35c0a6c7a574el },
      { 0x227d314693e79987l,0x0575bf30e89cb80el,0x2f4e247f0d1883bbl,
        0xebd512263274c3d0l } },
    /* 18 << 0 */
    { { 0x5f3e51c856ada97al,0x4afc964d8f8b403el,0xa6f247ab412e2979l,
        0x675abd1b6f80ebdal },
      { 0x66a2bd725e485a1dl,0x4b2a5caf8f4f0b3cl,0x2626927f1b847bbal,
        0x6c6fc7d90502394dl } },
    /* 19 << 0 */
    { { 0xfea912baa5659ae8l,0x68363aba25e1a16el,0xb8842277752c41acl,
        0xfe545c282897c3fcl },
      { 0x2d36e9e7dc4c696bl,0x5806244afba977c5l,0x85665e9be39508c1l,
        0xf720ee256d12597bl } },
    /* 20 << 0 */
    { { 0x8a979129d2337a31l,0x5916868f0f862bdcl,0x048099d95dd283bal,
        0xe2d1eeb6fe5bfb4el },
      { 0x82ef1c417884005dl,0xa2d4ec17ffffcbael,0x9161c53f8aa95e66l,
        0x5ee104e1c5fee0d0l } },
    /* 21 << 0 */
    { { 0x562e4cecc135b208l,0x74e1b2654783f47dl,0x6d2a506c5a3f3b30l,
        0xecead9f4c16762fcl },
      { 0xf29dd4b2e286e5b9l,0x1b0fadc083bb3c61l,0x7a75023e7fac29a4l,
        0xc086d5f1c9477fa3l } },
    /* 22 << 0 */
    { { 0x0fc611352f6f3076l,0xc99ffa23e3912a9al,0x6a0b0685d2f8ba3dl,
        0xfdc777e8e93358a4l },
      { 0x94a787bb35415f04l,0x640c2d6a4d23fea4l,0x9de917da153a35b5l,
        0x793e8d075d5cd074l } },
    /* 23 << 0 */
    { { 0xf4f876532de45068l,0x37c7a7e89e2e1f6el,0xd0825fa2a3584069l,
        0xaf2cea7c1727bf42l },
      { 0x0360a4fb9e4785a9l,0xe5fda49c27299f4al,0x48068e1371ac2f71l,
        0x83d0687b9077666fl } },
    /* 24 << 0 */
    { { 0x6d3883b215d02819l,0x6d0d755040dd9a35l,0x61d7cbf91d2b469fl,
        0xf97b232f2efc3115l },
      { 0xa551d750b24bcbc7l,0x11ea494988a1e356l,0x7669f03193cb7501l,
        0x595dc55eca737b8al } },
    /* 25 << 0 */
    { { 0xa4a319acd837879fl,0x6fc1b49eed6b67b0l,0xe395993332f1f3afl,
        0x966742eb65432a2el },
      { 0x4b8dc9feb4966228l,0x96cc631243f43950l,0x12068859c9b731eel,
        0x7b948dc356f79968l } },
    /* 26 << 0 */
    { { 0x61e4ad32ed1f8008l,0xe6c9267ad8b17538l,0x1ac7c5eb857ff6fbl,
        0x994baaa855f2fb10l },
      { 0x84cf14e11d248018l,0x5a39898b628ac508l,0x14fde97b5fa944f5l,
        0xed178030d12e5ac7l } },
    /* 27 << 0 */
    { { 0x042c2af497e2feb4l,0xd36a42d7aebf7313l,0x49d2c9eb084ffdd7l,
        0x9f8aa54b2ef7c76al },
      { 0x9200b7ba09895e70l,0x3bd0c66fddb7fb58l,0x2d97d10878eb4cbbl,
        0x2d431068d84bde31l } },
    /* 28 << 0 */
    { { 0x4b523eb7172ccd1fl,0x7323cb2830a6a892l,0x97082ec0cfe153ebl,
        0xe97f6b6af2aadb97l },
      { 0x1d3d393ed1a83da1l,0xa6a7f9c7804b2a68l,0x4a688b482d0cb71el,
        0xa9b4cc5f40585278l } },
    /* 29 << 0 */
    { { 0x5e5db46acb66e132l,0xf1be963a0d925880l,0x944a70270317b9e2l,
        0xe266f95948603d48l },
      { 0x98db66735c208899l,0x90472447a2fb18a3l,0x8a966939777c619fl,
        0x3798142a2a3be21bl } },
    /* 30 << 0 */
    { { 0xb4241cb13298b343l,0xa3a14e49b44f65a1l,0xc5f4d6cd3ac77acdl,
        0xd0288cb552b6fc3cl },
      { 0xd5cc8c2f1c040abcl,0xb675511e06bf9b4al,0xd667da379b3aa441l,
        0x460d45ce51601f72l } },
    /* 31 << 0 */
    { { 0xe2f73c696755ff89l,0xdd3cf7e7473017e6l,0x8ef5689d3cf7600dl,
        0x948dc4f8b1fc87b4l },
      { 0xd9e9fe814ea53299l,0x2d921ca298eb6028l,0xfaecedfd0c9803fcl,
        0xf38ae8914d7b4745l } },
    /* 32 << 0 */
    { { 0xd8c5fccfc5e3a3d8l,0xbefd904c4079dfbfl,0xbc6d6a58fead0197l,
        0x39227077695532a4l },
      { 0x09e23e6ddbef42f5l,0x7e449b64480a9908l,0x7b969c1aad9a2e40l,
        0x6231d7929591c2a4l } },
    /* 33 << 0 */
    { { 0x871514560f664534l,0x85ceae7c4b68f103l,0xac09c4ae65578ab9l,
        0x33ec6868f044b10cl },
      { 0x6ac4832b3a8ec1f1l,0x5509d1285847d5efl,0xf909604f763f1574l,
        0xb16c4303c32f63c4l } },
    /* 34 << 0 */
    { { 0xb6ab20147ca23cd3l,0xcaa7a5c6a391849dl,0x5b0673a375678d94l,
        0xc982ddd4dd303e64l },
      { 0xfd7b000b5db6f971l,0xbba2cb1f6f876f92l,0xc77332a33c569426l,
        0xa159100c570d74f8l } },
    /* 35 << 0 */
    { { 0xfd16847fdec67ef5l,0x742ee464233e76b7l,0x0b8e4134efc2b4c8l,
        0xca640b8642a3e521l },
      { 0x653a01908ceb6aa9l,0x313c300c547852d5l,0x24e4ab126b237af7l,
        0x2ba901628bb47af8l } },
    /* 36 << 0 */
    { { 0x3d5e58d6a8219bb7l,0xc691d0bd1b06c57fl,0x0ae4cb10d257576el,
        0x3569656cd54a3dc3l },
      { 0xe5ebaebd94cda03al,0x934e82d3162bfe13l,0x450ac0bae251a0c6l,
        0x480b9e11dd6da526l } },
    /* 37 << 0 */
    { { 0x00467bc58cce08b5l,0xb636458c7f178d55l,0xc5748baea677d806l,
        0x2763a387dfa394ebl },
      { 0xa12b448a7d3cebb6l,0xe7adda3e6f20d850l,0xf63ebce51558462cl,
        0x58b36143620088a8l } },
    /* 38 << 0 */
    { { 0x8a2cc3ca4d63c0eel,0x512331170fe948cel,0x7463fd85222ef33bl,
        0xadf0c7dc7c603d6cl },
      { 0x0ec32d3bfe7765e5l,0xccaab359bf380409l,0xbdaa84d68e59319cl,
        0xd9a4c2809c80c34dl } },
    /* 39 << 0 */
    { { 0xa9d89488a059c142l,0x6f5ae714ff0b9346l,0x068f237d16fb3664l,
        0x5853e4c4363186acl },
      { 0xe2d87d2363c52f98l,0x2ec4a76681828876l,0x47b864fae14e7b1cl,
        0x0c0bc0e569192408l } },
    /* 40 << 0 */
    { { 0xe4d7681db82e9f3el,0x83200f0bdf25e13cl,0x8909984c66f27280l,
        0x462d7b0075f73227l },
      { 0xd90ba188f2651798l,0x74c6e18c36ab1c34l,0xab256ea35ef54359l,
        0x03466612d1aa702fl } },
    /* 41 << 0 */
    { { 0x624d60492ed22e91l,0x6fdfe0b56f072822l,0xeeca111539ce2271l,
        0x98100a4fdb01614fl },
      { 0xb6b0daa2a35c628fl,0xb6f94d2ec87e9a47l,0xc67732591d57d9cel,
        0xf70bfeec03884a7bl } },
    /* 42 << 0 */
    { { 0x5fb35ccfed2bad01l,0xa155cbe31da6a5c7l,0xc2e2594c30a92f8fl,
        0x649c89ce5bfafe43l },
      { 0xd158667de9ff257al,0x9b359611f32c50ael,0x4b00b20b906014cfl,
        0xf3a8cfe389bc7d3dl } },
    /* 43 << 0 */
    { { 0x4ff23ffd248a7d06l,0x80c5bfb4878873fal,0xb7d9ad9005745981l,
        0x179c85db3db01994l },
      { 0xba41b06261a6966cl,0x4d82d052eadce5a8l,0x9e91cd3ba5e6a318l,
        0x47795f4f95b2dda0l } },
    /* 44 << 0 */
    { { 0xecfd7c1fd55a897cl,0x009194abb29110fbl,0x5f0e2046e381d3b0l,
        0x5f3425f6a98dd291l },
      { 0xbfa06687730d50dal,0x0423446c4b083b7fl,0x397a247dd69d3417l,
        0xeb629f90387ba42al } },
    /* 45 << 0 */
    { { 0x1ee426ccd5cd79bfl,0x0032940b946c6e18l,0x1b1e8ae057477f58l,
        0xe94f7d346d823278l },
      { 0xc747cb96782ba21al,0xc5254469f72b33a5l,0x772ef6dec7f80c81l,
        0xd73acbfe2cd9e6b5l } },
    /* 46 << 0 */
    { { 0x4075b5b149ee90d9l,0x785c339aa06e9ebal,0xa1030d5babf825e0l,
        0xcec684c3a42931dcl },
      { 0x42ab62c9c1586e63l,0x45431d665ab43f2bl,0x57c8b2c055f7835dl,
        0x033da338c1b7f865l } },
    /* 47 << 0 */
    { { 0x283c7513caa76097l,0x0a624fa936c83906l,0x6b20afec715af2c7l,
        0x4b969974eba78bfdl },
      { 0x220755ccd921d60el,0x9b944e107baeca13l,0x04819d515ded93d4l,
        0x9bbff86e6dddfd27l } },
    /* 48 << 0 */
    { { 0x6b34413077adc612l,0xa7496529bbd803a0l,0x1a1baaa76d8805bdl,
        0xc8403902470343adl },
      { 0x39f59f66175adff1l,0x0b26d7fbb7d8c5b7l,0xa875f5ce529d75e3l,
        0x85efc7e941325cc2l } },
    /* 49 << 0 */
    { { 0x21950b421ff6acd3l,0xffe7048453dc6909l,0xff4cd0b228766127l,
        0xabdbe6084fb7db2bl },
      { 0x837c92285e1109e8l,0x26147d27f4645b5al,0x4d78f592f7818ed8l,
        0xd394077ef247fa36l } },
    /* 50 << 0 */
    { { 0x0fb9c2d0488c171al,0xa78bfbaa13685278l,0xedfbe268d5b1fa6al,
        0x0dceb8db2b7eaba7l },
      { 0xbf9e80899ae2b710l,0xefde7ae6a4449c96l,0x43b7716bcc143a46l,
        0xd7d34194c3628c13l } },
    /* 51 << 0 */
    { { 0x508cec1c3b3f64c9l,0xe20bc0ba1e5edf3fl,0xda1deb852f4318d4l,
        0xd20ebe0d5c3fa443l },
      { 0x370b4ea773241ea3l,0x61f1511c5e1a5f65l,0x99a5e23d82681c62l,
        0xd731e383a2f54c2dl } },
    /* 52 << 0 */
    { { 0x2692f36e83445904l,0x2e0ec469af45f9c0l,0x905a3201c67528b7l,
        0x88f77f34d0e5e542l },
      { 0xf67a8d295864687cl,0x23b92eae22df3562l,0x5c27014b9bbec39el,
        0x7ef2f2269c0f0f8dl } },
    /* 53 << 0 */
    { { 0x97359638546c4d8dl,0x5f9c3fc492f24679l,0x912e8beda8c8acd9l,
        0xec3a318d306634b0l },
      { 0x80167f41c31cb264l,0x3db82f6f522113f2l,0xb155bcd2dcafe197l,
        0xfba1da5943465283l } },
    /* 54 << 0 */
    { { 0xa0425b8eb212cf53l,0x4f2e512ef8557c5fl,0xc1286ff925c4d56cl,
        0xbb8a0feaee26c851l },
      { 0xc28f70d2e7d6107el,0x7ee0c444e76265aal,0x3df277a41d1936b1l,
        0x1a556e3fea9595ebl } },
    /* 55 << 0 */
    { { 0x258bbbf9e7305683l,0x31eea5bf07ef5be6l,0x0deb0e4a46c814c1l,
        0x5cee8449a7b730ddl },
      { 0xeab495c5a0182bdel,0xee759f879e27a6b4l,0xc2cf6a6880e518cal,
        0x25e8013ff14cf3f4l } },
    /* 56 << 0 */
    { { 0x8fc441407e8d7a14l,0xbb1ff3ca9556f36al,0x6a84438514600044l,
        0xba3f0c4a7451ae63l },
      { 0xdfcac25b1f9af32al,0x01e0db86b1f2214bl,0x4e9a5bc2a4b596acl,
        0x83927681026c2c08l } },
    /* 57 << 0 */
    { { 0x3ec832e77acaca28l,0x1bfeea57c7385b29l,0x068212e3fd1eaf38l,
        0xc13298306acf8cccl },
      { 0xb909f2db2aac9e59l,0x5748060db661782al,0xc5ab2632c79b7a01l,
        0xda44c6c600017626l } },
    /* 58 << 0 */
    { { 0xf26c00e8a7ea82f0l,0x99cac80de4299aafl,0xd66fe3b67ed78be1l,
        0x305f725f648d02cdl },
      { 0x33ed1bc4623fb21bl,0xfa70533e7a6319adl,0x17ab562dbe5ffb3el,
        0x0637499456674741l } },
    /* 59 << 0 */
    { { 0x69d44ed65c46aa8el,0x2100d5d3a8d063d1l,0xcb9727eaa2d17c36l,
        0x4c2bab1b8add53b7l },
      { 0xa084e90c15426704l,0x778afcd3a837ebeal,0x6651f7017ce477f8l,
        0xa062499846fb7a8bl } },
    /* 60 << 0 */
    { { 0xdc1e6828ed8a6e19l,0x33fc23364189d9c7l,0x026f8fe2671c39bcl,
        0xd40c4ccdbc6f9915l },
      { 0xafa135bbf80e75cal,0x12c651a022adff2cl,0xc40a04bd4f51ad96l,
        0x04820109bbe4e832l } },
    /* 61 << 0 */
    { { 0x3667eb1a7f4c04ccl,0x59556621a9404f84l,0x71cdf6537eceb50al,
        0x994a44a69b8335fal },
      { 0xd7faf819dbeb9b69l,0x473c5680eed4350dl,0xb6658466da44bba2l,
        0x0d1bc780872bdbf3l } },
    /* 62 << 0 */
    { { 0xe535f175a1962f91l,0x6ed7e061ed58f5a7l,0x177aa4c02089a233l,
        0x0dbcb03ae539b413l },
      { 0xe3dc424ebb32e38el,0x6472e5ef6806701el,0xdd47ff98814be9eel,
        0x6b60cfff35ace009l } },
    /* 63 << 0 */
    { { 0xb8d3d9319ff91fe5l,0x039c4800f0518eedl,0x95c376329182cb26l,
        0x0763a43482fc568dl },
      { 0x707c04d5383e76bal,0xac98b930824e8197l,0x92bf7c8f91230de0l,
        0x90876a0140959b70l } },
    /* 64 << 0 */
    { { 0xdb6d96f305968b80l,0x380a0913089f73b9l,0x7da70b83c2c61e01l,
        0x95fb8394569b38c7l },
      { 0x9a3c651280edfe2fl,0x8f726bb98faeaf82l,0x8010a4a078424bf8l,
        0x296720440e844970l } },
    /* 0 << 7 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 7 */
    { { 0x63c5cb817a2ad62al,0x7ef2b6b9ac62ff54l,0x3749bba4b3ad9db5l,
        0xad311f2c46d5a617l },
      { 0xb77a8087c2ff3b6dl,0xb46feaf3367834ffl,0xf8aa266d75d6b138l,
        0xfa38d320ec008188l } },
    /* 2 << 7 */
    { { 0x486d8ffa696946fcl,0x50fbc6d8b9cba56dl,0x7e3d423e90f35a15l,
        0x7c3da195c0dd962cl },
      { 0xe673fdb03cfd5d8bl,0x0704b7c2889dfca5l,0xf6ce581ff52305aal,
        0x399d49eb914d5e53l } },
    /* 3 << 7 */
    { { 0x380a496d6ec293cdl,0x733dbda78e7051f5l,0x037e388db849140al,
        0xee4b32b05946dbf6l },
      { 0xb1c4fda9cae368d1l,0x5001a7b0fdb0b2f3l,0x6df593742e3ac46el,
        0x4af675f239b3e656l } },
    /* 4 << 7 */
    { { 0x44e3811039949296l,0x5b63827b361db1b5l,0x3e5323ed206eaff5l,
        0x942370d2c21f4290l },
      { 0xf2caaf2ee0d985a1l,0x192cc64b7239846dl,0x7c0b8f47ae6312f8l,
        0x7dc61f9196620108l } },
    /* 5 << 7 */
    { { 0xb830fb5bc2da7de9l,0xd0e643df0ff8d3bel,0x31ee77ba188a9641l,
        0x4e8aa3aabcf6d502l },
      { 0xf9fb65329a49110fl,0xd18317f62dd6b220l,0x7e3ced4152c3ea5al,
        0x0d296a147d579c4al } },
    /* 6 << 7 */
    { { 0x35d6a53eed4c3717l,0x9f8240cf3d0ed2a3l,0x8c0d4d05e5543aa5l,
        0x45d5bbfbdd33b4b4l },
      { 0xfa04cc73137fd28el,0x862ac6efc73b3ffdl,0x403ff9f531f51ef2l,
        0x34d5e0fcbc73f5a2l } },
    /* 7 << 7 */
    { { 0xf252682008913f4fl,0xea20ed61eac93d95l,0x51ed38b46ca6b26cl,
        0x8662dcbcea4327b0l },
      { 0x6daf295c725d2aaal,0xbad2752f8e52dcdal,0x2210e7210b17daccl,
        0xa37f7912d51e8232l } },
    /* 8 << 7 */
    { { 0x4f7081e144cc3addl,0xd5ffa1d687be82cfl,0x89890b6c0edd6472l,
        0xada26e1a3ed17863l },
      { 0x276f271563483caal,0xe6924cd92f6077fdl,0x05a7fe980a466e3cl,
        0xf1c794b0b1902d1fl } },
    /* 9 << 7 */
    { { 0xe521368882a8042cl,0xd931cfafcd278298l,0x069a0ae0f597a740l,
        0x0adbb3f3eb59107cl },
      { 0x983e951e5eaa8eb8l,0xe663a8b511b48e78l,0x1631cc0d8a03f2c5l,
        0x7577c11e11e271e2l } },
    /* 10 << 7 */
    { { 0x33b2385c08369a90l,0x2990c59b190eb4f8l,0x819a6145c68eac80l,
        0x7a786d622ec4a014l },
      { 0x33faadbe20ac3a8dl,0x31a217815aba2d30l,0x209d2742dba4f565l,
        0xdb2ce9e355aa0fbbl } },
    /* 11 << 7 */
    { { 0x8cef334b168984dfl,0xe81dce1733879638l,0xf6e6949c263720f0l,
        0x5c56feaff593cbecl },
      { 0x8bff5601fde58c84l,0x74e241172eccb314l,0xbcf01b614c9a8a78l,
        0xa233e35e544c9868l } },
    /* 12 << 7 */
    { { 0xb3156bf38bd7aff1l,0x1b5ee4cb1d81b146l,0x7ba1ac41d628a915l,
        0x8f3a8f9cfd89699el },
      { 0x7329b9c9a0748be7l,0x1d391c95a92e621fl,0xe51e6b214d10a837l,
        0xd255f53a4947b435l } },
    /* 13 << 7 */
    { { 0x07669e04f1788ee3l,0xc14f27afa86938a2l,0x8b47a334e93a01c0l,
        0xff627438d9366808l },
      { 0x7a0985d8ca2a5965l,0x3d9a5542d6e9b9b3l,0xc23eb80b4cf972e8l,
        0x5c1c33bb4fdf72fdl } },
    /* 14 << 7 */
    { { 0x0c4a58d474a86108l,0xf8048a8fee4c5d90l,0xe3c7c924e86d4c80l,
        0x28c889de056a1e60l },
      { 0x57e2662eb214a040l,0xe8c48e9837e10347l,0x8774286280ac748al,
        0xf1c24022186b06f2l } },
    /* 15 << 7 */
    { { 0xac2dd4c35f74040al,0x409aeb71fceac957l,0x4fbad78255c4ec23l,
        0xb359ed618a7b76ecl },
      { 0x12744926ed6f4a60l,0xe21e8d7f4b912de3l,0xe2575a59fc705a59l,
        0x72f1d4deed2dbc0el } },
    /* 16 << 7 */
    { { 0x3d2b24b9eb7926b8l,0xbff88cb3cdbe5509l,0xd0f399afe4dd640bl,
        0x3c5fe1302f76ed45l },
      { 0x6f3562f43764fb3dl,0x7b5af3183151b62dl,0xd5bd0bc7d79ce5f3l,
        0xfdaf6b20ec66890fl } },
    /* 17 << 7 */
    { { 0x735c67ec6063540cl,0x50b259c2e5f9cb8fl,0xb8734f9a3f99c6abl,
        0xf8cc13d5a3a7bc85l },
      { 0x80c1b305c5217659l,0xfe5364d44ec12a54l,0xbd87045e681345fel,
        0x7f8efeb1582f897fl } },
    /* 18 << 7 */
    { { 0xe8cbf1e5d5923359l,0xdb0cea9d539b9fb0l,0x0c5b34cf49859b98l,
        0x5e583c56a4403cc6l },
      { 0x11fc1a2dd48185b7l,0xc93fbc7e6e521787l,0x47e7a05805105b8bl,
        0x7b4d4d58db8260c8l } },
    /* 19 << 7 */
    { { 0xe33930b046eb842al,0x8e844a9a7bdae56dl,0x34ef3a9e13f7fdfcl,
        0xb3768f82636ca176l },
      { 0x2821f4e04e09e61cl,0x414dc3a1a0c7cddcl,0xd537943754945fcdl,
        0x151b6eefb3555ff1l } },
    /* 20 << 7 */
    { { 0xb31bd6136339c083l,0x39ff8155dfb64701l,0x7c3388d2e29604abl,
        0x1e19084ba6b10442l },
      { 0x17cf54c0eccd47efl,0x896933854a5dfb30l,0x69d023fb47daf9f6l,
        0x9222840b7d91d959l } },
    /* 21 << 7 */
    { { 0x439108f5803bac62l,0x0b7dd91d379bd45fl,0xd651e827ca63c581l,
        0x5c5d75f6509c104fl },
      { 0x7d5fc7381f2dc308l,0x20faa7bfd98454bel,0x95374beea517b031l,
        0xf036b9b1642692acl } },
    /* 22 << 7 */
    { { 0xc510610939842194l,0xb7e2353e49d05295l,0xfc8c1d5cefb42ee0l,
        0xe04884eb08ce811cl },
      { 0xf1f75d817419f40el,0x5b0ac162a995c241l,0x120921bbc4c55646l,
        0x713520c28d33cf97l } },
    /* 23 << 7 */
    { { 0xb4a65a5ce98c5100l,0x6cec871d2ddd0f5al,0x251f0b7f9ba2e78bl,
        0x224a8434ce3a2a5fl },
      { 0x26827f6125f5c46fl,0x6a22bedc48545ec0l,0x25ae5fa0b1bb5cdcl,
        0xd693682ffcb9b98fl } },
    /* 24 << 7 */
    { { 0x32027fe891e5d7d3l,0xf14b7d1773a07678l,0xf88497b3c0dfdd61l,
        0xf7c2eec02a8c4f48l },
      { 0xaa5573f43756e621l,0xc013a2401825b948l,0x1c03b34563878572l,
        0xa0472bea653a4184l } },
    /* 25 << 7 */
    { { 0xf4222e270ac69a80l,0x34096d25f51e54f6l,0x00a648cb8fffa591l,
        0x4e87acdc69b6527fl },
      { 0x0575e037e285ccb4l,0x188089e450ddcf52l,0xaa96c9a8870ff719l,
        0x74a56cd81fc7e369l } },
    /* 26 << 7 */
    { { 0x41d04ee21726931al,0x0bbbb2c83660ecfdl,0xa6ef6de524818e18l,
        0xe421cc51e7d57887l },
      { 0xf127d208bea87be6l,0x16a475d3b1cdd682l,0x9db1b684439b63f7l,
        0x5359b3dbf0f113b6l } },
    /* 27 << 7 */
    { { 0xdfccf1de8bf06e31l,0x1fdf8f44dd383901l,0x10775cad5017e7d2l,
        0xdfc3a59758d11eefl },
      { 0x6ec9c8a0b1ecff10l,0xee6ed6cc28400549l,0xb5ad7bae1b4f8d73l,
        0x61b4f11de00aaab9l } },
    /* 28 << 7 */
    { { 0x7b32d69bd4eff2d7l,0x88ae67714288b60fl,0x159461b437a1e723l,
        0x1f3d4789570aae8cl },
      { 0x869118c07f9871dal,0x35fbda78f635e278l,0x738f3641e1541dacl,
        0x6794b13ac0dae45fl } },
    /* 29 << 7 */
    { { 0x065064ac09cc0917l,0x27c53729c68540fdl,0x0d2d4c8eef227671l,
        0xd23a9f80a1785a04l },
      { 0x98c5952852650359l,0xfa09ad0174a1acadl,0x082d5a290b55bf5cl,
        0xa40f1c67419b8084l } },
    /* 30 << 7 */
    { { 0x3a5c752edcc18770l,0x4baf1f2f8825c3a5l,0xebd63f7421b153edl,
        0xa2383e47b2f64723l },
      { 0xe7bf620a2646d19al,0x56cb44ec03c83ffdl,0xaf7267c94f6be9f1l,
        0x8b2dfd7bc06bb5e9l } },
    /* 31 << 7 */
    { { 0xb87072f2a672c5c7l,0xeacb11c80d53c5e2l,0x22dac29dff435932l,
        0x37bdb99d4408693cl },
      { 0xf6e62fb62899c20fl,0x3535d512447ece24l,0xfbdc6b88ff577ce3l,
        0x726693bd190575f2l } },
    /* 32 << 7 */
    { { 0x6772b0e5ab4b35a2l,0x1d8b6001f5eeaacfl,0x728f7ce4795b9580l,
        0x4a20ed2a41fb81dal },
      { 0x9f685cd44fec01e6l,0x3ed7ddcca7ff50adl,0x460fd2640c2d97fdl,
        0x3a241426eb82f4f9l } },
    /* 33 << 7 */
    { { 0x17d1df2c6a8ea820l,0xb2b50d3bf22cc254l,0x03856cbab7291426l,
        0x87fd26ae04f5ee39l },
      { 0x9cb696cc02bee4bal,0x5312180406820fd6l,0xa5dfc2690212e985l,
        0x666f7ffa160f9a09l } },
    /* 34 << 7 */
    { { 0xc503cd33bccd9617l,0x365dede4ba7730a3l,0x798c63555ddb0786l,
        0xa6c3200efc9cd3bcl },
      { 0x060ffb2ce5e35efdl,0x99a4e25b5555a1c1l,0x11d95375f70b3751l,
        0x0a57354a160e1bf6l } },
    /* 35 << 7 */
    { { 0xecb3ae4bf8e4b065l,0x07a834c42e53022bl,0x1cd300b38692ed96l,
        0x16a6f79261ee14ecl },
      { 0x8f1063c66a8649edl,0xfbcdfcfe869f3e14l,0x2cfb97c100a7b3ecl,
        0xcea49b3c7130c2f1l } },
    /* 36 << 7 */
    { { 0x462d044fe9d96488l,0x4b53d52e8182a0c1l,0x84b6ddd30391e9e9l,
        0x80ab7b48b1741a09l },
      { 0xec0e15d427d3317fl,0x8dfc1ddb1a64671el,0x93cc5d5fd49c5b92l,
        0xc995d53d3674a331l } },
    /* 37 << 7 */
    { { 0x302e41ec090090ael,0x2278a0ccedb06830l,0x1d025932fbc99690l,
        0x0c32fbd2b80d68dal },
      { 0xd79146daf341a6c1l,0xae0ba1391bef68a0l,0xc6b8a5638d774b3al,
        0x1cf307bd880ba4d7l } },
    /* 38 << 7 */
    { { 0xc033bdc719803511l,0xa9f97b3b8888c3bel,0x3d68aebc85c6d05el,
        0xc3b88a9d193919ebl },
      { 0x2d300748c48b0ee3l,0x7506bc7c07a746c1l,0xfc48437c6e6d57f3l,
        0x5bd71587cfeaa91al } },
    /* 39 << 7 */
    { { 0xa4ed0408c1bc5225l,0xd0b946db2719226dl,0x109ecd62758d2d43l,
        0x75c8485a2751759bl },
      { 0xb0b75f499ce4177al,0x4fa61a1e79c10c3dl,0xc062d300a167fcd7l,
        0x4df3874c750f0fa8l } },
    /* 40 << 7 */
    { { 0x29ae2cf983dfedc9l,0xf84371348d87631al,0xaf5717117429c8d2l,
        0x18d15867146d9272l },
      { 0x83053ecf69769bb7l,0xc55eb856c479ab82l,0x5ef7791c21b0f4b2l,
        0xaa5956ba3d491525l } },
    /* 41 << 7 */
    { { 0x407a96c29fe20ebal,0xf27168bbe52a5ad3l,0x43b60ab3bf1d9d89l,
        0xe45c51ef710e727al },
      { 0xdfca5276099b4221l,0x8dc6407c2557a159l,0x0ead833591035895l,
        0x0a9db9579c55dc32l } },
    /* 42 << 7 */
    { { 0xe40736d3df61bc76l,0x13a619c03f778cdbl,0x6dd921a4c56ea28fl,
        0x76a524332fa647b4l },
      { 0x23591891ac5bdc5dl,0xff4a1a72bac7dc01l,0x9905e26162df8453l,
        0x3ac045dfe63b265fl } },
    /* 43 << 7 */
    { { 0x8a3f341bad53dba7l,0x8ec269cc837b625al,0xd71a27823ae31189l,
        0x8fb4f9a355e96120l },
      { 0x804af823ff9875cfl,0x23224f575d442a9bl,0x1c4d3b9eecc62679l,
        0x91da22fba0e7ddb1l } },
    /* 44 << 7 */
    { { 0xa370324d6c04a661l,0x9710d3b65e376d17l,0xed8c98f03044e357l,
        0xc364ebbe6422701cl },
      { 0x347f5d517733d61cl,0xd55644b9cea826c3l,0x80c6e0ad55a25548l,
        0x0aa7641d844220a7l } },
    /* 45 << 7 */
    { { 0x1438ec8131810660l,0x9dfa6507de4b4043l,0x10b515d8cc3e0273l,
        0x1b6066dd28d8cfb2l },
      { 0xd3b045919c9efebdl,0x425d4bdfa21c1ff4l,0x5fe5af19d57607d3l,
        0xbbf773f754481084l } },
    /* 46 << 7 */
    { { 0x8435bd6994b03ed1l,0xd9ad1de3634cc546l,0x2cf423fc00e420cal,
        0xeed26d80a03096ddl },
      { 0xd7f60be7a4db09d2l,0xf47f569d960622f7l,0xe5925fd77296c729l,
        0xeff2db2626ca2715l } },
    /* 47 << 7 */
    { { 0xa6fcd014b913e759l,0x53da47868ff4de93l,0x14616d79c32068e1l,
        0xb187d664ccdf352el },
      { 0xf7afb6501dc90b59l,0x8170e9437daa1b26l,0xc8e3bdd8700c0a84l,
        0x6e8d345f6482bdfal } },
    /* 48 << 7 */
    { { 0x84cfbfa1c5c5ea50l,0xd3baf14c67960681l,0x263984030dd50942l,
        0xe4b7839c4716a663l },
      { 0xd5f1f794e7de6dc0l,0x5cd0f4d4622aa7cel,0x5295f3f159acfeecl,
        0x8d933552953e0607l } },
    /* 49 << 7 */
    { { 0xc7db8ec5776c5722l,0xdc467e622b5f290cl,0xd4297e704ff425a9l,
        0x4be924c10cf7bb72l },
      { 0x0d5dc5aea1892131l,0x8bf8a8e3a705c992l,0x73a0b0647a305ac5l,
        0x00c9ca4e9a8c77a8l } },
    /* 50 << 7 */
    { { 0x5dfee80f83774bddl,0x6313160285734485l,0xa1b524ae914a69a9l,
        0xebc2ffafd4e300d7l },
      { 0x52c93db77cfa46a5l,0x71e6161f21653b50l,0x3574fc57a4bc580al,
        0xc09015dde1bc1253l } },
    /* 51 << 7 */
    { { 0x4b7b47b2d174d7aal,0x4072d8e8f3a15d04l,0xeeb7d47fd6fa07edl,
        0x6f2b9ff9edbdafb1l },
      { 0x18c516153760fe8al,0x7a96e6bff06c6c13l,0x4d7a04100ea2d071l,
        0xa1914e9b0be2a5cel } },
    /* 52 << 7 */
    { { 0x5726e357d8a3c5cfl,0x1197ecc32abb2b13l,0x6c0d7f7f31ae88ddl,
        0x15b20d1afdbb3efel },
      { 0xcd06aa2670584039l,0x2277c969a7dc9747l,0xbca695877855d815l,
        0x899ea2385188b32al } },
    /* 53 << 7 */
    { { 0x37d9228b760c1c9dl,0xc7efbb119b5c18dal,0x7f0d1bc819f6dbc5l,
        0x4875384b07e6905bl },
      { 0xc7c50baa3ba8cd86l,0xb0ce40fbc2905de0l,0x708406737a231952l,
        0xa912a262cf43de26l } },
    /* 54 << 7 */
    { { 0x9c38ddcceb5b76c1l,0x746f528526fc0ab4l,0x52a63a50d62c269fl,
        0x60049c5599458621l },
      { 0xe7f48f823c2f7c9el,0x6bd99043917d5cf3l,0xeb1317a88701f469l,
        0xbd3fe2ed9a449fe0l } },
    /* 55 << 7 */
    { { 0x421e79ca12ef3d36l,0x9ee3c36c3e7ea5del,0xe48198b5cdff36f7l,
        0xaff4f967c6b82228l },
      { 0x15e19dd0c47adb7el,0x45699b23032e7dfal,0x40680c8b1fae026al,
        0x5a347a48550dbf4dl } },
    /* 56 << 7 */
    { { 0xe652533b3cef0d7dl,0xd94f7b182bbb4381l,0x838752be0e80f500l,
        0x8e6e24889e9c9bfbl },
      { 0xc975169716caca6al,0x866c49d838531ad9l,0xc917e2397151ade1l,
        0x2d016ec16037c407l } },
    /* 57 << 7 */
    { { 0xa407ccc900eac3f9l,0x835f6280e2ed4748l,0xcc54c3471cc98e0dl,
        0x0e969937dcb572ebl },
      { 0x1b16c8e88f30c9cbl,0xa606ae75373c4661l,0x47aa689b35502cabl,
        0xf89014ae4d9bb64fl } },
    /* 58 << 7 */
    { { 0x202f6a9c31c71f7bl,0x01f95aa3296ffe5cl,0x5fc0601453cec3a3l,
        0xeb9912375f498a45l },
      { 0xae9a935e5d91ba87l,0xc6ac62810b564a19l,0x8a8fe81c3bd44e69l,
        0x7c8b467f9dd11d45l } },
    /* 59 << 7 */
    { { 0xf772251fea5b8e69l,0xaeecb3bdc5b75fbcl,0x1aca3331887ff0e5l,
        0xbe5d49ff19f0a131l },
      { 0x582c13aae5c8646fl,0xdbaa12e820e19980l,0x8f40f31af7abbd94l,
        0x1f13f5a81dfc7663l } },
    /* 60 << 7 */
    { { 0x5d81f1eeaceb4fc0l,0x362560025e6f0f42l,0x4b67d6d7751370c8l,
        0x2608b69803e80589l },
      { 0xcfc0d2fc05268301l,0xa6943d3940309212l,0x192a90c21fd0e1c2l,
        0xb209f11337f1dc76l } },
    /* 61 << 7 */
    { { 0xefcc5e0697bf1298l,0xcbdb6730219d639el,0xd009c116b81e8c6fl,
        0xa3ffdde31a7ce2e5l },
      { 0xc53fbaaaa914d3bal,0x836d500f88df85eel,0xd98dc71b66ee0751l,
        0x5a3d7005714516fdl } },
    /* 62 << 7 */
    { { 0x21d3634d39eedbbal,0x35cd2e680455a46dl,0xc8cafe65f9d7eb0cl,
        0xbda3ce9e00cefb3el },
      { 0xddc17a602c9cf7a4l,0x01572ee47bcb8773l,0xa92b2b018c7548dfl,
        0x732fd309a84600e3l } },
    /* 63 << 7 */
    { { 0xe22109c716543a40l,0x9acafd36fede3c6cl,0xfb2068526824e614l,
        0x2a4544a9da25dca0l },
      { 0x2598526291d60b06l,0x281b7be928753545l,0xec667b1a90f13b27l,
        0x33a83aff940e2eb4l } },
    /* 64 << 7 */
    { { 0x80009862d5d721d5l,0x0c3357a35bd3a182l,0x27f3a83b7aa2cda4l,
        0xb58ae74ef6f83085l },
      { 0x2a911a812e6dad6bl,0xde286051f43d6c5bl,0x4bdccc41f996c4d8l,
        0xe7312ec00ae1e24el } },
    /* 0 << 14 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 14 */
    { { 0xf8d112e76e6485b3l,0x4d3e24db771c52f8l,0x48e3ee41684a2f6dl,
        0x7161957d21d95551l },
      { 0x19631283cdb12a6cl,0xbf3fa8822e50e164l,0xf6254b633166cc73l,
        0x3aefa7aeaee8cc38l } },
    /* 2 << 14 */
    { { 0x79b0fe623b36f9fdl,0x26543b23fde19fc0l,0x136e64a0958482efl,
        0x23f637719b095825l },
      { 0x14cfd596b6a1142el,0x5ea6aac6335aac0bl,0x86a0e8bdf3081dd5l,
        0x5fb89d79003dc12al } },
    /* 3 << 14 */
    { { 0xf615c33af72e34d4l,0x0bd9ea40110eec35l,0x1c12bc5bc1dea34el,
        0x686584c949ae4699l },
      { 0x13ad95d38c97b942l,0x4609561a4e5c7562l,0x9e94a4aef2737f89l,
        0xf57594c6371c78b6l } },
    /* 4 << 14 */
    { { 0x0f0165fce3779ee3l,0xe00e7f9dbd495d9el,0x1fa4efa220284e7al,
        0x4564bade47ac6219l },
      { 0x90e6312ac4708e8el,0x4f5725fba71e9adfl,0xe95f55ae3d684b9fl,
        0x47f7ccb11e94b415l } },
    /* 5 << 14 */
    { { 0x7322851b8d946581l,0xf0d13133bdf4a012l,0xa3510f696584dae0l,
        0x03a7c1713c9f6c6dl },
      { 0x5be97f38e475381al,0xca1ba42285823334l,0xf83cc5c70be17ddal,
        0x158b14940b918c0fl } },
    /* 6 << 14 */
    { { 0xda3a77e5522e6b69l,0x69c908c3bbcd6c18l,0x1f1b9e48d924fd56l,
        0x37c64e36aa4bb3f7l },
      { 0x5a4fdbdfee478d7dl,0xba75c8bc0193f7a0l,0x84bc1e8456cd16dfl,
        0x1fb08f0846fad151l } },
    /* 7 << 14 */
    { { 0x8a7cabf9842e9f30l,0xa331d4bf5eab83afl,0xd272cfba017f2a6al,
        0x27560abc83aba0e3l },
      { 0x94b833870e3a6b75l,0x25c6aea26b9f50f5l,0x803d691db5fdf6d0l,
        0x03b77509e6333514l } },
    /* 8 << 14 */
    { { 0x3617890361a341c1l,0x3604dc600cfd6142l,0x022295eb8533316cl,
        0x3dbde4ac44af2922l },
      { 0x898afc5d1c7eef69l,0x58896805d14f4fa1l,0x05002160203c21cal,
        0x6f0d1f3040ef730bl } },
    /* 9 << 14 */
    { { 0x8e8c44d4196224f8l,0x75a4ab95374d079dl,0x79085ecc7d48f123l,
        0x56f04d311bf65ad8l },
      { 0xe220bf1cbda602b2l,0x73ee1742f9612c69l,0x76008fc8084fd06bl,
        0x4000ef9ff11380d1l } },
    /* 10 << 14 */
    { { 0x48201b4b12cfe297l,0x3eee129c292f74e5l,0xe1fe114ec9e874e8l,
        0x899b055c92c5fc41l },
      { 0x4e477a643a39c8cfl,0x82f09efe78963cc9l,0x6fd3fd8fd333f863l,
        0x85132b2adc949c63l } },
    /* 11 << 14 */
    { { 0x7e06a3ab516eb17bl,0x73bec06fd2c7372bl,0xe4f74f55ba896da6l,
        0xbb4afef88e9eb40fl },
      { 0x2d75bec8e61d66b0l,0x02bda4b4ef29300bl,0x8bbaa8de026baa5al,
        0xff54befda07f4440l } },
    /* 12 << 14 */
    { { 0xbd9b8b1dbe7a2af3l,0xec51caa94fb74a72l,0xb9937a4b63879697l,
        0x7c9a9d20ec2687d5l },
      { 0x1773e44f6ef5f014l,0x8abcf412e90c6900l,0x387bd0228142161el,
        0x50393755fcb6ff2al } },
    /* 13 << 14 */
    { { 0x9813fd56ed6def63l,0x53cf64827d53106cl,0x991a35bd431f7ac1l,
        0xf1e274dd63e65fafl },
      { 0xf63ffa3c44cc7880l,0x411a426b7c256981l,0xb698b9fd93a420e0l,
        0x89fdddc0ae53f8fel } },
    /* 14 << 14 */
    { { 0x766e072232398baal,0x205fee425cfca031l,0xa49f53417a029cf2l,
        0xa88c68b84023890dl },
      { 0xbc2750417337aaa8l,0x9ed364ad0eb384f4l,0xe0816f8529aba92fl,
        0x2e9e194104e38a88l } },
    /* 15 << 14 */
    { { 0x57eef44a3dafd2d5l,0x35d1fae597ed98d8l,0x50628c092307f9b1l,
        0x09d84aaed6cba5c6l },
      { 0x67071bc788aaa691l,0x2dea57a9afe6cb03l,0xdfe11bb43d78ac01l,
        0x7286418c7fd7aa51l } },
    /* 16 << 14 */
    { { 0xfabf770977f7195al,0x8ec86167adeb838fl,0xea1285a8bb4f012dl,
        0xd68835039a3eab3fl },
      { 0xee5d24f8309004c2l,0xa96e4b7613ffe95el,0x0cdffe12bd223ea4l,
        0x8f5c2ee5b6739a53l } },
    /* 17 << 14 */
    { { 0x5cb4aaa5dd968198l,0xfa131c5272413a6cl,0x53d46a909536d903l,
        0xb270f0d348606d8el },
      { 0x518c7564a053a3bcl,0x088254b71a86caefl,0xb3ba8cb40ab5efd0l,
        0x5c59900e4605945dl } },
    /* 18 << 14 */
    { { 0xecace1dda1887395l,0x40960f36932a65del,0x9611ff5c3aa95529l,
        0xc58215b07c1e5a36l },
      { 0xd48c9b58f0e1a524l,0xb406856bf590dfb8l,0xc7605e049cd95662l,
        0x0dd036eea33ecf82l } },
    /* 19 << 14 */
    { { 0xa50171acc33156b3l,0xf09d24ea4a80172el,0x4e1f72c676dc8eefl,
        0xe60caadc5e3d44eel },
      { 0x006ef8a6979b1d8fl,0x60908a1c97788d26l,0x6e08f95b266feec0l,
        0x618427c222e8c94el } },
    /* 20 << 14 */
    { { 0x3d61333959145a65l,0xcd9bc368fa406337l,0x82d11be32d8a52a0l,
        0xf6877b2797a1c590l },
      { 0x837a819bf5cbdb25l,0x2a4fd1d8de090249l,0x622a7de774990e5fl,
        0x840fa5a07945511bl } },
    /* 21 << 14 */
    { { 0x30b974be6558842dl,0x70df8c6417f3d0a6l,0x7c8035207542e46dl,
        0x7251fe7fe4ecc823l },
      { 0xe59134cb5e9aac9al,0x11bb0934f0045d71l,0x53e5d9b5dbcb1d4el,
        0x8d97a90592defc91l } },
    /* 22 << 14 */
    { { 0xfe2893277946d3f9l,0xe132bd2407472273l,0xeeeb510c1eb6ae86l,
        0x777708c5f0595067l },
      { 0x18e2c8cd1297029el,0x2c61095cbbf9305el,0xe466c2586b85d6d9l,
        0x8ac06c36da1ea530l } },
    /* 23 << 14 */
    { { 0xa365dc39a1304668l,0xe4a9c88507f89606l,0x65a4898facc7228dl,
        0x3e2347ff84ca8303l },
      { 0xa5f6fb77ea7d23a3l,0x2fac257d672a71cdl,0x6908bef87e6a44d3l,
        0x8ff87566891d3d7al } },
    /* 24 << 14 */
    { { 0xe58e90b36b0cf82el,0x6438d2462615b5e7l,0x07b1f8fc669c145al,
        0xb0d8b2da36f1e1cbl },
      { 0x54d5dadbd9184c4dl,0x3dbb18d5f93d9976l,0x0a3e0f56d1147d47l,
        0x2afa8c8da0a48609l } },
    /* 25 << 14 */
    { { 0x275353e8bc36742cl,0x898f427eeea0ed90l,0x26f4947e3e477b00l,
        0x8ad8848a308741e3l },
      { 0x6c703c38d74a2a46l,0x5e3e05a99ba17ba2l,0xc1fa6f664ab9a9e4l,
        0x474a2d9a3841d6ecl } },
    /* 26 << 14 */
    { { 0x871239ad653ae326l,0x14bcf72aa74cbb43l,0x8737650e20d4c083l,
        0x3df86536110ed4afl },
      { 0xd2d86fe7b53ca555l,0x688cb00dabd5d538l,0xcf81bda31ad38468l,
        0x7ccfe3ccf01167b6l } },
    /* 27 << 14 */
    { { 0xcf4f47e06c4c1fe6l,0x557e1f1a298bbb79l,0xf93b974f30d45a14l,
        0x174a1d2d0baf97c4l },
      { 0x7a003b30c51fbf53l,0xd8940991ee68b225l,0x5b0aa7b71c0f4173l,
        0x975797c9a20a7153l } },
    /* 28 << 14 */
    { { 0x26e08c07e3533d77l,0xd7222e6a2e341c99l,0x9d60ec3d8d2dc4edl,
        0xbdfe0d8f7c476cf8l },
      { 0x1fe59ab61d056605l,0xa9ea9df686a8551fl,0x8489941e47fb8d8cl,
        0xfeb874eb4a7f1b10l } },
    /* 29 << 14 */
    { { 0xfe5fea867ee0d98fl,0x201ad34bdbf61864l,0x45d8fe4737c031d4l,
        0xd5f49fae795f0822l },
      { 0xdb0fb291c7f4a40cl,0x2e69d9c1730ddd92l,0x754e105449d76987l,
        0x8a24911d7662db87l } },
    /* 30 << 14 */
    { { 0x61fc181060a71676l,0xe852d1a8f66a8ad1l,0x172bbd656417231el,
        0x0d6de7bd3babb11fl },
      { 0x6fde6f88c8e347f8l,0x1c5875479bd99cc3l,0x78e54ed034076950l,
        0x97f0f334796e83bal } },
    /* 31 << 14 */
    { { 0xe4dbe1ce4924867al,0xbd5f51b060b84917l,0x375300403cb09a79l,
        0xdb3fe0f8ff1743d8l },
      { 0xed7894d8556fa9dbl,0xfa26216923412fbfl,0x563be0dbba7b9291l,
        0x6ca8b8c00c9fb234l } },
    /* 32 << 14 */
    { { 0xed406aa9bd763802l,0xc21486a065303da1l,0x61ae291ec7e62ec4l,
        0x622a0492df99333el },
      { 0x7fd80c9dbb7a8ee0l,0xdc2ed3bc6c01aedbl,0x35c35a1208be74ecl,
        0xd540cb1a469f671fl } },
    /* 33 << 14 */
    { { 0xd16ced4ecf84f6c7l,0x8561fb9c2d090f43l,0x7e693d796f239db4l,
        0xa736f92877bd0d94l },
      { 0x07b4d9292c1950eel,0xda17754356dc11b3l,0xa5dfbbaa7a6a878el,
        0x1c70cb294decb08al } },
    /* 34 << 14 */
    { { 0xfba28c8b6f0f7c50l,0xa8eba2b8854dcc6dl,0x5ff8e89a36b78642l,
        0x070c1c8ef6873adfl },
      { 0xbbd3c3716484d2e4l,0xfb78318f0d414129l,0x2621a39c6ad93b0bl,
        0x979d74c2a9e917f7l } },
    /* 35 << 14 */
    { { 0xfc19564761fb0428l,0x4d78954abee624d4l,0xb94896e0b8ae86fdl,
        0x6667ac0cc91c8b13l },
      { 0x9f18051243bcf832l,0xfbadf8b7a0010137l,0xc69b4089b3ba8aa7l,
        0xfac4bacde687ce85l } },
    /* 36 << 14 */
    { { 0x9164088d977eab40l,0x51f4c5b62760b390l,0xd238238f340dd553l,
        0x358566c3db1d31c9l },
      { 0x3a5ad69e5068f5ffl,0xf31435fcdaff6b06l,0xae549a5bd6debff0l,
        0x59e5f0b775e01331l } },
    /* 37 << 14 */
    { { 0x5d492fb898559acfl,0x96018c2e4db79b50l,0x55f4a48f609f66aal,
        0x1943b3af4900a14fl },
      { 0xc22496df15a40d39l,0xb2a446844c20f7c5l,0x76a35afa3b98404cl,
        0xbec75725ff5d1b77l } },
    /* 38 << 14 */
    { { 0xb67aa163bea06444l,0x27e95bb2f724b6f2l,0x3c20e3e9d238c8abl,
        0x1213754eddd6ae17l },
      { 0x8c431020716e0f74l,0x6679c82effc095c2l,0x2eb3adf4d0ac2932l,
        0x2cc970d301bb7a76l } },
    /* 39 << 14 */
    { { 0x70c71f2f740f0e66l,0x545c616b2b6b23ccl,0x4528cfcbb40a8bd7l,
        0xff8396332ab27722l },
      { 0x049127d9025ac99al,0xd314d4a02b63e33bl,0xc8c310e728d84519l,
        0x0fcb8983b3bc84bal } },
    /* 40 << 14 */
    { { 0x2cc5226138634818l,0x501814f4b44c2e0bl,0xf7e181aa54dfdba3l,
        0xcfd58ff0e759718cl },
      { 0xf90cdb14d3b507a8l,0x57bd478ec50bdad8l,0x29c197e250e5f9aal,
        0x4db6eef8e40bc855l } },
    /* 41 << 14 */
    { { 0x2cc8f21ad1fc0654l,0xc71cc96381269d73l,0xecfbb204077f49f9l,
        0xdde92571ca56b793l },
      { 0x9abed6a3f97ad8f7l,0xe6c19d3f924de3bdl,0x8dce92f4a140a800l,
        0x85f44d1e1337af07l } },
    /* 42 << 14 */
    { { 0x5953c08b09d64c52l,0xa1b5e49ff5df9749l,0x336a8fb852735f7dl,
        0xb332b6db9add676bl },
      { 0x558b88a0b4511aa4l,0x09788752dbd5cc55l,0x16b43b9cd8cd52bdl,
        0x7f0bc5a0c2a2696bl } },
    /* 43 << 14 */
    { { 0x146e12d4c11f61efl,0x9ce107543a83e79el,0x08ec73d96cbfca15l,
        0x09ff29ad5b49653fl },
      { 0xe31b72bde7da946el,0xebf9eb3bee80a4f2l,0xd1aabd0817598ce4l,
        0x18b5fef453f37e80l } },
    /* 44 << 14 */
    { { 0xd5d5cdd35958cd79l,0x3580a1b51d373114l,0xa36e4c91fa935726l,
        0xa38c534def20d760l },
      { 0x7088e40a2ff5845bl,0xe5bb40bdbd78177fl,0x4f06a7a8857f9920l,
        0xe3cc3e50e968f05dl } },
    /* 45 << 14 */
    { { 0x1d68b7fee5682d26l,0x5206f76faec7f87cl,0x41110530041951abl,
        0x58ec52c1d4b5a71al },
      { 0xf3488f990f75cf9al,0xf411951fba82d0d5l,0x27ee75be618895abl,
        0xeae060d46d8aab14l } },
    /* 46 << 14 */
    { { 0x9ae1df737fb54dc2l,0x1f3e391b25963649l,0x242ec32afe055081l,
        0x5bd450ef8491c9bdl },
      { 0x367efc67981eb389l,0xed7e19283a0550d5l,0x362e776bab3ce75cl,
        0xe890e3081f24c523l } },
    /* 47 << 14 */
    { { 0xb961b682feccef76l,0x8b8e11f58bba6d92l,0x8f2ccc4c2b2375c4l,
        0x0d7f7a52e2f86cfal },
      { 0xfd94d30a9efe5633l,0x2d8d246b5451f934l,0x2234c6e3244e6a00l,
        0xde2b5b0dddec8c50l } },
    /* 48 << 14 */
    { { 0x2ce53c5abf776f5bl,0x6f72407160357b05l,0xb259371771bf3f7al,
        0x87d2501c440c4a9fl },
      { 0x440552e187b05340l,0xb7bf7cc821624c32l,0x4155a6ce22facddbl,
        0x5a4228cb889837efl } },
    /* 49 << 14 */
    { { 0xef87d6d6fd4fd671l,0xa233687ec2daa10el,0x7562224403c0eb96l,
        0x7632d1848bf19be6l },
      { 0x05d0f8e940735ff4l,0x3a3e6e13c00931f1l,0x31ccde6adafe3f18l,
        0xf381366acfe51207l } },
    /* 50 << 14 */
    { { 0x24c222a960167d92l,0x62f9d6f87529f18cl,0x412397c00353b114l,
        0x334d89dcef808043l },
      { 0xd9ec63ba2a4383cel,0xcec8e9375cf92ba0l,0xfb8b4288c8be74c0l,
        0x67d6912f105d4391l } },
    /* 51 << 14 */
    { { 0x7b996c461b913149l,0x36aae2ef3a4e02dal,0xb68aa003972de594l,
        0x284ec70d4ec6d545l },
      { 0xf3d2b2d061391d54l,0x69c5d5d6fe114e92l,0xbe0f00b5b4482dffl,
        0xe1596fa5f5bf33c5l } },
    /* 52 << 14 */
    { { 0x10595b5696a71cbal,0x944938b2fdcadeb7l,0xa282da4cfccd8471l,
        0x98ec05f30d37bfe1l },
      { 0xe171ce1b0698304al,0x2d69144421bdf79bl,0xd0cd3b741b21dec1l,
        0x712ecd8b16a15f71l } },
    /* 53 << 14 */
    { { 0x8d4c00a700fd56e1l,0x02ec9692f9527c18l,0x21c449374a3e42e1l,
        0x9176fbab1392ae0al },
      { 0x8726f1ba44b7b618l,0xb4d7aae9f1de491cl,0xf91df7b907b582c0l,
        0x7e116c30ef60aa3al } },
    /* 54 << 14 */
    { { 0x99270f81466265d7l,0xb15b6fe24df7adf0l,0xfe33b2d3f9738f7fl,
        0x48553ab9d6d70f95l },
      { 0x2cc72ac8c21e94dbl,0x795ac38dbdc0bbeel,0x0a1be4492e40478fl,
        0x81bd3394052bde55l } },
    /* 55 << 14 */
    { { 0x63c8dbe956b3c4f2l,0x017a99cf904177ccl,0x947bbddb4d010fc1l,
        0xacf9b00bbb2c9b21l },
      { 0x2970bc8d47173611l,0x1a4cbe08ac7d756fl,0x06d9f4aa67d541a2l,
        0xa3e8b68959c2cf44l } },
    /* 56 << 14 */
    { { 0xaad066da4d88f1ddl,0xc604f1657ad35deal,0x7edc07204478ca67l,
        0xa10dfae0ba02ce06l },
      { 0xeceb1c76af36f4e4l,0x994b2292af3f8f48l,0xbf9ed77b77c8a68cl,
        0x74f544ea51744c9dl } },
    /* 57 << 14 */
    { { 0x82d05bb98113a757l,0x4ef2d2b48a9885e4l,0x1e332be51aa7865fl,
        0x22b76b18290d1a52l },
      { 0x308a231044351683l,0x9d861896a3f22840l,0x5959ddcd841ed947l,
        0x0def0c94154b73bfl } },
    /* 58 << 14 */
    { { 0xf01054174c7c15e0l,0x539bfb023a277c32l,0xe699268ef9dccf5fl,
        0x9f5796a50247a3bdl },
      { 0x8b839de84f157269l,0xc825c1e57a30196bl,0x6ef0aabcdc8a5a91l,
        0xf4a8ce6c498b7fe6l } },
    /* 59 << 14 */
    { { 0x1cce35a770cbac78l,0x83488e9bf6b23958l,0x0341a070d76cb011l,
        0xda6c9d06ae1b2658l },
      { 0xb701fb30dd648c52l,0x994ca02c52fb9fd1l,0x069331176f563086l,
        0x3d2b810017856babl } },
    /* 60 << 14 */
    { { 0xe89f48c85963a46el,0x658ab875a99e61c7l,0x6e296f874b8517b4l,
        0x36c4fcdcfc1bc656l },
      { 0xde5227a1a3906defl,0x9fe95f5762418945l,0x20c91e81fdd96cdel,
        0x5adbe47eda4480del } },
    /* 61 << 14 */
    { { 0xa009370f396de2b6l,0x98583d4bf0ecc7bdl,0xf44f6b57e51d0672l,
        0x03d6b078556b1984l },
      { 0x27dbdd93b0b64912l,0x9b3a343415687b09l,0x0dba646151ec20a9l,
        0xec93db7fff28187cl } },
    /* 62 << 14 */
    { { 0x00ff8c2466e48bddl,0x2514f2f911ccd78el,0xeba11f4fe1250603l,
        0x8a22cd41243fa156l },
      { 0xa4e58df4b283e4c6l,0x78c298598b39783fl,0x5235aee2a5259809l,
        0xc16284b50e0227ddl } },
    /* 63 << 14 */
    { { 0xa5f579161338830dl,0x6d4b8a6bd2123fcal,0x236ea68af9c546f8l,
        0xc1d36873fa608d36l },
      { 0xcd76e4958d436d13l,0xd4d9c2218fb080afl,0x665c1728e8ad3fb5l,
        0xcf1ebe4db3d572e0l } },
    /* 64 << 14 */
    { { 0xa7a8746a584c5e20l,0x267e4ea1b9dc7035l,0x593a15cfb9548c9bl,
        0x5e6e21354bd012f3l },
      { 0xdf31cc6a8c8f936el,0x8af84d04b5c241dcl,0x63990a6f345efb86l,
        0x6fef4e61b9b962cbl } },
    /* 0 << 21 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 21 */
    { { 0xf6368f0925722608l,0x131260db131cf5c6l,0x40eb353bfab4f7acl,
        0x85c7888037eee829l },
      { 0x4c1581ffc3bdf24el,0x5bff75cbf5c3c5a8l,0x35e8c83fa14e6f40l,
        0xb81d1c0f0295e0cal } },
    /* 2 << 21 */
    { { 0xfcde7cc8f43a730fl,0xe89b6f3c33ab590el,0xc823f529ad03240bl,
        0x82b79afe98bea5dbl },
      { 0x568f2856962fe5del,0x0c590adb60c591f3l,0x1fc74a144a28a858l,
        0x3b662498b3203f4cl } },
    /* 3 << 21 */
    { { 0x91e3cf0d6c39765al,0xa2db3acdac3cca0bl,0x288f2f08cb953b50l,
        0x2414582ccf43cf1al },
      { 0x8dec8bbc60eee9a8l,0x54c79f02729aa042l,0xd81cd5ec6532f5d5l,
        0xa672303acf82e15fl } },
    /* 4 << 21 */
    { { 0x376aafa8719c0563l,0xcd8ad2dcbc5fc79fl,0x303fdb9fcb750cd3l,
        0x14ff052f4418b08el },
      { 0xf75084cf3e2d6520l,0x7ebdf0f8144ed509l,0xf43bf0f2d3f25b98l,
        0x86ad71cfa354d837l } },
    /* 5 << 21 */
    { { 0xb827fe9226f43572l,0xdfd3ab5b5d824758l,0x315dd23a539094c1l,
        0x85c0e37a66623d68l },
      { 0x575c79727be19ae0l,0x616a3396df0d36b5l,0xa1ebb3c826b1ff7el,
        0x635b9485140ad453l } },
    /* 6 << 21 */
    { { 0x92bf3cdada430c0bl,0x4702850e3a96dac6l,0xc91cf0a515ac326al,
        0x95de4f49ab8c25e4l },
      { 0xb01bad09e265c17cl,0x24e45464087b3881l,0xd43e583ce1fac5cal,
        0xe17cb3186ead97a6l } },
    /* 7 << 21 */
    { { 0x6cc3924374dcec46l,0x33cfc02d54c2b73fl,0x82917844f26cd99cl,
        0x8819dd95d1773f89l },
      { 0x09572aa60871f427l,0x8e0cf365f6f01c34l,0x7fa52988bff1f5afl,
        0x4eb357eae75e8e50l } },
    /* 8 << 21 */
    { { 0xd9d0c8c4868af75dl,0xd7325cff45c8c7eal,0xab471996cc81ecb0l,
        0xff5d55f3611824edl },
      { 0xbe3145411977a0eel,0x5085c4c5722038c6l,0x2d5335bff94bb495l,
        0x894ad8a6c8e2a082l } },
    /* 9 << 21 */
    { { 0x5c3e2341ada35438l,0xf4a9fc89049b8c4el,0xbeeb355a9f17cf34l,
        0x3f311e0e6c91fe10l },
      { 0xc2d2003892ab9891l,0x257bdcc13e8ce9a9l,0x1b2d978988c53beel,
        0x927ce89acdba143al } },
    /* 10 << 21 */
    { { 0xb0a32cca523db280l,0x5c889f8a50d43783l,0x503e04b34897d16fl,
        0x8cdb6e7808f5f2e8l },
      { 0x6ab91cf0179c8e74l,0xd8874e5248211d60l,0xf948d4d5ea851200l,
        0x4076d41ee6f9840al } },
    /* 11 << 21 */
    { { 0xc20e263c47b517eal,0x79a448fd30685e5el,0xe55f6f78f90631a0l,
        0x88a790b1a79e6346l },
      { 0x62160c7d80969fe8l,0x54f92fd441491bb9l,0xa6645c235c957526l,
        0xf44cc5aebea3ce7bl } },
    /* 12 << 21 */
    { { 0xf76283278b1e68b7l,0xc731ad7a303f29d3l,0xfe5a9ca957d03ecbl,
        0x96c0d50c41bc97a7l },
      { 0xc4669fe79b4f7f24l,0xfdd781d83d9967efl,0x7892c7c35d2c208dl,
        0x8bf64f7cae545cb3l } },
    /* 13 << 21 */
    { { 0xc01f862c467be912l,0xf4c85ee9c73d30ccl,0x1fa6f4be6ab83ec7l,
        0xa07a3c1c4e3e3cf9l },
      { 0x87f8ef450c00beb3l,0x30e2c2b3000d4c3el,0x1aa00b94fe08bf5bl,
        0x32c133aa9224ef52l } },
    /* 14 << 21 */
    { { 0x38df16bb32e5685dl,0x68a9e06958e6f544l,0x495aaff7cdc5ebc6l,
        0xf894a645378b135fl },
      { 0xf316350a09e27ecfl,0xeced201e58f7179dl,0x2eec273ce97861bal,
        0x47ec2caed693be2el } },
    /* 15 << 21 */
    { { 0xfa4c97c4f68367cel,0xe4f47d0bbe5a5755l,0x17de815db298a979l,
        0xd7eca659c177dc7dl },
      { 0x20fdbb7149ded0a3l,0x4cb2aad4fb34d3c5l,0x2cf31d2860858a33l,
        0x3b6873efa24aa40fl } },
    /* 16 << 21 */
    { { 0x540234b22c11bb37l,0x2d0366dded4c74a3l,0xf9a968daeec5f25dl,
        0x3660106867b63142l },
      { 0x07cd6d2c68d7b6d4l,0xa8f74f090c842942l,0xe27514047768b1eel,
        0x4b5f7e89fe62aee4l } },
    /* 17 << 21 */
    { { 0xc6a7717789070d26l,0xa1f28e4edd1c8bc7l,0xea5f4f06469e1f17l,
        0x78fc242afbdb78e0l },
      { 0xc9c7c5928b0588f1l,0xb6b7a0fd1535921el,0xcc5bdb91bde5ae35l,
        0xb42c485e12ff1864l } },
    /* 18 << 21 */
    { { 0xa1113e13dbab98aal,0xde9d469ba17b1024l,0x23f48b37c0462d3al,
        0x3752e5377c5c078dl },
      { 0xe3a86add15544eb9l,0xf013aea780fba279l,0x8b5bb76cf22001b5l,
        0xe617ba14f02891abl } },
    /* 19 << 21 */
    { { 0xd39182a6936219d3l,0x5ce1f194ae51cb19l,0xc78f8598bf07a74cl,
        0x6d7158f222cbf1bcl },
      { 0x3b846b21e300ce18l,0x35fba6302d11275dl,0x5fe25c36a0239b9bl,
        0xd8beb35ddf05d940l } },
    /* 20 << 21 */
    { { 0x4db02bb01f7e320dl,0x0641c3646da320eal,0x6d95fa5d821389a3l,
        0x926997488fcd8e3dl },
      { 0x316fef17ceb6c143l,0x67fcb841d933762bl,0xbb837e35118b17f8l,
        0x4b92552f9fd24821l } },
    /* 21 << 21 */
    { { 0xae6bc70e46aca793l,0x1cf0b0e4e579311bl,0x8dc631be5802f716l,
        0x099bdc6fbddbee4dl },
      { 0xcc352bb20caf8b05l,0xf74d505a72d63df2l,0xb9876d4b91c4f408l,
        0x1ce184739e229b2dl } },
    /* 22 << 21 */
    { { 0x4950759783abdb4al,0x850fbcb6dee84b18l,0x6325236e609e67dcl,
        0x04d831d99336c6d8l },
      { 0x8deaae3bfa12d45dl,0xe425f8ce4746e246l,0x8004c17524f5f31el,
        0xaca16d8fad62c3b7l } },
    /* 23 << 21 */
    { { 0x0dc15a6a9152f934l,0xf1235e5ded0e12c1l,0xc33c06ecda477dacl,
        0x76be8732b2ea0006l },
      { 0xcf3f78310c0cd313l,0x3c524553a614260dl,0x31a756f8cab22d15l,
        0x03ee10d177827a20l } },
    /* 24 << 21 */
    { { 0xd1e059b21994ef20l,0x2a653b69638ae318l,0x70d5eb582f699010l,
        0x279739f709f5f84al },
      { 0x5da4663c8b799336l,0xfdfdf14d203c37ebl,0x32d8a9dca1dbfb2dl,
        0xab40cff077d48f9bl } },
    /* 25 << 21 */
    { { 0xc018b383d20b42d5l,0xf9a810ef9f78845fl,0x40af3753bdba9df0l,
        0xb90bdcfc131dfdf9l },
      { 0x18720591f01ab782l,0xc823f2116af12a88l,0xa51b80f30dc14401l,
        0xde248f77fb2dfbe3l } },
    /* 26 << 21 */
    { { 0xef5a44e50cafe751l,0x73997c9cd4dcd221l,0x32fd86d1de854024l,
        0xd5b53adca09b84bbl },
      { 0x008d7a11dcedd8d1l,0x406bd1c874b32c84l,0x5d4472ff05dde8b1l,
        0x2e25f2cdfce2b32fl } },
    /* 27 << 21 */
    { { 0xbec0dd5e29dfc254l,0x4455fcf62b98b267l,0x0b4d43a5c72df2adl,
        0xea70e6be48a75397l },
      { 0x2aad61695820f3bfl,0xf410d2dd9e37f68fl,0x70fb7dba7be5ac83l,
        0x636bb64536ec3eecl } },
    /* 28 << 21 */
    { { 0x27104ea39754e21cl,0xbc87a3e68d63c373l,0x483351d74109db9al,
        0x0fa724e360134da7l },
      { 0x9ff44c29b0720b16l,0x2dd0cf1306aceeadl,0x5942758ce26929a6l,
        0x96c5db92b766a92bl } },
    /* 29 << 21 */
    { { 0xcec7d4c05f18395el,0xd3f227441f80d032l,0x7a68b37acb86075bl,
        0x074764ddafef92dbl },
      { 0xded1e9507bc7f389l,0xc580c850b9756460l,0xaeeec2a47da48157l,
        0x3f0b4e7f82c587b3l } },
    /* 30 << 21 */
    { { 0x231c6de8a9f19c53l,0x5717bd736974e34el,0xd9e1d216f1508fa9l,
        0x9f112361dadaa124l },
      { 0x80145e31823b7348l,0x4dd8f0d5ac634069l,0xe3d82fc72297c258l,
        0x276fcfee9cee7431l } },
    /* 31 << 21 */
    { { 0x8eb61b5e2bc0aea9l,0x4f668fd5de329431l,0x03a32ab138e4b87el,
        0xe137451773d0ef0bl },
      { 0x1a46f7e6853ac983l,0xc3bdf42e68e78a57l,0xacf207852ea96dd1l,
        0xa10649b9f1638460l } },
    /* 32 << 21 */
    { { 0xf2369f0b879fbbedl,0x0ff0ae86da9d1869l,0x5251d75956766f45l,
        0x4984d8c02be8d0fcl },
      { 0x7ecc95a6d21008f0l,0x29bd54a03a1a1c49l,0xab9828c5d26c50f3l,
        0x32c0087c51d0d251l } },
    /* 33 << 21 */
    { { 0x9bac3ce60c1cdb26l,0xcd94d947557ca205l,0x1b1bd5989db1fdcdl,
        0x0eda0108a3d8b149l },
      { 0x9506661056152fccl,0xc2f037e6e7192b33l,0xdeffb41ac92e05a4l,
        0x1105f6c2c2f6c62el } },
    /* 34 << 21 */
    { { 0x68e735008733913cl,0xcce861633f3adc40l,0xf407a94238a278e9l,
        0xd13c1b9d2ab21292l },
      { 0x93ed7ec71c74cf5cl,0x8887dc48f1a4c1b4l,0x3830ff304b3a11f1l,
        0x358c5a3c58937cb6l } },
    /* 35 << 21 */
    { { 0x027dc40489022829l,0x40e939773b798f79l,0x90ad333738be6eadl,
        0x9c23f6bcf34c0a5dl },
      { 0xd1711a35fbffd8bbl,0x60fcfb491949d3ddl,0x09c8ef4b7825d93al,
        0x24233cffa0a8c968l } },
    /* 36 << 21 */
    { { 0x67ade46ce6d982afl,0xebb6bf3ee7544d7cl,0xd6b9ba763d8bd087l,
        0x46fe382d4dc61280l },
      { 0xbd39a7e8b5bdbd75l,0xab381331b8f228fel,0x0709a77cce1c4300l,
        0x6a247e56f337ceacl } },
    /* 37 << 21 */
    { { 0x8f34f21b636288bel,0x9dfdca74c8a7c305l,0x6decfd1bea919e04l,
        0xcdf2688d8e1991f8l },
      { 0xe607df44d0f8a67el,0xd985df4b0b58d010l,0x57f834c50c24f8f4l,
        0xe976ef56a0bf01ael } },
    /* 38 << 21 */
    { { 0x536395aca1c32373l,0x351027aa734c0a13l,0xd2f1b5d65e6bd5bcl,
        0x2b539e24223debedl },
      { 0xd4994cec0eaa1d71l,0x2a83381d661dcf65l,0x5f1aed2f7b54c740l,
        0x0bea3fa5d6dda5eel } },
    /* 39 << 21 */
    { { 0x9d4fb68436cc6134l,0x8eb9bbf3c0a443ddl,0xfc500e2e383b7d2al,
        0x7aad621c5b775257l },
      { 0x69284d740a8f7cc0l,0xe820c2ce07562d65l,0xbf9531b9499758eel,
        0x73e95ca56ee0cc2dl } },
    /* 40 << 21 */
    { { 0xf61790abfbaf50a5l,0xdf55e76b684e0750l,0xec516da7f176b005l,
        0x575553bb7a2dddc7l },
      { 0x37c87ca3553afa73l,0x315f3ffc4d55c251l,0xe846442aaf3e5d35l,
        0x61b911496495ff28l } },
    /* 41 << 21 */
    { { 0x23cc95d3fa326dc3l,0x1df4da1f18fc2ceal,0x24bf9adcd0a37d59l,
        0xb6710053320d6e1el },
      { 0x96f9667e618344d1l,0xcc7ce042a06445afl,0xa02d8514d68dbc3al,
        0x4ea109e4280b5a5bl } },
    /* 42 << 21 */
    { { 0x5741a7acb40961bfl,0x4ada59376aa56bfal,0x7feb914502b765d1l,
        0x561e97bee6ad1582l },
      { 0xbbc4a5b6da3982f5l,0x0c2659edb546f468l,0xb8e7e6aa59612d20l,
        0xd83dfe20ac19e8e0l } },
    /* 43 << 21 */
    { { 0x8530c45fb835398cl,0x6106a8bfb38a41c2l,0x21e8f9a635f5dcdbl,
        0x39707137cae498edl },
      { 0x70c23834d8249f00l,0x9f14b58fab2537a0l,0xd043c3655f61c0c2l,
        0xdc5926d609a194a7l } },
    /* 44 << 21 */
    { { 0xddec03398e77738al,0xd07a63effba46426l,0x2e58e79cee7f6e86l,
        0xe59b0459ff32d241l },
      { 0xc5ec84e520fa0338l,0x97939ac8eaff5acel,0x0310a4e3b4a38313l,
        0x9115fba28f9d9885l } },
    /* 45 << 21 */
    { { 0x8dd710c25fadf8c3l,0x66be38a2ce19c0e2l,0xd42a279c4cfe5022l,
        0x597bb5300e24e1b8l },
      { 0x3cde86b7c153ca7fl,0xa8d30fb3707d63bdl,0xac905f92bd60d21el,
        0x98e7ffb67b9a54abl } },
    /* 46 << 21 */
    { { 0xd7147df8e9726a30l,0xb5e216ffafce3533l,0xb550b7992ff1ec40l,
        0x6b613b87a1e953fdl },
      { 0x87b88dba792d5610l,0x2ee1270aa190fbe1l,0x02f4e2dc2ef581dal,
        0x016530e4eff82a95l } },
    /* 47 << 21 */
    { { 0xcbb93dfd8fd6ee89l,0x16d3d98646848fffl,0x600eff241da47adfl,
        0x1b9754a00ad47a71l },
      { 0x8f9266df70c33b98l,0xaadc87aedf34186el,0x0d2ce8e14ad24132l,
        0x8a47cbfc19946ebal } },
    /* 48 << 21 */
    { { 0x47feeb6662b5f3afl,0xcefab5610abb3734l,0x449de60e19f35cb1l,
        0x39f8db14157f0eb9l },
      { 0xffaecc5b3c61bfd6l,0xa5a4d41d41216703l,0x7f8fabed224e1cc2l,
        0x0d5a8186871ad953l } },
    /* 49 << 21 */
    { { 0xf10774f7d22da9a9l,0x45b8a678cc8a9b0dl,0xd9c2e722bdc32cffl,
        0xbf71b5f5337202a5l },
      { 0x95c57f2f69fc4db9l,0xb6dad34c765d01e1l,0x7e0bd13fcb904635l,
        0x61751253763a588cl } },
    /* 50 << 21 */
    { { 0xd85c299781af2c2dl,0xc0f7d9c481b9d7dal,0x838a34ae08533e8dl,
        0x15c4cb08311d8311l },
      { 0x97f832858e121e14l,0xeea7dc1e85000a5fl,0x0c6059b65d256274l,
        0xec9beaceb95075c0l } },
    /* 51 << 21 */
    { { 0x173daad71df97828l,0xbf851cb5a8937877l,0xb083c59401646f3cl,
        0x3bad30cf50c6d352l },
      { 0xfeb2b202496bbceal,0x3cf9fd4f18a1e8bal,0xd26de7ff1c066029l,
        0x39c81e9e4e9ed4f8l } },
    /* 52 << 21 */
    { { 0xd8be0cb97b390d35l,0x01df2bbd964aab27l,0x3e8c1a65c3ef64f8l,
        0x567291d1716ed1ddl },
      { 0x95499c6c5f5406d3l,0x71fdda395ba8e23fl,0xcfeb320ed5096ecel,
        0xbe7ba92bca66dd16l } },
    /* 53 << 21 */
    { { 0x4608d36bc6fb5a7dl,0xe3eea15a6d2dd0e0l,0x75b0a3eb8f97a36al,
        0xf59814cc1c83de1el },
      { 0x56c9c5b01c33c23fl,0xa96c1da46faa4136l,0x46bf2074de316551l,
        0x3b866e7b1f756c8fl } },
    /* 54 << 21 */
    { { 0x727727d81495ed6bl,0xb2394243b682dce7l,0x8ab8454e758610f3l,
        0xc243ce84857d72a4l },
      { 0x7b320d71dbbf370fl,0xff9afa3778e0f7cal,0x0119d1e0ea7b523fl,
        0xb997f8cb058c7d42l } },
    /* 55 << 21 */
    { { 0x285bcd2a37bbb184l,0x51dcec49a45d1fa6l,0x6ade3b64e29634cbl,
        0x080c94a726b86ef1l },
      { 0xba583db12283fbe3l,0x902bddc85a9315edl,0x07c1ccb386964becl,
        0x78f4eacfb6258301l } },
    /* 56 << 21 */
    { { 0x4bdf3a4956f90823l,0xba0f5080741d777bl,0x091d71c3f38bf760l,
        0x9633d50f9b625b02l },
      { 0x03ecb743b8c9de61l,0xb47512545de74720l,0x9f9defc974ce1cb2l,
        0x774a4f6a00bd32efl } },
    /* 57 << 21 */
    { { 0xaca385f773848f22l,0x53dad716f3f8558el,0xab7b34b093c471f9l,
        0xf530e06919644bc7l },
      { 0x3d9fb1ffdd59d31al,0x4382e0df08daa795l,0x165c6f4bd5cc88d7l,
        0xeaa392d54a18c900l } },
    /* 58 << 21 */
    { { 0x94203c67648024eel,0x188763f28c2fabcdl,0xa80f87acbbaec835l,
        0x632c96e0f29d8d54l },
      { 0x29b0a60e4c00a95el,0x2ef17f40e011e9fal,0xf6c0e1d115b77223l,
        0xaaec2c6214b04e32l } },
    /* 59 << 21 */
    { { 0xd35688d83d84e58cl,0x2af5094c958571dbl,0x4fff7e19760682a6l,
        0x4cb27077e39a407cl },
      { 0x0f59c5474ff0e321l,0x169f34a61b34c8ffl,0x2bff109652bc1ba7l,
        0xa25423b783583544l } },
    /* 60 << 21 */
    { { 0x5d55d5d50ac8b782l,0xff6622ec2db3c892l,0x48fce7416b8bb642l,
        0x31d6998c69d7e3dcl },
      { 0xdbaf8004cadcaed0l,0x801b0142d81d053cl,0x94b189fc59630ec6l,
        0x120e9934af762c8el } },
    /* 61 << 21 */
    { { 0x53a29aa4fdc6a404l,0x19d8e01ea1909948l,0x3cfcabf1d7e89681l,
        0x3321a50d4e132d37l },
      { 0xd0496863e9a86111l,0x8c0cde6106a3bc65l,0xaf866c49fc9f8eefl,
        0x2066350eff7f5141l } },
    /* 62 << 21 */
    { { 0x4f8a4689e56ddfbdl,0xea1b0c07fe32983al,0x2b317462873cb8cbl,
        0x658deddc2d93229fl },
      { 0x65efaf4d0f64ef58l,0xfe43287d730cc7a8l,0xaebc0c723d047d70l,
        0x92efa539d92d26c9l } },
    /* 63 << 21 */
    { { 0x06e7845794b56526l,0x415cb80f0961002dl,0x89e5c56576dcb10fl,
        0x8bbb6982ff9259fel },
      { 0x4fe8795b9abc2668l,0xb5d4f5341e678fb1l,0x6601f3be7b7da2b9l,
        0x98da59e2a13d6805l } },
    /* 64 << 21 */
    { { 0x190d8ea601799a52l,0xa20cec41b86d2952l,0x3062ffb27fff2a7cl,
        0x741b32e579f19d37l },
      { 0xf80d81814eb57d47l,0x7a2d0ed416aef06bl,0x09735fb01cecb588l,
        0x1641caaac6061f5bl } },
    /* 0 << 28 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 28 */
    { { 0x7f99824f20151427l,0x206828b692430206l,0xaa9097d7e1112357l,
        0xacf9a2f209e414ecl },
      { 0xdbdac9da27915356l,0x7e0734b7001efee3l,0x54fab5bbd2b288e2l,
        0x4c630fc4f62dd09cl } },
    /* 2 << 28 */
    { { 0x8537107a1ac2703bl,0xb49258d86bc857b5l,0x57df14debcdaccd1l,
        0x24ab68d7c4ae8529l },
      { 0x7ed8b5d4734e59d0l,0x5f8740c8c495cc80l,0x84aedd5a291db9b3l,
        0x80b360f84fb995bel } },
    /* 3 << 28 */
    { { 0xae915f5d5fa067d1l,0x4134b57f9668960cl,0xbd3656d6a48edaacl,
        0xdac1e3e4fc1d7436l },
      { 0x674ff869d81fbb26l,0x449ed3ecb26c33d4l,0x85138705d94203e8l,
        0xccde538bbeeb6f4al } },
    /* 4 << 28 */
    { { 0x55d5c68da61a76fal,0x598b441dca1554dcl,0xd39923b9773b279cl,
        0x33331d3c36bf9efcl },
      { 0x2d4c848e298de399l,0xcfdb8e77a1a27f56l,0x94c855ea57b8ab70l,
        0xdcdb9dae6f7879bal } },
    /* 5 << 28 */
    { { 0x7bdff8c2019f2a59l,0xb3ce5bb3cb4fbc74l,0xea907f688a9173ddl,
        0x6cd3d0d395a75439l },
      { 0x92ecc4d6efed021cl,0x09a9f9b06a77339al,0x87ca6b157188c64al,
        0x10c2996844899158l } },
    /* 6 << 28 */
    { { 0x5859a229ed6e82efl,0x16f338e365ebaf4el,0x0cd313875ead67ael,
        0x1c73d22854ef0bb4l },
      { 0x4cb5513174a5c8c7l,0x01cd29707f69ad6al,0xa04d00dde966f87el,
        0xd96fe4470b7b0321l } },
    /* 7 << 28 */
    { { 0x342ac06e88fbd381l,0x02cd4a845c35a493l,0xe8fa89de54f1bbcdl,
        0x341d63672575ed4cl },
      { 0xebe357fbd238202bl,0x600b4d1aa984ead9l,0xc35c9f4452436ea0l,
        0x96fe0a39a370751bl } },
    /* 8 << 28 */
    { { 0x4c4f07367f636a38l,0x9f943fb70e76d5cbl,0xb03510baa8b68b8bl,
        0xc246780a9ed07a1fl },
      { 0x3c0514156d549fc2l,0xc2953f31607781cal,0x955e2c69d8d95413l,
        0xb300fadc7bd282e3l } },
    /* 9 << 28 */
    { { 0x81fe7b5087e9189fl,0xdb17375cf42dda27l,0x22f7d896cf0a5904l,
        0xa0e57c5aebe348e6l },
      { 0xa61011d3f40e3c80l,0xb11893218db705c5l,0x4ed9309e50fedec3l,
        0xdcf14a104d6d5c1dl } },
    /* 10 << 28 */
    { { 0x056c265b55691342l,0xe8e0850491049dc7l,0x131329f5c9bae20al,
        0x96c8b3e8d9dccdb4l },
      { 0x8c5ff838fb4ee6b4l,0xfc5a9aeb41e8ccf0l,0x7417b764fae050c6l,
        0x0953c3d700452080l } },
    /* 11 << 28 */
    { { 0x2137268238dfe7e8l,0xea417e152bb79d4bl,0x59641f1c76e7cf2dl,
        0x271e3059ea0bcfccl },
      { 0x624c7dfd7253ecbdl,0x2f552e254fca6186l,0xcbf84ecd4d866e9cl,
        0x73967709f68d4610l } },
    /* 12 << 28 */
    { { 0xa14b1163c27901b4l,0xfd9236e0899b8bf3l,0x42b091eccbc6da0al,
        0xbb1dac6f5ad1d297l },
      { 0x80e61d53a91cf76el,0x4110a412d31f1ee7l,0x2d87c3ba13efcf77l,
        0x1f374bb4df450d76l } },
    /* 13 << 28 */
    { { 0x5e78e2f20d188dabl,0xe3968ed0f4b885efl,0x46c0568e7314570fl,
        0x3161633801170521l },
      { 0x18e1e7e24f0c8afel,0x4caa75ffdeea78dal,0x82db67f27c5d8a51l,
        0x36a44d866f505370l } },
    /* 14 << 28 */
    { { 0xd72c5bda0333974fl,0x5db516ae27a70146l,0x34705281210ef921l,
        0xbff17a8f0c9c38e5l },
      { 0x78f4814e12476da1l,0xc1e1661333c16980l,0x9e5b386f424d4bcal,
        0x4c274e87c85740del } },
    /* 15 << 28 */
    { { 0xb6a9b88d6c2f5226l,0x14d1b944550d7ca8l,0x580c85fc1fc41709l,
        0xc1da368b54c6d519l },
      { 0x2b0785ced5113cf7l,0x0670f6335a34708fl,0x46e2376715cc3f88l,
        0x1b480cfa50c72c8fl } },
    /* 16 << 28 */
    { { 0x202886024147519al,0xd0981eac26b372f0l,0xa9d4a7caa785ebc8l,
        0xd953c50ddbdf58e9l },
      { 0x9d6361ccfd590f8fl,0x72e9626b44e6c917l,0x7fd9611022eb64cfl,
        0x863ebb7e9eb288f3l } },
    /* 17 << 28 */
    { { 0x6e6ab7616aca8ee7l,0x97d10b39d7b40358l,0x1687d3771e5feb0dl,
        0xc83e50e48265a27al },
      { 0x8f75a9fec954b313l,0xcc2e8f47310d1f61l,0xf5ba81c56557d0e0l,
        0x25f9680c3eaf6207l } },
    /* 18 << 28 */
    { { 0xf95c66094354080bl,0x5225bfa57bf2fe1cl,0xc5c004e25c7d98fal,
        0x3561bf1c019aaf60l },
      { 0x5e6f9f17ba151474l,0xdec2f934b04f6ecal,0x64e368a1269acb1el,
        0x1332d9e40cdda493l } },
    /* 19 << 28 */
    { { 0x60d6cf69df23de05l,0x66d17da2009339a0l,0x9fcac9850a693923l,
        0xbcf057fced7c6a6dl },
      { 0xc3c5c8c5f0b5662cl,0x25318dd8dcba4f24l,0x60e8cb75082b69ffl,
        0x7c23b3ee1e728c01l } },
    /* 20 << 28 */
    { { 0x15e10a0a097e4403l,0xcb3d0a8619854665l,0x88d8e211d67d4826l,
        0xb39af66e0b9d2839l },
      { 0xa5f94588bd475ca8l,0xe06b7966c077b80bl,0xfedb1485da27c26cl,
        0xd290d33afe0fd5e0l } },
    /* 21 << 28 */
    { { 0xa40bcc47f34fb0fal,0xb4760cc81fb1ab09l,0x8fca0993a273bfe3l,
        0x13e4fe07f70b213cl },
      { 0x3bcdb992fdb05163l,0x8c484b110c2b19b6l,0x1acb815faaf2e3e2l,
        0xc6905935b89ff1b4l } },
    /* 22 << 28 */
    { { 0xb2ad6f9d586e74e1l,0x488883ad67b80484l,0x758aa2c7369c3ddbl,
        0x8ab74e699f9afd31l },
      { 0x10fc2d285e21beb1l,0x3484518a318c42f9l,0x377427dc53cf40c3l,
        0x9de0781a391bc1d9l } },
    /* 23 << 28 */
    { { 0x8faee858693807e1l,0xa38653274e81ccc7l,0x02c30ff26f835b84l,
        0xb604437b0d3d38d4l },
      { 0xb3fc8a985ca1823dl,0xb82f7ec903be0324l,0xee36d761cf684a33l,
        0x5a01df0e9f29bf7dl } },
    /* 24 << 28 */
    { { 0x686202f31306583dl,0x05b10da0437c622el,0xbf9aaa0f076a7bc8l,
        0x25e94efb8f8f4e43l },
      { 0x8a35c9b7fa3dc26dl,0xe0e5fb9396ff03c5l,0xa77e3843ebc394cel,
        0xcede65958361de60l } },
    /* 25 << 28 */
    { { 0xd27c22f6a1993545l,0xab01cc3624d671bal,0x63fa2877a169c28el,
        0x925ef9042eb08376l },
      { 0x3b2fa3cf53aa0b32l,0xb27beb5b71c49d7al,0xb60e1834d105e27fl,
        0xd60897884f68570dl } },
    /* 26 << 28 */
    { { 0x23094ce0d6fbc2acl,0x738037a1815ff551l,0xda73b1bb6bef119cl,
        0xdcf6c430eef506bal },
      { 0x00e4fe7be3ef104al,0xebdd9a2c0a065628l,0x853a81c38792043el,
        0x22ad6eceb3b59108l } },
    /* 27 << 28 */
    { { 0x9fb813c039cd297dl,0x8ec7e16e05bda5d9l,0x2834797c0d104b96l,
        0xcc11a2e77c511510l },
      { 0x96ca5a5396ee6380l,0x054c8655cea38742l,0xb5946852d54dfa7dl,
        0x97c422e71f4ab207l } },
    /* 28 << 28 */
    { { 0xbf9075090c22b540l,0x2cde42aab7c267d4l,0xba18f9ed5ab0d693l,
        0x3ba62aa66e4660d9l },
      { 0xb24bf97bab9ea96al,0x5d039642e3b60e32l,0x4e6a45067c4d9bd5l,
        0x666c5b9e7ed4a6a4l } },
    /* 29 << 28 */
    { { 0xfa3fdcd98edbd7ccl,0x4660bb87c6ccd753l,0x9ae9082021e6b64fl,
        0x8a56a713b36bfb3fl },
      { 0xabfce0965726d47fl,0x9eed01b20b1a9a7fl,0x30e9cad44eb74a37l,
        0x7b2524cc53e9666dl } },
    /* 30 << 28 */
    { { 0x6a29683b8f4b002fl,0xc2200d7a41f4fc20l,0xcf3af47a3a338accl,
        0x6539a4fbe7128975l },
      { 0xcec31c14c33c7fcfl,0x7eb6799bc7be322bl,0x119ef4e96646f623l,
        0x7b7a26a554d7299bl } },
    /* 31 << 28 */
    { { 0xcb37f08d403f46f2l,0x94b8fc431a0ec0c7l,0xbb8514e3c332142fl,
        0xf3ed2c33e80d2a7al },
      { 0x8d2080afb639126cl,0xf7b6be60e3553adel,0x3950aa9f1c7e2b09l,
        0x847ff9586410f02bl } },
    /* 32 << 28 */
    { { 0x877b7cf5678a31b0l,0xd50301ae3998b620l,0x734257c5c00fb396l,
        0xf9fb18a004e672a6l },
      { 0xff8bd8ebe8758851l,0x1e64e4c65d99ba44l,0x4b8eaedf7dfd93b7l,
        0xba2f2a9804e76b8cl } },
    /* 33 << 28 */
    { { 0x7d790cbae8053433l,0xc8e725a03d2c9585l,0x58c5c476cdd8f5edl,
        0xd106b952efa9fe1dl },
      { 0x3c5c775b0eff13a9l,0x242442bae057b930l,0xe9f458d4c9b70cbdl,
        0x69b71448a3cdb89al } },
    /* 34 << 28 */
    { { 0x41ee46f60e2ed742l,0x573f104540067493l,0xb1e154ff9d54c304l,
        0x2ad0436a8d3a7502l },
      { 0xee4aaa2d431a8121l,0xcd38b3ab886f11edl,0x57d49ea6034a0eb7l,
        0xd2b773bdf7e85e58l } },
    /* 35 << 28 */
    { { 0x4a559ac49b5c1f14l,0xc444be1a3e54df2bl,0x13aad704eda41891l,
        0xcd927bec5eb5c788l },
      { 0xeb3c8516e48c8a34l,0x1b7ac8124b546669l,0x1815f896594df8ecl,
        0x87c6a79c79227865l } },
    /* 36 << 28 */
    { { 0xae02a2f09b56ddbdl,0x1339b5ac8a2f1cf3l,0xf2b569c7839dff0dl,
        0xb0b9e864fee9a43dl },
      { 0x4ff8ca4177bb064el,0x145a2812fd249f63l,0x3ab7beacf86f689al,
        0x9bafec2701d35f5el } },
    /* 37 << 28 */
    { { 0x28054c654265aa91l,0xa4b18304035efe42l,0x6887b0e69639dec7l,
        0xf4b8f6ad3d52aea5l },
      { 0xfb9293cc971a8a13l,0x3f159e5d4c934d07l,0x2c50e9b109acbc29l,
        0x08eb65e67154d129l } },
    /* 38 << 28 */
    { { 0x4feff58930b75c3el,0x0bb82fe294491c93l,0xd8ac377a89af62bbl,
        0xd7b514909685e49fl },
      { 0xabca9a7b04497f19l,0x1b35ed0a1a7ad13fl,0x6b601e213ec86ed6l,
        0xda91fcb9ce0c76f1l } },
    /* 39 << 28 */
    { { 0x9e28507bd7ab27e1l,0x7c19a55563945b7bl,0x6b43f0a1aafc9827l,
        0x443b4fbd3aa55b91l },
      { 0x962b2e656962c88fl,0x139da8d4ce0db0cal,0xb93f05dd1b8d6c4fl,
        0x779cdff7180b9824l } },
    /* 40 << 28 */
    { { 0xbba23fddae57c7b7l,0x345342f21b932522l,0xfd9c80fe556d4aa3l,
        0xa03907ba6525bb61l },
      { 0x38b010e1ff218933l,0xc066b654aa52117bl,0x8e14192094f2e6eal,
        0x66a27dca0d32f2b2l } },
    /* 41 << 28 */
    { { 0x69c7f993048b3717l,0xbf5a989ab178ae1cl,0x49fa9058564f1d6bl,
        0x27ec6e15d31fde4el },
      { 0x4cce03737276e7fcl,0x64086d7989d6bf02l,0x5a72f0464ccdd979l,
        0x909c356647775631l } },
    /* 42 << 28 */
    { { 0x1c07bc6b75dd7125l,0xb4c6bc9787a0428dl,0x507ece52fdeb6b9dl,
        0xfca56512b2c95432l },
      { 0x15d97181d0e8bd06l,0x384dd317c6bb46eal,0x5441ea203952b624l,
        0xbcf70dee4e7dc2fbl } },
    /* 43 << 28 */
    { { 0x372b016e6628e8c3l,0x07a0d667b60a7522l,0xcf05751b0a344ee2l,
        0x0ec09a48118bdeecl },
      { 0x6e4b3d4ed83dce46l,0x43a6316d99d2fc6el,0xa99d898956cf044cl,
        0x7c7f4454ae3e5fb7l } },
    /* 44 << 28 */
    { { 0xb2e6b121fbabbe92l,0x281850fbe1330076l,0x093581ec97890015l,
        0x69b1dded75ff77f5l },
      { 0x7cf0b18fab105105l,0x953ced31a89ccfefl,0x3151f85feb914009l,
        0x3c9f1b8788ed48adl } },
    /* 45 << 28 */
    { { 0xc9aba1a14a7eadcbl,0x928e7501522e71cfl,0xeaede7273a2e4f83l,
        0x467e10d11ce3bbd3l },
      { 0xf3442ac3b955dcf0l,0xba96307dd3d5e527l,0xf763a10efd77f474l,
        0x5d744bd06a6e1ff0l } },
    /* 46 << 28 */
    { { 0xd287282aa777899el,0xe20eda8fd03f3cdel,0x6a7e75bb50b07d31l,
        0x0b7e2a946f379de4l },
      { 0x31cb64ad19f593cfl,0x7b1a9e4f1e76ef1dl,0xe18c9c9db62d609cl,
        0x439bad6de779a650l } },
    /* 47 << 28 */
    { { 0x219d9066e032f144l,0x1db632b8e8b2ec6al,0xff0d0fd4fda12f78l,
        0x56fb4c2d2a25d265l },
      { 0x5f4e2ee1255a03f1l,0x61cd6af2e96af176l,0xe0317ba8d068bc97l,
        0x927d6bab264b988el } },
    /* 48 << 28 */
    { { 0xa18f07e0e90fb21el,0x00fd2b80bba7fca1l,0x20387f2795cd67b5l,
        0x5b89a4e7d39707f7l },
      { 0x8f83ad3f894407cel,0xa0025b946c226132l,0xc79563c7f906c13bl,
        0x5f548f314e7bb025l } },
    /* 49 << 28 */
    { { 0x2b4c6b8feac6d113l,0xa67e3f9c0e813c76l,0x3982717c3fe1f4b9l,
        0x5886581926d8050el },
      { 0x99f3640cf7f06f20l,0xdc6102162a66ebc2l,0x52f2c175767a1e08l,
        0x05660e1a5999871bl } },
    /* 50 << 28 */
    { { 0x6b0f17626d3c4693l,0xf0e7d62737ed7beal,0xc51758c7b75b226dl,
        0x40a886281f91613bl },
      { 0x889dbaa7bbb38ce0l,0xe0404b65bddcad81l,0xfebccd3a8bc9671fl,
        0xfbf9a357ee1f5375l } },
    /* 51 << 28 */
    { { 0x5dc169b028f33398l,0xb07ec11d72e90f65l,0xae7f3b4afaab1eb1l,
        0xd970195e5f17538al },
      { 0x52b05cbe0181e640l,0xf5debd622643313dl,0x761481545df31f82l,
        0x23e03b333a9e13c5l } },
    /* 52 << 28 */
    { { 0xff7589494fde0c1fl,0xbf8a1abee5b6ec20l,0x702278fb87e1db6cl,
        0xc447ad7a35ed658fl },
      { 0x48d4aa3803d0ccf2l,0x80acb338819a7c03l,0x9bc7c89e6e17ceccl,
        0x46736b8b03be1d82l } },
    /* 53 << 28 */
    { { 0xd65d7b60c0432f96l,0xddebe7a3deb5442fl,0x79a253077dff69a2l,
        0x37a56d9402cf3122l },
      { 0x8bab8aedf2350d0al,0x13c3f276037b0d9al,0xc664957c44c65cael,
        0x88b44089c2e71a88l } },
    /* 54 << 28 */
    { { 0xdb88e5a35cb02664l,0x5d4c0bf18686c72el,0xea3d9b62a682d53el,
        0x9b605ef40b2ad431l },
      { 0x71bac202c69645d0l,0xa115f03a6a1b66e7l,0xfe2c563a158f4dc4l,
        0xf715b3a04d12a78cl } },
    /* 55 << 28 */
    { { 0x8f7f0a48d413213al,0x2035806dc04becdbl,0xecd34a995d8587f5l,
        0x4d8c30799f6d3a71l },
      { 0x1b2a2a678d95a8f6l,0xc58c9d7df2110d0dl,0xdeee81d5cf8fba3fl,
        0xa42be3c00c7cdf68l } },
    /* 56 << 28 */
    { { 0x2126f742d43b5eaal,0x054a0766dfa59b85l,0x9d0d5e36126bfd45l,
        0xa1f8fbd7384f8a8fl },
      { 0x317680f5d563fcccl,0x48ca5055f280a928l,0xe00b81b227b578cfl,
        0x10aad9182994a514l } },
    /* 57 << 28 */
    { { 0xd9e07b62b7bdc953l,0x9f0f6ff25bc086ddl,0x09d1ccff655eee77l,
        0x45475f795bef7df1l },
      { 0x3faa28fa86f702ccl,0x92e609050f021f07l,0xe9e629687f8fa8c6l,
        0xbd71419af036ea2cl } },
    /* 58 << 28 */
    { { 0x171ee1cc6028da9al,0x5352fe1ac251f573l,0xf8ff236e3fa997f4l,
        0xd831b6c9a5749d5fl },
      { 0x7c872e1de350e2c2l,0xc56240d91e0ce403l,0xf9deb0776974f5cbl,
        0x7d50ba87961c3728l } },
    /* 59 << 28 */
    { { 0xd6f894265a3a2518l,0xcf817799c6303d43l,0x510a0471619e5696l,
        0xab049ff63a5e307bl },
      { 0xe4cdf9b0feb13ec7l,0xd5e971179d8ff90cl,0xf6f64d069afa96afl,
        0x00d0bf5e9d2012a2l } },
    /* 60 << 28 */
    { { 0xe63f301f358bcdc0l,0x07689e990a9d47f8l,0x1f689e2f4f43d43al,
        0x4d542a1690920904l },
      { 0xaea293d59ca0a707l,0xd061fe458ac68065l,0x1033bf1b0090008cl,
        0x29749558c08a6db6l } },
    /* 61 << 28 */
    { { 0x74b5fc59c1d5d034l,0xf712e9f667e215e0l,0xfd520cbd860200e6l,
        0x0229acb43ea22588l },
      { 0x9cd1e14cfff0c82el,0x87684b6259c69e73l,0xda85e61c96ccb989l,
        0x2d5dbb02a3d06493l } },
    /* 62 << 28 */
    { { 0xf22ad33ae86b173cl,0xe8e41ea5a79ff0e3l,0x01d2d725dd0d0c10l,
        0x31f39088032d28f9l },
      { 0x7b3f71e17829839el,0x0cf691b44502ae58l,0xef658dbdbefc6115l,
        0xa5cd6ee5b3ab5314l } },
    /* 63 << 28 */
    { { 0x206c8d7b5f1d2347l,0x794645ba4cc2253al,0xd517d8ff58389e08l,
        0x4fa20dee9f847288l },
      { 0xeba072d8d797770al,0x7360c91dbf429e26l,0x7200a3b380af8279l,
        0x6a1c915082dadce3l } },
    /* 64 << 28 */
    { { 0x0ee6d3a7c35d8794l,0x042e65580356bae5l,0x9f59698d643322fdl,
        0x9379ae1550a61967l },
      { 0x64b9ae62fcc9981el,0xaed3d6316d2934c6l,0x2454b3025e4e65ebl,
        0xab09f647f9950428l } },
    /* 0 << 35 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 35 */
    { { 0xb2083a1222248accl,0x1f6ec0ef3264e366l,0x5659b7045afdee28l,
        0x7a823a40e6430bb5l },
      { 0x24592a04e1900a79l,0xcde09d4ac9ee6576l,0x52b6463f4b5ea54al,
        0x1efe9ed3d3ca65a7l } },
    /* 2 << 35 */
    { { 0xe27a6dbe305406ddl,0x8eb7dc7fdd5d1957l,0xf54a6876387d4d8fl,
        0x9c479409c7762de4l },
      { 0xbe4d5b5d99b30778l,0x25380c566e793682l,0x602d37f3dac740e3l,
        0x140deabe1566e4ael } },
    /* 3 << 35 */
    { { 0x4481d067afd32acfl,0xd8f0fccae1f71ccfl,0xd208dd0cb596f2dal,
        0xd049d7309aad93f9l },
      { 0xc79f263d42ab580el,0x09411bb123f707b4l,0x8cfde1ff835e0edal,
        0x7270749090f03402l } },
    /* 4 << 35 */
    { { 0xeaee6126c49a861el,0x024f3b65e14f0d06l,0x51a3f1e8c69bfc17l,
        0xc3c3a8e9a7686381l },
      { 0x3400752cb103d4c8l,0x02bc46139218b36bl,0xc67f75eb7651504al,
        0xd6848b56d02aebfal } },
    /* 5 << 35 */
    { { 0xbd9802e6c30fa92bl,0x5a70d96d9a552784l,0x9085c4ea3f83169bl,
        0xfa9423bb06908228l },
      { 0x2ffebe12fe97a5b9l,0x85da604971b99118l,0x9cbc2f7f63178846l,
        0xfd96bc709153218el } },
    /* 6 << 35 */
    { { 0x958381db1782269bl,0xae34bf792597e550l,0xbb5c60645f385153l,
        0x6f0e96afe3088048l },
      { 0xbf6a021577884456l,0xb3b5688c69310ea7l,0x17c9429504fad2del,
        0xe020f0e517896d4dl } },
    /* 7 << 35 */
    { { 0x730ba0ab0976505fl,0x567f6813095e2ec5l,0x470620106331ab71l,
        0x72cfa97741d22b9fl },
      { 0x33e55ead8a2373dal,0xa8d0d5f47ba45a68l,0xba1d8f9c03029d15l,
        0x8f34f1ccfc55b9f3l } },
    /* 8 << 35 */
    { { 0xcca4428dbbe5a1a9l,0x8187fd5f3126bd67l,0x0036973a48105826l,
        0xa39b6663b8bd61a0l },
      { 0x6d42deef2d65a808l,0x4969044f94636b19l,0xf611ee47dd5d564cl,
        0x7b2f3a49d2873077l } },
    /* 9 << 35 */
    { { 0x94157d45300eb294l,0x2b2a656e169c1494l,0xc000dd76d3a47aa9l,
        0xa2864e4fa6243ea4l },
      { 0x82716c47db89842el,0x12dfd7d761479fb7l,0x3b9a2c56e0b2f6dcl,
        0x46be862ad7f85d67l } },
    /* 10 << 35 */
    { { 0x03b0d8dd0f82b214l,0x460c34f9f103cbc6l,0xf32e5c0318d79e19l,
        0x8b8888baa84117f8l },
      { 0x8f3c37dcc0722677l,0x10d21be91c1c0f27l,0xd47c8468e0f7a0c6l,
        0x9bf02213adecc0e0l } },
    /* 11 << 35 */
    { { 0x0baa7d1242b48b99l,0x1bcb665d48424096l,0x8b847cd6ebfb5cfbl,
        0x87c2ae569ad4d10dl },
      { 0xf1cbb1220de36726l,0xe7043c683fdfbd21l,0x4bd0826a4e79d460l,
        0x11f5e5984bd1a2cbl } },
    /* 12 << 35 */
    { { 0x97554160b7fe7b6el,0x7d16189a400a3fb2l,0xd73e9beae328ca1el,
        0x0dd04b97e793d8ccl },
      { 0xa9c83c9b506db8ccl,0x5cd47aaecf38814cl,0x26fc430db64b45e6l,
        0x079b5499d818ea84l } },
    /* 13 << 35 */
    { { 0xebb01102c1c24a3bl,0xca24e5681c161c1al,0x103eea6936f00a4al,
        0x9ad76ee876176c7bl },
      { 0x97451fc2538e0ff7l,0x94f898096604b3b0l,0x6311436e3249cfd7l,
        0x27b4a7bd41224f69l } },
    /* 14 << 35 */
    { { 0x03b5d21ae0ac2941l,0x279b0254c2d31937l,0x3307c052cac992d0l,
        0x6aa7cb92efa8b1f3l },
      { 0x5a1825800d37c7a5l,0x13380c37342d5422l,0x92ac2d66d5d2ef92l,
        0x035a70c9030c63c6l } },
    /* 15 << 35 */
    { { 0xc16025dd4ce4f152l,0x1f419a71f9df7c06l,0x6d5b221491e4bb14l,
        0xfc43c6cc839fb4cel },
      { 0x49f06591925d6b2dl,0x4b37d9d362186598l,0x8c54a971d01b1629l,
        0xe1a9c29f51d50e05l } },
    /* 16 << 35 */
    { { 0x5109b78571ba1861l,0x48b22d5cd0c8f93dl,0xe8fa84a78633bb93l,
        0x53fba6ba5aebbd08l },
      { 0x7ff27df3e5eea7d8l,0x521c879668ca7158l,0xb9d5133bce6f1a05l,
        0x2d50cd53fd0ebee4l } },
    /* 17 << 35 */
    { { 0xc82115d6c5a3ef16l,0x993eff9dba079221l,0xe4da2c5e4b5da81cl,
        0x9a89dbdb8033fd85l },
      { 0x60819ebf2b892891l,0x53902b215d14a4d5l,0x6ac35051d7fda421l,
        0xcc6ab88561c83284l } },
    /* 18 << 35 */
    { { 0x14eba133f74cff17l,0x240aaa03ecb813f2l,0xcfbb65406f665beel,
        0x084b1fe4a425ad73l },
      { 0x009d5d16d081f6a6l,0x35304fe8eef82c90l,0xf20346d5aa9eaa22l,
        0x0ada9f07ac1c91e3l } },
    /* 19 << 35 */
    { { 0xa6e21678968a6144l,0x54c1f77c07b31a1el,0xd6bb787e5781fbe1l,
        0x61bd2ee0e31f1c4al },
      { 0xf25aa1e9781105fcl,0x9cf2971f7b2f8e80l,0x26d15412cdff919bl,
        0x01db4ebe34bc896el } },
    /* 20 << 35 */
    { { 0x7d9b3e23b40df1cfl,0x5933737394e971b4l,0xbf57bd14669cf921l,
        0x865daedf0c1a1064l },
      { 0x3eb70bd383279125l,0xbc3d5b9f34ecdaabl,0x91e3ed7e5f755cafl,
        0x49699f54d41e6f02l } },
    /* 21 << 35 */
    { { 0x185770e1d4a7a15bl,0x08f3587aeaac87e7l,0x352018db473133eal,
        0x674ce71904fd30fcl },
      { 0x7b8d9835088b3e0el,0x7a0356a95d0d47a1l,0x9d9e76596474a3c4l,
        0x61ea48a7ff66966cl } },
    /* 22 << 35 */
    { { 0x304177580f3e4834l,0xfdbb21c217a9afcbl,0x756fa17f2f9a67b3l,
        0x2a6b2421a245c1a8l },
      { 0x64be27944af02291l,0xade465c62a5804fel,0x8dffbd39a6f08fd7l,
        0xc4efa84caa14403bl } },
    /* 23 << 35 */
    { { 0xa1b91b2a442b0f5cl,0xb748e317cf997736l,0x8d1b62bfcee90e16l,
        0x907ae2710b2078c0l },
      { 0xdf31534b0c9bcdddl,0x043fb05439adce83l,0x99031043d826846al,
        0x61a9c0d6b144f393l } },
    /* 24 << 35 */
    { { 0xdab4804647718427l,0xdf17ff9b6e830f8bl,0x408d7ee8e49a1347l,
        0x6ac71e2391c1d4ael },
      { 0xc8cbb9fd1defd73cl,0x19840657bbbbfec5l,0x39db1cb59e7ef8eal,
        0x78aa829664105f30l } },
    /* 25 << 35 */
    { { 0xa3d9b7f0a3738c29l,0x0a2f235abc3250a3l,0x55e506f6445e4cafl,
        0x0974f73d33475f7al },
      { 0xd37dbba35ba2f5a8l,0x542c6e636af40066l,0x26d99b53c5d73e2cl,
        0x06060d7d6c3ca33el } },
    /* 26 << 35 */
    { { 0xcdbef1c2065fef4al,0x77e60f7dfd5b92e3l,0xd7c549f026708350l,
        0x201b3ad034f121bfl },
      { 0x5fcac2a10334fc14l,0x8a9a9e09344552f6l,0x7dd8a1d397653082l,
        0x5fc0738f79d4f289l } },
    /* 27 << 35 */
    { { 0x787d244d17d2d8c3l,0xeffc634570830684l,0x5ddb96dde4f73ae5l,
        0x8efb14b1172549a5l },
      { 0x6eb73eee2245ae7al,0xbca4061eea11f13el,0xb577421d30b01f5dl,
        0xaa688b24782e152cl } },
    /* 28 << 35 */
    { { 0x67608e71bd3502bal,0x4ef41f24b4de75a0l,0xb08dde5efd6125e5l,
        0xde484825a409543fl },
      { 0x1f198d9865cc2295l,0x428a37716e0edfa2l,0x4f9697a2adf35fc7l,
        0x01a43c79f7cac3c7l } },
    /* 29 << 35 */
    { { 0xb05d70590fd3659al,0x8927f30cbb7f2d9al,0x4023d1ac8cf984d3l,
        0x32125ed302897a45l },
      { 0xfb572dad3d414205l,0x73000ef2e3fa82a9l,0x4c0868e9f10a5581l,
        0x5b61fc676b0b3ca5l } },
    /* 30 << 35 */
    { { 0xc1258d5b7cae440cl,0x21c08b41402b7531l,0xf61a8955de932321l,
        0x3568faf82d1408afl },
      { 0x71b15e999ecf965bl,0xf14ed248e917276fl,0xc6f4caa1820cf9e2l,
        0x681b20b218d83c7el } },
    /* 31 << 35 */
    { { 0x6cde738dc6c01120l,0x71db0813ae70e0dbl,0x95fc064474afe18cl,
        0x34619053129e2be7l },
      { 0x80615ceadb2a3b15l,0x0a49a19edb4c7073l,0x0e1b84c88fd2d367l,
        0xd74bf462033fb8aal } },
    /* 32 << 35 */
    { { 0x889f6d65533ef217l,0x7158c7e4c3ca2e87l,0xfb670dfbdc2b4167l,
        0x75910a01844c257fl },
      { 0xf336bf07cf88577dl,0x22245250e45e2acel,0x2ed92e8d7ca23d85l,
        0x29f8be4c2b812f58l } },
    /* 33 << 35 */
    { { 0xdd9ebaa7076fe12bl,0x3f2400cbae1537f9l,0x1aa9352817bdfb46l,
        0xc0f9843067883b41l },
      { 0x5590ede10170911dl,0x7562f5bb34d4b17fl,0xe1fa1df21826b8d2l,
        0xb40b796a6bd80d59l } },
    /* 34 << 35 */
    { { 0xd65bf1973467ba92l,0x8c9b46dbf70954b0l,0x97c8a0f30e78f15dl,
        0xa8f3a69a85a4c961l },
      { 0x4242660f61e4ce9bl,0xbf06aab36ea6790cl,0xc6706f8eec986416l,
        0x9e56dec19a9fc225l } },
    /* 35 << 35 */
    { { 0x527c46f49a9898d9l,0xd799e77b5633cdefl,0x24eacc167d9e4297l,
        0xabb61cea6b1cb734l },
      { 0xbee2e8a7f778443cl,0x3bb42bf129de2fe6l,0xcbed86a13003bb6fl,
        0xd3918e6cd781cdf6l } },
    /* 36 << 35 */
    { { 0x4bee32719a5103f1l,0x5243efc6f50eac06l,0xb8e122cb6adcc119l,
        0x1b7faa84c0b80a08l },
      { 0x32c3d1bd6dfcd08cl,0x129dec4e0be427del,0x98ab679c1d263c83l,
        0xafc83cb7cef64effl } },
    /* 37 << 35 */
    { { 0x85eb60882fa6be76l,0x892585fb1328cbfel,0xc154d3edcf618ddal,
        0xc44f601b3abaf26el },
      { 0x7bf57d0b2be1fdfdl,0xa833bd2d21137feel,0x9353af362db591a8l,
        0xc76f26dc5562a056l } },
    /* 38 << 35 */
    { { 0x1d87e47d3fdf5a51l,0x7afb5f9355c9cab0l,0x91bbf58f89e0586el,
        0x7c72c0180d843709l },
      { 0xa9a5aafb99b5c3dcl,0xa48a0f1d3844aeb0l,0x7178b7ddb667e482l,
        0x453985e96e23a59al } },
    /* 39 << 35 */
    { { 0x4a54c86001b25dd8l,0x0dd37f48fb897c8al,0x5f8aa6100ea90cd9l,
        0xc8892c6816d5830dl },
      { 0xeb4befc0ef514ca5l,0x478eb679e72c9ee6l,0x9bca20dadbc40d5fl,
        0xf015de21dde4f64al } },
    /* 40 << 35 */
    { { 0xaa6a4de0eaf4b8a5l,0x68cfd9ca4bc60e32l,0x668a4b017fd15e70l,
        0xd9f0694af27dc09dl },
      { 0xf6c3cad5ba708bcdl,0x5cd2ba695bb95c2al,0xaa28c1d333c0a58fl,
        0x23e274e3abc77870l } },
    /* 41 << 35 */
    { { 0x44c3692ddfd20a4al,0x091c5fd381a66653l,0x6c0bb69109a0757dl,
        0x9072e8b9667343eal },
      { 0x31d40eb080848becl,0x95bd480a79fd36ccl,0x01a77c6165ed43f5l,
        0xafccd1272e0d40bfl } },
    /* 42 << 35 */
    { { 0xeccfc82d1cc1884bl,0xc85ac2015d4753b4l,0xc7a6caac658e099fl,
        0xcf46369e04b27390l },
      { 0xe2e7d049506467eal,0x481b63a237cdecccl,0x4029abd8ed80143al,
        0x28bfe3c7bcb00b88l } },
    /* 43 << 35 */
    { { 0x3bec10090643d84al,0x885f3668abd11041l,0xdb02432cf83a34d6l,
        0x32f7b360719ceebel },
      { 0xf06c7837dad1fe7al,0x60a157a95441a0b0l,0x704970e9e2d47550l,
        0xcd2bd553271b9020l } },
    /* 44 << 35 */
    { { 0xff57f82f33e24a0bl,0x9cbee23ff2565079l,0x16353427eb5f5825l,
        0x276feec4e948d662l },
      { 0xd1b62bc6da10032bl,0x718351ddf0e72a53l,0x934520762420e7bal,
        0x96368fff3a00118dl } },
    /* 45 << 35 */
    { { 0x00ce2d26150a49e4l,0x0c28b6363f04706bl,0xbad65a4658b196d0l,
        0x6c8455fcec9f8b7cl },
      { 0xe90c895f2d71867el,0x5c0be31bedf9f38cl,0x2a37a15ed8f6ec04l,
        0x239639e78cd85251l } },
    /* 46 << 35 */
    { { 0xd89753159c7c4c6bl,0x603aa3c0d7409af7l,0xb8d53d0c007132fbl,
        0x68d12af7a6849238l },
      { 0xbe0607e7bf5d9279l,0x9aa50055aada74cel,0xe81079cbba7e8ccbl,
        0x610c71d1a5f4ff5el } },
    /* 47 << 35 */
    { { 0x9e2ee1a75aa07093l,0xca84004ba75da47cl,0x074d39513de75401l,
        0xf938f756bb311592l },
      { 0x9619761800a43421l,0x39a2536207bc78c8l,0x278f710a0a171276l,
        0xb28446ea8d1a8f08l } },
    /* 48 << 35 */
    { { 0x184781bfe3b6a661l,0x7751cb1de6d279f7l,0xf8ff95d6c59eb662l,
        0x186d90b758d3dea7l },
      { 0x0e4bb6c1dfb4f754l,0x5c5cf56b2b2801dcl,0xc561e4521f54564dl,
        0xb4fb8c60f0dd7f13l } },
    /* 49 << 35 */
    { { 0xf884963033ff98c7l,0x9619fffacf17769cl,0xf8090bf61bfdd80al,
        0x14d9a149422cfe63l },
      { 0xb354c3606f6df9eal,0xdbcf770d218f17eal,0x207db7c879eb3480l,
        0x213dbda8559b6a26l } },
    /* 50 << 35 */
    { { 0xac4c200b29fc81b3l,0xebc3e09f171d87c1l,0x917995301481aa9el,
        0x051b92e192e114fal },
      { 0xdf8f92e9ecb5537fl,0x44b1b2cc290c7483l,0xa711455a2adeb016l,
        0x964b685681a10c2cl } },
    /* 51 << 35 */
    { { 0x4f159d99cec03623l,0x05532225ef3271eal,0xb231bea3c5ee4849l,
        0x57a54f507094f103l },
      { 0x3e2d421d9598b352l,0xe865a49c67412ab4l,0xd2998a251cc3a912l,
        0x5d0928080c74d65dl } },
    /* 52 << 35 */
    { { 0x73f459084088567al,0xeb6b280e1f214a61l,0x8c9adc34caf0c13dl,
        0x39d12938f561fb80l },
      { 0xb2dc3a5ebc6edfb4l,0x7485b1b1fe4d210el,0x062e0400e186ae72l,
        0x91e32d5c6eeb3b88l } },
    /* 53 << 35 */
    { { 0x6df574d74be59224l,0xebc88ccc716d55f3l,0x26c2e6d0cad6ed33l,
        0xc6e21e7d0d3e8b10l },
      { 0x2cc5840e5bcc36bbl,0x9292445e7da74f69l,0x8be8d3214e5193a8l,
        0x3ec236298df06413l } },
    /* 54 << 35 */
    { { 0xc7e9ae85b134defal,0x6073b1d01bb2d475l,0xb9ad615e2863c00dl,
        0x9e29493d525f4ac4l },
      { 0xc32b1dea4e9acf4fl,0x3e1f01c8a50db88dl,0xb05d70ea04da916cl,
        0x714b0d0ad865803el } },
    /* 55 << 35 */
    { { 0x4bd493fc9920cb5el,0x5b44b1f792c7a3acl,0xa2a77293bcec9235l,
        0x5ee06e87cd378553l },
      { 0xceff8173da621607l,0x2bb03e4c99f5d290l,0x2945106aa6f734acl,
        0xb5056604d25c4732l } },
    /* 56 << 35 */
    { { 0x5945920ce079afeel,0x686e17a06789831fl,0x5966bee8b74a5ae5l,
        0x38a673a21e258d46l },
      { 0xbd1cc1f283141c95l,0x3b2ecf4f0e96e486l,0xcd3aa89674e5fc78l,
        0x415ec10c2482fa7al } },
    /* 57 << 35 */
    { { 0x1523441980503380l,0x513d917ad314b392l,0xb0b52f4e63caecael,
        0x07bf22ad2dc7780bl },
      { 0xe761e8a1e4306839l,0x1b3be9625dd7feaal,0x4fe728de74c778f1l,
        0xf1fa0bda5e0070f6l } },
    /* 58 << 35 */
    { { 0x85205a316ec3f510l,0x2c7e4a14d2980475l,0xde3c19c06f30ebfdl,
        0xdb1c1f38d4b7e644l },
      { 0xfe291a755dce364al,0xb7b22a3c058f5be3l,0x2cd2c30237fea38cl,
        0x2930967a2e17be17l } },
    /* 59 << 35 */
    { { 0x87f009de0c061c65l,0xcb014aacedc6ed44l,0x49bd1cb43bafb1ebl,
        0x81bd8b5c282d3688l },
      { 0x1cdab87ef01a17afl,0x21f37ac4e710063bl,0x5a6c567642fc8193l,
        0xf4753e7056a6015cl } },
    /* 60 << 35 */
    { { 0x020f795ea15b0a44l,0x8f37c8d78958a958l,0x63b7e89ba4b675b5l,
        0xb4fb0c0c0fc31aeal },
      { 0xed95e639a7ff1f2el,0x9880f5a3619614fbl,0xdeb6ff02947151abl,
        0x5bc5118ca868dcdbl } },
    /* 61 << 35 */
    { { 0xd8da20554c20cea5l,0xcac2776e14c4d69al,0xcccb22c1622d599bl,
        0xa4ddb65368a9bb50l },
      { 0x2c4ff1511b4941b4l,0xe1ff19b46efba588l,0x35034363c48345e0l,
        0x45542e3d1e29dfc4l } },
    /* 62 << 35 */
    { { 0xf197cb91349f7aedl,0x3b2b5a008fca8420l,0x7c175ee823aaf6d8l,
        0x54dcf42135af32b6l },
      { 0x0ba1430727d6561el,0x879d5ee4d175b1e2l,0xc7c4367399807db5l,
        0x77a544559cd55bcdl } },
    /* 63 << 35 */
    { { 0xe6c2ff130105c072l,0x18f7a99f8dda7da4l,0x4c3018200e2d35c1l,
        0x06a53ca0d9cc6c82l },
      { 0xaa21cc1ef1aa1d9el,0x324143344a75b1e8l,0x2a6d13280ebe9fdcl,
        0x16bd173f98a4755al } },
    /* 64 << 35 */
    { { 0xfbb9b2452133ffd9l,0x39a8b2f1830f1a20l,0x484bc97dd5a1f52al,
        0xd6aebf56a40eddf8l },
      { 0x32257acb76ccdac6l,0xaf4d36ec1586ff27l,0x8eaa8863f8de7dd1l,
        0x0045d5cf88647c16l } },
    /* 0 << 42 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 42 */
    { { 0xa6f3d574c005979dl,0xc2072b426a40e350l,0xfca5c1568de2ecf9l,
        0xa8c8bf5ba515344el },
      { 0x97aee555114df14al,0xd4374a4dfdc5ec6bl,0x754cc28f2ca85418l,
        0x71cb9e27d3c41f78l } },
    /* 2 << 42 */
    { { 0x8910507903605c39l,0xf0843d9ea142c96cl,0xf374493416923684l,
        0x732caa2ffa0a2893l },
      { 0xb2e8c27061160170l,0xc32788cc437fbaa3l,0x39cd818ea6eda3acl,
        0xe2e942399e2b2e07l } },
    /* 3 << 42 */
    { { 0x6967d39b0260e52al,0xd42585cc90653325l,0x0d9bd60521ca7954l,
        0x4fa2087781ed57b3l },
      { 0x60c1eff8e34a0bbel,0x56b0040c84f6ef64l,0x28be2b24b1af8483l,
        0xb2278163f5531614l } },
    /* 4 << 42 */
    { { 0x8df275455922ac1cl,0xa7b3ef5ca52b3f63l,0x8e77b21471de57c4l,
        0x31682c10834c008bl },
      { 0xc76824f04bd55d31l,0xb6d1c08617b61c71l,0x31db0903c2a5089dl,
        0x9c092172184e5d3fl } },
    /* 5 << 42 */
    { { 0xdd7ced5bc00cc638l,0x1a2015eb61278fc2l,0x2e8e52886a37f8d6l,
        0xc457786fe79933adl },
      { 0xb3fe4cce2c51211al,0xad9b10b224c20498l,0x90d87a4fd28db5e5l,
        0x698cd1053aca2fc3l } },
    /* 6 << 42 */
    { { 0x4f112d07e91b536dl,0xceb982f29eba09d6l,0x3c157b2c197c396fl,
        0xe23c2d417b66eb24l },
      { 0x480c57d93f330d37l,0xb3a4c8a179108debl,0x702388decb199ce5l,
        0x0b019211b944a8d4l } },
    /* 7 << 42 */
    { { 0x24f2a692840bb336l,0x7c353bdca669fa7bl,0xda20d6fcdec9c300l,
        0x625fbe2fa13a4f17l },
      { 0xa2b1b61adbc17328l,0x008965bfa9515621l,0x49690939c620ff46l,
        0x182dd27d8717e91cl } },
    /* 8 << 42 */
    { { 0x5ace5035ea6c3997l,0x54259aaac2610befl,0xef18bb3f3c80dd39l,
        0x6910b95b5fc3fa39l },
      { 0xfce2f51043e09aeel,0xced56c9fa7675665l,0x10e265acd872db61l,
        0x6982812eae9fce69l } },
    /* 9 << 42 */
    { { 0x29be11c6ce800998l,0x72bb1752b90360d9l,0x2c1931975a4ad590l,
        0x2ba2f5489fc1dbc0l },
      { 0x7fe4eebbe490ebe0l,0x12a0a4cd7fae11c0l,0x7197cf81e903ba37l,
        0xcf7d4aa8de1c6dd8l } },
    /* 10 << 42 */
    { { 0x92af6bf43fd5684cl,0x2b26eecf80360aa1l,0xbd960f3000546a82l,
        0x407b3c43f59ad8fel },
      { 0x86cae5fe249c82bal,0x9e0faec72463744cl,0x87f551e894916272l,
        0x033f93446ceb0615l } },
    /* 11 << 42 */
    { { 0x1e5eb0d18be82e84l,0x89967f0e7a582fefl,0xbcf687d5a6e921fal,
        0xdfee4cf3d37a09bal },
      { 0x94f06965b493c465l,0x638b9a1c7635c030l,0x7666786466f05e9fl,
        0xccaf6808c04da725l } },
    /* 12 << 42 */
    { { 0xca2eb690768fccfcl,0xf402d37db835b362l,0x0efac0d0e2fdfccel,
        0xefc9cdefb638d990l },
      { 0x2af12b72d1669a8bl,0x33c536bc5774ccbdl,0x30b21909fb34870el,
        0xc38fa2f77df25acal } },
    /* 13 << 42 */
    { { 0x74c5f02bbf81f3f5l,0x0525a5aeaf7e4581l,0x88d2aaba433c54ael,
        0xed9775db806a56c5l },
      { 0xd320738ac0edb37dl,0x25fdb6ee66cc1f51l,0xac661d1710600d76l,
        0x931ec1f3bdd1ed76l } },
    /* 14 << 42 */
    { { 0x65c11d6219ee43f1l,0x5cd57c3e60829d97l,0xd26c91a3984be6e8l,
        0xf08d93098b0c53bdl },
      { 0x94bc9e5bc016e4eal,0xd391683911d43d2bl,0x886c5ad773701155l,
        0xe037762620b00715l } },
    /* 15 << 42 */
    { { 0x7f01c9ecaa80ba59l,0x3083411a68538e51l,0x970370f1e88128afl,
        0x625cc3db91dec14bl },
      { 0xfef9666c01ac3107l,0xb2a8d577d5057ac3l,0xb0f2629992be5df7l,
        0xf579c8e500353924l } },
    /* 16 << 42 */
    { { 0xb8fa3d931341ed7al,0x4223272ca7b59d49l,0x3dcb194783b8c4a4l,
        0x4e413c01ed1302e4l },
      { 0x6d999127e17e44cel,0xee86bf7533b3adfbl,0xf6902fe625aa96cal,
        0xb73540e4e5aae47dl } },
    /* 17 << 42 */
    { { 0x32801d7b1b4a158cl,0xe571c99e27e2a369l,0x40cb76c010d9f197l,
        0xc308c2893167c0ael },
      { 0xa6ef9dd3eb7958f2l,0xa7226dfc300879b1l,0x6cd0b3627edf0636l,
        0x4efbce6c7bc37eedl } },
    /* 18 << 42 */
    { { 0x75f92a058d699021l,0x586d4c79772566e3l,0x378ca5f1761ad23al,
        0x650d86fc1465a8acl },
      { 0x7a4ed457842ba251l,0x6b65e3e642234933l,0xaf1543b731aad657l,
        0xa4cefe98cbfec369l } },
    /* 19 << 42 */
    { { 0xb587da909f47befbl,0x6562e9fb41312d13l,0xa691ea59eff1cefel,
        0xcc30477a05fc4cf6l },
      { 0xa16324610b0ffd3dl,0xa1f16f3b5b355956l,0x5b148d534224ec24l,
        0xdc834e7bf977012al } },
    /* 20 << 42 */
    { { 0x7bfc5e75b2c69dbcl,0x3aa77a2903c3da6cl,0xde0df03cca910271l,
        0xcbd5ca4a7806dc55l },
      { 0xe1ca58076db476cbl,0xfde15d625f37a31el,0xf49af520f41af416l,
        0x96c5c5b17d342db5l } },
    /* 21 << 42 */
    { { 0x155c43b7eb4ceb9bl,0x2e9930104e77371al,0x1d2987da675d43afl,
        0xef2bc1c08599fd72l },
      { 0x96894b7b9342f6b2l,0x201eadf27c8e71f0l,0xf3479d9f4a1f3efcl,
        0xe0f8a742702a9704l } },
    /* 22 << 42 */
    { { 0xeafd44b6b3eba40cl,0xf9739f29c1c1e0d0l,0x0091471a619d505el,
        0xc15f9c969d7c263el },
      { 0x5be4728583afbe33l,0xa3b6d6af04f1e092l,0xe76526b9751a9d11l,
        0x2ec5b26d9a4ae4d2l } },
    /* 23 << 42 */
    { { 0xeb66f4d902f6fb8dl,0x4063c56196912164l,0xeb7050c180ef3000l,
        0x288d1c33eaa5b3f0l },
      { 0xe87c68d607806fd8l,0xb2f7f9d54bbbf50fl,0x25972f3aac8d6627l,
        0xf854777410e8c13bl } },
    /* 24 << 42 */
    { { 0xcc50ef6c872b4a60l,0xab2a34a44613521bl,0x39c5c190983e15d1l,
        0x61dde5df59905512l },
      { 0xe417f6219f2275f3l,0x0750c8b6451d894bl,0x75b04ab978b0bdaal,
        0x3bfd9fd4458589bdl } },
    /* 25 << 42 */
    { { 0xf1013e30ee9120b6l,0x2b51af9323a4743el,0xea96ffae48d14d9el,
        0x71dc0dbe698a1d32l },
      { 0x914962d20180cca4l,0x1ae60677c3568963l,0x8cf227b1437bc444l,
        0xc650c83bc9962c7al } },
    /* 26 << 42 */
    { { 0x23c2c7ddfe7ccfc4l,0xf925c89d1b929d48l,0x4460f74b06783c33l,
        0xac2c8d49a590475al },
      { 0xfb40b407b807bba0l,0x9d1e362d69ff8f3al,0xa33e9681cbef64a4l,
        0x67ece5fa332fb4b2l } },
    /* 27 << 42 */
    { { 0x6900a99b739f10e3l,0xc3341ca9ff525925l,0xee18a626a9e2d041l,
        0xa5a8368529580dddl },
      { 0xf3470c819d7de3cdl,0xedf025862062cf9cl,0xf43522fac010edb0l,
        0x3031413513a4b1ael } },
    /* 28 << 42 */
    { { 0xc792e02adb22b94bl,0x993d8ae9a1eaa45bl,0x8aad6cd3cd1e1c63l,
        0x89529ca7c5ce688al },
      { 0x2ccee3aae572a253l,0xe02b643802a21efbl,0xa7091b6ec9430358l,
        0x06d1b1fa9d7db504l } },
    /* 29 << 42 */
    { { 0x58846d32c4744733l,0x40517c71379f9e34l,0x2f65655f130ef6cal,
        0x526e4488f1f3503fl },
      { 0x8467bd177ee4a976l,0x1d9dc913921363d1l,0xd8d24c33b069e041l,
        0x5eb5da0a2cdf7f51l } },
    /* 30 << 42 */
    { { 0x1c0f3cb1197b994fl,0x3c95a6c52843eae9l,0x7766ffc9a6097ea5l,
        0x7bea4093d723b867l },
      { 0xb48e1f734db378f9l,0x70025b00e37b77acl,0x943dc8e7af24ad46l,
        0xb98a15ac16d00a85l } },
    /* 31 << 42 */
    { { 0x3adc38ba2743b004l,0xb1c7f4f7334415eel,0xea43df8f1e62d05al,
        0x326189059d76a3b6l },
      { 0x2fbd0bb5a23a0f46l,0x5bc971db6a01918cl,0x7801d94ab4743f94l,
        0xb94df65e676ae22bl } },
    /* 32 << 42 */
    { { 0xaafcbfabaf95894cl,0x7b9bdc07276b2241l,0xeaf983625bdda48bl,
        0x5977faf2a3fcb4dfl },
      { 0xbed042ef052c4b5bl,0x9fe87f71067591f0l,0xc89c73ca22f24ec7l,
        0x7d37fa9ee64a9f1bl } },
    /* 33 << 42 */
    { { 0x2710841a15562627l,0x2c01a613c243b034l,0x1d135c562bc68609l,
        0xc2ca17158b03f1f6l },
      { 0xc9966c2d3eb81d82l,0xc02abf4a8f6df13el,0x77b34bd78f72b43bl,
        0xaff6218f360c82b0l } },
    /* 34 << 42 */
    { { 0x0aa5726c8d55b9d2l,0xdc0adbe999e9bffbl,0x9097549cefb9e72al,
        0x167557129dfb3111l },
      { 0xdd8bf984f26847f9l,0xbcb8e387dfb30cb7l,0xc1fd32a75171ef9cl,
        0x977f3fc7389b363fl } },
    /* 35 << 42 */
    { { 0x116eaf2bf4babda0l,0xfeab68bdf7113c8el,0xd1e3f064b7def526l,
        0x1ac30885e0b3fa02l },
      { 0x1c5a6e7b40142d9dl,0x839b560330921c0bl,0x48f301fa36a116a3l,
        0x380e1107cfd9ee6dl } },
    /* 36 << 42 */
    { { 0x7945ead858854be1l,0x4111c12ecbd4d49dl,0xece3b1ec3a29c2efl,
        0x6356d4048d3616f5l },
      { 0x9f0d6a8f594d320el,0x0989316df651ccd2l,0x6c32117a0f8fdde4l,
        0x9abe5cc5a26a9bbcl } },
    /* 37 << 42 */
    { { 0xcff560fb9723f671l,0x21b2a12d7f3d593cl,0xe4cb18da24ba0696l,
        0x186e2220c3543384l },
      { 0x722f64e088312c29l,0x94282a9917dc7752l,0x62467bbf5a85ee89l,
        0xf435c650f10076a0l } },
    /* 38 << 42 */
    { { 0xc9ff153943b3a50bl,0x7132130c1a53efbcl,0x31bfe063f7b0c5b7l,
        0xb0179a7d4ea994ccl },
      { 0x12d064b3c85f455bl,0x472593288f6e0062l,0xf64e590bb875d6d9l,
        0x22dd6225ad92bcc7l } },
    /* 39 << 42 */
    { { 0xb658038eb9c3bd6dl,0x00cdb0d6fbba27c8l,0x0c6813371062c45dl,
        0xd8515b8c2d33407dl },
      { 0xcb8f699e8cbb5ecfl,0x8c4347f8c608d7d8l,0x2c11850abb3e00dbl,
        0x20a8dafdecb49d19l } },
    /* 40 << 42 */
    { { 0xbd78148045ee2f40l,0x75e354af416b60cfl,0xde0b58a18d49a8c4l,
        0xe40e94e2fa359536l },
      { 0xbd4fa59f62accd76l,0x05cf466a8c762837l,0xb5abda99448c277bl,
        0x5a9e01bf48b13740l } },
    /* 41 << 42 */
    { { 0x9d457798326aad8dl,0xbdef4954c396f7e7l,0x6fb274a2c253e292l,
        0x2800bf0a1cfe53e7l },
      { 0x22426d3144438fd4l,0xef2339235e259f9al,0x4188503c03f66264l,
        0x9e5e7f137f9fdfabl } },
    /* 42 << 42 */
    { { 0x565eb76c5fcc1abal,0xea63254859b5bff8l,0x5587c087aab6d3fal,
        0x92b639ea6ce39c1bl },
      { 0x0706e782953b135cl,0x7308912e425268efl,0x599e92c7090e7469l,
        0x83b90f529bc35e75l } },
    /* 43 << 42 */
    { { 0x4750b3d0244975b3l,0xf3a4435811965d72l,0x179c67749c8dc751l,
        0xff18cdfed23d9ff0l },
      { 0xc40138332028e247l,0x96e280e2f3bfbc79l,0xf60417bdd0880a84l,
        0x263c9f3d2a568151l } },
    /* 44 << 42 */
    { { 0x36be15b32d2ce811l,0x846dc0c2f8291d21l,0x5cfa0ecb789fcfdbl,
        0x45a0beedd7535b9al },
      { 0xec8e9f0796d69af1l,0x31a7c5b8599ab6dcl,0xd36d45eff9e2e09fl,
        0x3cf49ef1dcee954bl } },
    /* 45 << 42 */
    { { 0x6be34cf3086cff9bl,0x88dbd49139a3360fl,0x1e96b8cc0dbfbd1dl,
        0xc1e5f7bfcb7e2552l },
      { 0x0547b21428819d98l,0xc770dd9c7aea9dcbl,0xaef0d4c7041d68c8l,
        0xcc2b981813cb9ba8l } },
    /* 46 << 42 */
    { { 0x7fc7bc76fe86c607l,0x6b7b9337502a9a95l,0x1948dc27d14dab63l,
        0x249dd198dae047bel },
      { 0xe8356584a981a202l,0x3531dd183a893387l,0x1be11f90c85c7209l,
        0x93d2fe1ee2a52b5al } },
    /* 47 << 42 */
    { { 0x8225bfe2ec6d6b97l,0x9cf6d6f4bd0aa5del,0x911459cb54779f5fl,
        0x5649cddb86aeb1f3l },
      { 0x321335793f26ce5al,0xc289a102550f431el,0x559dcfda73b84c6fl,
        0x84973819ee3ac4d7l } },
    /* 48 << 42 */
    { { 0xb51e55e6f2606a82l,0xe25f706190f2fb57l,0xacef6c2ab1a4e37cl,
        0x864e359d5dcf2706l },
      { 0x479e6b187ce57316l,0x2cab25003a96b23dl,0xed4898628ef16df7l,
        0x2056538cef3758b5l } },
    /* 49 << 42 */
    { { 0xa7df865ef15d3101l,0x80c5533a61b553d7l,0x366e19974ed14294l,
        0x6620741fb3c0bcd6l },
      { 0x21d1d9c4edc45418l,0x005b859ec1cc4a9dl,0xdf01f630a1c462f0l,
        0x15d06cf3f26820c7l } },
    /* 50 << 42 */
    { { 0x9f7f24ee3484be47l,0x2ff33e964a0c902fl,0x00bdf4575a0bc453l,
        0x2378dfaf1aa238dbl },
      { 0x272420ec856720f2l,0x2ad9d95b96797291l,0xd1242cc6768a1558l,
        0x2e287f8b5cc86aa8l } },
    /* 51 << 42 */
    { { 0x796873d0990cecaal,0xade55f81675d4080l,0x2645eea321f0cd84l,
        0x7a1efa0fb4e17d02l },
      { 0xf6858420037cc061l,0x682e05f0d5d43e12l,0x59c3699427218710l,
        0x85cbba4d3f7cd2fcl } },
    /* 52 << 42 */
    { { 0x726f97297a3cd22al,0x9f8cd5dc4a628397l,0x17b93ab9c23165edl,
        0xff5f5dbf122823d4l },
      { 0xc1e4e4b5654a446dl,0xd1a9496f677257bal,0x6387ba94de766a56l,
        0x23608bc8521ec74al } },
    /* 53 << 42 */
    { { 0x16a522d76688c4d4l,0x9d6b428207373abdl,0xa62f07acb42efaa3l,
        0xf73e00f7e3b90180l },
      { 0x36175fec49421c3el,0xc4e44f9b3dcf2678l,0x76df436b7220f09fl,
        0x172755fb3aa8b6cfl } },
    /* 54 << 42 */
    { { 0xbab89d57446139ccl,0x0a0a6e025fe0208fl,0xcdbb63e211e5d399l,
        0x33ecaa12a8977f0bl },
      { 0x59598b21f7c42664l,0xb3e91b32ab65d08al,0x035822eef4502526l,
        0x1dcf0176720a82a9l } },
    /* 55 << 42 */
    { { 0x50f8598f3d589e02l,0xdf0478ffb1d63d2cl,0x8b8068bd1571cd07l,
        0x30c3aa4fd79670cdl },
      { 0x25e8fd4b941ade7fl,0x3d1debdc32790011l,0x65b6dcbd3a3f9ff0l,
        0x282736a4793de69cl } },
    /* 56 << 42 */
    { { 0xef69a0c3d41d3bd3l,0xb533b8c907a26bdel,0xe2801d97db2edf9fl,
        0xdc4a8269e1877af0l },
      { 0x6c1c58513d590dbel,0x84632f6bee4e9357l,0xd36d36b779b33374l,
        0xb46833e39bbca2e6l } },
    /* 57 << 42 */
    { { 0x37893913f7fc0586l,0x385315f766bf4719l,0x72c56293b31855dcl,
        0xd1416d4e849061fel },
      { 0xbeb3ab7851047213l,0x447f6e61f040c996l,0xd06d310d638b1d0cl,
        0xe28a413fbad1522el } },
    /* 58 << 42 */
    { { 0x685a76cb82003f86l,0x610d07f70bcdbca3l,0x6ff660219ca4c455l,
        0x7df39b87cea10eecl },
      { 0xb9255f96e22db218l,0x8cc6d9eb08a34c44l,0xcd4ffb86859f9276l,
        0x8fa15eb250d07335l } },
    /* 59 << 42 */
    { { 0xdf553845cf2c24b5l,0x89f66a9f52f9c3bal,0x8f22b5b9e4a7ceb3l,
        0xaffef8090e134686l },
      { 0x3e53e1c68eb8fac2l,0x93c1e4eb28aec98el,0xb6b91ec532a43bcbl,
        0x2dbfa947b2d74a51l } },
    /* 60 << 42 */
    { { 0xe065d190ca84bad7l,0xfb13919fad58e65cl,0x3c41718bf1cb6e31l,
        0x688969f006d05c3fl },
      { 0xd4f94ce721264d45l,0xfdfb65e97367532bl,0x5b1be8b10945a39dl,
        0x229f789c2b8baf3bl } },
    /* 61 << 42 */
    { { 0xd8f41f3e6f49f15dl,0x678ce828907f0792l,0xc69ace82fca6e867l,
        0x106451aed01dcc89l },
      { 0x1bb4f7f019fc32d2l,0x64633dfcb00c52d2l,0x8f13549aad9ea445l,
        0x99a3bf50fb323705l } },
    /* 62 << 42 */
    { { 0x0c9625a2534d4dbcl,0x45b8f1d1c2a2fea3l,0x76ec21a1a530fc1al,
        0x4bac9c2a9e5bd734l },
      { 0x5996d76a7b4e3587l,0x0045cdee1182d9e3l,0x1aee24b91207f13dl,
        0x66452e9797345a41l } },
    /* 63 << 42 */
    { { 0x16e5b0549f950cd0l,0x9cc72fb1d7fdd075l,0x6edd61e766249663l,
        0xde4caa4df043cccbl },
      { 0x11b1f57a55c7ac17l,0x779cbd441a85e24dl,0x78030f86e46081e7l,
        0xfd4a60328e20f643l } },
    /* 64 << 42 */
    { { 0xcc7a64880a750c0fl,0x39bacfe34e548e83l,0x3d418c760c110f05l,
        0x3e4daa4cb1f11588l },
      { 0x2733e7b55ffc69ffl,0x46f147bc92053127l,0x885b2434d722df94l,
        0x6a444f65e6fc6b7cl } },
    /* 0 << 49 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 49 */
    { { 0x7a1a465ac3f16ea8l,0x115a461db2f1d11cl,0x4767dd956c68a172l,
        0x3392f2ebd13a4698l },
      { 0xc7a99ccde526cdc7l,0x8e537fdc22292b81l,0x76d8cf69a6d39198l,
        0xffc5ff432446852dl } },
    /* 2 << 49 */
    { { 0x97b14f7ea90567e6l,0x513257b7b6ae5cb7l,0x85454a3c9f10903dl,
        0xd8d2c9ad69bc3724l },
      { 0x38da93246b29cb44l,0xb540a21d77c8cbacl,0x9bbfe43501918e42l,
        0xfffa707a56c3614el } },
    /* 3 << 49 */
    { { 0x0ce4e3f1d4e353b7l,0x062d8a14ef46b0a0l,0x6408d5ab574b73fdl,
        0xbc41d1c9d3273ffdl },
      { 0x3538e1e76be77800l,0x71fe8b37c5655031l,0x1cd916216b9b331al,
        0xad825d0bbb388f73l } },
    /* 4 << 49 */
    { { 0x56c2e05b1cb76219l,0x0ec0bf9171567e7el,0xe7076f8661c4c910l,
        0xd67b085bbabc04d9l },
      { 0x9fb904595e93a96al,0x7526c1eafbdc249al,0x0d44d367ecdd0bb7l,
        0x953999179dc0d695l } },
    /* 5 << 49 */
    { { 0x61360ee99e240d18l,0x057cdcacb4b94466l,0xe7667cd12fe5325cl,
        0x1fa297b521974e3bl },
      { 0xfa4081e7db083d76l,0x31993be6f206bd15l,0x8949269b14c19f8cl,
        0x21468d72a9d92357l } },
    /* 6 << 49 */
    { { 0x2ccbc583a4c506ecl,0x957ed188d1acfe97l,0x8baed83312f1aea2l,
        0xef2a6cb48325362dl },
      { 0x130dde428e195c43l,0xc842025a0e6050c6l,0x2da972a708686a5dl,
        0xb52999a1e508b4a8l } },
    /* 7 << 49 */
    { { 0xd9f090b910a5a8bdl,0xca91d249096864dal,0x8e6a93be3f67dbc1l,
        0xacae6fbaf5f4764cl },
      { 0x1563c6e0d21411a0l,0x28fa787fda0a4ad8l,0xd524491c908c8030l,
        0x1257ba0e4c795f07l } },
    /* 8 << 49 */
    { { 0x83f49167ceca9754l,0x426d2cf64b7939a0l,0x2555e355723fd0bfl,
        0xa96e6d06c4f144e2l },
      { 0x4768a8dd87880e61l,0x15543815e508e4d5l,0x09d7e772b1b65e15l,
        0x63439dd6ac302fa0l } },
    /* 9 << 49 */
    { { 0xb93f802fc14e35c2l,0x71735b7c4341333cl,0x03a2510416d4f362l,
        0x3f4d069bbf433c8el },
      { 0x0d83ae01f78f5a7cl,0x50a8ffbe7c4eed07l,0xc74f890676e10f83l,
        0x7d0809669ddaf8e1l } },
    /* 10 << 49 */
    { { 0xb11df8e1698e04ccl,0x877be203169005c8l,0x32749e8c4f3c6179l,
        0x2dbc9d0a7853fc05l },
      { 0x187d4f939454d937l,0xe682ce9db4800e1bl,0xa9129ad8165e68e8l,
        0x0fe29735be7f785bl } },
    /* 11 << 49 */
    { { 0x5303f40c5b9e02b7l,0xa37c969235ee04e8l,0x5f46cc2034d6632bl,
        0x55ef72b296ac545bl },
      { 0xabec5c1f7b91b062l,0x0a79e1c7bb33e821l,0xbb04b4283a9f4117l,
        0x0de1f28ffd2a475al } },
    /* 12 << 49 */
    { { 0x31019ccf3a4434b4l,0xa34581111a7954dcl,0xa9dac80de34972a7l,
        0xb043d05474f6b8ddl },
      { 0x021c319e11137b1al,0x00a754ceed5cc03fl,0x0aa2c794cbea5ad4l,
        0x093e67f470c015b6l } },
    /* 13 << 49 */
    { { 0x72cdfee9c97e3f6bl,0xc10bcab4b6da7461l,0x3b02d2fcb59806b9l,
        0x85185e89a1de6f47l },
      { 0x39e6931f0eb6c4d4l,0x4d4440bdd4fa5b04l,0x5418786e34be7eb8l,
        0x6380e5219d7259bcl } },
    /* 14 << 49 */
    { { 0x20ac0351d598d710l,0x272c4166cb3a4da4l,0xdb82fe1aca71de1fl,
        0x746e79f2d8f54b0fl },
      { 0x6e7fc7364b573e9bl,0x75d03f46fd4b5040l,0x5c1cc36d0b98d87bl,
        0x513ba3f11f472da1l } },
    /* 15 << 49 */
    { { 0x79d0af26abb177ddl,0xf82ab5687891d564l,0x2b6768a972232173l,
        0xefbb3bb08c1f6619l },
      { 0xb29c11dba6d18358l,0x519e2797b0916d3al,0xd4dc18f09188e290l,
        0x648e86e398b0ca7fl } },
    /* 16 << 49 */
    { { 0x859d3145983c38b5l,0xb14f176c637abc8bl,0x2793fb9dcaff7be6l,
        0xebe5a55f35a66a5al },
      { 0x7cec1dcd9f87dc59l,0x7c595cd3fbdbf560l,0x5b543b2226eb3257l,
        0x69080646c4c935fdl } },
    /* 17 << 49 */
    { { 0x7f2e440381e9ede3l,0x243c3894caf6df0al,0x7c605bb11c073b11l,
        0xcd06a541ba6a4a62l },
      { 0x2916894949d4e2e5l,0x33649d074af66880l,0xbfc0c885e9a85035l,
        0xb4e52113fc410f4bl } },
    /* 18 << 49 */
    { { 0xdca3b70678a6513bl,0x92ea4a2a9edb1943l,0x02642216db6e2dd8l,
        0x9b45d0b49fd57894l },
      { 0x114e70dbc69d11ael,0x1477dd194c57595fl,0xbc2208b4ec77c272l,
        0x95c5b4d7db68f59cl } },
    /* 19 << 49 */
    { { 0xb8c4fc6342e532b7l,0x386ba4229ae35290l,0xfb5dda42d201ecbcl,
        0x2353dc8ba0e38fd6l },
      { 0x9a0b85ea68f7e978l,0x96ec56822ad6d11fl,0x5e279d6ce5f6886dl,
        0xd3fe03cd3cb1914dl } },
    /* 20 << 49 */
    { { 0xfe541fa47ea67c77l,0x952bd2afe3ea810cl,0x791fef568d01d374l,
        0xa3a1c6210f11336el },
      { 0x5ad0d5a9c7ec6d79l,0xff7038af3225c342l,0x003c6689bc69601bl,
        0x25059bc745e8747dl } },
    /* 21 << 49 */
    { { 0xfa4965b2f2086fbfl,0xf6840ea686916078l,0xd7ac762070081d6cl,
        0xe600da31b5328645l },
      { 0x01916f63529b8a80l,0xe80e48582d7d6f3el,0x29eb0fe8d664ca7cl,
        0xf017637be7b43b0cl } },
    /* 22 << 49 */
    { { 0x9a75c80676cb2566l,0x8f76acb1b24892d9l,0x7ae7b9cc1f08fe45l,
        0x19ef73296a4907d8l },
      { 0x2db4ab715f228bf0l,0xf3cdea39817032d7l,0x0b1f482edcabe3c0l,
        0x3baf76b4bb86325cl } },
    /* 23 << 49 */
    { { 0xd49065e010089465l,0x3bab5d298e77c596l,0x7636c3a6193dbd95l,
        0xdef5d294b246e499l },
      { 0xb22c58b9286b2475l,0xa0b93939cd80862bl,0x3002c83af0992388l,
        0x6de01f9beacbe14cl } },
    /* 24 << 49 */
    { { 0x6aac688eadd70482l,0x708de92a7b4a4e8al,0x75b6dd73758a6eefl,
        0xea4bf352725b3c43l },
      { 0x10041f2c87912868l,0xb1b1be95ef09297al,0x19ae23c5a9f3860al,
        0xc4f0f839515dcf4bl } },
    /* 25 << 49 */
    { { 0x3c7ecca397f6306al,0x744c44ae68a3a4b0l,0x69cd13a0b3a1d8a2l,
        0x7cad0a1e5256b578l },
      { 0xea653fcd33791d9el,0x9cc2a05d74b2e05fl,0x73b391dcfd7affa2l,
        0xddb7091eb6b05442l } },
    /* 26 << 49 */
    { { 0xc71e27bf8538a5c6l,0x195c63dd89abff17l,0xfd3152851b71e3dal,
        0x9cbdfda7fa680fa0l },
      { 0x9db876ca849d7eabl,0xebe2764b3c273271l,0x663357e3f208dceal,
        0x8c5bd833565b1b70l } },
    /* 27 << 49 */
    { { 0xccc3b4f59837fc0dl,0x9b641ba8a79cf00fl,0x7428243ddfdf3990l,
        0x83a594c4020786b1l },
      { 0xb712451a526c4502l,0x9d39438e6adb3f93l,0xfdb261e3e9ff0ccdl,
        0x80344e3ce07af4c3l } },
    /* 28 << 49 */
    { { 0x75900d7c2fa4f126l,0x08a3b8655c99a232l,0x2478b6bfdb25e0c3l,
        0x482cc2c271db2edfl },
      { 0x37df7e645f321bb8l,0x8a93821b9a8005b4l,0x3fa2f10ccc8c1958l,
        0x0d3322182c269d0al } },
    /* 29 << 49 */
    { { 0x20ab8119e246b0e6l,0xb39781e4d349fd17l,0xd293231eb31aa100l,
        0x4b779c97bb032168l },
      { 0x4b3f19e1c8470500l,0x45b7efe90c4c869dl,0xdb84f38aa1a6bbccl,
        0x3b59cb15b2fddbc1l } },
    /* 30 << 49 */
    { { 0xba5514df3fd165e8l,0x499fd6a9061f8811l,0x72cd1fe0bfef9f00l,
        0x120a4bb979ad7e8al },
      { 0xf2ffd0955f4a5ac5l,0xcfd174f195a7a2f0l,0xd42301ba9d17baf1l,
        0xd2fa487a77f22089l } },
    /* 31 << 49 */
    { { 0x9cb09efeb1dc77e1l,0xe956693921c99682l,0x8c5469016c6067bbl,
        0xfd37857461c24456l },
      { 0x2b6a6cbe81796b33l,0x62d550f658e87f8bl,0x1b763e1c7f1b01b4l,
        0x4b93cfea1b1b5e12l } },
    /* 32 << 49 */
    { { 0xb93452381d531696l,0x57201c0088cdde69l,0xdde922519a86afc7l,
        0xe3043895bd35cea8l },
      { 0x7608c1e18555970dl,0x8267dfa92535935el,0xd4c60a57322ea38bl,
        0xe0bf7977804ef8b5l } },
    /* 33 << 49 */
    { { 0x1a0dab28c06fece4l,0xd405991e94e7b49dl,0xc542b6d2706dab28l,
        0xcb228da3a91618fbl },
      { 0x224e4164107d1ceal,0xeb9fdab3d0f5d8f1l,0xc02ba3860d6e41cdl,
        0x676a72c59b1f7146l } },
    /* 34 << 49 */
    { { 0xffd6dd984d6cb00bl,0xcef9c5cade2e8d7cl,0xa1bbf5d7641c7936l,
        0x1b95b230ee8f772el },
      { 0xf765a92ee8ac25b1l,0xceb04cfc3a18b7c6l,0x27944cef0acc8966l,
        0xcbb3c957434c1004l } },
    /* 35 << 49 */
    { { 0x9c9971a1a43ff93cl,0x5bc2db17a1e358a9l,0x45b4862ea8d9bc82l,
        0x70ebfbfb2201e052l },
      { 0xafdf64c792871591l,0xea5bcae6b42d0219l,0xde536c552ad8f03cl,
        0xcd6c3f4da76aa33cl } },
    /* 36 << 49 */
    { { 0xbeb5f6230bca6de3l,0xdd20dd99b1e706fdl,0x90b3ff9dac9059d4l,
        0x2d7b29027ccccc4el },
      { 0x8a090a59ce98840fl,0xa5d947e08410680al,0x49ae346a923379a5l,
        0x7dbc84f9b28a3156l } },
    /* 37 << 49 */
    { { 0xfd40d91654a1aff2l,0xabf318ba3a78fb9bl,0x50152ed83029f95el,
        0x9fc1dd77c58ad7fal },
      { 0x5fa5791513595c17l,0xb95046688f62b3a9l,0x907b5b24ff3055b0l,
        0x2e995e359a84f125l } },
    /* 38 << 49 */
    { { 0x87dacf697e9bbcfbl,0x95d0c1d6e86d96e3l,0x65726e3c2d95a75cl,
        0x2c3c9001acd27f21l },
      { 0x1deab5616c973f57l,0x108b7e2ca5221643l,0x5fee9859c4ef79d4l,
        0xbd62b88a40d4b8c6l } },
    /* 39 << 49 */
    { { 0xb4dd29c4197c75d6l,0x266a6df2b7076febl,0x9512d0ea4bf2df11l,
        0x1320c24f6b0cc9ecl },
      { 0x6bb1e0e101a59596l,0x8317c5bbeff9aaacl,0x65bb405e385aa6c9l,
        0x613439c18f07988fl } },
    /* 40 << 49 */
    { { 0xd730049f16a66e91l,0xe97f2820fa1b0e0dl,0x4131e003304c28eal,
        0x820ab732526bac62l },
      { 0xb2ac9ef928714423l,0x54ecfffaadb10cb2l,0x8781476ef886a4ccl,
        0x4b2c87b5db2f8d49l } },
    /* 41 << 49 */
    { { 0xe857cd200a44295dl,0x707d7d2158c6b044l,0xae8521f9f596757cl,
        0x87448f0367b2b714l },
      { 0x13a9bc455ebcd58dl,0x79bcced99122d3c1l,0x3c6442479e076642l,
        0x0cf227782df4767dl } },
    /* 42 << 49 */
    { { 0x5e61aee471d444b6l,0x211236bfc5084a1dl,0x7e15bc9a4fd3eaf6l,
        0x68df2c34ab622bf5l },
      { 0x9e674f0f59bf4f36l,0xf883669bd7f34d73l,0xc48ac1b831497b1dl,
        0x323b925d5106703bl } },
    /* 43 << 49 */
    { { 0x22156f4274082008l,0xeffc521ac8482bcbl,0x5c6831bf12173479l,
        0xcaa2528fc4739490l },
      { 0x84d2102a8f1b3c4dl,0xcf64dfc12d9bec0dl,0x433febad78a546efl,
        0x1f621ec37b73cef1l } },
    /* 44 << 49 */
    { { 0x6aecd62737338615l,0x162082ab01d8edf6l,0x833a811919e86b66l,
        0x6023a251d299b5dbl },
      { 0xf5bb0c3abbf04b89l,0x6735eb69ae749a44l,0xd0e058c54713de3bl,
        0xfdf2593e2c3d4ccdl } },
    /* 45 << 49 */
    { { 0x1b8f414efdd23667l,0xdd52aacafa2015eel,0x3e31b517bd9625ffl,
        0x5ec9322d8db5918cl },
      { 0xbc73ac85a96f5294l,0x82aa5bf361a0666al,0x49755810bf08ac42l,
        0xd21cdfd5891cedfcl } },
    /* 46 << 49 */
    { { 0x918cb57b67f8be10l,0x365d1a7c56ffa726l,0x2435c5046532de93l,
        0xc0fc5e102674cd02l },
      { 0x6e51fcf89cbbb142l,0x1d436e5aafc50692l,0x766bffff3fbcae22l,
        0x3148c2fdfd55d3b8l } },
    /* 47 << 49 */
    { { 0x52c7fdc9233222fal,0x89ff1092e419fb6bl,0x3cd6db9925254977l,
        0x2e85a1611cf12ca7l },
      { 0xadd2547cdc810bc9l,0xea3f458f9d257c22l,0x642c1fbe27d6b19bl,
        0xed07e6b5140481a6l } },
    /* 48 << 49 */
    { { 0x6ada1d4286d2e0f8l,0xe59201220e8a9fd5l,0x02c936af708c1b49l,
        0x60f30fee2b4bfaffl },
      { 0x6637ad06858e6a61l,0xce4c77673fd374d0l,0x39d54b2d7188defbl,
        0xa8c9d250f56a6b66l } },
    /* 49 << 49 */
    { { 0x58fc0f5eb24fe1dcl,0x9eaf9dee6b73f24cl,0xa90d588b33650705l,
        0xde5b62c5af2ec729l },
      { 0x5c72cfaed3c2b36el,0x868c19d5034435dal,0x88605f93e17ee145l,
        0xaa60c4ee77a5d5b1l } },
    /* 50 << 49 */
    { { 0xbcf5bfd23b60c472l,0xaf4ef13ceb1d3049l,0x373f44fce13895c9l,
        0xf29b382f0cbc9822l },
      { 0x1bfcb85373efaef6l,0xcf56ac9ca8c96f40l,0xd7adf1097a191e24l,
        0x98035f44bf8a8dc2l } },
    /* 51 << 49 */
    { { 0xf40a71b91e750c84l,0xc57f7b0c5dc6c469l,0x49a0e79c6fbc19c1l,
        0x6b0f5889a48ebdb8l },
      { 0x5d3fd084a07c4e9fl,0xc3830111ab27de14l,0x0e4929fe33e08dccl,
        0xf4a5ad2440bb73a3l } },
    /* 52 << 49 */
    { { 0xde86c2bf490f97cal,0x288f09c667a1ce18l,0x364bb8861844478dl,
        0x7840fa42ceedb040l },
      { 0x1269fdd25a631b37l,0x94761f1ea47c8b7dl,0xfc0c2e17481c6266l,
        0x85e16ea23daa5fa7l } },
    /* 53 << 49 */
    { { 0xccd8603392491048l,0x0c2f6963f4d402d7l,0x6336f7dfdf6a865cl,
        0x0a2a463cb5c02a87l },
      { 0xb0e29be7bf2f12eel,0xf0a2200266bad988l,0x27f87e039123c1d7l,
        0x21669c55328a8c98l } },
    /* 54 << 49 */
    { { 0x186b980392f14529l,0xd3d056cc63954df3l,0x2f03fd58175a46f6l,
        0x63e34ebe11558558l },
      { 0xe13fedee5b80cfa5l,0xe872a120d401dbd1l,0x52657616e8a9d667l,
        0xbc8da4b6e08d6693l } },
    /* 55 << 49 */
    { { 0x370fb9bb1b703e75l,0x6773b186d4338363l,0x18dad378ecef7bffl,
        0xaac787ed995677dal },
      { 0x4801ea8b0437164bl,0xf430ad2073fe795el,0xb164154d8ee5eb73l,
        0x0884ecd8108f7c0el } },
    /* 56 << 49 */
    { { 0x0e6ec0965f520698l,0x640631fe44f7b8d9l,0x92fd34fca35a68b9l,
        0x9c5a4b664d40cf4el },
      { 0x949454bf80b6783dl,0x80e701fe3a320a10l,0x8d1a564a1a0a39b2l,
        0x1436d53d320587dbl } },
    /* 57 << 49 */
    { { 0xf5096e6d6556c362l,0xbc23a3c0e2455d7el,0x3a7aee54807230f9l,
        0x9ba1cfa622ae82fdl },
      { 0x833a057a99c5d706l,0x8be85f4b842315c9l,0xd083179a66a72f12l,
        0x2fc77d5dcdcc73cdl } },
    /* 58 << 49 */
    { { 0x22b88a805616ee30l,0xfb09548fe7ab1083l,0x8ad6ab0d511270cdl,
        0x61f6c57a6924d9abl },
      { 0xa0f7bf7290aecb08l,0x849f87c90df784a4l,0x27c79c15cfaf1d03l,
        0xbbf9f675c463facel } },
    /* 59 << 49 */
    { { 0x91502c65765ba543l,0x18ce3cac42ea60ddl,0xe5cee6ac6e43ecb3l,
        0x63e4e91068f2aeebl },
      { 0x26234fa3c85932eel,0x96883e8b4c90c44dl,0x29b9e738a18a50f6l,
        0xbfc62b2a3f0420dfl } },
    /* 60 << 49 */
    { { 0xd22a7d906d3e1fa9l,0x17115618fe05b8a3l,0x2a0c9926bb2b9c01l,
        0xc739fcc6e07e76a2l },
      { 0x540e9157165e439al,0x06353a626a9063d8l,0x84d9559461e927a3l,
        0x013b9b26e2e0be7fl } },
    /* 61 << 49 */
    { { 0x4feaec3b973497f1l,0x15c0f94e093ebc2dl,0x6af5f22733af0583l,
        0x0c2af206c61f3340l },
      { 0xd25dbdf14457397cl,0x2e8ed017cabcbae0l,0xe3010938c2815306l,
        0xbaa99337e8c6cd68l } },
    /* 62 << 49 */
    { { 0x085131823b0ec7del,0x1e1b822b58df05dfl,0x5c14842fa5c3b683l,
        0x98fe977e3eba34cel },
      { 0xfd2316c20d5e8873l,0xe48d839abd0d427dl,0x495b2218623fc961l,
        0x24ee56e7b46fba5el } },
    /* 63 << 49 */
    { { 0x9184a55b91e4de58l,0xa7488ca5dfdea288l,0xa723862ea8dcc943l,
        0x92d762b2849dc0fcl },
      { 0x3c444a12091ff4a9l,0x581113fa0cada274l,0xb9de0a4530d8eae2l,
        0x5e0fcd85df6b41eal } },
    /* 64 << 49 */
    { { 0x6233ea68c094dbb5l,0xb77d062ed968d410l,0x3e719bbc58b3002dl,
        0x68e7dd3d3dc49d58l },
      { 0x8d825740013a5e58l,0x213117473c9e3c1bl,0x0cb0a2a77c99b6abl,
        0x5c48a3b3c2f888f2l } },
    /* 0 << 56 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 56 */
    { { 0xc7913e91991724f3l,0x5eda799c39cbd686l,0xddb595c763d4fc1el,
        0x6b63b80bac4fed54l },
      { 0x6ea0fc697e5fb516l,0x737708bad0f1c964l,0x9628745f11a92ca5l,
        0x61f379589a86967al } },
    /* 2 << 56 */
    { { 0x9af39b2caa665072l,0x78322fa4efd324efl,0x3d153394c327bd31l,
        0x81d5f2713129dab0l },
      { 0xc72e0c42f48027f5l,0xaa40cdbc8536e717l,0xf45a657a2d369d0fl,
        0xb03bbfc4ea7f74e6l } },
    /* 3 << 56 */
    { { 0x46a8c4180d738dedl,0x6f1a5bb0e0de5729l,0xf10230b98ba81675l,
        0x32c6f30c112b33d4l },
      { 0x7559129dd8fffb62l,0x6a281b47b459bf05l,0x77c1bd3afa3b6776l,
        0x0709b3807829973al } },
    /* 4 << 56 */
    { { 0x8c26b232a3326505l,0x38d69272ee1d41bfl,0x0459453effe32afal,
        0xce8143ad7cb3ea87l },
      { 0x932ec1fa7e6ab666l,0x6cd2d23022286264l,0x459a46fe6736f8edl,
        0x50bf0d009eca85bbl } },
    /* 5 << 56 */
    { { 0x0b825852877a21ecl,0x300414a70f537a94l,0x3f1cba4021a9a6a2l,
        0x50824eee76943c00l },
      { 0xa0dbfcecf83cba5dl,0xf953814893b4f3c0l,0x6174416248f24dd7l,
        0x5322d64de4fb09ddl } },
    /* 6 << 56 */
    { { 0x574473843d9325f3l,0xa9bef2d0f371cb84l,0x77d2188ba61e36c5l,
        0xbbd6a7d7c602df72l },
      { 0xba3aa9028f61bc0bl,0xf49085ed6ed0b6a1l,0x8bc625d6ae6e8298l,
        0x832b0b1da2e9c01dl } },
    /* 7 << 56 */
    { { 0xa337c447f1f0ced1l,0x800cc7939492dd2bl,0x4b93151dbea08efal,
        0x820cf3f8de0a741el },
      { 0xff1982dc1c0f7d13l,0xef92196084dde6cal,0x1ad7d97245f96ee3l,
        0x319c8dbe29dea0c7l } },
    /* 8 << 56 */
    { { 0xd3ea38717b82b99bl,0x75922d4d470eb624l,0x8f66ec543b95d466l,
        0x66e673ccbee1e346l },
      { 0x6afe67c4b5f2b89al,0x3de9c1e6290e5cd3l,0x8c278bb6310a2adal,
        0x420fa3840bdb323bl } },
    /* 9 << 56 */
    { { 0x0ae1d63b0eb919b0l,0xd74ee51da74b9620l,0x395458d0a674290cl,
        0x324c930f4620a510l },
      { 0x2d1f4d19fbac27d4l,0x4086e8ca9bedeeacl,0x0cdd211b9b679ab8l,
        0x5970167d7090fec4l } },
    /* 10 << 56 */
    { { 0x3420f2c9faf1fc63l,0x616d333a328c8bb4l,0x7d65364c57f1fe4al,
        0x9343e87755e5c73al },
      { 0x5795176be970e78cl,0xa36ccebf60533627l,0xfc7c738009cdfc1bl,
        0xb39a2afeb3fec326l } },
    /* 11 << 56 */
    { { 0xb7ff1ba16224408al,0xcc856e92247cfc5el,0x01f102e7c18bc493l,
        0x4613ab742091c727l },
      { 0xaa25e89cc420bf2bl,0x00a5317690337ec2l,0xd2be9f437d025fc7l,
        0x3316fb856e6fe3dcl } },
    /* 12 << 56 */
    { { 0x27520af59ac50814l,0xfdf95e789a8e4223l,0xb7e7df2a56bec5a0l,
        0xf7022f7ddf159e5dl },
      { 0x93eeeab1cac1fe8fl,0x8040188c37451168l,0x7ee8aa8ad967dce6l,
        0xfa0e79e73abc9299l } },
    /* 13 << 56 */
    { { 0x67332cfc2064cfd1l,0x339c31deb0651934l,0x719b28d52a3bcbeal,
        0xee74c82b9d6ae5c6l },
      { 0x0927d05ebaf28ee6l,0x82cecf2c9d719028l,0x0b0d353eddb30289l,
        0xfe4bb977fddb2e29l } },
    /* 14 << 56 */
    { { 0xbb5bb990640bfd9el,0xd226e27782f62108l,0x4bf0098502ffdd56l,
        0x7756758a2ca1b1b5l },
      { 0xc32b62a35285fe91l,0xedbc546a8c9cd140l,0x1e47a013af5cb008l,
        0xbca7e720073ce8f2l } },
    /* 15 << 56 */
    { { 0xe10b2ab817a91cael,0xb89aab6508e27f63l,0x7b3074a7dba3ddf9l,
        0x1c20ce09330c2972l },
      { 0x6b9917b45fcf7e33l,0xe6793743945ceb42l,0x18fc22155c633d19l,
        0xad1adb3cc7485474l } },
    /* 16 << 56 */
    { { 0x646f96796424c49bl,0xf888dfe867c241c9l,0xe12d4b9324f68b49l,
        0x9a6b62d8a571df20l },
      { 0x81b4b26d179483cbl,0x666f96329511fae2l,0xd281b3e4d53aa51fl,
        0x7f96a7657f3dbd16l } },
    /* 17 << 56 */
    { { 0xa7f8b5bf074a30cel,0xd7f52107005a32e6l,0x6f9e090750237ed4l,
        0x2f21da478096fa2bl },
      { 0xf3e19cb4eec863a0l,0xd18f77fd9527620al,0x9505c81c407c1cf8l,
        0x9998db4e1b6ec284l } },
    /* 18 << 56 */
    { { 0x7e3389e5c247d44dl,0x125071413f4f3d80l,0xd4ba01104a78a6c7l,
        0x312874a0767720bel },
      { 0xded059a675944370l,0xd6123d903b2c0bddl,0xa56b717b51c108e3l,
        0x9bb7940e070623e9l } },
    /* 19 << 56 */
    { { 0x794e2d5984ac066cl,0xf5954a92e68c69a0l,0x28c524584fd99dccl,
        0x60e639fcb1012517l },
      { 0xc2e601257de79248l,0xe9ef6404f12fc6d7l,0x4c4f28082a3b5d32l,
        0x865ad32ec768eb8al } },
    /* 20 << 56 */
    { { 0xac02331b13fb70b6l,0x037b44c195599b27l,0x1a860fc460bd082cl,
        0xa2e25745c980cd01l },
      { 0xee3387a81da0263el,0x931bfb952d10f3d6l,0x5b687270a1f24a32l,
        0xf140e65dca494b86l } },
    /* 21 << 56 */
    { { 0x4f4ddf91b2f1ac7al,0xf99eaabb760fee27l,0x57f4008a49c228e5l,
        0x090be4401cf713bbl },
      { 0xac91fbe45004f022l,0xd838c2c2569e1af6l,0xd6c7d20b0f1daaa5l,
        0xaa063ac11bbb02c0l } },
    /* 22 << 56 */
    { { 0x0938a42259558a78l,0x5343c6698435da2fl,0x96f67b18034410dcl,
        0x7cc1e42484510804l },
      { 0x86a1543f16dfbb7dl,0x921fa9425b5bd592l,0x9dcccb6eb33dd03cl,
        0x8581ddd9b843f51el } },
    /* 23 << 56 */
    { { 0x54935fcb81d73c9el,0x6d07e9790a5e97abl,0x4dc7b30acf3a6babl,
        0x147ab1f3170bee11l },
      { 0x0aaf8e3d9fafdee4l,0xfab3dbcb538a8b95l,0x405df4b36ef13871l,
        0xf1f4e9cb088d5a49l } },
    /* 24 << 56 */
    { { 0x9bcd24d366b33f1dl,0x3b97b8205ce445c0l,0xe2926549ba93ff61l,
        0xd9c341ce4dafe616l },
      { 0xfb30a76e16efb6f3l,0xdf24b8ca605b953cl,0x8bd52afec2fffb9fl,
        0xbbac5ff7e19d0b96l } },
    /* 25 << 56 */
    { { 0x43c01b87459afccdl,0x6bd45143b7432652l,0x8473453055b5d78el,
        0x81088fdb1554ba7dl },
      { 0xada0a52c1e269375l,0xf9f037c42dc5ec10l,0xc066060794bfbc11l,
        0xc0a630bbc9c40d2fl } },
    /* 26 << 56 */
    { { 0x5efc797eab64c31el,0xffdb1dab74507144l,0xf61242871ca6790cl,
        0xe9609d81e69bf1bfl },
      { 0xdb89859500d24fc9l,0x9c750333e51fb417l,0x51830a91fef7bbdel,
        0x0ce67dc8945f585cl } },
    /* 27 << 56 */
    { { 0x9a730ed44763eb50l,0x24a0e221c1ab0d66l,0x643b6393648748f3l,
        0x1982daa16d3c6291l },
      { 0x6f00a9f78bbc5549l,0x7a1783e17f36384el,0xe8346323de977f50l,
        0x91ab688db245502al } },
    /* 28 << 56 */
    { { 0x331ab6b56d0bdd66l,0x0a6ef32e64b71229l,0x1028150efe7c352fl,
        0x27e04350ce7b39d3l },
      { 0x2a3c8acdc1070c82l,0xfb2034d380c9feefl,0x2d729621709f3729l,
        0x8df290bf62cb4549l } },
    /* 29 << 56 */
    { { 0x02f99f33fc2e4326l,0x3b30076d5eddf032l,0xbb21f8cf0c652fb5l,
        0x314fb49eed91cf7bl },
      { 0xa013eca52f700750l,0x2b9e3c23712a4575l,0xe5355557af30fbb0l,
        0x1ada35167c77e771l } },
    /* 30 << 56 */
    { { 0x45f6ecb27b135670l,0xe85d19df7cfc202el,0x0f1b50c758d1be9fl,
        0x5ebf2c0aead2e344l },
      { 0x1531fe4eabc199c9l,0xc703259256bab0ael,0x16ab2e486c1fec54l,
        0x0f87fda804280188l } },
    /* 31 << 56 */
    { { 0xdc9f46fc609e4a74l,0x2a44a143ba667f91l,0xbc3d8b95b4d83436l,
        0xa01e4bd0c7bd2958l },
      { 0x7b18293273483c90l,0xa79c6aa1a7c7b598l,0xbf3983c6eaaac07el,
        0x8f18181e96e0d4e6l } },
    /* 32 << 56 */
    { { 0x8553d37c051af62bl,0xe9a998eb0bf94496l,0xe0844f9fb0d59aa1l,
        0x983fd558e6afb813l },
      { 0x9670c0ca65d69804l,0x732b22de6ea5ff2dl,0xd7640ba95fd8623bl,
        0x9f619163a6351782l } },
    /* 33 << 56 */
    { { 0x0bfc27eeacee5043l,0xae419e732eb10f02l,0x19c028d18943fb05l,
        0x71f01cf7ff13aa2al },
      { 0x7790737e8887a132l,0x6751330966318410l,0x9819e8a37ddb795el,
        0xfecb8ef5dad100b2l } },
    /* 34 << 56 */
    { { 0x59f74a223021926al,0xb7c28a496f9b4c1cl,0xed1a733f912ad0abl,
        0x42a910af01a5659cl },
      { 0x3842c6e07bd68cabl,0x2b57fa3876d70ac8l,0x8a6707a83c53aaebl,
        0x62c1c51065b4db18l } },
    /* 35 << 56 */
    { { 0x8de2c1fbb2d09dc7l,0xc3dfed12266bd23bl,0x927d039bd5b27db6l,
        0x2fb2f0f1103243dal },
      { 0xf855a07b80be7399l,0xed9327ce1f9f27a8l,0xa0bd99c7729bdef7l,
        0x2b67125e28250d88l } },
    /* 36 << 56 */
    { { 0x784b26e88670ced7l,0xe3dfe41fc31bd3b4l,0x9e353a06bcc85cbcl,
        0x302e290960178a9dl },
      { 0x860abf11a6eac16el,0x76447000aa2b3aacl,0x46ff9d19850afdabl,
        0x35bdd6a5fdb2d4c1l } },
    /* 37 << 56 */
    { { 0xe82594b07e5c9ce9l,0x0f379e5320af346el,0x608b31e3bc65ad4al,
        0x710c6b12267c4826l },
      { 0x51c966f971954cf1l,0xb1cec7930d0aa215l,0x1f15598986bd23a8l,
        0xae2ff99cf9452e86l } },
    /* 38 << 56 */
    { { 0xd8dd953c340ceaa2l,0x263552752e2e9333l,0x15d4e5f98586f06dl,
        0xd6bf94a8f7cab546l },
      { 0x33c59a0ab76a9af0l,0x52740ab3ba095af7l,0xc444de8a24389ca0l,
        0xcc6f9863706da0cbl } },
    /* 39 << 56 */
    { { 0xb5a741a76b2515cfl,0x71c416019585c749l,0x78350d4fe683de97l,
        0x31d6152463d0b5f5l },
      { 0x7a0cc5e1fbce090bl,0xaac927edfbcb2a5bl,0xe920de4920d84c35l,
        0x8c06a0b622b4de26l } },
    /* 40 << 56 */
    { { 0xd34dd58bafe7ddf3l,0x55851fedc1e6e55bl,0xd1395616960696e7l,
        0x940304b25f22705fl },
      { 0x6f43f861b0a2a860l,0xcf1212820e7cc981l,0x121862120ab64a96l,
        0x09215b9ab789383cl } },
    /* 41 << 56 */
    { { 0x311eb30537387c09l,0xc5832fcef03ee760l,0x30358f5832f7ea19l,
        0xe01d3c3491d53551l },
      { 0x1ca5ee41da48ea80l,0x34e71e8ecf4fa4c1l,0x312abd257af1e1c7l,
        0xe3afcdeb2153f4a5l } },
    /* 42 << 56 */
    { { 0x9d5c84d700235e9al,0x0308d3f48c4c836fl,0xc0a66b0489332de5l,
        0x610dd39989e566efl },
      { 0xf8eea460d1ac1635l,0x84cbb3fb20a2c0dfl,0x40afb488e74a48c5l,
        0x29738198d326b150l } },
    /* 43 << 56 */
    { { 0x2a17747fa6d74081l,0x60ea4c0555a26214l,0x53514bb41f88c5fel,
        0xedd645677e83426cl },
      { 0xd5d6cbec96460b25l,0xa12fd0ce68dc115el,0xc5bc3ed2697840eal,
        0x969876a8a6331e31l } },
    /* 44 << 56 */
    { { 0x60c36217472ff580l,0xf42297054ad41393l,0x4bd99ef0a03b8b92l,
        0x501c7317c144f4f6l },
      { 0x159009b318464945l,0x6d5e594c74c5c6bel,0x2d587011321a3660l,
        0xd1e184b13898d022l } },
    /* 45 << 56 */
    { { 0x5ba047524c6a7e04l,0x47fa1e2b45550b65l,0x9419daf048c0a9a5l,
        0x663629537c243236l },
      { 0xcd0744b15cb12a88l,0x561b6f9a2b646188l,0x599415a566c2c0c0l,
        0xbe3f08590f83f09al } },
    /* 46 << 56 */
    { { 0x9141c5beb92041b8l,0x01ae38c726477d0dl,0xca8b71f3d12c7a94l,
        0xfab5b31f765c70dbl },
      { 0x76ae7492487443e9l,0x8595a310990d1349l,0xf8dbeda87d460a37l,
        0x7f7ad0821e45a38fl } },
    /* 47 << 56 */
    { { 0xed1d4db61059705al,0xa3dd492ae6b9c697l,0x4b92ee3a6eb38bd5l,
        0xbab2609d67cc0bb7l },
      { 0x7fc4fe896e70ee82l,0xeff2c56e13e6b7e3l,0x9b18959e34d26fcal,
        0x2517ab66889d6b45l } },
    /* 48 << 56 */
    { { 0xf167b4e0bdefdd4fl,0x69958465f366e401l,0x5aa368aba73bbec0l,
        0x121487097b240c21l },
      { 0x378c323318969006l,0xcb4d73cee1fe53d1l,0x5f50a80e130c4361l,
        0xd67f59517ef5212bl } },
    /* 49 << 56 */
    { { 0xf145e21e9e70c72el,0xb2e52e295566d2fbl,0x44eaba4a032397f5l,
        0x5e56937b7e31a7del },
      { 0x68dcf517456c61e1l,0xbc2e954aa8b0a388l,0xe3552fa760a8b755l,
        0x03442dae73ad0cdel } },
    /* 50 << 56 */
    { { 0x37ffe747ceb26210l,0x983545e8787baef9l,0x8b8c853586a3de31l,
        0xc621dbcbfacd46dbl },
      { 0x82e442e959266fbbl,0xa3514c37339d471cl,0x3a11b77162cdad96l,
        0xf0cb3b3cecf9bdf0l } },
    /* 51 << 56 */
    { { 0x3fcbdbce478e2135l,0x7547b5cfbda35342l,0xa97e81f18a677af6l,
        0xc8c2bf8328817987l },
      { 0xdf07eaaf45580985l,0xc68d1f05c93b45cbl,0x106aa2fec77b4cacl,
        0x4c1d8afc04a7ae86l } },
    /* 52 << 56 */
    { { 0xdb41c3fd9eb45ab2l,0x5b234b5bd4b22e74l,0xda253decf215958al,
        0x67e0606ea04edfa0l },
      { 0xabbbf070ef751b11l,0xf352f175f6f06dcel,0xdfc4b6af6839f6b4l,
        0x53ddf9a89959848el } },
    /* 53 << 56 */
    { { 0xda49c379c21520b0l,0x90864ff0dbd5d1b6l,0x2f055d235f49c7f7l,
        0xe51e4e6aa796b2d8l },
      { 0xc361a67f5c9dc340l,0x5ad53c37bca7c620l,0xda1d658832c756d0l,
        0xad60d9118bb67e13l } },
    /* 54 << 56 */
    { { 0xd6c47bdf0eeec8c6l,0x4a27fec1078a1821l,0x081f7415c3099524l,
        0x8effdf0b82cd8060l },
      { 0xdb70ec1c65842df8l,0x8821b358d319a901l,0x72ee56eede42b529l,
        0x5bb39592236e4286l } },
    /* 55 << 56 */
    { { 0xd1183316fd6f7140l,0xf9fadb5bbd8e81f7l,0x701d5e0c5a02d962l,
        0xfdee4dbf1b601324l },
      { 0xbed1740735d7620el,0x04e3c2c3f48c0012l,0x9ee29da73455449al,
        0x562cdef491a836c4l } },
    /* 56 << 56 */
    { { 0x8f682a5f47701097l,0x617125d8ff88d0c2l,0x948fda2457bb86ddl,
        0x348abb8f289f7286l },
      { 0xeb10eab599d94bbdl,0xd51ba28e4684d160l,0xabe0e51c30c8f41al,
        0x66588b4513254f4al } },
    /* 57 << 56 */
    { { 0x147ebf01fad097a5l,0x49883ea8610e815dl,0xe44d60ba8a11de56l,
        0xa970de6e827a7a6dl },
      { 0x2be414245e17fc19l,0xd833c65701214057l,0x1375813b363e723fl,
        0x6820bb88e6a52e9bl } },
    /* 58 << 56 */
    { { 0x7e7f6970d875d56al,0xd6a0a9ac51fbf6bfl,0x54ba8790a3083c12l,
        0xebaeb23d6ae7eb64l },
      { 0xa8685c3ab99a907al,0xf1e74550026bf40bl,0x7b73a027c802cd9el,
        0x9a8a927c4fef4635l } },
    /* 59 << 56 */
    { { 0xe1b6f60c08191224l,0xc4126ebbde4ec091l,0xe1dff4dc4ae38d84l,
        0xde3f57db4f2ef985l },
      { 0x34964337d446a1ddl,0x7bf217a0859e77f6l,0x8ff105278e1d13f5l,
        0xa304ef0374eeae27l } },
    /* 60 << 56 */
    { { 0xfc6f5e47d19dfa5al,0xdb007de37fad982bl,0x28205ad1613715f5l,
        0x251e67297889529el },
      { 0x727051841ae98e78l,0xf818537d271cac32l,0xc8a15b7eb7f410f5l,
        0xc474356f81f62393l } },
    /* 61 << 56 */
    { { 0x92dbdc5ac242316bl,0xabe060acdbf4aff5l,0x6e8c38fe909a8ec6l,
        0x43e514e56116cb94l },
      { 0x2078fa3807d784f9l,0x1161a880f4b5b357l,0x5283ce7913adea3dl,
        0x0756c3e6cc6a910bl } },
    /* 62 << 56 */
    { { 0x60bcfe01aaa79697l,0x04a73b2956391db1l,0xdd8dad47189b45a0l,
        0xbfac0dd048d5b8d9l },
      { 0x34ab3af57d3d2ec2l,0x6fa2fc2d207bd3afl,0x9ff4009266550dedl,
        0x719b3e871fd5b913l } },
    /* 63 << 56 */
    { { 0xa573a4966d17fbc7l,0x0cd1a70a73d2b24el,0x34e2c5cab2676937l,
        0xe7050b06bf669f21l },
      { 0xfbe948b61ede9046l,0xa053005197662659l,0x58cbd4edf10124c5l,
        0xde2646e4dd6c06c8l } },
    /* 64 << 56 */
    { { 0x332f81088cad38c0l,0x471b7e906bd68ae2l,0x56ac3fb20d8e27a3l,
        0xb54660db136b4b0dl },
      { 0x123a1e11a6fd8de4l,0x44dbffeaa37799efl,0x4540b977ce6ac17cl,
        0x495173a8af60acefl } },
    /* 0 << 63 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 63 */
    { { 0x9ebb284d391c2a82l,0xbcdd4863158308e8l,0x006f16ec83f1edcal,
        0xa13e2c37695dc6c8l },
      { 0x2ab756f04a057a87l,0xa8765500a6b48f98l,0x4252face68651c44l,
        0xa52b540be1765e02l } },
    /* 2 << 63 */
    { { 0x4f922fc516a0d2bbl,0x0d5cc16c1a623499l,0x9241cf3a57c62c8bl,
        0x2f5e6961fd1b667fl },
      { 0x5c15c70bf5a01797l,0x3d20b44d60956192l,0x04911b37071fdb52l,
        0xf648f9168d6f0f7bl } },
    /* 3 << 63 */
    { { 0x6dc1acafe60b7cf7l,0x25860a5084a9d869l,0x56fc6f09e7ba8ac4l,
        0x828c5bd06148d29el },
      { 0xac6b435edc55ae5fl,0xa527f56cc0117411l,0x94d5045efd24342cl,
        0x2c4c0a3570b67c0dl } },
    /* 4 << 63 */
    { { 0x027cc8b8fac61d9al,0x7d25e062e3c6fe8al,0xe08805bfe5bff503l,
        0x13271e6c6ff632f7l },
      { 0x55dca6c0232f76a5l,0x8957c32d701ef426l,0xee728bcba10a5178l,
        0x5ea60411b62c5173l } },
    /* 5 << 63 */
    { { 0xfc4e964ed0b8892bl,0x9ea176839301bb74l,0x6265c5aefcc48626l,
        0xe60cf82ebb3e9102l },
      { 0x57adf797d4df5531l,0x235b59a18deeefe2l,0x60adcf583f306eb1l,
        0x105c27533d09492dl } },
    /* 6 << 63 */
    { { 0x4090914bb5def996l,0x1cb69c83233dd1e7l,0xc1e9c1d39b3d5e76l,
        0x1f3338edfccf6012l },
      { 0xb1e95d0d2f5378a8l,0xacf4c2c72f00cd21l,0x6e984240eb5fe290l,
        0xd66c038d248088ael } },
    /* 7 << 63 */
    { { 0x804d264af94d70cfl,0xbdb802ef7314bf7el,0x8fb54de24333ed02l,
        0x740461e0285635d9l },
      { 0x4113b2c8365e9383l,0xea762c833fdef652l,0x4eec6e2e47b956c1l,
        0xa3d814be65620fa4l } },
    /* 8 << 63 */
    { { 0x9ad5462bb4d8bc50l,0x181c0b16a9195770l,0xebd4fe1c78412a68l,
        0xae0341bcc0dff48cl },
      { 0xb6bc45cf7003e866l,0xf11a6dea8a24a41bl,0x5407151ad04c24c2l,
        0x62c9d27dda5b7b68l } },
    /* 9 << 63 */
    { { 0x2e96423588cceff6l,0x8594c54f8b07ed69l,0x1578e73cc84d0d0dl,
        0x7b4e1055ff532868l },
      { 0xa348c0d5b5ec995al,0xbf4b9d5514289a54l,0x9ba155a658fbd777l,
        0x186ed7a81a84491dl } },
    /* 10 << 63 */
    { { 0xd4992b30614c0900l,0xda98d121bd00c24bl,0x7f534dc87ec4bfa1l,
        0x4a5ff67437dc34bcl },
      { 0x68c196b81d7ea1d7l,0x38cf289380a6d208l,0xfd56cd09e3cbbd6el,
        0xec72e27e4205a5b6l } },
    /* 11 << 63 */
    { { 0x15ea68f5a44f77f7l,0x7aa5f9fdb43c52bcl,0x86ff676f94f0e609l,
        0xa4cde9632e2d432bl },
      { 0x8cafa0c0eee470afl,0x84137d0e8a3f5ec8l,0xebb40411faa31231l,
        0xa239c13f6f7f7ccfl } },
    /* 12 << 63 */
    { { 0x32865719a8afd30bl,0x867983288a826dcel,0xdf04e891c4a8fbe0l,
        0xbb6b6e1bebf56ad3l },
      { 0x0a695b11471f1ff0l,0xd76c3389be15baf0l,0x018edb95be96c43el,
        0xf2beaaf490794158l } },
    /* 13 << 63 */
    { { 0x152db09ec3076a27l,0x5e82908ee416545dl,0xa2c41272356d6f2el,
        0xdc9c964231fd74e1l },
      { 0x66ceb88d519bf615l,0xe29ecd7605a2274el,0x3a0473c4bf5e2fa0l,
        0x6b6eb67164284e67l } },
    /* 14 << 63 */
    { { 0xe8b97932b88756ddl,0xed4e8652f17e3e61l,0xc2dd14993ee1c4a4l,
        0xc0aaee17597f8c0el },
      { 0x15c4edb96c168af3l,0x6563c7bfb39ae875l,0xadfadb6f20adb436l,
        0xad55e8c99a042ac0l } },
    /* 15 << 63 */
    { { 0x975a1ed8b76da1f5l,0x10dfa466a58acb94l,0x8dd7f7e3ac060282l,
        0x6813e66a572a051el },
      { 0xb4ccae1e350cb901l,0xb653d65650cb7822l,0x42484710dfab3b87l,
        0xcd7ee5379b670fd0l } },
    /* 16 << 63 */
    { { 0x0a50b12e523b8bf6l,0x8009eb5b8f910c1bl,0xf535af824a167588l,
        0x0f835f9cfb2a2abdl },
      { 0xf59b29312afceb62l,0xc797df2a169d383fl,0xeb3f5fb066ac02b0l,
        0x029d4c6fdaa2d0cal } },
    /* 17 << 63 */
    { { 0xd4059bc1afab4bc5l,0x833f5c6f56783247l,0xb53466308d2d3605l,
        0x83387891d34d8433l },
      { 0xd973b30fadd9419al,0xbcca1099afe3fce8l,0x081783150809aac6l,
        0x01b7f21a540f0f11l } },
    /* 18 << 63 */
    { { 0x65c29219909523c8l,0xa62f648fa3a1c741l,0x88598d4f60c9e55al,
        0xbce9141b0e4f347al },
      { 0x9af97d8435f9b988l,0x0210da62320475b6l,0x3c076e229191476cl,
        0x7520dbd944fc7834l } },
    /* 19 << 63 */
    { { 0x6a6b2cfec1ab1bbdl,0xef8a65bedc650938l,0x72855540805d7bc4l,
        0xda389396ed11fdfdl },
      { 0xa9d5bd3674660876l,0x11d67c54b45dff35l,0x6af7d148a4f5da94l,
        0xbb8d4c3fc0bbeb31l } },
    /* 20 << 63 */
    { { 0x87a7ebd1e0a1b12al,0x1e4ef88d770ba95fl,0x8c33345cdc2ae9cbl,
        0xcecf127601cc8403l },
      { 0x687c012e1b39b80fl,0xfd90d0ad35c33ba4l,0xa3ef5a675c9661c2l,
        0x368fc88ee017429el } },
    /* 21 << 63 */
    { { 0xd30c6761196a2fa2l,0x931b9817bd5b312el,0xba01000c72f54a31l,
        0xa203d2c866eaa541l },
      { 0xf2abdee098939db3l,0xe37d6c2c3e606c02l,0xf2921574521ff643l,
        0x2781b3c4d7e2fca3l } },
    /* 22 << 63 */
    { { 0x664300b07850ec06l,0xac5a38b97d3a10cfl,0x9233188de34ab39dl,
        0xe77057e45072cbb9l },
      { 0xbcf0c042b59e78dfl,0x4cfc91e81d97de52l,0x4661a26c3ee0ca4al,
        0x5620a4c1fb8507bcl } },
    /* 23 << 63 */
    { { 0x4b44d4aa049f842cl,0xceabc5d51540e82bl,0x306710fd15c6f156l,
        0xbe5ae52b63db1d72l },
      { 0x06f1e7e6334957f1l,0x57e388f031144a70l,0xfb69bb2fdf96447bl,
        0x0f78ebd373e38a12l } },
    /* 24 << 63 */
    { { 0xb82226052b7ce542l,0xe6d4ce997472bde1l,0x53e16ebe09d2f4dal,
        0x180ff42e53b92b2el },
      { 0xc59bcc022c34a1c6l,0x3803d6f9422c46c2l,0x18aff74f5c14a8a2l,
        0x55aebf8010a08b28l } },
    /* 25 << 63 */
    { { 0x66097d587135593fl,0x32e6eff72be570cdl,0x584e6a102a8c860dl,
        0xcd185890a2eb4163l },
      { 0x7ceae99d6d97e134l,0xd42c6b70dd8447cel,0x59ddbb4ab8c50273l,
        0x03c612df3cf34e1el } },
    /* 26 << 63 */
    { { 0x84b9ca1504b6c5a0l,0x35216f3918f0e3a3l,0x3ec2d2bcbd986c00l,
        0x8bf546d9d19228fel },
      { 0xd1c655a44cd623c3l,0x366ce718502b8e5al,0x2cfc84b4eea0bfe7l,
        0xe01d5ceecf443e8el } },
    /* 27 << 63 */
    { { 0x8ec045d9036520f8l,0xdfb3c3d192d40e98l,0x0bac4ccecc559a04l,
        0x35eccae5240ea6b1l },
      { 0x180b32dbf8a5a0acl,0x547972a5eb699700l,0xa3765801ca26bca0l,
        0x57e09d0ea647f25al } },
    /* 28 << 63 */
    { { 0xb956970e2fdd23ccl,0xb80288bc5682e971l,0xe6e6d91e9ae86ebcl,
        0x0564c83f8c9f1939l },
      { 0x551932a239560368l,0xe893752b049c28e2l,0x0b03cee5a6a158c3l,
        0xe12d656b04964263l } },
    /* 29 << 63 */
    { { 0x4b47554e63e3bc1dl,0xc719b6a245044ff7l,0x4f24d30ae48daa07l,
        0xa3f37556c8c1edc3l },
      { 0x9a47bf760700d360l,0xbb1a1824822ae4e2l,0x22e275a389f1fb4cl,
        0x72b1aa239968c5f5l } },
    /* 30 << 63 */
    { { 0xa75feacabe063f64l,0x9b392f43bce47a09l,0xd42415091ad07acal,
        0x4b0c591b8d26cd0fl },
      { 0x2d42ddfd92f1169al,0x63aeb1ac4cbf2392l,0x1de9e8770691a2afl,
        0xebe79af7d98021dal } },
    /* 31 << 63 */
    { { 0xcfdf2a4e40e50acfl,0xf0a98ad7af01d665l,0xefb640bf1831be1fl,
        0x6fe8bd2f80e9ada0l },
      { 0x94c103a16cafbc91l,0x170f87598308e08cl,0x5de2d2ab9780ff4fl,
        0x666466bc45b201f2l } },
    /* 32 << 63 */
    { { 0x58af2010f5b343bcl,0x0f2e400af2f142fel,0x3483bfdea85f4bdfl,
        0xf0b1d09303bfeaa9l },
      { 0x2ea01b95c7081603l,0xe943e4c93dba1097l,0x47be92adb438f3a6l,
        0x00bb7742e5bf6636l } },
    /* 33 << 63 */
    { { 0x136b7083824297b4l,0x9d0e55805584455fl,0xab48cedcf1c7d69el,
        0x53a9e4812a256e76l },
      { 0x0402b0e065eb2413l,0xdadbbb848fc407a7l,0xa65cd5a48d7f5492l,
        0x21d4429374bae294l } },
    /* 34 << 63 */
    { { 0x66917ce63b5f1cc4l,0x37ae52eace872e62l,0xbb087b722905f244l,
        0x120770861e6af74fl },
      { 0x4b644e491058edeal,0x827510e3b638ca1dl,0x8cf2b7046038591cl,
        0xffc8b47afe635063l } },
    /* 35 << 63 */
    { { 0x3ae220e61b4d5e63l,0xbd8647429d961b4bl,0x610c107e9bd16bedl,
        0x4270352a1127147bl },
      { 0x7d17ffe664cfc50el,0x50dee01a1e36cb42l,0x068a762235dc5f9al,
        0x9a08d536df53f62cl } },
    /* 36 << 63 */
    { { 0x4ed714576be5f7del,0xd93006f8c2263c9el,0xe073694ccacacb36l,
        0x2ff7a5b43ae118abl },
      { 0x3cce53f1cd871236l,0xf156a39dc2aa6d52l,0x9cc5f271b198d76dl,
        0xbc615b6f81383d39l } },
    /* 37 << 63 */
    { { 0xa54538e8de3eee6bl,0x58c77538ab910d91l,0x31e5bdbc58d278bdl,
        0x3cde4adfb963acael },
      { 0xb1881fd25302169cl,0x8ca60fa0a989ed8bl,0xa1999458ff96a0eel,
        0xc1141f03ac6c283dl } },
    /* 38 << 63 */
    { { 0x7677408d6dfafed3l,0x33a0165339661588l,0x3c9c15ec0b726fa0l,
        0x090cfd936c9b56dal },
      { 0xe34f4baea3c40af5l,0x3469eadbd21129f1l,0xcc51674a1e207ce8l,
        0x1e293b24c83b1ef9l } },
    /* 39 << 63 */
    { { 0x17173d131e6c0bb4l,0x1900469590776d35l,0xe7980e346de6f922l,
        0x873554cbf4dd9a22l },
      { 0x0316c627cbf18a51l,0x4d93651b3032c081l,0x207f27713946834dl,
        0x2c08d7b430cdbf80l } },
    /* 40 << 63 */
    { { 0x137a4fb486df2a61l,0xa1ed9c07ecf7b4a2l,0xb2e460e27bd042ffl,
        0xb7f5e2fa5f62f5ecl },
      { 0x7aa6ec6bcc2423b7l,0x75ce0a7fba63eea7l,0x67a45fb1f250a6e1l,
        0x93bc919ce53cdc9fl } },
    /* 41 << 63 */
    { { 0x9271f56f871942dfl,0x2372ff6f7859ad66l,0x5f4c2b9633cb1a78l,
        0xe3e291015838aa83l },
      { 0xa7ed1611e4e8110cl,0x2a2d70d5330198cel,0xbdf132e86720efe0l,
        0xe61a896266a471bfl } },
    /* 42 << 63 */
    { { 0x796d3a85825808bdl,0x51dc3cb73fd6e902l,0x643c768a916219d1l,
        0x36cd7685a2ad7d32l },
      { 0xe3db9d05b22922a4l,0x6494c87edba29660l,0xf0ac91dfbcd2ebc7l,
        0x4deb57a045107f8dl } },
    /* 43 << 63 */
    { { 0x42271f59c3d12a73l,0x5f71687ca5c2c51dl,0xcb1f50c605797bcbl,
        0x29ed0ed9d6d34eb0l },
      { 0xe5fe5b474683c2ebl,0x4956eeb597447c46l,0x5b163a4371207167l,
        0x93fa2fed0248c5efl } },
    /* 44 << 63 */
    { { 0x67930af231f63950l,0xa77797c114caa2c9l,0x526e80ee27ac7e62l,
        0xe1e6e62658b28aecl },
      { 0x636178b0b3c9fef0l,0xaf7752e06d5f90bel,0x94ecaf18eece51cfl,
        0x2864d0edca806e1fl } },
    /* 45 << 63 */
    { { 0x6de2e38397c69134l,0x5a42c316eb291293l,0xc77792196a60bae0l,
        0xa24de3466b7599d1l },
      { 0x49d374aab75d4941l,0x989005862d501ff0l,0x9f16d40eeb7974cfl,
        0x1033860bcdd8c115l } },
    /* 46 << 63 */
    { { 0xb6c69ac82094cec3l,0x9976fb88403b770cl,0x1dea026c4859590dl,
        0xb6acbb468562d1fdl },
      { 0x7cd6c46144569d85l,0xc3190a3697f0891dl,0xc6f5319548d5a17dl,
        0x7d919966d749abc8l } },
    /* 47 << 63 */
    { { 0x65104837dd1c8a20l,0x7e5410c82f683419l,0x958c3ca8be94022el,
        0x605c31976145dac2l },
      { 0x3fc0750101683d54l,0x1d7127c5595b1234l,0x10b8f87c9481277fl,
        0x677db2a8e65a1adbl } },
    /* 48 << 63 */
    { { 0xec2fccaaddce3345l,0x2a6811b7012a4350l,0x96760ff1ac598bdcl,
        0x054d652ad1bf4128l },
      { 0x0a1151d492a21005l,0xad7f397133110fdfl,0x8c95928c1960100fl,
        0x6c91c8257bf03362l } },
    /* 49 << 63 */
    { { 0xc8c8b2a2ce309f06l,0xfdb27b59ca27204bl,0xd223eaa50848e32el,
        0xb93e4b2ee7bfaf1el },
      { 0xc5308ae644aa3dedl,0x317a666ac015d573l,0xc888ce231a979707l,
        0xf141c1e60d5c4958l } },
    /* 50 << 63 */
    { { 0xb53b7de561906373l,0x858dbadeeb999595l,0x8cbb47b2a59e5c36l,
        0x660318b3dcf4e842l },
      { 0xbd161ccd12ba4b7al,0xf399daabf8c8282al,0x1587633aeeb2130dl,
        0xa465311ada38dd7dl } },
    /* 51 << 63 */
    { { 0x5f75eec864d3779bl,0x3c5d0476ad64c171l,0x874103712a914428l,
        0x8096a89190e2fc29l },
      { 0xd3d2ae9d23b3ebc2l,0x90bdd6dba580cfd6l,0x52dbb7f3c5b01f6cl,
        0xe68eded4e102a2dcl } },
    /* 52 << 63 */
    { { 0x17785b7799eb6df0l,0x26c3cc517386b779l,0x345ed9886417a48el,
        0xe990b4e407d6ef31l },
      { 0x0f456b7e2586abbal,0x239ca6a559c96e9al,0xe327459ce2eb4206l,
        0x3a4c3313a002b90al } },
    /* 53 << 63 */
    { { 0x2a114806f6a3f6fbl,0xad5cad2f85c251ddl,0x92c1f613f5a784d3l,
        0xec7bfacf349766d5l },
      { 0x04b3cd333e23cb3bl,0x3979fe84c5a64b2dl,0x192e27207e589106l,
        0xa60c43d1a15b527fl } },
    /* 54 << 63 */
    { { 0x2dae9082be7cf3a6l,0xcc86ba92bc967274l,0xf28a2ce8aea0a8a9l,
        0x404ca6d96ee988b3l },
      { 0xfd7e9c5d005921b8l,0xf56297f144e79bf9l,0xa163b4600d75ddc2l,
        0x30b23616a1f2be87l } },
    /* 55 << 63 */
    { { 0x4b070d21bfe50e2bl,0x7ef8cfd0e1bfede1l,0xadba00112aac4ae0l,
        0x2a3e7d01b9ebd033l },
      { 0x995277ece38d9d1cl,0xb500249e9c5d2de3l,0x8912b820f13ca8c9l,
        0xc8798114877793afl } },
    /* 56 << 63 */
    { { 0x19e6125dec3f1decl,0x07b1f040911178dal,0xd93ededa904a6738l,
        0x55187a5a0bebedcdl },
      { 0xf7d04722eb329d41l,0xf449099ef170b391l,0xfd317a69ca99f828l,
        0x50c3db2b34a4976dl } },
    /* 57 << 63 */
    { { 0xe9ba77843757b392l,0x326caefdaa3ca05al,0x78e5293bf1e593d4l,
        0x7842a9370d98fd13l },
      { 0xe694bf965f96b10dl,0x373a9df606a8cd05l,0x997d1e51e8f0c7fcl,
        0x1d01979063fd972el } },
    /* 58 << 63 */
    { { 0x0064d8585499fb32l,0x7b67bad977a8aeb7l,0x1d3eb9772d08eec5l,
        0x5fc047a6cbabae1dl },
      { 0x0577d159e54a64bbl,0x8862201bc43497e4l,0xad6b4e282ce0608dl,
        0x8b687b7d0b167aacl } },
    /* 59 << 63 */
    { { 0x6ed4d3678b2ecfa9l,0x24dfe62da90c3c38l,0xa1862e103fe5c42bl,
        0x1ca73dcad5732a9fl },
      { 0x35f038b776bb87adl,0x674976abf242b81fl,0x4f2bde7eb0fd90cdl,
        0x6efc172ea7fdf092l } },
    /* 60 << 63 */
    { { 0x3806b69b92222f1fl,0x5a2459ca6cf7ae70l,0x6789f69ca85217eel,
        0x5f232b5ee3dc85acl },
      { 0x660e3ec548e9e516l,0x124b4e473197eb31l,0x10a0cb13aafcca23l,
        0x7bd63ba48213224fl } },
    /* 61 << 63 */
    { { 0xaffad7cc290a7f4fl,0x6b409c9e0286b461l,0x58ab809fffa407afl,
        0xc3122eedc68ac073l },
      { 0x17bf9e504ef24d7el,0x5d9297943e2a5811l,0x519bc86702902e01l,
        0x76bba5da39c8a851l } },
    /* 62 << 63 */
    { { 0xe9f9669cda94951el,0x4b6af58d66b8d418l,0xfa32107417d426a4l,
        0xc78e66a99dde6027l },
      { 0x0516c0834a53b964l,0xfc659d38ff602330l,0x0ab55e5c58c5c897l,
        0x985099b2838bc5dfl } },
    /* 63 << 63 */
    { { 0x061d9efcc52fc238l,0x712b27286ac1da3fl,0xfb6581499283fe08l,
        0x4954ac94b8aaa2f7l },
      { 0x85c0ada47fb2e74fl,0xee8ba98eb89926b0l,0xe4f9d37d23d1af5bl,
        0x14ccdbf9ba9b015el } },
    /* 64 << 63 */
    { { 0xb674481b7bfe7178l,0x4e1debae65405868l,0x061b2821c48c867dl,
        0x69c15b35513b30eal },
      { 0x3b4a166636871088l,0xe5e29f5d1220b1ffl,0x4b82bb35233d9f4dl,
        0x4e07633318cdc675l } },
    /* 0 << 70 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 70 */
    { { 0x0d53f5c7a3e6fcedl,0xe8cbbdd5f45fbdebl,0xf85c01df13339a70l,
        0x0ff71880142ceb81l },
      { 0x4c4e8774bd70437al,0x5fb32891ba0bda6al,0x1cdbebd2f18bd26el,
        0x2f9526f103a9d522l } },
    /* 2 << 70 */
    { { 0x40ce305192c4d684l,0x8b04d7257612efcdl,0xb9dcda366f9cae20l,
        0x0edc4d24f058856cl },
      { 0x64f2e6bf85427900l,0x3de81295dc09dfeal,0xd41b4487379bf26cl,
        0x50b62c6d6df135a9l } },
    /* 3 << 70 */
    { { 0xd4f8e3b4c72dfe67l,0xc416b0f690e19fdfl,0x18b9098d4c13bd35l,
        0xac11118a15b8cb9el },
      { 0xf598a318f0062841l,0xbfe0602f89f356f4l,0x7ae3637e30177a0cl,
        0x3409774761136537l } },
    /* 4 << 70 */
    { { 0x0db2fb5ed005832al,0x5f5efd3b91042e4fl,0x8c4ffdc6ed70f8cal,
        0xe4645d0bb52da9ccl },
      { 0x9596f58bc9001d1fl,0x52c8f0bc4e117205l,0xfd4aa0d2e398a084l,
        0x815bfe3a104f49del } },
    /* 5 << 70 */
    { { 0x97e5443f23885e5fl,0xf72f8f99e8433aabl,0xbd00b154e4d4e604l,
        0xd0b35e6ae5e173ffl },
      { 0x57b2a0489164722dl,0x3e3c665b88761ec8l,0x6bdd13973da83832l,
        0x3c8b1a1e73dafe3bl } },
    /* 6 << 70 */
    { { 0x4497ace654317cacl,0xbe600ab9521771b3l,0xb42e409eb0dfe8b8l,
        0x386a67d73942310fl },
      { 0x25548d8d4431cc28l,0xa7cff142985dc524l,0x4d60f5a193c4be32l,
        0x83ebd5c8d071c6e1l } },
    /* 7 << 70 */
    { { 0xba3a80a7b1fd2b0bl,0x9b3ad3965bec33e8l,0xb3868d6179743fb3l,
        0xcfd169fcfdb462fal },
      { 0xd3b499d79ce0a6afl,0x55dc1cf1e42d3ff8l,0x04fb9e6cc6c3e1b2l,
        0x47e6961d6f69a474l } },
    /* 8 << 70 */
    { { 0x54eb3acce548b37bl,0xb38e754284d40549l,0x8c3daa517b341b4fl,
        0x2f6928ec690bf7fal },
      { 0x0496b32386ce6c41l,0x01be1c5510adadcdl,0xc04e67e74bb5faf9l,
        0x3cbaf678e15c9985l } },
    /* 9 << 70 */
    { { 0x8cd1214550ca4247l,0xba1aa47ae7dd30aal,0x2f81ddf1e58fee24l,
        0x03452936eec9b0e8l },
      { 0x8bdc3b81243aea96l,0x9a2919af15c3d0e5l,0x9ea640ec10948361l,
        0x5ac86d5b6e0bcccfl } },
    /* 10 << 70 */
    { { 0xf892d918c36cf440l,0xaed3e837c939719cl,0xb07b08d2c0218b64l,
        0x6f1bcbbace9790ddl },
      { 0x4a84d6ed60919b8el,0xd89007918ac1f9ebl,0xf84941aa0dd5daefl,
        0xb22fe40a67fd62c5l } },
    /* 11 << 70 */
    { { 0x97e15ba2157f2db3l,0xbda2fc8f8e28ca9cl,0x5d050da437b9f454l,
        0x3d57eb572379d72el },
      { 0xe9b5eba2fb5ee997l,0x01648ca2e11538cal,0x32bb76f6f6327974l,
        0x338f14b8ff3f4bb7l } },
    /* 12 << 70 */
    { { 0x524d226ad7ab9a2dl,0x9c00090d7dfae958l,0x0ba5f5398751d8c2l,
        0x8afcbcdd3ab8262dl },
      { 0x57392729e99d043bl,0xef51263baebc943al,0x9feace9320862935l,
        0x639efc03b06c817bl } },
    /* 13 << 70 */
    { { 0x1fe054b366b4be7al,0x3f25a9de84a37a1el,0xf39ef1ad78d75cd9l,
        0xd7b58f495062c1b5l },
      { 0x6f74f9a9ff563436l,0xf718ff29e8af51e7l,0x5234d31315e97fecl,
        0xb6a8e2b1292f1c0al } },
    /* 14 << 70 */
    { { 0xa7f53aa8327720c1l,0x956ca322ba092cc8l,0x8f03d64a28746c4dl,
        0x51fe178266d0d392l },
      { 0xd19b34db3c832c80l,0x60dccc5c6da2e3b4l,0x245dd62e0a104cccl,
        0xa7ab1de1620b21fdl } },
    /* 15 << 70 */
    { { 0xb293ae0b3893d123l,0xf7b75783b15ee71cl,0x5aa3c61442a9468bl,
        0xd686123cdb15d744l },
      { 0x8c616891a7ab4116l,0x6fcd72c8a4e6a459l,0xac21911077e5fad7l,
        0xfb6a20e7704fa46bl } },
    /* 16 << 70 */
    { { 0xe839be7d341d81dcl,0xcddb688932148379l,0xda6211a1f7026eadl,
        0xf3b2575ff4d1cc5el },
      { 0x40cfc8f6a7a73ae6l,0x83879a5e61d5b483l,0xc5acb1ed41a50ebcl,
        0x59a60cc83c07d8fal } },
    /* 17 << 70 */
    { { 0x1b73bdceb1876262l,0x2b0d79f012af4ee9l,0x8bcf3b0bd46e1d07l,
        0x17d6af9de45d152fl },
      { 0x735204616d736451l,0x43cbbd9756b0bf5al,0xb0833a5bd5999b9dl,
        0x702614f0eb72e398l } },
    /* 18 << 70 */
    { { 0x0aadf01a59c3e9f8l,0x40200e77ce6b3d16l,0xda22bdd3deddafadl,
        0x76dedaf4310d72e1l },
      { 0x49ef807c4bc2e88fl,0x6ba81291146dd5a5l,0xa1a4077a7d8d59e9l,
        0x87b6a2e7802db349l } },
    /* 19 << 70 */
    { { 0xd56799971b4e598el,0xf499ef1f06fe4b1dl,0x3978d3aefcb267c5l,
        0xb582b557235786d0l },
      { 0x32b3b2ca1715cb07l,0x4c3de6a28480241dl,0x63b5ffedcb571ecdl,
        0xeaf53900ed2fe9a9l } },
    /* 20 << 70 */
    { { 0xdec98d4ac3b81990l,0x1cb837229e0cc8fel,0xfe0b0491d2b427b9l,
        0x0f2386ace983a66cl },
      { 0x930c4d1eb3291213l,0xa2f82b2e59a62ae4l,0x77233853f93e89e3l,
        0x7f8063ac11777c7fl } },
    /* 21 << 70 */
    { { 0xff0eb56759ad2877l,0x6f4546429865c754l,0xe6fe701a236e9a84l,
        0xc586ef1606e40fc3l },
      { 0x3f62b6e024bafad9l,0xc8b42bd264da906al,0xc98e1eb4da3276a0l,
        0x30d0e5fc06cbf852l } },
    /* 22 << 70 */
    { { 0x1b6b2ae1e8b4dfd4l,0xd754d5c78301cbacl,0x66097629112a39acl,
        0xf86b599993ba4ab9l },
      { 0x26c9dea799f9d581l,0x0473b1a8c2fafeaal,0x1469af553b2505a5l,
        0x227d16d7d6a43323l } },
    /* 23 << 70 */
    { { 0x3316f73cad3d97f9l,0x52bf3bb51f137455l,0x953eafeb09954e7cl,
        0xa721dfeddd732411l },
      { 0xb4929821141d4579l,0x3411321caa3bd435l,0xafb355aa17fa6015l,
        0xb4e7ef4a18e42f0el } },
    /* 24 << 70 */
    { { 0x604ac97c59371000l,0xe1c48c707f759c18l,0x3f62ecc5a5db6b65l,
        0x0a78b17338a21495l },
      { 0x6be1819dbcc8ad94l,0x70dc04f6d89c3400l,0x462557b4a6b4840al,
        0x544c6ade60bd21c0l } },
    /* 25 << 70 */
    { { 0x6a00f24e907a544bl,0xa7520dcb313da210l,0xfe939b7511e4994bl,
        0x918b6ba6bc275d70l },
      { 0xd3e5e0fc644be892l,0x707a9816fdaf6c42l,0x60145567f15c13fel,
        0x4818ebaae130a54al } },
    /* 26 << 70 */
    { { 0x28aad3ad58d2f767l,0xdc5267fdd7e7c773l,0x4919cc88c3afcc98l,
        0xaa2e6ab02db8cd4bl },
      { 0xd46fec04d0c63eaal,0xa1cb92c519ffa832l,0x678dd178e43a631fl,
        0xfb5ae1cd3dc788b3l } },
    /* 27 << 70 */
    { { 0x68b4fb906e77de04l,0x7992bcf0f06dbb97l,0x896e6a13c417c01dl,
        0x8d96332cb956be01l },
      { 0x902fc93a413aa2b9l,0x99a4d915fc98c8a5l,0x52c29407565f1137l,
        0x4072690f21e4f281l } },
    /* 28 << 70 */
    { { 0x36e607cf02ff6072l,0xa47d2ca98ad98cdcl,0xbf471d1ef5f56609l,
        0xbcf86623f264ada0l },
      { 0xb70c0687aa9e5cb6l,0xc98124f217401c6cl,0x8189635fd4a61435l,
        0xd28fb8afa9d98ea6l } },
    /* 29 << 70 */
    { { 0xb9a67c2a40c251f8l,0x88cd5d87a2da44bel,0x437deb96e09b5423l,
        0x150467db64287dc1l },
      { 0xe161debbcdabb839l,0xa79e9742f1839a3el,0xbb8dd3c2652d202bl,
        0x7b3e67f7e9f97d96l } },
    /* 30 << 70 */
    { { 0x5aa5d78fb1cb6ac9l,0xffa13e8eca1d0d45l,0x369295dd2ba5bf95l,
        0xd68bd1f839aff05el },
      { 0xaf0d86f926d783f2l,0x543a59b3fc3aafc1l,0x3fcf81d27b7da97cl,
        0xc990a056d25dee46l } },
    /* 31 << 70 */
    { { 0x3e6775b8519cce2cl,0xfc9af71fae13d863l,0x774a4a6f47c1605cl,
        0x46ba42452fd205e8l },
      { 0xa06feea4d3fd524dl,0x1e7246416de1acc2l,0xf53816f1334e2b42l,
        0x49e5918e922f0024l } },
    /* 32 << 70 */
    { { 0x439530b665c7322dl,0xcf12cc01b3c1b3fbl,0xc70b01860172f685l,
        0xb915ee221b58391dl },
      { 0x9afdf03ba317db24l,0x87dec65917b8ffc4l,0x7f46597be4d3d050l,
        0x80a1c1ed006500e7l } },
    /* 33 << 70 */
    { { 0x84902a9678bf030el,0xfb5e9c9a50560148l,0x6dae0a9263362426l,
        0xdcaeecf4a9e30c40l },
      { 0xc0d887bb518d0c6bl,0x99181152cb985b9dl,0xad186898ef7bc381l,
        0x18168ffb9ee46201l } },
    /* 34 << 70 */
    { { 0x9a04cdaa2502753cl,0xbb279e2651407c41l,0xeacb03aaf23564e5l,
        0x1833658271e61016l },
      { 0x8684b8c4eb809877l,0xb336e18dea0e672el,0xefb601f034ee5867l,
        0x2733edbe1341cfd1l } },
    /* 35 << 70 */
    { { 0xb15e809a26025c3cl,0xe6e981a69350df88l,0x923762378502fd8el,
        0x4791f2160c12be9bl },
      { 0xb725678925f02425l,0xec8631947a974443l,0x7c0ce882fb41cc52l,
        0xc266ff7ef25c07f2l } },
    /* 36 << 70 */
    { { 0x3d4da8c3017025f3l,0xefcf628cfb9579b4l,0x5c4d00161f3716ecl,
        0x9c27ebc46801116el },
      { 0x5eba0ea11da1767el,0xfe15145247004c57l,0x3ace6df68c2373b7l,
        0x75c3dffe5dbc37acl } },
    /* 37 << 70 */
    { { 0x3dc32a73ddc925fcl,0xb679c8412f65ee0bl,0x715a3295451cbfebl,
        0xd9889768f76e9a29l },
      { 0xec20ce7fb28ad247l,0xe99146c400894d79l,0x71457d7c9f5e3ea7l,
        0x097b266238030031l } },
    /* 38 << 70 */
    { { 0xdb7f6ae6cf9f82a8l,0x319decb9438f473al,0xa63ab386283856c3l,
        0x13e3172fb06a361bl },
      { 0x2959f8dc7d5a006cl,0x2dbc27c675fba752l,0xc1227ab287c22c9el,
        0x06f61f7571a268b2l } },
    /* 39 << 70 */
    { { 0x1b6bb97104779ce2l,0xaca838120aadcb1dl,0x297ae0bcaeaab2d5l,
        0xa5c14ee75bfb9f13l },
      { 0xaa00c583f17a62c7l,0x39eb962c173759f6l,0x1eeba1d486c9a88fl,
        0x0ab6c37adf016c5el } },
    /* 40 << 70 */
    { { 0xa2a147dba28a0749l,0x246c20d6ee519165l,0x5068d1b1d3810715l,
        0xb1e7018c748160b9l },
      { 0x03f5b1faf380ff62l,0xef7fb1ddf3cb2c1el,0xeab539a8fc91a7dal,
        0x83ddb707f3f9b561l } },
    /* 41 << 70 */
    { { 0xc550e211fe7df7a4l,0xa7cd07f2063f6f40l,0xb0de36352976879cl,
        0xb5f83f85e55741dal },
      { 0x4ea9d25ef3d8ac3dl,0x6fe2066f62819f02l,0x4ab2b9c2cef4a564l,
        0x1e155d965ffa2de3l } },
    /* 42 << 70 */
    { { 0x0eb0a19bc3a72d00l,0x4037665b8513c31bl,0x2fb2b6bf04c64637l,
        0x45c34d6e08cdc639l },
      { 0x56f1e10ff01fd796l,0x4dfb8101fe3667b8l,0xe0eda2539021d0c0l,
        0x7a94e9ff8a06c6abl } },
    /* 43 << 70 */
    { { 0x2d3bb0d9bb9aa882l,0xea20e4e5ec05fd10l,0xed7eeb5f1a1ca64el,
        0x2fa6b43cc6327cbdl },
      { 0xb577e3cf3aa91121l,0x8c6bd5ea3a34079bl,0xd7e5ba3960e02fc0l,
        0xf16dd2c390141bf8l } },
    /* 44 << 70 */
    { { 0xb57276d980101b98l,0x760883fdb82f0f66l,0x89d7de754bc3eff3l,
        0x03b606435dc2ab40l },
      { 0xcd6e53dfe05beeacl,0xf2f1e862bc3325cdl,0xdd0f7921774f03c3l,
        0x97ca72214552cc1bl } },
    /* 45 << 70 */
    { { 0x5a0d6afe1cd19f72l,0xa20915dcf183fbebl,0x9fda4b40832c403cl,
        0x32738eddbe425442l },
      { 0x469a1df6b5eccf1al,0x4b5aff4228bbe1f0l,0x31359d7f570dfc93l,
        0xa18be235f0088628l } },
    /* 46 << 70 */
    { { 0xa5b30fbab00ed3a9l,0x34c6137473cdf8bel,0x2c5c5f46abc56797l,
        0x5cecf93db82a8ae2l },
      { 0x7d3dbe41a968fbf0l,0xd23d45831a5c7f3dl,0xf28f69a0c087a9c7l,
        0xc2d75471474471cal } },
    /* 47 << 70 */
    { { 0x36ec9f4a4eb732ecl,0x6c943bbdb1ca6bedl,0xd64535e1f2457892l,
        0x8b84a8eaf7e2ac06l },
      { 0xe0936cd32499dd5fl,0x12053d7e0ed04e57l,0x4bdd0076e4305d9dl,
        0x34a527b91f67f0a2l } },
    /* 48 << 70 */
    { { 0xe79a4af09cec46eal,0xb15347a1658b9bc7l,0x6bd2796f35af2f75l,
        0xac9579904051c435l },
      { 0x2669dda3c33a655dl,0x5d503c2e88514aa3l,0xdfa113373753dd41l,
        0x3f0546730b754f78l } },
    /* 49 << 70 */
    { { 0xbf185677496125bdl,0xfb0023c83775006cl,0xfa0f072f3a037899l,
        0x4222b6eb0e4aea57l },
      { 0x3dde5e767866d25al,0xb6eb04f84837aa6fl,0x5315591a2cf1cdb8l,
        0x6dfb4f412d4e683cl } },
    /* 50 << 70 */
    { { 0x7e923ea448ee1f3al,0x9604d9f705a2afd5l,0xbe1d4a3340ea4948l,
        0x5b45f1f4b44cbd2fl },
      { 0x5faf83764acc757el,0xa7cf9ab863d68ff7l,0x8ad62f69df0e404bl,
        0xd65f33c212bdafdfl } },
    /* 51 << 70 */
    { { 0xc365de15a377b14el,0x6bf5463b8e39f60cl,0x62030d2d2ce68148l,
        0xd95867efe6f843a8l },
      { 0xd39a0244ef5ab017l,0x0bd2d8c14ab55d12l,0xc9503db341639169l,
        0x2d4e25b0f7660c8al } },
    /* 52 << 70 */
    { { 0x760cb3b5e224c5d7l,0xfa3baf8c68616919l,0x9fbca1138d142552l,
        0x1ab18bf17669ebf5l },
      { 0x55e6f53e9bdf25ddl,0x04cc0bf3cb6cd154l,0x595bef4995e89080l,
        0xfe9459a8104a9ac1l } },
    /* 53 << 70 */
    { { 0xad2d89cacce9bb32l,0xddea65e1f7de8285l,0x62ed8c35b351bd4bl,
        0x4150ff360c0e19a7l },
      { 0x86e3c801345f4e47l,0x3bf21f71203a266cl,0x7ae110d4855b1f13l,
        0x5d6aaf6a07262517l } },
    /* 54 << 70 */
    { { 0x1e0f12e1813d28f1l,0x6000e11d7ad7a523l,0xc7d8deefc744a17bl,
        0x1e990b4814c05a00l },
      { 0x68fddaee93e976d5l,0x696241d146610d63l,0xb204e7c3893dda88l,
        0x8bccfa656a3a6946l } },
    /* 55 << 70 */
    { { 0xb59425b4c5cd1411l,0x701b4042ff3658b1l,0xe3e56bca4784cf93l,
        0x27de5f158fe68d60l },
      { 0x4ab9cfcef8d53f19l,0xddb10311a40a730dl,0x6fa73cd14eee0a8al,
        0xfd5487485249719dl } },
    /* 56 << 70 */
    { { 0x49d66316a8123ef0l,0x73c32db4e7f95438l,0x2e2ed2090d9e7854l,
        0xf98a93299d9f0507l },
      { 0xc5d33cf60c6aa20al,0x9a32ba1475279bb2l,0x7e3202cb774a7307l,
        0x64ed4bc4e8c42dbdl } },
    /* 57 << 70 */
    { { 0xc20f1a06d4caed0dl,0xb8021407171d22b3l,0xd426ca04d13268d7l,
        0x9237700725f4d126l },
      { 0x4204cbc371f21a85l,0x18461b7af82369bal,0xc0c07d313fc858f9l,
        0x5deb5a50e2bab569l } },
    /* 58 << 70 */
    { { 0xd5959d46d5eea89el,0xfdff842408437f4bl,0xf21071e43cfe254fl,
        0x7241769695468321l },
      { 0x5d8288b9102cae3el,0x2d143e3df1965dffl,0x00c9a376a078d847l,
        0x6fc0da3126028731l } },
    /* 59 << 70 */
    { { 0xa2baeadfe45083a2l,0x66bc72185e5b4bcdl,0x2c826442d04b8e7fl,
        0xc19f54516c4b586bl },
      { 0x60182c495b7eeed5l,0xd9954ecd7aa9dfa1l,0xa403a8ecc73884adl,
        0x7fb17de29bb39041l } },
    /* 60 << 70 */
    { { 0x694b64c5abb020e8l,0x3d18c18419c4eec7l,0x9c4673ef1c4793e5l,
        0xc7b8aeb5056092e6l },
      { 0x3aa1ca43f0f8c16bl,0x224ed5ecd679b2f6l,0x0d56eeaf55a205c9l,
        0xbfe115ba4b8e028bl } },
    /* 61 << 70 */
    { { 0x97e608493927f4fel,0xf91fbf94759aa7c5l,0x985af7696be90a51l,
        0xc1277b7878ccb823l },
      { 0x395b656ee7a75952l,0x00df7de0928da5f5l,0x09c231754ca4454fl,
        0x4ec971f47aa2d3c1l } },
    /* 62 << 70 */
    { { 0x45c3c507e75d9cccl,0x63b7be8a3dc90306l,0x37e09c665db44bdcl,
        0x50d60da16841c6a2l },
      { 0x6f9b65ee08df1b12l,0x387348797ff089dfl,0x9c331a663fe8013dl,
        0x017f5de95f42fcc8l } },
    /* 63 << 70 */
    { { 0x43077866e8e57567l,0xc9f781cef9fcdb18l,0x38131dda9b12e174l,
        0x25d84aa38a03752al },
      { 0x45e09e094d0c0ce2l,0x1564008b92bebba5l,0xf7e8ad31a87284c7l,
        0xb7c4b46c97e7bbaal } },
    /* 64 << 70 */
    { { 0x3e22a7b397acf4ecl,0x0426c4005ea8b640l,0x5e3295a64e969285l,
        0x22aabc59a6a45670l },
      { 0xb929714c5f5942bcl,0x9a6168bdfa3182edl,0x2216a665104152bal,
        0x46908d03b6926368l } },
    /* 0 << 77 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 77 */
    { { 0xa9f5d8745a1251fbl,0x967747a8c72725c7l,0x195c33e531ffe89el,
        0x609d210fe964935el },
      { 0xcafd6ca82fe12227l,0xaf9b5b960426469dl,0x2e9ee04c5693183cl,
        0x1084a333c8146fefl } },
    /* 2 << 77 */
    { { 0x96649933aed1d1f7l,0x566eaff350563090l,0x345057f0ad2e39cfl,
        0x148ff65b1f832124l },
      { 0x042e89d4cf94cf0dl,0x319bec84520c58b3l,0x2a2676265361aa0dl,
        0xc86fa3028fbc87adl } },
    /* 3 << 77 */
    { { 0xfc83d2ab5c8b06d5l,0xb1a785a2fe4eac46l,0xb99315bc846f7779l,
        0xcf31d816ef9ea505l },
      { 0x2391fe6a15d7dc85l,0x2f132b04b4016b33l,0x29547fe3181cb4c7l,
        0xdb66d8a6650155a1l } },
    /* 4 << 77 */
    { { 0x6b66d7e1adc1696fl,0x98ebe5930acd72d0l,0x65f24550cc1b7435l,
        0xce231393b4b9a5ecl },
      { 0x234a22d4db067df9l,0x98dda095caff9b00l,0x1bbc75a06100c9c1l,
        0x1560a9c8939cf695l } },
    /* 5 << 77 */
    { { 0xcf006d3e99e0925fl,0x2dd74a966322375al,0xc58b446ab56af5bal,
        0x50292683e0b9b4f1l },
      { 0xe2c34cb41aeaffa3l,0x8b17203f9b9587c1l,0x6d559207ead1350cl,
        0x2b66a215fb7f9604l } },
    /* 6 << 77 */
    { { 0x0850325efe51bf74l,0x9c4f579e5e460094l,0x5c87b92a76da2f25l,
        0x889de4e06febef33l },
      { 0x6900ec06646083cel,0xbe2a0335bfe12773l,0xadd1da35c5344110l,
        0x757568b7b802cd20l } },
    /* 7 << 77 */
    { { 0x7555977900f7e6c8l,0x38e8b94f0facd2f0l,0xfea1f3af03fde375l,
        0x5e11a1d875881dfcl },
      { 0xb3a6b02ec1e2f2efl,0x193d2bbbc605a6c5l,0x325ffeee339a0b2dl,
        0x27b6a7249e0c8846l } },
    /* 8 << 77 */
    { { 0xe4050f1cf1c367cal,0x9bc85a9bc90fbc7dl,0xa373c4a2e1a11032l,
        0xb64232b7ad0393a9l },
      { 0xf5577eb0167dad29l,0x1604f30194b78ab2l,0x0baa94afe829348bl,
        0x77fbd8dd41654342l } },
    /* 9 << 77 */
    { { 0xdab50ea5b964e39al,0xd4c29e3cd0d3c76el,0x80dae67c56d11964l,
        0x7307a8bfe5ffcc2fl },
      { 0x65bbc1aa91708c3bl,0xa151e62c28bf0eebl,0x6cb533816fa34db7l,
        0x5139e05ca29403a8l } },
    /* 10 << 77 */
    { { 0x6ff651b494a7cd2el,0x5671ffd10699336cl,0x6f5fd2cc979a896al,
        0x11e893a8d8148cefl },
      { 0x988906a165cf7b10l,0x81b67178c50d8485l,0x7c0deb358a35b3del,
        0x423ac855c1d29799l } },
    /* 11 << 77 */
    { { 0xaf580d87dac50b74l,0x28b2b89f5869734cl,0x99a3b936874e28fbl,
        0xbb2c919025f3f73al },
      { 0x199f691884a9d5b7l,0x7ebe23257e770374l,0xf442e1070738efe2l,
        0xcf9f3f56cf9082d2l } },
    /* 12 << 77 */
    { { 0x719f69e109618708l,0xcc9e8364c183f9b1l,0xec203a95366a21afl,
        0x6aec5d6d068b141fl },
      { 0xee2df78a994f04e9l,0xb39ccae8271245b0l,0xb875a4a997e43f4fl,
        0x507dfe11db2cea98l } },
    /* 13 << 77 */
    { { 0x4fbf81cb489b03e9l,0xdb86ec5b6ec414fal,0xfad444f9f51b3ae5l,
        0xca7d33d61914e3fel },
      { 0xa9c32f5c0ae6c4d0l,0xa9ca1d1e73969568l,0x98043c311aa7467el,
        0xe832e75ce21b5ac6l } },
    /* 14 << 77 */
    { { 0x314b7aea5232123dl,0x08307c8c65ae86dbl,0x06e7165caa4668edl,
        0xb170458bb4d3ec39l },
      { 0x4d2e3ec6c19bb986l,0xc5f34846ae0304edl,0x917695a06c9f9722l,
        0x6c7f73174cab1c0al } },
    /* 15 << 77 */
    { { 0x6295940e9d6d2e8bl,0xd318b8c1549f7c97l,0x2245320497713885l,
        0x468d834ba8a440fel },
      { 0xd81fe5b2bfba796el,0x152364db6d71f116l,0xbb8c7c59b5b66e53l,
        0x0b12c61b2641a192l } },
    /* 16 << 77 */
    { { 0x31f14802fcf0a7fdl,0x42fd07895488b01el,0x71d78d6d9952b498l,
        0x8eb572d907ac5201l },
      { 0xe0a2a44c4d194a88l,0xd2b63fd9ba017e66l,0x78efc6c8f888aefcl,
        0xb76f6bda4a881a11l } },
    /* 17 << 77 */
    { { 0x187f314bb46c2397l,0x004cf5665ded2819l,0xa9ea570438764d34l,
        0xbba4521778084709l },
      { 0x064745711171121el,0xad7b7eb1e7c9b671l,0xdacfbc40730f7507l,
        0x178cd8c6c7ad7bd1l } },
    /* 18 << 77 */
    { { 0xbf0be101b2a67238l,0x3556d367af9c14f2l,0x104b7831a5662075l,
        0x58ca59bb79d9e60al },
      { 0x4bc45392a569a73bl,0x517a52e85698f6c9l,0x85643da5aeadd755l,
        0x1aed0cd52a581b84l } },
    /* 19 << 77 */
    { { 0xb9b4ff8480af1372l,0x244c3113f1ba5d1fl,0x2a5dacbef5f98d31l,
        0x2c3323e84375bc2al },
      { 0x17a3ab4a5594b1ddl,0xa1928bfbceb4797el,0xe83af245e4886a19l,
        0x8979d54672b5a74al } },
    /* 20 << 77 */
    { { 0xa0f726bc19f9e967l,0xd9d03152e8fbbf4el,0xcfd6f51db7707d40l,
        0x633084d963f6e6e0l },
      { 0xedcd9cdc55667eafl,0x73b7f92b2e44d56fl,0xfb2e39b64e962b14l,
        0x7d408f6ef671fcbfl } },
    /* 21 << 77 */
    { { 0xcc634ddc164a89bbl,0x74a42bb23ef3bd05l,0x1280dbb2428decbbl,
        0x6103f6bb402c8596l },
      { 0xfa2bf581355a5752l,0x562f96a800946674l,0x4e4ca16d6da0223bl,
        0xfe47819f28d3aa25l } },
    /* 22 << 77 */
    { { 0x9eea3075f8dfcf8al,0xa284f0aa95669825l,0xb3fca250867d3fd8l,
        0x20757b5f269d691el },
      { 0xf2c2402093b8a5del,0xd3f93359ebc06da6l,0x1178293eb2739c33l,
        0xd2a3e770bcd686e5l } },
    /* 23 << 77 */
    { { 0xa76f49f4cd941534l,0x0d37406be3c71c0el,0x172d93973b97f7e3l,
        0xec17e239bd7fd0del },
      { 0xe32905516f496ba2l,0x6a69317236ad50e7l,0xc4e539a283e7eff5l,
        0x752737e718e1b4cfl } },
    /* 24 << 77 */
    { { 0xa2f7932c68af43eel,0x5502468e703d00bdl,0xe5dc978f2fb061f5l,
        0xc9a1904a28c815adl },
      { 0xd3af538d470c56a4l,0x159abc5f193d8cedl,0x2a37245f20108ef3l,
        0xfa17081e223f7178l } },
    /* 25 << 77 */
    { { 0x27b0fb2b10c8c0f5l,0x2102c3ea40650547l,0x594564df8ac3bfa7l,
        0x98102033509dad96l },
      { 0x6989643ff1d18a13l,0x35eebd91d7fc5af0l,0x078d096afaeaafd8l,
        0xb7a89341def3de98l } },
    /* 26 << 77 */
    { { 0x2a206e8decf2a73al,0x066a63978e551994l,0x3a6a088ab98d53a2l,
        0x0ce7c67c2d1124aal },
      { 0x48cec671759a113cl,0xe3b373d34f6f67fal,0x5455d479fd36727bl,
        0xe5a428eea13c0d81l } },
    /* 27 << 77 */
    { { 0xb853dbc81c86682bl,0xb78d2727b8d02b2al,0xaaf69bed8ebc329al,
        0xdb6b40b3293b2148l },
      { 0xe42ea77db8c4961fl,0xb1a12f7c20e5e0abl,0xa0ec527479e8b05el,
        0x68027391fab60a80l } },
    /* 28 << 77 */
    { { 0x6bfeea5f16b1bd5el,0xf957e4204de30ad3l,0xcbaf664e6a353b9el,
        0x5c87331226d14febl },
      { 0x4e87f98cb65f57cbl,0xdb60a6215e0cdd41l,0x67c16865a6881440l,
        0x1093ef1a46ab52aal } },
    /* 29 << 77 */
    { { 0xc095afb53f4ece64l,0x6a6bb02e7604551al,0x55d44b4e0b26b8cdl,
        0xe5f9a999f971268al },
      { 0xc08ec42511a7de84l,0x83568095fda469ddl,0x737bfba16c6c90a2l,
        0x1cb9c4a0be229831l } },
    /* 30 << 77 */
    { { 0x93bccbbabb2eec64l,0xa0c23b64da03adbel,0x5f7aa00ae0e86ac4l,
        0x470b941efc1401e6l },
      { 0x5ad8d6799df43574l,0x4ccfb8a90f65d810l,0x1bce80e3aa7fbd81l,
        0x273291ad9508d20al } },
    /* 31 << 77 */
    { { 0xf5c4b46b42a92806l,0x810684eca86ab44al,0x4591640bca0bc9f8l,
        0xb5efcdfc5c4b6054l },
      { 0x16fc89076e9edd12l,0xe29d0b50d4d792f9l,0xa45fd01c9b03116dl,
        0x85035235c81765a4l } },
    /* 32 << 77 */
    { { 0x1fe2a9b2b4b4b67cl,0xc1d10df0e8020604l,0x9d64abfcbc8058d8l,
        0x8943b9b2712a0fbbl },
      { 0x90eed9143b3def04l,0x85ab3aa24ce775ffl,0x605fd4ca7bbc9040l,
        0x8b34a564e2c75dfbl } },
    /* 33 << 77 */
    { { 0x41ffc94a10358560l,0x2d8a50729e5c28aal,0xe915a0fc4cc7eb15l,
        0xe9efab058f6d0f5dl },
      { 0xdbab47a9d19e9b91l,0x8cfed7450276154cl,0x154357ae2cfede0dl,
        0x520630df19f5a4efl } },
    /* 34 << 77 */
    { { 0x25759f7ce382360fl,0xb6db05c988bf5857l,0x2917d61d6c58d46cl,
        0x14f8e491fd20cb7al },
      { 0xb68a727a11c20340l,0x0386f86faf7ccbb6l,0x5c8bc6ccfee09a20l,
        0x7d76ff4abb7eea35l } },
    /* 35 << 77 */
    { { 0xa7bdebe7db15be7al,0x67a08054d89f0302l,0x56bf0ea9c1193364l,
        0xc824446762837ebel },
      { 0x32bd8e8b20d841b8l,0x127a0548dbb8a54fl,0x83dd4ca663b20236l,
        0x87714718203491fal } },
    /* 36 << 77 */
    { { 0x4dabcaaaaa8a5288l,0x91cc0c8aaf23a1c9l,0x34c72c6a3f220e0cl,
        0xbcc20bdf1232144al },
      { 0x6e2f42daa20ede1bl,0xc441f00c74a00515l,0xbf46a5b6734b8c4bl,
        0x574095037b56c9a4l } },
    /* 37 << 77 */
    { { 0x9f735261e4585d45l,0x9231faed6734e642l,0x1158a176be70ee6cl,
        0x35f1068d7c3501bfl },
      { 0x6beef900a2d26115l,0x649406f2ef0afee3l,0x3f43a60abc2420a1l,
        0x509002a7d5aee4acl } },
    /* 38 << 77 */
    { { 0xb46836a53ff3571bl,0x24f98b78837927c1l,0x6254256a4533c716l,
        0xf27abb0bd07ee196l },
      { 0xd7cf64fc5c6d5bfdl,0x6915c751f0cd7a77l,0xd9f590128798f534l,
        0x772b0da8f81d8b5fl } },
    /* 39 << 77 */
    { { 0x1244260c2e03fa69l,0x36cf0e3a3be1a374l,0x6e7c1633ef06b960l,
        0xa71a4c55671f90f6l },
      { 0x7a94125133c673dbl,0xc0bea51073e8c131l,0x61a8a699d4f6c734l,
        0x25e78c88341ed001l } },
    /* 40 << 77 */
    { { 0x5c18acf88e2f7d90l,0xfdbf33d777be32cdl,0x0a085cd7d2eb5ee9l,
        0x2d702cfbb3201115l },
      { 0xb6e0ebdb85c88ce8l,0x23a3ce3c1e01d617l,0x3041618e567333acl,
        0x9dd0fd8f157edb6bl } },
    /* 41 << 77 */
    { { 0x27f74702b57872b8l,0x2ef26b4f657d5fe1l,0x95426f0a57cf3d40l,
        0x847e2ad165a6067al },
      { 0xd474d9a009996a74l,0x16a56acd2a26115cl,0x02a615c3d16f4d43l,
        0xcc3fc965aadb85b7l } },
    /* 42 << 77 */
    { { 0x386bda73ce07d1b0l,0xd82910c258ad4178l,0x124f82cfcd2617f4l,
        0xcc2f5e8def691770l },
      { 0x82702550b8c30cccl,0x7b856aea1a8e575al,0xbb822fefb1ab9459l,
        0x085928bcec24e38el } },
    /* 43 << 77 */
    { { 0x5d0402ecba8f4b4dl,0xc07cd4ba00b4d58bl,0x5d8dffd529227e7al,
        0x61d44d0c31bf386fl },
      { 0xe486dc2b135e6f4dl,0x680962ebe79410efl,0xa61bd343f10088b5l,
        0x6aa76076e2e28686l } },
    /* 44 << 77 */
    { { 0x80463d118fb98871l,0xcb26f5c3bbc76affl,0xd4ab8eddfbe03614l,
        0xc8eb579bc0cf2deel },
      { 0xcc004c15c93bae41l,0x46fbae5d3aeca3b2l,0x671235cf0f1e9ab1l,
        0xadfba9349ec285c1l } },
    /* 45 << 77 */
    { { 0x88ded013f216c980l,0xc8ac4fb8f79e0bc1l,0xa29b89c6fb97a237l,
        0xb697b7809922d8e7l },
      { 0x3142c639ddb945b5l,0x447b06c7e094c3a9l,0xcdcb364272266c90l,
        0x633aad08a9385046l } },
    /* 46 << 77 */
    { { 0xa36c936bb57c6477l,0x871f8b64e94dbcc6l,0x28d0fb62a591a67bl,
        0x9d40e081c1d926f5l },
      { 0x3111eaf6f2d84b5al,0x228993f9a565b644l,0x0ccbf5922c83188bl,
        0xf87b30ab3df3e197l } },
    /* 47 << 77 */
    { { 0xb8658b317642bca8l,0x1a032d7f52800f17l,0x051dcae579bf9445l,
        0xeba6b8ee54a2e253l },
      { 0x5c8b9cadd4485692l,0x84bda40e8986e9bel,0xd16d16a42f0db448l,
        0x8ec80050a14d4188l } },
    /* 48 << 77 */
    { { 0xb2b2610798fa7aaal,0x41209ee4f073aa4el,0xf1570359f2d6b19bl,
        0xcbe6868cfc577cafl },
      { 0x186c4bdc32c04dd3l,0xa6c35faecfeee397l,0xb4a1b312f086c0cfl,
        0xe0a5ccc6d9461fe2l } },
    /* 49 << 77 */
    { { 0xc32278aa1536189fl,0x1126c55fba6df571l,0x0f71a602b194560el,
        0x8b2d7405324bd6e1l },
      { 0x8481939e3738be71l,0xb5090b1a1a4d97a9l,0x116c65a3f05ba915l,
        0x21863ad3aae448aal } },
    /* 50 << 77 */
    { { 0xd24e2679a7aae5d3l,0x7076013d0de5c1c4l,0x2d50f8babb05b629l,
        0x73c1abe26e66efbbl },
      { 0xefd4b422f2488af7l,0xe4105d02663ba575l,0x7eb60a8b53a69457l,
        0x62210008c945973bl } },
    /* 51 << 77 */
    { { 0xfb25547877a50ec6l,0xbf0392f70a37a72cl,0xa0a7a19c4be18e7al,
        0x90d8ea1625b1e0afl },
      { 0x7582a293ef953f57l,0x90a64d05bdc5465al,0xca79c497e2510717l,
        0x560dbb7c18cb641fl } },
    /* 52 << 77 */
    { { 0x1d8e32864b66abfbl,0xd26f52e559030900l,0x1ee3f6435584941al,
        0x6d3b3730569f5958l },
      { 0x9ff2a62f4789dba5l,0x91fcb81572b5c9b7l,0xf446cb7d6c8f9a0el,
        0x48f625c139b7ecb5l } },
    /* 53 << 77 */
    { { 0xbabae8011c6219b8l,0xe7a562d928ac2f23l,0xe1b4873226e20588l,
        0x06ee1cad775af051l },
      { 0xda29ae43faff79f7l,0xc141a412652ee9e0l,0x1e127f6f195f4bd0l,
        0x29c6ab4f072f34f8l } },
    /* 54 << 77 */
    { { 0x7b7c147730448112l,0x82b51af1e4a38656l,0x2bf2028a2f315010l,
        0xc9a4a01f6ea88cd4l },
      { 0xf63e95d8257e5818l,0xdd8efa10b4519b16l,0xed8973e00da910bfl,
        0xed49d0775c0fe4a9l } },
    /* 55 << 77 */
    { { 0xac3aac5eb7caee1el,0x1033898da7f4da57l,0x42145c0e5c6669b9l,
        0x42daa688c1aa2aa0l },
      { 0x629cc15c1a1d885al,0x25572ec0f4b76817l,0x8312e4359c8f8f28l,
        0x8107f8cd81965490l } },
    /* 56 << 77 */
    { { 0x516ff3a36fa6110cl,0x74fb1eb1fb93561fl,0x6c0c90478457522bl,
        0xcfd321046bb8bdc6l },
      { 0x2d6884a2cc80ad57l,0x7c27fc3586a9b637l,0x3461baedadf4e8cdl,
        0x1d56251a617242f0l } },
    /* 57 << 77 */
    { { 0x0b80d209c955bef4l,0xdf02cad206adb047l,0xf0d7cb915ec74feel,
        0xd25033751111ba44l },
      { 0x9671755edf53cb36l,0x54dcb6123368551bl,0x66d69aacc8a025a4l,
        0x6be946c6e77ef445l } },
    /* 58 << 77 */
    { { 0x719946d1a995e094l,0x65e848f6e51e04d8l,0xe62f33006a1e3113l,
        0x1541c7c1501de503l },
      { 0x4daac9faf4acfadel,0x0e58589744cd0b71l,0x544fd8690a51cd77l,
        0x60fc20ed0031016dl } },
    /* 59 << 77 */
    { { 0x58b404eca4276867l,0x46f6c3cc34f34993l,0x477ca007c636e5bdl,
        0x8018f5e57c458b47l },
      { 0xa1202270e47b668fl,0xcef48ccdee14f203l,0x23f98bae62ff9b4dl,
        0x55acc035c589edddl } },
    /* 60 << 77 */
    { { 0x3fe712af64db4444l,0x19e9d634becdd480l,0xe08bc047a930978al,
        0x2dbf24eca1280733l },
      { 0x3c0ae38c2cd706b2l,0x5b012a5b359017b9l,0x3943c38c72e0f5ael,
        0x786167ea57176fa3l } },
    /* 61 << 77 */
    { { 0xe5f9897d594881dcl,0x6b5efad8cfb820c1l,0xb2179093d55018del,
        0x39ad7d320bac56cel },
      { 0xb55122e02cfc0e81l,0x117c4661f6d89daal,0x362d01e1cb64fa09l,
        0x6a309b4e3e9c4dddl } },
    /* 62 << 77 */
    { { 0xfa979fb7abea49b1l,0xb4b1d27d10e2c6c5l,0xbd61c2c423afde7al,
        0xeb6614f89786d358l },
      { 0x4a5d816b7f6f7459l,0xe431a44f09360e7bl,0x8c27a032c309914cl,
        0xcea5d68acaede3d8l } },
    /* 63 << 77 */
    { { 0x3668f6653a0a3f95l,0x893694167ceba27bl,0x89981fade4728fe9l,
        0x7102c8a08a093562l },
      { 0xbb80310e235d21c8l,0x505e55d1befb7f7bl,0xa0a9081112958a67l,
        0xd67e106a4d851fefl } },
    /* 64 << 77 */
    { { 0xb84011a9431dd80el,0xeb7c7cca73306cd9l,0x20fadd29d1b3b730l,
        0x83858b5bfe37b3d3l },
      { 0xbf4cd193b6251d5cl,0x1cca1fd31352d952l,0xc66157a490fbc051l,
        0x7990a63889b98636l } },
    /* 0 << 84 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 84 */
    { { 0xe5aa692a87dec0e1l,0x010ded8df7b39d00l,0x7b1b80c854cfa0b5l,
        0x66beb876a0f8ea28l },
      { 0x50d7f5313476cd0el,0xa63d0e65b08d3949l,0x1a09eea953479fc6l,
        0x82ae9891f499e742l } },
    /* 2 << 84 */
    { { 0xab58b9105ca7d866l,0x582967e23adb3b34l,0x89ae4447cceac0bcl,
        0x919c667c7bf56af5l },
      { 0x9aec17b160f5dcd7l,0xec697b9fddcaadbcl,0x0b98f341463467f5l,
        0xb187f1f7a967132fl } },
    /* 3 << 84 */
    { { 0x90fe7a1d214aeb18l,0x1506af3c741432f7l,0xbb5565f9e591a0c4l,
        0x10d41a77b44f1bc3l },
      { 0xa09d65e4a84bde96l,0x42f060d8f20a6a1cl,0x652a3bfdf27f9ce7l,
        0xb6bdb65c3b3d739fl } },
    /* 4 << 84 */
    { { 0xeb5ddcb6ec7fae9fl,0x995f2714efb66e5al,0xdee95d8e69445d52l,
        0x1b6c2d4609e27620l },
      { 0x32621c318129d716l,0xb03909f10958c1aal,0x8c468ef91af4af63l,
        0x162c429ffba5cdf6l } },
    /* 5 << 84 */
    { { 0x2f682343753b9371l,0x29cab45a5f1f9cd7l,0x571623abb245db96l,
        0xc507db093fd79999l },
      { 0x4e2ef652af036c32l,0x86f0cc7805018e5cl,0xc10a73d4ab8be350l,
        0x6519b3977e826327l } },
    /* 6 << 84 */
    { { 0xe8cb5eef9c053df7l,0x8de25b37b300ea6fl,0xdb03fa92c849cffbl,
        0x242e43a7e84169bbl },
      { 0xe4fa51f4dd6f958el,0x6925a77ff4445a8dl,0xe6e72a50e90d8949l,
        0xc66648e32b1f6390l } },
    /* 7 << 84 */
    { { 0xb2ab1957173e460cl,0x1bbbce7530704590l,0xc0a90dbddb1c7162l,
        0x505e399e15cdd65dl },
      { 0x68434dcb57797ab7l,0x60ad35ba6a2ca8e8l,0x4bfdb1e0de3336c1l,
        0xbbef99ebd8b39015l } },
    /* 8 << 84 */
    { { 0x6c3b96f31711ebecl,0x2da40f1fce98fdc4l,0xb99774d357b4411fl,
        0x87c8bdf415b65bb6l },
      { 0xda3a89e3c2eef12dl,0xde95bb9b3c7471f3l,0x600f225bd812c594l,
        0x54907c5d2b75a56bl } },
    /* 9 << 84 */
    { { 0xa93cc5f08db60e35l,0x743e3cd6fa833319l,0x7dad5c41f81683c9l,
        0x70c1e7d99c34107el },
      { 0x0edc4a39a6be0907l,0x36d4703586d0b7d3l,0x8c76da03272bfa60l,
        0x0b4a07ea0f08a414l } },
    /* 10 << 84 */
    { { 0x699e4d2945c1dd53l,0xcadc5898231debb5l,0xdf49fcc7a77f00e0l,
        0x93057bbfa73e5a0el },
      { 0x2f8b7ecd027a4cd1l,0x114734b3c614011al,0xe7a01db767677c68l,
        0x89d9be5e7e273f4fl } },
    /* 11 << 84 */
    { { 0xd225cb2e089808efl,0xf1f7a27dd59e4107l,0x53afc7618211b9c9l,
        0x0361bc67e6819159l },
      { 0x2a865d0b7f071426l,0x6a3c1810e7072567l,0x3e3bca1e0d6bcabdl,
        0xa1b02bc1408591bcl } },
    /* 12 << 84 */
    { { 0xe0deee5931fba239l,0xf47424d398bd91d1l,0x0f8886f4071a3c1dl,
        0x3f7d41e8a819233bl },
      { 0x708623c2cf6eb998l,0x86bb49af609a287fl,0x942bb24963c90762l,
        0x0ef6eea555a9654bl } },
    /* 13 << 84 */
    { { 0x5f6d2d7236f5defel,0xfa9922dc56f99176l,0x6c8c5ecef78ce0c7l,
        0x7b44589dbe09b55el },
      { 0xe11b3bca9ea83770l,0xd7fa2c7f2ab71547l,0x2a3dd6fa2a1ddcc0l,
        0x09acb4305a7b7707l } },
    /* 14 << 84 */
    { { 0x4add4a2e649d4e57l,0xcd53a2b01917526el,0xc526233020b44ac4l,
        0x4028746abaa2c31dl },
      { 0x5131839064291d4cl,0xbf48f151ee5ad909l,0xcce57f597b185681l,
        0x7c3ac1b04854d442l } },
    /* 15 << 84 */
    { { 0x65587dc3c093c171l,0xae7acb2424f42b65l,0x5a338adb955996cbl,
        0xc8e656756051f91bl },
      { 0x66711fba28b8d0b1l,0x15d74137b6c10a90l,0x70cdd7eb3a232a80l,
        0xc9e2f07f6191ed24l } },
    /* 16 << 84 */
    { { 0xa80d1db6f79588c0l,0xfa52fc69b55768ccl,0x0b4df1ae7f54438al,
        0x0cadd1a7f9b46a4fl },
      { 0xb40ea6b31803dd6fl,0x488e4fa555eaae35l,0x9f047d55382e4e16l,
        0xc9b5b7e02f6e0c98l } },
    /* 17 << 84 */
    { { 0x6b1bd2d395762649l,0xa9604ee7c7aea3f6l,0x3646ff276dc6f896l,
        0x9bf0e7f52860bad1l },
      { 0x2d92c8217cb44b92l,0xa2f5ce63aea9c182l,0xd0a2afb19154a5fdl,
        0x482e474c95801da6l } },
    /* 18 << 84 */
    { { 0xc19972d0b611c24bl,0x1d468e6560a8f351l,0xeb7580697bcf6421l,
        0xec9dd0ee88fbc491l },
      { 0x5b59d2bf956c2e32l,0x73dc6864dcddf94el,0xfd5e2321bcee7665l,
        0xa7b4f8ef5e9a06c4l } },
    /* 19 << 84 */
    { { 0xfba918dd7280f855l,0xbbaac2608baec688l,0xa3b3f00f33400f42l,
        0x3d2dba2966f2e6e4l },
      { 0xb6f71a9498509375l,0x8f33031fcea423ccl,0x009b8dd04807e6fbl,
        0x5163cfe55cdb954cl } },
    /* 20 << 84 */
    { { 0x03cc8f17cf41c6e8l,0xf1f03c2a037b925cl,0xc39c19cc66d2427cl,
        0x823d24ba7b6c18e4l },
      { 0x32ef9013901f0b4fl,0x684360f1f8941c2el,0x0ebaff522c28092el,
        0x7891e4e3256c932fl } },
    /* 21 << 84 */
    { { 0x51264319ac445e3dl,0x553432e78ea74381l,0xe6eeaa6967e9c50al,
        0x27ced28462e628c7l },
      { 0x3f96d3757a4afa57l,0xde0a14c3e484c150l,0x364a24eb38bd9923l,
        0x1df18da0e5177422l } },
    /* 22 << 84 */
    { { 0x174e8f82d8d38a9bl,0x2e97c600e7de1391l,0xc5709850a1c175ddl,
        0x969041a032ae5035l },
      { 0xcbfd533b76a2086bl,0xd6bba71bd7c2e8fel,0xb2d58ee6099dfb67l,
        0x3a8b342d064a85d9l } },
    /* 23 << 84 */
    { { 0x3bc07649522f9be3l,0x690c075bdf1f49a8l,0x80e1aee83854ec42l,
        0x2a7dbf4417689dc7l },
      { 0xc004fc0e3faf4078l,0xb2f02e9edf11862cl,0xf10a5e0fa0a1b7b3l,
        0x30aca6238936ec80l } },
    /* 24 << 84 */
    { { 0xf83cbf0502f40d9al,0x4681c4682c318a4dl,0x985756180e9c2674l,
        0xbe79d0461847092el },
      { 0xaf1e480a78bd01e0l,0x6dd359e472a51db9l,0x62ce3821e3afbab6l,
        0xc5cee5b617733199l } },
    /* 25 << 84 */
    { { 0xe08b30d46ffd9fbbl,0x6e5bc69936c610b7l,0xf343cff29ce262cfl,
        0xca2e4e3568b914c1l },
      { 0x011d64c016de36c5l,0xe0b10fdd42e2b829l,0x789429816685aaf8l,
        0xe7511708230ede97l } },
    /* 26 << 84 */
    { { 0x671ed8fc3b922bf8l,0xe4d8c0a04c29b133l,0x87eb12393b6e99c4l,
        0xaff3974c8793bebal },
      { 0x037494052c18df9bl,0xc5c3a29391007139l,0x6a77234fe37a0b95l,
        0x02c29a21b661c96bl } },
    /* 27 << 84 */
    { { 0xc3aaf1d6141ecf61l,0x9195509e3bb22f53l,0x2959740422d51357l,
        0x1b083822537bed60l },
      { 0xcd7d6e35e07289f0l,0x1f94c48c6dd86effl,0xc8bb1f82eb0f9cfal,
        0x9ee0b7e61b2eb97dl } },
    /* 28 << 84 */
    { { 0x5a52fe2e34d74e31l,0xa352c3103bf79ab6l,0x97ff6c5aabfeeb8fl,
        0xbfbe8feff5c97305l },
      { 0xd6081ce6a7904608l,0x1f812f3ac4fca249l,0x9b24bc9ab9e5e200l,
        0x91022c6738012ee8l } },
    /* 29 << 84 */
    { { 0xe83d9c5d30a713a1l,0x4876e3f084ef0f93l,0xc9777029c1fbf928l,
        0xef7a6bb3bce7d2a4l },
      { 0xb8067228dfa2a659l,0xd5cd3398d877a48fl,0xbea4fd8f025d0f3fl,
        0xd67d2e352eae7c2bl } },
    /* 30 << 84 */
    { { 0x184de7d7cc5f4394l,0xb5551b5c4536e142l,0x2e89b212d34aa60al,
        0x14a96feaf50051d5l },
      { 0x4e21ef740d12bb0bl,0xc522f02060b9677el,0x8b12e4672df7731dl,
        0x39f803827b326d31l } },
    /* 31 << 84 */
    { { 0xdfb8630c39024a94l,0xaacb96a897319452l,0xd68a3961eda3867cl,
        0x0c58e2b077c4ffcal },
      { 0x3d545d634da919fal,0xef79b69af15e2289l,0x54bc3d3d808bab10l,
        0xc8ab300745f82c37l } },
    /* 32 << 84 */
    { { 0xc12738b67c4a658al,0xb3c4763940e72182l,0x3b77be468798e44fl,
        0xdc047df217a7f85fl },
      { 0x2439d4c55e59d92dl,0xcedca475e8e64d8dl,0xa724cd0d87ca9b16l,
        0x35e4fd59a5540dfel } },
    /* 33 << 84 */
    { { 0xf8c1ff18e4bcf6b1l,0x856d6285295018fal,0x433f665c3263c949l,
        0xa6a76dd6a1f21409l },
      { 0x17d32334cc7b4f79l,0xa1d0312206720e4al,0xadb6661d81d9bed5l,
        0xf0d6fb0211db15d1l } },
    /* 34 << 84 */
    { { 0x7fd11ad51fb747d2l,0xab50f9593033762bl,0x2a7e711bfbefaf5al,
        0xc73932783fef2bbfl },
      { 0xe29fa2440df6f9bel,0x9092757b71efd215l,0xee60e3114f3d6fd9l,
        0x338542d40acfb78bl } },
    /* 35 << 84 */
    { { 0x44a23f0838961a0fl,0x1426eade986987cal,0x36e6ee2e4a863cc6l,
        0x48059420628b8b79l },
      { 0x30303ad87396e1del,0x5c8bdc4838c5aad1l,0x3e40e11f5c8f5066l,
        0xabd6e7688d246bbdl } },
    /* 36 << 84 */
    { { 0x68aa40bb23330a01l,0xd23f5ee4c34eafa0l,0x3bbee3155de02c21l,
        0x18dd4397d1d8dd06l },
      { 0x3ba1939a122d7b44l,0xe6d3b40aa33870d6l,0x8e620f701c4fe3f8l,
        0xf6bba1a5d3a50cbfl } },
    /* 37 << 84 */
    { { 0x4a78bde5cfc0aee0l,0x847edc46c08c50bdl,0xbaa2439cad63c9b2l,
        0xceb4a72810fc2acbl },
      { 0xa419e40e26da033dl,0x6cc3889d03e02683l,0x1cd28559fdccf725l,
        0x0fd7e0f18d13d208l } },
    /* 38 << 84 */
    { { 0x01b9733b1f0df9d4l,0x8cc2c5f3a2b5e4f3l,0x43053bfa3a304fd4l,
        0x8e87665c0a9f1aa7l },
      { 0x087f29ecd73dc965l,0x15ace4553e9023dbl,0x2370e3092bce28b4l,
        0xf9723442b6b1e84al } },
    /* 39 << 84 */
    { { 0xbeee662eb72d9f26l,0xb19396def0e47109l,0x85b1fa73e13289d0l,
        0x436cf77e54e58e32l },
      { 0x0ec833b3e990ef77l,0x7373e3ed1b11fc25l,0xbe0eda870fc332cel,
        0xced049708d7ea856l } },
    /* 40 << 84 */
    { { 0xf85ff7857e977ca0l,0xb66ee8dadfdd5d2bl,0xf5e37950905af461l,
        0x587b9090966d487cl },
      { 0x6a198a1b32ba0127l,0xa7720e07141615acl,0xa23f3499996ef2f2l,
        0xef5f64b4470bcb3dl } },
    /* 41 << 84 */
    { { 0xa526a96292b8c559l,0x0c14aac069740a0fl,0x0d41a9e3a6bdc0a5l,
        0x97d521069c48aef4l },
      { 0xcf16bd303e7c253bl,0xcc834b1a47fdedc1l,0x7362c6e5373aab2el,
        0x264ed85ec5f590ffl } },
    /* 42 << 84 */
    { { 0x7a46d9c066d41870l,0xa50c20b14787ba09l,0x185e7e51e3d44635l,
        0xb3b3e08031e2d8dcl },
      { 0xbed1e558a179e9d9l,0x2daa3f7974a76781l,0x4372baf23a40864fl,
        0x46900c544fe75cb5l } },
    /* 43 << 84 */
    { { 0xb95f171ef76765d0l,0x4ad726d295c87502l,0x2ec769da4d7c99bdl,
        0x5e2ddd19c36cdfa8l },
      { 0xc22117fca93e6deal,0xe8a2583b93771123l,0xbe2f6089fa08a3a2l,
        0x4809d5ed8f0e1112l } },
    /* 44 << 84 */
    { { 0x3b414aa3da7a095el,0x9049acf126f5aaddl,0x78d46a4d6be8b84al,
        0xd66b1963b732b9b3l },
      { 0x5c2ac2a0de6e9555l,0xcf52d098b5bd8770l,0x15a15fa60fd28921l,
        0x56ccb81e8b27536dl } },
    /* 45 << 84 */
    { { 0x0f0d8ab89f4ccbb8l,0xed5f44d2db221729l,0x4314198800bed10cl,
        0xc94348a41d735b8bl },
      { 0x79f3e9c429ef8479l,0x4c13a4e3614c693fl,0x32c9af568e143a14l,
        0xbc517799e29ac5c4l } },
    /* 46 << 84 */
    { { 0x05e179922774856fl,0x6e52fb056c1bf55fl,0xaeda4225e4f19e16l,
        0x70f4728aaf5ccb26l },
      { 0x5d2118d1b2947f22l,0xc827ea16281d6fb9l,0x8412328d8cf0eabdl,
        0x45ee9fb203ef9dcfl } },
    /* 47 << 84 */
    { { 0x8e700421bb937d63l,0xdf8ff2d5cc4b37a6l,0xa4c0d5b25ced7b68l,
        0x6537c1efc7308f59l },
      { 0x25ce6a263b37f8e8l,0x170e9a9bdeebc6cel,0xdd0379528728d72cl,
        0x445b0e55850154bcl } },
    /* 48 << 84 */
    { { 0x4b7d0e0683a7337bl,0x1e3416d4ffecf249l,0x24840eff66a2b71fl,
        0xd0d9a50ab37cc26dl },
      { 0xe21981506fe28ef7l,0x3cc5ef1623324c7fl,0x220f3455769b5263l,
        0xe2ade2f1a10bf475l } },
    /* 49 << 84 */
    { { 0x28cd20fa458d3671l,0x1549722c2dc4847bl,0x6dd01e55591941e3l,
        0x0e6fbcea27128ccbl },
      { 0xae1a1e6b3bef0262l,0xfa8c472c8f54e103l,0x7539c0a872c052ecl,
        0xd7b273695a3490e9l } },
    /* 50 << 84 */
    { { 0x143fe1f171684349l,0x36b4722e32e19b97l,0xdc05922790980affl,
        0x175c9c889e13d674l },
      { 0xa7de5b226e6bfdb1l,0x5ea5b7b2bedb4b46l,0xd5570191d34a6e44l,
        0xfcf60d2ea24ff7e6l } },
    /* 51 << 84 */
    { { 0x614a392d677819e1l,0x7be74c7eaa5a29e8l,0xab50fece63c85f3fl,
        0xaca2e2a946cab337l },
      { 0x7f700388122a6fe3l,0xdb69f703882a04a8l,0x9a77935dcf7aed57l,
        0xdf16207c8d91c86fl } },
    /* 52 << 84 */
    { { 0x2fca49ab63ed9998l,0xa3125c44a77ddf96l,0x05dd8a8624344072l,
        0xa023dda2fec3fb56l },
      { 0x421b41fc0c743032l,0x4f2120c15e438639l,0xfb7cae51c83c1b07l,
        0xb2370caacac2171al } },
    /* 53 << 84 */
    { { 0x2eb2d9626cc820fbl,0x59feee5cb85a44bfl,0x94620fca5b6598f0l,
        0x6b922cae7e314051l },
      { 0xff8745ad106bed4el,0x546e71f5dfa1e9abl,0x935c1e481ec29487l,
        0x9509216c4d936530l } },
    /* 54 << 84 */
    { { 0xc7ca306785c9a2dbl,0xd6ae51526be8606fl,0x09dbcae6e14c651dl,
        0xc9536e239bc32f96l },
      { 0xa90535a934521b03l,0xf39c526c878756ffl,0x383172ec8aedf03cl,
        0x20a8075eefe0c034l } },
    /* 55 << 84 */
    { { 0xf22f9c6264026422l,0x8dd1078024b9d076l,0x944c742a3bef2950l,
        0x55b9502e88a2b00bl },
      { 0xa59e14b486a09817l,0xa39dd3ac47bb4071l,0x55137f663be0592fl,
        0x07fcafd4c9e63f5bl } },
    /* 56 << 84 */
    { { 0x963652ee346eb226l,0x7dfab085ec2facb7l,0x273bf2b8691add26l,
        0x30d74540f2b46c44l },
      { 0x05e8e73ef2c2d065l,0xff9b8a00d42eeac9l,0x2fcbd20597209d22l,
        0xeb740ffade14ea2cl } },
    /* 57 << 84 */
    { { 0xc71ff913a8aef518l,0x7bfc74bbfff4cfa2l,0x1716680cb6b36048l,
        0x121b2cce9ef79af1l },
      { 0xbff3c836a01eb3d3l,0x50eb1c6a5f79077bl,0xa48c32d6a004bbcfl,
        0x47a593167d64f61dl } },
    /* 58 << 84 */
    { { 0x6068147f93102016l,0x12c5f65494d12576l,0xefb071a7c9bc6b91l,
        0x7c2da0c56e23ea95l },
      { 0xf4fd45b6d4a1dd5dl,0x3e7ad9b69122b13cl,0x342ca118e6f57a48l,
        0x1c2e94a706f8288fl } },
    /* 59 << 84 */
    { { 0x99e68f075a97d231l,0x7c80de974d838758l,0xbce0f5d005872727l,
        0xbe5d95c219c4d016l },
      { 0x921d5cb19c2492eel,0x42192dc1404d6fb3l,0x4c84dcd132f988d3l,
        0xde26d61fa17b8e85l } },
    /* 60 << 84 */
    { { 0xc466dcb6137c7408l,0x9a38d7b636a266dal,0x7ef5cb0683bebf1bl,
        0xe5cdcbbf0fd014e3l },
      { 0x30aa376df65965a0l,0x60fe88c2ebb3e95el,0x33fd0b6166ee6f20l,
        0x8827dcdb3f41f0a0l } },
    /* 61 << 84 */
    { { 0xbf8a9d240c56c690l,0x40265dadddb7641dl,0x522b05bf3a6b662bl,
        0x466d1dfeb1478c9bl },
      { 0xaa6169621484469bl,0x0db6054902df8f9fl,0xc37bca023cb8bf51l,
        0x5effe34621371ce8l } },
    /* 62 << 84 */
    { { 0xe8f65264ff112c32l,0x8a9c736d7b971fb2l,0xa4f194707b75080dl,
        0xfc3f2c5a8839c59bl },
      { 0x1d6c777e5aeb49c2l,0xf3db034dda1addfel,0xd76fee5a5535affcl,
        0x0853ac70b92251fdl } },
    /* 63 << 84 */
    { { 0x37e3d5948b2a29d5l,0x28f1f4574de00ddbl,0x8083c1b5f42c328bl,
        0xd8ef1d8fe493c73bl },
      { 0x96fb626041dc61bdl,0xf74e8a9d27ee2f8al,0x7c605a802c946a5dl,
        0xeed48d653839ccfdl } },
    /* 64 << 84 */
    { { 0x9894344f3a29467al,0xde81e949c51eba6dl,0xdaea066ba5e5c2f2l,
        0x3fc8a61408c8c7b3l },
      { 0x7adff88f06d0de9fl,0xbbc11cf53b75ce0al,0x9fbb7accfbbc87d5l,
        0xa1458e267badfde2l } },
    /* 0 << 91 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 91 */
    { { 0x1cb43668e039c256l,0x5f26fb8b7c17fd5dl,0xeee426af79aa062bl,
        0x072002d0d78fbf04l },
      { 0x4c9ca237e84fb7e3l,0xb401d8a10c82133dl,0xaaa525926d7e4181l,
        0xe943083373dbb152l } },
    /* 2 << 91 */
    { { 0xf92dda31be24319al,0x03f7d28be095a8e7l,0xa52fe84098782185l,
        0x276ddafe29c24dbcl },
      { 0x80cd54961d7a64ebl,0xe43608897f1dbe42l,0x2f81a8778438d2d5l,
        0x7e4d52a885169036l } },
    /* 3 << 91 */
    { { 0x19e3d5b11d59715dl,0xc7eaa762d788983el,0xe5a730b0abf1f248l,
        0xfbab8084fae3fd83l },
      { 0x65e50d2153765b2fl,0xbdd4e083fa127f3dl,0x9cf3c074397b1b10l,
        0x59f8090cb1b59fd3l } },
    /* 4 << 91 */
    { { 0x7b15fd9d615faa8fl,0x8fa1eb40968554edl,0x7bb4447e7aa44882l,
        0x2bb2d0d1029fff32l },
      { 0x075e2a646caa6d2fl,0x8eb879de22e7351bl,0xbcd5624e9a506c62l,
        0x218eaef0a87e24dcl } },
    /* 5 << 91 */
    { { 0x37e5684744ddfa35l,0x9ccfc5c5dab3f747l,0x9ac1df3f1ee96cf4l,
        0x0c0571a13b480b8fl },
      { 0x2fbeb3d54b3a7b3cl,0x35c036695dcdbb99l,0x52a0f5dcb2415b3al,
        0xd57759b44413ed9al } },
    /* 6 << 91 */
    { { 0x1fe647d83d30a2c5l,0x0857f77ef78a81dcl,0x11d5a334131a4a9bl,
        0xc0a94af929d393f5l },
      { 0xbc3a5c0bdaa6ec1al,0xba9fe49388d2d7edl,0xbb4335b4bb614797l,
        0x991c4d6872f83533l } },
    /* 7 << 91 */
    { { 0x53258c28d2f01cb3l,0x93d6eaa3d75db0b1l,0x419a2b0de87d0db4l,
        0xa1e48f03d8fe8493l },
      { 0xf747faf6c508b23al,0xf137571a35d53549l,0x9f5e58e2fcf9b838l,
        0xc7186ceea7fd3cf5l } },
    /* 8 << 91 */
    { { 0x77b868cee978a1d3l,0xe3a68b337ab92d04l,0x5102979487a5b862l,
        0x5f0606c33a61d41dl },
      { 0x2814be276f9326f1l,0x2f521c14c6fe3c2el,0x17464d7dacdf7351l,
        0x10f5f9d3777f7e44l } },
    /* 9 << 91 */
    { { 0xce8e616b269fb37dl,0xaaf738047de62de5l,0xaba111754fdd4153l,
        0x515759ba3770b49bl },
      { 0x8b09ebf8aa423a61l,0x592245a1cd41fb92l,0x1cba8ec19b4c8936l,
        0xa87e91e3af36710el } },
    /* 10 << 91 */
    { { 0x1fd84ce43d34a2e3l,0xee3759ceb43b5d61l,0x895bc78c619186c7l,
        0xf19c3809cbb9725al },
      { 0xc0be21aade744b1fl,0xa7d222b060f8056bl,0x74be6157b23efe11l,
        0x6fab2b4f0cd68253l } },
    /* 11 << 91 */
    { { 0xad33ea5f4bf1d725l,0x9c1d8ee24f6c950fl,0x544ee78aa377af06l,
        0x54f489bb94a113e1l },
      { 0x8f11d634992fb7e8l,0x0169a7aaa2a44347l,0x1d49d4af95020e00l,
        0x95945722e08e120bl } },
    /* 12 << 91 */
    { { 0xb6e33878a4d32282l,0xe36e029d48020ae7l,0xe05847fb37a9b750l,
        0xf876812cb29e3819l },
      { 0x84ad138ed23a17f0l,0x6d7b4480f0b3950el,0xdfa8aef42fd67ae0l,
        0x8d3eea2452333af6l } },
    /* 13 << 91 */
    { { 0x0d052075b15d5accl,0xc6d9c79fbd815bc4l,0x8dcafd88dfa36cf2l,
        0x908ccbe238aa9070l },
      { 0x638722c4ba35afcel,0x5a3da8b0fd6abf0bl,0x2dce252cc9c335c1l,
        0x84e7f0de65aa799bl } },
    /* 14 << 91 */
    { { 0x2101a522b99a72cbl,0x06de6e6787618016l,0x5ff8c7cde6f3653el,
        0x0a821ab5c7a6754al },
      { 0x7e3fa52b7cb0b5a2l,0xa7fb121cc9048790l,0x1a72502006ce053al,
        0xb490a31f04e929b0l } },
    /* 15 << 91 */
    { { 0xe17be47d62dd61adl,0x781a961c6be01371l,0x1063bfd3dae3cbbal,
        0x356474067f73c9bal },
      { 0xf50e957b2736a129l,0xa6313702ed13f256l,0x9436ee653a19fcc5l,
        0xcf2bdb29e7a4c8b6l } },
    /* 16 << 91 */
    { { 0xb06b1244c5f95cd8l,0xda8c8af0f4ab95f4l,0x1bae59c2b9e5836dl,
        0x07d51e7e3acffffcl },
      { 0x01e15e6ac2ccbcdal,0x3bc1923f8528c3e0l,0x43324577a49fead4l,
        0x61a1b8842aa7a711l } },
    /* 17 << 91 */
    { { 0xf9a86e08700230efl,0x0af585a1bd19adf8l,0x7645f361f55ad8f2l,
        0x6e67622346c3614cl },
      { 0x23cb257c4e774d3fl,0x82a38513ac102d1bl,0x9bcddd887b126aa5l,
        0xe716998beefd3ee4l } },
    /* 18 << 91 */
    { { 0x4239d571fb167583l,0xdd011c78d16c8f8al,0x271c289569a27519l,
        0x9ce0a3b7d2d64b6al },
      { 0x8c977289d5ec6738l,0xa3b49f9a8840ef6bl,0x808c14c99a453419l,
        0x5c00295b0cf0a2d5l } },
    /* 19 << 91 */
    { { 0x524414fb1d4bcc76l,0xb07691d2459a88f1l,0x77f43263f70d110fl,
        0x64ada5e0b7abf9f3l },
      { 0xafd0f94e5b544cf5l,0xb4a13a15fd2713fel,0xb99b7d6e250c74f4l,
        0x097f2f7320324e45l } },
    /* 20 << 91 */
    { { 0x994b37d8affa8208l,0xc3c31b0bdc29aafcl,0x3da746517a3a607fl,
        0xd8e1b8c1fe6955d6l },
      { 0x716e1815c8418682l,0x541d487f7dc91d97l,0x48a04669c6996982l,
        0xf39cab1583a6502el } },
    /* 21 << 91 */
    { { 0x025801a0e68db055l,0xf3569758ba3338d5l,0xb0c8c0aaee2afa84l,
        0x4f6985d3fb6562d1l },
      { 0x351f1f15132ed17al,0x510ed0b4c04365fel,0xa3f98138e5b1f066l,
        0xbc9d95d632df03dcl } },
    /* 22 << 91 */
    { { 0xa83ccf6e19abd09el,0x0b4097c14ff17edbl,0x58a5c478d64a06cel,
        0x2ddcc3fd544a58fdl },
      { 0xd449503d9e8153b8l,0x3324fd027774179bl,0xaf5d47c8dbd9120cl,
        0xeb86016234fa94dbl } },
    /* 23 << 91 */
    { { 0x5817bdd1972f07f4l,0xe5579e2ed27bbcebl,0x86847a1f5f11e5a6l,
        0xb39ed2557c3cf048l },
      { 0xe1076417a2f62e55l,0x6b9ab38f1bcf82a2l,0x4bb7c3197aeb29f9l,
        0xf6d17da317227a46l } },
    /* 24 << 91 */
    { { 0xab53ddbd0f968c00l,0xa03da7ec000c880bl,0x7b2396246a9ad24dl,
        0x612c040101ec60d0l },
      { 0x70d10493109f5df1l,0xfbda403080af7550l,0x30b93f95c6b9a9b3l,
        0x0c74ec71007d9418l } },
    /* 25 << 91 */
    { { 0x941755646edb951fl,0x5f4a9d787f22c282l,0xb7870895b38d1196l,
        0xbc593df3a228ce7cl },
      { 0xc78c5bd46af3641al,0x7802200b3d9b3dccl,0x0dc73f328be33304l,
        0x847ed87d61ffb79al } },
    /* 26 << 91 */
    { { 0xf85c974e6d671192l,0x1e14100ade16f60fl,0x45cb0d5a95c38797l,
        0x18923bba9b022da4l },
      { 0xef2be899bbe7e86el,0x4a1510ee216067bfl,0xd98c815484d5ce3el,
        0x1af777f0f92a2b90l } },
    /* 27 << 91 */
    { { 0x9fbcb4004ef65724l,0x3e04a4c93c0ca6fel,0xfb3e2cb555002994l,
        0x1f3a93c55363ecabl },
      { 0x1fe00efe3923555bl,0x744bedd91e1751eal,0x3fb2db596ab69357l,
        0x8dbd7365f5e6618bl } },
    /* 28 << 91 */
    { { 0x99d53099df1ea40el,0xb3f24a0b57d61e64l,0xd088a198596eb812l,
        0x22c8361b5762940bl },
      { 0x66f01f97f9c0d95cl,0x884611728e43cdael,0x11599a7fb72b15c3l,
        0x135a7536420d95ccl } },
    /* 29 << 91 */
    { { 0x2dcdf0f75f7ae2f6l,0x15fc6e1dd7fa6da2l,0x81ca829ad1d441b6l,
        0x84c10cf804a106b6l },
      { 0xa9b26c95a73fbbd0l,0x7f24e0cb4d8f6ee8l,0x48b459371e25a043l,
        0xf8a74fca036f3dfel } },
    /* 30 << 91 */
    { { 0x1ed46585c9f84296l,0x7fbaa8fb3bc278b0l,0xa8e96cd46c4fcbd0l,
        0x940a120273b60a5fl },
      { 0x34aae12055a4aec8l,0x550e9a74dbd742f0l,0x794456d7228c68abl,
        0x492f8868a4e25ec6l } },
    /* 31 << 91 */
    { { 0x682915adb2d8f398l,0xf13b51cc5b84c953l,0xcda90ab85bb917d6l,
        0x4b6155604ea3dee1l },
      { 0x578b4e850a52c1c8l,0xeab1a69520b75fc4l,0x60c14f3caa0bb3c6l,
        0x220f448ab8216094l } },
    /* 32 << 91 */
    { { 0x4fe7ee31b0e63d34l,0xf4600572a9e54fabl,0xc0493334d5e7b5a4l,
        0x8589fb9206d54831l },
      { 0xaa70f5cc6583553al,0x0879094ae25649e5l,0xcc90450710044652l,
        0xebb0696d02541c4fl } },
    /* 33 << 91 */
    { { 0x5a171fdeb9718710l,0x38f1bed8f374a9f5l,0xc8c582e1ba39bdc1l,
        0xfc457b0a908cc0cel },
      { 0x9a187fd4883841e2l,0x8ec25b3938725381l,0x2553ed0596f84395l,
        0x095c76616f6c6897l } },
    /* 34 << 91 */
    { { 0x917ac85c4bdc5610l,0xb2885fe4179eb301l,0x5fc655478b78bdccl,
        0x4a9fc893e59e4699l },
      { 0xbb7ff0cd3ce299afl,0x195be9b3adf38b20l,0x6a929c87d38ddb8fl,
        0x55fcc99cb21a51b9l } },
    /* 35 << 91 */
    { { 0x2b695b4c721a4593l,0xed1e9a15768eaac2l,0xfb63d71c7489f914l,
        0xf98ba31c78118910l },
      { 0x802913739b128eb4l,0x7801214ed448af4al,0xdbd2e22b55418dd3l,
        0xeffb3c0dd3998242l } },
    /* 36 << 91 */
    { { 0xdfa6077cc7bf3827l,0xf2165bcb47f8238fl,0xfe37cf688564d554l,
        0xe5f825c40a81fb98l },
      { 0x43cc4f67ffed4d6fl,0xbc609578b50a34b0l,0x8aa8fcf95041faf1l,
        0x5659f053651773b6l } },
    /* 37 << 91 */
    { { 0xe87582c36044d63bl,0xa60894090cdb0ca0l,0x8c993e0fbfb2bcf6l,
        0xfc64a71945985cfcl },
      { 0x15c4da8083dbedbal,0x804ae1122be67df7l,0xda4c9658a23defdel,
        0x12002ddd5156e0d3l } },
    /* 38 << 91 */
    { { 0xe68eae895dd21b96l,0x8b99f28bcf44624dl,0x0ae008081ec8897al,
        0xdd0a93036712f76el },
      { 0x962375224e233de4l,0x192445b12b36a8a5l,0xabf9ff74023993d9l,
        0x21f37bf42aad4a8fl } },
    /* 39 << 91 */
    { { 0x340a4349f8bd2bbdl,0x1d902cd94868195dl,0x3d27bbf1e5fdb6f1l,
        0x7a5ab088124f9f1cl },
      { 0xc466ab06f7a09e03l,0x2f8a197731f2c123l,0xda355dc7041b6657l,
        0xcb840d128ece2a7cl } },
    /* 40 << 91 */
    { { 0xb600ad9f7db32675l,0x78fea13307a06f1bl,0x5d032269b31f6094l,
        0x07753ef583ec37aal },
      { 0x03485aed9c0bea78l,0x41bb3989bc3f4524l,0x09403761697f726dl,
        0x6109beb3df394820l } },
    /* 41 << 91 */
    { { 0x804111ea3b6d1145l,0xb6271ea9a8582654l,0x619615e624e66562l,
        0xa2554945d7b6ad9cl },
      { 0xd9c4985e99bfe35fl,0x9770ccc07b51cdf6l,0x7c32701392881832l,
        0x8777d45f286b26d1l } },
    /* 42 << 91 */
    { { 0x9bbeda22d847999dl,0x03aa33b6c3525d32l,0x4b7b96d428a959a1l,
        0xbb3786e531e5d234l },
      { 0xaeb5d3ce6961f247l,0x20aa85af02f93d3fl,0x9cd1ad3dd7a7ae4fl,
        0xbf6688f0781adaa8l } },
    /* 43 << 91 */
    { { 0xb1b40e867469ceadl,0x1904c524309fca48l,0x9b7312af4b54bbc7l,
        0xbe24bf8f593affa2l },
      { 0xbe5e0790bd98764bl,0xa0f45f17a26e299el,0x4af0d2c26b8fe4c7l,
        0xef170db18ae8a3e6l } },
    /* 44 << 91 */
    { { 0x0e8d61a029e0ccc1l,0xcd53e87e60ad36cal,0x328c6623c8173822l,
        0x7ee1767da496be55l },
      { 0x89f13259648945afl,0x9e45a5fd25c8009cl,0xaf2febd91f61ab8cl,
        0x43f6bc868a275385l } },
    /* 45 << 91 */
    { { 0x87792348f2142e79l,0x17d89259c6e6238al,0x7536d2f64a839d9bl,
        0x1f428fce76a1fbdcl },
      { 0x1c1096010db06dfel,0xbfc16bc150a3a3ccl,0xf9cbd9ec9b30f41bl,
        0x5b5da0d600138ccel } },
    /* 46 << 91 */
    { { 0xec1d0a4856ef96a7l,0xb47eb848982bf842l,0x66deae32ec3f700dl,
        0x4e43c42caa1181e0l },
      { 0xa1d72a31d1a4aa2al,0x440d4668c004f3cel,0x0d6a2d3b45fe8a7al,
        0x820e52e2fb128365l } },
    /* 47 << 91 */
    { { 0x29ac5fcf25e51b09l,0x180cd2bf2023d159l,0xa9892171a1ebf90el,
        0xf97c4c877c132181l },
      { 0x9f1dc724c03dbb7el,0xae043765018cbbe4l,0xfb0b2a360767d153l,
        0xa8e2f4d6249cbaebl } },
    /* 48 << 91 */
    { { 0x172a5247d95ea168l,0x1758fada2970764al,0xac803a511d978169l,
        0x299cfe2ede77e01bl },
      { 0x652a1e17b0a98927l,0x2e26e1d120014495l,0x7ae0af9f7175b56al,
        0xc2e22a80d64b9f95l } },
    /* 49 << 91 */
    { { 0x4d0ff9fbd90a060al,0x496a27dbbaf38085l,0x32305401da776bcfl,
        0xb8cdcef6725f209el },
      { 0x61ba0f37436a0bbal,0x263fa10876860049l,0x92beb98eda3542cfl,
        0xa2d4d14ad5849538l } },
    /* 50 << 91 */
    { { 0x989b9d6812e9a1bcl,0x61d9075c5f6e3268l,0x352c6aa999ace638l,
        0xde4e4a55920f43ffl },
      { 0xe5e4144ad673c017l,0x667417ae6f6e05eal,0x613416aedcd1bd56l,
        0x5eb3620186693711l } },
    /* 51 << 91 */
    { { 0x2d7bc5043a1aa914l,0x175a129976dc5975l,0xe900e0f23fc8125cl,
        0x569ef68c11198875l },
      { 0x9012db6363a113b4l,0xe3bd3f5698835766l,0xa5c94a5276412deal,
        0xad9e2a09aa735e5cl } },
    /* 52 << 91 */
    { { 0x405a984c508b65e9l,0xbde4a1d16df1a0d1l,0x1a9433a1dfba80dal,
        0xe9192ff99440ad2el },
      { 0x9f6496965099fe92l,0x25ddb65c0b27a54al,0x178279ddc590da61l,
        0x5479a999fbde681al } },
    /* 53 << 91 */
    { { 0xd0e84e05013fe162l,0xbe11dc92632d471bl,0xdf0b0c45fc0e089fl,
        0x04fb15b04c144025l },
      { 0xa61d5fc213c99927l,0xa033e9e03de2eb35l,0xf8185d5cb8dacbb4l,
        0x9a88e2658644549dl } },
    /* 54 << 91 */
    { { 0xf717af6254671ff6l,0x4bd4241b5fa58603l,0x06fba40be67773c0l,
        0xc1d933d26a2847e9l },
      { 0xf4f5acf3689e2c70l,0x92aab0e746bafd31l,0x798d76aa3473f6e5l,
        0xcc6641db93141934l } },
    /* 55 << 91 */
    { { 0xcae27757d31e535el,0x04cc43b687c2ee11l,0x8d1f96752e029ffal,
        0xc2150672e4cc7a2cl },
      { 0x3b03c1e08d68b013l,0xa9d6816fedf298f3l,0x1bfbb529a2804464l,
        0x95a52fae5db22125l } },
    /* 56 << 91 */
    { { 0x55b321600e1cb64el,0x004828f67e7fc9fel,0x13394b821bb0fb93l,
        0xb6293a2d35f1a920l },
      { 0xde35ef21d145d2d9l,0xbe6225b3bb8fa603l,0x00fc8f6b32cf252dl,
        0xa28e52e6117cf8c2l } },
    /* 57 << 91 */
    { { 0x9d1dc89b4c371e6dl,0xcebe067536ef0f28l,0x5de05d09a4292f81l,
        0xa8303593353e3083l },
      { 0xa1715b0a7e37a9bbl,0x8c56f61e2b8faec3l,0x5250743133c9b102l,
        0x0130cefca44431f0l } },
    /* 58 << 91 */
    { { 0x56039fa0bd865cfbl,0x4b03e578bc5f1dd7l,0x40edf2e4babe7224l,
        0xc752496d3a1988f6l },
      { 0xd1572d3b564beb6bl,0x0db1d11039a1c608l,0x568d193416f60126l,
        0x05ae9668f354af33l } },
    /* 59 << 91 */
    { { 0x19de6d37c92544f2l,0xcc084353a35837d5l,0xcbb6869c1a514ecel,
        0xb633e7282e1d1066l },
      { 0xf15dd69f936c581cl,0x96e7b8ce7439c4f9l,0x5e676f482e448a5bl,
        0xb2ca7d5bfd916bbbl } },
    /* 60 << 91 */
    { { 0xd55a2541f5024025l,0x47bc5769e4c2d937l,0x7d31b92a0362189fl,
        0x83f3086eef7816f9l },
      { 0xf9f46d94b587579al,0xec2d22d830e76c5fl,0x27d57461b000ffcfl,
        0xbb7e65f9364ffc2cl } },
    /* 61 << 91 */
    { { 0x7c7c94776652a220l,0x61618f89d696c981l,0x5021701d89effff3l,
        0xf2c8ff8e7c314163l },
      { 0x2da413ad8efb4d3el,0x937b5adfce176d95l,0x22867d342a67d51cl,
        0x262b9b1018eb3ac9l } },
    /* 62 << 91 */
    { { 0x4e314fe4c43ff28bl,0x764766276a664e7al,0x3e90e40bb7a565c2l,
        0x8588993ac1acf831l },
      { 0xd7b501d68f938829l,0x996627ee3edd7d4cl,0x37d44a6290cd34c7l,
        0xa8327499f3833e8dl } },
    /* 63 << 91 */
    { { 0x2e18917d4bf50353l,0x85dd726b556765fbl,0x54fe65d693d5ab66l,
        0x3ddbaced915c25fel },
      { 0xa799d9a412f22e85l,0xe2a248676d06f6bcl,0xf4f1ee5643ca1637l,
        0xfda2828b61ece30al } },
    /* 64 << 91 */
    { { 0x758c1a3ea2dee7a6l,0xdcde2f3c734b2284l,0xaba445d24eaba6adl,
        0x35aaf66876cee0a7l },
      { 0x7e0b04a9e5aa049al,0xe74083ad91103e84l,0xbeb183ce40afecc3l,
        0x6b89de9fea043f7al } },
    /* 0 << 98 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 98 */
    { { 0x0e299d23fe67ba66l,0x9145076093cf2f34l,0xf45b5ea997fcf913l,
        0x5be008438bd7dddal },
      { 0x358c3e05d53ff04dl,0xbf7ccdc35de91ef7l,0xad684dbfb69ec1a0l,
        0x367e7cf2801fd997l } },
    /* 2 << 98 */
    { { 0x0ca1f3b7b0dc8595l,0x27de46089f1d9f2el,0x1af3bf39badd82a7l,
        0x79356a7965862448l },
      { 0xc0602345f5f9a052l,0x1a8b0f89139a42f9l,0xb53eee42844d40fcl,
        0x93b0bfe54e5b6368l } },
    /* 3 << 98 */
    { { 0x5434dd02c024789cl,0x90dca9ea41b57bfcl,0x8aa898e2243398dfl,
        0xf607c834894a94bbl },
      { 0xbb07be97c2c99b76l,0x6576ba6718c29302l,0x3d79efcce703a88cl,
        0xf259ced7b6a0d106l } },
    /* 4 << 98 */
    { { 0x0f893a5dc8de610bl,0xe8c515fb67e223cel,0x7774bfa64ead6dc5l,
        0x89d20f95925c728fl },
      { 0x7a1e0966098583cel,0xa2eedb9493f2a7d7l,0x1b2820974c304d4al,
        0x0842e3dac077282dl } },
    /* 5 << 98 */
    { { 0xe4d972a33b9e2d7bl,0x7cc60b27c48218ffl,0x8fc7083884149d91l,
        0x5c04346f2f461eccl },
      { 0xebe9fdf2614650a9l,0x5e35b537c1f666acl,0x645613d188babc83l,
        0x88cace3ac5e1c93el } },
    /* 6 << 98 */
    { { 0x209ca3753de92e23l,0xccb03cc85fbbb6e3l,0xccb90f03d7b1487el,
        0xfa9c2a38c710941fl },
      { 0x756c38236724ceedl,0x3a902258192d0323l,0xb150e519ea5e038el,
        0xdcba2865c7427591l } },
    /* 7 << 98 */
    { { 0xe549237f78890732l,0xc443bef953fcb4d9l,0x9884d8a6eb3480d6l,
        0x8a35b6a13048b186l },
      { 0xb4e4471665e9a90al,0x45bf380d653006c0l,0x8f3f820d4fe9ae3bl,
        0x244a35a0979a3b71l } },
    /* 8 << 98 */
    { { 0xa1010e9d74cd06ffl,0x9c17c7dfaca3eeacl,0x74c86cd38063aa2bl,
        0x8595c4b3734614ffl },
      { 0xa3de00ca990f62ccl,0xd9bed213ca0c3be5l,0x7886078adf8ce9f5l,
        0xddb27ce35cd44444l } },
    /* 9 << 98 */
    { { 0xed374a6658926dddl,0x138b2d49908015b8l,0x886c6579de1f7ab8l,
        0x888b9aa0c3020b7al },
      { 0xd3ec034e3a96e355l,0xba65b0b8f30fbe9al,0x064c8e50ff21367al,
        0x1f508ea40b04b46el } },
    /* 10 << 98 */
    { { 0x98561a49747c866cl,0xbbb1e5fe0518a062l,0x20ff4e8becdc3608l,
        0x7f55cded20184027l },
      { 0x8d73ec95f38c85f0l,0x5b589fdf8bc3b8c3l,0xbe95dd980f12b66fl,
        0xf5bd1a090e338e01l } },
    /* 11 << 98 */
    { { 0x65163ae55e915918l,0x6158d6d986f8a46bl,0x8466b538eeebf99cl,
        0xca8761f6bca477efl },
      { 0xaf3449c29ebbc601l,0xef3b0f41e0c3ae2fl,0xaa6c577d5de63752l,
        0xe916660164682a51l } },
    /* 12 << 98 */
    { { 0x5a3097befc15aa1el,0x40d12548b54b0745l,0x5bad4706519a5f12l,
        0xed03f717a439dee6l },
      { 0x0794bb6c4a02c499l,0xf725083dcffe71d2l,0x2cad75190f3adcafl,
        0x7f68ea1c43729310l } },
    /* 13 << 98 */
    { { 0xe747c8c7b7ffd977l,0xec104c3580761a22l,0x8395ebaf5a3ffb83l,
        0xfb3261f4e4b63db7l },
      { 0x53544960d883e544l,0x13520d708cc2eeb8l,0x08f6337bd3d65f99l,
        0x83997db2781cf95bl } },
    /* 14 << 98 */
    { { 0xce6ff1060dbd2c01l,0x4f8eea6b1f9ce934l,0x546f7c4b0e993921l,
        0x6236a3245e753fc7l },
      { 0x65a41f84a16022e9l,0x0c18d87843d1dbb2l,0x73c556402d4cef9cl,
        0xa042810870444c74l } },
    /* 15 << 98 */
    { { 0x68e4f15e9afdfb3cl,0x49a561435bdfb6dfl,0xa9bc1bd45f823d97l,
        0xbceb5970ea111c2al },
      { 0x366b455fb269bbc4l,0x7cd85e1ee9bc5d62l,0xc743c41c4f18b086l,
        0xa4b4099095294fb9l } },
    /* 16 << 98 */
    { { 0x9c7c581d26ee8382l,0xcf17dcc5359d638el,0xee8273abb728ae3dl,
        0x1d112926f821f047l },
      { 0x1149847750491a74l,0x687fa761fde0dfb9l,0x2c2580227ea435abl,
        0x6b8bdb9491ce7e3fl } },
    /* 17 << 98 */
    { { 0x4c5b5dc93bf834aal,0x043718194f6c7e4bl,0xc284e00a3736bcadl,
        0x0d88111821ae8f8dl },
      { 0xf9cf0f82f48c8e33l,0xa11fd075a1bf40dbl,0xdceab0dedc2733e5l,
        0xc560a8b58e986bd7l } },
    /* 18 << 98 */
    { { 0x48dd1fe23929d097l,0x3885b29092f188f1l,0x0f2ae613da6fcdacl,
        0x9054303eb662a46cl },
      { 0xb6871e440738042al,0x98e6a977bdaf6449l,0xd8bc0650d1c9df1bl,
        0xef3d645136e098f9l } },
    /* 19 << 98 */
    { { 0x03fbae82b6d72d28l,0x77ca9db1f5d84080l,0x8a112cffa58efc1cl,
        0x518d761cc564cb4al },
      { 0x69b5740ef0d1b5cel,0x717039cce9eb1785l,0x3fe29f9022f53382l,
        0x8e54ba566bc7c95cl } },
    /* 20 << 98 */
    { { 0x9c806d8af7f91d0fl,0x3b61b0f1a82a5728l,0x4640032d94d76754l,
        0x273eb5de47d834c6l },
      { 0x2988abf77b4e4d53l,0xb7ce66bfde401777l,0x9fba6b32715071b3l,
        0x82413c24ad3a1a98l } },
    /* 21 << 98 */
    { { 0x5b7fc8c4e0e8ad93l,0xb5679aee5fab868dl,0xb1f9d2fa2b3946f3l,
        0x458897dc5685b50al },
      { 0x1e98c93089d0caf3l,0x39564c5f78642e92l,0x1b77729a0dbdaf18l,
        0xf9170722579e82e6l } },
    /* 22 << 98 */
    { { 0x680c0317e4515fa5l,0xf85cff84fb0c790fl,0xc7a82aab6d2e0765l,
        0x7446bca935c82b32l },
      { 0x5de607aa6d63184fl,0x7c1a46a8262803a6l,0xd218313daebe8035l,
        0x92113ffdc73c51f8l } },
    /* 23 << 98 */
    { { 0x4b38e08312e7e46cl,0x69d0a37a56126bd5l,0xfb3f324b73c07e04l,
        0xa0c22f678fda7267l },
      { 0x8f2c00514d2c7d8fl,0xbc45ced3cbe2cae5l,0xe1c6cf07a8f0f277l,
        0xbc3923121eb99a98l } },
    /* 24 << 98 */
    { { 0x75537b7e3cc8ac85l,0x8d725f57dd02753bl,0xfd05ff64b737df2fl,
        0x55fe8712f6d2531dl },
      { 0x57ce04a96ab6b01cl,0x69a02a897cd93724l,0x4f82ac35cf86699bl,
        0x8242d3ad9cb4b232l } },
    /* 25 << 98 */
    { { 0x713d0f65d62105e5l,0xbb222bfa2d29be61l,0xf2f9a79e6cfbef09l,
        0xfc24d8d3d5d6782fl },
      { 0x5db77085d4129967l,0xdb81c3ccdc3c2a43l,0x9d655fc005d8d9a3l,
        0x3f5d057a54298026l } },
    /* 26 << 98 */
    { { 0x1157f56d88c54694l,0xb26baba59b09573el,0x2cab03b022adffd1l,
        0x60a412c8dd69f383l },
      { 0xed76e98b54b25039l,0xd4ee67d3687e714dl,0x877396487b00b594l,
        0xce419775c9ef709bl } },
    /* 27 << 98 */
    { { 0x40f76f851c203a40l,0x30d352d6eafd8f91l,0xaf196d3d95578dd2l,
        0xea4bb3d777cc3f3dl },
      { 0x42a5bd03b98e782bl,0xac958c400624920dl,0xb838134cfc56fcc8l,
        0x86ec4ccf89572e5el } },
    /* 28 << 98 */
    { { 0x69c435269be47be0l,0x323b7dd8cb28fea1l,0xfa5538ba3a6c67e5l,
        0xef921d701d378e46l },
      { 0xf92961fc3c4b880el,0x3f6f914e98940a67l,0xa990eb0afef0ff39l,
        0xa6c2920ff0eeff9cl } },
    /* 29 << 98 */
    { { 0xca80416651b8d9a3l,0x42531bc90ffb0db1l,0x72ce4718aa82e7cel,
        0x6e199913df574741l },
      { 0xd5f1b13dd5d36946l,0x8255dc65f68f0194l,0xdc9df4cd8710d230l,
        0x3453c20f138c1988l } },
    /* 30 << 98 */
    { { 0x9af98dc089a6ef01l,0x4dbcc3f09857df85l,0x348056015c1ad924l,
        0x40448da5d0493046l },
      { 0xf629926d4ee343e2l,0x6343f1bd90e8a301l,0xefc9349140815b3fl,
        0xf882a423de8f66fbl } },
    /* 31 << 98 */
    { { 0x3a12d5f4e7db9f57l,0x7dfba38a3c384c27l,0x7a904bfd6fc660b1l,
        0xeb6c5db32773b21cl },
      { 0xc350ee661cdfe049l,0x9baac0ce44540f29l,0xbc57b6aba5ec6aadl,
        0x167ce8c30a7c1baal } },
    /* 32 << 98 */
    { { 0xb23a03a553fb2b56l,0x6ce141e74e057f78l,0x796525c389e490d9l,
        0x0bc95725a31a7e75l },
      { 0x1ec567911220fd06l,0x716e3a3c408b0bd6l,0x31cd6bf7e8ebeba9l,
        0xa7326ca6bee6b670l } },
    /* 33 << 98 */
    { { 0x3d9f851ccd090c43l,0x561e8f13f12c3988l,0x50490b6a904b7be4l,
        0x61690ce10410737bl },
      { 0x299e9a370f009052l,0x258758f0f026092el,0x9fa255f3fdfcdc0fl,
        0xdbc9fb1fc0e1bcd2l } },
    /* 34 << 98 */
    { { 0x35f9dd6e24651840l,0xdca45a84a5c59abcl,0x103d396fecca4938l,
        0x4532da0ab97b3f29l },
      { 0xc4135ea51999a6bfl,0x3aa9505a5e6bf2eel,0xf77cef063f5be093l,
        0x97d1a0f8a943152el } },
    /* 35 << 98 */
    { { 0x2cb0ebba2e1c21ddl,0xf41b29fc2c6797c4l,0xc6e17321b300101fl,
        0x4422b0e9d0d79a89l },
      { 0x49e4901c92f1bfc4l,0x06ab1f8fe1e10ed9l,0x84d35577db2926b8l,
        0xca349d39356e8ec2l } },
    /* 36 << 98 */
    { { 0x70b63d32343bf1a9l,0x8fd3bd2837d1a6b1l,0x0454879c316865b4l,
        0xee959ff6c458efa2l },
      { 0x0461dcf89706dc3fl,0x737db0e2164e4b2el,0x092626802f8843c8l,
        0x54498bbc7745e6f6l } },
    /* 37 << 98 */
    { { 0x359473faa29e24afl,0xfcc3c45470aa87a1l,0xfd2c4bf500573acel,
        0xb65b514e28dd1965l },
      { 0xe46ae7cf2193e393l,0x60e9a4e1f5444d97l,0xe7594e9600ff38edl,
        0x43d84d2f0a0e0f02l } },
    /* 38 << 98 */
    { { 0x8b6db141ee398a21l,0xb88a56aee3bcc5bel,0x0a1aa52f373460eal,
        0x20da1a56160bb19bl },
      { 0xfb54999d65bf0384l,0x71a14d245d5a180el,0xbc44db7b21737b04l,
        0xd84fcb1801dd8e92l } },
    /* 39 << 98 */
    { { 0x80de937bfa44b479l,0x535054995c98fd4fl,0x1edb12ab28f08727l,
        0x4c58b582a5f3ef53l },
      { 0xbfb236d88327f246l,0xc3a3bfaa4d7df320l,0xecd96c59b96024f2l,
        0xfc293a537f4e0433l } },
    /* 40 << 98 */
    { { 0x5341352b5acf6e10l,0xc50343fdafe652c3l,0x4af3792d18577a7fl,
        0xe1a4c617af16823dl },
      { 0x9b26d0cd33425d0al,0x306399ed9b7bc47fl,0x2a792f33706bb20bl,
        0x3121961498111055l } },
    /* 41 << 98 */
    { { 0x864ec06487f5d28bl,0x11392d91962277fdl,0xb5aa7942bb6aed5fl,
        0x080094dc47e799d9l },
      { 0x4afa588c208ba19bl,0xd3e7570f8512f284l,0xcbae64e602f5799al,
        0xdeebe7ef514b9492l } },
    /* 42 << 98 */
    { { 0x30300f98e5c298ffl,0x17f561be3678361fl,0xf52ff31298cb9a16l,
        0x6233c3bc5562d490l },
      { 0x7bfa15a192e3a2cbl,0x961bcfd1e6365119l,0x3bdd29bf2c8c53b1l,
        0x739704df822844bal } },
    /* 43 << 98 */
    { { 0x7dacfb587e7b754bl,0x23360791a806c9b9l,0xe7eb88c923504452l,
        0x2983e996852c1783l },
      { 0xdd4ae529958d881dl,0x026bae03262c7b3cl,0x3a6f9193960b52d1l,
        0xd0980f9092696cfbl } },
    /* 44 << 98 */
    { { 0x4c1f428cd5f30851l,0x94dfed272a4f6630l,0x4df53772fc5d48a4l,
        0xdd2d5a2f933260cel },
      { 0x574115bdd44cc7a5l,0x4ba6b20dbd12533al,0x30e93cb8243057c9l,
        0x794c486a14de320el } },
    /* 45 << 98 */
    { { 0xe925d4cef21496e4l,0xf951d198ec696331l,0x9810e2de3e8d812fl,
        0xd0a47259389294abl },
      { 0x513ba2b50e3bab66l,0x462caff5abad306fl,0xe2dc6d59af04c49el,
        0x1aeb8750e0b84b0bl } },
    /* 46 << 98 */
    { { 0xc034f12f2f7d0ca2l,0x6d2e8128e06acf2fl,0x801f4f8321facc2fl,
        0xa1170c03f40ef607l },
      { 0xfe0a1d4f7805a99cl,0xbde56a36cc26aba5l,0x5b1629d035531f40l,
        0xac212c2b9afa6108l } },
    /* 47 << 98 */
    { { 0x30a06bf315697be5l,0x6f0545dc2c63c7c1l,0x5d8cb8427ccdadafl,
        0xd52e379bac7015bbl },
      { 0xc4f56147f462c23el,0xd44a429846bc24b0l,0xbc73d23ae2856d4fl,
        0x61cedd8c0832bcdfl } },
    /* 48 << 98 */
    { { 0x6095355699f241d7l,0xee4adbd7001a349dl,0x0b35bf6aaa89e491l,
        0x7f0076f4136f7546l },
      { 0xd19a18ba9264da3dl,0x6eb2d2cd62a7a28bl,0xcdba941f8761c971l,
        0x1550518ba3be4a5dl } },
    /* 49 << 98 */
    { { 0xd0e8e2f057d0b70cl,0xeea8612ecd133ba3l,0x814670f044416aecl,
        0x424db6c330775061l },
      { 0xd96039d116213fd1l,0xc61e7fa518a3478fl,0xa805bdcccb0c5021l,
        0xbdd6f3a80cc616ddl } },
    /* 50 << 98 */
    { { 0x060096675d97f7e2l,0x31db0fc1af0bf4b6l,0x23680ed45491627al,
        0xb99a3c667d741fb1l },
      { 0xe9bb5f5536b1ff92l,0x29738577512b388dl,0xdb8a2ce750fcf263l,
        0x385346d46c4f7b47l } },
    /* 51 << 98 */
    { { 0xbe86c5ef31631f9el,0xbf91da2103a57a29l,0xc3b1f7967b23f821l,
        0x0f7d00d2770db354l },
      { 0x8ffc6c3bd8fe79dal,0xcc5e8c40d525c996l,0x4640991dcfff632al,
        0x64d97e8c67112528l } },
    /* 52 << 98 */
    { { 0xc232d97302f1cd1el,0xce87eacb1dd212a4l,0x6e4c8c73e69802f7l,
        0x12ef02901fffddbdl },
      { 0x941ec74e1bcea6e2l,0xd0b540243cb92cbbl,0x809fb9d47e8f9d05l,
        0x3bf16159f2992aael } },
    /* 53 << 98 */
    { { 0xad40f279f8a7a838l,0x11aea63105615660l,0xbf52e6f1a01f6fa1l,
        0xef0469953dc2aec9l },
      { 0x785dbec9d8080711l,0xe1aec60a9fdedf76l,0xece797b5fa21c126l,
        0xc66e898f05e52732l } },
    /* 54 << 98 */
    { { 0x39bb69c408811fdbl,0x8bfe1ef82fc7f082l,0xc8e7a393174f4138l,
        0xfba8ad1dd58d1f98l },
      { 0xbc21d0cebfd2fd5bl,0x0b839a826ee60d61l,0xaacf7658afd22253l,
        0xb526bed8aae396b3l } },
    /* 55 << 98 */
    { { 0xccc1bbc238564464l,0x9e3ff9478c45bc73l,0xcde9bca358188a78l,
        0x138b8ee0d73bf8f7l },
      { 0x5c7e234c4123c489l,0x66e69368fa643297l,0x0629eeee39a15fa3l,
        0x95fab881a9e2a927l } },
    /* 56 << 98 */
    { { 0xb2497007eafbb1e1l,0xd75c9ce6e75b7a93l,0x3558352defb68d78l,
        0xa2f26699223f6396l },
      { 0xeb911ecfe469b17al,0x62545779e72d3ec2l,0x8ea47de782cb113fl,
        0xebe4b0864e1fa98dl } },
    /* 57 << 98 */
    { { 0xec2d5ed78cdfedb1l,0xa535c077fe211a74l,0x9678109b11d244c5l,
        0xf17c8bfbbe299a76l },
      { 0xb651412efb11fbc4l,0xea0b548294ab3f65l,0xd8dffd950cf78243l,
        0x2e719e57ce0361d4l } },
    /* 58 << 98 */
    { { 0x9007f085304ddc5bl,0x095e8c6d4daba2eal,0x5a33cdb43f9d28a9l,
        0x85b95cd8e2283003l },
      { 0xbcd6c819b9744733l,0x29c5f538fc7f5783l,0x6c49b2fad59038e4l,
        0x68349cc13bbe1018l } },
    /* 59 << 98 */
    { { 0xcc490c1d21830ee5l,0x36f9c4eee9bfa297l,0x58fd729448de1a94l,
        0xaadb13a84e8f2cdcl },
      { 0x515eaaa081313dbal,0xc76bb468c2152dd8l,0x357f8d75a653dbf8l,
        0xe4d8c4d1b14ac143l } },
    /* 60 << 98 */
    { { 0xbdb8e675b055cb40l,0x898f8e7b977b5167l,0xecc65651b82fb863l,
        0x565448146d88f01fl },
      { 0xb0928e95263a75a9l,0xcfb6836f1a22fcdal,0x651d14db3f3bd37cl,
        0x1d3837fbb6ad4664l } },
    /* 61 << 98 */
    { { 0x7c5fb538ff4f94abl,0x7243c7126d7fb8f2l,0xef13d60ca85c5287l,
        0x18cfb7c74bb8dd1bl },
      { 0x82f9bfe672908219l,0x35c4592b9d5144abl,0x52734f379cf4b42fl,
        0x6bac55e78c60ddc4l } },
    /* 62 << 98 */
    { { 0xb5cd811e94dea0f6l,0x259ecae4e18cc1a3l,0x6a0e836e15e660f8l,
        0x6c639ea60e02bff2l },
      { 0x8721b8cb7e1026fdl,0x9e73b50b63261942l,0xb8c7097477f01da3l,
        0x1839e6a68268f57fl } },
    /* 63 << 98 */
    { { 0x571b94155150b805l,0x1892389ef92c7097l,0x8d69c18e4a084b95l,
        0x7014c512be5b495cl },
      { 0x4780db361b07523cl,0x2f6219ce2c1c64fal,0xc38b81b0602c105al,
        0xab4f4f205dc8e360l } },
    /* 64 << 98 */
    { { 0x20d3c982cf7d62d2l,0x1f36e29d23ba8150l,0x48ae0bf092763f9el,
        0x7a527e6b1d3a7007l },
      { 0xb4a89097581a85e3l,0x1f1a520fdc158be5l,0xf98db37d167d726el,
        0x8802786e1113e862l } },
    /* 0 << 105 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 105 */
    { { 0xefb2149e36f09ab0l,0x03f163ca4a10bb5bl,0xd029704506e20998l,
        0x56f0af001b5a3babl },
      { 0x7af4cfec70880e0dl,0x7332a66fbe3d913fl,0x32e6c84a7eceb4bdl,
        0xedc4a79a9c228f55l } },
    /* 2 << 105 */
    { { 0xc37c7dd0c55c4496l,0xa6a9635725bbabd2l,0x5b7e63f2add7f363l,
        0x9dce37822e73f1dfl },
      { 0xe1e5a16ab2b91f71l,0xe44898235ba0163cl,0xf2759c32f6e515adl,
        0xa5e2f1f88615eecfl } },
    /* 3 << 105 */
    { { 0x74519be7abded551l,0x03d358b8c8b74410l,0x4d00b10b0e10d9a9l,
        0x6392b0b128da52b7l },
      { 0x6744a2980b75c904l,0xc305b0aea8f7f96cl,0x042e421d182cf932l,
        0xf6fc5d509e4636cal } },
    /* 4 << 105 */
    { { 0x795847c9d64cc78cl,0x6c50621b9b6cb27bl,0x07099bf8df8022abl,
        0x48f862ebc04eda1dl },
      { 0xd12732ede1603c16l,0x19a80e0f5c9a9450l,0xe2257f54b429b4fcl,
        0x66d3b2c645460515l } },
    /* 5 << 105 */
    { { 0x6ca4f87e822e37bel,0x73f237b4253bda4el,0xf747f3a241190aebl,
        0xf06fa36f804cf284l },
      { 0x0a6bbb6efc621c12l,0x5d624b6440b80ec6l,0x4b0724257ba556f3l,
        0x7fa0c3543e2d20a8l } },
    /* 6 << 105 */
    { { 0xe921fa31e3229d41l,0xa929c65294531bd4l,0x84156027a6d38209l,
        0xf3d69f736bdb97bdl },
      { 0x8906d19a16833631l,0x68a34c2e03d51be3l,0xcb59583b0e511cd8l,
        0x99ce6bfdfdc132a8l } },
    /* 7 << 105 */
    { { 0x3facdaaaffcdb463l,0x658bbc1a34a38b08l,0x12a801f8f1a9078dl,
        0x1567bcf96ab855del },
      { 0xe08498e03572359bl,0xcf0353e58659e68bl,0xbb86e9c87d23807cl,
        0xbc08728d2198e8a2l } },
    /* 8 << 105 */
    { { 0x8de2b7bc453cadd6l,0x203900a7bc0bc1f8l,0xbcd86e47a6abd3afl,
        0x911cac128502effbl },
      { 0x2d550242ec965469l,0x0e9f769229e0017el,0x633f078f65979885l,
        0xfb87d4494cf751efl } },
    /* 9 << 105 */
    { { 0xe1790e4bfc25419al,0x364672034bff3cfdl,0xc8db638625b6e83fl,
        0x6cc69f236cad6fd2l },
      { 0x0219e45a6bc68bb9l,0xe43d79b6297f7334l,0x7d445368465dc97cl,
        0x4b9eea322a0b949al } },
    /* 10 << 105 */
    { { 0x1b96c6ba6102d021l,0xeaafac782f4461eal,0xd4b85c41c49f19a8l,
        0x275c28e4cf538875l },
      { 0x35451a9ddd2e54e0l,0x6991adb50605618bl,0x5b8b4bcd7b36cd24l,
        0x372a4f8c56f37216l } },
    /* 11 << 105 */
    { { 0xc890bd73a6a5da60l,0x6f083da0dc4c9ff0l,0xf4e14d94f0536e57l,
        0xf9ee1edaaaec8243l },
      { 0x571241ec8bdcf8e7l,0xa5db82710b041e26l,0x9a0b9a99e3fff040l,
        0xcaaf21dd7c271202l } },
    /* 12 << 105 */
    { { 0xb4e2b2e14f0dd2e8l,0xe77e7c4f0a377ac7l,0x69202c3f0d7a2198l,
        0xf759b7ff28200eb8l },
      { 0xc87526eddcfe314el,0xeb84c52453d5cf99l,0xb1b52ace515138b6l,
        0x5aa7ff8c23fca3f4l } },
    /* 13 << 105 */
    { { 0xff0b13c3b9791a26l,0x960022dacdd58b16l,0xdbd55c9257aad2del,
        0x3baaaaa3f30fe619l },
      { 0x9a4b23460d881efdl,0x506416c046325e2al,0x91381e76035c18d4l,
        0xb3bb68bef27817b0l } },
    /* 14 << 105 */
    { { 0x15bfb8bf5116f937l,0x7c64a586c1268943l,0x71e25cc38419a2c8l,
        0x9fd6b0c48335f463l },
      { 0x4bf0ba3ce8ee0e0el,0x6f6fba60298c21fal,0x57d57b39ae66bee0l,
        0x292d513022672544l } },
    /* 15 << 105 */
    { { 0xf451105dbab093b3l,0x012f59b902839986l,0x8a9158023474a89cl,
        0x048c919c2de03e97l },
      { 0xc476a2b591071cd5l,0x791ed89a034970a5l,0x89bd9042e1b7994bl,
        0x8eaf5179a1057ffdl } },
    /* 16 << 105 */
    { { 0x6066e2a2d551ee10l,0x87a8f1d8727e09a6l,0x00d08bab2c01148dl,
        0x6da8e4f1424f33fel },
      { 0x466d17f0cf9a4e71l,0xff5020103bf5cb19l,0xdccf97d8d062ecc0l,
        0x80c0d9af81d80ac4l } },
    /* 17 << 105 */
    { { 0xe87771d8033f2876l,0xb0186ec67d5cc3dbl,0x58e8bb803bc9bc1dl,
        0x4d1395cc6f6ef60el },
      { 0xa73c62d6186244a0l,0x918e5f23110a5b53l,0xed4878ca741b7eabl,
        0x3038d71adbe03e51l } },
    /* 18 << 105 */
    { { 0x840204b7a93c3246l,0x21ab6069a0b9b4cdl,0xf5fa6e2bb1d64218l,
        0x1de6ad0ef3d56191l },
      { 0x570aaa88ff1929c7l,0xc6df4c6b640e87b5l,0xde8a74f2c65f0cccl,
        0x8b972fd5e6f6cc01l } },
    /* 19 << 105 */
    { { 0x3fff36b60b846531l,0xba7e45e610a5e475l,0x84a1d10e4145b6c5l,
        0xf1f7f91a5e046d9dl },
      { 0x0317a69244de90d7l,0x951a1d4af199c15el,0x91f78046c9d73debl,
        0x74c82828fab8224fl } },
    /* 20 << 105 */
    { { 0xaa6778fce7560b90l,0xb4073e61a7e824cel,0xff0d693cd642eba8l,
        0x7ce2e57a5dccef38l },
      { 0x89c2c7891df1ad46l,0x83a06922098346fdl,0x2d715d72da2fc177l,
        0x7b6dd71d85b6cf1dl } },
    /* 21 << 105 */
    { { 0xc60a6d0a73fa9cb0l,0xedd3992e328bf5a9l,0xc380ddd0832c8c82l,
        0xd182d410a2a0bf50l },
      { 0x7d9d7438d9a528dbl,0xe8b1a0e9caf53994l,0xddd6e5fe0e19987cl,
        0xacb8df03190b059dl } },
    /* 22 << 105 */
    { { 0x53703a328300129fl,0x1f63766268c43bfdl,0xbcbd191300e54051l,
        0x812fcc627bf5a8c5l },
      { 0x3f969d5f29fb85dal,0x72f4e00a694759e8l,0x426b6e52790726b7l,
        0x617bbc873bdbb209l } },
    /* 23 << 105 */
    { { 0x511f8bb997aee317l,0x812a4096e81536a8l,0x137dfe593ac09b9bl,
        0x0682238fba8c9a7al },
      { 0x7072ead6aeccb4bdl,0x6a34e9aa692ba633l,0xc82eaec26fff9d33l,
        0xfb7535121d4d2b62l } },
    /* 24 << 105 */
    { { 0x1a0445ff1d7aadabl,0x65d38260d5f6a67cl,0x6e62fb0891cfb26fl,
        0xef1e0fa55c7d91d6l },
      { 0x47e7c7ba33db72cdl,0x017cbc09fa7c74b2l,0x3c931590f50a503cl,
        0xcac54f60616baa42l } },
    /* 25 << 105 */
    { { 0x9b6cd380b2369f0fl,0x97d3a70d23c76151l,0x5f9dd6fc9862a9c6l,
        0x044c4ab212312f51l },
      { 0x035ea0fd834a2ddcl,0x49e6b862cc7b826dl,0xb03d688362fce490l,
        0x62f2497ab37e36e9l } },
    /* 26 << 105 */
    { { 0x04b005b6c6458293l,0x36bb5276e8d10af7l,0xacf2dc138ee617b8l,
        0x470d2d35b004b3d4l },
      { 0x06790832feeb1b77l,0x2bb75c3985657f9cl,0xd70bd4edc0f60004l,
        0xfe797ecc219b018bl } },
    /* 27 << 105 */
    { { 0x9b5bec2a753aebccl,0xdaf9f3dcc939eca5l,0xd6bc6833d095ad09l,
        0x98abdd51daa4d2fcl },
      { 0xd9840a318d168be5l,0xcf7c10e02325a23cl,0xa5c02aa07e6ecfafl,
        0x2462e7e6b5bfdf18l } },
    /* 28 << 105 */
    { { 0xab2d8a8ba0cc3f12l,0x68dd485dbc672a29l,0x72039752596f2cd3l,
        0x5d3eea67a0cf3d8dl },
      { 0x810a1a81e6602671l,0x8f144a4014026c0cl,0xbc753a6d76b50f85l,
        0xc4dc21e8645cd4a4l } },
    /* 29 << 105 */
    { { 0xc5262dea521d0378l,0x802b8e0e05011c6fl,0x1ba19cbb0b4c19eal,
        0x21db64b5ebf0aaecl },
      { 0x1f394ee970342f9dl,0x93a10aee1bc44a14l,0xa7eed31b3efd0baal,
        0x6e7c824e1d154e65l } },
    /* 30 << 105 */
    { { 0xee23fa819966e7eel,0x64ec4aa805b7920dl,0x2d44462d2d90aad4l,
        0xf44dd195df277ad5l },
      { 0x8d6471f1bb46b6a1l,0x1e65d313fd885090l,0x33a800f513a977b4l,
        0xaca9d7210797e1efl } },
    /* 31 << 105 */
    { { 0x9a5a85a0fcff6a17l,0x9970a3f31eca7ceel,0xbb9f0d6bc9504be3l,
        0xe0c504beadd24ee2l },
      { 0x7e09d95677fcc2f4l,0xef1a522765bb5fc4l,0x145d4fb18b9286aal,
        0x66fd0c5d6649028bl } },
    /* 32 << 105 */
    { { 0x98857ceb1bf4581cl,0xe635e186aca7b166l,0x278ddd22659722acl,
        0xa0903c4c1db68007l },
      { 0x366e458948f21402l,0x31b49c14b96abda2l,0x329c4b09e0403190l,
        0x97197ca3d29f43fel } },
    /* 33 << 105 */
    { { 0x8073dd1e274983d8l,0xda1a3bde55717c8fl,0xfd3d4da20361f9d1l,
        0x1332d0814c7de1cel },
      { 0x9b7ef7a3aa6d0e10l,0x17db2e73f54f1c4al,0xaf3dffae4cd35567l,
        0xaaa2f406e56f4e71l } },
    /* 34 << 105 */
    { { 0x8966759e7ace3fc7l,0x9594eacf45a8d8c6l,0x8de3bd8b91834e0el,
        0xafe4ca53548c0421l },
      { 0xfdd7e856e6ee81c6l,0x8f671beb6b891a3al,0xf7a58f2bfae63829l,
        0x9ab186fb9c11ac9fl } },
    /* 35 << 105 */
    { { 0x8d6eb36910b5be76l,0x046b7739fb040bcdl,0xccb4529fcb73de88l,
        0x1df0fefccf26be03l },
      { 0xad7757a6bcfcd027l,0xa8786c75bb3165cal,0xe9db1e347e99a4d9l,
        0x99ee86dfb06c504bl } },
    /* 36 << 105 */
    { { 0x5b7c2dddc15c9f0al,0xdf87a7344295989el,0x59ece47c03d08fdal,
        0xb074d3ddad5fc702l },
      { 0x2040790351a03776l,0x2bb1f77b2a608007l,0x25c58f4fe1153185l,
        0xe6df62f6766e6447l } },
    /* 37 << 105 */
    { { 0xefb3d1beed51275al,0x5de47dc72f0f483fl,0x7932d98e97c2bedfl,
        0xd5c119270219f8a1l },
      { 0x9d751200a73a294el,0x5f88434a9dc20172l,0xd28d9fd3a26f506al,
        0xa890cd319d1dcd48l } },
    /* 38 << 105 */
    { { 0x0aebaec170f4d3b4l,0xfd1a13690ffc8d00l,0xb9d9c24057d57838l,
        0x45929d2668bac361l },
      { 0x5a2cd06025b15ca6l,0x4b3c83e16e474446l,0x1aac7578ee1e5134l,
        0xa418f5d6c91e2f41l } },
    /* 39 << 105 */
    { { 0x6936fc8a213ed68bl,0x860ae7ed510a5224l,0x63660335def09b53l,
        0x641b2897cd79c98dl },
      { 0x29bd38e101110f35l,0x79c26f42648b1937l,0x64dae5199d9164f4l,
        0xd85a23100265c273l } },
    /* 40 << 105 */
    { { 0x7173dd5d4b07e2b1l,0xd144c4cb8d9ea221l,0xe8b04ea41105ab14l,
        0x92dda542fe80d8f1l },
      { 0xe9982fa8cf03dce6l,0x8b5ea9651a22cffcl,0xf7f4ea7f3fad88c4l,
        0x62db773e6a5ba95cl } },
    /* 41 << 105 */
    { { 0xd20f02fb93f24567l,0xfd46c69a315257cal,0x0ac74cc78bcab987l,
        0x46f31c015ceca2f5l },
      { 0x40aedb59888b219el,0xe50ecc37e1fccd02l,0x1bcd9dad911f816cl,
        0x583cc1ec8db9b00cl } },
    /* 42 << 105 */
    { { 0xf3cd2e66a483bf11l,0xfa08a6f5b1b2c169l,0xf375e2454be9fa28l,
        0x99a7ffec5b6d011fl },
      { 0x6a3ebddbc4ae62dal,0x6cea00ae374aef5dl,0xab5fb98d9d4d05bcl,
        0x7cba1423d560f252l } },
    /* 43 << 105 */
    { { 0x49b2cc21208490del,0x1ca66ec3bcfb2879l,0x7f1166b71b6fb16fl,
        0xfff63e0865fe5db3l },
      { 0xb8345abe8b2610bel,0xb732ed8039de3df4l,0x0e24ed50211c32b4l,
        0xd10d8a69848ff27dl } },
    /* 44 << 105 */
    { { 0xc1074398ed4de248l,0xd7cedace10488927l,0xa4aa6bf885673e13l,
        0xb46bae916daf30afl },
      { 0x07088472fcef7ad8l,0x61151608d4b35e97l,0xbcfe8f26dde29986l,
        0xeb84c4c7d5a34c79l } },
    /* 45 << 105 */
    { { 0xc1eec55c164e1214l,0x891be86da147bb03l,0x9fab4d100ba96835l,
        0xbf01e9b8a5c1ae9fl },
      { 0x6b4de139b186ebc0l,0xd5c74c2685b91bcal,0x5086a99cc2d93854l,
        0xeed62a7ba7a9dfbcl } },
    /* 46 << 105 */
    { { 0x8778ed6f76b7618al,0xbff750a503b66062l,0x4cb7be22b65186dbl,
        0x369dfbf0cc3a6d13l },
      { 0xc7dab26c7191a321l,0x9edac3f940ed718el,0xbc142b36d0cfd183l,
        0xc8af82f67c991693l } },
    /* 47 << 105 */
    { { 0xb3d1e4d897ce0b2al,0xe6d7c87fc3a55cdfl,0x35846b9568b81afel,
        0x018d12afd3c239d8l },
      { 0x2b2c620801206e15l,0xe0e42453a3b882c6l,0x854470a3a50162d5l,
        0x081574787017a62al } },
    /* 48 << 105 */
    { { 0x18bd3fb4820357c7l,0x992039ae6f1458adl,0x9a1df3c525b44aa1l,
        0x2d780357ed3d5281l },
      { 0x58cf7e4dc77ad4d4l,0xd49a7998f9df4fc4l,0x4465a8b51d71205el,
        0xa0ee0ea6649254aal } },
    /* 49 << 105 */
    { { 0x4b5eeecfab7bd771l,0x6c87307335c262b9l,0xdc5bd6483c9d61e7l,
        0x233d6d54321460d2l },
      { 0xd20c5626fc195bccl,0x2544595804d78b63l,0xe03fcb3d17ec8ef3l,
        0x54b690d146b8f781l } },
    /* 50 << 105 */
    { { 0x82fa2c8a21230646l,0xf51aabb9084f418cl,0xff4fbec11a30ba43l,
        0x6a5acf73743c9df7l },
      { 0x1da2b357d635b4d5l,0xc3de68ddecd5c1dal,0xa689080bd61af0ddl,
        0xdea5938ad665bf99l } },
    /* 51 << 105 */
    { { 0x0231d71afe637294l,0x01968aa6a5a81cd8l,0x11252d50048e63b5l,
        0xc446bc526ca007e9l },
      { 0xef8c50a696d6134bl,0x9361fbf59e09a05cl,0xf17f85a6dca3291al,
        0xb178d548ff251a21l } },
    /* 52 << 105 */
    { { 0x87f6374ba4df3915l,0x566ce1bf2fd5d608l,0x425cba4d7de35102l,
        0x6b745f8f58c5d5e2l },
      { 0x88402af663122edfl,0x3190f9ed3b989a89l,0x4ad3d387ebba3156l,
        0xef385ad9c7c469a5l } },
    /* 53 << 105 */
    { { 0xb08281de3f642c29l,0x20be0888910ffb88l,0xf353dd4ad5292546l,
        0x3f1627de8377a262l },
      { 0xa5faa013eefcd638l,0x8f3bf62674cc77c3l,0x32618f65a348f55el,
        0x5787c0dc9fefeb9el } },
    /* 54 << 105 */
    { { 0xf1673aa2d9a23e44l,0x88dfa9934e10690dl,0x1ced1b362bf91108l,
        0x9193ceca3af48649l },
      { 0xfb34327d2d738fc5l,0x6697b037975fee6cl,0x2f485da0c04079a5l,
        0x2cdf57352feaa1acl } },
    /* 55 << 105 */
    { { 0x76944420bd55659el,0x7973e32b4376090cl,0x86bb4fe1163b591al,
        0x10441aedc196f0cal },
      { 0x3b431f4a045ad915l,0x6c11b437a4afacb1l,0x30b0c7db71fdbbd8l,
        0xb642931feda65acdl } },
    /* 56 << 105 */
    { { 0x4baae6e89c92b235l,0xa73bbd0e6b3993a1l,0xd06d60ec693dd031l,
        0x03cab91b7156881cl },
      { 0xd615862f1db3574bl,0x485b018564bb061al,0x27434988a0181e06l,
        0x2cd61ad4c1c0c757l } },
    /* 57 << 105 */
    { { 0x3effed5a2ff9f403l,0x8dc98d8b62239029l,0x2206021e1f17b70dl,
        0xafbec0cabf510015l },
      { 0x9fed716480130dfal,0x306dc2b58a02dcf5l,0x48f06620feb10fc0l,
        0x78d1e1d55a57cf51l } },
    /* 58 << 105 */
    { { 0xadef8c5a192ef710l,0x88afbd4b3b7431f9l,0x7e1f740764250c9el,
        0x6e31318db58bec07l },
      { 0xfd4fc4b824f89b4el,0x65a5dd8848c36a2al,0x4f1eccfff024baa7l,
        0x22a21cf2cba94650l } },
    /* 59 << 105 */
    { { 0x95d29dee42a554f7l,0x828983a5002ec4bal,0x8112a1f78badb73dl,
        0x79ea8897a27c1839l },
      { 0x8969a5a7d065fd83l,0xf49af791b262a0bcl,0xfcdea8b6af2b5127l,
        0x10e913e1564c2dbcl } },
    /* 60 << 105 */
    { { 0x51239d14bc21ef51l,0xe51c3ceb4ce57292l,0x795ff06847bbcc3bl,
        0x86b46e1ebd7e11e6l },
      { 0x0ea6ba2380041ef4l,0xd72fe5056262342el,0x8abc6dfd31d294d4l,
        0xbbe017a21278c2c9l } },
    /* 61 << 105 */
    { { 0xb1fcfa09b389328al,0x322fbc62d01771b5l,0x04c0d06360b045bfl,
        0xdb652edc10e52d01l },
      { 0x50ef932c03ec6627l,0xde1b3b2dc1ee50e3l,0x5ab7bdc5dc37a90dl,
        0xfea6721331e33a96l } },
    /* 62 << 105 */
    { { 0x6482b5cb4f2999aal,0x38476cc6b8cbf0ddl,0x93ebfacb173405bbl,
        0x15cdafe7e52369ecl },
      { 0xd42d5ba4d935b7dbl,0x648b60041c99a4cdl,0x785101bda3b5545bl,
        0x4bf2c38a9dd67fafl } },
    /* 63 << 105 */
    { { 0xb1aadc634442449cl,0xe0e9921a33ad4fb8l,0x5c552313aa686d82l,
        0xdee635fa465d866cl },
      { 0xbc3c224a18ee6e8al,0xeed748a6ed42e02fl,0xe70f930ad474cd08l,
        0x774ea6ecfff24adfl } },
    /* 64 << 105 */
    { { 0x03e2de1cf3480d4al,0xf0d8edc7bc8acf1al,0xf23e330368295a9cl,
        0xfadd5f68c546a97dl },
      { 0x895597ad96f8acb1l,0xbddd49d5671bdae2l,0x16fcd52821dd43f4l,
        0xa5a454126619141al } },
    /* 0 << 112 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 112 */
    { { 0x8ce9b6bfc360e25al,0xe6425195075a1a78l,0x9dc756a8481732f4l,
        0x83c0440f5432b57al },
      { 0xc670b3f1d720281fl,0x2205910ed135e051l,0xded14b0edb052be7l,
        0x697b3d27c568ea39l } },
    /* 2 << 112 */
    { { 0x2e599b9afb3ff9edl,0x28c2e0ab17f6515cl,0x1cbee4fd474da449l,
        0x071279a44f364452l },
      { 0x97abff6601fbe855l,0x3ee394e85fda51c4l,0x190385f667597c0bl,
        0x6e9fccc6a27ee34bl } },
    /* 3 << 112 */
    { { 0x0b89de9314092ebbl,0xf17256bd428e240cl,0xcf89a7f393d2f064l,
        0x4f57841ee1ed3b14l },
      { 0x4ee14405e708d855l,0x856aae7203f1c3d0l,0xc8e5424fbdd7eed5l,
        0x3333e4ef73ab4270l } },
    /* 4 << 112 */
    { { 0x3bc77adedda492f8l,0xc11a3aea78297205l,0x5e89a3e734931b4cl,
        0x17512e2e9f5694bbl },
      { 0x5dc349f3177bf8b6l,0x232ea4ba08c7ff3el,0x9c4f9d16f511145dl,
        0xccf109a333b379c3l } },
    /* 5 << 112 */
    { { 0xe75e7a88a1f25897l,0x7ac6961fa1b5d4d8l,0xe3e1077308f3ed5cl,
        0x208a54ec0a892dfbl },
      { 0xbe826e1978660710l,0x0cf70a97237df2c8l,0x418a7340ed704da5l,
        0xa3eeb9a908ca33fdl } },
    /* 6 << 112 */
    { { 0x49d96233169bca96l,0x04d286d42da6aafbl,0xc09606eca0c2fa94l,
        0x8869d0d523ff0fb3l },
      { 0xa99937e5d0150d65l,0xa92e2503240c14c9l,0x656bf945108e2d49l,
        0x152a733aa2f59e2bl } },
    /* 7 << 112 */
    { { 0xb4323d588434a920l,0xc0af8e93622103c5l,0x667518ef938dbf9al,
        0xa184307383a9cdf2l },
      { 0x350a94aa5447ab80l,0xe5e5a325c75a3d61l,0x74ba507f68411a9el,
        0x10581fc1594f70c5l } },
    /* 8 << 112 */
    { { 0x60e2857080eb24a9l,0x7bedfb4d488e0cfdl,0x721ebbd7c259cdb8l,
        0x0b0da855bc6390a9l },
      { 0x2b4d04dbde314c70l,0xcdbf1fbc6c32e846l,0x33833eabb162fc9el,
        0x9939b48bb0dd3ab7l } },
    /* 9 << 112 */
    { { 0x5aaa98a7cb0c9c8cl,0x75105f3081c4375cl,0xceee50575ef1c90fl,
        0xb31e065fc23a17bfl },
      { 0x5364d275d4b6d45al,0xd363f3ad62ec8996l,0xb5d212394391c65bl,
        0x84564765ebb41b47l } },
    /* 10 << 112 */
    { { 0x20d18ecc37107c78l,0xacff3b6b570c2a66l,0x22f975d99bd0d845l,
        0xef0a0c46ba178fa0l },
      { 0x1a41965176b6028el,0xc49ec674248612d4l,0x5b6ac4f27338af55l,
        0x06145e627bee5a36l } },
    /* 11 << 112 */
    { { 0x33e95d07e75746b5l,0x1c1e1f6dc40c78bel,0x967833ef222ff8e2l,
        0x4bedcf6ab49180adl },
      { 0x6b37e9c13d7a4c8al,0x2748887c6ddfe760l,0xf7055123aa3a5bbcl,
        0x954ff2257bbb8e74l } },
    /* 12 << 112 */
    { { 0xc42b8ab197c3dfb9l,0x55a549b0cf168154l,0xad6748e7c1b50692l,
        0x2775780f6fc5cbcbl },
      { 0x4eab80b8e1c9d7c8l,0x8c69dae13fdbcd56l,0x47e6b4fb9969eacel,
        0x002f1085a705cb5al } },
    /* 13 << 112 */
    { { 0x4e23ca446d3fea55l,0xb4ae9c86f4810568l,0x47bfb91b2a62f27dl,
        0x60deb4c9d9bac28cl },
      { 0xa892d8947de6c34cl,0x4ee682594494587dl,0x914ee14e1a3f8a5bl,
        0xbb113eaa28700385l } },
    /* 14 << 112 */
    { { 0x81ca03b92115b4c9l,0x7c163d388908cad1l,0xc912a118aa18179al,
        0xe09ed750886e3081l },
      { 0xa676e3fa26f516cal,0x753cacf78e732f91l,0x51592aea833da8b4l,
        0xc626f42f4cbea8aal } },
    /* 15 << 112 */
    { { 0xef9dc899a7b56eafl,0x00c0e52c34ef7316l,0x5b1e4e24fe818a86l,
        0x9d31e20dc538be47l },
      { 0x22eb932d3ed68974l,0xe44bbc087c4e87c4l,0x4121086e0dde9aefl,
        0x8e6b9cff134f4345l } },
    /* 16 << 112 */
    { { 0x96892c1f711b0eb9l,0xb905f2c8780ab954l,0xace26309a20792dbl,
        0xec8ac9b30684e126l },
      { 0x486ad8b6b40a2447l,0x60121fc19fe3fb24l,0x5626fccf1a8e3b3fl,
        0x4e5686226ad1f394l } },
    /* 17 << 112 */
    { { 0xda7aae0d196aa5a1l,0xe0df8c771041b5fbl,0x451465d926b318b7l,
        0xc29b6e557ab136e9l },
      { 0x2c2ab48b71148463l,0xb5738de364454a76l,0x54ccf9a05a03abe4l,
        0x377c02960427d58el } },
    /* 18 << 112 */
    { { 0x73f5f0b92bb39c1fl,0x14373f2ce608d8c5l,0xdcbfd31400fbb805l,
        0xdf18fb2083afdcfbl },
      { 0x81a57f4242b3523fl,0xe958532d87f650fbl,0xaa8dc8b68b0a7d7cl,
        0x1b75dfb7150166bel } },
    /* 19 << 112 */
    { { 0x90e4f7c92d7d1413l,0x67e2d6b59834f597l,0x4fd4f4f9a808c3e8l,
        0xaf8237e0d5281ec1l },
      { 0x25ab5fdc84687ceel,0xc5ded6b1a5b26c09l,0x8e4a5aecc8ea7650l,
        0x23b73e5c14cc417fl } },
    /* 20 << 112 */
    { { 0x2bfb43183037bf52l,0xb61e6db578c725d7l,0x8efd4060bbb3e5d7l,
        0x2e014701dbac488el },
      { 0xac75cf9a360aa449l,0xb70cfd0579634d08l,0xa591536dfffb15efl,
        0xb2c37582d07c106cl } },
    /* 21 << 112 */
    { { 0xb4293fdcf50225f9l,0xc52e175cb0e12b03l,0xf649c3bad0a8bf64l,
        0x745a8fefeb8ae3c6l },
      { 0x30d7e5a358321bc3l,0xb1732be70bc4df48l,0x1f217993e9ea5058l,
        0xf7a71cde3e4fd745l } },
    /* 22 << 112 */
    { { 0x86cc533e894c5bbbl,0x6915c7d969d83082l,0xa6aa2d055815c244l,
        0xaeeee59249b22ce5l },
      { 0x89e39d1378135486l,0x3a275c1f16b76f2fl,0xdb6bcc1be036e8f5l,
        0x4df69b215e4709f5l } },
    /* 23 << 112 */
    { { 0xa188b2502d0f39aal,0x622118bb15a85947l,0x2ebf520ffde0f4fal,
        0xa40e9f294860e539l },
      { 0x7b6a51eb22b57f0fl,0x849a33b97e80644al,0x50e5d16f1cf095fel,
        0xd754b54eec55f002l } },
    /* 24 << 112 */
    { { 0x5cfbbb22236f4a98l,0x0b0c59e9066800bbl,0x4ac69a8f5a9a7774l,
        0x2b33f804d6bec948l },
      { 0xb372929532e6c466l,0x68956d0f4e599c73l,0xa47a249f155c31ccl,
        0x24d80f0de1ce284el } },
    /* 25 << 112 */
    { { 0xcd821dfb988baf01l,0xe6331a7ddbb16647l,0x1eb8ad33094cb960l,
        0x593cca38c91bbca5l },
      { 0x384aac8d26567456l,0x40fa0309c04b6490l,0x97834cd6dab6c8f6l,
        0x68a7318d3f91e55fl } },
    /* 26 << 112 */
    { { 0xa00fd04efc4d3157l,0xb56f8ab22bf3bdeal,0x014f56484fa57172l,
        0x948c5860450abdb3l },
      { 0x342b5df00ebd4f08l,0x3e5168cd0e82938el,0x7aedc1ceb0df5dd0l,
        0x6bbbc6d9e5732516l } },
    /* 27 << 112 */
    { { 0xc7bfd486605daaa6l,0x46fd72b7bb9a6c9el,0xe4847fb1a124fb89l,
        0x75959cbda2d8ffbcl },
      { 0x42579f65c8a588eel,0x368c92e6b80b499dl,0xea4ef6cd999a5df1l,
        0xaa73bb7f936fe604l } },
    /* 28 << 112 */
    { { 0xf347a70d6457d188l,0x86eda86b8b7a388bl,0xb7cdff060ccd6013l,
        0xbeb1b6c7d0053fb2l },
      { 0x0b02238799240a9fl,0x1bbb384f776189b2l,0x8695e71e9066193al,
        0x2eb5009706ffac7el } },
    /* 29 << 112 */
    { { 0x0654a9c04a7d2caal,0x6f3fb3d1a5aaa290l,0x835db041ff476e8fl,
        0x540b8b0bc42295e4l },
      { 0xa5c73ac905e214f5l,0x9a74075a56a0b638l,0x2e4b1090ce9e680bl,
        0x57a5b4796b8d9afal } },
    /* 30 << 112 */
    { { 0x0dca48e726bfe65cl,0x097e391c7290c307l,0x683c462e6669e72el,
        0xf505be1e062559acl },
      { 0x5fbe3ea1e3a3035al,0x6431ebf69cd50da8l,0xfd169d5c1f6407f2l,
        0x8d838a9560fce6b8l } },
    /* 31 << 112 */
    { { 0x2a2bfa7f650006f0l,0xdfd7dad350c0fbb2l,0x92452495ccf9ad96l,
        0x183bf494d95635f9l },
      { 0x02d5df434a7bd989l,0x505385cca5431095l,0xdd98e67dfd43f53el,
        0xd61e1a6c500c34a9l } },
    /* 32 << 112 */
    { { 0x5a4b46c64a8a3d62l,0x8469c4d0247743d2l,0x2bb3a13d88f7e433l,
        0x62b23a1001be5849l },
      { 0xe83596b4a63d1a4cl,0x454e7fea7d183f3el,0x643fce6117afb01cl,
        0x4e65e5e61c4c3638l } },
    /* 33 << 112 */
    { { 0x41d85ea1ef74c45bl,0x2cfbfa66ae328506l,0x98b078f53ada7da9l,
        0xd985fe37ec752fbbl },
      { 0xeece68fe5a0148b4l,0x6f9a55c72d78136dl,0x232dccc4d2b729cel,
        0xa27e0dfd90aafbc4l } },
    /* 34 << 112 */
    { { 0x9647445212b4603el,0xa876c5516b706d14l,0xdf145fcf69a9d412l,
        0xe2ab75b72d479c34l },
      { 0x12df9a761a23ff97l,0xc61389925d359d10l,0x6e51c7aefa835f22l,
        0x69a79cb1c0fcc4d9l } },
    /* 35 << 112 */
    { { 0xf57f350d594cc7e1l,0x3079ca633350ab79l,0x226fb6149aff594al,
        0x35afec026d59a62bl },
      { 0x9bee46f406ed2c6el,0x58da17357d939a57l,0x44c504028fd1797el,
        0xd8853e7c5ccea6cal } },
    /* 36 << 112 */
    { { 0x4065508da35fcd5fl,0x8965df8c495ccaebl,0x0f2da85012e1a962l,
        0xee471b94c1cf1cc4l },
      { 0xcef19bc80a08fb75l,0x704958f581de3591l,0x2867f8b23aef4f88l,
        0x8d749384ea9f9a5fl } },
    /* 37 << 112 */
    { { 0x1b3855378c9049f4l,0x5be948f37b92d8b6l,0xd96f725db6e2bd6bl,
        0x37a222bc958c454dl },
      { 0xe7c61abb8809bf61l,0x46f07fbc1346f18dl,0xfb567a7ae87c0d1cl,
        0x84a461c87ef3d07al } },
    /* 38 << 112 */
    { { 0x0a5adce6d9278d98l,0x24d948139dfc73e1l,0x4f3528b6054321c3l,
        0x2e03fdde692ea706l },
      { 0x10e6061947b533c0l,0x1a8bc73f2ca3c055l,0xae58d4b21bb62b8fl,
        0xb2045a73584a24e3l } },
    /* 39 << 112 */
    { { 0x3ab3d5afbd76e195l,0x478dd1ad6938a810l,0x6ffab3936ee3d5cbl,
        0xdfb693db22b361e4l },
      { 0xf969449651dbf1a7l,0xcab4b4ef08a2e762l,0xe8c92f25d39bba9al,
        0x850e61bcf1464d96l } },
    /* 40 << 112 */
    { { 0xb7e830e3dc09508bl,0xfaf6d2cf74317655l,0x72606cebdf690355l,
        0x48bb92b3d0c3ded6l },
      { 0x65b754845c7cf892l,0xf6cd7ac9d5d5f01fl,0xc2c30a5996401d69l,
        0x91268650ed921878l } },
    /* 41 << 112 */
    { { 0x380bf913b78c558fl,0x43c0baebc8afdaa9l,0x377f61d554f169d3l,
        0xf8da07e3ae5ff20bl },
      { 0xb676c49da8a90ea8l,0x81c1ff2b83a29b21l,0x383297ac2ad8d276l,
        0x3001122fba89f982l } },
    /* 42 << 112 */
    { { 0xe1d794be6718e448l,0x246c14827c3e6e13l,0x56646ef85d26b5efl,
        0x80f5091e88069cddl },
      { 0xc5992e2f724bdd38l,0x02e915b48471e8c7l,0x96ff320a0d0ff2a9l,
        0xbf8864874384d1a0l } },
    /* 43 << 112 */
    { { 0xbbe1e6a6c93f72d6l,0xd5f75d12cad800eal,0xfa40a09fe7acf117l,
        0x32c8cdd57581a355l },
      { 0x742219927023c499l,0xa8afe5d738ec3901l,0x5691afcba90e83f0l,
        0x41bcaa030b8f8eacl } },
    /* 44 << 112 */
    { { 0xe38b5ff98d2668d5l,0x0715281a7ad81965l,0x1bc8fc7c03c6ce11l,
        0xcbbee6e28b650436l },
      { 0x06b00fe80cdb9808l,0x17d6e066fe3ed315l,0x2e9d38c64d0b5018l,
        0xab8bfd56844dcaefl } },
    /* 45 << 112 */
    { { 0x42894a59513aed8bl,0xf77f3b6d314bd07al,0xbbdecb8f8e42b582l,
        0xf10e2fa8d2390fe6l },
      { 0xefb9502262a2f201l,0x4d59ea5050ee32b0l,0xd87f77286da789a8l,
        0xcf98a2cff79492c4l } },
    /* 46 << 112 */
    { { 0xf9577239720943c2l,0xba044cf53990b9d0l,0x5aa8e82395f2884al,
        0x834de6ed0278a0afl },
      { 0xc8e1ee9a5f25bd12l,0x9259ceaa6f7ab271l,0x7e6d97a277d00b76l,
        0x5c0c6eeaa437832al } },
    /* 47 << 112 */
    { { 0x5232c20f5606b81dl,0xabd7b3750d991ee5l,0x4d2bfe358632d951l,
        0x78f8514698ed9364l },
      { 0x951873f0f30c3282l,0x0da8ac80a789230bl,0x3ac7789c5398967fl,
        0xa69b8f7fbdda0fb5l } },
    /* 48 << 112 */
    { { 0xe5db77176add8545l,0x1b71cb6672c49b66l,0xd856073968421d77l,
        0x03840fe883e3afeal },
      { 0xb391dad51ec69977l,0xae243fb9307f6726l,0xc88ac87be8ca160cl,
        0x5174cced4ce355f4l } },
    /* 49 << 112 */
    { { 0x98a35966e58ba37dl,0xfdcc8da27817335dl,0x5b75283083fbc7bfl,
        0x68e419d4d9c96984l },
      { 0x409a39f402a40380l,0x88940faf1fe977bcl,0xc640a94b8f8edea6l,
        0x1e22cd17ed11547dl } },
    /* 50 << 112 */
    { { 0xe28568ce59ffc3e2l,0x60aa1b55c1dee4e7l,0xc67497c8837cb363l,
        0x06fb438a105a2bf2l },
      { 0x30357ec4500d8e20l,0x1ad9095d0670db10l,0x7f589a05c73b7cfdl,
        0xf544607d880d6d28l } },
    /* 51 << 112 */
    { { 0x17ba93b1a20ef103l,0xad8591306ba6577bl,0x65c91cf66fa214a0l,
        0xd7d49c6c27990da5l },
      { 0xecd9ec8d20bb569dl,0xbd4b2502eeffbc33l,0x2056ca5a6bed0467l,
        0x7916a1f75b63728cl } },
    /* 52 << 112 */
    { { 0xd4f9497d53a4f566l,0x8973466497b56810l,0xf8e1da740494a621l,
        0x82546a938d011c68l },
      { 0x1f3acb19c61ac162l,0x52f8fa9cabad0d3el,0x15356523b4b7ea43l,
        0x5a16ad61ae608125l } },
    /* 53 << 112 */
    { { 0xb0bcb87f4faed184l,0x5f236b1d5029f45fl,0xd42c76070bc6b1fcl,
        0xc644324e68aefce3l },
      { 0x8e191d595c5d8446l,0xc020807713ae1979l,0xadcaee553ba59cc7l,
        0x20ed6d6ba2cb81bal } },
    /* 54 << 112 */
    { { 0x0952ba19b6efcffcl,0x60f12d6897c0b87cl,0x4ee2c7c49caa30bcl,
        0x767238b797fbff4el },
      { 0xebc73921501b5d92l,0x3279e3dfc2a37737l,0x9fc12bc86d197543l,
        0xfa94dc6f0a40db4el } },
    /* 55 << 112 */
    { { 0x7392b41a530ccbbdl,0x87c82146ea823525l,0xa52f984c05d98d0cl,
        0x2ae57d735ef6974cl },
      { 0x9377f7bf3042a6ddl,0xb1a007c019647a64l,0xfaa9079a0cca9767l,
        0x3d81a25bf68f72d5l } },
    /* 56 << 112 */
    { { 0x752067f8ff81578el,0x786221509045447dl,0xc0c22fcf0505aa6fl,
        0x1030f0a66bed1c77l },
      { 0x31f29f151f0bd739l,0x2d7989c7e6debe85l,0x5c070e728e677e98l,
        0x0a817bd306e81fd5l } },
    /* 57 << 112 */
    { { 0xc110d830b0f2ac95l,0x48d0995aab20e64el,0x0f3e00e17729cd9al,
        0x2a570c20dd556946l },
      { 0x912dbcfd4e86214dl,0x2d014ee2cf615498l,0x55e2b1e63530d76el,
        0xc5135ae4fd0fd6d1l } },
    /* 58 << 112 */
    { { 0x0066273ad4f3049fl,0xbb8e9893e7087477l,0x2dba1ddb14c6e5fdl,
        0xdba3788651f57e6cl },
      { 0x5aaee0a65a72f2cfl,0x1208bfbf7bea5642l,0xf5c6aa3b67872c37l,
        0xd726e08343f93224l } },
    /* 59 << 112 */
    { { 0x1854daa5061f1658l,0xc0016df1df0cd2b3l,0xc2a3f23e833d50del,
        0x73b681d2bbbd3017l },
      { 0x2f046dc43ac343c0l,0x9c847e7d85716421l,0xe1e13c910917eed4l,
        0x3fc9eebd63a1b9c6l } },
    /* 60 << 112 */
    { { 0x0f816a727fe02299l,0x6335ccc2294f3319l,0x3820179f4745c5bel,
        0xe647b782922f066el },
      { 0xc22e49de02cafb8al,0x299bc2fffcc2ecccl,0x9a8feea26e0e8282l,
        0xa627278bfe893205l } },
    /* 61 << 112 */
    { { 0xa7e197337933e47bl,0xf4ff6b132e766402l,0xa4d8be0a98440d9fl,
        0x658f5c2f38938808l },
      { 0x90b75677c95b3b3el,0xfa0442693137b6ffl,0x077b039b43c47c29l,
        0xcca95dd38a6445b2l } },
    /* 62 << 112 */
    { { 0x0b498ba42333fc4cl,0x274f8e68f736a1b1l,0x6ca348fd5f1d4b2el,
        0x24d3be78a8f10199l },
      { 0x8535f858ca14f530l,0xa6e7f1635b982e51l,0x847c851236e1bf62l,
        0xf6a7c58e03448418l } },
    /* 63 << 112 */
    { { 0x583f3703f9374ab6l,0x864f91956e564145l,0x33bc3f4822526d50l,
        0x9f323c801262a496l },
      { 0xaa97a7ae3f046a9al,0x70da183edf8a039al,0x5b68f71c52aa0ba6l,
        0x9be0fe5121459c2dl } },
    /* 64 << 112 */
    { { 0xc1e17eb6cbc613e5l,0x33131d55497ea61cl,0x2f69d39eaf7eded5l,
        0x73c2f434de6af11bl },
      { 0x4ca52493a4a375fal,0x5f06787cb833c5c2l,0x814e091f3e6e71cfl,
        0x76451f578b746666l } },
    /* 0 << 119 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 119 */
    { { 0x80f9bdef694db7e0l,0xedca8787b9fcddc6l,0x51981c3403b8dce1l,
        0x4274dcf170e10ba1l },
      { 0xf72743b86def6d1al,0xd25b1670ebdb1866l,0xc4491e8c050c6f58l,
        0x2be2b2ab87fbd7f5l } },
    /* 2 << 119 */
    { { 0x3e0e5c9dd111f8ecl,0xbcc33f8db7c4e760l,0x702f9a91bd392a51l,
        0x7da4a795c132e92dl },
      { 0x1a0b0ae30bb1151bl,0x54febac802e32251l,0xea3a5082694e9e78l,
        0xe58ffec1e4fe40b8l } },
    /* 3 << 119 */
    { { 0xf85592fcd1e0cf9el,0xdea75f0dc0e7b2e8l,0xc04215cfc135584el,
        0x174fc7272f57092al },
      { 0xe7277877eb930beal,0x504caccb5eb02a5al,0xf9fe08f7f5241b9bl,
        0xe7fb62f48d5ca954l } },
    /* 4 << 119 */
    { { 0xfbb8349d29c4120bl,0x9f94391fc0d0d915l,0xc4074fa75410ba51l,
        0xa66adbf6150a5911l },
      { 0xc164543c34bfca38l,0xe0f27560b9e1ccfcl,0x99da0f53e820219cl,
        0xe8234498c6b4997al } },
    /* 5 << 119 */
    { { 0xcfb88b769d4c5423l,0x9e56eb10b0521c49l,0x418e0b5ebe8700a1l,
        0x00cbaad6f93cb58al },
      { 0xe923fbded92a5e67l,0xca4979ac1f347f11l,0x89162d856bc0585bl,
        0xdd6254afac3c70e3l } },
    /* 6 << 119 */
    { { 0x7b23c513516e19e4l,0x56e2e847c5c4d593l,0x9f727d735ce71ef6l,
        0x5b6304a6f79a44c5l },
      { 0x6638a7363ab7e433l,0x1adea470fe742f83l,0xe054b8545b7fc19fl,
        0xf935381aba1d0698l } },
    /* 7 << 119 */
    { { 0x546eab2d799e9a74l,0x96239e0ea949f729l,0xca274c6b7090055al,
        0x835142c39020c9b0l },
      { 0xa405667aa2e8807fl,0x29f2c0851aa3d39el,0xcc555d6442fc72f5l,
        0xe856e0e7fbeacb3cl } },
    /* 8 << 119 */
    { { 0xb5504f9d918e4936l,0x65035ef6b2513982l,0x0553a0c26f4d9cb9l,
        0x6cb10d56bea85509l },
      { 0x48d957b7a242da11l,0x16a4d3dd672b7268l,0x3d7e637c8502a96bl,
        0x27c7032b730d463bl } },
    /* 9 << 119 */
    { { 0xbdc02b18e4136a14l,0xbacf969d678e32bfl,0xc98d89a3dd9c3c03l,
        0x7b92420a23becc4fl },
      { 0xd4b41f78c64d565cl,0x9f969d0010f28295l,0xec7f7f76b13d051al,
        0x08945e1ea92da585l } },
    /* 10 << 119 */
    { { 0x55366b7d5846426fl,0xe7d09e89247d441dl,0x510b404d736fbf48l,
        0x7fa003d0e784bd7dl },
      { 0x25f7614f17fd9596l,0x49e0e0a135cb98dbl,0x2c65957b2e83a76al,
        0x5d40da8dcddbe0f8l } },
    /* 11 << 119 */
    { { 0xf2b8c405050bad24l,0x8918426dc2aa4823l,0x2aeab3dda38365a7l,
        0x720317177c91b690l },
      { 0x8b00d69960a94120l,0x478a255de99eaeecl,0xbf656a5f6f60aafdl,
        0xdfd7cb755dee77b3l } },
    /* 12 << 119 */
    { { 0x37f68bb4a595939dl,0x0355647928740217l,0x8e740e7c84ad7612l,
        0xd89bc8439044695fl },
      { 0xf7f3da5d85a9184dl,0x562563bb9fc0b074l,0x06d2e6aaf88a888el,
        0x612d8643161fbe7cl } },
    /* 13 << 119 */
    { { 0x465edba7f64085e7l,0xb230f30429aa8511l,0x53388426cda2d188l,
        0x908857354b666649l },
      { 0x6f02ff9a652f54f6l,0x65c822945fae2bf0l,0x7816ade062f5eee3l,
        0xdcdbdf43fcc56d70l } },
    /* 14 << 119 */
    { { 0x9fb3bba354530bb2l,0xbde3ef77cb0869eal,0x89bc90460b431163l,
        0x4d03d7d2e4819a35l },
      { 0x33ae4f9e43b6a782l,0x216db3079c88a686l,0x91dd88e000ffedd9l,
        0xb280da9f12bd4840l } },
    /* 15 << 119 */
    { { 0x32a7cb8a1635e741l,0xfe14008a78be02a7l,0x3fafb3341b7ae030l,
        0x7fd508e75add0ce9l },
      { 0x72c83219d607ad51l,0x0f229c0a8d40964al,0x1be2c3361c878da2l,
        0xe0c96742eab2ab86l } },
    /* 16 << 119 */
    { { 0x458f86913e538cd7l,0xa7001f6c8e08ad53l,0x52b8c6e6bf5d15ffl,
        0x548234a4011215ddl },
      { 0xff5a9d2d3d5b4045l,0xb0ffeeb64a904190l,0x55a3aca448607f8bl,
        0x8cbd665c30a0672al } },
    /* 17 << 119 */
    { { 0x87f834e042583068l,0x02da2aebf3f6e683l,0x6b763e5d05c12248l,
        0x7230378f65a8aefcl },
      { 0x93bd80b571e8e5cal,0x53ab041cb3b62524l,0x1b8605136c9c552el,
        0xe84d402cd5524e66l } },
    /* 18 << 119 */
    { { 0xa37f3573f37f5937l,0xeb0f6c7dd1e4fca5l,0x2965a554ac8ab0fcl,
        0x17fbf56c274676acl },
      { 0x2e2f6bd9acf7d720l,0x41fc8f8810224766l,0x517a14b385d53befl,
        0xdae327a57d76a7d1l } },
    /* 19 << 119 */
    { { 0x6ad0a065c4818267l,0x33aa189b37c1bbc1l,0x64970b5227392a92l,
        0x21699a1c2d1535eal },
      { 0xcd20779cc2d7a7fdl,0xe318605999c83cf2l,0x9b69440b72c0b8c7l,
        0xa81497d77b9e0e4dl } },
    /* 20 << 119 */
    { { 0x515d5c891f5f82dcl,0x9a7f67d76361079el,0xa8da81e311a35330l,
        0xe44990c44b18be1bl },
      { 0xc7d5ed95af103e59l,0xece8aba78dac9261l,0xbe82b0999394b8d3l,
        0x6830f09a16adfe83l } },
    /* 21 << 119 */
    { { 0x250a29b488172d01l,0x8b20bd65caff9e02l,0xb8a7661ee8a6329al,
        0x4520304dd3fce920l },
      { 0xae45da1f2b47f7efl,0xe07f52885bffc540l,0xf79970093464f874l,
        0x2244c2cda6fa1f38l } },
    /* 22 << 119 */
    { { 0x43c41ac194d7d9b1l,0x5bafdd82c82e7f17l,0xdf0614c15fda0fcal,
        0x74b043a7a8ae37adl },
      { 0x3ba6afa19e71734cl,0x15d5437e9c450f2el,0x4a5883fe67e242b1l,
        0x5143bdc22c1953c2l } },
    /* 23 << 119 */
    { { 0x542b8b53fc5e8920l,0x363bf9a89a9cee08l,0x02375f10c3486e08l,
        0x2037543b8c5e70d2l },
      { 0x7109bccc625640b4l,0xcbc1051e8bc62c3bl,0xf8455fed803f26eal,
        0x6badceabeb372424l } },
    /* 24 << 119 */
    { { 0xa2a9ce7c6b53f5f9l,0x642465951b176d99l,0xb1298d36b95c081bl,
        0x53505bb81d9a9ee6l },
      { 0x3f6f9e61f2ba70b0l,0xd07e16c98afad453l,0x9f1694bbe7eb4a6al,
        0xdfebced93cb0bc8el } },
    /* 25 << 119 */
    { { 0x92d3dcdc53868c8bl,0x174311a2386107a6l,0x4109e07c689b4e64l,
        0x30e4587f2df3dcb6l },
      { 0x841aea310811b3b2l,0x6144d41d0cce43eal,0x464c45812a9a7803l,
        0xd03d371f3e158930l } },
    /* 26 << 119 */
    { { 0xc676d7f2b1f3390bl,0x9f7a1b8ca5b61272l,0x4ebebfc9c2e127a9l,
        0x4602500c5dd997bfl },
      { 0x7f09771c4711230fl,0x058eb37c020f09c1l,0xab693d4bfee5e38bl,
        0x9289eb1f4653cbc0l } },
    /* 27 << 119 */
    { { 0xbecf46abd51b9cf5l,0xd2aa9c029f0121afl,0x36aaf7d2e90dc274l,
        0x909e4ea048b95a3cl },
      { 0xe6b704966f32dbdbl,0x672188a08b030b3el,0xeeffe5b3cfb617e2l,
        0x87e947de7c82709el } },
    /* 28 << 119 */
    { { 0xa44d2b391770f5a7l,0xe4d4d7910e44eb82l,0x42e69d1e3f69712al,
        0xbf11c4d6ac6a820el },
      { 0xb5e7f3e542c4224cl,0xd6b4e81c449d941cl,0x5d72bd165450e878l,
        0x6a61e28aee25ac54l } },
    /* 29 << 119 */
    { { 0x33272094e6f1cd95l,0x7512f30d0d18673fl,0x32f7a4ca5afc1464l,
        0x2f0956566bbb977bl },
      { 0x586f47caa8226200l,0x02c868ad1ac07369l,0x4ef2b845c613acbel,
        0x43d7563e0386054cl } },
    /* 30 << 119 */
    { { 0x54da9dc7ab952578l,0xb5423df226e84d0bl,0xa8b64eeb9b872042l,
        0xac2057825990f6dfl },
      { 0x4ff696eb21f4c77al,0x1a79c3e4aab273afl,0x29bc922e9436b3f1l,
        0xff807ef8d6d9a27al } },
    /* 31 << 119 */
    { { 0x82acea3d778f22a0l,0xfb10b2e85b5e7469l,0xc0b169802818ee7dl,
        0x011afff4c91c1a2fl },
      { 0x95a6d126ad124418l,0x31c081a5e72e295fl,0x36bb283af2f4db75l,
        0xd115540f7acef462l } },
    /* 32 << 119 */
    { { 0xc7f3a8f833f6746cl,0x21e46f65fea990cal,0x915fd5c5caddb0a9l,
        0xbd41f01678614555l },
      { 0x346f4434426ffb58l,0x8055943614dbc204l,0xf3dd20fe5a969b7fl,
        0x9d59e956e899a39al } },
    /* 33 << 119 */
    { { 0xf1b0971c8ad4cf4bl,0x034488602ffb8fb8l,0xf071ac3c65340ba4l,
        0x408d0596b27fd758l },
      { 0xe7c78ea498c364b0l,0xa4aac4a5051e8ab5l,0xb9e1d560485d9002l,
        0x9acd518a88844455l } },
    /* 34 << 119 */
    { { 0xe4ca688fd06f56c0l,0xa48af70ddf027972l,0x691f0f045e9a609dl,
        0xa9dd82cdee61270el },
      { 0x8903ca63a0ef18d3l,0x9fb7ee353d6ca3bdl,0xa7b4a09cabf47d03l,
        0x4cdada011c67de8el } },
    /* 35 << 119 */
    { { 0x520037499355a244l,0xe77fd2b64f2151a9l,0x695d6cf666b4efcbl,
        0xc5a0cacfda2cfe25l },
      { 0x104efe5cef811865l,0xf52813e89ea5cc3dl,0x855683dc40b58dbcl,
        0x0338ecde175fcb11l } },
    /* 36 << 119 */
    { { 0xf9a0563774921592l,0xb4f1261db9bb9d31l,0x551429b74e9c5459l,
        0xbe182e6f6ea71f53l },
      { 0xd3a3b07cdfc50573l,0x9ba1afda62be8d44l,0x9bcfd2cb52ab65d3l,
        0xdf11d547a9571802l } },
    /* 37 << 119 */
    { { 0x099403ee02a2404al,0x497406f421088a71l,0x994794095004ae71l,
        0xbdb42078a812c362l },
      { 0x2b72a30fd8828442l,0x283add27fcb5ed1cl,0xf7c0e20066a40015l,
        0x3e3be64108b295efl } },
    /* 38 << 119 */
    { { 0xac127dc1e038a675l,0x729deff38c5c6320l,0xb7df8fd4a90d2c53l,
        0x9b74b0ec681e7cd3l },
      { 0x5cb5a623dab407e5l,0xcdbd361576b340c6l,0xa184415a7d28392cl,
        0xc184c1d8e96f7830l } },
    /* 39 << 119 */
    { { 0xc3204f1981d3a80fl,0xfde0c841c8e02432l,0x78203b3e8149e0c1l,
        0x5904bdbb08053a73l },
      { 0x30fc1dd1101b6805l,0x43c223bc49aa6d49l,0x9ed671417a174087l,
        0x311469a0d5997008l } },
    /* 40 << 119 */
    { { 0xb189b6845e43fc61l,0xf3282375e0d3ab57l,0x4fa34b67b1181da8l,
        0x621ed0b299ee52b8l },
      { 0x9b178de1ad990676l,0xd51de67b56d54065l,0x2a2c27c47538c201l,
        0x33856ec838a40f5cl } },
    /* 41 << 119 */
    { { 0x2522fc15be6cdcdel,0x1e603f339f0c6f89l,0x7994edc3103e30a6l,
        0x033a00db220c853el },
      { 0xd3cfa409f7bb7fd7l,0x70f8781e462d18f6l,0xbbd82980687fe295l,
        0x6eef4c32595669f3l } },
    /* 42 << 119 */
    { { 0x86a9303b2f7e85c3l,0x5fce462171988f9bl,0x5b935bf6c138acb5l,
        0x30ea7d6725661212l },
      { 0xef1eb5f4e51ab9a2l,0x0587c98aae067c78l,0xb3ce1b3c77ca9ca6l,
        0x2a553d4d54b5f057l } },
    /* 43 << 119 */
    { { 0xc78982364da29ec2l,0xdbdd5d13b9c57316l,0xc57d6e6b2cd80d47l,
        0x80b460cffe9e7391l },
      { 0x98648cabf963c31el,0x67f9f633cc4d32fdl,0x0af42a9dfdf7c687l,
        0x55f292a30b015ea7l } },
    /* 44 << 119 */
    { { 0x89e468b2cd21ab3dl,0xe504f022c393d392l,0xab21e1d4a5013af9l,
        0xe3283f78c2c28acbl },
      { 0xf38b35f6226bf99fl,0xe83542740e291e69l,0x61673a15b20c162dl,
        0xc101dc75b04fbdbel } },
    /* 45 << 119 */
    { { 0x8323b4c2255bd617l,0x6c9696936c2a9154l,0xc6e6586062679387l,
        0x8e01db0cb8c88e23l },
      { 0x33c42873893a5559l,0x7630f04b47a3e149l,0xb5d80805ddcf35f8l,
        0x582ca08077dfe732l } },
    /* 46 << 119 */
    { { 0x2c7156e10b1894a0l,0x92034001d81c68c0l,0xed225d00c8b115b5l,
        0x237f9c2283b907f2l },
      { 0x0ea2f32f4470e2c0l,0xb725f7c158be4e95l,0x0f1dcafab1ae5463l,
        0x59ed51871ba2fc04l } },
    /* 47 << 119 */
    { { 0xf6e0f316d0115d4dl,0x5180b12fd3691599l,0x157e32c9527f0a41l,
        0x7b0b081da8e0ecc0l },
      { 0x6dbaaa8abf4f0dd0l,0x99b289c74d252696l,0x79b7755edbf864fel,
        0x6974e2b176cad3abl } },
    /* 48 << 119 */
    { { 0x35dbbee206ddd657l,0xe7cbdd112ff3a96dl,0x88381968076be758l,
        0x2d737e7208c91f5dl },
      { 0x5f83ab6286ec3776l,0x98aa649d945fa7a1l,0xf477ec3772ef0933l,
        0x66f52b1e098c17b1l } },
    /* 49 << 119 */
    { { 0x9eec58fbd803738bl,0x91aaade7e4e86aa4l,0x6b1ae617a5b51492l,
        0x63272121bbc45974l },
      { 0x7e0e28f0862c5129l,0x0a8f79a93321a4a0l,0xe26d16645041c88fl,
        0x0571b80553233e3al } },
    /* 50 << 119 */
    { { 0xd1b0ccdec9520711l,0x55a9e4ed3c8b84bfl,0x9426bd39a1fef314l,
        0x4f5f638e6eb93f2bl },
      { 0xba2a1ed32bf9341bl,0xd63c13214d42d5a9l,0xd2964a89316dc7c5l,
        0xd1759606ca511851l } },
    /* 51 << 119 */
    { { 0xd8a9201ff9e6ed35l,0xb7b5ee456736925al,0x0a83fbbc99581af7l,
        0x3076bc4064eeb051l },
      { 0x5511c98c02dec312l,0x270de898238dcb78l,0x2cf4cf9c539c08c9l,
        0xa70cb65e38d3b06el } },
    /* 52 << 119 */
    { { 0xb12ec10ecfe57bbdl,0x82c7b65635a0c2b5l,0xddc7d5cd161c67bdl,
        0xe32e8985ae3a32ccl },
      { 0x7aba9444d11a5529l,0xe964ed022427fa1al,0x1528392d24a1770al,
        0xa152ce2c12c72fcdl } },
    /* 53 << 119 */
    { { 0x714553a48ec07649l,0x18b4c290459dd453l,0xea32b7147b64b110l,
        0xb871bfa52e6f07a2l },
      { 0xb67112e59e2e3c9bl,0xfbf250e544aa90f6l,0xf77aedb8bd539006l,
        0x3b0cdf9ad172a66fl } },
    /* 54 << 119 */
    { { 0xedf69feaf8c51187l,0x05bb67ec741e4da7l,0x47df0f3208114345l,
        0x56facb07bb9792b1l },
      { 0xf3e007e98f6229e4l,0x62d103f4526fba0fl,0x4f33bef7b0339d79l,
        0x9841357bb59bfec1l } },
    /* 55 << 119 */
    { { 0xfa8dbb59c34e6705l,0xc3c7180b7fdaa84cl,0xf95872fca4108537l,
        0x8750cc3b932a3e5al },
      { 0xb61cc69db7275d7dl,0xffa0168b2e59b2e9l,0xca032abc6ecbb493l,
        0x1d86dbd32c9082d8l } },
    /* 56 << 119 */
    { { 0xae1e0b67e28ef5bal,0x2c9a4699cb18e169l,0x0ecd0e331e6bbd20l,
        0x571b360eaf5e81d2l },
      { 0xcd9fea58101c1d45l,0x6651788e18880452l,0xa99726351f8dd446l,
        0x44bed022e37281d0l } },
    /* 57 << 119 */
    { { 0x094b2b2d33da525dl,0xf193678e13144fd8l,0xb8ab5ba4f4c1061dl,
        0x4343b5fadccbe0f4l },
      { 0xa870237163812713l,0x47bf6d2df7611d93l,0x46729b8cbd21e1d7l,
        0x7484d4e0d629e77dl } },
    /* 58 << 119 */
    { { 0x830e6eea60dbac1fl,0x23d8c484da06a2f7l,0x896714b050ca535bl,
        0xdc8d3644ebd97a9bl },
      { 0x106ef9fab12177b4l,0xf79bf464534d5d9cl,0x2537a349a6ab360bl,
        0xc7c54253a00c744fl } },
    /* 59 << 119 */
    { { 0xb3c7a047e5911a76l,0x61ffa5c8647f1ee7l,0x15aed36f8f56ab42l,
        0x6a0d41b0a3ff9ac9l },
      { 0x68f469f5cc30d357l,0xbe9adf816b72be96l,0x1cd926fe903ad461l,
        0x7e89e38fcaca441bl } },
    /* 60 << 119 */
    { { 0xf0f82de5facf69d4l,0x363b7e764775344cl,0x6894f312b2e36d04l,
        0x3c6cb4fe11d1c9a5l },
      { 0x85d9c3394008e1f2l,0x5e9a85ea249f326cl,0xdc35c60a678c5e06l,
        0xc08b944f9f86fba9l } },
    /* 61 << 119 */
    { { 0xde40c02c89f71f0fl,0xad8f3e31ff3da3c0l,0x3ea5096b42125dedl,
        0x13879cbfa7379183l },
      { 0x6f4714a56b306a0bl,0x359c2ea667646c5el,0xfacf894307726368l,
        0x07a5893565ff431el } },
    /* 62 << 119 */
    { { 0x24d661d168754ab0l,0x801fce1d6f429a76l,0xc068a85fa58ce769l,
        0xedc35c545d5eca2bl },
      { 0xea31276fa3f660d1l,0xa0184ebeb8fc7167l,0x0f20f21a1d8db0ael,
        0xd96d095f56c35e12l } },
    /* 63 << 119 */
    { { 0xedf402b5f8c2a25bl,0x1bb772b9059204b6l,0x50cbeae219b4e34cl,
        0x93109d803fa0845al },
      { 0x54f7ccf78ef59fb5l,0x3b438fe288070963l,0x9e28c65931f3ba9bl,
        0x9cc31b46ead9da92l } },
    /* 64 << 119 */
    { { 0x3c2f0ba9b733aa5fl,0xdece47cbf05af235l,0xf8e3f715a2ac82a5l,
        0xc97ba6412203f18al },
      { 0xc3af550409c11060l,0x56ea2c0546af512dl,0xfac28daff3f28146l,
        0x87fab43a959ef494l } },
    /* 0 << 126 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 126 */
    { { 0x09891641d4c5105fl,0x1ae80f8e6d7fbd65l,0x9d67225fbee6bdb0l,
        0x3b433b597fc4d860l },
      { 0x44e66db693e85638l,0xf7b59252e3e9862fl,0xdb785157665c32ecl,
        0x702fefd7ae362f50l } },
    /* 2 << 126 */
    { { 0x3754475d0fefb0c3l,0xd48fb56b46d7c35dl,0xa070b633363798a4l,
        0xae89f3d28fdb98e6l },
      { 0x970b89c86363d14cl,0x8981752167abd27dl,0x9bf7d47444d5a021l,
        0xb3083bafcac72aeel } },
    /* 3 << 126 */
    { { 0x389741debe949a44l,0x638e9388546a4fa5l,0x3fe6419ca0047bdcl,
        0x7047f648aaea57cal },
      { 0x54e48a9041fbab17l,0xda8e0b28576bdba2l,0xe807eebcc72afddcl,
        0x07d3336df42577bfl } },
    /* 4 << 126 */
    { { 0x62a8c244bfe20925l,0x91c19ac38fdce867l,0x5a96a5d5dd387063l,
        0x61d587d421d324f6l },
      { 0xe87673a2a37173eal,0x2384800853778b65l,0x10f8441e05bab43el,
        0xfa11fe124621efbel } },
    /* 5 << 126 */
    { { 0x047b772e81685d7bl,0x23f27d81bf34a976l,0xc27608e2915f48efl,
        0x3b0b43faa521d5c3l },
      { 0x7613fb2663ca7284l,0x7f5729b41d4db837l,0x87b14898583b526bl,
        0x00b732a6bbadd3d1l } },
    /* 6 << 126 */
    { { 0x8e02f4262048e396l,0x436b50b6383d9de4l,0xf78d3481471e85adl,
        0x8b01ea6ad005c8d6l },
      { 0xd3c7afee97015c07l,0x46cdf1a94e3ba2ael,0x7a42e50183d3a1d2l,
        0xd54b5268b541dff4l } },
    /* 7 << 126 */
    { { 0x3f24cf304e23e9bcl,0x4387f816126e3624l,0x26a46a033b0b6d61l,
        0xaf1bc8458b2d777cl },
      { 0x25c401ba527de79cl,0x0e1346d44261bbb6l,0x4b96c44b287b4bc7l,
        0x658493c75254562fl } },
    /* 8 << 126 */
    { { 0x23f949feb8a24a20l,0x17ebfed1f52ca53fl,0x9b691bbebcfb4853l,
        0x5617ff6b6278a05dl },
      { 0x241b34c5e3c99ebdl,0xfc64242e1784156al,0x4206482f695d67dfl,
        0xb967ce0eee27c011l } },
    /* 9 << 126 */
    { { 0x65db375121c80b5dl,0x2e7a563ca31ecca0l,0xe56ffc4e5238a07el,
        0x3d6c296632ced854l },
      { 0xe99d7d1aaf70b885l,0xafc3bad92d686459l,0x9c78bf460cc8ba5bl,
        0x5a43951918955aa3l } },
    /* 10 << 126 */
    { { 0xf8b517a85fe4e314l,0xe60234d0fcb8906fl,0xffe542acf2061b23l,
        0x287e191f6b4cb59cl },
      { 0x21857ddc09d877d8l,0x1c23478c14678941l,0xbbf0c056b6e05ea4l,
        0x82da4b53b01594fel } },
    /* 11 << 126 */
    { { 0xf7526791fadb8608l,0x049e832d7b74cdf6l,0xa43581ccc2b90a34l,
        0x73639eb89360b10cl },
      { 0x4fba331fe1e4a71bl,0x6ffd6b938072f919l,0x6e53271c65679032l,
        0x67206444f14272cel } },
    /* 12 << 126 */
    { { 0xc0f734a3b2335834l,0x9526205a90ef6860l,0xcb8be71704e2bb0dl,
        0x2418871e02f383fal },
      { 0xd71776814082c157l,0xcc914ad029c20073l,0xf186c1ebe587e728l,
        0x6fdb3c2261bcd5fdl } },
    /* 13 << 126 */
    { { 0x30d014a6f2f9f8e9l,0x963ece234fec49d2l,0x862025c59605a8d9l,
        0x3987444519f8929al },
      { 0x01b6ff6512bf476al,0x598a64d809cf7d91l,0xd7ec774993be56cal,
        0x10899785cbb33615l } },
    /* 14 << 126 */
    { { 0xb8a092fd02eee3adl,0xa86b3d3530145270l,0x323d98c68512b675l,
        0x4b8bc78562ebb40fl },
      { 0x7d301f54413f9cdel,0xa5e4fb4f2bab5664l,0x1d2b252d1cbfec23l,
        0xfcd576bbe177120dl } },
    /* 15 << 126 */
    { { 0x04427d3e83731a34l,0x2bb9028eed836e8el,0xb36acff8b612ca7cl,
        0xb88fe5efd3d9c73al },
      { 0xbe2a6bc6edea4eb3l,0x43b93133488eec77l,0xf41ff566b17106e1l,
        0x469e9172654efa32l } },
    /* 16 << 126 */
    { { 0xb4480f0441c23fa3l,0xb4712eb0c1989a2el,0x3ccbba0f93a29ca7l,
        0x6e205c14d619428cl },
      { 0x90db7957b3641686l,0x0432691d45ac8b4el,0x07a759acf64e0350l,
        0x0514d89c9c972517l } },
    /* 17 << 126 */
    { { 0x1701147fa8e67fc3l,0x9e2e0b8bab2085bel,0xd5651824ac284e57l,
        0x890d432574893664l },
      { 0x8a7c5e6ec55e68a3l,0xbf12e90b4339c85al,0x31846b85f922b655l,
        0x9a54ce4d0bf4d700l } },
    /* 18 << 126 */
    { { 0xd7f4e83af1a14295l,0x916f955cb285d4f9l,0xe57bb0e099ffdabal,
        0x28a43034eab0d152l },
      { 0x0a36ffa2b8a9cef8l,0x5517407eb9ec051al,0x9c796096ea68e672l,
        0x853db5fbfb3c77fbl } },
    /* 19 << 126 */
    { { 0x21474ba9e864a51al,0x6c2676996e8a1b8bl,0x7c82362694120a28l,
        0xe61e9a488383a5dbl },
      { 0x7dd750039f84216dl,0xab020d07ad43cd85l,0x9437ae48da12c659l,
        0x6449c2ebe65452adl } },
    /* 20 << 126 */
    { { 0xcc7c4c1c2cf9d7c1l,0x1320886aee95e5abl,0xbb7b9056beae170cl,
        0xc8a5b250dbc0d662l },
      { 0x4ed81432c11d2303l,0x7da669121f03769fl,0x3ac7a5fd84539828l,
        0x14dada943bccdd02l } },
    /* 21 << 126 */
    { { 0x8b84c3217ef6b0d1l,0x52a9477a7c933f22l,0x5ef6728afd440b82l,
        0x5c3bd8596ce4bd5el },
      { 0x918b80f5f22c2d3el,0x368d5040b7bb6cc5l,0xb66142a12695a11cl,
        0x60ac583aeb19ea70l } },
    /* 22 << 126 */
    { { 0x317cbb980eab2437l,0x8cc08c555e2654c8l,0xfe2d6520e6d8307fl,
        0xe9f147f357428993l },
      { 0x5f9c7d14d2fd6cf1l,0xa3ecd0642d4fcbb0l,0xad83fef08e7341f7l,
        0x643f23a03a63115cl } },
    /* 23 << 126 */
    { { 0xd38a78abe65ab743l,0xbf7c75b135edc89cl,0x3dd8752e530df568l,
        0xf85c4a76e308c682l },
      { 0x4c9955b2e68acf37l,0xa544df3dab32af85l,0x4b8ec3f5a25cf493l,
        0x4d8f27641a622febl } },
    /* 24 << 126 */
    { { 0x7bb4f7aaf0dcbc49l,0x7de551f970bbb45bl,0xcfd0f3e49f2ca2e5l,
        0xece587091f5c76efl },
      { 0x32920edd167d79ael,0x039df8a2fa7d7ec1l,0xf46206c0bb30af91l,
        0x1ff5e2f522676b59l } },
    /* 25 << 126 */
    { { 0x11f4a0396ea51d66l,0x506c1445807d7a26l,0x60da5705755a9b24l,
        0x8fc8cc321f1a319el },
      { 0x83642d4d9433d67dl,0x7fa5cb8f6a7dd296l,0x576591db9b7bde07l,
        0x13173d25419716fbl } },
    /* 26 << 126 */
    { { 0xea30599dd5b340ffl,0xfc6b5297b0fe76c5l,0x1c6968c8ab8f5adcl,
        0xf723c7f5901c928dl },
      { 0x4203c3219773d402l,0xdf7c6aa31b51dd47l,0x3d49e37a552be23cl,
        0x57febee80b5a6e87l } },
    /* 27 << 126 */
    { { 0xc5ecbee47bd8e739l,0x79d44994ae63bf75l,0x168bd00f38fb8923l,
        0x75d48ee4d0533130l },
      { 0x554f77aadb5cdf33l,0x3396e8963c696769l,0x2fdddbf2d3fd674el,
        0xbbb8f6ee99d0e3e5l } },
    /* 28 << 126 */
    { { 0x51b90651cbae2f70l,0xefc4bc0593aaa8ebl,0x8ecd8689dd1df499l,
        0x1aee99a822f367a5l },
      { 0x95d485b9ae8274c5l,0x6c14d4457d30b39cl,0xbafea90bbcc1ef81l,
        0x7c5f317aa459a2edl } },
    /* 29 << 126 */
    { { 0x012110754ef44227l,0xa17bed6edc20f496l,0x0cdfe424819853cdl,
        0x13793298f71e2ce7l },
      { 0x3c1f3078dbbe307bl,0x6dd1c20e76ee9936l,0x23ee4b57423caa20l,
        0x4ac3793b8efb840el } },
    /* 30 << 126 */
    { { 0x934438ebed1f8ca0l,0x3e5466584ebb25a2l,0xc415af0ec069896fl,
        0xc13eddb09a5aa43dl },
      { 0x7a04204fd49eb8f6l,0xd0d5bdfcd74f1670l,0x3697e28656fc0558l,
        0x1020737101cebadel } },
    /* 31 << 126 */
    { { 0x5f87e6900647a82bl,0x908e0ed48f40054fl,0xa9f633d479853803l,
        0x8ed13c9a4a28b252l },
      { 0x3e2ef6761f460f64l,0x53930b9b36d06336l,0x347073ac8fc4979bl,
        0x84380e0e5ecd5597l } },
    /* 32 << 126 */
    { { 0xe3b22c6bc4fe3c39l,0xba4a81536c7bebdfl,0xf23ab6b725693459l,
        0x53bc377014922b11l },
      { 0x4645c8ab5afc60dbl,0xaa02235520b9f2a3l,0x52a2954cce0fc507l,
        0x8c2731bb7ce1c2e7l } },
    /* 33 << 126 */
    { { 0xf39608ab18a0339dl,0xac7a658d3735436cl,0xb22c2b07cd992b4fl,
        0x4e83daecf40dcfd4l },
      { 0x8a34c7be2f39ea3el,0xef0c005fb0a56d2el,0x62731f6a6edd8038l,
        0x5721d7404e3cb075l } },
    /* 34 << 126 */
    { { 0x1ea41511fbeeee1bl,0xd1ef5e73ef1d0c05l,0x42feefd173c07d35l,
        0xe530a00a8a329493l },
      { 0x5d55b7fef15ebfb0l,0x549de03cd322491al,0xf7b5f602745b3237l,
        0x3632a3a21ab6e2b6l } },
    /* 35 << 126 */
    { { 0x0d3bba890ef59f78l,0x0dfc6443c9e52b9al,0x1dc7969972631447l,
        0xef033917b3be20b1l },
      { 0x0c92735db1383948l,0xc1fc29a2c0dd7d7dl,0x6485b697403ed068l,
        0x13bfaab3aac93bdcl } },
    /* 36 << 126 */
    { { 0x410dc6a90deeaf52l,0xb003fb024c641c15l,0x1384978c5bc504c4l,
        0x37640487864a6a77l },
      { 0x05991bc6222a77dal,0x62260a575e47eb11l,0xc7af6613f21b432cl,
        0x22f3acc9ab4953e9l } },
    /* 37 << 126 */
    { { 0x529349228e41d155l,0x4d0245683ac059efl,0xb02017554d884411l,
        0xce8055cfa59a178fl },
      { 0xcd77d1aff6204549l,0xa0a00a3ec7066759l,0x471071ef0272c229l,
        0x009bcf6bd3c4b6b0l } },
    /* 38 << 126 */
    { { 0x2a2638a822305177l,0xd51d59df41645bbfl,0xa81142fdc0a7a3c0l,
        0xa17eca6d4c7063eel },
      { 0x0bb887ed60d9dcecl,0xd6d28e5120ad2455l,0xebed6308a67102bal,
        0x042c31148bffa408l } },
    /* 39 << 126 */
    { { 0xfd099ac58aa68e30l,0x7a6a3d7c1483513el,0xffcc6b75ba2d8f0cl,
        0x54dacf961e78b954l },
      { 0xf645696fa4a9af89l,0x3a41194006ac98ecl,0x41b8b3f622a67a20l,
        0x2d0b1e0f99dec626l } },
    /* 40 << 126 */
    { { 0x27c8919240be34e8l,0xc7162b3791907f35l,0x90188ec1a956702bl,
        0xca132f7ddf93769cl },
      { 0x3ece44f90e2025b4l,0x67aaec690c62f14cl,0xad74141822e3cc11l,
        0xcf9b75c37ff9a50el } },
    /* 41 << 126 */
    { { 0x02fa2b164d348272l,0xbd99d61a9959d56dl,0xbc4f19db18762916l,
        0xcc7cce5049c1ac80l },
      { 0x4d59ebaad846bd83l,0x8775a9dca9202849l,0x07ec4ae16e1f4ca9l,
        0x27eb5875ba893f11l } },
    /* 42 << 126 */
    { { 0x00284d51662cc565l,0x82353a6b0db4138dl,0xd9c7aaaaaa32a594l,
        0xf5528b5ea5669c47l },
      { 0xf32202312f23c5ffl,0xe3e8147a6affa3a1l,0xfb423d5c202ddda0l,
        0x3d6414ac6b871bd4l } },
    /* 43 << 126 */
    { { 0x586f82e1a51a168al,0xb712c67148ae5448l,0x9a2e4bd176233eb8l,
        0x0188223a78811ca9l },
      { 0x553c5e21f7c18de1l,0x7682e451b27bb286l,0x3ed036b30e51e929l,
        0xf487211bec9cb34fl } },
    /* 44 << 126 */
    { { 0x0d0942770c24efc8l,0x0349fd04bef737a4l,0x6d1c9dd2514cdd28l,
        0x29c135ff30da9521l },
      { 0xea6e4508f78b0b6fl,0x176f5dd2678c143cl,0x081484184be21e65l,
        0x27f7525ce7df38c4l } },
    /* 45 << 126 */
    { { 0x1fb70e09748ab1a4l,0x9cba50a05efe4433l,0x7846c7a615f75af2l,
        0x2a7c2c575ee73ea8l },
      { 0x42e566a43f0a449al,0x45474c3bad90fc3dl,0x7447be3d8b61d057l,
        0x3e9d1cf13a4ec092l } },
    /* 46 << 126 */
    { { 0x1603e453f380a6e6l,0x0b86e4319b1437c2l,0x7a4173f2ef29610al,
        0x8fa729a7f03d57f7l },
      { 0x3e186f6e6c9c217el,0xbe1d307991919524l,0x92a62a70153d4fb1l,
        0x32ed3e34d68c2f71l } },
    /* 47 << 126 */
    { { 0xd785027f9eb1a8b7l,0xbc37eb77c5b22fe8l,0x466b34f0b9d6a191l,
        0x008a89af9a05f816l },
      { 0x19b028fb7d42c10al,0x7fe8c92f49b3f6b8l,0x58907cc0a5a0ade3l,
        0xb3154f51559d1a7cl } },
    /* 48 << 126 */
    { { 0x5066efb6d9790ed6l,0xa77a0cbca6aa793bl,0x1a915f3c223e042el,
        0x1c5def0469c5874bl },
      { 0x0e83007873b6c1dal,0x55cf85d2fcd8557al,0x0f7c7c760460f3b1l,
        0x87052acb46e58063l } },
    /* 49 << 126 */
    { { 0x09212b80907eae66l,0x3cb068e04d721c89l,0xa87941aedd45ac1cl,
        0xde8d5c0d0daa0dbbl },
      { 0xda421fdce3502e6el,0xc89442014d89a084l,0x7307ba5ef0c24bfbl,
        0xda212beb20bde0efl } },
    /* 50 << 126 */
    { { 0xea2da24bf82ce682l,0x058d381607f71fe4l,0x35a024625ffad8del,
        0xcd7b05dcaadcefabl },
      { 0xd442f8ed1d9f54ecl,0x8be3d618b2d3b5cal,0xe2220ed0e06b2ce2l,
        0x82699a5f1b0da4c0l } },
    /* 51 << 126 */
    { { 0x3ff106f571c0c3a7l,0x8f580f5a0d34180cl,0x4ebb120e22d7d375l,
        0x5e5782cce9513675l },
      { 0x2275580c99c82a70l,0xe8359fbf15ea8c4cl,0x53b48db87b415e70l,
        0xaacf2240100c6014l } },
    /* 52 << 126 */
    { { 0x9faaccf5e4652f1dl,0xbd6fdd2ad56157b2l,0xa4f4fb1f6261ec50l,
        0x244e55ad476bcd52l },
      { 0x881c9305047d320bl,0x1ca983d56181263fl,0x354e9a44278fb8eel,
        0xad2dbc0f396e4964l } },
    /* 53 << 126 */
    { { 0x723f3aa29268b3del,0x0d1ca29ae6e0609al,0x794866aa6cf44252l,
        0x0b59f3e301af87edl },
      { 0xe234e5ff7f4a6c51l,0xa8768fd261dc2f7el,0xdafc73320a94d81fl,
        0xd7f8428206938ce1l } },
    /* 54 << 126 */
    { { 0xae0b3c0e0546063el,0x7fbadcb25d61abc6l,0xd5d7a2c9369ac400l,
        0xa5978d09ae67d10cl },
      { 0x290f211e4f85eaacl,0xe61e2ad1facac681l,0xae125225388384cdl,
        0xa7fb68e9ccfde30fl } },
    /* 55 << 126 */
    { { 0x7a59b9363daed4c2l,0x80a9aa402606f789l,0xb40c1ea5f6a6d90al,
        0x948364d3514d5885l },
      { 0x062ebc6070985182l,0xa6db5b0e33310895l,0x64a12175e329c2f5l,
        0xc5f25bd290ea237el } },
    /* 56 << 126 */
    { { 0x7915c5242d0a4c23l,0xeb5d26e46bb3cc52l,0x369a9116c09e2c92l,
        0x0c527f92cf182cf8l },
      { 0x9e5919382aede0acl,0xb29222086cc34939l,0x3c9d896299a34361l,
        0x3c81836dc1905fe6l } },
    /* 57 << 126 */
    { { 0x4bfeb57fa001ec5al,0xe993f5bba0dc5dbal,0x47884109724a1380l,
        0x8a0369ab32fe9a04l },
      { 0xea068d608c927db8l,0xbf5f37cf94655741l,0x47d402a204b6c7eal,
        0x4551c2956af259cbl } },
    /* 58 << 126 */
    { { 0x698b71e7ed77ee8bl,0xbddf7bd0f309d5c7l,0x6201c22c34e780cal,
        0xab04f7d84c295ef4l },
      { 0x1c9472944313a8cel,0xe532e4ac92ca4cfel,0x89738f80d0a7a97al,
        0xec088c88a580fd5bl } },
    /* 59 << 126 */
    { { 0x612b1ecc42ce9e51l,0x8f9840fdb25fdd2al,0x3cda78c001e7f839l,
        0x546b3d3aece05480l },
      { 0x271719a980d30916l,0x45497107584c20c4l,0xaf8f94785bc78608l,
        0x28c7d484277e2a4cl } },
    /* 60 << 126 */
    { { 0xfce0176788a2ffe4l,0xdc506a3528e169a5l,0x0ea108617af9c93al,
        0x1ed2436103fa0e08l },
      { 0x96eaaa92a3d694e7l,0xc0f43b4def50bc74l,0xce6aa58c64114db4l,
        0x8218e8ea7c000fd4l } },
    /* 61 << 126 */
    { { 0xac815dfb185f8844l,0xcd7e90cb1557abfbl,0x23d16655afbfecdfl,
        0x80f3271f085cac4al },
      { 0x7fc39aa7d0e62f47l,0x88d519d1460a48e5l,0x59559ac4d28f101el,
        0x7981d9e9ca9ae816l } },
    /* 62 << 126 */
    { { 0x5c38652c9ac38203l,0x86eaf87f57657fe5l,0x568fc472e21f5416l,
        0x2afff39ce7e597b5l },
      { 0x3adbbb07256d4eabl,0x225986928285ab89l,0x35f8112a041caefel,
        0x95df02e3a5064c8bl } },
    /* 63 << 126 */
    { { 0x4d63356ec7004bf3l,0x230a08f4db83c7del,0xca27b2708709a7b7l,
        0x0d1c4cc4cb9abd2dl },
      { 0x8a0bc66e7550fee8l,0x369cd4c79cf7247el,0x75562e8492b5b7e7l,
        0x8fed0da05802af7bl } },
    /* 64 << 126 */
    { { 0x6a7091c2e48fb889l,0x26882c137b8a9d06l,0xa24986631b82a0e2l,
        0x844ed7363518152dl },
      { 0x282f476fd86e27c7l,0xa04edaca04afefdcl,0x8b256ebc6119e34dl,
        0x56a413e90787d78bl } },
    /* 0 << 133 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 133 */
    { { 0x82ee061d5a74be50l,0xe41781c4dea16ff5l,0xe0b0c81e99bfc8a2l,
        0x624f4d690b547e2dl },
      { 0x3a83545dbdcc9ae4l,0x2573dbb6409b1e8el,0x482960c4a6c93539l,
        0xf01059ad5ae18798l } },
    /* 2 << 133 */
    { { 0x715c9f973112795fl,0xe8244437984e6ee1l,0x55cb4858ecb66bcdl,
        0x7c136735abaffbeel },
      { 0x546615955dbec38el,0x51c0782c388ad153l,0x9ba4c53ac6e0952fl,
        0x27e6782a1b21dfa8l } },
    /* 3 << 133 */
    { { 0x682f903d4ed2dbc2l,0x0eba59c87c3b2d83l,0x8e9dc84d9c7e9335l,
        0x5f9b21b00eb226d7l },
      { 0xe33bd394af267bael,0xaa86cc25be2e15ael,0x4f0bf67d6a8ec500l,
        0x5846aa44f9630658l } },
    /* 4 << 133 */
    { { 0xfeb09740e2c2bf15l,0x627a2205a9e99704l,0xec8d73d0c2fbc565l,
        0x223eed8fc20c8de8l },
      { 0x1ee32583a8363b49l,0x1a0b6cb9c9c2b0a6l,0x49f7c3d290dbc85cl,
        0xa8dfbb971ef4c1acl } },
    /* 5 << 133 */
    { { 0xafb34d4c65c7c2abl,0x1d4610e7e2c5ea84l,0x893f6d1b973c4ab5l,
        0xa3cdd7e9945ba5c4l },
      { 0x60514983064417eel,0x1459b23cad6bdf2bl,0x23b2c3415cf726c3l,
        0x3a82963532d6354al } },
    /* 6 << 133 */
    { { 0x294f901fab192c18l,0xec5fcbfe7030164fl,0xe2e2fcb7e2246ba6l,
        0x1e7c88b3221a1a0cl },
      { 0x72c7dd93c92d88c5l,0x41c2148e1106fb59l,0x547dd4f5a0f60f14l,
        0xed9b52b263960f31l } },
    /* 7 << 133 */
    { { 0x6c8349ebb0a5b358l,0xb154c5c29e7e2ed6l,0xcad5eccfeda462dbl,
        0xf2d6dbe42de66b69l },
      { 0x426aedf38665e5b2l,0x488a85137b7f5723l,0x15cc43b38bcbb386l,
        0x27ad0af3d791d879l } },
    /* 8 << 133 */
    { { 0xc16c236e846e364fl,0x7f33527cdea50ca0l,0xc48107750926b86dl,
        0x6c2a36090598e70cl },
      { 0xa6755e52f024e924l,0xe0fa07a49db4afcal,0x15c3ce7d66831790l,
        0x5b4ef350a6cbb0d6l } },
    /* 9 << 133 */
    { { 0x2c4aafc4b6205969l,0x42563f02f6c7854fl,0x016aced51d983b48l,
        0xfeb356d899949755l },
      { 0x8c2a2c81d1a39bd7l,0x8f44340fe6934ae9l,0x148cf91c447904dal,
        0x7340185f0f51a926l } },
    /* 10 << 133 */
    { { 0x2f8f00fb7409ab46l,0x057e78e680e289b2l,0x03e5022ca888e5d1l,
        0x3c87111a9dede4e2l },
      { 0x5b9b0e1c7809460bl,0xe751c85271c9abc7l,0x8b944e28c7cc1dc9l,
        0x4f201ffa1d3cfa08l } },
    /* 11 << 133 */
    { { 0x02fc905c3e6721cel,0xd52d70dad0b3674cl,0x5dc2e5ca18810da4l,
        0xa984b2735c69dd99l },
      { 0x63b9252784de5ca4l,0x2f1c9872c852dec4l,0x18b03593c2e3de09l,
        0x19d70b019813dc2fl } },
    /* 12 << 133 */
    { { 0x42806b2da6dc1d29l,0xd3030009f871e144l,0xa1feb333aaf49276l,
        0xb5583b9ec70bc04bl },
      { 0x1db0be7895695f20l,0xfc84181189d012b5l,0x6409f27205f61643l,
        0x40d34174d5883128l } },
    /* 13 << 133 */
    { { 0xd79196f567419833l,0x6059e252863b7b08l,0x84da18171c56700cl,
        0x5758ee56b28d3ec4l },
      { 0x7da2771d013b0ea6l,0xfddf524b54c5e9b9l,0x7df4faf824305d80l,
        0x58f5c1bf3a97763fl } },
    /* 14 << 133 */
    { { 0xa5af37f17c696042l,0xd4cba22c4a2538del,0x211cb9959ea42600l,
        0xcd105f417b069889l },
      { 0xb1e1cf19ddb81e74l,0x472f2d895157b8cal,0x086fb008ee9db885l,
        0x365cd5700f26d131l } },
    /* 15 << 133 */
    { { 0x284b02bba2be7053l,0xdcbbf7c67ab9a6d6l,0x4425559c20f7a530l,
        0x961f2dfa188767c8l },
      { 0xe2fd943570dc80c4l,0x104d6b63f0784120l,0x7f592bc153567122l,
        0xf6bc1246f688ad77l } },
    /* 16 << 133 */
    { { 0x05214c050f15dde9l,0xa47a76a80d5f2b82l,0xbb254d3062e82b62l,
        0x11a05fe03ec955eel },
      { 0x7eaff46e9d529b36l,0x55ab13018f9e3df6l,0xc463e37199317698l,
        0xfd251438ccda47adl } },
    /* 17 << 133 */
    { { 0xca9c354723d695eal,0x48ce626e16e589b5l,0x6b5b64c7b187d086l,
        0xd02e1794b2207948l },
      { 0x8b58e98f7198111dl,0x90ca6305dcf9c3ccl,0x5691fe72f34089b0l,
        0x60941af1fc7c80ffl } },
    /* 18 << 133 */
    { { 0xa09bc0a222eb51e5l,0xc0bb7244aa9cf09al,0x36a8077f80159f06l,
        0x8b5c989edddc560el },
      { 0x19d2f316512e1f43l,0x02eac554ad08ff62l,0x012ab84c07d20b4el,
        0x37d1e115d6d4e4e1l } },
    /* 19 << 133 */
    { { 0xb6443e1aab7b19a8l,0xf08d067edef8cd45l,0x63adf3e9685e03dal,
        0xcf15a10e4792b916l },
      { 0xf44bcce5b738a425l,0xebe131d59636b2fdl,0x940688417850d605l,
        0x09684eaab40d749dl } },
    /* 20 << 133 */
    { { 0x8c3c669c72ba075bl,0x89f78b55ba469015l,0x5706aade3e9f8ba8l,
        0x6d8bd565b32d7ed7l },
      { 0x25f4e63b805f08d6l,0x7f48200dc3bcc1b5l,0x4e801968b025d847l,
        0x74afac0487cbe0a8l } },
    /* 21 << 133 */
    { { 0x43ed2c2b7e63d690l,0xefb6bbf00223cdb8l,0x4fec3cae2884d3fel,
        0x065ecce6d75e25a4l },
      { 0x6c2294ce69f79071l,0x0d9a8e5f044b8666l,0x5009f23817b69d8fl,
        0x3c29f8fec5dfdaf7l } },
    /* 22 << 133 */
    { { 0x9067528febae68c4l,0x5b38563230c5ba21l,0x540df1191fdd1aecl,
        0xcf37825bcfba4c78l },
      { 0x77eff980beb11454l,0x40a1a99160c1b066l,0xe8018980f889a1c7l,
        0xb9c52ae976c24be0l } },
    /* 23 << 133 */
    { { 0x05fbbcce45650ef4l,0xae000f108aa29ac7l,0x884b71724f04c470l,
        0x7cd4fde219bb5c25l },
      { 0x6477b22ae8840869l,0xa88688595fbd0686l,0xf23cc02e1116dfbal,
        0x76cd563fd87d7776l } },
    /* 24 << 133 */
    { { 0xe2a37598a9d82abfl,0x5f188ccbe6c170f5l,0x816822005066b087l,
        0xda22c212c7155adal },
      { 0x151e5d3afbddb479l,0x4b606b846d715b99l,0x4a73b54bf997cb2el,
        0x9a1bfe433ecd8b66l } },
    /* 25 << 133 */
    { { 0x1c3128092a67d48al,0xcd6a671e031fa9e2l,0xbec3312a0e43a34al,
        0x1d93563955ef47d3l },
      { 0x5ea024898fea73eal,0x8247b364a035afb2l,0xb58300a65265b54cl,
        0x3286662f722c7148l } },
    /* 26 << 133 */
    { { 0xb77fd76bb4ec4c20l,0xf0a12fa70f3fe3fdl,0xf845bbf541d8c7e8l,
        0xe4d969ca5ec10aa8l },
      { 0x4c0053b743e232a3l,0xdc7a3fac37f8a45al,0x3c4261c520d81c8fl,
        0xfd4b3453b00eab00l } },
    /* 27 << 133 */
    { { 0x76d48f86d36e3062l,0x626c5277a143ff02l,0x538174deaf76f42el,
        0x2267aa866407ceacl },
      { 0xfad7635172e572d5l,0xab861af7ba7330ebl,0xa0a1c8c7418d8657l,
        0x988821cb20289a52l } },
    /* 28 << 133 */
    { { 0x79732522cccc18adl,0xaadf3f8df1a6e027l,0xf7382c9317c2354dl,
        0x5ce1680cd818b689l },
      { 0x359ebbfcd9ecbee9l,0x4330689c1cae62acl,0xb55ce5b4c51ac38al,
        0x7921dfeafe238ee8l } },
    /* 29 << 133 */
    { { 0x3972bef8271d1ca5l,0x3e423bc7e8aabd18l,0x57b09f3f44a3e5e3l,
        0x5da886ae7b444d66l },
      { 0x68206634a9964375l,0x356a2fa3699cd0ffl,0xaf0faa24dba515e9l,
        0x536e1f5cb321d79al } },
    /* 30 << 133 */
    { { 0xd3b9913a5c04e4eal,0xd549dcfed6f11513l,0xee227bf579fd1d94l,
        0x9f35afeeb43f2c67l },
      { 0xd2638d24f1314f53l,0x62baf948cabcd822l,0x5542de294ef48db0l,
        0xb3eb6a04fc5f6bb2l } },
    /* 31 << 133 */
    { { 0x23c110ae1208e16al,0x1a4d15b5f8363e24l,0x30716844164be00bl,
        0xa8e24824f6f4690dl },
      { 0x548773a290b170cfl,0xa1bef33142f191f4l,0x70f418d09247aa97l,
        0xea06028e48be9147l } },
    /* 32 << 133 */
    { { 0xe13122f3dbfb894el,0xbe9b79f6ce274b18l,0x85a49de5ca58aadfl,
        0x2495775811487351l },
      { 0x111def61bb939099l,0x1d6a974a26d13694l,0x4474b4ced3fc253bl,
        0x3a1485e64c5db15el } },
    /* 33 << 133 */
    { { 0xe79667b4147c15b4l,0xe34f553b7bc61301l,0x032b80f817094381l,
        0x55d8bafd723eaa21l },
      { 0x5a987995f1c0e74el,0x5a9b292eebba289cl,0x413cd4b2eb4c8251l,
        0x98b5d243d162db0al } },
    /* 34 << 133 */
    { { 0xbb47bf6668342520l,0x08d68949baa862d1l,0x11f349c7e906abcdl,
        0x454ce985ed7bf00el },
      { 0xacab5c9eb55b803bl,0xb03468ea31e3c16dl,0x5c24213dd273bf12l,
        0x211538eb71587887l } },
    /* 35 << 133 */
    { { 0x198e4a2f731dea2dl,0xd5856cf274ed7b2al,0x86a632eb13a664fel,
        0x932cd909bda41291l },
      { 0x850e95d4c0c4ddc0l,0xc0f422f8347fc2c9l,0xe68cbec486076bcbl,
        0xf9e7c0c0cd6cd286l } },
    /* 36 << 133 */
    { { 0x65994ddb0f5f27cal,0xe85461fba80d59ffl,0xff05481a66601023l,
        0xc665427afc9ebbfbl },
      { 0xb0571a697587fd52l,0x935289f88d49efcel,0x61becc60ea420688l,
        0xb22639d913a786afl } },
    /* 37 << 133 */
    { { 0x1a8e6220361ecf90l,0x001f23e025506463l,0xe4ae9b5d0a5c2b79l,
        0xebc9cdadd8149db5l },
      { 0xb33164a1934aa728l,0x750eb00eae9b60f3l,0x5a91615b9b9cfbfdl,
        0x97015cbfef45f7f6l } },
    /* 38 << 133 */
    { { 0xb462c4a5bf5151dfl,0x21adcc41b07118f2l,0xd60c545b043fa42cl,
        0xfc21aa54e96be1abl },
      { 0xe84bc32f4e51ea80l,0x3dae45f0259b5d8dl,0xbb73c7ebc38f1b5el,
        0xe405a74ae8ae617dl } },
    /* 39 << 133 */
    { { 0xbb1ae9c69f1c56bdl,0x8c176b9849f196a4l,0xc448f3116875092bl,
        0xb5afe3de9f976033l },
      { 0xa8dafd49145813e5l,0x687fc4d9e2b34226l,0xf2dfc92d4c7ff57fl,
        0x004e3fc1401f1b46l } },
    /* 40 << 133 */
    { { 0x5afddab61430c9abl,0x0bdd41d32238e997l,0xf0947430418042ael,
        0x71f9addacdddc4cbl },
      { 0x7090c016c52dd907l,0xd9bdf44d29e2047fl,0xe6f1fe801b1011a6l,
        0xb63accbcd9acdc78l } },
    /* 41 << 133 */
    { { 0xcfc7e2351272a95bl,0x0c667717a6276ac8l,0x3c0d3709e2d7eef7l,
        0x5add2b069a685b3el },
      { 0x363ad32d14ea5d65l,0xf8e01f068d7dd506l,0xc9ea221375b4aac6l,
        0xed2a2bf90d353466l } },
    /* 42 << 133 */
    { { 0x439d79b5e9d3a7c3l,0x8e0ee5a681b7f34bl,0xcf3dacf51dc4ba75l,
        0x1d3d1773eb3310c7l },
      { 0xa8e671127747ae83l,0x31f43160197d6b40l,0x0521cceecd961400l,
        0x67246f11f6535768l } },
    /* 43 << 133 */
    { { 0x702fcc5aef0c3133l,0x247cc45d7e16693bl,0xfd484e49c729b749l,
        0x522cef7db218320fl },
      { 0xe56ef40559ab93b3l,0x225fba119f181071l,0x33bd659515330ed0l,
        0xc4be69d51ddb32f7l } },
    /* 44 << 133 */
    { { 0x264c76680448087cl,0xac30903f71432dael,0x3851b26600f9bf47l,
        0x400ed3116cdd6d03l },
      { 0x045e79fef8fd2424l,0xfdfd974afa6da98bl,0x45c9f6410c1e673al,
        0x76f2e7335b2c5168l } },
    /* 45 << 133 */
    { { 0x1adaebb52a601753l,0xb286514cc57c2d49l,0xd87696701e0bfd24l,
        0x950c547e04478922l },
      { 0xd1d41969e5d32bfel,0x30bc1472750d6c3el,0x8f3679fee0e27f3al,
        0x8f64a7dca4a6ee0cl } },
    /* 46 << 133 */
    { { 0x2fe59937633dfb1fl,0xea82c395977f2547l,0xcbdfdf1a661ea646l,
        0xc7ccc591b9085451l },
      { 0x8217796281761e13l,0xda57596f9196885cl,0xbc17e84928ffbd70l,
        0x1e6e0a412671d36fl } },
    /* 47 << 133 */
    { { 0x61ae872c4152fcf5l,0x441c87b09e77e754l,0xd0799dd5a34dff09l,
        0x766b4e4488a6b171l },
      { 0xdc06a51211f1c792l,0xea02ae934be35c3el,0xe5ca4d6de90c469el,
        0x4df4368e56e4ff5cl } },
    /* 48 << 133 */
    { { 0x7817acab4baef62el,0x9f5a2202a85b91e8l,0x9666ebe66ce57610l,
        0x32ad31f3f73bfe03l },
      { 0x628330a425bcf4d6l,0xea950593515056e6l,0x59811c89e1332156l,
        0xc89cf1fe8c11b2d7l } },
    /* 49 << 133 */
    { { 0x75b6391304e60cc0l,0xce811e8d4625d375l,0x030e43fc2d26e562l,
        0xfbb30b4b608d36a0l },
      { 0x634ff82c48528118l,0x7c6fe085cd285911l,0x7f2830c099358f28l,
        0x2e60a95e665e6c09l } },
    /* 50 << 133 */
    { { 0x08407d3d9b785dbfl,0x530889aba759bce7l,0xf228e0e652f61239l,
        0x2b6d14616879be3cl },
      { 0xe6902c0451a7bbf7l,0x30ad99f076f24a64l,0x66d9317a98bc6da0l,
        0xf4f877f3cb596ac0l } },
    /* 51 << 133 */
    { { 0xb05ff62d4c44f119l,0x4555f536e9b77416l,0xc7c0d0598caed63bl,
        0x0cd2b7cec358b2a9l },
      { 0x3f33287b46945fa3l,0xf8785b20d67c8791l,0xc54a7a619637bd08l,
        0x54d4598c18be79d7l } },
    /* 52 << 133 */
    { { 0x889e5acbc46d7ce1l,0x9a515bb78b085877l,0xfac1a03d0b7a5050l,
        0x7d3e738af2926035l },
      { 0x861cc2ce2a6cb0ebl,0x6f2e29558f7adc79l,0x61c4d45133016376l,
        0xd9fd2c805ad59090l } },
    /* 53 << 133 */
    { { 0xe5a83738b2b836a1l,0x855b41a07c0d6622l,0x186fe3177cc19af1l,
        0x6465c1fffdd99acbl },
      { 0x46e5c23f6974b99el,0x75a7cf8ba2717cbel,0x4d2ebc3f062be658l,
        0x094b44475f209c98l } },
    /* 54 << 133 */
    { { 0x4af285edb940cb5al,0x6706d7927cc82f10l,0xc8c8776c030526fal,
        0xfa8e6f76a0da9140l },
      { 0x77ea9d34591ee4f0l,0x5f46e33740274166l,0x1bdf98bbea671457l,
        0xd7c08b46862a1fe2l } },
    /* 55 << 133 */
    { { 0x46cc303c1c08ad63l,0x995434404c845e7bl,0x1b8fbdb548f36bf7l,
        0x5b82c3928c8273a7l },
      { 0x08f712c4928435d5l,0x071cf0f179330380l,0xc74c2d24a8da054al,
        0xcb0e720143c46b5cl } },
    /* 56 << 133 */
    { { 0x0ad7337ac0b7eff3l,0x8552225ec5e48b3cl,0xe6f78b0c73f13a5fl,
        0x5e70062e82349cbel },
      { 0x6b8d5048e7073969l,0x392d2a29c33cb3d2l,0xee4f727c4ecaa20fl,
        0xa068c99e2ccde707l } },
    /* 57 << 133 */
    { { 0xfcd5651fb87a2913l,0xea3e3c153cc252f0l,0x777d92df3b6cd3e4l,
        0x7a414143c5a732e7l },
      { 0xa895951aa71ff493l,0xfe980c92bbd37cf6l,0x45bd5e64decfeeffl,
        0x910dc2a9a44c43e9l } },
    /* 58 << 133 */
    { { 0xcb403f26cca9f54dl,0x928bbdfb9303f6dbl,0x3c37951ea9eee67cl,
        0x3bd61a52f79961c3l },
      { 0x09a238e6395c9a79l,0x6940ca2d61eb352dl,0x7d1e5c5ec1875631l,
        0x1e19742c1e1b20d1l } },
    /* 59 << 133 */
    { { 0x4633d90823fc2e6el,0xa76e29a908959149l,0x61069d9c84ed7da5l,
        0x0baa11cf5dbcad51l },
      { 0xd01eec64961849dal,0x93b75f1faf3d8c28l,0x57bc4f9f1ca2ee44l,
        0x5a26322d00e00558l } },
    /* 60 << 133 */
    { { 0x1888d65861a023efl,0x1d72aab4b9e5246el,0xa9a26348e5563ec0l,
        0xa0971963c3439a43l },
      { 0x567dd54badb9b5b7l,0x73fac1a1c45a524bl,0x8fe97ef7fe38e608l,
        0x608748d23f384f48l } },
    /* 61 << 133 */
    { { 0xb0571794c486094fl,0x869254a38bf3a8d6l,0x148a8dd1310b0e25l,
        0x99ab9f3f9aa3f7d8l },
      { 0x0927c68a6706c02el,0x22b5e76c69790e6cl,0x6c3252606c71376cl,
        0x53a5769009ef6657l } },
    /* 62 << 133 */
    { { 0x8d63f852edffcf3al,0xb4d2ed043c0a6f55l,0xdb3aa8de12519b9el,
        0x5d38e9c41e0a569al },
      { 0x871528bf303747e2l,0xa208e77cf5b5c18dl,0x9d129c88ca6bf923l,
        0xbcbf197fbf02839fl } },
    /* 63 << 133 */
    { { 0x9b9bf03027323194l,0x3b055a8b339ca59dl,0xb46b23120f669520l,
        0x19789f1f497e5f24l },
      { 0x9c499468aaf01801l,0x72ee11908b69d59cl,0x8bd39595acf4c079l,
        0x3ee11ece8e0cd048l } },
    /* 64 << 133 */
    { { 0xebde86ec1ed66f18l,0x225d906bd61fce43l,0x5cab07d6e8bed74dl,
        0x16e4617f27855ab7l },
      { 0x6568aaddb2fbc3ddl,0xedb5484f8aeddf5bl,0x878f20e86dcf2fadl,
        0x3516497c615f5699l } },
    /* 0 << 140 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 140 */
    { { 0xef0a3fecfa181e69l,0x9ea02f8130d69a98l,0xb2e9cf8e66eab95dl,
        0x520f2beb24720021l },
      { 0x621c540a1df84361l,0x1203772171fa6d5dl,0x6e3c7b510ff5f6ffl,
        0x817a069babb2bef3l } },
    /* 2 << 140 */
    { { 0x83572fb6b294cda6l,0x6ce9bf75b9039f34l,0x20e012f0095cbb21l,
        0xa0aecc1bd063f0dal },
      { 0x57c21c3af02909e5l,0xc7d59ecf48ce9cdcl,0x2732b8448ae336f8l,
        0x056e37233f4f85f4l } },
    /* 3 << 140 */
    { { 0x8a10b53189e800cal,0x50fe0c17145208fdl,0x9e43c0d3b714ba37l,
        0x427d200e34189accl },
      { 0x05dee24fe616e2c0l,0x9c25f4c8ee1854c1l,0x4d3222a58f342a73l,
        0x0807804fa027c952l } },
    /* 4 << 140 */
    { { 0xc222653a4f0d56f3l,0x961e4047ca28b805l,0x2c03f8b04a73434bl,
        0x4c966787ab712a19l },
      { 0xcc196c42864fee42l,0xc1be93da5b0ece5cl,0xa87d9f22c131c159l,
        0x2bb6d593dce45655l } },
    /* 5 << 140 */
    { { 0x22c49ec9b809b7cel,0x8a41486be2c72c2cl,0x813b9420fea0bf36l,
        0xb3d36ee9a66dac69l },
      { 0x6fddc08a328cc987l,0x0a3bcd2c3a326461l,0x7103c49dd810dbbal,
        0xf9d81a284b78a4c4l } },
    /* 6 << 140 */
    { { 0x3de865ade4d55941l,0xdedafa5e30384087l,0x6f414abb4ef18b9bl,
        0x9ee9ea42faee5268l },
      { 0x260faa1637a55a4al,0xeb19a514015f93b9l,0x51d7ebd29e9c3598l,
        0x523fc56d1932178el } },
    /* 7 << 140 */
    { { 0x501d070cb98fe684l,0xd60fbe9a124a1458l,0xa45761c892bc6b3fl,
        0xf5384858fe6f27cbl },
      { 0x4b0271f7b59e763bl,0x3d4606a95b5a8e5el,0x1eda5d9b05a48292l,
        0xda7731d0e6fec446l } },
    /* 8 << 140 */
    { { 0xa3e3369390d45871l,0xe976404006166d8dl,0xb5c3368289a90403l,
        0x4bd1798372f1d637l },
      { 0xa616679ed5d2c53al,0x5ec4bcd8fdcf3b87l,0xae6d7613b66a694el,
        0x7460fc76e3fc27e5l } },
    /* 9 << 140 */
    { { 0x70469b8295caabeel,0xde024ca5889501e3l,0x6bdadc06076ed265l,
        0x0cb1236b5a0ef8b2l },
      { 0x4065ddbf0972ebf9l,0xf1dd387522aca432l,0xa88b97cf744aff76l,
        0xd1359afdfe8e3d24l } },
    /* 10 << 140 */
    { { 0x52a3ba2b91502cf3l,0x2c3832a8084db75dl,0x04a12dddde30b1c9l,
        0x7802eabce31fd60cl },
      { 0x33707327a37fddabl,0x65d6f2abfaafa973l,0x3525c5b811e6f91al,
        0x76aeb0c95f46530bl } },
    /* 11 << 140 */
    { { 0xe8815ff62f93a675l,0xa6ec968405f48679l,0x6dcbb556358ae884l,
        0x0af61472e19e3873l },
      { 0x72334372a5f696bel,0xc65e57ea6f22fb70l,0x268da30c946cea90l,
        0x136a8a8765681b2al } },
    /* 12 << 140 */
    { { 0xad5e81dc0f9f44d4l,0xf09a69602c46585al,0xd1649164c447d1b1l,
        0x3b4b36c8879dc8b1l },
      { 0x20d4177b3b6b234cl,0x096a25051730d9d0l,0x0611b9b8ef80531dl,
        0xba904b3b64bb495dl } },
    /* 13 << 140 */
    { { 0x1192d9d493a3147al,0x9f30a5dc9a565545l,0x90b1f9cb6ef07212l,
        0x299585460d87fc13l },
      { 0xd3323effc17db9bal,0xcb18548ccb1644a8l,0x18a306d44f49ffbcl,
        0x28d658f14c2e8684l } },
    /* 14 << 140 */
    { { 0x44ba60cda99f8c71l,0x67b7abdb4bf742ffl,0x66310f9c914b3f99l,
        0xae430a32f412c161l },
      { 0x1e6776d388ace52fl,0x4bc0fa2452d7067dl,0x03c286aa8f07cd1bl,
        0x4cb8f38ca985b2c1l } },
    /* 15 << 140 */
    { { 0x83ccbe808c3bff36l,0x005a0bd25263e575l,0x460d7dda259bdcd1l,
        0x4a1c5642fa5cab6bl },
      { 0x2b7bdbb99fe4fc88l,0x09418e28cc97bbb5l,0xd8274fb4a12321ael,
        0xb137007d5c87b64el } },
    /* 16 << 140 */
    { { 0x80531fe1c63c4962l,0x50541e89981fdb25l,0xdc1291a1fd4c2b6bl,
        0xc0693a17a6df4fcal },
      { 0xb2c4604e0117f203l,0x245f19630a99b8d0l,0xaedc20aac6212c44l,
        0xb1ed4e56520f52a8l } },
    /* 17 << 140 */
    { { 0xfe48f575f8547be3l,0x0a7033cda9e45f98l,0x4b45d3a918c50100l,
        0xb2a6cd6aa61d41dal },
      { 0x60bbb4f557933c6bl,0xa7538ebd2b0d7ffcl,0x9ea3ab8d8cd626b6l,
        0x8273a4843601625al } },
    /* 18 << 140 */
    { { 0x888598450168e508l,0x8cbc9bb299a94abdl,0x713ac792fab0a671l,
        0xa3995b196c9ebffcl },
      { 0xe711668e1239e152l,0x56892558bbb8dff4l,0x8bfc7dabdbf17963l,
        0x5b59fe5ab3de1253l } },
    /* 19 << 140 */
    { { 0x7e3320eb34a9f7ael,0xe5e8cf72d751efe4l,0x7ea003bcd9be2f37l,
        0xc0f551a0b6c08ef7l },
      { 0x56606268038f6725l,0x1dd38e356d92d3b6l,0x07dfce7cc3cbd686l,
        0x4e549e04651c5da8l } },
    /* 20 << 140 */
    { { 0x4058f93b08b19340l,0xc2fae6f4cac6d89dl,0x4bad8a8c8f159cc7l,
        0x0ddba4b3cb0b601cl },
      { 0xda4fc7b51dd95f8cl,0x1d163cd7cea5c255l,0x30707d06274a8c4cl,
        0x79d9e0082802e9cel } },
    /* 21 << 140 */
    { { 0x02a29ebfe6ddd505l,0x37064e74b50bed1al,0x3f6bae65a7327d57l,
        0x3846f5f1f83920bcl },
      { 0x87c3749160df1b9bl,0x4cfb28952d1da29fl,0x10a478ca4ed1743cl,
        0x390c60303edd47c6l } },
    /* 22 << 140 */
    { { 0x8f3e53128c0a78del,0xccd02bda1e85df70l,0xd6c75c03a61b6582l,
        0x0762921cfc0eebd1l },
      { 0xd34d0823d85010c0l,0xd73aaacb0044cf1fl,0xfb4159bba3b5e78al,
        0x2287c7f7e5826f3fl } },
    /* 23 << 140 */
    { { 0x4aeaf742580b1a01l,0xf080415d60423b79l,0xe12622cda7dea144l,
        0x49ea499659d62472l },
      { 0xb42991ef571f3913l,0x0610f214f5b25a8al,0x47adc58530b79e8fl,
        0xf90e3df607a065a2l } },
    /* 24 << 140 */
    { { 0x5d0a5deb43e2e034l,0x53fb5a34444024aal,0xa8628c686b0c9f7fl,
        0x9c69c29cac563656l },
      { 0x5a231febbace47b6l,0xbdce02899ea5a2ecl,0x05da1fac9463853el,
        0x96812c52509e78aal } },
    /* 25 << 140 */
    { { 0xd3fb577157151692l,0xeb2721f8d98e1c44l,0xc050608732399be1l,
        0xda5a5511d979d8b8l },
      { 0x737ed55dc6f56780l,0xe20d30040dc7a7f4l,0x02ce7301f5941a03l,
        0x91ef5215ed30f83al } },
    /* 26 << 140 */
    { { 0x28727fc14092d85fl,0x72d223c65c49e41al,0xa7cf30a2ba6a4d81l,
        0x7c086209b030d87dl },
      { 0x04844c7dfc588b09l,0x728cd4995874bbb0l,0xcc1281eee84c0495l,
        0x0769b5baec31958fl } },
    /* 27 << 140 */
    { { 0x665c228bf99c2471l,0xf2d8a11b191eb110l,0x4594f494d36d7024l,
        0x482ded8bcdcb25a1l },
      { 0xc958a9d8dadd4885l,0x7004477ef1d2b547l,0x0a45f6ef2a0af550l,
        0x4fc739d62f8d6351l } },
    /* 28 << 140 */
    { { 0x75cdaf27786f08a9l,0x8700bb2642c2737fl,0x855a71411c4e2670l,
        0x810188c115076fefl },
      { 0xc251d0c9abcd3297l,0xae4c8967f48108ebl,0xbd146de718ceed30l,
        0xf9d4f07ac986bcedl } },
    /* 29 << 140 */
    { { 0x5ad98ed583fa1e08l,0x7780d33ebeabd1fbl,0xe330513c903b1196l,
        0xba11de9ea47bc8c4l },
      { 0x684334da02c2d064l,0x7ecf360da48de23bl,0x57a1b4740a9089d8l,
        0xf28fa439ff36734cl } },
    /* 30 << 140 */
    { { 0xf2a482cbea4570b3l,0xee65d68ba5ebcee9l,0x988d0036b9694cd5l,
        0x53edd0e937885d32l },
      { 0xe37e3307beb9bc6dl,0xe9abb9079f5c6768l,0x4396ccd551f2160fl,
        0x2500888c47336da6l } },
    /* 31 << 140 */
    { { 0x383f9ed9926fce43l,0x809dd1c704da2930l,0x30f6f5968a4cb227l,
        0x0d700c7f73a56b38l },
      { 0x1825ea33ab64a065l,0xaab9b7351338df80l,0x1516100d9b63f57fl,
        0x2574395a27a6a634l } },
    /* 32 << 140 */
    { { 0xb5560fb6700a1acdl,0xe823fd73fd999681l,0xda915d1f6cb4e1bal,
        0x0d0301186ebe00a3l },
      { 0x744fb0c989fca8cdl,0x970d01dbf9da0e0bl,0x0ad8c5647931d76fl,
        0xb15737bff659b96al } },
    /* 33 << 140 */
    { { 0xdc9933e8a8b484e7l,0xb2fdbdf97a26dec7l,0x2349e9a49f1f0136l,
        0x7860368e70fddddbl },
      { 0xd93d2c1cf9ad3e18l,0x6d6c5f17689f4e79l,0x7a544d91b24ff1b6l,
        0x3e12a5ebfe16cd8cl } },
    /* 34 << 140 */
    { { 0x543574e9a56b872fl,0xa1ad550cfcf68ea2l,0x689e37d23f560ef7l,
        0x8c54b9cac9d47a8bl },
      { 0x46d40a4a088ac342l,0xec450c7c1576c6d0l,0xb589e31c1f9689e9l,
        0xdacf2602b8781718l } },
    /* 35 << 140 */
    { { 0xa89237c6c8cb6b42l,0x1326fc93b96ef381l,0x55d56c6db5f07825l,
        0xacba2eea7449e22dl },
      { 0x74e0887a633c3000l,0xcb6cd172d7cbcf71l,0x309e81dec36cf1bel,
        0x07a18a6d60ae399bl } },
    /* 36 << 140 */
    { { 0xb36c26799edce57el,0x52b892f4df001d41l,0xd884ae5d16a1f2c6l,
        0x9b329424efcc370al },
      { 0x3120daf2bd2e21dfl,0x55298d2d02470a99l,0x0b78af6ca05db32el,
        0x5c76a331601f5636l } },
    /* 37 << 140 */
    { { 0xaae861fff8a4f29cl,0x70dc9240d68f8d49l,0x960e649f81b1321cl,
        0x3d2c801b8792e4cel },
      { 0xf479f77242521876l,0x0bed93bc416c79b1l,0xa67fbc05263e5bc9l,
        0x01e8e630521db049l } },
    /* 38 << 140 */
    { { 0x76f26738c6f3431el,0xe609cb02e3267541l,0xb10cff2d818c877cl,
        0x1f0e75ce786a13cbl },
      { 0xf4fdca641158544dl,0x5d777e896cb71ed0l,0x3c233737a9aa4755l,
        0x7b453192e527ab40l } },
    /* 39 << 140 */
    { { 0xdb59f68839f05ffel,0x8f4f4be06d82574el,0xcce3450cee292d1bl,
        0xaa448a1261ccd086l },
      { 0xabce91b3f7914967l,0x4537f09b1908a5edl,0xa812421ef51042e7l,
        0xfaf5cebcec0b3a34l } },
    /* 40 << 140 */
    { { 0x730ffd874ca6b39al,0x70fb72ed02efd342l,0xeb4735f9d75c8edbl,
        0xc11f2157c278aa51l },
      { 0xc459f635bf3bfebfl,0x3a1ff0b46bd9601fl,0xc9d12823c420cb73l,
        0x3e9af3e23c2915a3l } },
    /* 41 << 140 */
    { { 0xe0c82c72b41c3440l,0x175239e5e3039a5fl,0xe1084b8a558795a3l,
        0x328d0a1dd01e5c60l },
      { 0x0a495f2ed3788a04l,0x25d8ff1666c11a9fl,0xf5155f059ed692d6l,
        0x954fa1074f425fe4l } },
    /* 42 << 140 */
    { { 0xd16aabf2e98aaa99l,0x90cd8ba096b0f88al,0x957f4782c154026al,
        0x54ee073452af56d2l },
      { 0xbcf89e5445b4147al,0x3d102f219a52816cl,0x6808517e39b62e77l,
        0x92e2542169169ad8l } },
    /* 43 << 140 */
    { { 0xd721d871bb608558l,0x60e4ebaef6d4ff9bl,0x0ba1081941f2763el,
        0xca2e45be51ee3247l },
      { 0x66d172ec2bfd7a5fl,0x528a8f2f74d0b12dl,0xe17f1e38dabe70dcl,
        0x1d5d73169f93983cl } },
    /* 44 << 140 */
    { { 0x51b2184adf423e31l,0xcb417291aedb1a10l,0x2054ca93625bcab9l,
        0x54396860a98998f0l },
      { 0x4e53f6c4a54ae57el,0x0ffeb590ee648e9dl,0xfbbdaadc6afaf6bcl,
        0xf88ae796aa3bfb8al } },
    /* 45 << 140 */
    { { 0x209f1d44d2359ed9l,0xac68dd03f3544ce2l,0xf378da47fd51e569l,
        0xe1abd8602cc80097l },
      { 0x23ca18d9343b6e3al,0x480797e8b40a1bael,0xd1f0c717533f3e67l,
        0x4489697006e6cdfcl } },
    /* 46 << 140 */
    { { 0x8ca2105552a82e8dl,0xb2caf78578460cdcl,0x4c1b7b62e9037178l,
        0xefc09d2cdb514b58l },
      { 0x5f2df9ee9113be5cl,0x2fbda78fb3f9271cl,0xe09a81af8f83fc54l,
        0x06b138668afb5141l } },
    /* 47 << 140 */
    { { 0x38f6480f43e3865dl,0x72dd77a81ddf47d9l,0xf2a8e9714c205ff7l,
        0x46d449d89d088ad8l },
      { 0x926619ea185d706fl,0xe47e02ebc7dd7f62l,0xe7f120a78cbc2031l,
        0xc18bef00998d4ac9l } },
    /* 48 << 140 */
    { { 0x18f37a9c6bdf22dal,0xefbc432f90dc82dfl,0xc52cef8e5d703651l,
        0x82887ba0d99881a5l },
      { 0x7cec9ddab920ec1dl,0xd0d7e8c3ec3e8d3bl,0x445bc3954ca88747l,
        0xedeaa2e09fd53535l } },
    /* 49 << 140 */
    { { 0x461b1d936cc87475l,0xd92a52e26d2383bdl,0xfabccb59d7903546l,
        0x6111a7613d14b112l },
      { 0x0ae584feb3d5f612l,0x5ea69b8d60e828ecl,0x6c07898554087030l,
        0x649cab04ac4821fel } },
    /* 50 << 140 */
    { { 0x25ecedcf8bdce214l,0xb5622f7286af7361l,0x0e1227aa7038b9e2l,
        0xd0efb273ac20fa77l },
      { 0x817ff88b79df975bl,0x856bf2861999503el,0xb4d5351f5038ec46l,
        0x740a52c5fc42af6el } },
    /* 51 << 140 */
    { { 0x2e38bb152cbb1a3fl,0xc3eb99fe17a83429l,0xca4fcbf1dd66bb74l,
        0x880784d6cde5e8fcl },
      { 0xddc84c1cb4e7a0bel,0x8780510dbd15a72fl,0x44bcf1af81ec30e1l,
        0x141e50a80a61073el } },
    /* 52 << 140 */
    { { 0x0d95571847be87ael,0x68a61417f76a4372l,0xf57e7e87c607c3d3l,
        0x043afaf85252f332l },
      { 0xcc14e1211552a4d2l,0xb6dee692bb4d4ab4l,0xb6ab74c8a03816a4l,
        0x84001ae46f394a29l } },
    /* 53 << 140 */
    { { 0x5bed8344d795fb45l,0x57326e7db79f55a5l,0xc9533ce04accdffcl,
        0x53473caf3993fa04l },
      { 0x7906eb93a13df4c8l,0xa73e51f697cbe46fl,0xd1ab3ae10ae4ccf8l,
        0x256145088a5b3dbcl } },
    /* 54 << 140 */
    { { 0x61eff96211a71b27l,0xdf71412b6bb7fa39l,0xb31ba6b82bd7f3efl,
        0xb0b9c41569180d29l },
      { 0xeec14552014cdde5l,0x702c624b227b4bbbl,0x2b15e8c2d3e988f3l,
        0xee3bcc6da4f7fd04l } },
    /* 55 << 140 */
    { { 0x9d00822a42ac6c85l,0x2db0cea61df9f2b7l,0xd7cad2ab42de1e58l,
        0x346ed5262d6fbb61l },
      { 0xb39629951a2faf09l,0x2fa8a5807c25612el,0x30ae04da7cf56490l,
        0x756629080eea3961l } },
    /* 56 << 140 */
    { { 0x3609f5c53d080847l,0xcb081d395241d4f6l,0xb4fb381077961a63l,
        0xc20c59842abb66fcl },
      { 0x3d40aa7cf902f245l,0x9cb127364e536b1el,0x5eda24da99b3134fl,
        0xafbd9c695cd011afl } },
    /* 57 << 140 */
    { { 0x9a16e30ac7088c7dl,0x5ab657103207389fl,0x1b09547fe7407a53l,
        0x2322f9d74fdc6eabl },
      { 0xc0f2f22d7430de4dl,0x19382696e68ca9a9l,0x17f1eff1918e5868l,
        0xe3b5b635586f4204l } },
    /* 58 << 140 */
    { { 0x146ef9803fbc4341l,0x359f2c805b5eed4el,0x9f35744e7482e41dl,
        0x9a9ac3ecf3b224c2l },
      { 0x9161a6fe91fc50ael,0x89ccc66bc613fa7cl,0x89268b14c732f15al,
        0x7cd6f4e2b467ed03l } },
    /* 59 << 140 */
    { { 0xfbf79869ce56b40el,0xf93e094cc02dde98l,0xefe0c3a8edee2cd7l,
        0x90f3ffc0b268fd42l },
      { 0x81a7fd5608241aedl,0x95ab7ad800b1afe8l,0x401270563e310d52l,
        0xd3ffdeb109d9fc43l } },
    /* 60 << 140 */
    { { 0xc8f85c91d11a8594l,0x2e74d25831cf6db8l,0x829c7ca302b5dfd0l,
        0xe389cfbe69143c86l },
      { 0xd01b6405941768d8l,0x4510399503bf825dl,0xcc4ee16656cd17e2l,
        0xbea3c283ba037e79l } },
    /* 61 << 140 */
    { { 0x4e1ac06ed9a47520l,0xfbfe18aaaf852404l,0x5615f8e28087648al,
        0x7301e47eb9d150d9l },
      { 0x79f9f9ddb299b977l,0x76697a7ba5b78314l,0x10d674687d7c90e7l,
        0x7afffe03937210b5l } },
    /* 62 << 140 */
    { { 0x5aef3e4b28c22ceel,0xefb0ecd809fd55ael,0x4cea71320d2a5d6al,
        0x9cfb5fa101db6357l },
      { 0x395e0b57f36e1ac5l,0x008fa9ad36cafb7dl,0x8f6cdf705308c4dbl,
        0x51527a3795ed2477l } },
    /* 63 << 140 */
    { { 0xba0dee305bd21311l,0x6ed41b22909c90d7l,0xc5f6b7587c8696d3l,
        0x0db8eaa83ce83a80l },
      { 0xd297fe37b24b4b6fl,0xfe58afe8522d1f0dl,0x973587368c98dbd9l,
        0x6bc226ca9454a527l } },
    /* 64 << 140 */
    { { 0xa12b384ece53c2d0l,0x779d897d5e4606dal,0xa53e47b073ec12b0l,
        0x462dbbba5756f1adl },
      { 0x69fe09f2cafe37b6l,0x273d1ebfecce2e17l,0x8ac1d5383cf607fdl,
        0x8035f7ff12e10c25l } },
    /* 0 << 147 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 147 */
    { { 0x854d34c77e6c5520l,0xc27df9efdcb9ea58l,0x405f2369d686666dl,
        0x29d1febf0417aa85l },
      { 0x9846819e93470afel,0x3e6a9669e2a27f9el,0x24d008a2e31e6504l,
        0xdba7cecf9cb7680al } },
    /* 2 << 147 */
    { { 0xecaff541338d6e43l,0x56f7dd734541d5ccl,0xb5d426de96bc88cal,
        0x48d94f6b9ed3a2c3l },
      { 0x6354a3bb2ef8279cl,0xd575465b0b1867f2l,0xef99b0ff95225151l,
        0xf3e19d88f94500d8l } },
    /* 3 << 147 */
    { { 0x92a83268e32dd620l,0x913ec99f627849a2l,0xedd8fdfa2c378882l,
        0xaf96f33eee6f8cfel },
      { 0xc06737e5dc3fa8a5l,0x236bb531b0b03a1dl,0x33e59f2989f037b0l,
        0x13f9b5a7d9a12a53l } },
    /* 4 << 147 */
    { { 0x0d0df6ce51efb310l,0xcb5b2eb4958df5bel,0xd6459e2936158e59l,
        0x82aae2b91466e336l },
      { 0xfb658a39411aa636l,0x7152ecc5d4c0a933l,0xf10c758a49f026b7l,
        0xf4837f97cb09311fl } },
    /* 5 << 147 */
    { { 0xddfb02c4c753c45fl,0x18ca81b6f9c840fel,0x846fd09ab0f8a3e6l,
        0xb1162adde7733dbcl },
      { 0x7070ad20236e3ab6l,0xf88cdaf5b2a56326l,0x05fc8719997cbc7al,
        0x442cd4524b665272l } },
    /* 6 << 147 */
    { { 0x7807f364b71698f5l,0x6ba418d29f7b605el,0xfd20b00fa03b2cbbl,
        0x883eca37da54386fl },
      { 0xff0be43ff3437f24l,0xe910b432a48bb33cl,0x4963a128329df765l,
        0xac1dd556be2fe6f7l } },
    /* 7 << 147 */
    { { 0x557610f924a0a3fcl,0x38e17bf4e881c3f9l,0x6ba84fafed0dac99l,
        0xd4a222c359eeb918l },
      { 0xc79c1dbe13f542b6l,0x1fc65e0de425d457l,0xeffb754f1debb779l,
        0x638d8fd09e08af60l } },
    /* 8 << 147 */
    { { 0x994f523a626332d5l,0x7bc388335561bb44l,0x005ed4b03d845ea2l,
        0xd39d3ee1c2a1f08al },
      { 0x6561fdd3e7676b0dl,0x620e35fffb706017l,0x36ce424ff264f9a8l,
        0xc4c3419fda2681f7l } },
    /* 9 << 147 */
    { { 0xfb6afd2f69beb6e8l,0x3a50b9936d700d03l,0xc840b2ad0c83a14fl,
        0x573207be54085befl },
      { 0x5af882e309fe7e5bl,0x957678a43b40a7e1l,0x172d4bdd543056e2l,
        0x9c1b26b40df13c0al } },
    /* 10 << 147 */
    { { 0x1c30861cf405ff06l,0xebac86bd486e828bl,0xe791a971636933fcl,
        0x50e7c2be7aeee947l },
      { 0xc3d4a095fa90d767l,0xae60eb7be670ab7bl,0x17633a64397b056dl,
        0x93a21f33105012aal } },
    /* 11 << 147 */
    { { 0x663c370babb88643l,0x91df36d722e21599l,0x183ba8358b761671l,
        0x381eea1d728f3bf1l },
      { 0xb9b2f1ba39966e6cl,0x7c464a28e7295492l,0x0fd5f70a09b26b7fl,
        0xa9aba1f9fbe009dfl } },
    /* 12 << 147 */
    { { 0x857c1f22369b87adl,0x3c00e5d932fca556l,0x1ad74cab90b06466l,
        0xa7112386550faaf2l },
      { 0x7435e1986d9bd5f5l,0x2dcc7e3859c3463fl,0xdc7df748ca7bd4b2l,
        0x13cd4c089dec2f31l } },
    /* 13 << 147 */
    { { 0x0d3b5df8e3237710l,0x0dadb26ecbd2f7b0l,0x9f5966abe4aa082bl,
        0x666ec8de350e966el },
      { 0x1bfd1ed5ee524216l,0xcd93c59b41dab0b6l,0x658a8435d186d6bal,
        0x1b7d34d2159d1195l } },
    /* 14 << 147 */
    { { 0x5936e46022caf46bl,0x6a45dd8f9a96fe4fl,0xf7925434b98f474el,
        0x414104120053ef15l },
      { 0x71cf8d1241de97bfl,0xb8547b61bd80bef4l,0xb47d3970c4db0037l,
        0xf1bcd328fef20dffl } },
    /* 15 << 147 */
    { { 0x31a92e0910caad67l,0x1f5919605531a1e1l,0x3bb852e05f4fc840l,
        0x63e297ca93a72c6cl },
      { 0x3c2b0b2e49abad67l,0x6ec405fced3db0d9l,0xdc14a5307fef1d40l,
        0xccd19846280896fcl } },
    /* 16 << 147 */
    { { 0x00f831769bb81648l,0xd69eb485653120d0l,0xd17d75f44ccabc62l,
        0x34a07f82b749fcb1l },
      { 0x2c3af787bbfb5554l,0xb06ed4d062e283f8l,0x5722889fa19213a0l,
        0x162b085edcf3c7b4l } },
    /* 17 << 147 */
    { { 0xbcaecb31e0dd3ecal,0xc6237fbce52f13a5l,0xcc2b6b0327bac297l,
        0x2ae1cac5b917f54al },
      { 0x474807d47845ae4fl,0xfec7dd92ce5972e0l,0xc3bd25411d7915bbl,
        0x66f85dc4d94907cal } },
    /* 18 << 147 */
    { { 0xd981b888bdbcf0cal,0xd75f5da6df279e9fl,0x128bbf247054e934l,
        0x3c6ff6e581db134bl },
      { 0x795b7cf4047d26e4l,0xf370f7b85049ec37l,0xc6712d4dced945afl,
        0xdf30b5ec095642bcl } },
    /* 19 << 147 */
    { { 0x9b034c624896246el,0x5652c016ee90bbd1l,0xeb38636f87fedb73l,
        0x5e32f8470135a613l },
      { 0x0703b312cf933c83l,0xd05bb76e1a7f47e6l,0x825e4f0c949c2415l,
        0x569e56227250d6f8l } },
    /* 20 << 147 */
    { { 0xbbe9eb3a6568013el,0x8dbd203f22f243fcl,0x9dbd7694b342734al,
        0x8f6d12f846afa984l },
      { 0xb98610a2c9eade29l,0xbab4f32347dd0f18l,0x5779737b671c0d46l,
        0x10b6a7c6d3e0a42al } },
    /* 21 << 147 */
    { { 0xfb19ddf33035b41cl,0xd336343f99c45895l,0x61fe493854c857e5l,
        0xc4d506beae4e57d5l },
      { 0x3cd8c8cbbbc33f75l,0x7281f08a9262c77dl,0x083f4ea6f11a2823l,
        0x8895041e9fba2e33l } },
    /* 22 << 147 */
    { { 0xfcdfea499c438edfl,0x7678dcc391edba44l,0xf07b3b87e2ba50f0l,
        0xc13888ef43948c1bl },
      { 0xc2135ad41140af42l,0x8e5104f3926ed1a7l,0xf24430cb88f6695fl,
        0x0ce0637b6d73c120l } },
    /* 23 << 147 */
    { { 0xb2db01e6fe631e8fl,0x1c5563d7d7bdd24bl,0x8daea3ba369ad44fl,
        0x000c81b68187a9f9l },
      { 0x5f48a951aae1fd9al,0xe35626c78d5aed8al,0x209527630498c622l,
        0x76d17634773aa504l } },
    /* 24 << 147 */
    { { 0x36d90ddaeb300f7al,0x9dcf7dfcedb5e801l,0x645cb26874d5244cl,
        0xa127ee79348e3aa2l },
      { 0x488acc53575f1dbbl,0x95037e8580e6161el,0x57e59283292650d0l,
        0xabe67d9914938216l } },
    /* 25 << 147 */
    { { 0x3c7f944b3f8e1065l,0xed908cb6330e8924l,0x08ee8fd56f530136l,
        0x2227b7d5d7ffc169l },
      { 0x4f55c893b5cd6dd5l,0x82225e11a62796e8l,0x5c6cead1cb18e12cl,
        0x4381ae0c84f5a51al } },
    /* 26 << 147 */
    { { 0x345913d37fafa4c8l,0x3d9180820491aac0l,0x9347871f3e69264cl,
        0xbea9dd3cb4f4f0cdl },
      { 0xbda5d0673eadd3e7l,0x0033c1b80573bcd8l,0x255893795da2486cl,
        0xcb89ee5b86abbee7l } },
    /* 27 << 147 */
    { { 0x8fe0a8f322532e5dl,0xb6410ff0727dfc4cl,0x619b9d58226726dbl,
        0x5ec256697a2b2dc7l },
      { 0xaf4d2e064c3beb01l,0x852123d07acea556l,0x0e9470faf783487al,
        0x75a7ea045664b3ebl } },
    /* 28 << 147 */
    { { 0x4ad78f356798e4bal,0x9214e6e5c7d0e091l,0xc420b488b1290403l,
        0x64049e0afc295749l },
      { 0x03ef5af13ae9841fl,0xdbe4ca19b0b662a6l,0x46845c5ffa453458l,
        0xf8dabf1910b66722l } },
    /* 29 << 147 */
    { { 0xb650f0aacce2793bl,0x71db851ec5ec47c1l,0x3eb78f3e3b234fa9l,
        0xb0c60f35fc0106cel },
      { 0x05427121774eadbdl,0x25367fafce323863l,0x7541b5c9cd086976l,
        0x4ff069e2dc507ad1l } },
    /* 30 << 147 */
    { { 0x741452568776e667l,0x6e76142cb23c6bb5l,0xdbf307121b3a8a87l,
        0x60e7363e98450836l },
      { 0x5741450eb7366d80l,0xe4ee14ca4837dbdfl,0xa765eb9b69d4316fl,
        0x04548dca8ef43825l } },
    /* 31 << 147 */
    { { 0x9c9f4e4c5ae888ebl,0x733abb5156e9ac99l,0xdaad3c20ba6ac029l,
        0x9b8dd3d32ba3e38el },
      { 0xa9bb4c920bc5d11al,0xf20127a79c5f88a3l,0x4f52b06e161d3cb8l,
        0x26c1ff096afaf0a6l } },
    /* 32 << 147 */
    { { 0x32670d2f7189e71fl,0xc64387485ecf91e7l,0x15758e57db757a21l,
        0x427d09f8290a9ce5l },
      { 0x846a308f38384a7al,0xaac3acb4b0732b99l,0x9e94100917845819l,
        0x95cba111a7ce5e03l } },
    /* 33 << 147 */
    { { 0x6f3d4f7fb00009c4l,0xb8396c278ff28b5fl,0xb1a9ae431c97975dl,
        0x9d7ba8afe5d9fed5l },
      { 0x338cf09f34f485b6l,0xbc0ddacc64122516l,0xa450da1205d471fel,
        0x4c3a6250628dd8c9l } },
    /* 34 << 147 */
    { { 0x69c7d103d1295837l,0xa2893e503807eb2fl,0xd6e1e1debdb41491l,
        0xc630745b5e138235l },
      { 0xc892109e48661ae1l,0x8d17e7ebea2b2674l,0x00ec0f87c328d6b5l,
        0x6d858645f079ff9el } },
    /* 35 << 147 */
    { { 0x6cdf243e19115eadl,0x1ce1393e4bac4fcfl,0x2c960ed09c29f25bl,
        0x59be4d8e9d388a05l },
      { 0x0d46e06cd0def72bl,0xb923db5de0342748l,0xf7d3aacd936d4a3dl,
        0x558519cc0b0b099el } },
    /* 36 << 147 */
    { { 0x3ea8ebf8827097efl,0x259353dbd054f55dl,0x84c89abc6d2ed089l,
        0x5c548b698e096a7cl },
      { 0xd587f616994b995dl,0x4d1531f6a5845601l,0x792ab31e451fd9f0l,
        0xc8b57bb265adf6cal } },
    /* 37 << 147 */
    { { 0x68440fcb1cd5ad73l,0xb9c860e66144da4fl,0x2ab286aa8462beb8l,
        0xcc6b8fffef46797fl },
      { 0xac820da420c8a471l,0x69ae05a177ff7fafl,0xb9163f39bfb5da77l,
        0xbd03e5902c73ab7al } },
    /* 38 << 147 */
    { { 0x7e862b5eb2940d9el,0x3c663d864b9af564l,0xd8309031bde3033dl,
        0x298231b2d42c5bc6l },
      { 0x42090d2c552ad093l,0xa4799d1cff854695l,0x0a88b5d6d31f0d00l,
        0xf8b40825a2f26b46l } },
    /* 39 << 147 */
    { { 0xec29b1edf1bd7218l,0xd491c53b4b24c86el,0xd2fe588f3395ea65l,
        0x6f3764f74456ef15l },
      { 0xdb43116dcdc34800l,0xcdbcd456c1e33955l,0xefdb554074ab286bl,
        0x948c7a51d18c5d7cl } },
    /* 40 << 147 */
    { { 0xeb81aa377378058el,0x41c746a104411154l,0xa10c73bcfb828ac7l,
        0x6439be919d972b29l },
      { 0x4bf3b4b043a2fbadl,0x39e6dadf82b5e840l,0x4f7164086397bd4cl,
        0x0f7de5687f1eeccbl } },
    /* 41 << 147 */
    { { 0x5865c5a1d2ffbfc1l,0xf74211fa4ccb6451l,0x66368a88c0b32558l,
        0x5b539dc29ad7812el },
      { 0x579483d02f3af6f6l,0x5213207899934ecel,0x50b9650fdcc9e983l,
        0xca989ec9aee42b8al } },
    /* 42 << 147 */
    { { 0x6a44c829d6f62f99l,0x8f06a3094c2a7c0cl,0x4ea2b3a098a0cb0al,
        0x5c547b70beee8364l },
      { 0x461d40e1682afe11l,0x9e0fc77a7b41c0a8l,0x79e4aefde20d5d36l,
        0x2916e52032dd9f63l } },
    /* 43 << 147 */
    { { 0xf59e52e83f883fafl,0x396f96392b868d35l,0xc902a9df4ca19881l,
        0x0fc96822db2401a6l },
      { 0x4123758766f1c68dl,0x10fc6de3fb476c0dl,0xf8b6b579841f5d90l,
        0x2ba8446cfa24f44al } },
    /* 44 << 147 */
    { { 0xa237b920ef4a9975l,0x60bb60042330435fl,0xd6f4ab5acfb7e7b5l,
        0xb2ac509783435391l },
      { 0xf036ee2fb0d1ea67l,0xae779a6a74c56230l,0x59bff8c8ab838ae6l,
        0xcd83ca999b38e6f0l } },
    /* 45 << 147 */
    { { 0xbb27bef5e33deed3l,0xe6356f6f001892a8l,0xbf3be6cc7adfbd3el,
        0xaecbc81c33d1ac9dl },
      { 0xe4feb909e6e861dcl,0x90a247a453f5f801l,0x01c50acb27346e57l,
        0xce29242e461acc1bl } },
    /* 46 << 147 */
    { { 0x04dd214a2f998a91l,0x271ee9b1d4baf27bl,0x7e3027d1e8c26722l,
        0x21d1645c1820dce5l },
      { 0x086f242c7501779cl,0xf0061407fa0e8009l,0xf23ce47760187129l,
        0x05bbdedb0fde9bd0l } },
    /* 47 << 147 */
    { { 0x682f483225d98473l,0xf207fe855c658427l,0xb6fdd7ba4166ffa1l,
        0x0c3140569eed799dl },
      { 0x0db8048f4107e28fl,0x74ed387141216840l,0x74489f8f56a3c06el,
        0x1e1c005b12777134l } },
    /* 48 << 147 */
    { { 0xdb332a73f37ec3c3l,0xc65259bddd59eba0l,0x2291709cdb4d3257l,
        0x9a793b25bd389390l },
      { 0xf39fe34be43756f0l,0x2f76bdce9afb56c9l,0x9f37867a61208b27l,
        0xea1d4307089972c3l } },
    /* 49 << 147 */
    { { 0x8c5953308bdf623al,0x5f5accda8441fb7dl,0xfafa941832ddfd95l,
        0x6ad40c5a0fde9be7l },
      { 0x43faba89aeca8709l,0xc64a7cf12c248a9dl,0x1662025272637a76l,
        0xaee1c79122b8d1bbl } },
    /* 50 << 147 */
    { { 0xf0f798fd21a843b2l,0x56e4ed4d8d005cb1l,0x355f77801f0d8abel,
        0x197b04cf34522326l },
      { 0x41f9b31ffd42c13fl,0x5ef7feb2b40f933dl,0x27326f425d60bad4l,
        0x027ecdb28c92cf89l } },
    /* 51 << 147 */
    { { 0x04aae4d14e3352fel,0x08414d2f73591b90l,0x5ed6124eb7da7d60l,
        0xb985b9314d13d4ecl },
      { 0xa592d3ab96bf36f9l,0x012dbed5bbdf51dfl,0xa57963c0df6c177dl,
        0x010ec86987ca29cfl } },
    /* 52 << 147 */
    { { 0xba1700f6bf926dffl,0x7c9fdbd1f4bf6bc2l,0xdc18dc8f64da11f5l,
        0xa6074b7ad938ae75l },
      { 0x14270066e84f44a4l,0x99998d38d27b954el,0xc1be8ab2b4f38e9al,
        0x8bb55bbf15c01016l } },
    /* 53 << 147 */
    { { 0xf73472b40ea2ab30l,0xd365a340f73d68ddl,0xc01a716819c2e1ebl,
        0x32f49e3734061719l },
      { 0xb73c57f101d8b4d6l,0x03c8423c26b47700l,0x321d0bc8a4d8826al,
        0x6004213c4bc0e638l } },
    /* 54 << 147 */
    { { 0xf78c64a1c1c06681l,0x16e0a16fef018e50l,0x31cbdf91db42b2b3l,
        0xf8f4ffcee0d36f58l },
      { 0xcdcc71cd4cc5e3e0l,0xd55c7cfaa129e3e0l,0xccdb6ba00fb2cbf1l,
        0x6aba0005c4bce3cbl } },
    /* 55 << 147 */
    { { 0x501cdb30d232cfc4l,0x9ddcf12ed58a3cefl,0x02d2cf9c87e09149l,
        0xdc5d7ec72c976257l },
      { 0x6447986e0b50d7ddl,0x88fdbaf7807f112al,0x58c9822ab00ae9f6l,
        0x6abfb9506d3d27e0l } },
    /* 56 << 147 */
    { { 0xd0a744878a429f4fl,0x0649712bdb516609l,0xb826ba57e769b5dfl,
        0x82335df21fc7aaf2l },
      { 0x2389f0675c93d995l,0x59ac367a68677be6l,0xa77985ff21d9951bl,
        0x038956fb85011ccel } },
    /* 57 << 147 */
    { { 0x608e48cbbb734e37l,0xc08c0bf22be5b26fl,0x17bbdd3bf9b1a0d9l,
        0xeac7d89810483319l },
      { 0xc95c4bafbc1a6deal,0xfdd0e2bf172aafdbl,0x40373cbc8235c41al,
        0x14303f21fb6f41d5l } },
    /* 58 << 147 */
    { { 0xba0636210408f237l,0xcad3b09aecd2d1edl,0x4667855a52abb6a2l,
        0xba9157dcaa8b417bl },
      { 0xfe7f35074f013efbl,0x1b112c4baa38c4a2l,0xa1406a609ba64345l,
        0xe53cba336993c80bl } },
    /* 59 << 147 */
    { { 0x45466063ded40d23l,0x3d5f1f4d54908e25l,0x9ebefe62403c3c31l,
        0x274ea0b50672a624l },
      { 0xff818d99451d1b71l,0x80e826438f79cf79l,0xa165df1373ce37f5l,
        0xa744ef4ffe3a21fdl } },
    /* 60 << 147 */
    { { 0x73f1e7f5cf551396l,0xc616898e868c676bl,0x671c28c78c442c36l,
        0xcfe5e5585e0a317dl },
      { 0x1242d8187051f476l,0x56fad2a614f03442l,0x262068bc0a44d0f6l,
        0xdfa2cd6ece6edf4el } },
    /* 61 << 147 */
    { { 0x0f43813ad15d1517l,0x61214cb2377d44f5l,0xd399aa29c639b35fl,
        0x42136d7154c51c19l },
      { 0x9774711b08417221l,0x0a5546b352545a57l,0x80624c411150582dl,
        0x9ec5c418fbc555bcl } },
    /* 62 << 147 */
    { { 0x2c87dcad771849f1l,0xb0c932c501d7bf6fl,0x6aa5cd3e89116eb2l,
        0xd378c25a51ca7bd3l },
      { 0xc612a0da9e6e3e31l,0x0417a54db68ad5d0l,0x00451e4a22c6edb8l,
        0x9fbfe019b42827cel } },
    /* 63 << 147 */
    { { 0x2fa92505ba9384a2l,0x21b8596e64ad69c1l,0x8f4fcc49983b35a6l,
        0xde09376072754672l },
      { 0x2f14ccc8f7bffe6dl,0x27566bff5d94263dl,0xb5b4e9c62df3ec30l,
        0x94f1d7d53e6ea6bal } },
    /* 64 << 147 */
    { { 0x97b7851aaaca5e9bl,0x518aa52156713b97l,0x3357e8c7150a61f6l,
        0x7842e7e2ec2c2b69l },
      { 0x8dffaf656868a548l,0xd963bd82e068fc81l,0x64da5c8b65917733l,
        0x927090ff7b247328l } },
    /* 0 << 154 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 154 */
    { { 0x214bc9a7d298c241l,0xe3b697ba56807cfdl,0xef1c78024564eadbl,
        0xdde8cdcfb48149c5l },
      { 0x946bf0a75a4d2604l,0x27154d7f6c1538afl,0x95cc9230de5b1fccl,
        0xd88519e966864f82l } },
    /* 2 << 154 */
    { { 0xb828dd1a7cb1282cl,0xa08d7626be46973al,0x6baf8d40e708d6b2l,
        0x72571fa14daeb3f3l },
      { 0x85b1732ff22dfd98l,0x87ab01a70087108dl,0xaaaafea85988207al,
        0xccc832f869f00755l } },
    /* 3 << 154 */
    { { 0x964d950e36ff3bf0l,0x8ad20f6ff0b34638l,0x4d9177b3b5d7585fl,
        0xcf839760ef3f019fl },
      { 0x582fc5b38288c545l,0x2f8e4e9b13116bd1l,0xf91e1b2f332120efl,
        0xcf5687242a17dd23l } },
    /* 4 << 154 */
    { { 0x488f1185ca8d9d1al,0xadf2c77dd987ded2l,0x5f3039f060c46124l,
        0xe5d70b7571e095f4l },
      { 0x82d586506260e70fl,0x39d75ea7f750d105l,0x8cf3d0b175bac364l,
        0xf3a7564d21d01329l } },
    /* 5 << 154 */
    { { 0x182f04cd2f52d2a7l,0x4fde149ae2df565al,0xb80c5eeca79fb2f7l,
        0xab491d7b22ddc897l },
      { 0x99d76c18c6312c7fl,0xca0d5f3d6aa41a57l,0x71207325d15363a0l,
        0xe82aa265beb252c2l } },
    /* 6 << 154 */
    { { 0x94ab4700ec3128c2l,0x6c76d8628e383f49l,0xdc36b150c03024ebl,
        0xfb43947753daac69l },
      { 0xfc68764a8dc79623l,0x5b86995db440fbb2l,0xd66879bfccc5ee0dl,
        0x0522894295aa8bd3l } },
    /* 7 << 154 */
    { { 0xb51a40a51e6a75c1l,0x24327c760ea7d817l,0x0663018207774597l,
        0xd6fdbec397fa7164l },
      { 0x20c99dfb13c90f48l,0xd6ac5273686ef263l,0xc6a50bdcfef64eebl,
        0xcd87b28186fdfc32l } },
    /* 8 << 154 */
    { { 0xb24aa43e3fcd3efcl,0xdd26c034b8088e9al,0xa5ef4dc9bd3d46eal,
        0xa2f99d588a4c6a6fl },
      { 0xddabd3552f1da46cl,0x72c3f8ce1afacdd1l,0xd90c4eee92d40578l,
        0xd28bb41fca623b94l } },
    /* 9 << 154 */
    { { 0x50fc0711745edc11l,0x9dd9ad7d3dc87558l,0xce6931fbb49d1e64l,
        0x6c77a0a2c98bd0f9l },
      { 0x62b9a6296baf7cb1l,0xcf065f91ccf72d22l,0x7203cce979639071l,
        0x09ae4885f9cb732fl } },
    /* 10 << 154 */
    { { 0x5e7c3becee8314f3l,0x1c068aeddbea298fl,0x08d381f17c80acecl,
        0x03b56be8e330495bl },
      { 0xaeffb8f29222882dl,0x95ff38f6c4af8bf7l,0x50e32d351fc57d8cl,
        0x6635be5217b444f0l } },
    /* 11 << 154 */
    { { 0x04d15276a5177900l,0x4e1dbb47f6858752l,0x5b475622c615796cl,
        0xa6fa0387691867bfl },
      { 0xed7f5d562844c6d0l,0xc633cf9b03a2477dl,0xf6be5c402d3721d6l,
        0xaf312eb7e9fd68e6l } },
    /* 12 << 154 */
    { { 0x242792d2e7417ce1l,0xff42bc71970ee7f5l,0x1ff4dc6d5c67a41el,
        0x77709b7b20882a58l },
      { 0x3554731dbe217f2cl,0x2af2a8cd5bb72177l,0x58eee769591dd059l,
        0xbb2930c94bba6477l } },
    /* 13 << 154 */
    { { 0x863ee0477d930cfcl,0x4c262ad1396fd1f4l,0xf4765bc8039af7e1l,
        0x2519834b5ba104f6l },
      { 0x7cd61b4cd105f961l,0xa5415da5d63bca54l,0x778280a088a1f17cl,
        0xc49689492329512cl } },
    /* 14 << 154 */
    { { 0x174a9126cecdaa7al,0xfc8c7e0e0b13247bl,0x29c110d23484c1c4l,
        0xf8eb8757831dfc3bl },
      { 0x022f0212c0067452l,0x3f6f69ee7b9b926cl,0x09032da0ef42daf4l,
        0x79f00ade83f80de4l } },
    /* 15 << 154 */
    { { 0x6210db7181236c97l,0x74f7685b3ee0781fl,0x4df7da7ba3e41372l,
        0x2aae38b1b1a1553el },
      { 0x1688e222f6dd9d1bl,0x576954485b8b6487l,0x478d21274b2edeaal,
        0xb2818fa51e85956al } },
    /* 16 << 154 */
    { { 0x1e6adddaf176f2c0l,0x01ca4604e2572658l,0x0a404ded85342ffbl,
        0x8cf60f96441838d6l },
      { 0x9bbc691cc9071c4al,0xfd58874434442803l,0x97101c85809c0d81l,
        0xa7fb754c8c456f7fl } },
    /* 17 << 154 */
    { { 0xc95f3c5cd51805e1l,0xab4ccd39b299dca8l,0x3e03d20b47eaf500l,
        0xfa3165c1d7b80893l },
      { 0x005e8b54e160e552l,0xdc4972ba9019d11fl,0x21a6972e0c9a4a7al,
        0xa52c258f37840fd7l } },
    /* 18 << 154 */
    { { 0xf8559ff4c1e99d81l,0x08e1a7d6a3c617c0l,0xb398fd43248c6ba7l,
        0x6ffedd91d1283794l },
      { 0x8a6a59d2d629d208l,0xa9d141d53490530el,0x42f6fc1838505989l,
        0x09bf250d479d94eel } },
    /* 19 << 154 */
    { { 0x223ad3b1b3822790l,0x6c5926c093b8971cl,0x609efc7e75f7fa62l,
        0x45d66a6d1ec2d989l },
      { 0x4422d663987d2792l,0x4a73caad3eb31d2bl,0xf06c2ac1a32cb9e6l,
        0xd9445c5f91aeba84l } },
    /* 20 << 154 */
    { { 0x6af7a1d5af71013fl,0xe68216e50bedc946l,0xf4cba30bd27370a0l,
        0x7981afbf870421ccl },
      { 0x02496a679449f0e1l,0x86cfc4be0a47edael,0x3073c936b1feca22l,
        0xf569461203f8f8fbl } },
    /* 21 << 154 */
    { { 0xd063b723901515eal,0x4c6c77a5749cf038l,0x6361e360ab9e5059l,
        0x596cf171a76a37c0l },
      { 0x800f53fa6530ae7al,0x0f5e631e0792a7a6l,0x5cc29c24efdb81c9l,
        0xa269e8683f9c40bal } },
    /* 22 << 154 */
    { { 0xec14f9e12cb7191el,0x78ea1bd8e5b08ea6l,0x3c65aa9b46332bb9l,
        0x84cc22b3bf80ce25l },
      { 0x0098e9e9d49d5bf1l,0xcd4ec1c619087da4l,0x3c9d07c5aef6e357l,
        0x839a02689f8f64b8l } },
    /* 23 << 154 */
    { { 0xc5e9eb62c6d8607fl,0x759689f56aa995e4l,0x70464669bbb48317l,
        0x921474bfe402417dl },
      { 0xcabe135b2a354c8cl,0xd51e52d2812fa4b5l,0xec74109653311fe8l,
        0x4f774535b864514bl } },
    /* 24 << 154 */
    { { 0xbcadd6715bde48f8l,0xc97038732189bc7dl,0x5d45299ec709ee8al,
        0xd1287ee2845aaff8l },
      { 0x7d1f8874db1dbf1fl,0xea46588b990c88d6l,0x60ba649a84368313l,
        0xd5fdcbce60d543ael } },
    /* 25 << 154 */
    { { 0x90b46d43810d5ab0l,0x6739d8f904d7e5ccl,0x021c1a580d337c33l,
        0x00a6116268e67c40l },
      { 0x95ef413b379f0a1fl,0xfe126605e9e2ab95l,0x67578b852f5f199cl,
        0xf5c003292cb84913l } },
    /* 26 << 154 */
    { { 0xf795643037577dd8l,0x83b82af429c5fe88l,0x9c1bea26cdbdc132l,
        0x589fa0869c04339el },
      { 0x033e9538b13799dfl,0x85fa8b21d295d034l,0xdf17f73fbd9ddccal,
        0xf32bd122ddb66334l } },
    /* 27 << 154 */
    { { 0x55ef88a7858b044cl,0x1f0d69c25aa9e397l,0x55fd9cc340d85559l,
        0xc774df727785ddb2l },
      { 0x5dcce9f6d3bd2e1cl,0xeb30da20a85dfed0l,0x5ed7f5bbd3ed09c4l,
        0x7d42a35c82a9c1bdl } },
    /* 28 << 154 */
    { { 0xcf3de9959890272dl,0x75f3432a3e713a10l,0x5e13479fe28227b8l,
        0xb8561ea9fefacdc8l },
      { 0xa6a297a08332aafdl,0x9b0d8bb573809b62l,0xd2fa1cfd0c63036fl,
        0x7a16eb55bd64bda8l } },
    /* 29 << 154 */
    { { 0x3f5cf5f678e62ddcl,0x2267c45407fd752bl,0x5e361b6b5e437bbel,
        0x95c595018354e075l },
      { 0xec725f85f2b254d9l,0x844b617d2cb52b4el,0xed8554f5cf425fb5l,
        0xab67703e2af9f312l } },
    /* 30 << 154 */
    { { 0x4cc34ec13cf48283l,0xb09daa259c8a705el,0xd1e9d0d05b7d4f84l,
        0x4df6ef64db38929dl },
      { 0xe16b0763aa21ba46l,0xc6b1d178a293f8fbl,0x0ff5b602d520aabfl,
        0x94d671bdc339397al } },
    /* 31 << 154 */
    { { 0x7c7d98cf4f5792fal,0x7c5e0d6711215261l,0x9b19a631a7c5a6d4l,
        0xc8511a627a45274dl },
      { 0x0c16621ca5a60d99l,0xf7fbab88cf5e48cbl,0xab1e6ca2f7ddee08l,
        0x83bd08cee7867f3cl } },
    /* 32 << 154 */
    { { 0xf7e48e8a2ac13e27l,0x4494f6df4eb1a9f5l,0xedbf84eb981f0a62l,
        0x49badc32536438f0l },
      { 0x50bea541004f7571l,0xbac67d10df1c94eel,0x253d73a1b727bc31l,
        0xb3d01cf230686e28l } },
    /* 33 << 154 */
    { { 0x51b77b1b55fd0b8bl,0xa099d183feec3173l,0x202b1fb7670e72b7l,
        0xadc88b33a8e1635fl },
      { 0x34e8216af989d905l,0xc2e68d2029b58d01l,0x11f81c926fe55a93l,
        0x15f1462a8f296f40l } },
    /* 34 << 154 */
    { { 0x1915d375ea3d62f2l,0xa17765a301c8977dl,0x7559710ae47b26f6l,
        0xe0bd29c8535077a5l },
      { 0x615f976d08d84858l,0x370dfe8569ced5c1l,0xbbc7503ca734fa56l,
        0xfbb9f1ec91ac4574l } },
    /* 35 << 154 */
    { { 0x95d7ec53060dd7efl,0xeef2dacd6e657979l,0x54511af3e2a08235l,
        0x1e324aa41f4aea3dl },
      { 0x550e7e71e6e67671l,0xbccd5190bf52faf7l,0xf880d316223cc62al,
        0x0d402c7e2b32eb5dl } },
    /* 36 << 154 */
    { { 0xa40bc039306a5a3bl,0x4e0a41fd96783a1bl,0xa1e8d39a0253cdd4l,
        0x6480be26c7388638l },
      { 0xee365e1d2285f382l,0x188d8d8fec0b5c36l,0x34ef1a481f0f4d82l,
        0x1a8f43e1a487d29al } },
    /* 37 << 154 */
    { { 0x8168226d77aefb3al,0xf69a751e1e72c253l,0x8e04359ae9594df1l,
        0x475ffd7dd14c0467l },
      { 0xb5a2c2b13844e95cl,0x85caf647dd12ef94l,0x1ecd2a9ff1063d00l,
        0x1dd2e22923843311l } },
    /* 38 << 154 */
    { { 0x38f0e09d73d17244l,0x3ede77468fc653f1l,0xae4459f5dc20e21cl,
        0x00db2ffa6a8599eal },
      { 0x11682c3930cfd905l,0x4934d074a5c112a6l,0xbdf063c5568bfe95l,
        0x779a440a016c441al } },
    /* 39 << 154 */
    { { 0x0c23f21897d6fbdcl,0xd3a5cd87e0776aacl,0xcee37f72d712e8dbl,
        0xfb28c70d26f74e8dl },
      { 0xffe0c728b61301a0l,0xa6282168d3724354l,0x7ff4cb00768ffedcl,
        0xc51b308803b02de9l } },
    /* 40 << 154 */
    { { 0xa5a8147c3902dda5l,0x35d2f706fe6973b4l,0x5ac2efcfc257457el,
        0x933f48d48700611bl },
      { 0xc365af884912beb2l,0x7f5a4de6162edf94l,0xc646ba7c0c32f34bl,
        0x632c6af3b2091074l } },
    /* 41 << 154 */
    { { 0x58d4f2e3753e43a9l,0x70e1d21724d4e23fl,0xb24bf729afede6a6l,
        0x7f4a94d8710c8b60l },
      { 0xaad90a968d4faa6al,0xd9ed0b32b066b690l,0x52fcd37b78b6dbfdl,
        0x0b64615e8bd2b431l } },
    /* 42 << 154 */
    { { 0x228e2048cfb9fad5l,0xbeaa386d240b76bdl,0x2d6681c890dad7bcl,
        0x3e553fc306d38f5el },
      { 0xf27cdb9b9d5f9750l,0x3e85c52ad28c5b0el,0x190795af5247c39bl,
        0x547831ebbddd6828l } },
    /* 43 << 154 */
    { { 0xf327a2274a82f424l,0x36919c787e47f89dl,0xe478391943c7392cl,
        0xf101b9aa2316fefel },
      { 0xbcdc9e9c1c5009d2l,0xfb55ea139cd18345l,0xf5b5e231a3ce77c7l,
        0xde6b4527d2f2cb3dl } },
    /* 44 << 154 */
    { { 0x10f6a3339bb26f5fl,0x1e85db8e044d85b6l,0xc3697a0894197e54l,
        0x65e18cc0a7cb4ea8l },
      { 0xa38c4f50a471fe6el,0xf031747a2f13439cl,0x53c4a6bac007318bl,
        0xa8da3ee51deccb3dl } },
    /* 45 << 154 */
    { { 0x0555b31c558216b1l,0x90c7810c2f79e6c2l,0x9b669f4dfe8eed3cl,
        0x70398ec8e0fac126l },
      { 0xa96a449ef701b235l,0x0ceecdb3eb94f395l,0x285fc368d0cb7431l,
        0x0d37bb5216a18c64l } },
    /* 46 << 154 */
    { { 0x05110d38b880d2ddl,0xa60f177b65930d57l,0x7da34a67f36235f5l,
        0x47f5e17c183816b9l },
      { 0xc7664b57db394af4l,0x39ba215d7036f789l,0x46d2ca0e2f27b472l,
        0xc42647eef73a84b7l } },
    /* 47 << 154 */
    { { 0x44bc754564488f1dl,0xaa922708f4cf85d5l,0x721a01d553e4df63l,
        0x649c0c515db46cedl },
      { 0x6bf0d64e3cffcb6cl,0xe3bf93fe50f71d96l,0x75044558bcc194a0l,
        0x16ae33726afdc554l } },
    /* 48 << 154 */
    { { 0xbfc01adf5ca48f3fl,0x64352f06e22a9b84l,0xcee54da1c1099e4al,
        0xbbda54e8fa1b89c0l },
      { 0x166a3df56f6e55fbl,0x1ca44a2420176f88l,0x936afd88dfb7b5ffl,
        0xe34c24378611d4a0l } },
    /* 49 << 154 */
    { { 0x7effbb7586142103l,0x6704ba1b1f34fc4dl,0x7c2a468f10c1b122l,
        0x36b3a6108c6aace9l },
      { 0xabfcc0a775a0d050l,0x066f91973ce33e32l,0xce905ef429fe09bel,
        0x89ee25baa8376351l } },
    /* 50 << 154 */
    { { 0x2a3ede22fd29dc76l,0x7fd32ed936f17260l,0x0cadcf68284b4126l,
        0x63422f08a7951fc8l },
      { 0x562b24f40807e199l,0xfe9ce5d122ad4490l,0xc2f51b100db2b1b4l,
        0xeb3613ffe4541d0dl } },
    /* 51 << 154 */
    { { 0xbd2c4a052680813bl,0x527aa55d561b08d6l,0xa9f8a40ea7205558l,
        0xe3eea56f243d0becl },
      { 0x7b853817a0ff58b3l,0xb67d3f651a69e627l,0x0b76bbb9a869b5d6l,
        0xa3afeb82546723edl } },
    /* 52 << 154 */
    { { 0x5f24416d3e554892l,0x8413b53d430e2a45l,0x99c56aee9032a2a0l,
        0x09432bf6eec367b1l },
      { 0x552850c6daf0ecc1l,0x49ebce555bc92048l,0xdfb66ba654811307l,
        0x1b84f7976f298597l } },
    /* 53 << 154 */
    { { 0x795904818d1d7a0dl,0xd9fabe033a6fa556l,0xa40f9c59ba9e5d35l,
        0xcb1771c1f6247577l },
      { 0x542a47cae9a6312bl,0xa34b3560552dd8c5l,0xfdf94de00d794716l,
        0xd46124a99c623094l } },
    /* 54 << 154 */
    { { 0x56b7435d68afe8b4l,0x27f205406c0d8ea1l,0x12b77e1473186898l,
        0xdbc3dd467479490fl },
      { 0x951a9842c03b0c05l,0x8b1b3bb37921bc96l,0xa573b3462b202e0al,
        0x77e4665d47254d56l } },
    /* 55 << 154 */
    { { 0x08b70dfcd23e3984l,0xab86e8bcebd14236l,0xaa3e07f857114ba7l,
        0x5ac71689ab0ef4f2l },
      { 0x88fca3840139d9afl,0x72733f8876644af0l,0xf122f72a65d74f4al,
        0x13931577a5626c7al } },
    /* 56 << 154 */
    { { 0xd5b5d9eb70f8d5a4l,0x375adde7d7bbb228l,0x31e88b860c1c0b32l,
        0xd1f568c4173edbaal },
      { 0x1592fc835459df02l,0x2beac0fb0fcd9a7el,0xb0a6fdb81b473b0al,
        0xe3224c6f0fe8fc48l } },
    /* 57 << 154 */
    { { 0x680bd00ee87edf5bl,0x30385f0220e77cf5l,0xe9ab98c04d42d1b2l,
        0x72d191d2d3816d77l },
      { 0x1564daca0917d9e5l,0x394eab591f8fed7fl,0xa209aa8d7fbb3896l,
        0x5564f3b9be6ac98el } },
    /* 58 << 154 */
    { { 0xead21d05d73654efl,0x68d1a9c413d78d74l,0x61e017086d4973a0l,
        0x83da350046e6d32al },
      { 0x6a3dfca468ae0118l,0xa1b9a4c9d02da069l,0x0b2ff9c7ebab8302l,
        0x98af07c3944ba436l } },
    /* 59 << 154 */
    { { 0x85997326995f0f9fl,0x467fade071b58bc6l,0x47e4495abd625a2bl,
        0xfdd2d01d33c3b8cdl },
      { 0x2c38ae28c693f9fal,0x48622329348f7999l,0x97bf738e2161f583l,
        0x15ee2fa7565e8cc9l } },
    /* 60 << 154 */
    { { 0xa1a5c8455777e189l,0xcc10bee0456f2829l,0x8ad95c56da762bd5l,
        0x152e2214e9d91da8l },
      { 0x975b0e727cb23c74l,0xfd5d7670a90c66dfl,0xb5b5b8ad225ffc53l,
        0xab6dff73faded2ael } },
    /* 61 << 154 */
    { { 0xebd567816f4cbe9dl,0x0ed8b2496a574bd7l,0x41c246fe81a881fal,
        0x91564805c3db9c70l },
      { 0xd7c12b085b862809l,0x1facd1f155858d7bl,0x7693747caf09e92al,
        0x3b69dcba189a425fl } },
    /* 62 << 154 */
    { { 0x0be28e9f967365efl,0x57300eb2e801f5c9l,0x93b8ac6ad583352fl,
        0xa2cf1f89cd05b2b7l },
      { 0x7c0c9b744dcc40ccl,0xfee38c45ada523fbl,0xb49a4dec1099cc4dl,
        0x325c377f69f069c6l } },
    /* 63 << 154 */
    { { 0xe12458ce476cc9ffl,0x580e0b6cc6d4cb63l,0xd561c8b79072289bl,
        0x0377f264a619e6dal },
      { 0x2668536288e591a5l,0xa453a7bd7523ca2bl,0x8a9536d2c1df4533l,
        0xc8e50f2fbe972f79l } },
    /* 64 << 154 */
    { { 0xd433e50f6d3549cfl,0x6f33696ffacd665el,0x695bfdacce11fcb4l,
        0x810ee252af7c9860l },
      { 0x65450fe17159bb2cl,0xf7dfbebe758b357bl,0x2b057e74d69fea72l,
        0xd485717a92731745l } },
    /* 0 << 161 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 161 */
    { { 0x896c42e8ee36860cl,0xdaf04dfd4113c22dl,0x1adbb7b744104213l,
        0xe5fd5fa11fd394eal },
      { 0x68235d941a4e0551l,0x6772cfbe18d10151l,0x276071e309984523l,
        0xe4e879de5a56ba98l } },
    /* 2 << 161 */
    { { 0xaaafafb0285b9491l,0x01a0be881e4c705el,0xff1d4f5d2ad9caabl,
        0x6e349a4ac37a233fl },
      { 0xcf1c12464a1c6a16l,0xd99e6b6629383260l,0xea3d43665f6d5471l,
        0x36974d04ff8cc89bl } },
    /* 3 << 161 */
    { { 0xc26c49a1cfe89d80l,0xb42c026dda9c8371l,0xca6c013adad066d2l,
        0xfb8f722856a4f3eel },
      { 0x08b579ecd850935bl,0x34c1a74cd631e1b3l,0xcb5fe596ac198534l,
        0x39ff21f6e1f24f25l } },
    /* 4 << 161 */
    { { 0x27f29e148f929057l,0x7a64ae06c0c853dfl,0x256cd18358e9c5cel,
        0x9d9cce82ded092a5l },
      { 0xcc6e59796e93b7c7l,0xe1e4709231bb9e27l,0xb70b3083aa9e29a0l,
        0xbf181a753785e644l } },
    /* 5 << 161 */
    { { 0xf53f2c658ead09f7l,0x1335e1d59780d14dl,0x69cc20e0cd1b66bcl,
        0x9b670a37bbe0bfc8l },
      { 0xce53dc8128efbeedl,0x0c74e77c8326a6e5l,0x3604e0d2b88e9a63l,
        0xbab38fca13dc2248l } },
    /* 6 << 161 */
    { { 0x8ed6e8c85c0a3f1el,0xbcad24927c87c37fl,0xfdfb62bb9ee3b78dl,
        0xeba8e477cbceba46l },
      { 0x37d38cb0eeaede4bl,0x0bc498e87976deb6l,0xb2944c046b6147fbl,
        0x8b123f35f71f9609l } },
    /* 7 << 161 */
    { { 0xa155dcc7de79dc24l,0xf1168a32558f69cdl,0xbac215950d1850dfl,
        0x15c8295bb204c848l },
      { 0xf661aa367d8184ffl,0xc396228e30447bdbl,0x11cd5143bde4a59el,
        0xe3a26e3b6beab5e6l } },
    /* 8 << 161 */
    { { 0xd3b3a13f1402b9d0l,0x573441c32c7bc863l,0x4b301ec4578c3e6el,
        0xc26fc9c40adaf57el },
      { 0x96e71bfd7493cea3l,0xd05d4b3f1af81456l,0xdaca2a8a6a8c608fl,
        0x53ef07f60725b276l } },
    /* 9 << 161 */
    { { 0x07a5fbd27824fc56l,0x3467521813289077l,0x5bf69fd5e0c48349l,
        0xa613ddd3b6aa7875l },
      { 0x7f78c19c5450d866l,0x46f4409c8f84a481l,0x9f1d192890fce239l,
        0x016c4168b2ce44b9l } },
    /* 10 << 161 */
    { { 0xbae023f0c7435978l,0xb152c88820e30e19l,0x9c241645e3fa6fafl,
        0x735d95c184823e60l },
      { 0x0319757303955317l,0x0b4b02a9f03b4995l,0x076bf55970274600l,
        0x32c5cc53aaf57508l } },
    /* 11 << 161 */
    { { 0xe8af6d1f60624129l,0xb7bc5d649a5e2b5el,0x3814b0485f082d72l,
        0x76f267f2ce19677al },
      { 0x626c630fb36eed93l,0x55230cd73bf56803l,0x78837949ce2736a0l,
        0x0d792d60aa6c55f1l } },
    /* 12 << 161 */
    { { 0x0318dbfdd5c7c5d2l,0xb38f8da7072b342dl,0x3569bddc7b8de38al,
        0xf25b5887a1c94842l },
      { 0xb2d5b2842946ad60l,0x854f29ade9d1707el,0xaa5159dc2c6a4509l,
        0x899f94c057189837l } },
    /* 13 << 161 */
    { { 0xcf6adc51f4a55b03l,0x261762de35e3b2d5l,0x4cc4301204827b51l,
        0xcd22a113c6021442l },
      { 0xce2fd61a247c9569l,0x59a50973d152becal,0x6c835a1163a716d4l,
        0xc26455ed187dedcfl } },
    /* 14 << 161 */
    { { 0x27f536e049ce89e7l,0x18908539cc890cb5l,0x308909abd83c2aa1l,
        0xecd3142b1ab73bd3l },
      { 0x6a85bf59b3f5ab84l,0x3c320a68f2bea4c6l,0xad8dc5386da4541fl,
        0xeaf34eb0b7c41186l } },
    /* 15 << 161 */
    { { 0x1c780129977c97c4l,0x5ff9beebc57eb9fal,0xa24d0524c822c478l,
        0xfd8eec2a461cd415l },
      { 0xfbde194ef027458cl,0xb4ff53191d1be115l,0x63f874d94866d6f4l,
        0x35c75015b21ad0c9l } },
    /* 16 << 161 */
    { { 0xa6b5c9d646ac49d2l,0x42c77c0b83137aa9l,0x24d000fc68225a38l,
        0x0f63cfc82fe1e907l },
      { 0x22d1b01bc6441f95l,0x7d38f719ec8e448fl,0x9b33fa5f787fb1bal,
        0x94dcfda1190158dfl } },
    /* 17 << 161 */
    { { 0xc47cb3395f6d4a09l,0x6b4f355cee52b826l,0x3d100f5df51b930al,
        0xf4512fac9f668f69l },
      { 0x546781d5206c4c74l,0xd021d4d4cb4d2e48l,0x494a54c2ca085c2dl,
        0xf1dbaca4520850a8l } },
    /* 18 << 161 */
    { { 0x63c79326490a1acal,0xcb64dd9c41526b02l,0xbb772591a2979258l,
        0x3f58297048d97846l },
      { 0xd66b70d17c213ba7l,0xc28febb5e8a0ced4l,0x6b911831c10338c1l,
        0x0d54e389bf0126f3l } },
    /* 19 << 161 */
    { { 0x7048d4604af206eel,0x786c88f677e97cb9l,0xd4375ae1ac64802el,
        0x469bcfe1d53ec11cl },
      { 0xfc9b340d47062230l,0xe743bb57c5b4a3acl,0xfe00b4aa59ef45acl,
        0x29a4ef2359edf188l } },
    /* 20 << 161 */
    { { 0x40242efeb483689bl,0x2575d3f6513ac262l,0xf30037c80ca6db72l,
        0xc9fcce8298864be2l },
      { 0x84a112ff0149362dl,0x95e575821c4ae971l,0x1fa4b1a8945cf86cl,
        0x4525a7340b024a2fl } },
    /* 21 << 161 */
    { { 0xe76c8b628f338360l,0x483ff59328edf32bl,0x67e8e90a298b1aecl,
        0x9caab338736d9a21l },
      { 0x5c09d2fd66892709l,0x2496b4dcb55a1d41l,0x93f5fb1ae24a4394l,
        0x08c750496fa8f6c1l } },
    /* 22 << 161 */
    { { 0xcaead1c2c905d85fl,0xe9d7f7900733ae57l,0x24c9a65cf07cdd94l,
        0x7389359ca4b55931l },
      { 0xf58709b7367e45f7l,0x1f203067cb7e7adcl,0x82444bffc7b72818l,
        0x07303b35baac8033l } },
    /* 23 << 161 */
    { { 0x1e1ee4e4d13b7ea1l,0xe6489b24e0e74180l,0xa5f2c6107e70ef70l,
        0xa1655412bdd10894l },
      { 0x555ebefb7af4194el,0x533c1c3c8e89bd9cl,0x735b9b5789895856l,
        0x15fb3cd2567f5c15l } },
    /* 24 << 161 */
    { { 0x057fed45526f09fdl,0xe8a4f10c8128240al,0x9332efc4ff2bfd8dl,
        0x214e77a0bd35aa31l },
      { 0x32896d7314faa40el,0x767867ec01e5f186l,0xc9adf8f117a1813el,
        0xcb6cda7854741795l } },
    /* 25 << 161 */
    { { 0xb7521b6d349d51aal,0xf56b5a9ee3c7b8e9l,0xc6f1e5c932a096dfl,
        0x083667c4a3635024l },
      { 0x365ea13518087f2fl,0xf1b8eaacd136e45dl,0xc8a0e48473aec989l,
        0xd75a324b142c9259l } },
    /* 26 << 161 */
    { { 0xb7b4d00101dae185l,0x45434e0b9b7a94bcl,0xf54339affbd8cb0bl,
        0xdcc4569ee98ef49el },
      { 0x7789318a09a51299l,0x81b4d206b2b025d8l,0xf64aa418fae85792l,
        0x3e50258facd7baf7l } },
    /* 27 << 161 */
    { { 0xdce84cdb2996864bl,0xa2e670891f485fa4l,0xb28b2bb6534c6a5al,
        0x31a7ec6bc94b9d39l },
      { 0x1d217766d6bc20dal,0x4acdb5ec86761190l,0x6872632873701063l,
        0x4d24ee7c2128c29bl } },
    /* 28 << 161 */
    { { 0xc072ebd3a19fd868l,0x612e481cdb8ddd3bl,0xb4e1d7541a64d852l,
        0x00ef95acc4c6c4abl },
      { 0x1536d2edaa0a6c46l,0x6129408643774790l,0x54af25e8343fda10l,
        0x9ff9d98dfd25d6f2l } },
    /* 29 << 161 */
    { { 0x0746af7c468b8835l,0x977a31cb730ecea7l,0xa5096b80c2cf4a81l,
        0xaa9868336458c37al },
      { 0x6af29bf3a6bd9d34l,0x6a62fe9b33c5d854l,0x50e6c304b7133b5el,
        0x04b601597d6e6848l } },
    /* 30 << 161 */
    { { 0x4cd296df5579bea4l,0x10e35ac85ceedaf1l,0x04c4c5fde3bcc5b1l,
        0x95f9ee8a89412cf9l },
      { 0x2c9459ee82b6eb0fl,0x2e84576595c2aaddl,0x774a84aed327fcfel,
        0xd8c937220368d476l } },
    /* 31 << 161 */
    { { 0x0dbd5748f83e8a3bl,0xa579aa968d2495f3l,0x535996a0ae496e9bl,
        0x07afbfe9b7f9bcc2l },
      { 0x3ac1dc6d5b7bd293l,0x3b592cff7022323dl,0xba0deb989c0a3e76l,
        0x18e78e9f4b197acbl } },
    /* 32 << 161 */
    { { 0x211cde10296c36efl,0x7ee8967282c4da77l,0xb617d270a57836dal,
        0xf0cd9c319cb7560bl },
      { 0x01fdcbf7e455fe90l,0x3fb53cbb7e7334f3l,0x781e2ea44e7de4ecl,
        0x8adab3ad0b384fd0l } },
    /* 33 << 161 */
    { { 0x129eee2f53d64829l,0x7a471e17a261492bl,0xe4f9adb9e4cb4a2cl,
        0x3d359f6f97ba2c2dl },
      { 0x346c67860aacd697l,0x92b444c375c2f8a8l,0xc79fa117d85df44el,
        0x56782372398ddf31l } },
    /* 34 << 161 */
    { { 0x60e690f2bbbab3b8l,0x4851f8ae8b04816bl,0xc72046ab9c92e4d2l,
        0x518c74a17cf3136bl },
      { 0xff4eb50af9877d4cl,0x14578d90a919cabbl,0x8218f8c4ac5eb2b6l,
        0xa3ccc547542016e4l } },
    /* 35 << 161 */
    { { 0x025bf48e327f8349l,0xf3e97346f43cb641l,0xdc2bafdf500f1085l,
        0x571678762f063055l },
      { 0x5bd914b9411925a6l,0x7c078d48a1123de5l,0xee6bf835182b165dl,
        0xb11b5e5bba519727l } },
    /* 36 << 161 */
    { { 0xe33ea76c1eea7b85l,0x2352b46192d4f85el,0xf101d334afe115bbl,
        0xfabc1294889175a3l },
      { 0x7f6bcdc05233f925l,0xe0a802dbe77fec55l,0xbdb47b758069b659l,
        0x1c5e12def98fbd74l } },
    /* 37 << 161 */
    { { 0x869c58c64b8457eel,0xa5360f694f7ea9f7l,0xe576c09ff460b38fl,
        0x6b70d54822b7fb36l },
      { 0x3fd237f13bfae315l,0x33797852cbdff369l,0x97df25f525b516f9l,
        0x46f388f2ba38ad2dl } },
    /* 38 << 161 */
    { { 0x656c465889d8ddbbl,0x8830b26e70f38ee8l,0x4320fd5cde1212b0l,
        0xc34f30cfe4a2edb2l },
      { 0xabb131a356ab64b8l,0x7f77f0ccd99c5d26l,0x66856a37bf981d94l,
        0x19e76d09738bd76el } },
    /* 39 << 161 */
    { { 0xe76c8ac396238f39l,0xc0a482bea830b366l,0xb7b8eaff0b4eb499l,
        0x8ecd83bc4bfb4865l },
      { 0x971b2cb7a2f3776fl,0xb42176a4f4b88adfl,0xb9617df5be1fa446l,
        0x8b32d508cd031bd2l } },
    /* 40 << 161 */
    { { 0x1c6bd47d53b618c0l,0xc424f46c6a227923l,0x7303ffdedd92d964l,
        0xe971287871b5abf2l },
      { 0x8f48a632f815561dl,0x85f48ff5d3c055d1l,0x222a14277525684fl,
        0xd0d841a067360cc3l } },
    /* 41 << 161 */
    { { 0x4245a9260b9267c6l,0xc78913f1cf07f863l,0xaa844c8e4d0d9e24l,
        0xa42ad5223d5f9017l },
      { 0xbd371749a2c989d5l,0x928292dfe1f5e78el,0x493b383e0a1ea6dal,
        0x5136fd8d13aee529l } },
    /* 42 << 161 */
    { { 0x860c44b1f2c34a99l,0x3b00aca4bf5855acl,0xabf6aaa0faaf37bel,
        0x65f436822a53ec08l },
      { 0x1d9a5801a11b12e1l,0x78a7ab2ce20ed475l,0x0de1067e9a41e0d5l,
        0x30473f5f305023eal } },
    /* 43 << 161 */
    { { 0xdd3ae09d169c7d97l,0x5cd5baa4cfaef9cdl,0x5cd7440b65a44803l,
        0xdc13966a47f364del },
      { 0x077b2be82b8357c1l,0x0cb1b4c5e9d57c2al,0x7a4ceb3205ff363el,
        0xf310fa4dca35a9efl } },
    /* 44 << 161 */
    { { 0xdbb7b352f97f68c6l,0x0c773b500b02cf58l,0xea2e48213c1f96d9l,
        0xffb357b0eee01815l },
      { 0xb9c924cde0f28039l,0x0b36c95a46a3fbe4l,0x1faaaea45e46db6cl,
        0xcae575c31928aaffl } },
    /* 45 << 161 */
    { { 0x7f671302a70dab86l,0xfcbd12a971c58cfcl,0xcbef9acfbee0cb92l,
        0x573da0b9f8c1b583l },
      { 0x4752fcfe0d41d550l,0xe7eec0e32155cffel,0x0fc39fcb545ae248l,
        0x522cb8d18065f44el } },
    /* 46 << 161 */
    { { 0x263c962a70cbb96cl,0xe034362abcd124a9l,0xf120db283c2ae58dl,
        0xb9a38d49fef6d507l },
      { 0xb1fd2a821ff140fdl,0xbd162f3020aee7e0l,0x4e17a5d4cb251949l,
        0x2aebcb834f7e1c3dl } },
    /* 47 << 161 */
    { { 0x608eb25f937b0527l,0xf42e1e47eb7d9997l,0xeba699c4b8a53a29l,
        0x1f921c71e091b536l },
      { 0xcce29e7b5b26bbd5l,0x7a8ef5ed3b61a680l,0xe5ef8043ba1f1c7el,
        0x16ea821718158ddal } },
    /* 48 << 161 */
    { { 0x01778a2b599ff0f9l,0x68a923d78104fc6bl,0x5bfa44dfda694ff3l,
        0x4f7199dbf7667f12l },
      { 0xc06d8ff6e46f2a79l,0x08b5deade9f8131dl,0x02519a59abb4ce7cl,
        0xc4f710bcb42aec3el } },
    /* 49 << 161 */
    { { 0x3d77b05778bde41al,0x6474bf80b4186b5al,0x048b3f6788c65741l,
        0xc64519de03c7c154l },
      { 0xdf0738460edfcc4fl,0x319aa73748f1aa6bl,0x8b9f8a02ca909f77l,
        0x902581397580bfefl } },
    /* 50 << 161 */
    { { 0xd8bfd3cac0c22719l,0xc60209e4c9ca151el,0x7a744ab5d9a1a69cl,
        0x6de5048b14937f8fl },
      { 0x171938d8e115ac04l,0x7df709401c6b16d2l,0xa6aeb6637f8e94e7l,
        0xc130388e2a2cf094l } },
    /* 51 << 161 */
    { { 0x1850be8477f54e6el,0x9f258a7265d60fe5l,0xff7ff0c06c9146d6l,
        0x039aaf90e63a830bl },
      { 0x38f27a739460342fl,0x4703148c3f795f8al,0x1bb5467b9681a97el,
        0x00931ba5ecaeb594l } },
    /* 52 << 161 */
    { { 0xcdb6719d786f337cl,0xd9c01cd2e704397dl,0x0f4a3f20555c2fefl,
        0x004525097c0af223l },
      { 0x54a5804784db8e76l,0x3bacf1aa93c8aa06l,0x11ca957cf7919422l,
        0x5064105378cdaa40l } },
    /* 53 << 161 */
    { { 0x7a3038749f7144ael,0x170c963f43d4acfdl,0x5e14814958ddd3efl,
        0xa7bde5829e72dba8l },
      { 0x0769da8b6fa68750l,0xfa64e532572e0249l,0xfcaadf9d2619ad31l,
        0x87882daaa7b349cdl } },
    /* 54 << 161 */
    { { 0x9f6eb7316c67a775l,0xcb10471aefc5d0b1l,0xb433750ce1b806b2l,
        0x19c5714d57b1ae7el },
      { 0xc0dc8b7bed03fd3fl,0xdd03344f31bc194el,0xa66c52a78c6320b5l,
        0x8bc82ce3d0b6fd93l } },
    /* 55 << 161 */
    { { 0xf8e13501b35f1341l,0xe53156dd25a43e42l,0xd3adf27e4daeb85cl,
        0xb81d8379bbeddeb5l },
      { 0x1b0b546e2e435867l,0x9020eb94eba5dd60l,0x37d911618210cb9dl,
        0x4c596b315c91f1cfl } },
    /* 56 << 161 */
    { { 0xb228a90f0e0b040dl,0xbaf02d8245ff897fl,0x2aac79e600fa6122l,
        0x248288178e36f557l },
      { 0xb9521d31113ec356l,0x9e48861e15eff1f8l,0x2aa1d412e0d41715l,
        0x71f8620353f131b8l } },
    /* 57 << 161 */
    { { 0xf60da8da3fd19408l,0x4aa716dc278d9d99l,0x394531f7a8c51c90l,
        0xb560b0e8f59db51cl },
      { 0xa28fc992fa34bdadl,0xf024fa149cd4f8bdl,0x5cf530f723a9d0d3l,
        0x615ca193e28c9b56l } },
    /* 58 << 161 */
    { { 0x6d2a483d6f73c51el,0xa4cb2412ea0dc2ddl,0x50663c411eb917ffl,
        0x3d3a74cfeade299el },
      { 0x29b3990f4a7a9202l,0xa9bccf59a7b15c3dl,0x66a3ccdca5df9208l,
        0x48027c1443f2f929l } },
    /* 59 << 161 */
    { { 0xd385377c40b557f0l,0xe001c366cd684660l,0x1b18ed6be2183a27l,
        0x879738d863210329l },
      { 0xa687c74bbda94882l,0xd1bbcc48a684b299l,0xaf6f1112863b3724l,
        0x6943d1b42c8ce9f8l } },
    /* 60 << 161 */
    { { 0xe044a3bb098cafb4l,0x27ed231060d48cafl,0x542b56753a31b84dl,
        0xcbf3dd50fcddbed7l },
      { 0x25031f1641b1d830l,0xa7ec851dcb0c1e27l,0xac1c8fe0b5ae75dbl,
        0xb24c755708c52120l } },
    /* 61 << 161 */
    { { 0x57f811dc1d4636c3l,0xf8436526681a9939l,0x1f6bc6d99c81adb3l,
        0x840f8ac35b7d80d4l },
      { 0x731a9811f4387f1al,0x7c501cd3b5156880l,0xa5ca4a07dfe68867l,
        0xf123d8f05fcea120l } },
    /* 62 << 161 */
    { { 0x1fbb0e71d607039el,0x2b70e215cd3a4546l,0x32d2f01d53324091l,
        0xb796ff08180ab19bl },
      { 0x32d87a863c57c4aal,0x2aed9cafb7c49a27l,0x9fb35eac31630d98l,
        0x338e8cdf5c3e20a3l } },
    /* 63 << 161 */
    { { 0x80f1618266cde8dbl,0x4e1599802d72fd36l,0xd7b8f13b9b6e5072l,
        0xf52139073b7b5dc1l },
      { 0x4d431f1d8ce4396el,0x37a1a680a7ed2142l,0xbf375696d01aaf6bl,
        0xaa1c0c54e63aab66l } },
    /* 64 << 161 */
    { { 0x3014368b4ed80940l,0x67e6d0567a6fceddl,0x7c208c49ca97579fl,
        0xfe3d7a81a23597f6l },
      { 0x5e2032027e096ae2l,0xb1f3e1e724b39366l,0x26da26f32fdcdffcl,
        0x79422f1d6097be83l } },
    /* 0 << 168 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 168 */
    { { 0x263a2cfb9db3b381l,0x9c3a2deed4df0a4bl,0x728d06e97d04e61fl,
        0x8b1adfbc42449325l },
      { 0x6ec1d9397e053a1bl,0xee2be5c766daf707l,0x80ba1e14810ac7abl,
        0xdd2ae778f530f174l } },
    /* 2 << 168 */
    { { 0x0435d97a205b9d8bl,0x6eb8f064056756d4l,0xd5e88a8bb6f8210el,
        0x070ef12dec9fd9eal },
      { 0x4d8495053bcc876al,0x12a75338a7404ce3l,0xd22b49e1b8a1db5el,
        0xec1f205114bfa5adl } },
    /* 3 << 168 */
    { { 0xadbaeb79b6828f36l,0x9d7a025801bd5b9el,0xeda01e0d1e844b0cl,
        0x4b625175887edfc9l },
      { 0x14109fdd9669b621l,0x88a2ca56f6f87b98l,0xfe2eb788170df6bcl,
        0x0cea06f4ffa473f9l } },
    /* 4 << 168 */
    { { 0x43ed81b5c4e83d33l,0xd9f358795efd488bl,0x164a620f9deb4d0fl,
        0xc6927bdbac6a7394l },
      { 0x45c28df79f9e0f03l,0x2868661efcd7e1a9l,0x7cf4e8d0ffa348f1l,
        0x6bd4c284398538e0l } },
    /* 5 << 168 */
    { { 0x2618a091289a8619l,0xef796e606671b173l,0x664e46e59090c632l,
        0xa38062d41e66f8fbl },
      { 0x6c744a200573274el,0xd07b67e4a9271394l,0x391223b26bdc0e20l,
        0xbe2d93f1eb0a05a7l } },
    /* 6 << 168 */
    { { 0xf23e2e533f36d141l,0xe84bb3d44dfca442l,0xb804a48d6b7c023al,
        0x1e16a8fa76431c3bl },
      { 0x1b5452adddd472e0l,0x7d405ee70d1ee127l,0x50fc6f1dffa27599l,
        0x351ac53cbf391b35l } },
    /* 7 << 168 */
    { { 0x7efa14b84444896bl,0x64974d2ff94027fbl,0xefdcd0e8de84487dl,
        0x8c45b2602b48989bl },
      { 0xa8fcbbc2d8463487l,0xd1b2b3f73fbc476cl,0x21d005b7c8f443c0l,
        0x518f2e6740c0139cl } },
    /* 8 << 168 */
    { { 0x56036e8c06d75fc1l,0x2dcf7bb73249a89fl,0x81dd1d3de245e7ddl,
        0xf578dc4bebd6e2a7l },
      { 0x4c028903df2ce7a0l,0xaee362889c39afacl,0xdc847c31146404abl,
        0x6304c0d8a4e97818l } },
    /* 9 << 168 */
    { { 0xae51dca2a91f6791l,0x2abe41909baa9efcl,0xd9d2e2f4559c7ac1l,
        0xe82f4b51fc9f773al },
      { 0xa77130274073e81cl,0xc0276facfbb596fcl,0x1d819fc9a684f70cl,
        0x29b47fddc9f7b1e0l } },
    /* 10 << 168 */
    { { 0x358de103459b1940l,0xec881c595b013e93l,0x51574c9349532ad3l,
        0x2db1d445b37b46del },
      { 0xc6445b87df239fd8l,0xc718af75151d24eel,0xaea1c4a4f43c6259l,
        0x40c0e5d770be02f7l } },
    /* 11 << 168 */
    { { 0x6a4590f4721b33f2l,0x2124f1fbfedf04eal,0xf8e53cde9745efe7l,
        0xe7e1043265f046d9l },
      { 0xc3fca28ee4d0c7e6l,0x847e339a87253b1bl,0x9b5953483743e643l,
        0xcb6a0a0b4fd12fc5l } },
    /* 12 << 168 */
    { { 0xfb6836c327d02dccl,0x5ad009827a68bcc2l,0x1b24b44c005e912dl,
        0xcc83d20f811fdcfel },
      { 0x36527ec1666fba0cl,0x6994819714754635l,0xfcdcb1a8556da9c2l,
        0xa593426781a732b2l } },
    /* 13 << 168 */
    { { 0xec1214eda714181dl,0x609ac13b6067b341l,0xff4b4c97a545df1fl,
        0xa124050134d2076bl },
      { 0x6efa0c231409ca97l,0x254cc1a820638c43l,0xd4e363afdcfb46cdl,
        0x62c2adc303942a27l } },
    /* 14 << 168 */
    { { 0xc67b9df056e46483l,0xa55abb2063736356l,0xab93c098c551bc52l,
        0x382b49f9b15fe64bl },
      { 0x9ec221ad4dff8d47l,0x79caf615437df4d6l,0x5f13dc64bb456509l,
        0xe4c589d9191f0714l } },
    /* 15 << 168 */
    { { 0x27b6a8ab3fd40e09l,0xe455842e77313ea9l,0x8b51d1e21f55988bl,
        0x5716dd73062bbbfcl },
      { 0x633c11e54e8bf3del,0x9a0e77b61b85be3bl,0x565107290911cca6l,
        0x27e76495efa6590fl } },
    /* 16 << 168 */
    { { 0xe4ac8b33070d3aabl,0x2643672b9a2cd5e5l,0x52eff79b1cfc9173l,
        0x665ca49b90a7c13fl },
      { 0x5a8dda59b3efb998l,0x8a5b922d052f1341l,0xae9ebbab3cf9a530l,
        0x35986e7bf56da4d7l } },
    /* 17 << 168 */
    { { 0x3a636b5cff3513ccl,0xbb0cf8ba3198f7ddl,0xb8d4052241f16f86l,
        0x760575d8de13a7bfl },
      { 0x36f74e169f7aa181l,0x163a3ecff509ed1cl,0x6aead61f3c40a491l,
        0x158c95fcdfe8fcaal } },
    /* 18 << 168 */
    { { 0xa3991b6e13cda46fl,0x79482415342faed0l,0xf3ba5bde666b5970l,
        0x1d52e6bcb26ab6ddl },
      { 0x768ba1e78608dd3dl,0x4930db2aea076586l,0xd9575714e7dc1afal,
        0x1fc7bf7df7c58817l } },
    /* 19 << 168 */
    { { 0x6b47accdd9eee96cl,0x0ca277fbe58cec37l,0x113fe413e702c42al,
        0xdd1764eec47cbe51l },
      { 0x041e7cde7b3ed739l,0x50cb74595ce9e1c0l,0x355685132925b212l,
        0x7cff95c4001b081cl } },
    /* 20 << 168 */
    { { 0x63ee4cbd8088b454l,0xdb7f32f79a9e0c8al,0xb377d4186b2447cbl,
        0xe3e982aad370219bl },
      { 0x06ccc1e4c2a2a593l,0x72c368650773f24fl,0xa13b4da795859423l,
        0x8bbf1d3375040c8fl } },
    /* 21 << 168 */
    { { 0x726f0973da50c991l,0x48afcd5b822d6ee2l,0xe5fc718b20fd7771l,
        0xb9e8e77dfd0807a1l },
      { 0x7f5e0f4499a7703dl,0x6972930e618e36f3l,0x2b7c77b823807bbel,
        0xe5b82405cb27ff50l } },
    /* 22 << 168 */
    { { 0xba8b8be3bd379062l,0xd64b7a1d2dce4a92l,0x040a73c5b2952e37l,
        0x0a9e252ed438aecal },
      { 0xdd43956bc39d3bcbl,0x1a31ca00b32b2d63l,0xd67133b85c417a18l,
        0xd08e47902ef442c8l } },
    /* 23 << 168 */
    { { 0x98cb1ae9255c0980l,0x4bd863812b4a739fl,0x5a5c31e11e4a45a1l,
        0x1e5d55fe9cb0db2fl },
      { 0x74661b068ff5cc29l,0x026b389f0eb8a4f4l,0x536b21a458848c24l,
        0x2e5bf8ec81dc72b0l } },
    /* 24 << 168 */
    { { 0x03c187d0ad886aacl,0x5c16878ab771b645l,0xb07dfc6fc74045abl,
        0x2c6360bf7800caedl },
      { 0x24295bb5b9c972a3l,0xc9e6f88e7c9a6dbal,0x90ffbf2492a79aa6l,
        0xde29d50a41c26ac2l } },
    /* 25 << 168 */
    { { 0x9f0af483d309cbe6l,0x5b020d8ae0bced4fl,0x606e986db38023e3l,
        0xad8f2c9d1abc6933l },
      { 0x19292e1de7400e93l,0xfe3e18a952be5e4dl,0xe8e9771d2e0680bfl,
        0x8c5bec98c54db063l } },
    /* 26 << 168 */
    { { 0x2af9662a74a55d1fl,0xe3fbf28f046f66d8l,0xa3a72ab4d4dc4794l,
        0x09779f455c7c2dd8l },
      { 0xd893bdafc3d19d8dl,0xd5a7509457d6a6dfl,0x8cf8fef9952e6255l,
        0x3da67cfbda9a8affl } },
    /* 27 << 168 */
    { { 0x4c23f62a2c160dcdl,0x34e6c5e38f90eaefl,0x35865519a9a65d5al,
        0x07c48aae8fd38a3dl },
      { 0xb7e7aeda50068527l,0x2c09ef231c90936al,0x31ecfeb6e879324cl,
        0xa0871f6bfb0ec938l } },
    /* 28 << 168 */
    { { 0xb1f0fb68d84d835dl,0xc90caf39861dc1e6l,0x12e5b0467594f8d7l,
        0x26897ae265012b92l },
      { 0xbcf68a08a4d6755dl,0x403ee41c0991fbdal,0x733e343e3bbf17e8l,
        0xd2c7980d679b3d65l } },
    /* 29 << 168 */
    { { 0x33056232d2e11305l,0x966be492f3c07a6fl,0x6a8878ffbb15509dl,
        0xff2211010a9b59a4l },
      { 0x6c9f564aabe30129l,0xc6f2c940336e64cfl,0x0fe752628b0c8022l,
        0xbe0267e96ae8db87l } },
    /* 30 << 168 */
    { { 0x22e192f193bc042bl,0xf085b534b237c458l,0xa0d192bd832c4168l,
        0x7a76e9e3bdf6271dl },
      { 0x52a882fab88911b5l,0xc85345e4b4db0eb5l,0xa3be02a681a7c3ffl,
        0x51889c8cf0ec0469l } },
    /* 31 << 168 */
    { { 0x9d031369a5e829e5l,0xcbb4c6fc1607aa41l,0x75ac59a6241d84c1l,
        0xc043f2bf8829e0eel },
      { 0x82a38f758ea5e185l,0x8bda40b9d87cbd9fl,0x9e65e75e2d8fc601l,
        0x3d515f74a35690b3l } },
    /* 32 << 168 */
    { { 0x534acf4fda79e5acl,0x68b83b3a8630215fl,0x5c748b2ed085756el,
        0xb0317258e5d37cb2l },
      { 0x6735841ac5ccc2c4l,0x7d7dc96b3d9d5069l,0xa147e410fd1754bdl,
        0x65296e94d399ddd5l } },
    /* 33 << 168 */
    { { 0xf6b5b2d0bc8fa5bcl,0x8a5ead67500c277bl,0x214625e6dfa08a5dl,
        0x51fdfedc959cf047l },
      { 0x6bc9430b289fca32l,0xe36ff0cf9d9bdc3fl,0x2fe187cb58ea0edel,
        0xed66af205a900b3fl } },
    /* 34 << 168 */
    { { 0x00e0968b5fa9f4d6l,0x2d4066ce37a362e7l,0xa99a9748bd07e772l,
        0x710989c006a4f1d0l },
      { 0xd5dedf35ce40cbd8l,0xab55c5f01743293dl,0x766f11448aa24e2cl,
        0x94d874f8605fbcb4l } },
    /* 35 << 168 */
    { { 0xa365f0e8a518001bl,0xee605eb69d04ef0fl,0x5a3915cdba8d4d25l,
        0x44c0e1b8b5113472l },
      { 0xcbb024e88b6740dcl,0x89087a53ee1d4f0cl,0xa88fa05c1fc4e372l,
        0x8bf395cbaf8b3af2l } },
    /* 36 << 168 */
    { { 0x1e71c9a1deb8568bl,0xa35daea080fb3d32l,0xe8b6f2662cf8fb81l,
        0x6d51afe89490696al },
      { 0x81beac6e51803a19l,0xe3d24b7f86219080l,0x727cfd9ddf6f463cl,
        0x8c6865ca72284ee8l } },
    /* 37 << 168 */
    { { 0x32c88b7db743f4efl,0x3793909be7d11dcel,0xd398f9222ff2ebe8l,
        0x2c70ca44e5e49796l },
      { 0xdf4d9929cb1131b1l,0x7826f29825888e79l,0x4d3a112cf1d8740al,
        0x00384cb6270afa8bl } },
    /* 38 << 168 */
    { { 0xcb64125b3ab48095l,0x3451c25662d05106l,0xd73d577da4955845l,
        0x39570c16bf9f4433l },
      { 0xd7dfaad3adecf263l,0xf1c3d8d1dc76e102l,0x5e774a5854c6a836l,
        0xdad4b6723e92d47bl } },
    /* 39 << 168 */
    { { 0xbe7e990ff0d796a0l,0x5fc62478df0e8b02l,0x8aae8bf4030c00adl,
        0x3d2db93b9004ba0fl },
      { 0xe48c8a79d85d5ddcl,0xe907caa76bb07f34l,0x58db343aa39eaed5l,
        0x0ea6e007adaf5724l } },
    /* 40 << 168 */
    { { 0xe00df169d23233f3l,0x3e32279677cb637fl,0x1f897c0e1da0cf6cl,
        0xa651f5d831d6bbddl },
      { 0xdd61af191a230c76l,0xbd527272cdaa5e4al,0xca753636d0abcd7el,
        0x78bdd37c370bd8dcl } },
    /* 41 << 168 */
    { { 0xc23916c217cd93fel,0x65b97a4ddadce6e2l,0xe04ed4eb174e42f8l,
        0x1491ccaabb21480al },
      { 0x145a828023196332l,0x3c3862d7587b479al,0x9f4a88a301dcd0edl,
        0x4da2b7ef3ea12f1fl } },
    /* 42 << 168 */
    { { 0xf8e7ae33b126e48el,0x404a0b32f494e237l,0x9beac474c55acadbl,
        0x4ee5cf3bcbec9fd9l },
      { 0x336b33b97df3c8c3l,0xbd905fe3b76808fdl,0x8f436981aa45c16al,
        0x255c5bfa3dd27b62l } },
    /* 43 << 168 */
    { { 0x71965cbfc3dd9b4dl,0xce23edbffc068a87l,0xb78d4725745b029bl,
        0x74610713cefdd9bdl },
      { 0x7116f75f1266bf52l,0x0204672218e49bb6l,0xdf43df9f3d6f19e3l,
        0xef1bc7d0e685cb2fl } },
    /* 44 << 168 */
    { { 0xcddb27c17078c432l,0xe1961b9cb77fedb7l,0x1edc2f5cc2290570l,
        0x2c3fefca19cbd886l },
      { 0xcf880a36c2af389al,0x96c610fdbda71ceal,0xf03977a932aa8463l,
        0x8eb7763f8586d90al } },
    /* 45 << 168 */
    { { 0x3f3424542a296e77l,0xc871868342837a35l,0x7dc710906a09c731l,
        0x54778ffb51b816dbl },
      { 0x6b33bfecaf06defdl,0xfe3c105f8592b70bl,0xf937fda461da6114l,
        0x3c13e6514c266ad7l } },
    /* 46 << 168 */
    { { 0xe363a829855938e8l,0x2eeb5d9e9de54b72l,0xbeb93b0e20ccfab9l,
        0x3dffbb5f25e61a25l },
      { 0x7f655e431acc093dl,0x0cb6cc3d3964ce61l,0x6ab283a1e5e9b460l,
        0x55d787c5a1c7e72dl } },
    /* 47 << 168 */
    { { 0x4d2efd47deadbf02l,0x11e80219ac459068l,0x810c762671f311f0l,
        0xfa17ef8d4ab6ef53l },
      { 0xaf47fd2593e43bffl,0x5cb5ff3f0be40632l,0x546871068ee61da3l,
        0x7764196eb08afd0fl } },
    /* 48 << 168 */
    { { 0x831ab3edf0290a8fl,0xcae81966cb47c387l,0xaad7dece184efb4fl,
        0xdcfc53b34749110el },
      { 0x6698f23c4cb632f9l,0xc42a1ad6b91f8067l,0xb116a81d6284180al,
        0xebedf5f8e901326fl } },
    /* 49 << 168 */
    { { 0xf2274c9f97e3e044l,0x4201852011d09fc9l,0x56a65f17d18e6e23l,
        0x2ea61e2a352b683cl },
      { 0x27d291bc575eaa94l,0x9e7bc721b8ff522dl,0x5f7268bfa7f04d6fl,
        0x5868c73faba41748l } },
    /* 50 << 168 */
    { { 0x9f85c2db7be0eeadl,0x511e7842ff719135l,0x5a06b1e9c5ea90d7l,
        0x0c19e28326fab631l },
      { 0x8af8f0cfe9206c55l,0x89389cb43553c06al,0x39dbed97f65f8004l,
        0x0621b037c508991dl } },
    /* 51 << 168 */
    { { 0x1c52e63596e78cc4l,0x5385c8b20c06b4a8l,0xd84ddfdbb0e87d03l,
        0xc49dfb66934bafadl },
      { 0x7071e17059f70772l,0x3a073a843a1db56bl,0x034949033b8af190l,
        0x7d882de3d32920f0l } },
    /* 52 << 168 */
    { { 0x91633f0ab2cf8940l,0x72b0b1786f948f51l,0x2d28dc30782653c8l,
        0x88829849db903a05l },
      { 0xb8095d0c6a19d2bbl,0x4b9e7f0c86f782cbl,0x7af739882d907064l,
        0xd12be0fe8b32643cl } },
    /* 53 << 168 */
    { { 0x358ed23d0e165dc3l,0x3d47ce624e2378cel,0x7e2bb0b9feb8a087l,
        0x3246e8aee29e10b9l },
      { 0x459f4ec703ce2b4dl,0xe9b4ca1bbbc077cfl,0x2613b4f20e9940c1l,
        0xfc598bb9047d1eb1l } },
    /* 54 << 168 */
    { { 0x9744c62b45036099l,0xa9dee742167c65d8l,0x0c511525dabe1943l,
        0xda11055493c6c624l },
      { 0xae00a52c651a3be2l,0xcda5111d884449a6l,0x063c06f4ff33bed1l,
        0x73baaf9a0d3d76b4l } },
    /* 55 << 168 */
    { { 0x52fb0c9d7fc63668l,0x6886c9dd0c039cdel,0x602bd59955b22351l,
        0xb00cab02360c7c13l },
      { 0x8cb616bc81b69442l,0x41486700b55c3ceel,0x71093281f49ba278l,
        0xad956d9c64a50710l } },
    /* 56 << 168 */
    { { 0x9561f28b638a7e81l,0x54155cdf5980ddc3l,0xb2db4a96d26f247al,
        0x9d774e4e4787d100l },
      { 0x1a9e6e2e078637d2l,0x1c363e2d5e0ae06al,0x7493483ee9cfa354l,
        0x76843cb37f74b98dl } },
    /* 57 << 168 */
    { { 0xbaca6591d4b66947l,0xb452ce9804460a8cl,0x6830d24643768f55l,
        0xf4197ed87dff12dfl },
      { 0x6521b472400dd0f7l,0x59f5ca8f4b1e7093l,0x6feff11b080338ael,
        0x0ada31f6a29ca3c6l } },
    /* 58 << 168 */
    { { 0x24794eb694a2c215l,0xd83a43ab05a57ab4l,0x264a543a2a6f89fel,
        0x2c2a3868dd5ec7c2l },
      { 0xd33739408439d9b2l,0x715ea6720acd1f11l,0x42c1d235e7e6cc19l,
        0x81ce6e96b990585cl } },
    /* 59 << 168 */
    { { 0x04e5dfe0d809c7bdl,0xd7b2580c8f1050abl,0x6d91ad78d8a4176fl,
        0x0af556ee4e2e897cl },
      { 0x162a8b73921de0acl,0x52ac9c227ea78400l,0xee2a4eeaefce2174l,
        0xbe61844e6d637f79l } },
    /* 60 << 168 */
    { { 0x0491f1bc789a283bl,0x72d3ac3d880836f4l,0xaa1c5ea388e5402dl,
        0x1b192421d5cc473dl },
      { 0x5c0b99989dc84cacl,0xb0a8482d9c6e75b8l,0x639961d03a191ce2l,
        0xda3bc8656d837930l } },
    /* 61 << 168 */
    { { 0xca990653056e6f8fl,0x84861c4164d133a7l,0x8b403276746abe40l,
        0xb7b4d51aebf8e303l },
      { 0x05b43211220a255dl,0xc997152c02419e6el,0x76ff47b6630c2feal,
        0x50518677281fdadel } },
    /* 62 << 168 */
    { { 0x3283b8bacf902b0bl,0x8d4b4eb537db303bl,0xcc89f42d755011bcl,
        0xb43d74bbdd09d19bl },
      { 0x65746bc98adba350l,0x364eaf8cb51c1927l,0x13c7659610ad72ecl,
        0x30045121f8d40c20l } },
    /* 63 << 168 */
    { { 0x6d2d99b7ea7b979bl,0xcd78cd74e6fb3bcdl,0x11e45a9e86cffbfel,
        0x78a61cf4637024f6l },
      { 0xd06bc8723d502295l,0xf1376854458cb288l,0xb9db26a1342f8586l,
        0xf33effcf4beee09el } },
    /* 64 << 168 */
    { { 0xd7e0c4cdb30cfb3al,0x6d09b8c16c9db4c8l,0x40ba1a4207c8d9dfl,
        0x6fd495f71c52c66dl },
      { 0xfb0e169f275264dal,0x80c2b746e57d8362l,0xedd987f749ad7222l,
        0xfdc229af4398ec7bl } },
    /* 0 << 175 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 175 */
    { { 0xb0d1ed8452666a58l,0x4bcb6e00e6a9c3c2l,0x3c57411c26906408l,
        0xcfc2075513556400l },
      { 0xa08b1c505294dba3l,0xa30ba2868b7dd31el,0xd70ba90e991eca74l,
        0x094e142ce762c2b9l } },
    /* 2 << 175 */
    { { 0xb81d783e979f3925l,0x1efd130aaf4c89a7l,0x525c2144fd1bf7fal,
        0x4b2969041b265a9el },
      { 0xed8e9634b9db65b6l,0x35c82e3203599d8al,0xdaa7a54f403563f3l,
        0x9df088ad022c38abl } },
    /* 3 << 175 */
    { { 0xe5cfb066bb3fd30al,0x429169daeff0354el,0x809cf8523524e36cl,
        0x136f4fb30155be1dl },
      { 0x4826af011fbba712l,0x6ef0f0b4506ba1a1l,0xd9928b3177aea73el,
        0xe2bf6af25eaa244el } },
    /* 4 << 175 */
    { { 0x8d084f124237b64bl,0x688ebe99e3ecfd07l,0x57b8a70cf6845dd8l,
        0x808fc59c5da4a325l },
      { 0xa9032b2ba3585862l,0xb66825d5edf29386l,0xb5a5a8db431ec29bl,
        0xbb143a983a1e8dc8l } },
    /* 5 << 175 */
    { { 0x35ee94ce12ae381bl,0x3a7f176c86ccda90l,0xc63a657e4606eacal,
        0x9ae5a38043cd04dfl },
      { 0x9bec8d15ed251b46l,0x1f5d6d30caca5e64l,0x347b3b359ff20f07l,
        0x4d65f034f7e4b286l } },
    /* 6 << 175 */
    { { 0x9e93ba24f111661el,0xedced484b105eb04l,0x96dc9ba1f424b578l,
        0xbf8f66b7e83e9069l },
      { 0x872d4df4d7ed8216l,0xbf07f3778e2cbecfl,0x4281d89998e73754l,
        0xfec85fbb8aab8708l } },
    /* 7 << 175 */
    { { 0x9a3c0deea5ba5b0bl,0xe6a116ce42d05299l,0xae9775fee9b02d42l,
        0x72b05200a1545cb6l },
      { 0xbc506f7d31a3b4eal,0xe58930788bbd9b32l,0xc8bc5f37e4b12a97l,
        0x6b000c064a73b671l } },
    /* 8 << 175 */
    { { 0x13b5bf22765fa7d0l,0x59805bf01d6a5370l,0x67a5e29d4280db98l,
        0x4f53916f776b1ce3l },
      { 0x714ff61f33ddf626l,0x4206238ea085d103l,0x1c50d4b7e5809ee3l,
        0x999f450d85f8eb1dl } },
    /* 9 << 175 */
    { { 0x658a6051e4c79e9bl,0x1394cb73c66a9feal,0x27f31ed5c6be7b23l,
        0xf4c88f365aa6f8fel },
      { 0x0fb0721f4aaa499el,0x68b3a7d5e3fb2a6bl,0xa788097d3a92851dl,
        0x060e7f8ae96f4913l } },
    /* 10 << 175 */
    { { 0x82eebe731a3a93bcl,0x42bbf465a21adc1al,0xc10b6fa4ef030efdl,
        0x247aa4c787b097bbl },
      { 0x8b8dc632f60c77dal,0x6ffbc26ac223523el,0xa4f6ff11344579cfl,
        0x5825653c980250f6l } },
    /* 11 << 175 */
    { { 0xb2dd097ebc1aa2b9l,0x0788939337a0333al,0x1cf55e7137a0db38l,
        0x2648487f792c1613l },
      { 0xdad013363fcef261l,0x6239c81d0eabf129l,0x8ee761de9d276be2l,
        0x406a7a341eda6ad3l } },
    /* 12 << 175 */
    { { 0x4bf367ba4a493b31l,0x54f20a529bf7f026l,0xb696e0629795914bl,
        0xcddab96d8bf236acl },
      { 0x4ff2c70aed25ea13l,0xfa1d09eb81cbbbe7l,0x88fc8c87468544c5l,
        0x847a670d696b3317l } },
    /* 13 << 175 */
    { { 0xf133421e64bcb626l,0xaea638c826dee0b5l,0xd6e7680bb310346cl,
        0xe06f4097d5d4ced3l },
      { 0x099614527512a30bl,0xf3d867fde589a59al,0x2e73254f52d0c180l,
        0x9063d8a3333c74acl } },
    /* 14 << 175 */
    { { 0xeda6c595d314e7bcl,0x2ee7464b467899edl,0x1cef423c0a1ed5d3l,
        0x217e76ea69cc7613l },
      { 0x27ccce1fe7cda917l,0x12d8016b8a893f16l,0xbcd6de849fc74f6bl,
        0xfa5817e2f3144e61l } },
    /* 15 << 175 */
    { { 0x1f3541640821ee4cl,0x1583eab40bc61992l,0x7490caf61d72879fl,
        0x998ad9f3f76ae7b2l },
      { 0x1e181950a41157f7l,0xa9d7e1e6e8da3a7el,0x963784eb8426b95fl,
        0x0ee4ed6e542e2a10l } },
    /* 16 << 175 */
    { { 0xb79d4cc5ac751e7bl,0x93f96472fd4211bdl,0x8c72d3d2c8de4fc6l,
        0x7b69cbf5df44f064l },
      { 0x3da90ca2f4bf94e1l,0x1a5325f8f12894e2l,0x0a437f6c7917d60bl,
        0x9be7048696c9cb5dl } },
    /* 17 << 175 */
    { { 0xb4d880bfe1dc5c05l,0xd738addaeebeeb57l,0x6f0119d3df0fe6a3l,
        0x5c686e5566eaaf5al },
      { 0x9cb10b50dfd0b7ecl,0xbdd0264b6a497c21l,0xfc0935148c546c96l,
        0x58a947fa79dbf42al } },
    /* 18 << 175 */
    { { 0xc0b48d4e49ccd6d7l,0xff8fb02c88bd5580l,0xc75235e907d473b2l,
        0x4fab1ac5a2188af3l },
      { 0x030fa3bc97576ec0l,0xe8c946e80b7e7d2fl,0x40a5c9cc70305600l,
        0x6d8260a9c8b013b4l } },
    /* 19 << 175 */
    { { 0x0368304f70bba85cl,0xad090da1a4a0d311l,0x7170e8702415eec1l,
        0xbfba35fe8461ea47l },
      { 0x6279019ac1e91938l,0xa47638f31afc415fl,0x36c65cbbbcba0e0fl,
        0x02160efb034e2c48l } },
    /* 20 << 175 */
    { { 0xe6c51073615cd9e4l,0x498ec047f1243c06l,0x3e5a8809b17b3d8cl,
        0x5cd99e610cc565f1l },
      { 0x81e312df7851dafel,0xf156f5baa79061e2l,0x80d62b71880c590el,
        0xbec9746f0a39faa1l } },
    /* 21 << 175 */
    { { 0x1d98a9c1c8ed1f7al,0x09e43bb5a81d5ff2l,0xd5f00f680da0794al,
        0x412050d9661aa836l },
      { 0xa89f7c4e90747e40l,0x6dc05ebbb62a3686l,0xdf4de847308e3353l,
        0x53868fbb9fb53bb9l } },
    /* 22 << 175 */
    { { 0x2b09d2c3cfdcf7ddl,0x41a9fce3723fcab4l,0x73d905f707f57ca3l,
        0x080f9fb1ac8e1555l },
      { 0x7c088e849ba7a531l,0x07d35586ed9a147fl,0x602846abaf48c336l,
        0x7320fd320ccf0e79l } },
    /* 23 << 175 */
    { { 0xaa780798b18bd1ffl,0x52c2e300afdd2905l,0xf27ea3d6434267cdl,
        0x8b96d16d15605b5fl },
      { 0x7bb310494b45706bl,0xe7f58b8e743d25f8l,0xe9b5e45b87f30076l,
        0xd19448d65d053d5al } },
    /* 24 << 175 */
    { { 0x1ecc8cb9d3210a04l,0x6bc7d463dafb5269l,0x3e59b10a67c3489fl,
        0x1769788c65641e1bl },
      { 0x8a53b82dbd6cb838l,0x7066d6e6236d5f22l,0x03aa1c616908536el,
        0xc971da0d66ae9809l } },
    /* 25 << 175 */
    { { 0x01b3a86bc49a2facl,0x3b8420c03092e77al,0x020573007d6fb556l,
        0x6941b2a1bff40a87l },
      { 0x140b63080658ff2al,0x878043633424ab36l,0x0253bd515751e299l,
        0xc75bcd76449c3e3al } },
    /* 26 << 175 */
    { { 0x92eb40907f8f875dl,0x9c9d754e56c26bbfl,0x158cea618110bbe7l,
        0x62a6b802745f91eal },
      { 0xa79c41aac6e7394bl,0x445b6a83ad57ef10l,0x0c5277eb6ea6f40cl,
        0x319fe96b88633365l } },
    /* 27 << 175 */
    { { 0x0b0fc61f385f63cbl,0x41250c8422bdd127l,0x67d153f109e942c2l,
        0x60920d08c021ad5dl },
      { 0x229f5746724d81a5l,0xb7ffb8925bba3299l,0x518c51a1de413032l,
        0x2a9bfe773c2fd94cl } },
    /* 28 << 175 */
    { { 0xcbcde2393191f4fdl,0x43093e16d3d6ada1l,0x184579f358769606l,
        0x2c94a8b3d236625cl },
      { 0x6922b9c05c437d8el,0x3d4ae423d8d9f3c8l,0xf72c31c12e7090a2l,
        0x4ac3f5f3d76a55bdl } },
    /* 29 << 175 */
    { { 0x342508fc6b6af991l,0x0d5271001b5cebbdl,0xb84740d0dd440dd7l,
        0x748ef841780162fdl },
      { 0xa8dbfe0edfc6fafbl,0xeadfdf05f7300f27l,0x7d06555ffeba4ec9l,
        0x12c56f839e25fa97l } },
    /* 30 << 175 */
    { { 0x77f84203d39b8c34l,0xed8b1be63125eddbl,0x5bbf2441f6e39dc5l,
        0xb00f6ee66a5d678al },
      { 0xba456ecf57d0ea99l,0xdcae0f5817e06c43l,0x01643de40f5b4baal,
        0x2c324341d161b9bel } },
    /* 31 << 175 */
    { { 0x80177f55e126d468l,0xed325f1f76748e09l,0x6116004acfa9bdc2l,
        0x2d8607e63a9fb468l },
      { 0x0e573e276009d660l,0x3a525d2e8d10c5a1l,0xd26cb45c3b9009a0l,
        0xb6b0cdc0de9d7448l } },
    /* 32 << 175 */
    { { 0x949c9976e1337c26l,0x6faadebdd73d68e5l,0x9e158614f1b768d9l,
        0x22dfa5579cc4f069l },
      { 0xccd6da17be93c6d6l,0x24866c61a504f5b9l,0x2121353c8d694da1l,
        0x1c6ca5800140b8c6l } },
    /* 33 << 175 */
    { { 0xc245ad8ce964021el,0xb83bffba032b82b3l,0xfaa220c647ef9898l,
        0x7e8d3ac6982c948al },
      { 0x1faa2091bc2d124al,0xbd54c3dd05b15ff4l,0x386bf3abc87c6fb7l,
        0xfb2b0563fdeb6f66l } },
    /* 34 << 175 */
    { { 0x4e77c5575b45afb4l,0xe9ded649efb8912dl,0x7ec9bbf542f6e557l,
        0x2570dfff62671f00l },
      { 0x2b3bfb7888e084bdl,0xa024b238f37fe5b4l,0x44e7dc0495649aeel,
        0x498ca2555e7ec1d8l } },
    /* 35 << 175 */
    { { 0x3bc766eaaaa07e86l,0x0db6facbf3608586l,0xbadd2549bdc259c8l,
        0x95af3c6e041c649fl },
      { 0xb36a928c02e30afbl,0x9b5356ad008a88b8l,0x4b67a5f1cf1d9e9dl,
        0xc6542e47a5d8d8cel } },
    /* 36 << 175 */
    { { 0x73061fe87adfb6ccl,0xcc826fd398678141l,0x00e758b13c80515al,
        0x6afe324741485083l },
      { 0x0fcb08b9b6ae8a75l,0xb8cf388d4acf51e1l,0x344a55606961b9d6l,
        0x1a6778b86a97fd0cl } },
    /* 37 << 175 */
    { { 0xd840fdc1ecc4c7e3l,0xde9fe47d16db68ccl,0xe95f89dea3e216aal,
        0x84f1a6a49594a8bel },
      { 0x7ddc7d725a7b162bl,0xc5cfda19adc817a3l,0x80a5d35078b58d46l,
        0x93365b1382978f19l } },
    /* 38 << 175 */
    { { 0x2e44d22526a1fc90l,0x0d6d10d24d70705dl,0xd94b6b10d70c45f4l,
        0x0f201022b216c079l },
      { 0xcec966c5658fde41l,0xa8d2bc7d7e27601dl,0xbfcce3e1ff230be7l,
        0x3394ff6b0033ffb5l } },
    /* 39 << 175 */
    { { 0xd890c5098132c9afl,0xaac4b0eb361e7868l,0x5194ded3e82d15aal,
        0x4550bd2e23ae6b7dl },
      { 0x3fda318eea5399d4l,0xd989bffa91638b80l,0x5ea124d0a14aa12dl,
        0x1fb1b8993667b944l } },
    /* 40 << 175 */
    { { 0x95ec796944c44d6al,0x91df144a57e86137l,0x915fd62073adac44l,
        0x8f01732d59a83801l },
      { 0xec579d253aa0a633l,0x06de5e7cc9d6d59cl,0xc132f958b1ef8010l,
        0x29476f96e65c1a02l } },
    /* 41 << 175 */
    { { 0x336a77c0d34c3565l,0xef1105b21b9f1e9el,0x63e6d08bf9e08002l,
        0x9aff2f21c613809el },
      { 0xb5754f853a80e75dl,0xde71853e6bbda681l,0x86f041df8197fd7al,
        0x8b332e08127817fal } },
    /* 42 << 175 */
    { { 0x05d99be8b9c20cdal,0x89f7aad5d5cd0c98l,0x7ef936fe5bb94183l,
        0x92ca0753b05cd7f2l },
      { 0x9d65db1174a1e035l,0x02628cc813eaea92l,0xf2d9e24249e4fbf2l,
        0x94fdfd9be384f8b7l } },
    /* 43 << 175 */
    { { 0x65f5605463428c6bl,0x2f7205b290b409a5l,0xf778bb78ff45ae11l,
        0xa13045bec5ee53b2l },
      { 0xe00a14ff03ef77fel,0x689cd59fffef8befl,0x3578f0ed1e9ade22l,
        0xe99f3ec06268b6a8l } },
    /* 44 << 175 */
    { { 0xa2057d91ea1b3c3el,0x2d1a7053b8823a4al,0xabbb336a2cca451el,
        0xcd2466e32218bb5dl },
      { 0x3ac1f42fc8cb762dl,0x7e312aae7690211fl,0xebb9bd7345d07450l,
        0x207c4b8246c2213fl } },
    /* 45 << 175 */
    { { 0x99d425c1375913ecl,0x94e45e9667908220l,0xc08f3087cd67dbf6l,
        0xa5670fbec0887056l },
      { 0x6717b64a66f5b8fcl,0xd5a56aea786fec28l,0xa8c3f55fc0ff4952l,
        0xa77fefae457ac49bl } },
    /* 46 << 175 */
    { { 0x29882d7c98379d44l,0xd000bdfb509edc8al,0xc6f95979e66fe464l,
        0x504a6115fa61bde0l },
      { 0x56b3b871effea31al,0x2d3de26df0c21a54l,0x21dbff31834753bfl,
        0xe67ecf4969269d86l } },
    /* 47 << 175 */
    { { 0x7a176952151fe690l,0x035158047f2adb5fl,0xee794b15d1b62a8dl,
        0xf004ceecaae454e6l },
      { 0x0897ea7cf0386facl,0x3b62ff12d1fca751l,0x154181df1b7a04ecl,
        0x2008e04afb5847ecl } },
    /* 48 << 175 */
    { { 0xd147148e41dbd772l,0x2b419f7322942654l,0x669f30d3e9c544f7l,
        0x52a2c223c8540149l },
      { 0x5da9ee14634dfb02l,0x5f074ff0f47869f3l,0x74ee878da3933accl,
        0xe65106514fe35ed1l } },
    /* 49 << 175 */
    { { 0xb3eb9482f1012e7al,0x51013cc0a8a566ael,0xdd5e924347c00d3bl,
        0x7fde089d946bb0e5l },
      { 0x030754fec731b4b3l,0x12a136a499fda062l,0x7c1064b85a1a35bcl,
        0xbf1f5763446c84efl } },
    /* 50 << 175 */
    { { 0xed29a56da16d4b34l,0x7fba9d09dca21c4fl,0x66d7ac006d8de486l,
        0x6006198773a2a5e1l },
      { 0x8b400f869da28ff0l,0x3133f70843c4599cl,0x9911c9b8ee28cb0dl,
        0xcd7e28748e0af61dl } },
    /* 51 << 175 */
    { { 0x5a85f0f272ed91fcl,0x85214f319cd4a373l,0x881fe5be1925253cl,
        0xd8dc98e091e8bc76l },
      { 0x7120affe585cc3a2l,0x724952ed735bf97al,0x5581e7dc3eb34581l,
        0x5cbff4f2e52ee57dl } },
    /* 52 << 175 */
    { { 0x8d320a0e87d8cc7bl,0x9beaa7f3f1d280d0l,0x7a0b95719beec704l,
        0x9126332e5b7f0057l },
      { 0x01fbc1b48ed3bd6dl,0x35bb2c12d945eb24l,0x6404694e9a8ae255l,
        0xb6092eec8d6abfb3l } },
    /* 53 << 175 */
    { { 0x4d76143fcc058865l,0x7b0a5af26e249922l,0x8aef94406a50d353l,
        0xe11e4bcc64f0e07al },
      { 0x4472993aa14a90fal,0x7706e20cba0c51d4l,0xf403292f1532672dl,
        0x52573bfa21829382l } },
    /* 54 << 175 */
    { { 0x6a7bb6a93b5bdb83l,0x08da65c0a4a72318l,0xc58d22aa63eb065fl,
        0x1717596c1b15d685l },
      { 0x112df0d0b266d88bl,0xf688ae975941945al,0x487386e37c292cacl,
        0x42f3b50d57d6985cl } },
    /* 55 << 175 */
    { { 0x6da4f9986a90fc34l,0xc8f257d365ca8a8dl,0xc2feabca6951f762l,
        0xe1bc81d074c323acl },
      { 0x1bc68f67251a2a12l,0x10d86587be8a70dcl,0xd648af7ff0f84d2el,
        0xf0aa9ebc6a43ac92l } },
    /* 56 << 175 */
    { { 0x69e3be0427596893l,0xb6bb02a645bf452bl,0x0875c11af4c698c8l,
        0x6652b5c7bece3794l },
      { 0x7b3755fd4f5c0499l,0x6ea16558b5532b38l,0xd1c69889a2e96ef7l,
        0x9c773c3a61ed8f48l } },
    /* 57 << 175 */
    { { 0x2b653a409b323abcl,0xe26605e1f0e1d791l,0x45d410644a87157al,
        0x8f9a78b7cbbce616l },
      { 0xcf1e44aac407edddl,0x81ddd1d8a35b964fl,0x473e339efd083999l,
        0x6c94bdde8e796802l } },
    /* 58 << 175 */
    { { 0x5a304ada8545d185l,0x82ae44ea738bb8cbl,0x628a35e3df87e10el,
        0xd3624f3da15b9fe3l },
      { 0xcc44209b14be4254l,0x7d0efcbcbdbc2ea5l,0x1f60336204c37bbel,
        0x21f363f556a5852cl } },
    /* 59 << 175 */
    { { 0xa1503d1ca8501550l,0x2251e0e1d8ab10bbl,0xde129c966961c51cl,
        0x1f7246a481910f68l },
      { 0x2eb744ee5f2591f2l,0x3c47d33f5e627157l,0x4d6d62c922f3bd68l,
        0x6120a64bcb8df856l } },
    /* 60 << 175 */
    { { 0x3a9ac6c07b5d07dfl,0xa92b95587ef39783l,0xe128a134ab3a9b4fl,
        0x41c18807b1252f05l },
      { 0xfc7ed08980ba9b1cl,0xac8dc6dec532a9ddl,0xbf829cef55246809l,
        0x101b784f5b4ee80fl } },
    /* 61 << 175 */
    { { 0xc09945bbb6f11603l,0x57b09dbe41d2801el,0xfba5202fa97534a8l,
        0x7fd8ae5fc17b9614l },
      { 0xa50ba66678308435l,0x9572f77cd3868c4dl,0x0cef7bfd2dd7aab0l,
        0xe7958e082c7c79ffl } },
    /* 62 << 175 */
    { { 0x81262e4225346689l,0x716da290b07c7004l,0x35f911eab7950ee3l,
        0x6fd72969261d21b5l },
      { 0x5238980308b640d3l,0x5b0026ee887f12a1l,0x20e21660742e9311l,
        0x0ef6d5415ff77ff7l } },
    /* 63 << 175 */
    { { 0x969127f0f9c41135l,0xf21d60c968a64993l,0x656e5d0ce541875cl,
        0xf1e0f84ea1d3c233l },
      { 0x9bcca35906002d60l,0xbe2da60c06191552l,0x5da8bbae61181ec3l,
        0x9f04b82365806f19l } },
    /* 64 << 175 */
    { { 0xf1604a7dd4b79bb8l,0xaee806fb52c878c8l,0x34144f118d47b8e8l,
        0x72edf52b949f9054l },
      { 0xebfca84e2127015al,0x9051d0c09cb7cef3l,0x86e8fe58296deec8l,
        0x33b2818841010d74l } },
    /* 0 << 182 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 182 */
    { { 0x01079383171b445fl,0x9bcf21e38131ad4cl,0x8cdfe205c93987e8l,
        0xe63f4152c92e8c8fl },
      { 0x729462a930add43dl,0x62ebb143c980f05al,0x4f3954e53b06e968l,
        0xfe1d75ad242cf6b1l } },
    /* 2 << 182 */
    { { 0x5f95c6c7af8685c8l,0xd4c1c8ce2f8f01aal,0xc44bbe322574692al,
        0xb8003478d4a4a068l },
      { 0x7c8fc6e52eca3cdbl,0xea1db16bec04d399l,0xb05bc82e8f2bc5cfl,
        0x763d517ff44793d2l } },
    /* 3 << 182 */
    { { 0x4451c1b808bd98d0l,0x644b1cd46575f240l,0x6907eb337375d270l,
        0x56c8bebdfa2286bdl },
      { 0xc713d2acc4632b46l,0x17da427aafd60242l,0x313065b7c95c7546l,
        0xf8239898bf17a3del } },
    /* 4 << 182 */
    { { 0xf3b7963f4c830320l,0x842c7aa0903203e3l,0xaf22ca0ae7327afbl,
        0x38e13092967609b6l },
      { 0x73b8fb62757558f1l,0x3cc3e831f7eca8c1l,0xe4174474f6331627l,
        0xa77989cac3c40234l } },
    /* 5 << 182 */
    { { 0xe5fd17a144a081e0l,0xd797fb7db70e296al,0x2b472b30481f719cl,
        0x0e632a98fe6f8c52l },
      { 0x89ccd116c5f0c284l,0xf51088af2d987c62l,0x2a2bccda4c2de6cfl,
        0x810f9efef679f0f9l } },
    /* 6 << 182 */
    { { 0xb0f394b97ffe4b3el,0x0b691d21e5fa5d21l,0xb0bd77479dfbbc75l,
        0xd2830fdafaf78b00l },
      { 0xf78c249c52434f57l,0x4b1f754598096dabl,0x73bf6f948ff8c0b3l,
        0x34aef03d454e134cl } },
    /* 7 << 182 */
    { { 0xf8d151f4b7ac7ec5l,0xd6ceb95ae50da7d5l,0xa1b492b0dc3a0eb8l,
        0x75157b69b3dd2863l },
      { 0xe2c4c74ec5413d62l,0xbe329ff7bc5fc4c7l,0x835a2aea60fa9ddal,
        0xf117f5ad7445cb87l } },
    /* 8 << 182 */
    { { 0xae8317f4b0166f7al,0xfbd3e3f7ceec74e6l,0xfdb516ace0874bfdl,
        0x3d846019c681f3a3l },
      { 0x0b12ee5c7c1620b0l,0xba68b4dd2b63c501l,0xac03cd326668c51el,
        0x2a6279f74e0bcb5bl } },
    /* 9 << 182 */
    { { 0x17bd69b06ae85c10l,0x729469791dfdd3a6l,0xd9a032682c078becl,
        0x41c6a658bfd68a52l },
      { 0xcdea10240e023900l,0xbaeec121b10d144dl,0x5a600e74058ab8dcl,
        0x1333af21bb89ccddl } },
    /* 10 << 182 */
    { { 0xdf25eae03aaba1f1l,0x2cada16e3b7144cfl,0x657ee27d71ab98bcl,
        0x99088b4c7a6fc96el },
      { 0x05d5c0a03549dbd4l,0x42cbdf8ff158c3acl,0x3fb6b3b087edd685l,
        0x22071cf686f064d0l } },
    /* 11 << 182 */
    { { 0xd2d6721fff2811e5l,0xdb81b703fe7fae8cl,0x3cfb74efd3f1f7bbl,
        0x0cdbcd7616cdeb5dl },
      { 0x4f39642a566a808cl,0x02b74454340064d6l,0xfabbadca0528fa6fl,
        0xe4c3074cd3fc0bb6l } },
    /* 12 << 182 */
    { { 0xb32cb8b0b796d219l,0xc3e95f4f34741dd9l,0x8721212568edf6f5l,
        0x7a03aee4a2b9cb8el },
      { 0x0cd3c376f53a89aal,0x0d8af9b1948a28dcl,0xcf86a3f4902ab04fl,
        0x8aacb62a7f42002dl } },
    /* 13 << 182 */
    { { 0x106985ebf62ffd52l,0xe670b54e5797bf10l,0x4b405209c5e30aefl,
        0x12c97a204365b5e9l },
      { 0x104646ce1fe32093l,0x13cb4ff63907a8c9l,0x8b9f30d1d46e726bl,
        0xe1985e21aba0f499l } },
    /* 14 << 182 */
    { { 0xc573dea910a230cdl,0x24f46a93cd30f947l,0xf2623fcfabe2010al,
        0x3f278cb273f00e4fl },
      { 0xed55c67d50b920ebl,0xf1cb9a2d8e760571l,0x7c50d1090895b709l,
        0x4207cf07190d4369l } },
    /* 15 << 182 */
    { { 0x3b027e81c4127fe1l,0xa9f8b9ad3ae9c566l,0x5ab10851acbfbba5l,
        0xa747d648569556f5l },
      { 0xcc172b5c2ba97bf7l,0x15e0f77dbcfa3324l,0xa345b7977686279dl,
        0x5a723480e38003d3l } },
    /* 16 << 182 */
    { { 0xfd8e139f8f5fcda8l,0xf3e558c4bdee5bfdl,0xd76cbaf4e33f9f77l,
        0x3a4c97a471771969l },
      { 0xda27e84bf6dce6a7l,0xff373d9613e6c2d1l,0xf115193cd759a6e9l,
        0x3f9b702563d2262cl } },
    /* 17 << 182 */
    { { 0xd9764a31317cd062l,0x30779d8e199f8332l,0xd807410616b11b0bl,
        0x7917ab9f78aeaed8l },
      { 0xb67a9cbe28fb1d8el,0x2e313563136eda33l,0x010b7069a371a86cl,
        0x44d90fa26744e6b7l } },
    /* 18 << 182 */
    { { 0x68190867d6b3e243l,0x9fe6cd9d59048c48l,0xb900b02895731538l,
        0xa012062f32cae04fl },
      { 0x8107c8bc9399d082l,0x47e8c54a41df12e2l,0x14ba5117b6ef3f73l,
        0x22260bea81362f0bl } },
    /* 19 << 182 */
    { { 0x90ea261e1a18cc20l,0x2192999f2321d636l,0xef64d314e311b6a0l,
        0xd7401e4c3b54a1f5l },
      { 0x190199836fbca2bal,0x46ad32938fbffc4bl,0xa142d3f63786bf40l,
        0xeb5cbc26b67039fcl } },
    /* 20 << 182 */
    { { 0x9cb0ae6c252bd479l,0x05e0f88a12b5848fl,0x78f6d2b2a5c97663l,
        0x6f6e149bc162225cl },
      { 0xe602235cde601a89l,0xd17bbe98f373be1fl,0xcaf49a5ba8471827l,
        0x7e1a0a8518aaa116l } },
    /* 21 << 182 */
    { { 0x6c833196270580c3l,0x1e233839f1c98a14l,0x67b2f7b4ae34e0a5l,
        0x47ac8745d8ce7289l },
      { 0x2b74779a100dd467l,0x274a43374ee50d09l,0x603dcf1383608bc9l,
        0xcd9da6c3c89e8388l } },
    /* 22 << 182 */
    { { 0x2660199f355116acl,0xcc38bb59b6d18eedl,0x3075f31f2f4bc071l,
        0x9774457f265dc57el },
      { 0x06a6a9c8c6db88bbl,0x6429d07f4ec98e04l,0x8d05e57b05ecaa8bl,
        0x20f140b17872ea7bl } },
    /* 23 << 182 */
    { { 0xdf8c0f09ca494693l,0x48d3a020f252e909l,0x4c5c29af57b14b12l,
        0x7e6fa37dbf47ad1cl },
      { 0x66e7b50649a0c938l,0xb72c0d486be5f41fl,0x6a6242b8b2359412l,
        0xcd35c7748e859480l } },
    /* 24 << 182 */
    { { 0x12536fea87baa627l,0x58c1fec1f72aa680l,0x6c29b637601e5dc9l,
        0x9e3c3c1cde9e01b9l },
      { 0xefc8127b2bcfe0b0l,0x351071022a12f50dl,0x6ccd6cb14879b397l,
        0xf792f804f8a82f21l } },
    /* 25 << 182 */
    { { 0x509d4804a9b46402l,0xedddf85dc10f0850l,0x928410dc4b6208aal,
        0xf6229c46391012dcl },
      { 0xc5a7c41e7727b9b6l,0x289e4e4baa444842l,0x049ba1d9e9a947eal,
        0x44f9e47f83c8debcl } },
    /* 26 << 182 */
    { { 0xfa77a1fe611f8b8el,0xfd2e416af518f427l,0xc5fffa70114ebac3l,
        0xfe57c4e95d89697bl },
      { 0xfdd053acb1aaf613l,0x31df210fea585a45l,0x318cc10e24985034l,
        0x1a38efd15f1d6130l } },
    /* 27 << 182 */
    { { 0xbf86f2370b1e9e21l,0xb258514d1dbe88aal,0x1e38a58890c1baf9l,
        0x2936a01ebdb9b692l },
      { 0xd576de986dd5b20cl,0xb586bf7170f98ecfl,0xcccf0f12c42d2fd7l,
        0x8717e61cfb35bd7bl } },
    /* 28 << 182 */
    { { 0x8b1e572235e6fc06l,0x3477728f0b3e13d5l,0x150c294daa8a7372l,
        0xc0291d433bfa528al },
      { 0xc6c8bc67cec5a196l,0xdeeb31e45c2e8a7cl,0xba93e244fb6e1c51l,
        0xb9f8b71b2e28e156l } },
    /* 29 << 182 */
    { { 0xce65a287968a2ab9l,0xe3c5ce6946bbcb1fl,0xf8c835b9e7ae3f30l,
        0x16bbee26ff72b82bl },
      { 0x665e2017fd42cd22l,0x1e139970f8b1d2a0l,0x125cda2979204932l,
        0x7aee94a549c3bee5l } },
    /* 30 << 182 */
    { { 0x68c7016089821a66l,0xf7c376788f981669l,0xd90829fc48cc3645l,
        0x346af049d70addfcl },
      { 0x2057b232370bf29cl,0xf90c73ce42e650eel,0xe03386eaa126ab90l,
        0x0e266e7e975a087bl } },
    /* 31 << 182 */
    { { 0x80578eb90fca65d9l,0x7e2989ea16af45b8l,0x7438212dcac75a4el,
        0x38c7ca394fef36b8l },
      { 0x8650c494d402676al,0x26ab5a66f72c7c48l,0x4e6cb426ce3a464el,
        0xf8f998962b72f841l } },
    /* 32 << 182 */
    { { 0x8c3184911a335cc8l,0x563459ba6a5913e4l,0x1b920d61c7b32919l,
        0x805ab8b6a02425adl },
      { 0x2ac512da8d006086l,0x6ca4846abcf5c0fdl,0xafea51d8ac2138d7l,
        0xcb647545344cd443l } },
    /* 33 << 182 */
    { { 0x0429ee8fbd7d9040l,0xee66a2de819b9c96l,0x54f9ec25dea7d744l,
        0x2ffea642671721bbl },
      { 0x4f19dbd1114344eal,0x04304536fd0dbc8bl,0x014b50aa29ec7f91l,
        0xb5fc22febb06014dl } },
    /* 34 << 182 */
    { { 0x60d963a91ee682e0l,0xdf48abc0fe85c727l,0x0cadba132e707c2dl,
        0xde608d3aa645aeffl },
      { 0x05f1c28bedafd883l,0x3c362edebd94de1fl,0x8dd0629d13593e41l,
        0x0a5e736f766d6eafl } },
    /* 35 << 182 */
    { { 0xbfa92311f68cf9d1l,0xa4f9ef87c1797556l,0x10d75a1f5601c209l,
        0x651c374c09b07361l },
      { 0x49950b5888b5ceadl,0x0ef000586fa9dbaal,0xf51ddc264e15f33al,
        0x1f8b5ca62ef46140l } },
    /* 36 << 182 */
    { { 0x343ac0a3ee9523f0l,0xbb75eab2975ea978l,0x1bccf332107387f4l,
        0x790f92599ab0062el },
      { 0xf1a363ad1e4f6a5fl,0x06e08b8462519a50l,0x609151877265f1eel,
        0x6a80ca3493ae985el } },
    /* 37 << 182 */
    { { 0x81b29768aaba4864l,0xb13cabf28d52a7d6l,0xb5c363488ead03f1l,
        0xc932ad9581c7c1c0l },
      { 0x5452708ecae1e27bl,0x9dac42691b0df648l,0x233e3f0cdfcdb8bcl,
        0xe6ceccdfec540174l } },
    /* 38 << 182 */
    { { 0xbd0d845e95081181l,0xcc8a7920699355d5l,0x111c0f6dc3b375a8l,
        0xfd95bc6bfd51e0dcl },
      { 0x4a106a266888523al,0x4d142bd6cb01a06dl,0x79bfd289adb9b397l,
        0x0bdbfb94e9863914l } },
    /* 39 << 182 */
    { { 0x29d8a2291660f6a6l,0x7f6abcd6551c042dl,0x13039deb0ac3ffe8l,
        0xa01be628ec8523fbl },
      { 0x6ea341030ca1c328l,0xc74114bdb903928el,0x8aa4ff4e9e9144b0l,
        0x7064091f7f9a4b17l } },
    /* 40 << 182 */
    { { 0xa3f4f521e447f2c4l,0x81b8da7a604291f0l,0xd680bc467d5926del,
        0x84f21fd534a1202fl },
      { 0x1d1e31814e9df3d8l,0x1ca4861a39ab8d34l,0x809ddeec5b19aa4al,
        0x59f72f7e4d329366l } },
    /* 41 << 182 */
    { { 0xa2f93f41386d5087l,0x40bf739cdd67d64fl,0xb449420566702158l,
        0xc33c65be73b1e178l },
      { 0xcdcd657c38ca6153l,0x97f4519adc791976l,0xcc7c7f29cd6e1f39l,
        0x38de9cfb7e3c3932l } },
    /* 42 << 182 */
    { { 0xe448eba37b793f85l,0xe9f8dbf9f067e914l,0xc0390266f114ae87l,
        0x39ed75a7cd6a8e2al },
      { 0xadb148487ffba390l,0x67f8cb8b6af9bc09l,0x322c38489c7476dbl,
        0xa320fecf52a538d6l } },
    /* 43 << 182 */
    { { 0xe0493002b2aced2bl,0xdfba1809616bd430l,0x531c4644c331be70l,
        0xbc04d32e90d2e450l },
      { 0x1805a0d10f9f142dl,0x2c44a0c547ee5a23l,0x31875a433989b4e3l,
        0x6b1949fd0c063481l } },
    /* 44 << 182 */
    { { 0x2dfb9e08be0f4492l,0x3ff0da03e9d5e517l,0x03dbe9a1f79466a8l,
        0x0b87bcd015ea9932l },
      { 0xeb64fc83ab1f58abl,0x6d9598da817edc8al,0x699cff661d3b67e5l,
        0x645c0f2992635853l } },
    /* 45 << 182 */
    { { 0x253cdd82eabaf21cl,0x82b9602a2241659el,0x2cae07ec2d9f7091l,
        0xbe4c720c8b48cd9bl },
      { 0x6ce5bc036f08d6c9l,0x36e8a997af10bf40l,0x83422d213e10ff12l,
        0x7b26d3ebbcc12494l } },
    /* 46 << 182 */
    { { 0xb240d2d0c9469ad6l,0xc4a11b4d30afa05bl,0x4b604acedd6ba286l,
        0x184866003ee2864cl },
      { 0x5869d6ba8d9ce5bel,0x0d8f68c5ff4bfb0dl,0xb69f210b5700cf73l,
        0x61f6653a6d37c135l } },
    /* 47 << 182 */
    { { 0xff3d432b5aff5a48l,0x0d81c4b972ba3a69l,0xee879ae9fa1899efl,
        0xbac7e2a02d6acafdl },
      { 0xd6d93f6c1c664399l,0x4c288de15bcb135dl,0x83031dab9dab7cbfl,
        0xfe23feb03abbf5f0l } },
    /* 48 << 182 */
    { { 0x9f1b2466cdedca85l,0x140bb7101a09538cl,0xac8ae8515e11115dl,
        0x0d63ff676f03f59el },
      { 0x755e55517d234afbl,0x61c2db4e7e208fc1l,0xaa9859cef28a4b5dl,
        0xbdd6d4fc34af030fl } },
    /* 49 << 182 */
    { { 0xd1c4a26d3be01cb1l,0x9ba14ffc243aa07cl,0xf95cd3a9b2503502l,
        0xe379bc067d2a93abl },
      { 0x3efc18e9d4ca8d68l,0x083558ec80bb412al,0xd903b9409645a968l,
        0xa499f0b69ba6054fl } },
    /* 50 << 182 */
    { { 0x208b573cb8349abel,0x3baab3e530b4fc1cl,0x87e978bacb524990l,
        0x3524194eccdf0e80l },
      { 0x627117257d4bcc42l,0xe90a3d9bb90109bal,0x3b1bdd571323e1e0l,
        0xb78e9bd55eae1599l } },
    /* 51 << 182 */
    { { 0x0794b7469e03d278l,0x80178605d70e6297l,0x171792f899c97855l,
        0x11b393eef5a86b5cl },
      { 0x48ef6582d8884f27l,0xbd44737abf19ba5fl,0x8698de4ca42062c6l,
        0x8975eb8061ce9c54l } },
    /* 52 << 182 */
    { { 0xd50e57c7d7fe71f3l,0x15342190bc97ce38l,0x51bda2de4df07b63l,
        0xba12aeae200eb87dl },
      { 0xabe135d2a9b4f8f6l,0x04619d65fad6d99cl,0x4a6683a77994937cl,
        0x7a778c8b6f94f09al } },
    /* 53 << 182 */
    { { 0x8c50862320a71b89l,0x241a2aed1c229165l,0x352be595aaf83a99l,
        0x9fbfee7f1562bac8l },
      { 0xeaf658b95c4017e3l,0x1dc7f9e015120b86l,0xd84f13dd4c034d6fl,
        0x283dd737eaea3038l } },
    /* 54 << 182 */
    { { 0x197f2609cd85d6a2l,0x6ebbc345fae60177l,0xb80f031b4e12fedel,
        0xde55d0c207a2186bl },
      { 0x1fb3e37f24dcdd5al,0x8d602da57ed191fbl,0x108fb05676023e0dl,
        0x70178c71459c20c0l } },
    /* 55 << 182 */
    { { 0xfad5a3863fe54cf0l,0xa4a3ec4f02bbb475l,0x1aa5ec20919d94d7l,
        0x5d3b63b5a81e4ab3l },
      { 0x7fa733d85ad3d2afl,0xfbc586ddd1ac7a37l,0x282925de40779614l,
        0xfe0ffffbe74a242al } },
    /* 56 << 182 */
    { { 0x3f39e67f906151e5l,0xcea27f5f55e10649l,0xdca1d4e1c17cf7b7l,
        0x0c326d122fe2362dl },
      { 0x05f7ac337dd35df3l,0x0c3b7639c396dbdfl,0x0912f5ac03b7db1cl,
        0x9dea4b705c9ed4a9l } },
    /* 57 << 182 */
    { { 0x475e6e53aae3f639l,0xfaba0e7cfc278bacl,0x16f9e2219490375fl,
        0xaebf9746a5a7ed0al },
      { 0x45f9af3ff41ad5d6l,0x03c4623cb2e99224l,0x82c5bb5cb3cf56aal,
        0x6431181934567ed3l } },
    /* 58 << 182 */
    { { 0xec57f2118be489acl,0x2821895db9a1104bl,0x610dc8756064e007l,
        0x8e526f3f5b20d0fel },
      { 0x6e71ca775b645aeel,0x3d1dcb9f800e10ffl,0x36b51162189cf6del,
        0x2c5a3e306bb17353l } },
    /* 59 << 182 */
    { { 0xc186cd3e2a6c6fbfl,0xa74516fa4bf97906l,0x5b4b8f4b279d6901l,
        0x0c4e57b42b573743l },
      { 0x75fdb229b6e386b6l,0xb46793fd99deac27l,0xeeec47eacf712629l,
        0xe965f3c4cbc3b2ddl } },
    /* 60 << 182 */
    { { 0x8dd1fb83425c6559l,0x7fc00ee60af06fdal,0xe98c922533d956dfl,
        0x0f1ef3354fbdc8a2l },
      { 0x2abb5145b79b8ea2l,0x40fd2945bdbff288l,0x6a814ac4d7185db7l,
        0xc4329d6fc084609al } },
    /* 61 << 182 */
    { { 0xc9ba7b52ed1be45dl,0x891dd20de4cd2c74l,0x5a4d4a7f824139b1l,
        0x66c17716b873c710l },
      { 0x5e5bc1412843c4e0l,0xd5ac4817b97eb5bfl,0xc0f8af54450c95c7l,
        0xc91b3fa0318406c5l } },
    /* 62 << 182 */
    { { 0x360c340aab9d97f8l,0xfb57bd0790a2d611l,0x4339ae3ca6a6f7e5l,
        0x9c1fcd2a2feb8a10l },
      { 0x972bcca9c7ea7432l,0x1b0b924c308076f6l,0x80b2814a2a5b4ca5l,
        0x2f78f55b61ef3b29l } },
    /* 63 << 182 */
    { { 0xf838744ac18a414fl,0xc611eaae903d0a86l,0x94dabc162a453f55l,
        0xe6f2e3da14efb279l },
      { 0x5b7a60179320dc3cl,0x692e382f8df6b5a4l,0x3f5e15e02d40fa90l,
        0xc87883ae643dd318l } },
    /* 64 << 182 */
    { { 0x511053e453544774l,0x834d0ecc3adba2bcl,0x4215d7f7bae371f5l,
        0xfcfd57bf6c8663bcl },
      { 0xded2383dd6901b1dl,0x3b49fbb4b5587dc3l,0xfd44a08d07625f62l,
        0x3ee4d65b9de9b762l } },
    /* 0 << 189 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 189 */
    { { 0x64e5137d0d63d1fal,0x658fc05202a9d89fl,0x4889487450436309l,
        0xe9ae30f8d598da61l },
      { 0x2ed710d1818baf91l,0xe27e9e068b6a0c20l,0x1e28dcfb1c1a6b44l,
        0x883acb64d6ac57dcl } },
    /* 2 << 189 */
    { { 0x8735728dc2c6ff70l,0x79d6122fc5dc2235l,0x23f5d00319e277f9l,
        0x7ee84e25dded8cc7l },
      { 0x91a8afb063cd880al,0x3f3ea7c63574af60l,0x0cfcdc8402de7f42l,
        0x62d0792fb31aa152l } },
    /* 3 << 189 */
    { { 0x8e1b4e438a5807cel,0xad283893e4109a7el,0xc30cc9cbafd59ddal,
        0xf65f36c63d8d8093l },
      { 0xdf31469ea60d32b2l,0xee93df4b3e8191c8l,0x9c1017c5355bdeb5l,
        0xd26231858616aa28l } },
    /* 4 << 189 */
    { { 0xb02c83f9dec31a21l,0x988c8b236ad9d573l,0x53e983aea57be365l,
        0xe968734d646f834el },
      { 0x9137ea8f5da6309bl,0x10f3a624c1f1ce16l,0x782a9ea2ca440921l,
        0xdf94739e5b46f1b5l } },
    /* 5 << 189 */
    { { 0x9f9be006cce85c9bl,0x360e70d6a4c7c2d3l,0x2cd5beeaaefa1e60l,
        0x64cf63c08c3d2b6dl },
      { 0xfb107fa3e1cf6f90l,0xb7e937c6d5e044e6l,0x74e8ca78ce34db9fl,
        0x4f8b36c13e210bd0l } },
    /* 6 << 189 */
    { { 0x1df165a434a35ea8l,0x3418e0f74d4412f6l,0x5af1f8af518836c3l,
        0x42ceef4d130e1965l },
      { 0x5560ca0b543a1957l,0xc33761e5886cb123l,0x66624b1ffe98ed30l,
        0xf772f4bf1090997dl } },
    /* 7 << 189 */
    { { 0xf4e540bb4885d410l,0x7287f8109ba5f8d7l,0x22d0d865de98dfb1l,
        0x49ff51a1bcfbb8a3l },
      { 0xb6b6fa536bc3012el,0x3d31fd72170d541dl,0x8018724f4b0f4966l,
        0x79e7399f87dbde07l } },
    /* 8 << 189 */
    { { 0x56f8410ef4f8b16al,0x97241afec47b266al,0x0a406b8e6d9c87c1l,
        0x803f3e02cd42ab1bl },
      { 0x7f0309a804dbec69l,0xa83b85f73bbad05fl,0xc6097273ad8e197fl,
        0xc097440e5067adc1l } },
    /* 9 << 189 */
    { { 0x730eafb63524ff16l,0xd7f9b51e823fc6cel,0x27bd0d32443e4ac0l,
        0x40c59ad94d66f217l },
      { 0x6c33136f17c387a4l,0x5043b8d5eb86804dl,0x74970312675a73c9l,
        0x838fdb31f16669b6l } },
    /* 10 << 189 */
    { { 0xc507b6dd418e7dddl,0x39888d93472f19d6l,0x7eae26be0c27eb4dl,
        0x17b53ed3fbabb884l },
      { 0xfc27021b2b01ae4fl,0x88462e87cf488682l,0xbee096ec215e2d87l,
        0xeb2fea9ad242e29bl } },
    /* 11 << 189 */
    { { 0x5d985b5fb821fc28l,0x89d2e197dc1e2ad2l,0x55b566b89030ba62l,
        0xe3fd41b54f41b1c6l },
      { 0xb738ac2eb9a96d61l,0x7f8567ca369443f4l,0x8698622df803a440l,
        0x2b5862368fe2f4dcl } },
    /* 12 << 189 */
    { { 0xbbcc00c756b95bcel,0x5ec03906616da680l,0x79162ee672214252l,
        0x43132b6386a892d2l },
      { 0x4bdd3ff22f3263bfl,0xd5b3733c9cd0a142l,0x592eaa8244415ccbl,
        0x663e89248d5474eal } },
    /* 13 << 189 */
    { { 0x8058a25e5236344el,0x82e8df9dbda76ee6l,0xdcf6efd811cc3d22l,
        0x00089cda3b4ab529l },
      { 0x91d3a071bd38a3dbl,0x4ea97fc0ef72b925l,0x0c9fc15bea3edf75l,
        0x5a6297cda4348ed3l } },
    /* 14 << 189 */
    { { 0x0d38ab35ce7c42d4l,0x9fd493ef82feab10l,0x46056b6d82111b45l,
        0xda11dae173efc5c3l },
      { 0xdc7402785545a7fbl,0xbdb2601c40d507e6l,0x121dfeeb7066fa58l,
        0x214369a839ae8c2al } },
    /* 15 << 189 */
    { { 0x195709cb06e0956cl,0x4c9d254f010cd34bl,0xf51e13f70471a532l,
        0xe19d67911e73054dl },
      { 0xf702a628db5c7be3l,0xc7141218b24dde05l,0xdc18233cf29b2e2el,
        0x3a6bd1e885342dbal } },
    /* 16 << 189 */
    { { 0x3f747fa0b311898cl,0xe2a272e4cd0eac65l,0x4bba5851f914d0bcl,
        0x7a1a9660c4a43ee3l },
      { 0xe5a367cea1c8cde9l,0x9d958ba97271abe3l,0xf3ff7eb63d1615cdl,
        0xa2280dcef5ae20b0l } },
    /* 17 << 189 */
    { { 0x56dba5c1cf640147l,0xea5a2e3d5e83d118l,0x04cd6b6dda24c511l,
        0x1c0f4671e854d214l },
      { 0x91a6b7a969565381l,0xdc966240decf1f5bl,0x1b22d21cfcf5d009l,
        0x2a05f6419021dbd5l } },
    /* 18 << 189 */
    { { 0x8c0ed566d4312483l,0x5179a95d643e216fl,0xcc185fec17044493l,
        0xb306333954991a21l },
      { 0xd801ecdb0081a726l,0x0149b0c64fa89bbbl,0xafe9065a4391b6b9l,
        0xedc92786d633f3a3l } },
    /* 19 << 189 */
    { { 0xe408c24aae6a8e13l,0x85833fde9f3897abl,0x43800e7ed81a0715l,
        0xde08e346b44ffc5fl },
      { 0x7094184ccdeff2e0l,0x49f9387b165eaed1l,0x635d6129777c468al,
        0x8c0dcfd1538c2dd8l } },
    /* 20 << 189 */
    { { 0xd6d9d9e37a6a308bl,0x623758304c2767d3l,0x874a8bc6f38cbeb6l,
        0xd94d3f1accb6fd9el },
      { 0x92a9735bba21f248l,0x272ad0e56cd1efb0l,0x7437b69c05b03284l,
        0xe7f047026948c225l } },
    /* 21 << 189 */
    { { 0x8a56c04acba2ececl,0x0c181270e3a73e41l,0x6cb34e9d03e93725l,
        0xf77c8713496521a9l },
      { 0x94569183fa7f9f90l,0xf2e7aa4c8c9707adl,0xced2c9ba26c1c9a3l,
        0x9109fe9640197507l } },
    /* 22 << 189 */
    { { 0x9ae868a9e9adfe1cl,0x3984403d314e39bbl,0xb5875720f2fe378fl,
        0x33f901e0ba44a628l },
      { 0xea1125fe3652438cl,0xae9ec4e69dd1f20bl,0x1e740d9ebebf7fbdl,
        0x6dbd3ddc42dbe79cl } },
    /* 23 << 189 */
    { { 0x62082aecedd36776l,0xf612c478e9859039l,0xa493b201032f7065l,
        0xebd4d8f24ff9b211l },
      { 0x3f23a0aaaac4cb32l,0xea3aadb715ed4005l,0xacf17ea4afa27e63l,
        0x56125c1ac11fd66cl } },
    /* 24 << 189 */
    { { 0x266344a43794f8dcl,0xdcca923a483c5c36l,0x2d6b6bbf3f9d10a0l,
        0xb320c5ca81d9bdf3l },
      { 0x620e28ff47b50a95l,0x933e3b01cef03371l,0xf081bf8599100153l,
        0x183be9a0c3a8c8d6l } },
    /* 25 << 189 */
    { { 0x4e3ddc5ad6bbe24dl,0xc6c7463053843795l,0x78193dd765ec2d4cl,
        0xb8df26cccd3c89b2l },
      { 0x98dbe3995a483f8dl,0x72d8a9577dd3313al,0x65087294ab0bd375l,
        0xfcd892487c259d16l } },
    /* 26 << 189 */
    { { 0x8a9443d77613aa81l,0x8010080085fe6584l,0x70fc4dbc7fb10288l,
        0xf58280d3e86beee8l },
      { 0x14fdd82f7c978c38l,0xdf1204c10de44d7bl,0xa08a1c844160252fl,
        0x591554cac17646a5l } },
    /* 27 << 189 */
    { { 0x214a37d6a05bd525l,0x48d5f09b07957b3cl,0x0247cdcbd7109bc9l,
        0x40f9e4bb30599ce7l },
      { 0xc325fa03f46ad2ecl,0x00f766cfc3e3f9eel,0xab556668d43a4577l,
        0x68d30a613ee03b93l } },
    /* 28 << 189 */
    { { 0x7ddc81ea77b46a08l,0xcf5a6477c7480699l,0x43a8cb346633f683l,
        0x1b867e6b92363c60l },
      { 0x439211141f60558el,0xcdbcdd632f41450el,0x7fc04601cc630e8bl,
        0xea7c66d597038b43l } },
    /* 29 << 189 */
    { { 0x7259b8a504e99fd8l,0x98a8dd124785549al,0x0e459a7c840552e1l,
        0xcdfcf4d04bb0909el },
      { 0x34a86db253758da7l,0xe643bb83eac997e1l,0x96400bd7530c5b7el,
        0x9f97af87b41c8b52l } },
    /* 30 << 189 */
    { { 0x34fc8820fbeee3f9l,0x93e5349049091afdl,0x764b9be59a31f35cl,
        0x71f3786457e3d924l },
      { 0x02fb34e0943aa75el,0xa18c9c58ab8ff6e4l,0x080f31b133cf0d19l,
        0x5c9682db083518a7l } },
    /* 31 << 189 */
    { { 0x873d4ca6b709c3del,0x64a842623575b8f0l,0x6275da1f020154bbl,
        0x97678caad17cf1abl },
      { 0x8779795f951a95c3l,0xdd35b16350fccc08l,0x3270962733d8f031l,
        0x3c5ab10a498dd85cl } },
    /* 32 << 189 */
    { { 0xb6c185c341dca566l,0x7de7fedad8622aa3l,0x99e84d92901b6dfbl,
        0x30a02b0e7c4ad288l },
      { 0xc7c81daa2fd3cf36l,0xd1319547df89e59fl,0xb2be8184cd496733l,
        0xd5f449eb93d3412bl } },
    /* 33 << 189 */
    { { 0x7ea41b1b25fe531dl,0xf97974326a1d5646l,0x86067f722bde501al,
        0xf91481c00c85e89cl },
      { 0xca8ee465f8b05bc6l,0x1844e1cf02e83cdal,0xca82114ab4dbe33bl,
        0x0f9f87694eabfde2l } },
    /* 34 << 189 */
    { { 0x4936b1c038b27fe2l,0x63b6359baba402dfl,0x40c0ea2f656bdbabl,
        0x9c992a896580c39cl },
      { 0x600e8f152a60aed1l,0xeb089ca4e0bf49dfl,0x9c233d7d2d42d99al,
        0x648d3f954c6bc2fal } },
    /* 35 << 189 */
    { { 0xdcc383a8e1add3f3l,0xf42c0c6a4f64a348l,0x2abd176f0030dbdbl,
        0x4de501a37d6c215el },
      { 0x4a107c1f4b9a64bcl,0xa77f0ad32496cd59l,0xfb78ac627688dffbl,
        0x7025a2ca67937d8el } },
    /* 36 << 189 */
    { { 0xfde8b2d1d1a8f4e7l,0xf5b3da477354927cl,0xe48606a3d9205735l,
        0xac477cc6e177b917l },
      { 0xfb1f73d2a883239al,0xe12572f6cc8b8357l,0x9d355e9cfb1f4f86l,
        0x89b795f8d9f3ec6el } },
    /* 37 << 189 */
    { { 0x27be56f1b54398dcl,0x1890efd73fedeed5l,0x62f77f1f9c6d0140l,
        0x7ef0e314596f0ee4l },
      { 0x50ca6631cc61dab3l,0x4a39801df4866e4fl,0x66c8d032ae363b39l,
        0x22c591e52ead66aal } },
    /* 38 << 189 */
    { { 0x954ba308de02a53el,0x2a6c060fd389f357l,0xe6cfcde8fbf40b66l,
        0x8e02fc56c6340ce1l },
      { 0xe495779573adb4bal,0x7b86122ca7b03805l,0x63f835120c8e6fa6l,
        0x83660ea0057d7804l } },
    /* 39 << 189 */
    { { 0xbad7910521ba473cl,0xb6c50beeded5389dl,0xee2caf4daa7c9bc0l,
        0xd97b8de48c4e98a7l },
      { 0xa9f63e70ab3bbddbl,0x3898aabf2597815al,0x7659af89ac15b3d9l,
        0xedf7725b703ce784l } },
    /* 40 << 189 */
    { { 0x25470fabe085116bl,0x04a4337587285310l,0x4e39187ee2bfd52fl,
        0x36166b447d9ebc74l },
      { 0x92ad433cfd4b322cl,0x726aa817ba79ab51l,0xf96eacd8c1db15ebl,
        0xfaf71e910476be63l } },
    /* 41 << 189 */
    { { 0xdd69a640641fad98l,0xb799591829622559l,0x03c6daa5de4199dcl,
        0x92cadc97ad545eb4l },
      { 0x1028238b256534e4l,0x73e80ce68595409al,0x690d4c66d05dc59bl,
        0xc95f7b8f981dee80l } },
    /* 42 << 189 */
    { { 0xf4337014d856ac25l,0x441bd9ddac524dcal,0x640b3d855f0499f5l,
        0x39cf84a9d5fda182l },
      { 0x04e7b055b2aa95a0l,0x29e33f0a0ddf1860l,0x082e74b5423f6b43l,
        0x217edeb90aaa2b0fl } },
    /* 43 << 189 */
    { { 0x58b83f3583cbea55l,0xc485ee4dbc185d70l,0x833ff03b1e5f6992l,
        0xb5b9b9cccf0c0dd5l },
      { 0x7caaee8e4e9e8a50l,0x462e907b6269dafdl,0x6ed5cee9fbe791c6l,
        0x68ca3259ed430790l } },
    /* 44 << 189 */
    { { 0x2b72bdf213b5ba88l,0x60294c8a35ef0ac4l,0x9c3230ed19b99b08l,
        0x560fff176c2589aal },
      { 0x552b8487d6770374l,0xa373202d9a56f685l,0xd3e7f90745f175d9l,
        0x3c2f315fd080d810l } },
    /* 45 << 189 */
    { { 0x1130e9dd7b9520e8l,0xc078f9e20af037b5l,0x38cd2ec71e9c104cl,
        0x0f684368c472fe92l },
      { 0xd3f1b5ed6247e7efl,0xb32d33a9396dfe21l,0x46f59cf44a9aa2c2l,
        0x69cd5168ff0f7e41l } },
    /* 46 << 189 */
    { { 0x3f59da0f4b3234dal,0xcf0b0235b4579ebel,0x6d1cbb256d2476c7l,
        0x4f0837e69dc30f08l },
      { 0x9a4075bb906f6e98l,0x253bb434c761e7d1l,0xde2e645f6e73af10l,
        0xb89a40600c5f131cl } },
    /* 47 << 189 */
    { { 0xd12840c5b8cc037fl,0x3d093a5b7405bb47l,0x6202c253206348b8l,
        0xbf5d57fcc55a3ca7l },
      { 0x89f6c90c8c3bef48l,0x23ac76235a0a960al,0xdfbd3d6b552b42abl,
        0x3ef22458132061f6l } },
    /* 48 << 189 */
    { { 0xd74e9bdac97e6516l,0x88779360c230f49el,0xa6ec1de31e74ea49l,
        0x581dcee53fb645a2l },
      { 0xbaef23918f483f14l,0x6d2dddfcd137d13bl,0x54cde50ed2743a42l,
        0x89a34fc5e4d97e67l } },
    /* 49 << 189 */
    { { 0x13f1f5b312e08ce5l,0xa80540b8a7f0b2cal,0x854bcf7701982805l,
        0xb8653ffd233bea04l },
      { 0x8e7b878702b0b4c9l,0x2675261f9acb170al,0x061a9d90930c14e5l,
        0xb59b30e0def0abeal } },
    /* 50 << 189 */
    { { 0x1dc19ea60200ec7dl,0xb6f4a3f90bce132bl,0xb8d5de90f13e27e0l,
        0xbaee5ef01fade16fl },
      { 0x6f406aaae4c6cf38l,0xab4cfe06d1369815l,0x0dcffe87efd550c6l,
        0x9d4f59c775ff7d39l } },
    /* 51 << 189 */
    { { 0xb02553b151deb6adl,0x812399a4b1877749l,0xce90f71fca6006e1l,
        0xc32363a6b02b6e77l },
      { 0x02284fbedc36c64dl,0x86c81e31a7e1ae61l,0x2576c7e5b909d94al,
        0x8b6f7d02818b2bb0l } },
    /* 52 << 189 */
    { { 0xeca3ed0756faa38al,0xa3790e6c9305bb54l,0xd784eeda7bc73061l,
        0xbd56d3696dd50614l },
      { 0xd6575949229a8aa9l,0xdcca8f474595ec28l,0x814305c106ab4fe6l,
        0xc8c3976824f43f16l } },
    /* 53 << 189 */
    { { 0xe2a45f36523f2b36l,0x995c6493920d93bbl,0xf8afdab790f1632bl,
        0x79ebbecd1c295954l },
      { 0xc7bb3ddb79592f48l,0x67216a7b5f88e998l,0xd91f098bbc01193el,
        0xf7d928a5b1db83fcl } },
    /* 54 << 189 */
    { { 0x55e38417e991f600l,0x2a91113e2981a934l,0xcbc9d64806b13bdel,
        0xb011b6ac0755ff44l },
      { 0x6f4cb518045ec613l,0x522d2d31c2f5930al,0x5acae1af382e65del,
        0x5764306727bc966fl } },
    /* 55 << 189 */
    { { 0x5e12705d1c7193f0l,0xf0f32f473be8858el,0x785c3d7d96c6dfc7l,
        0xd75b4a20bf31795dl },
      { 0x91acf17b342659d4l,0xe596ea3444f0378fl,0x4515708fce52129dl,
        0x17387e1e79f2f585l } },
    /* 56 << 189 */
    { { 0x72cfd2e949dee168l,0x1ae052233e2af239l,0x009e75be1d94066al,
        0x6cca31c738abf413l },
      { 0xb50bd61d9bc49908l,0x4a9b4a8cf5e2bc1el,0xeb6cc5f7946f83acl,
        0x27da93fcebffab28l } },
    /* 57 << 189 */
    { { 0xea314c964821c8c5l,0x8de49deda83c15f4l,0x7a64cf207af33004l,
        0x45f1bfebc9627e10l },
      { 0x878b062654b9df60l,0x5e4fdc3ca95c0b33l,0xe54a37cac2035d8el,
        0x9087cda980f20b8cl } },
    /* 58 << 189 */
    { { 0x36f61c238319ade4l,0x766f287ade8cfdf8l,0x48821948346f3705l,
        0x49a7b85316e4f4a2l },
      { 0xb9b3f8a75cedadfdl,0x8f5628158db2a815l,0xc0b7d55401f68f95l,
        0x12971e27688a208el } },
    /* 59 << 189 */
    { { 0xc9f8b696d0ff34fcl,0x20824de21222718cl,0x7213cf9f0c95284dl,
        0xe2ad741bdc158240l },
      { 0x0ee3a6df54043ccfl,0x16ff479bd84412b3l,0xf6c74ee0dfc98af0l,
        0xa78a169f52fcd2fbl } },
    /* 60 << 189 */
    { { 0xd8ae874699c930e9l,0x1d33e85849e117a5l,0x7581fcb46624759fl,
        0xde50644f5bedc01dl },
      { 0xbeec5d00caf3155el,0x672d66acbc73e75fl,0x86b9d8c6270b01dbl,
        0xd249ef8350f55b79l } },
    /* 61 << 189 */
    { { 0x6131d6d473978fe3l,0xcc4e4542754b00a1l,0x4e05df0557dfcfe9l,
        0x94b29cdd51ef6bf0l },
      { 0xe4530cff9bc7edf2l,0x8ac236fdd3da65f3l,0x0faf7d5fc8eb0b48l,
        0x4d2de14c660eb039l } },
    /* 62 << 189 */
    { { 0xc006bba760430e54l,0x10a2d0d6da3289abl,0x9c037a5dd7979c59l,
        0x04d1f3d3a116d944l },
      { 0x9ff224738a0983cdl,0x28e25b38c883cabbl,0xe968dba547a58995l,
        0x2c80b505774eebdfl } },
    /* 63 << 189 */
    { { 0xee763b714a953bebl,0x502e223f1642e7f6l,0x6fe4b64161d5e722l,
        0x9d37c5b0dbef5316l },
      { 0x0115ed70f8330bc7l,0x139850e675a72789l,0x27d7faecffceccc2l,
        0x3016a8604fd9f7f6l } },
    /* 64 << 189 */
    { { 0xc492ec644cd8f64cl,0x58a2d790279d7b51l,0x0ced1fc51fc75256l,
        0x3e658aed8f433017l },
      { 0x0b61942e05da59ebl,0xba3d60a30ddc3722l,0x7c311cd1742e7f87l,
        0x6473ffeef6b01b6el } },
    /* 0 << 196 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 196 */
    { { 0x8303604f692ac542l,0xf079ffe1227b91d3l,0x19f63e6315aaf9bdl,
        0xf99ee565f1f344fbl },
      { 0x8a1d661fd6219199l,0x8c883bc6d48ce41cl,0x1065118f3c74d904l,
        0x713889ee0faf8b1bl } },
    /* 2 << 196 */
    { { 0x972b3f8f81a1b3bel,0x4f3ce145ce2764a0l,0xe2d0f1cc28c4f5f7l,
        0xdeee0c0dc7f3985bl },
      { 0x7df4adc0d39e25c3l,0x40619820c467a080l,0x440ebc9361cf5a58l,
        0x527729a6422ad600l } },
    /* 3 << 196 */
    { { 0xca6c0937b1b76ba6l,0x1a2eab854d2026dcl,0xb1715e1519d9ae0al,
        0xf1ad9199bac4a026l },
      { 0x35b3dfb807ea7b0el,0xedf5496f3ed9eb89l,0x8932e5ff2d6d08abl,
        0xf314874e25bd2731l } },
    /* 4 << 196 */
    { { 0xefb26a753f73f449l,0x1d1c94f88d44fc79l,0x49f0fbc53bc0dc4dl,
        0xb747ea0b3698a0d0l },
      { 0x5218c3fe228d291el,0x35b804b543c129d6l,0xfac859b8d1acc516l,
        0x6c10697d95d6e668l } },
    /* 5 << 196 */
    { { 0xc38e438f0876fd4el,0x45f0c30783d2f383l,0x203cc2ecb10934cbl,
        0x6a8f24392c9d46eel },
      { 0xf16b431b65ccde7bl,0x41e2cd1827e76a6fl,0xb9c8cf8f4e3484d7l,
        0x64426efd8315244al } },
    /* 6 << 196 */
    { { 0x1c0a8e44fc94dea3l,0x34c8cdbfdad6a0b0l,0x919c384004113cefl,
        0xfd32fba415490ffal },
      { 0x58d190f6795dcfb7l,0xfef01b0383588bafl,0x9e6d1d63ca1fc1c0l,
        0x53173f96f0a41ac9l } },
    /* 7 << 196 */
    { { 0x2b1d402aba16f73bl,0x2fb310148cf9b9fcl,0x2d51e60e446ef7bfl,
        0xc731021bb91e1745l },
      { 0x9d3b47244fee99d4l,0x4bca48b6fac5c1eal,0x70f5f514bbea9af7l,
        0x751f55a5974c283al } },
    /* 8 << 196 */
    { { 0x6e30251acb452fdbl,0x31ee696550f30650l,0xb0b3e508933548d9l,
        0xb8949a4ff4b0ef5bl },
      { 0x208b83263c88f3bdl,0xab147c30db1d9989l,0xed6515fd44d4df03l,
        0x17a12f75e72eb0c5l } },
    /* 9 << 196 */
    { { 0x3b59796d36cf69dbl,0x1219eee956670c18l,0xfe3341f77a070d8el,
        0x9b70130ba327f90cl },
      { 0x36a324620ae18e0el,0x2021a62346c0a638l,0x251b5817c62eb0d4l,
        0x87bfbcdf4c762293l } },
    /* 10 << 196 */
    { { 0xf78ab505cdd61d64l,0x8c7a53fcc8c18857l,0xa653ce6f16147515l,
        0x9c923aa5ea7d52d5l },
      { 0xc24709cb5c18871fl,0x7d53bec873b3cc74l,0x59264afffdd1d4c4l,
        0x5555917e240da582l } },
    /* 11 << 196 */
    { { 0xcae8bbda548f5a0el,0x1910eaba3bbfbbe1l,0xae5796857677afc3l,
        0x49ea61f173ff0b5cl },
      { 0x786554784f7c3922l,0x95d337cd20c68eefl,0x68f1e1e5df779ab9l,
        0x14b491b0b5cf69a8l } },
    /* 12 << 196 */
    { { 0x7a6cbbe028e3fe89l,0xe7e1fee4c5aac0ebl,0x7f47eda5697e5140l,
        0x4f450137b454921fl },
      { 0xdb625f8495cd8185l,0x74be0ba1cdb2e583l,0xaee4fd7cdd5e6de4l,
        0x4251437de8101739l } },
    /* 13 << 196 */
    { { 0x686d72a0ac620366l,0x4be3fb9cb6d59344l,0x6e8b44e7a1eb75b9l,
        0x84e39da391a5c10cl },
      { 0x37cc1490b38f0409l,0x029519432c2ade82l,0x9b6887831190a2d8l,
        0x25627d14231182bal } },
    /* 14 << 196 */
    { { 0x6eb550aa658a6d87l,0x1405aaa7cf9c7325l,0xd147142e5c8748c9l,
        0x7f637e4f53ede0e0l },
      { 0xf8ca277614ffad2cl,0xe58fb1bdbafb6791l,0x17158c23bf8f93fcl,
        0x7f15b3730a4a4655l } },
    /* 15 << 196 */
    { { 0x39d4add2d842ca72l,0xa71e43913ed96305l,0x5bb09cbe6700be14l,
        0x68d69d54d8befcf6l },
      { 0xa45f536737183bcfl,0x7152b7bb3370dff7l,0xcf887baabf12525bl,
        0xe7ac7bddd6d1e3cdl } },
    /* 16 << 196 */
    { { 0x25914f7881fdad90l,0xcf638f560d2cf6abl,0xb90bc03fcc054de5l,
        0x932811a718b06350l },
      { 0x2f00b3309bbd11ffl,0x76108a6fb4044974l,0x801bb9e0a851d266l,
        0x0dd099bebf8990c1l } },
    /* 17 << 196 */
    { { 0x58c5aaaaabe32986l,0x0fe9dd2a50d59c27l,0x84951ff48d307305l,
        0x6c23f82986529b78l },
      { 0x50bb22180b136a79l,0x7e2174de77a20996l,0x6f00a4b9c0bb4da6l,
        0x89a25a17efdde8dal } },
    /* 18 << 196 */
    { { 0xf728a27ec11ee01dl,0xf900553ae5f10dfbl,0x189a83c802ec893cl,
        0x3ca5bdc123f66d77l },
      { 0x9878153797eada9fl,0x59c50ab310256230l,0x346042d9323c69b3l,
        0x1b715a6d2c460449l } },
    /* 19 << 196 */
    { { 0xa41dd4766ae06e0bl,0xcdd7888e9d42e25fl,0x0f395f7456b25a20l,
        0xeadfe0ae8700e27el },
      { 0xb09d52a969950093l,0x3525d9cb327f8d40l,0xb8235a9467df886al,
        0x77e4b0dd035faec2l } },
    /* 20 << 196 */
    { { 0x115eb20a517d7061l,0x77fe34336c2df683l,0x6870ddc7cdc6fc67l,
        0xb16105880b87de83l },
      { 0x343584cad9c4ddbel,0xb3164f1c3d754be2l,0x0731ed3ac1e6c894l,
        0x26327dec4f6b904cl } },
    /* 21 << 196 */
    { { 0x9d49c6de97b5cd32l,0x40835daeb5eceecdl,0xc66350edd9ded7fel,
        0x8aeebb5c7a678804l },
      { 0x51d42fb75b8ee9ecl,0xd7a17bdd8e3ca118l,0x40d7511a2ef4400el,
        0xc48990ac875a66f4l } },
    /* 22 << 196 */
    { { 0x8de07d2a2199e347l,0xbee755562a39e051l,0x56918786916e51dcl,
        0xeb1913134a2d89ecl },
      { 0x6679610d37d341edl,0x434fbb4156d51c2bl,0xe54b7ee7d7492dbal,
        0xaa33a79a59021493l } },
    /* 23 << 196 */
    { { 0x49fc5054e4bd6d3dl,0x09540f045ab551d0l,0x8acc90854942d3a6l,
        0x231af02f2d28323bl },
      { 0x93458cac0992c163l,0x1fef8e71888e3bb4l,0x27578da5be8c268cl,
        0xcc8be792e805ec00l } },
    /* 24 << 196 */
    { { 0x29267baec61c3855l,0xebff429d58c1fd3bl,0x22d886c08c0b93b8l,
        0xca5e00b22ddb8953l },
      { 0xcf330117c3fed8b7l,0xd49ac6fa819c01f6l,0x6ddaa6bd3c0fbd54l,
        0x917430688049a2cfl } },
    /* 25 << 196 */
    { { 0xd67f981eaff2ef81l,0xc3654d352818ae80l,0x81d050441b2aa892l,
        0x2db067bf3d099328l },
      { 0xe7c79e86703dcc97l,0xe66f9b37e133e215l,0xcdf119a6e39a7a5cl,
        0x47c60de3876f1b61l } },
    /* 26 << 196 */
    { { 0x6e405939d860f1b2l,0x3e9a1dbcf5ed4d4al,0x3f23619ec9b6bcbdl,
        0x5ee790cf734e4497l },
      { 0xf0a834b15bdaf9bbl,0x02cedda74ca295f0l,0x4619aa2bcb8e378cl,
        0xe5613244cc987ea4l } },
    /* 27 << 196 */
    { { 0x0bc022cc76b23a50l,0x4a2793ad0a6c21cel,0x3832878089cac3f5l,
        0x29176f1bcba26d56l },
      { 0x062961874f6f59ebl,0x86e9bca98bdc658el,0x2ca9c4d357e30402l,
        0x5438b216516a09bbl } },
    /* 28 << 196 */
    { { 0x0a6a063c7672765al,0x37a3ce640547b9bfl,0x42c099c898b1a633l,
        0xb5ab800d05ee6961l },
      { 0xf1963f5911a5acd6l,0xbaee615746201063l,0x36d9a649a596210al,
        0xaed043631ba7138cl } },
    /* 29 << 196 */
    { { 0xcf817d1ca4a82b76l,0x5586960ef3806be9l,0x7ab67c8909dc6bb5l,
        0x52ace7a0114fe7ebl },
      { 0xcd987618cbbc9b70l,0x4f06fd5a604ca5e1l,0x90af14ca6dbde133l,
        0x1afe4322948a3264l } },
    /* 30 << 196 */
    { { 0xa70d2ca6c44b2c6cl,0xab7267990ef87dfel,0x310f64dc2e696377l,
        0x49b42e684c8126a0l },
      { 0x0ea444c3cea0b176l,0x53a8ddf7cb269182l,0xf3e674ebbbba9dcbl,
        0x0d2878a8d8669d33l } },
    /* 31 << 196 */
    { { 0x04b935d5d019b6a3l,0xbb5cf88e406f1e46l,0xa1912d165b57c111l,
        0x9803fc2119ebfd78l },
      { 0x4f231c9ec07764a9l,0xd93286eeb75bd055l,0x83a9457d8ee6c9del,
        0x046959156087ec90l } },
    /* 32 << 196 */
    { { 0x14c6dd8a58d6cd46l,0x9cb633b58e6634d2l,0xc1305047f81bc328l,
        0x12ede0e226a177e5l },
      { 0x332cca62065a6f4fl,0xc3a47ecd67be487bl,0x741eb1870f47ed1cl,
        0x99e66e58e7598b14l } },
    /* 33 << 196 */
    { { 0x6f0544ca63d0ff12l,0xe5efc784b610a05fl,0xf72917b17cad7b47l,
        0x3ff6ea20f2cac0c0l },
      { 0xcc23791bf21db8b7l,0x7dac70b1d7d93565l,0x682cda1d694bdaadl,
        0xeb88bb8c1023516dl } },
    /* 34 << 196 */
    { { 0xc4c634b4dfdbeb1bl,0x22f5ca72b4ee4deal,0x1045a368e6524821l,
        0xed9e8a3f052b18b2l },
      { 0x9b7f2cb1b961f49al,0x7fee2ec17b009670l,0x350d875422507a6dl,
        0x561bd7114db55f1dl } },
    /* 35 << 196 */
    { { 0x4c189ccc320bbcafl,0x568434cfdf1de48cl,0x6af1b00e0fa8f128l,
        0xf0ba9d028907583cl },
      { 0x735a400432ff9f60l,0x3dd8e4b6c25dcf33l,0xf2230f1642c74cefl,
        0xd8117623013fa8adl } },
    /* 36 << 196 */
    { { 0x36822876f51fe76el,0x8a6811cc11d62589l,0xc3fc7e6546225718l,
        0xb7df2c9fc82fdbcdl },
      { 0x3b1d4e52dd7b205bl,0xb695947847a2e414l,0x05e4d793efa91148l,
        0xb47ed446fd2e9675l } },
    /* 37 << 196 */
    { { 0x1a7098b904c9d9bfl,0x661e28811b793048l,0xb1a16966b01ee461l,
        0xbc5213082954746fl },
      { 0xc909a0fc2477de50l,0xd80bb41c7dbd51efl,0xa85be7ec53294905l,
        0x6d465b1883958f97l } },
    /* 38 << 196 */
    { { 0x16f6f330fb6840fdl,0xfaaeb2143401e6c8l,0xaf83d30fccb5b4f8l,
        0x22885739266dec4bl },
      { 0x51b4367c7bc467dfl,0x926562e3d842d27al,0xdfcb66140fea14a6l,
        0xeb394daef2734cd9l } },
    /* 39 << 196 */
    { { 0x3eeae5d211c0be98l,0xb1e6ed11814e8165l,0x191086bce52bce1cl,
        0x14b74cc6a75a04dal },
      { 0x63cf11868c060985l,0x071047de2dbd7f7cl,0x4e433b8bce0942cal,
        0xecbac447d8fec61dl } },
    /* 40 << 196 */
    { { 0x8f0ed0e2ebf3232fl,0xfff80f9ec52a2eddl,0xad9ab43375b55fdbl,
        0x73ca7820e42e0c11l },
      { 0x6dace0a0e6251b46l,0x89bc6b5c4c0d932dl,0x3438cd77095da19al,
        0x2f24a9398d48bdfbl } },
    /* 41 << 196 */
    { { 0x99b47e46766561b7l,0x736600e60ed0322al,0x06a47cb1638e1865l,
        0x927c1c2dcb136000l },
      { 0x295423370cc5df69l,0x99b37c0209d649a9l,0xc5f0043c6aefdb27l,
        0x6cdd99871be95c27l } },
    /* 42 << 196 */
    { { 0x69850931390420d2l,0x299c40ac0983efa4l,0x3a05e778af39aeadl,
        0x8427440843a45193l },
      { 0x6bcd0fb991a711a0l,0x461592c89f52ab17l,0xb49302b4da3c6ed6l,
        0xc51fddc7330d7067l } },
    /* 43 << 196 */
    { { 0x94babeb6da50d531l,0x521b840da6a7b9dal,0x5305151e404bdc89l,
        0x1bcde201d0d07449l },
      { 0xf427a78b3b76a59al,0xf84841ce07791a1bl,0xebd314bebf91ed1cl,
        0x8e61d34cbf172943l } },
    /* 44 << 196 */
    { { 0x1d5dc4515541b892l,0xb186ee41fc9d9e54l,0x9d9f345ed5bf610dl,
        0x3e7ba65df6acca9fl },
      { 0x9dda787aa8369486l,0x09f9dab78eb5ba53l,0x5afb2033d6481bc3l,
        0x76f4ce30afa62104l } },
    /* 45 << 196 */
    { { 0xa8fa00cff4f066b5l,0x89ab5143461dafc2l,0x44339ed7a3389998l,
        0x2ff862f1bc214903l },
      { 0x2c88f985b05556e3l,0xcd96058e3467081el,0x7d6a4176edc637eal,
        0xe1743d0936a5acdcl } },
    /* 46 << 196 */
    { { 0x66fd72e27eb37726l,0xf7fa264e1481a037l,0x9fbd3bde45f4aa79l,
        0xed1e0147767c3e22l },
      { 0x7621f97982e7abe2l,0x19eedc7245f633f8l,0xe69b155e6137bf3al,
        0xa0ad13ce414ee94el } },
    /* 47 << 196 */
    { { 0x93e3d5241c0e651al,0xab1a6e2a02ce227el,0xe7af17974ab27ecal,
        0x245446debd444f39l },
      { 0x59e22a2156c07613l,0x43deafcef4275498l,0x10834ccb67fd0946l,
        0xa75841e547406edfl } },
    /* 48 << 196 */
    { { 0xebd6a6777b0ac93dl,0xa6e37b0d78f5e0d7l,0x2516c09676f5492bl,
        0x1e4bf8889ac05f3al },
      { 0xcdb42ce04df0ba2bl,0x935d5cfd5062341bl,0x8a30333382acac20l,
        0x429438c45198b00el } },
    /* 49 << 196 */
    { { 0x1d083bc9049d33fal,0x58b82dda946f67ffl,0xac3e2db867a1d6a3l,
        0x62e6bead1798aac8l },
      { 0xfc85980fde46c58cl,0xa7f6937969c8d7bel,0x23557927837b35ecl,
        0x06a933d8e0790c0cl } },
    /* 50 << 196 */
    { { 0x827c0e9b077ff55dl,0x53977798bb26e680l,0x595308741d9cb54fl,
        0xcca3f4494aac53efl },
      { 0x11dc5c87a07eda0fl,0xc138bccffd6400c8l,0x549680d313e5da72l,
        0xc93eed824540617el } },
    /* 51 << 196 */
    { { 0xfd3db1574d0b75c0l,0x9716eb426386075bl,0x0639605c817b2c16l,
        0x09915109f1e4f201l },
      { 0x35c9a9285cca6c3bl,0xb25f7d1a3505c900l,0xeb9f7d20630480c4l,
        0xc3c7b8c62a1a501cl } },
    /* 52 << 196 */
    { { 0x3f99183c5a1f8e24l,0xfdb118fa9dd255f0l,0xb9b18b90c27f62a6l,
        0xe8f732f7396ec191l },
      { 0x524a2d910be786abl,0x5d32adef0ac5a0f5l,0x9b53d4d69725f694l,
        0x032a76c60510ba89l } },
    /* 53 << 196 */
    { { 0x840391a3ebeb1544l,0x44b7b88c3ed73ac3l,0xd24bae7a256cb8b3l,
        0x7ceb151ae394cb12l },
      { 0xbd6b66d05bc1e6a8l,0xec70cecb090f07bfl,0x270644ed7d937589l,
        0xee9e1a3d5f1dccfel } },
    /* 54 << 196 */
    { { 0xb0d40a84745b98d2l,0xda429a212556ed40l,0xf676eced85148cb9l,
        0x5a22d40cded18936l },
      { 0x3bc4b9e570e8a4cel,0xbfd1445b9eae0379l,0xf23f2c0c1a0bd47el,
        0xa9c0bb31e1845531l } },
    /* 55 << 196 */
    { { 0x9ddc4d600a4c3f6bl,0xbdfaad792c15ef44l,0xce55a2367f484accl,
        0x08653ca7055b1f15l },
      { 0x2efa8724538873a3l,0x09299e5dace1c7e7l,0x07afab66ade332bal,
        0x9be1fdf692dd71b7l } },
    /* 56 << 196 */
    { { 0xa49b5d595758b11cl,0x0b852893c8654f40l,0xb63ef6f452379447l,
        0xd4957d29105e690cl },
      { 0x7d484363646559b0l,0xf4a8273c49788a8el,0xee406cb834ce54a9l,
        0x1e1c260ff86fda9bl } },
    /* 57 << 196 */
    { { 0xe150e228cf6a4a81l,0x1fa3b6a31b488772l,0x1e6ff110c5a9c15bl,
        0xc6133b918ad6aa47l },
      { 0x8ac5d55c9dffa978l,0xba1d1c1d5f3965f2l,0xf969f4e07732b52fl,
        0xfceecdb5a5172a07l } },
    /* 58 << 196 */
    { { 0xb0120a5f10f2b8f5l,0xc83a6cdf5c4c2f63l,0x4d47a491f8f9c213l,
        0xd9e1cce5d3f1bbd5l },
      { 0x0d91bc7caba7e372l,0xfcdc74c8dfd1a2dbl,0x05efa800374618e5l,
        0x1121696915a7925el } },
    /* 59 << 196 */
    { { 0xd4c89823f6021c5dl,0x880d5e84eff14423l,0x6523bc5a6dcd1396l,
        0xd1acfdfc113c978bl },
      { 0xb0c164e8bbb66840l,0xf7f4301e72b58459l,0xc29ad4a6a638e8ecl,
        0xf5ab896146b78699l } },
    /* 60 << 196 */
    { { 0x9dbd79740e954750l,0x0121de8864f9d2c6l,0x2e597b42d985232el,
        0x55b6c3c553451777l },
      { 0xbb53e547519cb9fbl,0xf134019f8428600dl,0x5a473176e081791al,
        0x2f3e226335fb0c08l } },
    /* 61 << 196 */
    { { 0xb28c301773d273b0l,0xccd210767721ef9al,0x054cc292b650dc39l,
        0x662246de6188045el },
      { 0x904b52fa6b83c0d1l,0xa72df26797e9cd46l,0x886b43cd899725e4l,
        0x2b651688d849ff22l } },
    /* 62 << 196 */
    { { 0x60479b7902f34533l,0x5e354c140c77c148l,0xb4bb7581a8537c78l,
        0x188043d7efe1495fl },
      { 0x9ba12f428c1d5026l,0x2e0c8a2693d4aaabl,0xbdba7b8baa57c450l,
        0x140c9ad69bbdafefl } },
    /* 63 << 196 */
    { { 0x2067aa4225ac0f18l,0xf7b1295b04d1fbf3l,0x14829111a4b04824l,
        0x2ce3f19233bd5e91l },
      { 0x9c7a1d558f2e1b72l,0xfe932286302aa243l,0x497ca7b4d4be9554l,
        0xb8e821b8e0547a6el } },
    /* 64 << 196 */
    { { 0xfb2838be67e573e0l,0x05891db94084c44bl,0x9131137396c1c2c5l,
        0x6aebfa3fd958444bl },
      { 0xac9cdce9e56e55c1l,0x7148ced32caa46d0l,0x2e10c7efb61fe8ebl,
        0x9fd835daff97cf4dl } },
    /* 0 << 203 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 203 */
    { { 0xa36da109081e9387l,0xfb9780d78c935828l,0xd5940332e540b015l,
        0xc9d7b51be0f466fal },
      { 0xfaadcd41d6d9f671l,0xba6c1e28b1a2ac17l,0x066a7833ed201e5fl,
        0x19d99719f90f462bl } },
    /* 2 << 203 */
    { { 0xf431f462060b5f61l,0xa56f46b47bd057c2l,0x348dca6c47e1bf65l,
        0x9a38783e41bcf1ffl },
      { 0x7a5d33a9da710718l,0x5a7799872e0aeaf6l,0xca87314d2d29d187l,
        0xfa0edc3ec687d733l } },
    /* 3 << 203 */
    { { 0x9df336216a31e09bl,0xde89e44dc1350e35l,0x292148714ca0cf52l,
        0xdf3796720b88a538l },
      { 0xc92a510a2591d61bl,0x79aa87d7585b447bl,0xf67db604e5287f77l,
        0x1697c8bf5efe7a80l } },
    /* 4 << 203 */
    { { 0x1c894849cb198ac7l,0xa884a93d0f264665l,0x2da964ef9b200678l,
        0x3c351b87009834e6l },
      { 0xafb2ef9fe2c4b44bl,0x580f6c473326790cl,0xb84805210b02264al,
        0x8ba6f9e242a194e2l } },
    /* 5 << 203 */
    { { 0xfc87975f8fb54738l,0x3516078827c3ead3l,0x834116d2b74a085al,
        0x53c99a73a62fe996l },
      { 0x87585be05b81c51bl,0x925bafa8be0852b7l,0x76a4fafda84d19a7l,
        0x39a45982585206d4l } },
    /* 6 << 203 */
    { { 0x499b6ab65eb03c0el,0xf19b795472bc3fdel,0xa86b5b9c6e3a80d2l,
        0xe43775086d42819fl },
      { 0xc1663650bb3ee8a3l,0x75eb14fcb132075fl,0xa8ccc9067ad834f6l,
        0xea6a2474e6e92ffdl } },
    /* 7 << 203 */
    { { 0x9d72fd950f8d6758l,0xcb84e101408c07ddl,0xb9114bfda5e23221l,
        0x358b5fe2e94e742cl },
      { 0x1c0577ec95f40e75l,0xf01554513d73f3d6l,0x9d55cd67bd1b9b66l,
        0x63e86e78af8d63c7l } },
    /* 8 << 203 */
    { { 0x39d934abd3c095f1l,0x04b261bee4b76d71l,0x1d2e6970e73e6984l,
        0x879fb23b5e5fcb11l },
      { 0x11506c72dfd75490l,0x3a97d08561bcf1c1l,0x43201d82bf5e7007l,
        0x7f0ac52f798232a7l } },
    /* 9 << 203 */
    { { 0x2715cbc46eb564d4l,0x8d6c752c9e570e29l,0xf80247c89ef5fd5dl,
        0xc3c66b46d53eb514l },
      { 0x9666b4010f87de56l,0xce62c06fc6c603b5l,0xae7b4c607e4fc942l,
        0x38ac0b77663a9c19l } },
    /* 10 << 203 */
    { { 0xcb4d20ee4b049136l,0x8b63bf12356a4613l,0x1221aef670e08128l,
        0xe62d8c514acb6b16l },
      { 0x71f64a67379e7896l,0xb25237a2cafd7fa5l,0xf077bd983841ba6al,
        0xc4ac02443cd16e7el } },
    /* 11 << 203 */
    { { 0x548ba86921fea4cal,0xd36d0817f3dfdac1l,0x09d8d71ff4685fafl,
        0x8eff66bec52c459al },
      { 0x182faee70b57235el,0xee3c39b10106712bl,0x5107331fc0fcdcb0l,
        0x669fb9dca51054bal } },
    /* 12 << 203 */
    { { 0xb25101fb319d7682l,0xb02931290a982feel,0x51c1c9b90261b344l,
        0x0e008c5bbfd371fal },
      { 0xd866dd1c0278ca33l,0x666f76a6e5aa53b1l,0xe5cfb7796013a2cfl,
        0x1d3a1aada3521836l } },
    /* 13 << 203 */
    { { 0xcedd253173faa485l,0xc8ee6c4fc0a76878l,0xddbccfc92a11667dl,
        0x1a418ea91c2f695al },
      { 0xdb11bd9251f73971l,0x3e4b3c82da2ed89fl,0x9a44f3f4e73e0319l,
        0xd1e3de0f303431afl } },
    /* 14 << 203 */
    { { 0x3c5604ff50f75f9cl,0x1d8eddf37e752b22l,0x0ef074dd3c9a1118l,
        0xd0ffc172ccb86d7bl },
      { 0xabd1ece3037d90f2l,0xe3f307d66055856cl,0x422f93287e4c6dafl,
        0x902aac66334879a0l } },
    /* 15 << 203 */
    { { 0xb6a1e7bf94cdfadel,0x6c97e1ed7fc6d634l,0x662ad24da2fb63f8l,
        0xf81be1b9a5928405l },
      { 0x86d765e4d14b4206l,0xbecc2e0e8fa0db65l,0xa28838e0b17fc76cl,
        0xe49a602ae37cf24el } },
    /* 16 << 203 */
    { { 0x76b4131a567193ecl,0xaf3c305ae5f6e70bl,0x9587bd39031eebddl,
        0x5709def871bbe831l },
      { 0x570599830eb2b669l,0x4d80ce1b875b7029l,0x838a7da80364ac16l,
        0x2f431d23be1c83abl } },
    /* 17 << 203 */
    { { 0xe56812a6f9294dd3l,0xb448d01f9b4b0d77l,0xf3ae606104e8305cl,
        0x2bead64594d8c63el },
      { 0x0a85434d84fd8b07l,0x537b983ff7a9dee5l,0xedcc5f18ef55bd85l,
        0x2041af6221c6cf8bl } },
    /* 18 << 203 */
    { { 0x8e52874cb940c71el,0x211935a9db5f4b3al,0x94350492301b1dc3l,
        0x33d2646d29958620l },
      { 0x16b0d64bef911404l,0x9d1f25ea9a3c5ef4l,0x20f200eb4a352c78l,
        0x43929f2c4bd0b428l } },
    /* 19 << 203 */
    { { 0xa5656667c7196e29l,0x7992c2f09391be48l,0xaaa97cbd9ee0cd6el,
        0x51b0310c3dc8c9bfl },
      { 0x237f8acfdd9f22cbl,0xbb1d81a1b585d584l,0x8d5d85f58c416388l,
        0x0d6e5a5a42fe474fl } },
    /* 20 << 203 */
    { { 0xe781276638235d4el,0x1c62bd67496e3298l,0x8378660c3f175bc8l,
        0x4d04e18917afdd4dl },
      { 0x32a8160185a8068cl,0xdb58e4e192b29a85l,0xe8a65b86c70d8a3bl,
        0x5f0e6f4e98a0403bl } },
    /* 21 << 203 */
    { { 0x0812968469ed2370l,0x34dc30bd0871ee26l,0x3a5ce9487c9c5b05l,
        0x7d487b8043a90c87l },
      { 0x4089ba37dd0e7179l,0x45f80191b4041811l,0x1c3e105898747ba5l,
        0x98c4e13a6e1ae592l } },
    /* 22 << 203 */
    { { 0xd44636e6e82c9f9el,0x711db87cc33a1043l,0x6f431263aa8aec05l,
        0x43ff120d2744a4aal },
      { 0xd3bd892fae77779bl,0xf0fe0cc98cdc9f82l,0xca5f7fe6f1c5b1bcl,
        0xcc63a68244929a72l } },
    /* 23 << 203 */
    { { 0xc7eaba0c09dbe19al,0x2f3585ad6b5c73c2l,0x8ab8924b0ae50c30l,
        0x17fcd27a638b30bal },
      { 0xaf414d3410b3d5a5l,0x09c107d22a9accf1l,0x15dac49f946a6242l,
        0xaec3df2ad707d642l } },
    /* 24 << 203 */
    { { 0x2c2492b73f894ae0l,0xf59df3e5b75f18cel,0x7cb740d28f53cad0l,
        0x3eb585fbc4f01294l },
      { 0x17da0c8632c7f717l,0xeb8c795baf943f4cl,0x4ee23fb5f67c51d2l,
        0xef18757568889949l } },
    /* 25 << 203 */
    { { 0xa6b4bdb20389168bl,0xc4ecd258ea577d03l,0x3a63782b55743082l,
        0x6f678f4cc72f08cdl },
      { 0x553511cf65e58dd8l,0xd53b4e3ed402c0cdl,0x37de3e29a037c14cl,
        0x86b6c516c05712aal } },
    /* 26 << 203 */
    { { 0x2834da3eb38dff6fl,0xbe012c52ea636be8l,0x292d238c61dd37f8l,
        0x0e54523f8f8142dbl },
      { 0xe31eb436036a05d8l,0x83e3cdff1e93c0ffl,0x3fd2fe0f50821ddfl,
        0xc8e19b0dff9eb33bl } },
    /* 27 << 203 */
    { { 0xc8cc943fb569a5fel,0xad0090d4d4342d75l,0x82090b4bcaeca000l,
        0xca39687f1bd410ebl },
      { 0xe7bb0df765959d77l,0x39d782189c964999l,0xd87f62e8b2415451l,
        0xe5efb774bed76108l } },
    /* 28 << 203 */
    { { 0x3ea011a4e822f0d0l,0xbc647ad15a8704f8l,0xbb315b3550c6820fl,
        0x863dec3db7e76becl },
      { 0x01ff5d3af017bfc7l,0x20054439976b8229l,0x067fca370bbd0d3bl,
        0xf63dde647f5e3d0fl } },
    /* 29 << 203 */
    { { 0x22dbefb32a4c94e9l,0xafbff0fe96f8278al,0x80aea0b13503793dl,
        0xb22380295f06cd29l },
      { 0x65703e578ec3fecal,0x06c38314393e7053l,0xa0b751eb7c6734c4l,
        0xd2e8a435c59f0f1el } },
    /* 30 << 203 */
    { { 0x147d90525e9ca895l,0x2f4dd31e972072dfl,0xa16fda8ee6c6755cl,
        0xc66826ffcf196558l },
      { 0x1f1a76a30cf43895l,0xa9d604e083c3097bl,0xe190830966390e0el,
        0xa50bf753b3c85effl } },
    /* 31 << 203 */
    { { 0x0696bddef6a70251l,0x548b801b3c6ab16al,0x37fcf704a4d08762l,
        0x090b3defdff76c4el },
      { 0x87e8cb8969cb9158l,0x44a90744995ece43l,0xf85395f40ad9fbf5l,
        0x49b0f6c54fb0c82dl } },
    /* 32 << 203 */
    { { 0x75d9bc15adf7cccfl,0x81a3e5d6dfa1e1b0l,0x8c39e444249bc17el,
        0xf37dccb28ea7fd43l },
      { 0xda654873907fba12l,0x35daa6da4a372904l,0x0564cfc66283a6c5l,
        0xd09fa4f64a9395bfl } },
    /* 33 << 203 */
    { { 0x688e9ec9aeb19a36l,0xd913f1cec7bfbfb4l,0x797b9a3c61c2faa6l,
        0x2f979bec6a0a9c12l },
      { 0xb5969d0f359679ecl,0xebcf523d079b0460l,0xfd6b000810fab870l,
        0x3f2edcda9373a39cl } },
    /* 34 << 203 */
    { { 0x0d64f9a76f568431l,0xf848c27c02f8898cl,0xf418ade1260b5bd5l,
        0xc1f3e3236973dee8l },
      { 0x46e9319c26c185ddl,0x6d85b7d8546f0ac4l,0x427965f2247f9d57l,
        0xb519b636b0035f48l } },
    /* 35 << 203 */
    { { 0x6b6163a9ab87d59cl,0xff9f58c339caaa11l,0x4ac39cde3177387bl,
        0x5f6557c2873e77f9l },
      { 0x6750400636a83041l,0x9b1c96ca75ef196cl,0xf34283deb08c7940l,
        0x7ea096441128c316l } },
    /* 36 << 203 */
    { { 0xb510b3b56aa39dffl,0x59b43da29f8e4d8cl,0xa8ce31fd9e4c4b9fl,
        0x0e20be26c1303c01l },
      { 0x18187182e8ee47c9l,0xd9687cdb7db98101l,0x7a520e4da1e14ff6l,
        0x429808ba8836d572l } },
    /* 37 << 203 */
    { { 0xa37ca60d4944b663l,0xf901f7a9a3f91ae5l,0xe4e3e76e9e36e3b1l,
        0x9aa219cf29d93250l },
      { 0x347fe275056a2512l,0xa4d643d9de65d95cl,0x9669d396699fc3edl,
        0xb598dee2cf8c6bbel } },
    /* 38 << 203 */
    { { 0x682ac1e5dda9e5c6l,0x4e0d3c72caa9fc95l,0x17faaade772bea44l,
        0x5ef8428cab0009c8l },
      { 0xcc4ce47a460ff016l,0xda6d12bf725281cbl,0x44c678480223aad2l,
        0x6e342afa36256e28l } },
    /* 39 << 203 */
    { { 0x1400bb0b93a37c04l,0x62b1bc9bdd10bd96l,0x7251adeb0dac46b7l,
        0x7d33b92e7be4ef51l },
      { 0x28b2a94be61fa29al,0x4b2be13f06422233l,0x36d6d062330d8d37l,
        0x5ef80e1eb28ca005l } },
    /* 40 << 203 */
    { { 0x174d46996d16768el,0x9fc4ff6a628bf217l,0x77705a94154e490dl,
        0x9d96dd288d2d997al },
      { 0x77e2d9d8ce5d72c4l,0x9d06c5a4c11c714fl,0x02aa513679e4a03el,
        0x1386b3c2030ff28bl } },
    /* 41 << 203 */
    { { 0xfe82e8a6fb283f61l,0x7df203e5f3abc3fbl,0xeec7c3513a4d3622l,
        0xf7d17dbfdf762761l },
      { 0xc3956e44522055f0l,0xde3012db8fa748dbl,0xca9fcb63bf1dcc14l,
        0xa56d9dcfbe4e2f3al } },
    /* 42 << 203 */
    { { 0xb86186b68bcec9c2l,0x7cf24df9680b9f06l,0xc46b45eac0d29281l,
        0xfff42bc507b10e12l },
      { 0x12263c404d289427l,0x3d5f1899b4848ec4l,0x11f97010d040800cl,
        0xb4c5f529300feb20l } },
    /* 43 << 203 */
    { { 0xcc543f8fde94fdcbl,0xe96af739c7c2f05el,0xaa5e0036882692e1l,
        0x09c75b68950d4ae9l },
      { 0x62f63df2b5932a7al,0x2658252ede0979adl,0x2a19343fb5e69631l,
        0x718c7501525b666bl } },
    /* 44 << 203 */
    { { 0x26a42d69ea40dc3al,0xdc84ad22aecc018fl,0x25c36c7b3270f04al,
        0x46ba6d4750fa72edl },
      { 0x6c37d1c593e58a8el,0xa2394731120c088cl,0xc3be4263cb6e86dal,
        0x2c417d367126d038l } },
    /* 45 << 203 */
    { { 0x5b70f9c58b6f8efal,0x671a2faa37718536l,0xd3ced3c6b539c92bl,
        0xe56f1bd9a31203c2l },
      { 0x8b096ec49ff3c8ebl,0x2deae43243491ceal,0x2465c6eb17943794l,
        0x5d267e6620586843l } },
    /* 46 << 203 */
    { { 0x9d3d116db07159d0l,0xae07a67fc1896210l,0x8fc84d87bb961579l,
        0x30009e491c1f8dd6l },
      { 0x8a8caf22e3132819l,0xcffa197cf23ab4ffl,0x58103a44205dd687l,
        0x57b796c30ded67a2l } },
    /* 47 << 203 */
    { { 0x0b9c3a6ca1779ad7l,0xa33cfe2e357c09c5l,0x2ea293153db4a57el,
        0x919596958ebeb52el },
      { 0x118db9a6e546c879l,0x8e996df46295c8d6l,0xdd99048455ec806bl,
        0x24f291ca165c1035l } },
    /* 48 << 203 */
    { { 0xcca523bb440e2229l,0x324673a273ef4d04l,0xaf3adf343e11ec39l,
        0x6136d7f1dc5968d3l },
      { 0x7a7b2899b053a927l,0x3eaa2661ae067ecdl,0x8549b9c802779cd9l,
        0x061d7940c53385eal } },
    /* 49 << 203 */
    { { 0x3e0ba883f06d18bdl,0x4ba6de53b2700843l,0xb966b668591a9e4dl,
        0x93f675677f4fa0edl },
      { 0x5a02711b4347237bl,0xbc041e2fe794608el,0x55af10f570f73d8cl,
        0xd2d4d4f7bb7564f7l } },
    /* 50 << 203 */
    { { 0xd7d27a89b3e93ce7l,0xf7b5a8755d3a2c1bl,0xb29e68a0255b218al,
        0xb533837e8af76754l },
      { 0xd1b05a73579fab2el,0xb41055a1ecd74385l,0xb2369274445e9115l,
        0x2972a7c4f520274el } },
    /* 51 << 203 */
    { { 0x6c08334ef678e68al,0x4e4160f099b057edl,0x3cfe11b852ccb69al,
        0x2fd1823a21c8f772l },
      { 0xdf7f072f3298f055l,0x8c0566f9fec74a6el,0xe549e0195bb4d041l,
        0x7c3930ba9208d850l } },
    /* 52 << 203 */
    { { 0xe07141fcaaa2902bl,0x539ad799e4f69ad3l,0xa6453f94813f9ffdl,
        0xc58d3c48375bc2f7l },
      { 0xb3326fad5dc64e96l,0x3aafcaa9b240e354l,0x1d1b0903aca1e7a9l,
        0x4ceb97671211b8a0l } },
    /* 53 << 203 */
    { { 0xeca83e49e32a858el,0x4c32892eae907badl,0xd5b42ab62eb9b494l,
        0x7fde3ee21eabae1bl },
      { 0x13b5ab09caf54957l,0xbfb028bee5f5d5d5l,0x928a06502003e2c0l,
        0x90793aac67476843l } },
    /* 54 << 203 */
    { { 0x5e942e79c81710a0l,0x557e4a3627ccadd4l,0x72a2bc564bcf6d0cl,
        0x09ee5f4326d7b80cl },
      { 0x6b70dbe9d4292f19l,0x56f74c2663f16b18l,0xc23db0f735fbb42al,
        0xb606bdf66ae10040l } },
    /* 55 << 203 */
    { { 0x1eb15d4d044573acl,0x7dc3cf86556b0ba4l,0x97af9a33c60df6f7l,
        0x0b1ef85ca716ce8cl },
      { 0x2922f884c96958bel,0x7c32fa9435690963l,0x2d7f667ceaa00061l,
        0xeaaf7c173547365cl } },
    /* 56 << 203 */
    { { 0x1eb4de4687032d58l,0xc54f3d835e2c79e0l,0x07818df45d04ef23l,
        0x55faa9c8673d41b4l },
      { 0xced64f6f89b95355l,0x4860d2eab7415c84l,0x5fdb9bd2050ebad3l,
        0xdb53e0cc6685a5bfl } },
    /* 57 << 203 */
    { { 0xb830c0319feb6593l,0xdd87f3106accff17l,0x2303ebab9f555c10l,
        0x94603695287e7065l },
      { 0xf88311c32e83358cl,0x508dd9b4eefb0178l,0x7ca237062dba8652l,
        0x62aac5a30047abe5l } },
    /* 58 << 203 */
    { { 0x9a61d2a08b1ea7b3l,0xd495ab63ae8b1485l,0x38740f8487052f99l,
        0x178ebe5bb2974eeal },
      { 0x030bbcca5b36d17fl,0xb5e4cce3aaf86eeal,0xb51a022068f8e9e0l,
        0xa434879609eb3e75l } },
    /* 59 << 203 */
    { { 0xbe592309eef1a752l,0x5d7162d76f2aa1edl,0xaebfb5ed0f007dd2l,
        0x255e14b2c89edd22l },
      { 0xba85e0720303b697l,0xc5d17e25f05720ffl,0x02b58d6e5128ebb6l,
        0x2c80242dd754e113l } },
    /* 60 << 203 */
    { { 0x919fca5fabfae1cal,0x937afaac1a21459bl,0x9e0ca91c1f66a4d2l,
        0x194cc7f323ec1331l },
      { 0xad25143a8aa11690l,0xbe40ad8d09b59e08l,0x37d60d9be750860al,
        0x6c53b008c6bf434cl } },
    /* 61 << 203 */
    { { 0xb572415d1356eb80l,0xb8bf9da39578ded8l,0x22658e365e8fb38bl,
        0x9b70ce225af8cb22l },
      { 0x7c00018a829a8180l,0x84329f93b81ed295l,0x7c343ea25f3cea83l,
        0x38f8655f67586536l } },
    /* 62 << 203 */
    { { 0xa661a0d01d3ec517l,0x98744652512321ael,0x084ca591eca92598l,
        0xa9bb9dc91dcb3febl },
      { 0x14c5435578b4c240l,0x5ed62a3b610cafdcl,0x07512f371b38846bl,
        0x571bb70ab0e38161l } },
    /* 63 << 203 */
    { { 0xb556b95b2da705d2l,0x3ef8ada6b1a08f98l,0x85302ca7ddecfbe5l,
        0x0e530573943105cdl },
      { 0x60554d5521a9255dl,0x63a32fa1f2f3802al,0x35c8c5b0cd477875l,
        0x97f458ea6ad42da1l } },
    /* 64 << 203 */
    { { 0x832d7080eb6b242dl,0xd30bd0233b71e246l,0x7027991bbe31139dl,
        0x68797e91462e4e53l },
      { 0x423fe20a6b4e185al,0x82f2c67e42d9b707l,0x25c817684cf7811bl,
        0xbd53005e045bb95dl } },
    /* 0 << 210 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 210 */
    { { 0xe5f649be9d8e68fdl,0xdb0f05331b044320l,0xf6fde9b3e0c33398l,
        0x92f4209b66c8cfael },
      { 0xe9d1afcc1a739d4bl,0x09aea75fa28ab8del,0x14375fb5eac6f1d0l,
        0x6420b560708f7aa5l } },
    /* 2 << 210 */
    { { 0x9eae499c6254dc41l,0x7e2939247a837e7el,0x74aec08c090524a7l,
        0xf82b92198d6f55f2l },
      { 0x493c962e1402cec5l,0x9f17ca17fa2f30e7l,0xbcd783e8e9b879cbl,
        0xea3d8c145a6f145fl } },
    /* 3 << 210 */
    { { 0xdede15e75e0dee6el,0x74f24872dc628aa2l,0xd3e9c4fe7861bb93l,
        0x56d4822a6187b2e0l },
      { 0xb66417cfc59826f9l,0xca2609692408169el,0xedf69d06c79ef885l,
        0x00031f8adc7d138fl } },
    /* 4 << 210 */
    { { 0x103c46e60ebcf726l,0x4482b8316231470el,0x6f6dfaca487c2109l,
        0x2e0ace9762e666efl },
      { 0x3246a9d31f8d1f42l,0x1b1e83f1574944d2l,0x13dfa63aa57f334bl,
        0x0cf8daed9f025d81l } },
    /* 5 << 210 */
    { { 0x30d78ea800ee11c1l,0xeb053cd4b5e3dd75l,0x9b65b13ed58c43c5l,
        0xc3ad49bdbd151663l },
      { 0x99fd8e41b6427990l,0x12cf15bd707eae1el,0x29ad4f1b1aabb71el,
        0x5143e74d07545d0el } },
    /* 6 << 210 */
    { { 0x30266336c88bdee1l,0x25f293065876767cl,0x9c078571c6731996l,
        0xc88690b2ed552951l },
      { 0x274f2c2d852705b4l,0xb0bf8d444e09552dl,0x7628beeb986575d1l,
        0x407be2387f864651l } },
    /* 7 << 210 */
    { { 0x0e5e3049a639fc6bl,0xe75c35d986003625l,0x0cf35bd85dcc1646l,
        0x8bcaced26c26273al },
      { 0xe22ecf1db5536742l,0x013dd8971a9e068bl,0x17f411cb8a7909c5l,
        0x5757ac98861dd506l } },
    /* 8 << 210 */
    { { 0x85de1f0d1e935abbl,0xdefd10b4154de37al,0xb8d9e392369cebb5l,
        0x54d5ef9b761324bel },
      { 0x4d6341ba74f17e26l,0xc0a0e3c878c1dde4l,0xa6d7758187d918fdl,
        0x6687601502ca3a13l } },
    /* 9 << 210 */
    { { 0xc7313e9cf36658f0l,0xc433ef1c71f8057el,0x853262461b6a835al,
        0xc8f053987c86394cl },
      { 0xff398cdfe983c4a1l,0xbf5e816203b7b931l,0x93193c46b7b9045bl,
        0x1e4ebf5da4a6e46bl } },
    /* 10 << 210 */
    { { 0xf9942a6043a24fe7l,0x29c1191effb3492bl,0x9f662449902fde05l,
        0xc792a7ac6713c32dl },
      { 0x2fd88ad8b737982cl,0x7e3a0319a21e60e3l,0x09b0de447383591al,
        0x6df141ee8310a456l } },
    /* 11 << 210 */
    { { 0xaec1a039e6d6f471l,0x14b2ba0f1198d12el,0xebc1a1603aeee5acl,
        0x401f4836e0b964cel },
      { 0x2ee437964fd03f66l,0x3fdb4e49dd8f3f12l,0x6ef267f629380f18l,
        0x3e8e96708da64d16l } },
    /* 12 << 210 */
    { { 0xbc19180c207674f1l,0x112e09a733ae8fdbl,0x996675546aaeb71el,
        0x79432af1e101b1c7l },
      { 0xd5eb558fde2ddec6l,0x81392d1f5357753fl,0xa7a76b973ae1158al,
        0x416fbbff4a899991l } },
    /* 13 << 210 */
    { { 0x9e65fdfd0d4a9dcfl,0x7bc29e48944ddf12l,0xbc1a92d93c856866l,
        0x273c69056e98dfe2l },
      { 0x69fce418cdfaa6b8l,0x606bd8235061c69fl,0x42d495a06af75e27l,
        0x8ed3d5056d873a1fl } },
    /* 14 << 210 */
    { { 0xaf5528416ab25b6al,0xc6c0ffc72b1a4523l,0xab18827b21c99e03l,
        0x060e86489034691bl },
      { 0x5207f90f93c7f398l,0x9f4a96cb82f8d10bl,0xdd71cd793ad0f9e3l,
        0x84f435d2fc3a54f5l } },
    /* 15 << 210 */
    { { 0x4b03c55b8e33787fl,0xef42f975a6384673l,0xff7304f75051b9f0l,
        0x18aca1dc741c87c2l },
      { 0x56f120a72d4bfe80l,0xfd823b3d053e732cl,0x11bccfe47537ca16l,
        0xdf6c9c741b5a996bl } },
    /* 16 << 210 */
    { { 0xee7332c7904fc3fal,0x14a23f45c7e3636al,0xc38659c3f091d9aal,
        0x4a995e5db12d8540l },
      { 0x20a53becf3a5598al,0x56534b17b1eaa995l,0x9ed3dca4bf04e03cl,
        0x716c563ad8d56268l } },
    /* 17 << 210 */
    { { 0x27ba77a41d6178e7l,0xe4c80c4068a1ff8el,0x750110990a13f63dl,
        0x7bf33521a61d46f3l },
      { 0x0aff218e10b365bbl,0x810218040fd7ea75l,0x05a3fd8aa4b3a925l,
        0xb829e75f9b3db4e6l } },
    /* 18 << 210 */
    { { 0x6bdc75a54d53e5fbl,0x04a5dc02d52717e3l,0x86af502fe9a42ec2l,
        0x8867e8fb2630e382l },
      { 0xbf845c6ebec9889bl,0x54f491f2cb47c98dl,0xa3091fba790c2a12l,
        0xd7f6fd78c20f708bl } },
    /* 19 << 210 */
    { { 0xa569ac30acde5e17l,0xd0f996d06852b4d7l,0xe51d4bb54609ae54l,
        0x3fa37d170daed061l },
      { 0x62a8868434b8fb41l,0x99a2acbd9efb64f1l,0xb75c1a5e6448e1f2l,
        0xfa99951a42b5a069l } },
    /* 20 << 210 */
    { { 0x6d956e892f3b26e7l,0xf4709860da875247l,0x3ad151792482dda3l,
        0xd64110e3017d82f0l },
      { 0x14928d2cfad414e4l,0x2b155f582ed02b24l,0x481a141bcb821bf1l,
        0x12e3c7704f81f5dal } },
    /* 21 << 210 */
    { { 0xe49c5de59fff8381l,0x110532325bbec894l,0xa0d051cc454d88c4l,
        0x4f6db89c1f8e531bl },
      { 0x34fe3fd6ca563a44l,0x7f5c221558da8ab9l,0x8445016d9474f0a1l,
        0x17d34d61cb7d8a0al } },
    /* 22 << 210 */
    { { 0x8e9d39101c474019l,0xcaff2629d52ceefbl,0xf9cf3e32c1622c2bl,
        0xd4b95e3ce9071a05l },
      { 0xfbbca61f1594438cl,0x1eb6e6a604aadedfl,0x853027f468e14940l,
        0x221d322adfabda9cl } },
    /* 23 << 210 */
    { { 0xed8ea9f6b7cb179al,0xdc7b764db7934dccl,0xfcb139405e09180dl,
        0x6629a6bfb47dc2ddl },
      { 0xbfc55e4e9f5a915el,0xb1db9d376204441el,0xf82d68cf930c5f53l,
        0x17d3a142cbb605b1l } },
    /* 24 << 210 */
    { { 0xdd5944ea308780f2l,0xdc8de7613845f5e4l,0x6beaba7d7624d7a3l,
        0x1e709afd304df11el },
      { 0x9536437602170456l,0xbf204b3ac8f94b64l,0x4e53af7c5680ca68l,
        0x0526074ae0c67574l } },
    /* 25 << 210 */
    { { 0x95d8cef8ecd92af6l,0xe6b9fa7a6cd1745al,0x3d546d3da325c3e4l,
        0x1f57691d9ae93aael },
      { 0xe891f3fe9d2e1a33l,0xd430093fac063d35l,0xeda59b125513a327l,
        0xdc2134f35536f18fl } },
    /* 26 << 210 */
    { { 0xaa51fe2c5c210286l,0x3f68aaee1cab658cl,0x5a23a00bf9357292l,
        0x9a626f397efdabedl },
      { 0xfe2b3bf3199d78e3l,0xb7a2af7771bbc345l,0x3d19827a1e59802cl,
        0x823bbc15b487a51cl } },
    /* 27 << 210 */
    { { 0x856139f299d0a422l,0x9ac3df65f456c6fbl,0xaddf65c6701f8bd6l,
        0x149f321e3758df87l },
      { 0xb1ecf714721b7ebal,0xe17df09831a3312al,0xdb2fd6ecd5c4d581l,
        0xfd02996f8fcea1b3l } },
    /* 28 << 210 */
    { { 0xe29fa63e7882f14fl,0xc9f6dc3507c6cadcl,0x46f22d6fb882bed0l,
        0x1a45755bd118e52cl },
      { 0x9f2c7c277c4608cfl,0x7ccbdf32568012c2l,0xfcb0aedd61729b0el,
        0x7ca2ca9ef7d75dbfl } },
    /* 29 << 210 */
    { { 0xf58fecb16f640f62l,0xe274b92b39f51946l,0x7f4dfc046288af44l,
        0x0a91f32aeac329e5l },
      { 0x43ad274bd6aaba31l,0x719a16400f6884f9l,0x685d29f6daf91e20l,
        0x5ec1cc3327e49d52l } },
    /* 30 << 210 */
    { { 0x38f4de963b54a059l,0x0e0015e5efbcfdb3l,0x177d23d94dbb8da6l,
        0x98724aa297a617adl },
      { 0x30f0885bfdb6558el,0xf9f7a28ac7899a96l,0xd2ae8ac8872dc112l,
        0xfa0642ca73c3c459l } },
    /* 31 << 210 */
    { { 0x15296981e7dfc8d6l,0x67cd44501fb5b94al,0x0ec71cf10eddfd37l,
        0xc7e5eeb39a8eddc7l },
      { 0x02ac8e3d81d95028l,0x0088f17270b0e35dl,0xec041fabe1881fe3l,
        0x62cf71b8d99e7faal } },
    /* 32 << 210 */
    { { 0x5043dea7e0f222c2l,0x309d42ac72e65142l,0x94fe9ddd9216cd30l,
        0xd6539c7d0f87feecl },
      { 0x03c5a57c432ac7d7l,0x72692cf0327fda10l,0xec28c85f280698del,
        0x2331fb467ec283b1l } },
    /* 33 << 210 */
    { { 0xd34bfa322867e633l,0x78709a820a9cc815l,0xb7fe6964875e2fa5l,
        0x25cc064f9e98bfb5l },
      { 0x9eb0151c493a65c5l,0x5fb5d94153182464l,0x69e6f130f04618e2l,
        0xa8ecec22f89c8ab6l } },
    /* 34 << 210 */
    { { 0xcd6ac88bb96209bdl,0x65fa8cdbb3e1c9e0l,0xa47d22f54a8d8eacl,
        0x83895cdf8d33f963l },
      { 0xa8adca59b56cd3d1l,0x10c8350bdaf38232l,0x2b161fb3a5080a9fl,
        0xbe7f5c643af65b3al } },
    /* 35 << 210 */
    { { 0x2c75403997403a11l,0x94626cf7121b96afl,0x431de7c46a983ec2l,
        0x3780dd3a52cc3df7l },
      { 0xe28a0e462baf8e3bl,0xabe68aad51d299ael,0x603eb8f9647a2408l,
        0x14c61ed65c750981l } },
    /* 36 << 210 */
    { { 0x88b34414c53352e7l,0x5a34889c1337d46el,0x612c1560f95f2bc8l,
        0x8a3f8441d4807a3al },
      { 0x680d9e975224da68l,0x60cd6e88c3eb00e9l,0x3875a98e9a6bc375l,
        0xdc80f9244fd554c2l } },
    /* 37 << 210 */
    { { 0x6c4b34156ac77407l,0xa1e5ea8f25420681l,0x541bfa144607a458l,
        0x5dbc7e7a96d7fbf9l },
      { 0x646a851b31590a47l,0x039e85ba15ee6df8l,0xd19fa231d7b43fc0l,
        0x84bc8be8299a0e04l } },
    /* 38 << 210 */
    { { 0x2b9d2936f20df03al,0x240543828608d472l,0x76b6ba049149202al,
        0xb21c38313670e7b7l },
      { 0xddd93059d6fdee10l,0x9da47ad378488e71l,0x99cc1dfda0fcfb25l,
        0x42abde1064696954l } },
    /* 39 << 210 */
    { { 0x14cc15fc17eab9fel,0xd6e863e4d3e70972l,0x29a7765c6432112cl,
        0x886600015b0774d8l },
      { 0x3729175a2c088eael,0x13afbcae8230b8d4l,0x44768151915f4379l,
        0xf086431ad8d22812l } },
    /* 40 << 210 */
    { { 0x37461955c298b974l,0x905fb5f0f8711e04l,0x787abf3afe969d18l,
        0x392167c26f6a494el },
      { 0xfc7a0d2d28c511dal,0xf127c7dcb66a262dl,0xf9c4bb95fd63fdf0l,
        0x900165893913ef46l } },
    /* 41 << 210 */
    { { 0x74d2a73c11aa600dl,0x2f5379bd9fb5ab52l,0xe49e53a47fb70068l,
        0x68dd39e5404aa9a7l },
      { 0xb9b0cf572ecaa9c3l,0xba0e103be824826bl,0x60c2198b4631a3c4l,
        0xc5ff84abfa8966a2l } },
    /* 42 << 210 */
    { { 0x2d6ebe22ac95aff8l,0x1c9bb6dbb5a46d09l,0x419062da53ee4f8dl,
        0x7b9042d0bb97efefl },
      { 0x0f87f080830cf6bdl,0x4861d19a6ec8a6c6l,0xd3a0daa1202f01aal,
        0xb0111674f25afbd5l } },
    /* 43 << 210 */
    { { 0x6d00d6cf1afb20d9l,0x1369500040671bc5l,0x913ab0dc2485ea9bl,
        0x1f2bed069eef61acl },
      { 0x850c82176d799e20l,0x93415f373271c2del,0x5afb06e96c4f5910l,
        0x688a52dfc4e9e421l } },
    /* 44 << 210 */
    { { 0x30495ba3e2a9a6dbl,0x4601303d58f9268bl,0xbe3b0dad7eb0f04fl,
        0x4ea472504456936dl },
      { 0x8caf8798d33fd3e7l,0x1ccd8a89eb433708l,0x9effe3e887fd50adl,
        0xbe240a566b29c4dfl } },
    /* 45 << 210 */
    { { 0xec4ffd98ca0e7ebdl,0xf586783ae748616el,0xa5b00d8fc77baa99l,
        0x0acada29b4f34c9cl },
      { 0x36dad67d0fe723acl,0x1d8e53a539c36c1el,0xe4dd342d1f4bea41l,
        0x64fd5e35ebc9e4e0l } },
    /* 46 << 210 */
    { { 0x96f01f9057908805l,0xb5b9ea3d5ed480ddl,0x366c5dc23efd2dd0l,
        0xed2fe3056e9dfa27l },
      { 0x4575e8926e9197e2l,0x11719c09ab502a5dl,0x264c7bece81f213fl,
        0x741b924155f5c457l } },
    /* 47 << 210 */
    { { 0x78ac7b6849a5f4f4l,0xf91d70a29fc45b7dl,0x39b05544b0f5f355l,
        0x11f06bceeef930d9l },
      { 0xdb84d25d038d05e1l,0x04838ee5bacc1d51l,0x9da3ce869e8ee00bl,
        0xc3412057c36eda1fl } },
    /* 48 << 210 */
    { { 0xae80b91364d9c2f4l,0x7468bac3a010a8ffl,0xdfd2003737359d41l,
        0x1a0f5ab815efeaccl },
      { 0x7c25ad2f659d0ce0l,0x4011bcbb6785cff1l,0x128b99127e2192c7l,
        0xa549d8e113ccb0e8l } },
    /* 49 << 210 */
    { { 0x805588d8c85438b1l,0x5680332dbc25cb27l,0xdcd1bc961a4bfdf4l,
        0x779ff428706f6566l },
      { 0x8bbee998f059987al,0xf6ce8cf2cc686de7l,0xf8ad3c4a953cfdb2l,
        0xd1d426d92205da36l } },
    /* 50 << 210 */
    { { 0xb3c0f13fc781a241l,0x3e89360ed75362a8l,0xccd05863c8a91184l,
        0x9bd0c9b7efa8a7f4l },
      { 0x97ee4d538a912a4bl,0xde5e15f8bcf518fdl,0x6a055bf8c467e1e0l,
        0x10be4b4b1587e256l } },
    /* 51 << 210 */
    { { 0xd90c14f2668621c9l,0xd5518f51ab9c92c1l,0x8e6a0100d6d47b3cl,
        0xcbe980dd66716175l },
      { 0x500d3f10ddd83683l,0x3b6cb35d99cac73cl,0x53730c8b6083d550l,
        0xcf159767df0a1987l } },
    /* 52 << 210 */
    { { 0x84bfcf5343ad73b3l,0x1b528c204f035a94l,0x4294edf733eeac69l,
        0xb6283e83817f3240l },
      { 0xc3fdc9590a5f25b1l,0xefaf8aa55844ee22l,0xde269ba5dbdde4del,
        0xe3347160c56133bfl } },
    /* 53 << 210 */
    { { 0xc11842198d9ea9f8l,0x090de5dbf3fc1ab5l,0x404c37b10bf22cdal,
        0x7de20ec8f5618894l },
      { 0x754c588eecdaecabl,0x6ca4b0ed88342743l,0x76f08bddf4a938ecl,
        0xd182de8991493ccbl } },
    /* 54 << 210 */
    { { 0xd652c53ec8a4186al,0xb3e878db946d8e33l,0x088453c05f37663cl,
        0x5cd9daaab407748bl },
      { 0xa1f5197f586d5e72l,0x47500be8c443ca59l,0x78ef35b2e2652424l,
        0x09c5d26f6dd7767dl } },
    /* 55 << 210 */
    { { 0x7175a79aa74d3f7bl,0x0428fd8dcf5ea459l,0x511cb97ca5d1746dl,
        0x36363939e71d1278l },
      { 0xcf2df95510350bf4l,0xb381743960aae782l,0xa748c0e43e688809l,
        0x98021fbfd7a5a006l } },
    /* 56 << 210 */
    { { 0x9076a70c0e367a98l,0xbea1bc150f62b7c2l,0x2645a68c30fe0343l,
        0xacaffa78699dc14fl },
      { 0xf4469964457bf9c4l,0x0db6407b0d2ead83l,0x68d56cadb2c6f3ebl,
        0x3b512e73f376356cl } },
    /* 57 << 210 */
    { { 0xe43b0e1ffce10408l,0x89ddc0035a5e257dl,0xb0ae0d120362e5b3l,
        0x07f983c7b0519161l },
      { 0xc2e94d155d5231e7l,0xcff22aed0b4f9513l,0xb02588dd6ad0b0b5l,
        0xb967d1ac11d0dcd5l } },
    /* 58 << 210 */
    { { 0x8dac6bc6cf777b6cl,0x0062bdbd4c6d1959l,0x53da71b50ef5cc85l,
        0x07012c7d4006f14fl },
      { 0x4617f962ac47800dl,0x53365f2bc102ed75l,0xb422efcb4ab8c9d3l,
        0x195cb26b34af31c9l } },
    /* 59 << 210 */
    { { 0x3a926e2905f2c4cel,0xbd2bdecb9856966cl,0x5d16ab3a85527015l,
        0x9f81609e4486c231l },
      { 0xd8b96b2cda350002l,0xbd054690fa1b7d36l,0xdc90ebf5e71d79bcl,
        0xf241b6f908964e4el } },
    /* 60 << 210 */
    { { 0x7c8386432fe3cd4cl,0xe0f33acbb4bc633cl,0xb4a9ecec3d139f1fl,
        0x05ce69cddc4a1f49l },
      { 0xa19d1b16f5f98aafl,0x45bb71d66f23e0efl,0x33789fcd46cdfdd3l,
        0x9b8e2978cee040cal } },
    /* 61 << 210 */
    { { 0x9c69b246ae0a6828l,0xba533d247078d5aal,0x7a2e42c07bb4fbdbl,
        0xcfb4879a7035385cl },
      { 0x8c3dd30b3281705bl,0x7e361c6c404fe081l,0x7b21649c3f604edfl,
        0x5dbf6a3fe52ffe47l } },
    /* 62 << 210 */
    { { 0xc41b7c234b54d9bfl,0x1374e6813511c3d9l,0x1863bf16c1b2b758l,
        0x90e785071e9e6a96l },
      { 0xab4bf98d5d86f174l,0xd74e0bd385e96fe4l,0x8afde39fcac5d344l,
        0x90946dbcbd91b847l } },
    /* 63 << 210 */
    { { 0xf5b42358fe1a838cl,0x05aae6c5620ac9d8l,0x8e193bd8a1ce5a0bl,
        0x8f7105714dabfd72l },
      { 0x8d8fdd48182caaacl,0x8c4aeefa040745cfl,0x73c6c30af3b93e6dl,
        0x991241f316f42011l } },
    /* 64 << 210 */
    { { 0xa0158eeae457a477l,0xd19857dbee6ddc05l,0xb326522418c41671l,
        0x3ffdfc7e3c2c0d58l },
      { 0x3a3a525426ee7cdal,0x341b0869df02c3a8l,0xa023bf42723bbfc8l,
        0x3d15002a14452691l } },
    /* 0 << 217 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 217 */
    { { 0x5ef7324c85edfa30l,0x2597655487d4f3dal,0x352f5bc0dcb50c86l,
        0x8f6927b04832a96cl },
      { 0xd08ee1ba55f2f94cl,0x6a996f99344b45fal,0xe133cb8da8aa455dl,
        0x5d0721ec758dc1f7l } },
    /* 2 << 217 */
    { { 0x6ba7a92079e5fb67l,0xe1331feb70aa725el,0x5080ccf57df5d837l,
        0xe4cae01d7ff72e21l },
      { 0xd9243ee60412a77dl,0x06ff7cacdf449025l,0xbe75f7cd23ef5a31l,
        0xbc9578220ddef7a8l } },
    /* 3 << 217 */
    { { 0x8cf7230cb0ce1c55l,0x5b534d050bbfb607l,0xee1ef1130e16363bl,
        0x27e0aa7ab4999e82l },
      { 0xce1dac2d79362c41l,0x67920c9091bb6cb0l,0x1e648d632223df24l,
        0x0f7d9eefe32e8f28l } },
    /* 4 << 217 */
    { { 0x6943f39afa833834l,0x22951722a6328562l,0x81d63dd54170fc10l,
        0x9f5fa58faecc2e6dl },
      { 0xb66c8725e77d9a3bl,0x11235cea6384ebe0l,0x06a8c1185845e24al,
        0x0137b286ebd093b1l } },
    /* 5 << 217 */
    { { 0xc589e1ce44ace150l,0xe0f8d3d94381e97cl,0x59e99b1162c5a4b8l,
        0x90d262f7fd0ec9f9l },
      { 0xfbc854c9283e13c9l,0x2d04fde7aedc7085l,0x057d776547dcbecbl,
        0x8dbdf5919a76fa5fl } },
    /* 6 << 217 */
    { { 0xd01506950de1e578l,0x2e1463e7e9f72bc6l,0xffa684411b39eca5l,
        0x673c85307c037f2fl },
      { 0xd0d6a600747f91dal,0xb08d43e1c9cb78e9l,0x0fc0c64427b5cef5l,
        0x5c1d160aa60a2fd6l } },
    /* 7 << 217 */
    { { 0xf98cae5328c8e13bl,0x375f10c4b2eddcd1l,0xd4eb8b7f5cce06adl,
        0xb4669f4580a2e1efl },
      { 0xd593f9d05bbd8699l,0x5528a4c9e7976d13l,0x3923e0951c7e28d3l,
        0xb92937903f6bb577l } },
    /* 8 << 217 */
    { { 0xdb567d6ac42bd6d2l,0x6df86468bb1f96ael,0x0efe5b1a4843b28el,
        0x961bbb056379b240l },
      { 0xb6caf5f070a6a26bl,0x70686c0d328e6e39l,0x80da06cf895fc8d3l,
        0x804d8810b363fdc9l } },
    /* 9 << 217 */
    { { 0xbe22877b207f1670l,0x9b0dd1884e615291l,0x625ae8dc97a3c2bfl,
        0x08584ef7439b86e8l },
      { 0xde7190a5dcd898ffl,0x26286c402058ee3dl,0x3db0b2175f87b1c1l,
        0xcc334771102a6db5l } },
    /* 10 << 217 */
    { { 0xd99de9542f770fb1l,0x97c1c6204cd7535el,0xd3b6c4483f09cefcl,
        0xd725af155a63b4f8l },
      { 0x0c95d24fc01e20ecl,0xdfd374949ae7121fl,0x7d6ddb72ec77b7ecl,
        0xfe079d3b0353a4ael } },
    /* 11 << 217 */
    { { 0x3066e70a2e6ac8d2l,0x9c6b5a43106e5c05l,0x52d3c6f5ede59b8cl,
        0x30d6a5c3fccec9ael },
      { 0xedec7c224fc0a9efl,0x190ff08395c16cedl,0xbe12ec8f94de0fdel,
        0x0d131ab8852d3433l } },
    /* 12 << 217 */
    { { 0x42ace07e85701291l,0x94793ed9194061a8l,0x30e83ed6d7f4a485l,
        0x9eec7269f9eeff4dl },
      { 0x90acba590c9d8005l,0x5feca4581e79b9d1l,0x8fbe54271d506a1el,
        0xa32b2c8e2439cfa7l } },
    /* 13 << 217 */
    { { 0x1671c17373dd0b4el,0x37a2821444a054c6l,0x81760a1b4e8b53f1l,
        0xa6c04224f9f93b9el },
      { 0x18784b34cf671e3cl,0x81bbecd2cda9b994l,0x38831979b2ab3848l,
        0xef54feb7f2e03c2dl } },
    /* 14 << 217 */
    { { 0xcf197ca7fb8088fal,0x014272474ddc96c5l,0xa2d2550a30777176l,
        0x534698984d0cf71dl },
      { 0x6ce937b83a2aaac6l,0xe9f91dc35af38d9bl,0x2598ad83c8bf2899l,
        0x8e706ac9b5536c16l } },
    /* 15 << 217 */
    { { 0x40dc7495f688dc98l,0x26490cd7124c4afcl,0xe651ec841f18775cl,
        0x393ea6c3b4fdaf4al },
      { 0x1e1f33437f338e0dl,0x39fb832b6053e7b5l,0x46e702da619e14d5l,
        0x859cacd1cdeef6e0l } },
    /* 16 << 217 */
    { { 0x63b99ce74462007dl,0xb8ab48a54cb5f5b7l,0x9ec673d2f55edde7l,
        0xd1567f748cfaefdal },
      { 0x46381b6b0887bcecl,0x694497cee178f3c2l,0x5e6525e31e6266cbl,
        0x5931de26697d6413l } },
    /* 17 << 217 */
    { { 0x87f8df7c0e58d493l,0xb1ae5ed058b73f12l,0xc368f784dea0c34dl,
        0x9bd0a120859a91a0l },
      { 0xb00d88b7cc863c68l,0x3a1cc11e3d1f4d65l,0xea38e0e70aa85593l,
        0x37f13e987dc4aee8l } },
    /* 18 << 217 */
    { { 0x10d38667bc947badl,0x738e07ce2a36ee2el,0xc93470cdc577fcacl,
        0xdee1b6162782470dl },
      { 0x36a25e672e793d12l,0xd6aa6caee0f186dal,0x474d0fd980e07af7l,
        0xf7cdc47dba8a5cd4l } },
    /* 19 << 217 */
    { { 0x28af6d9dab15247fl,0x7c789c10493a537fl,0x7ac9b11023a334e7l,
        0x0236ac0912c9c277l },
      { 0xa7e5bd251d7a5144l,0x098b9c2af13ec4ecl,0x3639dacad3f0abcal,
        0x642da81aa23960f9l } },
    /* 20 << 217 */
    { { 0x7d2e5c054f7269b1l,0xfcf30777e287c385l,0x10edc84ff2a46f21l,
        0x354417574f43fa36l },
      { 0xf1327899fd703431l,0xa438d7a616dd587al,0x65c34c57e9c8352dl,
        0xa728edab5cc5a24el } },
    /* 21 << 217 */
    { { 0xaed78abc42531689l,0x0a51a0e8010963efl,0x5776fa0ad717d9b3l,
        0xf356c2397dd3428bl },
      { 0x29903fff8d3a3dacl,0x409597fa3d94491fl,0x4cd7a5ffbf4a56a4l,
        0xe50964748adab462l } },
    /* 22 << 217 */
    { { 0xa97b51265c3427b0l,0x6401405cd282c9bdl,0x3629f8d7222c5c45l,
        0xb1c02c16e8d50aedl },
      { 0xbea2ed75d9635bc9l,0x226790c76e24552fl,0x3c33f2a365f1d066l,
        0x2a43463e6dfccc2el } },
    /* 23 << 217 */
    { { 0x8cc3453adb483761l,0xe7cc608565d5672bl,0x277ed6cbde3efc87l,
        0x19f2f36869234eafl },
      { 0x9aaf43175c0b800bl,0x1f1e7c898b6da6e2l,0x6cfb4715b94ec75el,
        0xd590dd5f453118c2l } },
    /* 24 << 217 */
    { { 0x14e49da11f17a34cl,0x5420ab39235a1456l,0xb76372412f50363bl,
        0x7b15d623c3fabb6el },
      { 0xa0ef40b1e274e49cl,0x5cf5074496b1860al,0xd6583fbf66afe5a4l,
        0x44240510f47e3e9al } },
    /* 25 << 217 */
    { { 0x9925434311b2d595l,0xf1367499eec8df57l,0x3cb12c613e73dd05l,
        0xd248c0337dac102al },
      { 0xcf154f13a77739f5l,0xbf4288cb23d2af42l,0xaa64c9b632e4a1cfl,
        0xee8c07a8c8a208f3l } },
    /* 26 << 217 */
    { { 0xe10d49996fe8393fl,0x0f809a3fe91f3a32l,0x61096d1c802f63c8l,
        0x289e146257750d3dl },
      { 0xed06167e9889feeal,0xd5c9c0e2e0993909l,0x46fca0d856508ac6l,
        0x918260474f1b8e83l } },
    /* 27 << 217 */
    { { 0x4f2c877a9a4a2751l,0x71bd0072cae6feadl,0x38df8dcc06aa1941l,
        0x5a074b4c63beeaa8l },
      { 0xd6d65934c1cec8edl,0xa6ecb49eaabc03bdl,0xaade91c2de8a8415l,
        0xcfb0efdf691136e0l } },
    /* 28 << 217 */
    { { 0x11af45ee23ab3495l,0xa132df880b77463dl,0x8923c15c815d06f4l,
        0xc3ceb3f50d61a436l },
      { 0xaf52291de88fb1dal,0xea0579741da12179l,0xb0d7218cd2fef720l,
        0x6c0899c98e1d8845l } },
    /* 29 << 217 */
    { { 0x98157504752ddad7l,0xd60bd74fa1a68a97l,0x7047a3a9f658fb99l,
        0x1f5d86d65f8511e4l },
      { 0xb8a4bc424b5a6d88l,0x69eb2c331abefa7dl,0x95bf39e813c9c510l,
        0xf571960ad48aab43l } },
    /* 30 << 217 */
    { { 0x7e8cfbcf704e23c6l,0xc71b7d2228aaa65bl,0xa041b2bd245e3c83l,
        0x69b98834d21854ffl },
      { 0x89d227a3963bfeecl,0x99947aaade7da7cbl,0x1d9ee9dbee68a9b1l,
        0x0a08f003698ec368l } },
    /* 31 << 217 */
    { { 0xe9ea409478ef2487l,0xc8d2d41502cfec26l,0xc52f9a6eb7dcf328l,
        0x0ed489e385b6a937l },
      { 0x9b94986bbef3366el,0x0de59c70edddddb8l,0xffdb748ceadddbe2l,
        0x9b9784bb8266ea40l } },
    /* 32 << 217 */
    { { 0x142b55021a93507al,0xb4cd11878d3c06cfl,0xdf70e76a91ec3f40l,
        0x484e81ad4e7553c2l },
      { 0x830f87b5272e9d6el,0xea1c93e5c6ff514al,0x67cc2adcc4192a8el,
        0xc77e27e242f4535al } },
    /* 33 << 217 */
    { { 0x9cdbab36d2b713c5l,0x86274ea0cf7b0cd3l,0x784680f309af826bl,
        0xbfcc837a0c72dea3l },
      { 0xa8bdfe9dd6529b73l,0x708aa22863a88002l,0x6c7a9a54c91d45b9l,
        0xdf1a38bbfd004f56l } },
    /* 34 << 217 */
    { { 0x2e8c9a26b8bad853l,0x2d52cea33723eae7l,0x054d6d8156ca2830l,
        0xa3317d149a8dc411l },
      { 0xa08662fefd4ddedal,0xed2a153ab55d792bl,0x7035c16abfc6e944l,
        0xb6bc583400171cf3l } },
    /* 35 << 217 */
    { { 0xe27152b383d102b6l,0xfe695a470646b848l,0xa5bb09d8916e6d37l,
        0xb4269d640d17015el },
      { 0x8d8156a10a1d2285l,0xfeef6c5146d26d72l,0x9dac57c84c5434a7l,
        0x0282e5be59d39e31l } },
    /* 36 << 217 */
    { { 0xedfff181721c486dl,0x301baf10bc58824el,0x8136a6aa00570031l,
        0x55aaf78c1cddde68l },
      { 0x2682937159c63952l,0x3a3bd2748bc25bafl,0xecdf8657b7e52dc3l,
        0x2dd8c087fd78e6c8l } },
    /* 37 << 217 */
    { { 0x20553274f5531461l,0x8b4a12815d95499bl,0xe2c8763a1a80f9d2l,
        0xd1dbe32b4ddec758l },
      { 0xaf12210d30c34169l,0xba74a95378baa533l,0x3d133c6ea438f254l,
        0xa431531a201bef5bl } },
    /* 38 << 217 */
    { { 0x15295e22f669d7ecl,0xca374f64357fb515l,0x8a8406ffeaa3fdb3l,
        0x106ae448df3f2da8l },
      { 0x8f9b0a9033c8e9a1l,0x234645e271ad5885l,0x3d0832241c0aed14l,
        0xf10a7d3e7a942d46l } },
    /* 39 << 217 */
    { { 0x7c11deee40d5c9bel,0xb2bae7ffba84ed98l,0x93e97139aad58dddl,
        0x3d8727963f6d1fa3l },
      { 0x483aca818569ff13l,0x8b89a5fb9a600f72l,0x4cbc27c3c06f2b86l,
        0x2213071363ad9c0bl } },
    /* 40 << 217 */
    { { 0xb5358b1e48ac2840l,0x18311294ecba9477l,0xda58f990a6946b43l,
        0x3098baf99ab41819l },
      { 0x66c4c1584198da52l,0xab4fc17c146bfd1bl,0x2f0a4c3cbf36a908l,
        0x2ae9e34b58cf7838l } },
    /* 41 << 217 */
    { { 0xf411529e3fa11b1fl,0x21e43677974af2b4l,0x7c20958ec230793bl,
        0x710ea88516e840f3l },
      { 0xfc0b21fcc5dc67cfl,0x08d5164788405718l,0xd955c21fcfe49eb7l,
        0x9722a5d556dd4a1fl } },
    /* 42 << 217 */
    { { 0xc9ef50e2c861baa5l,0xc0c21a5d9505ac3el,0xaf6b9a338b7c063fl,
        0xc63703392f4779c1l },
      { 0x22df99c7638167c3l,0xfe6ffe76795db30cl,0x2b822d33a4854989l,
        0xfef031dd30563aa5l } },
    /* 43 << 217 */
    { { 0x16b09f82d57c667fl,0xc70312cecc0b76f1l,0xbf04a9e6c9118aecl,
        0x82fcb4193409d133l },
      { 0x1a8ab385ab45d44dl,0xfba07222617b83a3l,0xb05f50dd58e81b52l,
        0x1d8db55321ce5affl } },
    /* 44 << 217 */
    { { 0x3097b8d4e344a873l,0x7d8d116dfe36d53el,0x6db22f587875e750l,
        0x2dc5e37343e144eal },
      { 0xc05f32e6e799eb95l,0xe9e5f4df6899e6ecl,0xbdc3bd681fab23d5l,
        0xb72b8ab773af60e6l } },
    /* 45 << 217 */
    { { 0x8db27ae02cecc84al,0x600016d87bdb871cl,0x42a44b13d7c46f58l,
        0xb8919727c3a77d39l },
      { 0xcfc6bbbddafd6088l,0x1a7401466bd20d39l,0x8c747abd98c41072l,
        0x4c91e765bdf68ea1l } },
    /* 46 << 217 */
    { { 0x7c95e5ca08819a78l,0xcf48b729c9587921l,0x091c7c5fdebbcc7dl,
        0x6f287404f0e05149l },
      { 0xf83b5ac226cd44ecl,0x88ae32a6cfea250el,0x6ac5047a1d06ebc5l,
        0xc7e550b4d434f781l } },
    /* 47 << 217 */
    { { 0x61ab1cf25c727bd2l,0x2e4badb11cf915b0l,0x1b4dadecf69d3920l,
        0xe61b1ca6f14c1dfel },
      { 0x90b479ccbd6bd51fl,0x8024e4018045ec30l,0xcab29ca325ef0e62l,
        0x4f2e941649e4ebc0l } },
    /* 48 << 217 */
    { { 0x45eb40ec0ccced58l,0x25cd4b9c0da44f98l,0x43e06458871812c6l,
        0x99f80d5516cef651l },
      { 0x571340c9ce6dc153l,0x138d5117d8665521l,0xacdb45bc4e07014dl,
        0x2f34bb3884b60b91l } },
    /* 49 << 217 */
    { { 0xf44a4fd22ae8921el,0xb039288e892ba1e2l,0x9da50174b1c180b2l,
        0x6b70ab661693dc87l },
      { 0x7e9babc9e7057481l,0x4581ddef9c80dc41l,0x0c890da951294682l,
        0x0b5629d33f4736e5l } },
    /* 50 << 217 */
    { { 0x2340c79eb06f5b41l,0xa42e84ce4e243469l,0xf9a20135045a71a9l,
        0xefbfb415d27b6fb6l },
      { 0x25ebea239d33cd6fl,0x9caedb88aa6c0af8l,0x53dc7e9ad9ce6f96l,
        0x3897f9fd51e0b15al } },
    /* 51 << 217 */
    { { 0xf51cb1f88e5d788el,0x1aec7ba8e1d490eel,0x265991e0cc58cb3cl,
        0x9f306e8c9fc3ad31l },
      { 0x5fed006e5040a0acl,0xca9d5043fb476f2el,0xa19c06e8beea7a23l,
        0xd28658010edabb63l } },
    /* 52 << 217 */
    { { 0xdb92293f6967469al,0x2894d8398d8a8ed8l,0x87c9e406bbc77122l,
        0x8671c6f12ea3a26al },
      { 0xe42df8d6d7de9853l,0x2e3ce346b1f2bcc7l,0xda601dfc899d50cfl,
        0xbfc913defb1b598fl } },
    /* 53 << 217 */
    { { 0x81c4909fe61f7908l,0x192e304f9bbc7b29l,0xc3ed8738c104b338l,
        0xedbe9e47783f5d61l },
      { 0x0c06e9be2db30660l,0xda3e613fc0eb7d8el,0xd8fa3e97322e096el,
        0xfebd91e8d336e247l } },
    /* 54 << 217 */
    { { 0x8f13ccc4df655a49l,0xa9e00dfc5eb20210l,0x84631d0fc656b6eal,
        0x93a058cdd8c0d947l },
      { 0x6846904a67bd3448l,0x4a3d4e1af394fd5cl,0xc102c1a5db225f52l,
        0xe3455bbafc4f5e9al } },
    /* 55 << 217 */
    { { 0x6b36985b4b9ad1cel,0xa98185365bb7f793l,0x6c25e1d048b1a416l,
        0x1381dd533c81bee7l },
      { 0xd2a30d617a4a7620l,0xc841292639b8944cl,0x3c1c6fbe7a97c33al,
        0x941e541d938664e7l } },
    /* 56 << 217 */
    { { 0x417499e84a34f239l,0x15fdb83cb90402d5l,0xb75f46bf433aa832l,
        0xb61e15af63215db1l },
      { 0xaabe59d4a127f89al,0x5d541e0c07e816dal,0xaaba0659a618b692l,
        0x5532773317266026l } },
    /* 57 << 217 */
    { { 0xaf53a0fc95f57552l,0x329476506cacb0c9l,0x253ff58dc821be01l,
        0xb0309531a06f1146l },
      { 0x59bbbdf505c2e54dl,0x158f27ad26e8dd22l,0xcc5b7ffb397e1e53l,
        0xae03f65b7fc1e50dl } },
    /* 58 << 217 */
    { { 0xa9784ebd9c95f0f9l,0x5ed9deb224640771l,0x31244af7035561c4l,
        0x87332f3a7ee857del },
      { 0x09e16e9e2b9e0d88l,0x52d910f456a06049l,0x507ed477a9592f48l,
        0x85cb917b2365d678l } },
    /* 59 << 217 */
    { { 0xf8511c934c8998d1l,0x2186a3f1730ea58fl,0x50189626b2029db0l,
        0x9137a6d902ceb75al },
      { 0x2fe17f37748bc82cl,0x87c2e93180469f8cl,0x850f71cdbf891aa2l,
        0x0ca1b89b75ec3d8dl } },
    /* 60 << 217 */
    { { 0x516c43aa5e1cd3cdl,0x893978089a887c28l,0x0059c699ddea1f9fl,
        0x7737d6fa8e6868f7l },
      { 0x6d93746a60f1524bl,0x36985e55ba052aa7l,0x41b1d322ed923ea5l,
        0x3429759f25852a11l } },
    /* 61 << 217 */
    { { 0xbeca6ec3092e9f41l,0x3a238c6662256bbdl,0xd82958ea70ad487dl,
        0x4ac8aaf965610d93l },
      { 0x3fa101b15e4ccab0l,0x9bf430f29de14bfbl,0xa10f5cc66531899dl,
        0x590005fbea8ce17dl } },
    /* 62 << 217 */
    { { 0xc437912f24544cb6l,0x9987b71ad79ac2e3l,0x13e3d9ddc058a212l,
        0x00075aacd2de9606l },
      { 0x80ab508b6cac8369l,0x87842be7f54f6c89l,0xa7ad663d6bc532a4l,
        0x67813de778a91bc8l } },
    /* 63 << 217 */
    { { 0x5dcb61cec3427239l,0x5f3c7cf0c56934d9l,0xc079e0fbe3191591l,
        0xe40896bdb01aada7l },
      { 0x8d4667910492d25fl,0x8aeb30c9e7408276l,0xe94374959287aaccl,
        0x23d4708d79fe03d4l } },
    /* 64 << 217 */
    { { 0x8cda9cf2d0c05199l,0x502fbc22fae78454l,0xc0bda9dff572a182l,
        0x5f9b71b86158b372l },
      { 0xe0f33a592b82dd07l,0x763027359523032el,0x7fe1a721c4505a32l,
        0x7b6e3e82f796409fl } },
    /* 0 << 224 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 224 */
    { { 0xe3417bc035d0b34al,0x440b386b8327c0a7l,0x8fb7262dac0362d1l,
        0x2c41114ce0cdf943l },
      { 0x2ba5cef1ad95a0b1l,0xc09b37a867d54362l,0x26d6cdd201e486c9l,
        0x20477abf42ff9297l } },
    /* 2 << 224 */
    { { 0xa004dcb3292a9287l,0xddc15cf677b092c7l,0x083a8464806c0605l,
        0x4a68df703db997b0l },
      { 0x9c134e4505bf7dd0l,0xa4e63d398ccf7f8cl,0xa6e6517f41b5f8afl,
        0xaa8b9342ad7bc1ccl } },
    /* 3 << 224 */
    { { 0x126f35b51e706ad9l,0xb99cebb4c3a9ebdfl,0xa75389afbf608d90l,
        0x76113c4fc6c89858l },
      { 0x80de8eb097e2b5aal,0x7e1022cc63b91304l,0x3bdab6056ccc066cl,
        0x33cbb144b2edf900l } },
    /* 4 << 224 */
    { { 0xc41764717af715d2l,0xe2f7f594d0134a96l,0x2c1873efa41ec956l,
        0xe4e7b4f677821304l },
      { 0xe5c8ff9788d5374al,0x2b915e6380823d5bl,0xea6bc755b2ee8fe2l,
        0x6657624ce7112651l } },
    /* 5 << 224 */
    { { 0x157af101dace5acal,0xc4fdbcf211a6a267l,0xdaddf340c49c8609l,
        0x97e49f52e9604a65l },
      { 0x9be8e790937e2ad5l,0x846e2508326e17f1l,0x3f38007a0bbbc0dcl,
        0xcf03603fb11e16d6l } },
    /* 6 << 224 */
    { { 0xd6f800e07442f1d5l,0x475607d166e0e3abl,0x82807f16b7c64047l,
        0x8858e1e3a749883dl },
      { 0x5859120b8231ee10l,0x1b80e7eb638a1ecel,0xcb72525ac6aa73a4l,
        0xa7cdea3d844423acl } },
    /* 7 << 224 */
    { { 0x5ed0c007f8ae7c38l,0x6db07a5c3d740192l,0xbe5e9c2a5fe36db3l,
        0xd5b9d57a76e95046l },
      { 0x54ac32e78eba20f2l,0xef11ca8f71b9a352l,0x305e373eff98a658l,
        0xffe5a100823eb667l } },
    /* 8 << 224 */
    { { 0x57477b11e51732d2l,0xdfd6eb282538fc0el,0x5c43b0cc3b39eec5l,
        0x6af12778cb36cc57l },
      { 0x70b0852d06c425ael,0x6df92f8c5c221b9bl,0x6c8d4f9ece826d9cl,
        0xf59aba7bb49359c3l } },
    /* 9 << 224 */
    { { 0x5c8ed8d5da64309dl,0x61a6de5691b30704l,0xd6b52f6a2f9b5808l,
        0x0eee419498c958a7l },
      { 0xcddd9aab771e4caal,0x83965dfd78bc21bel,0x02affce3b3b504f5l,
        0x30847a21561c8291l } },
    /* 10 << 224 */
    { { 0xd2eb2cf152bfda05l,0xe0e4c4e96197b98cl,0x1d35076cf8a1726fl,
        0x6c06085b2db11e3dl },
      { 0x15c0c4d74463ba14l,0x9d292f830030238cl,0x1311ee8b3727536dl,
        0xfeea86efbeaedc1el } },
    /* 11 << 224 */
    { { 0xb9d18cd366131e2el,0xf31d974f80fe2682l,0xb6e49e0fe4160289l,
        0x7c48ec0b08e92799l },
      { 0x818111d8d1989aa7l,0xb34fa0aaebf926f9l,0xdb5fe2f5a245474al,
        0xf80a6ebb3c7ca756l } },
    /* 12 << 224 */
    { { 0xa7f96054afa05dd8l,0x26dfcf21fcaf119el,0xe20ef2e30564bb59l,
        0xef4dca5061cb02b8l },
      { 0xcda7838a65d30672l,0x8b08d534fd657e86l,0x4c5b439546d595c8l,
        0x39b58725425cb836l } },
    /* 13 << 224 */
    { { 0x8ea610593de9abe3l,0x404348819cdc03bel,0x9b261245cfedce8cl,
        0x78c318b4cf5234a1l },
      { 0x510bcf16fde24c99l,0x2a77cb75a2c2ff5dl,0x9c895c2b27960fb4l,
        0xd30ce975b0eda42bl } },
    /* 14 << 224 */
    { { 0xfda853931a62cc26l,0x23c69b9650c0e052l,0xa227df15bfc633f3l,
        0x2ac788481bae7d48l },
      { 0x487878f9187d073dl,0x6c2be919967f807dl,0x765861d8336e6d8fl,
        0x88b8974cce528a43l } },
    /* 15 << 224 */
    { { 0x09521177ff57d051l,0x2ff38037fb6a1961l,0xfc0aba74a3d76ad4l,
        0x7c76480325a7ec17l },
      { 0x7532d75f48879bc8l,0xea7eacc058ce6bc1l,0xc82176b48e896c16l,
        0x9a30e0b22c750fedl } },
    /* 16 << 224 */
    { { 0xc37e2c2e421d3aa4l,0xf926407ce84fa840l,0x18abc03d1454e41cl,
        0x26605ecd3f7af644l },
      { 0x242341a6d6a5eabfl,0x1edb84f4216b668el,0xd836edb804010102l,
        0x5b337ce7945e1d8cl } },
    /* 17 << 224 */
    { { 0xd2075c77c055dc14l,0x2a0ffa2581d89cdfl,0x8ce815ea6ffdcbafl,
        0xa3428878fb648867l },
      { 0x277699cf884655fbl,0xfa5b5bd6364d3e41l,0x01f680c6441e1cb7l,
        0x3fd61e66b70a7d67l } },
    /* 18 << 224 */
    { { 0x666ba2dccc78cf66l,0xb30181746fdbff77l,0x8d4dd0db168d4668l,
        0x259455d01dab3a2al },
      { 0xf58564c5cde3acecl,0x7714192513adb276l,0x527d725d8a303f65l,
        0x55deb6c9e6f38f7bl } },
    /* 19 << 224 */
    { { 0xfd5bb657b1fa70fbl,0xfa07f50fd8073a00l,0xf72e3aa7bca02500l,
        0xf68f895d9975740dl },
      { 0x301120605cae2a6al,0x01bd721802874842l,0x3d4238917ce47bd3l,
        0xa66663c1789544f6l } },
    /* 20 << 224 */
    { { 0x864d05d73272d838l,0xe22924f9fa6295c5l,0x8189593f6c2fda32l,
        0x330d7189b184b544l },
      { 0x79efa62cbde1f714l,0x35771c94e5cb1a63l,0x2f4826b8641c8332l,
        0x00a894fbc8cee854l } },
    /* 21 << 224 */
    { { 0xb4b9a39b36194d40l,0xe857a7c577612601l,0xf4209dd24ecf2f58l,
        0x82b9e66d5a033487l },
      { 0xc1e36934e4e8b9ddl,0xd2372c9da42377d7l,0x51dc94c70e3ae43bl,
        0x4c57761e04474f6fl } },
    /* 22 << 224 */
    { { 0xdcdacd0a1058a318l,0x369cf3f578053a9al,0xc6c3de5031c68de2l,
        0x4653a5763c4b6d9fl },
      { 0x1688dd5aaa4e5c97l,0x5be80aa1b7ab3c74l,0x70cefe7cbc65c283l,
        0x57f95f1306867091l } },
    /* 23 << 224 */
    { { 0xa39114e24415503bl,0xc08ff7c64cbb17e9l,0x1eff674dd7dec966l,
        0x6d4690af53376f63l },
      { 0xff6fe32eea74237bl,0xc436d17ecd57508el,0x15aa28e1edcc40fel,
        0x0d769c04581bbb44l } },
    /* 24 << 224 */
    { { 0xc240b6de34eaacdal,0xd9e116e82ba0f1del,0xcbe45ec779438e55l,
        0x91787c9d96f752d7l },
      { 0x897f532bf129ac2fl,0xd307b7c85a36e22cl,0x91940675749fb8f3l,
        0xd14f95d0157fdb28l } },
    /* 25 << 224 */
    { { 0xfe51d0296ae55043l,0x8931e98f44a87de1l,0xe57f1cc609e4fee2l,
        0x0d063b674e072d92l },
      { 0x70a998b9ed0e4316l,0xe74a736b306aca46l,0xecf0fbf24fda97c7l,
        0xa40f65cb3e178d93l } },
    /* 26 << 224 */
    { { 0x1625360416df4285l,0xb0c9babbd0c56ae2l,0x73032b19cfc5cfc3l,
        0xe497e5c309752056l },
      { 0x12096bb4164bda96l,0x1ee42419a0b74da1l,0x8fc36243403826bal,
        0x0c8f0069dc09e660l } },
    /* 27 << 224 */
    { { 0x8667e981c27253c9l,0x05a6aefb92b36a45l,0xa62c4b369cb7bb46l,
        0x8394f37511f7027bl },
      { 0x747bc79c5f109d0fl,0xcad88a765b8cc60al,0x80c5a66b58f09e68l,
        0xe753d451f6127eacl } },
    /* 28 << 224 */
    { { 0xc44b74a15b0ec6f5l,0x47989fe45289b2b8l,0x745f848458d6fc73l,
        0xec362a6ff61c70abl },
      { 0x070c98a7b3a8ad41l,0x73a20fc07b63db51l,0xed2c2173f44c35f4l,
        0x8a56149d9acc9dcal } },
    /* 29 << 224 */
    { { 0x98f178819ac6e0f4l,0x360fdeafa413b5edl,0x0625b8f4a300b0fdl,
        0xf1f4d76a5b3222d3l },
      { 0x9d6f5109587f76b8l,0x8b4ee08d2317fdb5l,0x88089bb78c68b095l,
        0x95570e9a5808d9b9l } },
    /* 30 << 224 */
    { { 0xa395c36f35d33ae7l,0x200ea12350bb5a94l,0x20c789bd0bafe84bl,
        0x243ef52d0919276al },
      { 0x3934c577e23ae233l,0xb93807afa460d1ecl,0xb72a53b1f8fa76a4l,
        0xd8914cb0c3ca4491l } },
    /* 31 << 224 */
    { { 0x2e1284943fb42622l,0x3b2700ac500907d5l,0xf370fb091a95ec63l,
        0xf8f30be231b6dfbdl },
      { 0xf2b2f8d269e55f15l,0x1fead851cc1323e9l,0xfa366010d9e5eef6l,
        0x64d487b0e316107el } },
    /* 32 << 224 */
    { { 0x4c076b86d23ddc82l,0x03fd344c7e0143f0l,0xa95362ff317af2c5l,
        0x0add3db7e18b7a4fl },
      { 0x9c673e3f8260e01bl,0xfbeb49e554a1cc91l,0x91351bf292f2e433l,
        0xc755e7ec851141ebl } },
    /* 33 << 224 */
    { { 0xc9a9513929607745l,0x0ca07420a26f2b28l,0xcb2790e74bc6f9ddl,
        0x345bbb58adcaffc0l },
      { 0xc65ea38cbe0f27a2l,0x67c24d7c641fcb56l,0x2c25f0a7a9e2c757l,
        0x93f5cdb016f16c49l } },
    /* 34 << 224 */
    { { 0x2ca5a9d7c5ee30a1l,0xd1593635b909b729l,0x804ce9f3dadeff48l,
        0xec464751b07c30c3l },
      { 0x89d65ff39e49af6al,0xf2d6238a6f3d01bcl,0x1095561e0bced843l,
        0x51789e12c8a13fd8l } },
    /* 35 << 224 */
    { { 0xd633f929763231dfl,0x46df9f7de7cbddefl,0x01c889c0cb265da8l,
        0xfce1ad10af4336d2l },
      { 0x8d110df6fc6a0a7el,0xdd431b986da425dcl,0xcdc4aeab1834aabel,
        0x84deb1248439b7fcl } },
    /* 36 << 224 */
    { { 0x8796f1693c2a5998l,0x9b9247b47947190dl,0x55b9d9a511597014l,
        0x7e9dd70d7b1566eel },
      { 0x94ad78f7cbcd5e64l,0x0359ac179bd4c032l,0x3b11baaf7cc222ael,
        0xa6a6e284ba78e812l } },
    /* 37 << 224 */
    { { 0x8392053f24cea1a0l,0xc97bce4a33621491l,0x7eb1db3435399ee9l,
        0x473f78efece81ad1l },
      { 0x41d72fe0f63d3d0dl,0xe620b880afab62fcl,0x92096bc993158383l,
        0x41a213578f896f6cl } },
    /* 38 << 224 */
    { { 0x1b5ee2fac7dcfcabl,0x650acfde9546e007l,0xc081b749b1b02e07l,
        0xda9e41a0f9eca03dl },
      { 0x013ba727175a54abl,0xca0cd190ea5d8d10l,0x85ea52c095fd96a9l,
        0x2c591b9fbc5c3940l } },
    /* 39 << 224 */
    { { 0x6fb4d4e42bad4d5fl,0xfa4c3590fef0059bl,0x6a10218af5122294l,
        0x9a78a81aa85751d1l },
      { 0x04f20579a98e84e7l,0xfe1242c04997e5b5l,0xe77a273bca21e1e4l,
        0xfcc8b1ef9411939dl } },
    /* 40 << 224 */
    { { 0xe20ea30292d0487al,0x1442dbec294b91fel,0x1f7a4afebb6b0e8fl,
        0x1700ef746889c318l },
      { 0xf5bbffc370f1fc62l,0x3b31d4b669c79ccal,0xe8bc2aaba7f6340dl,
        0xb0b08ab4a725e10al } },
    /* 41 << 224 */
    { { 0x44f05701ae340050l,0xba4b30161cf0c569l,0x5aa29f83fbe19a51l,
        0x1b9ed428b71d752el },
      { 0x1666e54eeb4819f5l,0x616cdfed9e18b75bl,0x112ed5be3ee27b0bl,
        0xfbf2831944c7de4dl } },
    /* 42 << 224 */
    { { 0xd685ec85e0e60d84l,0x68037e301db7ee78l,0x5b65bdcd003c4d6el,
        0x33e7363a93e29a6al },
      { 0x995b3a6108d0756cl,0xd727f85c2faf134bl,0xfac6edf71d337823l,
        0x99b9aa500439b8b4l } },
    /* 43 << 224 */
    { { 0x722eb104e2b4e075l,0x49987295437c4926l,0xb1e4c0e446a9b82dl,
        0xd0cb319757a006f5l },
      { 0xf3de0f7dd7808c56l,0xb5c54d8f51f89772l,0x500a114aadbd31aal,
        0x9afaaaa6295f6cabl } },
    /* 44 << 224 */
    { { 0x94705e2104cf667al,0xfc2a811b9d3935d7l,0x560b02806d09267cl,
        0xf19ed119f780e53bl },
      { 0xf0227c09067b6269l,0x967b85335caef599l,0x155b924368efeebcl,
        0xcd6d34f5c497bae6l } },
    /* 45 << 224 */
    { { 0x1dd8d5d36cceb370l,0x2aeac579a78d7bf9l,0x5d65017d70b67a62l,
        0x70c8e44f17c53f67l },
      { 0xd1fc095086a34d09l,0xe0fca256e7134907l,0xe24fa29c80fdd315l,
        0x2c4acd03d87499adl } },
    /* 46 << 224 */
    { { 0xbaaf75173b5a9ba6l,0xb9cbe1f612e51a51l,0xd88edae35e154897l,
        0xe4309c3c77b66ca0l },
      { 0xf5555805f67f3746l,0x85fc37baa36401ffl,0xdf86e2cad9499a53l,
        0x6270b2a3ecbc955bl } },
    /* 47 << 224 */
    { { 0xafae64f5974ad33bl,0x04d85977fe7b2df1l,0x2a3db3ff4ab03f73l,
        0x0b87878a8702740al },
      { 0x6d263f015a061732l,0xc25430cea32a1901l,0xf7ebab3ddb155018l,
        0x3a86f69363a9b78el } },
    /* 48 << 224 */
    { { 0x349ae368da9f3804l,0x470f07fea164349cl,0xd52f4cc98562baa5l,
        0xc74a9e862b290df3l },
      { 0xd3a1aa3543471a24l,0x239446beb8194511l,0xbec2dd0081dcd44dl,
        0xca3d7f0fc42ac82dl } },
    /* 49 << 224 */
    { { 0x1f3db085fdaf4520l,0xbb6d3e804549daf2l,0xf5969d8a19ad5c42l,
        0x7052b13ddbfd1511l },
      { 0x11890d1b682b9060l,0xa71d3883ac34452cl,0xa438055b783805b4l,
        0x432412774725b23el } },
    /* 50 << 224 */
    { { 0xf20cf96e4901bbedl,0x6419c710f432a2bbl,0x57a0fbb9dfa9cd7dl,
        0x589111e400daa249l },
      { 0x19809a337b60554el,0xea5f8887ede283a4l,0x2d713802503bfd35l,
        0x151bb0af585d2a53l } },
    /* 51 << 224 */
    { { 0x40b08f7443b30ca8l,0xe10b5bbad9934583l,0xe8a546d6b51110adl,
        0x1dd50e6628e0b6c5l },
      { 0x292e9d54cff2b821l,0x3882555d47281760l,0x134838f83724d6e3l,
        0xf2c679e022ddcda1l } },
    /* 52 << 224 */
    { { 0x40ee88156d2a5768l,0x7f227bd21c1e7e2dl,0x487ba134d04ff443l,
        0x76e2ff3dc614e54bl },
      { 0x36b88d6fa3177ec7l,0xbf731d512328fff5l,0x758caea249ba158el,
        0x5ab8ff4c02938188l } },
    /* 53 << 224 */
    { { 0x33e1605635edc56dl,0x5a69d3497e940d79l,0x6c4fd00103866dcbl,
        0x20a38f574893cdefl },
      { 0xfbf3e790fac3a15bl,0x6ed7ea2e7a4f8e6bl,0xa663eb4fbc3aca86l,
        0x22061ea5080d53f7l } },
    /* 54 << 224 */
    { { 0x2480dfe6f546783fl,0xd38bc6da5a0a641el,0xfb093cd12ede8965l,
        0x89654db4acb455cfl },
      { 0x413cbf9a26e1adeel,0x291f3764373294d4l,0x00797257648083fel,
        0x25f504d3208cc341l } },
    /* 55 << 224 */
    { { 0x635a8e5ec3a0ee43l,0x70aaebca679898ffl,0x9ee9f5475dc63d56l,
        0xce987966ffb34d00l },
      { 0xf9f86b195e26310al,0x9e435484382a8ca8l,0x253bcb81c2352fe4l,
        0xa4eac8b04474b571l } },
    /* 56 << 224 */
    { { 0xc1b97512c1ad8cf8l,0x193b4e9e99e0b697l,0x939d271601e85df0l,
        0x4fb265b3cd44eafdl },
      { 0x321e7dcde51e1ae2l,0x8e3a8ca6e3d8b096l,0x8de46cb052604998l,
        0x91099ad839072aa7l } },
    /* 57 << 224 */
    { { 0x2617f91c93aa96b8l,0x0fc8716b7fca2e13l,0xa7106f5e95328723l,
        0xd1c9c40b262e6522l },
      { 0xb9bafe8642b7c094l,0x1873439d1543c021l,0xe1baa5de5cbefd5dl,
        0xa363fc5e521e8affl } },
    /* 58 << 224 */
    { { 0xefe6320df862eaacl,0x14419c6322c647dcl,0x0e06707c4e46d428l,
        0xcb6c834f4a178f8fl },
      { 0x0f993a45d30f917cl,0xd4c4b0499879afeel,0xb6142a1e70500063l,
        0x7c9b41c3a5d9d605l } },
    /* 59 << 224 */
    { { 0xbc00fc2f2f8ba2c7l,0x0966eb2f7c67aa28l,0x13f7b5165a786972l,
        0x3bfb75578a2fbba0l },
      { 0x131c4f235a2b9620l,0xbff3ed276faf46bel,0x9b4473d17e172323l,
        0x421e8878339f6246l } },
    /* 60 << 224 */
    { { 0x0fa8587a25a41632l,0xc0814124a35b6c93l,0x2b18a9f559ebb8dbl,
        0x264e335776edb29cl },
      { 0xaf245ccdc87c51e2l,0x16b3015b501e6214l,0xbb31c5600a3882cel,
        0x6961bb94fec11e04l } },
    /* 61 << 224 */
    { { 0x3b825b8deff7a3a0l,0xbec33738b1df7326l,0x68ad747c99604a1fl,
        0xd154c9349a3bd499l },
      { 0xac33506f1cc7a906l,0x73bb53926c560e8fl,0x6428fcbe263e3944l,
        0xc11828d51c387434l } },
    /* 62 << 224 */
    { { 0x3cd04be13e4b12ffl,0xc3aad9f92d88667cl,0xc52ddcf8248120cfl,
        0x985a892e2a389532l },
      { 0xfbb4b21b3bb85fa0l,0xf95375e08dfc6269l,0xfb4fb06c7ee2aceal,
        0x6785426e309c4d1fl } },
    /* 63 << 224 */
    { { 0x659b17c8d8ceb147l,0x9b649eeeb70a5554l,0x6b7fa0b5ac6bc634l,
        0xd99fe2c71d6e732fl },
      { 0x30e6e7628d3abba2l,0x18fee6e7a797b799l,0x5c9d360dc696464dl,
        0xe3baeb4827bfde12l } },
    /* 64 << 224 */
    { { 0x2bf5db47f23206d5l,0x2f6d34201d260152l,0x17b876533f8ff89al,
        0x5157c30c378fa458l },
      { 0x7517c5c52d4fb936l,0xef22f7ace6518cdcl,0xdeb483e6bf847a64l,
        0xf508455892e0fa89l } },
    /* 0 << 231 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 231 */
    { { 0xab9659d8df7304d4l,0xb71bcf1bff210e8el,0xa9a2438bd73fbd60l,
        0x4595cd1f5d11b4del },
      { 0x9c0d329a4835859dl,0x4a0f0d2d7dbb6e56l,0xc6038e5edf928a4el,
        0xc94296218f5ad154l } },
    /* 2 << 231 */
    { { 0x91213462f23f2d92l,0x6cab71bd60b94078l,0x6bdd0a63176cde20l,
        0x54c9b20cee4d54bcl },
      { 0x3cd2d8aa9f2ac02fl,0x03f8e617206eedb0l,0xc7f68e1693086434l,
        0x831469c592dd3db9l } },
    /* 3 << 231 */
    { { 0x8521df248f981354l,0x587e23ec3588a259l,0xcbedf281d7a0992cl,
        0x06930a5538961407l },
      { 0x09320debbe5bbe21l,0xa7ffa5b52491817fl,0xe6c8b4d909065160l,
        0xac4f3992fff6d2a9l } },
    /* 4 << 231 */
    { { 0x7aa7a1583ae9c1bdl,0xe0af6d98e37ce240l,0xe54342d928ab38b4l,
        0xe8b750070a1c98cal },
      { 0xefce86afe02358f2l,0x31b8b856ea921228l,0x052a19120a1c67fcl,
        0xb4069ea4e3aead59l } },
    /* 5 << 231 */
    { { 0x3232d6e27fa03cb3l,0xdb938e5b0fdd7d88l,0x04c1d2cd2ccbfc5dl,
        0xd2f45c12af3a580fl },
      { 0x592620b57883e614l,0x5fd27e68be7c5f26l,0x139e45a91567e1e3l,
        0x2cc71d2d44d8aaafl } },
    /* 6 << 231 */
    { { 0x4a9090cde36d0757l,0xf722d7b1d9a29382l,0xfb7fb04c04b48ddfl,
        0x628ad2a7ebe16f43l },
      { 0xcd3fbfb520226040l,0x6c34ecb15104b6c4l,0x30c0754ec903c188l,
        0xec336b082d23cab0l } },
    /* 7 << 231 */
    { { 0x473d62a21e206ee5l,0xf1e274808c49a633l,0x87ab956ce9f6b2c3l,
        0x61830b4862b606eal },
      { 0x67cd6846e78e815fl,0xfe40139f4c02082al,0x52bbbfcb952ec365l,
        0x74c116426b9836abl } },
    /* 8 << 231 */
    { { 0x9f51439e558df019l,0x230da4baac712b27l,0x518919e355185a24l,
        0x4dcefcdd84b78f50l },
      { 0xa7d90fb2a47d4c5al,0x55ac9abfb30e009el,0xfd2fc35974eed273l,
        0xb72d824cdbea8fafl } },
    /* 9 << 231 */
    { { 0xce721a744513e2cal,0x0b41861238240b2cl,0x05199968d5baa450l,
        0xeb1757ed2b0e8c25l },
      { 0x6ebc3e283dfac6d5l,0xb2431e2e48a237f5l,0x2acb5e2352f61499l,
        0x5558a2a7e06c936bl } },
    /* 10 << 231 */
    { { 0xd213f923cbb13d1bl,0x98799f425bfb9bfel,0x1ae8ddc9701144a9l,
        0x0b8b3bb64c5595eel },
      { 0x0ea9ef2e3ecebb21l,0x17cb6c4b3671f9a7l,0x47ef464f726f1d1fl,
        0x171b94846943a276l } },
    /* 11 << 231 */
    { { 0x51a4ae2d7ef0329cl,0x0850922291c4402al,0x64a61d35afd45bbcl,
        0x38f096fe3035a851l },
      { 0xc7468b74a1dec027l,0xe8cf10e74fc7dcbal,0xea35ff40f4a06353l,
        0x0b4c0dfa8b77dd66l } },
    /* 12 << 231 */
    { { 0x779b8552de7e5c19l,0xfab28609c1c0256cl,0x64f58eeeabd4743dl,
        0x4e8ef8387b6cc93bl },
      { 0xee650d264cb1bf3dl,0x4c1f9d0973dedf61l,0xaef7c9d7bfb70cedl,
        0x1ec0507e1641de1el } },
    /* 13 << 231 */
    { { 0xcd7e5cc7cde45079l,0xde173c9a516ac9e4l,0x517a8494c170315cl,
        0x438fd90591d8e8fbl },
      { 0x5145c506c7d9630bl,0x6457a87bf47d4d75l,0xd31646bf0d9a80e8l,
        0x453add2bcef3aabel } },
    /* 14 << 231 */
    { { 0xc9941109a607419dl,0xfaa71e62bb6bca80l,0x34158c1307c431f3l,
        0x594abebc992bc47al },
      { 0x6dfea691eb78399fl,0x48aafb353f42cba4l,0xedcd65af077c04f0l,
        0x1a29a366e884491al } },
    /* 15 << 231 */
    { { 0x023a40e51c21f2bfl,0xf99a513ca5057aeel,0xa3fe7e25bcab072el,
        0x8568d2e140e32bcfl },
      { 0x904594ebd3f69d9fl,0x181a973307affab1l,0xe4d68d76b6e330f4l,
        0x87a6dafbc75a7fc1l } },
    /* 16 << 231 */
    { { 0x549db2b5ef7d9289l,0x2480d4a8197f015al,0x61d5590bc40493b6l,
        0x3a55b52e6f780331l },
      { 0x40eb8115309eadb0l,0xdea7de5a92e5c625l,0x64d631f0cc6a3d5al,
        0x9d5e9d7c93e8dd61l } },
    /* 17 << 231 */
    { { 0xf297bef5206d3ffcl,0x23d5e0337d808bd4l,0x4a4f6912d24cf5bal,
        0xe4d8163b09cdaa8al },
      { 0x0e0de9efd3082e8el,0x4fe1246c0192f360l,0x1f9001504b8eee0al,
        0x5219da81f1da391bl } },
    /* 18 << 231 */
    { { 0x7bf6a5c1f7ea25aal,0xd165e6bffbb07d5fl,0xe353936189e78671l,
        0xa3fcac892bac4219l },
      { 0xdfab6fd4f0baa8abl,0x5a4adac1e2c1c2e5l,0x6cd75e3140d85849l,
        0xce263fea19b39181l } },
    /* 19 << 231 */
    { { 0xcb6803d307032c72l,0x7f40d5ce790968c8l,0xa6de86bddce978f0l,
        0x25547c4f368f751cl },
      { 0xb1e685fd65fb2a9el,0xce69336f1eb9179cl,0xb15d1c2712504442l,
        0xb7df465cb911a06bl } },
    /* 20 << 231 */
    { { 0xb8d804a3315980cdl,0x693bc492fa3bebf7l,0x3578aeee2253c504l,
        0x158de498cd2474a2l },
      { 0x1331f5c7cfda8368l,0xd2d7bbb378d7177el,0xdf61133af3c1e46el,
        0x5836ce7dd30e7be8l } },
    /* 21 << 231 */
    { { 0x83084f1994f834cbl,0xd35653d4429ed782l,0xa542f16f59e58243l,
        0xc2b52f650470a22dl },
      { 0xe3b6221b18f23d96l,0xcb05abac3f5252b4l,0xca00938b87d61402l,
        0x2f186cdd411933e4l } },
    /* 22 << 231 */
    { { 0xe042ece59a29a5c5l,0xb19b3c073b6c8402l,0xc97667c719d92684l,
        0xb5624622ebc66372l },
      { 0x0cb96e653c04fa02l,0x83a7176c8eaa39aal,0x2033561deaa1633fl,
        0x45a9d0864533df73l } },
    /* 23 << 231 */
    { { 0xe0542c1d3dc090bcl,0x82c996efaa59c167l,0xe3f735e80ee7fc4dl,
        0x7b1793937c35db79l },
      { 0xb6419e25f8c5dbfdl,0x4d9d7a1e1f327b04l,0x979f6f9b298dfca8l,
        0xc7c5dff18de9366al } },
    /* 24 << 231 */
    { { 0x1b7a588d04c82bddl,0x68005534f8319dfdl,0xde8a55b5d8eb9580l,
        0x5ea886da8d5bca81l },
      { 0xe8530a01252a0b4dl,0x1bffb4fe35eaa0a1l,0x2ad828b1d8e99563l,
        0x7de96ef595f9cd87l } },
    /* 25 << 231 */
    { { 0x4abb2d0cd77d970cl,0x03cfb933d33ef9cbl,0xb0547c018b211fe9l,
        0x2fe64809a56ed1c6l },
      { 0xcb7d5624c2ac98ccl,0x2a1372c01a393e33l,0xc8d1ec1c29660521l,
        0xf3d31b04b37ac3e9l } },
    /* 26 << 231 */
    { { 0xa29ae9df5ece6e7cl,0x0603ac8f0facfb55l,0xcfe85b7adda233a5l,
        0xe618919fbd75f0b8l },
      { 0xf555a3d299bf1603l,0x1f43afc9f184255al,0xdcdaf341319a3e02l,
        0xd3b117ef03903a39l } },
    /* 27 << 231 */
    { { 0xe095da1365d1d131l,0x86f16367c37ad03el,0x5f37389e462cd8ddl,
        0xc103fa04d67a60e6l },
      { 0x57c34344f4b478f0l,0xce91edd8e117c98dl,0x001777b0231fc12el,
        0x11ae47f2b207bccbl } },
    /* 28 << 231 */
    { { 0xd983cf8d20f8a242l,0x7aff5b1df22e1ad8l,0x68fd11d07fc4feb3l,
        0x5d53ae90b0f1c3e1l },
      { 0x50fb7905ec041803l,0x85e3c97714404888l,0x0e67faedac628d8fl,
        0x2e8651506668532cl } },
    /* 29 << 231 */
    { { 0x15acaaa46a67a6b0l,0xf4cdee25b25cec41l,0x49ee565ae4c6701el,
        0x2a04ca66fc7d63d8l },
      { 0xeb105018ef0543fbl,0xf709a4f5d1b0d81dl,0x5b906ee62915d333l,
        0xf4a8741296f1f0abl } },
    /* 30 << 231 */
    { { 0xb6b82fa74d82f4c2l,0x90725a606804efb3l,0xbc82ec46adc3425el,
        0xb7b805812787843el },
      { 0xdf46d91cdd1fc74cl,0xdc1c62cbe783a6c4l,0x59d1b9f31a04cbbal,
        0xd87f6f7295e40764l } },
    /* 31 << 231 */
    { { 0x02b4cfc1317f4a76l,0x8d2703eb91036bcel,0x98206cc6a5e72a56l,
        0x57be9ed1cf53fb0fl },
      { 0x09374571ef0b17acl,0x74b2655ed9181b38l,0xc8f80ea889935d0el,
        0xc0d9e94291529936l } },
    /* 32 << 231 */
    { { 0x196860411e84e0e5l,0xa5db84d3aea34c93l,0xf9d5bb197073a732l,
        0xb8d2fe566bcfd7c0l },
      { 0x45775f36f3eb82fal,0x8cb20cccfdff8b58l,0x1659b65f8374c110l,
        0xb8b4a422330c789al } },
    /* 33 << 231 */
    { { 0x75e3c3ea6fe8208bl,0xbd74b9e4286e78fel,0x0be2e81bd7d93a1al,
        0x7ed06e27dd0a5aael },
      { 0x721f5a586be8b800l,0x428299d1d846db28l,0x95cb8e6b5be88ed3l,
        0xc3186b231c034e11l } },
    /* 34 << 231 */
    { { 0xa6312c9e8977d99bl,0xbe94433183f531e7l,0x8232c0c218d3b1d4l,
        0x617aae8be1247b73l },
      { 0x40153fc4282aec3bl,0xc6063d2ff7b8f823l,0x68f10e583304f94cl,
        0x31efae74ee676346l } },
    /* 35 << 231 */
    { { 0xbadb6c6d40a9b97cl,0x14702c634f666256l,0xdeb954f15184b2e3l,
        0x5184a52694b6ca40l },
      { 0xfff05337003c32eal,0x5aa374dd205974c7l,0x9a7638544b0dd71al,
        0x459cd27fdeb947ecl } },
    /* 36 << 231 */
    { { 0xa6e28161459c2b92l,0x2f020fa875ee8ef5l,0xb132ec2d30b06310l,
        0xc3e15899bc6a4530l },
      { 0xdc5f53feaa3f451al,0x3a3c7f23c2d9acacl,0x2ec2f8926b27e58bl,
        0x68466ee7d742799fl } },
    /* 37 << 231 */
    { { 0x98324dd41fa26613l,0xa2dc6dabbdc29d63l,0xf9675faad712d657l,
        0x813994be21fd8d15l },
      { 0x5ccbb722fd4f7553l,0x5135ff8bf3a36b20l,0x44be28af69559df5l,
        0x40b65bed9d41bf30l } },
    /* 38 << 231 */
    { { 0xd98bf2a43734e520l,0x5e3abbe3209bdcbal,0x77c76553bc945b35l,
        0x5331c093c6ef14aal },
      { 0x518ffe2976b60c80l,0x2285593b7ace16f8l,0xab1f64ccbe2b9784l,
        0xe8f2c0d9ab2421b6l } },
    /* 39 << 231 */
    { { 0x617d7174c1df065cl,0xafeeb5ab5f6578fal,0x16ff1329263b54a8l,
        0x45c55808c990dce3l },
      { 0x42eab6c0ecc8c177l,0x799ea9b55982ecaal,0xf65da244b607ef8el,
        0x8ab226ce32a3fc2cl } },
    /* 40 << 231 */
    { { 0x745741e57ea973dcl,0x5c00ca7020888f2el,0x7cdce3cf45fd9cf1l,
        0x8a741ef15507f872l },
      { 0x47c51c2f196b4cecl,0x70d08e43c97ea618l,0x930da15c15b18a2bl,
        0x33b6c6782f610514l } },
    /* 41 << 231 */
    { { 0xc662e4f807ac9794l,0x1eccf050ba06cb79l,0x1ff08623e7d954e5l,
        0x6ef2c5fb24cf71c3l },
      { 0xb2c063d267978453l,0xa0cf37961d654af8l,0x7cb242ea7ebdaa37l,
        0x206e0b10b86747e0l } },
    /* 42 << 231 */
    { { 0x481dae5fd5ecfefcl,0x07084fd8c2bff8fcl,0x8040a01aea324596l,
        0x4c646980d4de4036l },
      { 0x9eb8ab4ed65abfc3l,0xe01cb91f13541ec7l,0x8f029adbfd695012l,
        0x9ae284833c7569ecl } },
    /* 43 << 231 */
    { { 0xa5614c9ea66d80a1l,0x680a3e4475f5f911l,0x0c07b14dceba4fc1l,
        0x891c285ba13071c1l },
      { 0xcac67ceb799ece3cl,0x29b910a941e07e27l,0x66bdb409f2e43123l,
        0x06f8b1377ac9ecbel } },
    /* 44 << 231 */
    { { 0x5981fafd38547090l,0x19ab8b9f85e3415dl,0xfc28c194c7e31b27l,
        0x843be0aa6fbcbb42l },
      { 0xf3b1ed43a6db836cl,0x2a1330e401a45c05l,0x4f19f3c595c1a377l,
        0xa85f39d044b5ee33l } },
    /* 45 << 231 */
    { { 0x3da18e6d4ae52834l,0x5a403b397423dcb0l,0xbb555e0af2374aefl,
        0x2ad599c41e8ca111l },
      { 0x1b3a2fb9014b3bf8l,0x73092684f66d5007l,0x079f1426c4340102l,
        0x1827cf818fddf4del } },
    /* 46 << 231 */
    { { 0xc83605f6f10ff927l,0xd387145123739fc6l,0x6d163450cac1c2ccl,
        0x6b521296a2ec1ac5l },
      { 0x0606c4f96e3cb4a5l,0xe47d3f41778abff7l,0x425a8d5ebe8e3a45l,
        0x53ea9e97a6102160l } },
    /* 47 << 231 */
    { { 0x477a106e39cbb688l,0x532401d2f3386d32l,0x8e564f64b1b9b421l,
        0xca9b838881dad33fl },
      { 0xb1422b4e2093913el,0x533d2f9269bc8112l,0x3fa017beebe7b2c7l,
        0xb2767c4acaf197c6l } },
    /* 48 << 231 */
    { { 0xc925ff87aedbae9fl,0x7daf0eb936880a54l,0x9284ddf59c4d0e71l,
        0x1581cf93316f8cf5l },
      { 0x3eeca8873ac1f452l,0xb417fce9fb6aeffel,0xa5918046eefb8dc3l,
        0x73d318ac02209400l } },
    /* 49 << 231 */
    { { 0xe800400f728693e5l,0xe87d814b339927edl,0x93e94d3b57ea9910l,
        0xff8a35b62245fb69l },
      { 0x043853d77f200d34l,0x470f1e680f653ce1l,0x81ac05bd59a06379l,
        0xa14052c203930c29l } },
    /* 50 << 231 */
    { { 0x6b72fab526bc2797l,0x13670d1699f16771l,0x001700521e3e48d1l,
        0x978fe401b7adf678l },
      { 0x55ecfb92d41c5dd4l,0x5ff8e247c7b27da5l,0xe7518272013fb606l,
        0x5768d7e52f547a3cl } },
    /* 51 << 231 */
    { { 0xbb24eaa360017a5fl,0x6b18e6e49c64ce9bl,0xc225c655103dde07l,
        0xfc3672ae7592f7eal },
      { 0x9606ad77d06283a1l,0x542fc650e4d59d99l,0xabb57c492a40e7c2l,
        0xac948f13a8db9f55l } },
    /* 52 << 231 */
    { { 0x6d4c9682b04465c3l,0xe3d062fa6468bd15l,0xa51729ac5f318d7el,
        0x1fc87df69eb6fc95l },
      { 0x63d146a80591f652l,0xa861b8f7589621aal,0x59f5f15ace31348cl,
        0x8f663391440da6dal } },
    /* 53 << 231 */
    { { 0xcfa778acb591ffa3l,0x027ca9c54cdfebcel,0xbe8e05a5444ea6b3l,
        0x8aab4e69a78d8254l },
      { 0x2437f04fb474d6b8l,0x6597ffd4045b3855l,0xbb0aea4eca47ecaal,
        0x568aae8385c7ebfcl } },
    /* 54 << 231 */
    { { 0x0e966e64c73b2383l,0x49eb3447d17d8762l,0xde1078218da05dabl,
        0x443d8baa016b7236l },
      { 0x163b63a5ea7610d6l,0xe47e4185ce1ca979l,0xae648b6580baa132l,
        0xebf53de20e0d5b64l } },
    /* 55 << 231 */
    { { 0x8d3bfcb4d3c8c1cal,0x0d914ef35d04b309l,0x55ef64153de7d395l,
        0xbde1666f26b850e8l },
      { 0xdbe1ca6ed449ab19l,0x8902b322e89a2672l,0xb1674b7edacb7a53l,
        0x8e9faf6ef52523ffl } },
    /* 56 << 231 */
    { { 0x6ba535da9a85788bl,0xd21f03aebd0626d4l,0x099f8c47e873dc64l,
        0xcda8564d018ec97el },
      { 0x3e8d7a5cde92c68cl,0x78e035a173323cc4l,0x3ef26275f880ff7cl,
        0xa4ee3dff273eedaal } },
    /* 57 << 231 */
    { { 0x58823507af4e18f8l,0x967ec9b50672f328l,0x9ded19d9559d3186l,
        0x5e2ab3de6cdce39cl },
      { 0xabad6e4d11c226dfl,0xf9783f4387723014l,0x9a49a0cf1a885719l,
        0xfc0c1a5a90da9dbfl } },
    /* 58 << 231 */
    { { 0x8bbaec49571d92acl,0x569e85fe4692517fl,0x8333b014a14ea4afl,
        0x32f2a62f12e5c5adl },
      { 0x98c2ce3a06d89b85l,0xb90741aa2ff77a08l,0x2530defc01f795a2l,
        0xd6e5ba0b84b3c199l } },
    /* 59 << 231 */
    { { 0x7d8e845112e4c936l,0xae419f7dbd0be17bl,0xa583fc8c22262bc9l,
        0x6b842ac791bfe2bdl },
      { 0x33cef4e9440d6827l,0x5f69f4deef81fb14l,0xf16cf6f6234fbb92l,
        0x76ae3fc3d9e7e158l } },
    /* 60 << 231 */
    { { 0x4e89f6c2e9740b33l,0x677bc85d4962d6a1l,0x6c6d8a7f68d10d15l,
        0x5f9a72240257b1cdl },
      { 0x7096b9164ad85961l,0x5f8c47f7e657ab4al,0xde57d7d0f7461d7el,
        0x7eb6094d80ce5ee2l } },
    /* 61 << 231 */
    { { 0x0b1e1dfd34190547l,0x8a394f43f05dd150l,0x0a9eb24d97df44e6l,
        0x78ca06bf87675719l },
      { 0x6f0b34626ffeec22l,0x9d91bcea36cdd8fbl,0xac83363ca105be47l,
        0x81ba76c1069710e3l } },
    /* 62 << 231 */
    { { 0x3d1b24cb28c682c6l,0x27f252288612575bl,0xb587c779e8e66e98l,
        0x7b0c03e9405eb1fel },
      { 0xfdf0d03015b548e7l,0xa8be76e038b36af7l,0x4cdab04a4f310c40l,
        0x6287223ef47ecaecl } },
    /* 63 << 231 */
    { { 0x678e60558b399320l,0x61fe3fa6c01e4646l,0xc482866b03261a5el,
        0xdfcf45b85c2f244al },
      { 0x8fab9a512f684b43l,0xf796c654c7220a66l,0x1d90707ef5afa58fl,
        0x2c421d974fdbe0del } },
    /* 64 << 231 */
    { { 0xc4f4cda3af2ebc2fl,0xa0af843dcb4efe24l,0x53b857c19ccd10b1l,
        0xddc9d1eb914d3e04l },
      { 0x7bdec8bb62771debl,0x829277aa91c5aa81l,0x7af18dd6832391ael,
        0x1740f316c71a84cal } },
    /* 0 << 238 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 238 */
    { { 0x8928e99aeeaf8c49l,0xee7aa73d6e24d728l,0x4c5007c2e72b156cl,
        0x5fcf57c5ed408a1dl },
      { 0x9f719e39b6057604l,0x7d343c01c2868bbfl,0x2cca254b7e103e2dl,
        0xe6eb38a9f131bea2l } },
    /* 2 << 238 */
    { { 0xb33e624f8be762b4l,0x2a9ee4d1058e3413l,0x968e636967d805fal,
        0x9848949b7db8bfd7l },
      { 0x5308d7e5d23a8417l,0x892f3b1df3e29da5l,0xc95c139e3dee471fl,
        0x8631594dd757e089l } },
    /* 3 << 238 */
    { { 0xe0c82a3cde918dccl,0x2e7b599426fdcf4bl,0x82c5024932cb1b2dl,
        0xea613a9d7657ae07l },
      { 0xc2eb5f6cf1fdc9f7l,0xb6eae8b8879fe682l,0x253dfee0591cbc7fl,
        0x000da7133e1290e6l } },
    /* 4 << 238 */
    { { 0x1083e2ea1f095615l,0x0a28ad7714e68c33l,0x6bfc02523d8818bel,
        0xb585113af35850cdl },
      { 0x7d935f0b30df8aa1l,0xaddda07c4ab7e3acl,0x92c34299552f00cbl,
        0xc33ed1de2909df6cl } },
    /* 5 << 238 */
    { { 0x22c2195d80e87766l,0x9e99e6d89ddf4ac0l,0x09642e4e65e74934l,
        0x2610ffa2ff1ff241l },
      { 0x4d1d47d4751c8159l,0x697b4985af3a9363l,0x0318ca4687477c33l,
        0xa90cb5659441eff3l } },
    /* 6 << 238 */
    { { 0x58bb384836f024cbl,0x85be1f7736016168l,0x6c59587cdc7e07f1l,
        0x191be071af1d8f02l },
      { 0xbf169fa5cca5e55cl,0x3864ba3cf7d04eacl,0x915e367f8d7d05dbl,
        0xb48a876da6549e5dl } },
    /* 7 << 238 */
    { { 0xef89c656580e40a2l,0xf194ed8c728068bcl,0x74528045a47990c9l,
        0xf53fc7d75e1a4649l },
      { 0xbec5ae9b78593e7dl,0x2cac4ee341db65d7l,0xa8c1eb2404a3d39bl,
        0x53b7d63403f8f3efl } },
    /* 8 << 238 */
    { { 0x2dc40d483e07113cl,0x6e4a5d397d8b63ael,0x5582a94b79684c2bl,
        0x932b33d4622da26cl },
      { 0xf534f6510dbbf08dl,0x211d07c964c23a52l,0x0eeece0fee5bdc9bl,
        0xdf178168f7015558l } },
    /* 9 << 238 */
    { { 0xd42946350a712229l,0x93cbe44809273f8cl,0x00b095ef8f13bc83l,
        0xbb7419728798978cl },
      { 0x9d7309a256dbe6e7l,0xe578ec565a5d39ecl,0x3961151b851f9a31l,
        0x2da7715de5709eb4l } },
    /* 10 << 238 */
    { { 0x867f301753dfabf0l,0x728d2078b8e39259l,0x5c75a0cd815d9958l,
        0xf84867a616603be1l },
      { 0xc865b13d70e35b1cl,0x0241446819b03e2cl,0xe46041daac1f3121l,
        0x7c9017ad6f028a7cl } },
    /* 11 << 238 */
    { { 0xabc96de90a482873l,0x4265d6b1b77e54d4l,0x68c38e79a57d88e7l,
        0xd461d7669ce82de3l },
      { 0x817a9ec564a7e489l,0xcc5675cda0def5f2l,0x9a00e785985d494el,
        0xc626833f1b03514al } },
    /* 12 << 238 */
    { { 0xabe7905a83cdd60el,0x50602fb5a1170184l,0x689886cdb023642al,
        0xd568d090a6e1fb00l },
      { 0x5b1922c70259217fl,0x93831cd9c43141e4l,0xdfca35870c95f86el,
        0xdec2057a568ae828l } },
    /* 13 << 238 */
    { { 0xc44ea599f98a759al,0x55a0a7a2f7c23c1dl,0xd5ffb6e694c4f687l,
        0x3563cce212848478l },
      { 0x812b3517e7b1fbe1l,0x8a7dc9794f7338e0l,0x211ecee952d048dbl,
        0x2eea4056c86ea3b8l } },
    /* 14 << 238 */
    { { 0xd8cb68a7ba772b34l,0xe16ed3415f4e2541l,0x9b32f6a60fec14dbl,
        0xeee376f7391698bel },
      { 0xe9a7aa1783674c02l,0x65832f975843022al,0x29f3a8da5ba4990fl,
        0x79a59c3afb8e3216l } },
    /* 15 << 238 */
    { { 0x9cdc4d2ebd19bb16l,0xc6c7cfd0b3262d86l,0xd4ce14d0969c0b47l,
        0x1fa352b713e56128l },
      { 0x383d55b8973db6d3l,0x71836850e8e5b7bfl,0xc7714596e6bb571fl,
        0x259df31f2d5b2dd2l } },
    /* 16 << 238 */
    { { 0x568f8925913cc16dl,0x18bc5b6de1a26f5al,0xdfa413bef5f499ael,
        0xf8835decc3f0ae84l },
      { 0xb6e60bd865a40ab0l,0x65596439194b377el,0xbcd8562592084a69l,
        0x5ce433b94f23ede0l } },
    /* 17 << 238 */
    { { 0xe8e8f04f6ad65143l,0x11511827d6e14af6l,0x3d390a108295c0c7l,
        0x71e29ee4621eba16l },
      { 0xa588fc0963717b46l,0x02be02fee06ad4a2l,0x931558c604c22b22l,
        0xbb4d4bd612f3c849l } },
    /* 18 << 238 */
    { { 0x54a4f49620efd662l,0x92ba6d20c5952d14l,0x2db8ea1ecc9784c2l,
        0x81cc10ca4b353644l },
      { 0x40b570ad4b4d7f6cl,0x5c9f1d9684a1dcd2l,0x01379f813147e797l,
        0xe5c6097b2bd499f5l } },
    /* 19 << 238 */
    { { 0x40dcafa6328e5e20l,0xf7b5244a54815550l,0xb9a4f11847bfc978l,
        0x0ea0e79fd25825b1l },
      { 0xa50f96eb646c7ecfl,0xeb811493446dea9dl,0x2af04677dfabcf69l,
        0xbe3a068fc713f6e8l } },
    /* 20 << 238 */
    { { 0x860d523d42e06189l,0xbf0779414e3aff13l,0x0b616dcac1b20650l,
        0xe66dd6d12131300dl },
      { 0xd4a0fd67ff99abdel,0xc9903550c7aac50dl,0x022ecf8b7c46b2d7l,
        0x3333b1e83abf92afl } },
    /* 21 << 238 */
    { { 0x11cc113c6c491c14l,0x0597668880dd3f88l,0xf5b4d9e729d932edl,
        0xe982aad8a2c38b6dl },
      { 0x6f9253478be0dcf0l,0x700080ae65ca53f2l,0xd8131156443ca77fl,
        0xe92d6942ec51f984l } },
    /* 22 << 238 */
    { { 0xd2a08af885dfe9ael,0xd825d9a54d2a86cal,0x2c53988d39dff020l,
        0xf38b135a430cdc40l },
      { 0x0c918ae062a7150bl,0xf31fd8de0c340e9bl,0xafa0e7ae4dbbf02el,
        0x5847fb2a5eba6239l } },
    /* 23 << 238 */
    { { 0x6b1647dcdccbac8bl,0xb642aa7806f485c8l,0x873f37657038ecdfl,
        0x2ce5e865fa49d3fel },
      { 0xea223788c98c4400l,0x8104a8cdf1fa5279l,0xbcf7cc7a06becfd7l,
        0x49424316c8f974ael } },
    /* 24 << 238 */
    { { 0xc0da65e784d6365dl,0xbcb7443f8f759fb8l,0x35c712b17ae81930l,
        0x80428dff4c6e08abl },
      { 0xf19dafefa4faf843l,0xced8538dffa9855fl,0x20ac409cbe3ac7cel,
        0x358c1fb6882da71el } },
    /* 25 << 238 */
    { { 0xafa9c0e5fd349961l,0x2b2cfa518421c2fcl,0x2a80db17f3a28d38l,
        0xa8aba5395d138e7el },
      { 0x52012d1d6e96eb8dl,0x65d8dea0cbaf9622l,0x57735447b264f56cl,
        0xbeebef3f1b6c8da2l } },
    /* 26 << 238 */
    { { 0xfc346d98ce785254l,0xd50e8d72bb64a161l,0xc03567c749794addl,
        0x15a76065752c7ef6l },
      { 0x59f3a222961f23d6l,0x378e443873ecc0b0l,0xc74be4345a82fde4l,
        0xae509af2d8b9cf34l } },
    /* 27 << 238 */
    { { 0x4a61ee46577f44a1l,0xe09b748cb611deebl,0xc0481b2cf5f7b884l,
        0x3562667861acfa6bl },
      { 0x37f4c518bf8d21e6l,0x22d96531b205a76dl,0x37fb85e1954073c0l,
        0xbceafe4f65b3a567l } },
    /* 28 << 238 */
    { { 0xefecdef7be42a582l,0xd3fc608065046be6l,0xc9af13c809e8dba9l,
        0x1e6c9847641491ffl },
      { 0x3b574925d30c31f7l,0xb7eb72baac2a2122l,0x776a0dacef0859e7l,
        0x06fec31421900942l } },
    /* 29 << 238 */
    { { 0x2464bc10f8c22049l,0x9bfbcce7875ebf69l,0xd7a88e2a4336326bl,
        0xda05261c5bc2acfal },
      { 0xc29f5bdceba7efc8l,0x471237ca25dbbf2el,0xa72773f22975f127l,
        0xdc744e8e04d0b326l } },
    /* 30 << 238 */
    { { 0x38a7ed16a56edb73l,0x64357e372c007e70l,0xa167d15b5080b400l,
        0x07b4116423de4be1l },
      { 0xb2d91e3274c89883l,0x3c1628212882e7edl,0xad6b36ba7503e482l,
        0x48434e8e0ea34331l } },
    /* 31 << 238 */
    { { 0x79f4f24f2c7ae0b9l,0xc46fbf811939b44al,0x76fefae856595eb1l,
        0x417b66abcd5f29c7l },
      { 0x5f2332b2c5ceec20l,0xd69661ffe1a1cae2l,0x5ede7e529b0286e6l,
        0x9d062529e276b993l } },
    /* 32 << 238 */
    { { 0x324794b07e50122bl,0xdd744f8b4af07ca5l,0x30a12f08d63fc97bl,
        0x39650f1a76626d9dl },
      { 0x101b47f71fa38477l,0x3d815f19d4dc124fl,0x1569ae95b26eb58al,
        0xc3cde18895fb1887l } },
    /* 33 << 238 */
    { { 0x54e9f37bf9539a48l,0xb0100e067408c1a5l,0x821d9811ea580cbbl,
        0x8af52d3586e50c56l },
      { 0xdfbd9d47dbbf698bl,0x2961a1ea03dc1c73l,0x203d38f8e76a5df8l,
        0x08a53a686def707al } },
    /* 34 << 238 */
    { { 0x26eefb481bee45d4l,0xb3cee3463c688036l,0x463c5315c42f2469l,
        0x19d84d2e81378162l },
      { 0x22d7c3c51c4d349fl,0x65965844163d59c5l,0xcf198c56b8abceael,
        0x6fb1fb1b628559d5l } },
    /* 35 << 238 */
    { { 0x8bbffd0607bf8fe3l,0x46259c583467734bl,0xd8953cea35f7f0d3l,
        0x1f0bece2d65b0ff1l },
      { 0xf7d5b4b3f3c72914l,0x29e8ea953cb53389l,0x4a365626836b6d46l,
        0xe849f910ea174fdel } },
    /* 36 << 238 */
    { { 0x7ec62fbbf4737f21l,0xd8dba5ab6209f5acl,0x24b5d7a9a5f9adbel,
        0x707d28f7a61dc768l },
      { 0x7711460bcaa999eal,0xba7b174d1c92e4ccl,0x3c4bab6618d4bf2dl,
        0xb8f0c980eb8bd279l } },
    /* 37 << 238 */
    { { 0x024bea9a324b4737l,0xfba9e42332a83bcal,0x6e635643a232dcedl,
        0x996193672571c8bal },
      { 0xe8c9f35754b7032bl,0xf936b3ba2442d54al,0x2263f0f08290c65al,
        0x48989780ee2c7fdbl } },
    /* 38 << 238 */
    { { 0xadc5d55a13d4f95el,0x737cff85ad9b8500l,0x271c557b8a73f43dl,
        0xbed617a4e18bc476l },
      { 0x662454017dfd8ab2l,0xae7b89ae3a2870aal,0x1b555f5323a7e545l,
        0x6791e247be057e4cl } },
    /* 39 << 238 */
    { { 0x860136ad324fa34dl,0xea1114474cbeae28l,0x023a4270bedd3299l,
        0x3d5c3a7fc1c35c34l },
      { 0xb0f6db678d0412d2l,0xd92625e2fcdc6b9al,0x92ae5ccc4e28a982l,
        0xea251c3647a3ce7el } },
    /* 40 << 238 */
    { { 0x9d658932790691bfl,0xed61058906b736ael,0x712c2f04c0d63b6el,
        0x5cf06fd5c63d488fl },
      { 0x97363facd9588e41l,0x1f9bf7622b93257el,0xa9d1ffc4667acacel,
        0x1cf4a1aa0a061ecfl } },
    /* 41 << 238 */
    { { 0x40e48a49dc1818d0l,0x0643ff39a3621ab0l,0x5768640ce39ef639l,
        0x1fc099ea04d86854l },
      { 0x9130b9c3eccd28fdl,0xd743cbd27eec54abl,0x052b146fe5b475b6l,
        0x058d9a82900a7d1fl } },
    /* 42 << 238 */
    { { 0x65e0229291262b72l,0x96f924f9bb0edf03l,0x5cfa59c8fe206842l,
        0xf60370045eafa720l },
      { 0x5f30699e18d7dd96l,0x381e8782cbab2495l,0x91669b46dd8be949l,
        0xb40606f526aae8efl } },
    /* 43 << 238 */
    { { 0x2812b839fc6751a4l,0x16196214fba800efl,0x4398d5ca4c1a2875l,
        0x720c00ee653d8349l },
      { 0xc2699eb0d820007cl,0x880ee660a39b5825l,0x70694694471f6984l,
        0xf7d16ea8e3dda99al } },
    /* 44 << 238 */
    { { 0x28d675b2c0519a23l,0x9ebf94fe4f6952e3l,0xf28bb767a2294a8al,
        0x85512b4dfe0af3f5l },
      { 0x18958ba899b16a0dl,0x95c2430cba7548a7l,0xb30d1b10a16be615l,
        0xe3ebbb9785bfb74cl } },
    /* 45 << 238 */
    { { 0xa3273cfe18549fdbl,0xf6e200bf4fcdb792l,0x54a76e1883aba56cl,
        0x73ec66f689ef6aa2l },
      { 0x8d17add7d1b9a305l,0xa959c5b9b7ae1b9dl,0x886435226bcc094al,
        0xcc5616c4d7d429b9l } },
    /* 46 << 238 */
    { { 0xa6dada01e6a33f7cl,0xc6217a079d4e70adl,0xd619a81809c15b7cl,
        0xea06b3290e80c854l },
      { 0x174811cea5f5e7b9l,0x66dfc310787c65f4l,0x4ea7bd693316ab54l,
        0xc12c4acb1dcc0f70l } },
    /* 47 << 238 */
    { { 0xe4308d1a1e407dd9l,0xe8a3587c91afa997l,0xea296c12ab77b7a5l,
        0xb5ad49e4673c0d52l },
      { 0x40f9b2b27006085al,0xa88ff34087bf6ec2l,0x978603b14e3066a6l,
        0xb3f99fc2b5e486e2l } },
    /* 48 << 238 */
    { { 0x07b53f5eb2e63645l,0xbe57e54784c84232l,0xd779c2167214d5cfl,
        0x617969cd029a3acal },
      { 0xd17668cd8a7017a0l,0x77b4d19abe9b7ee8l,0x58fd0e939c161776l,
        0xa8c4f4efd5968a72l } },
    /* 49 << 238 */
    { { 0x296071cc67b3de77l,0xae3c0b8e634f7905l,0x67e440c28a7100c9l,
        0xbb8c3c1beb4b9b42l },
      { 0x6d71e8eac51b3583l,0x7591f5af9525e642l,0xf73a2f7b13f509f3l,
        0x618487aa5619ac9bl } },
    /* 50 << 238 */
    { { 0x3a72e5f79d61718al,0x00413bcc7592d28cl,0x7d9b11d3963c35cfl,
        0x77623bcfb90a46edl },
      { 0xdeef273bdcdd2a50l,0x4a741f9b0601846el,0x33b89e510ec6e929l,
        0xcb02319f8b7f22cdl } },
    /* 51 << 238 */
    { { 0xbbe1500d084bae24l,0x2f0ae8d7343d2693l,0xacffb5f27cdef811l,
        0xaa0c030a263fb94fl },
      { 0x6eef0d61a0f442del,0xf92e181727b139d3l,0x1ae6deb70ad8bc28l,
        0xa89e38dcc0514130l } },
    /* 52 << 238 */
    { { 0x81eeb865d2fdca23l,0x5a15ee08cc8ef895l,0x768fa10a01905614l,
        0xeff5b8ef880ee19bl },
      { 0xf0c0cabbcb1c8a0el,0x2e1ee9cdb8c838f9l,0x0587d8b88a4a14c0l,
        0xf6f278962ff698e5l } },
    /* 53 << 238 */
    { { 0xed38ef1c89ee6256l,0xf44ee1fe6b353b45l,0x9115c0c770e903b3l,
        0xc78ec0a1818f31dfl },
      { 0x6c003324b7dccbc6l,0xd96dd1f3163bbc25l,0x33aa82dd5cedd805l,
        0x123aae4f7f7eb2f1l } },
    /* 54 << 238 */
    { { 0x1723fcf5a26262cdl,0x1f7f4d5d0060ebd5l,0xf19c5c01b2eaa3afl,
        0x2ccb9b149790accfl },
      { 0x1f9c1cad52324aa6l,0x632005267247df54l,0x5732fe42bac96f82l,
        0x52fe771f01a1c384l } },
    /* 55 << 238 */
    { { 0x546ca13db1001684l,0xb56b4eeea1709f75l,0x266545a9d5db8672l,
        0xed971c901e8f3cfbl },
      { 0x4e7d8691e3a07b29l,0x7570d9ece4b696b9l,0xdc5fa0677bc7e9ael,
        0x68b44cafc82c4844l } },
    /* 56 << 238 */
    { { 0x519d34b3bf44da80l,0x283834f95ab32e66l,0x6e6087976278a000l,
        0x1e62960e627312f6l },
      { 0x9b87b27be6901c55l,0x80e7853824fdbc1fl,0xbbbc09512facc27dl,
        0x06394239ac143b5al } },
    /* 57 << 238 */
    { { 0x35bb4a40376c1944l,0x7cb6269463da1511l,0xafd29161b7148a3bl,
        0xa6f9d9ed4e2ea2eel },
      { 0x15dc2ca2880dd212l,0x903c3813a61139a9l,0x2aa7b46d6c0f8785l,
        0x36ce2871901c60ffl } },
    /* 58 << 238 */
    { { 0xc683b028e10d9c12l,0x7573baa2032f33d3l,0x87a9b1f667a31b58l,
        0xfd3ed11af4ffae12l },
      { 0x83dcaa9a0cb2748el,0x8239f0185d6fdf16l,0xba67b49c72753941l,
        0x2beec455c321cb36l } },
    /* 59 << 238 */
    { { 0x880156063f8b84cel,0x764170838d38c86fl,0x054f1ca7598953ddl,
        0xc939e1104e8e7429l },
      { 0x9b1ac2b35a914f2fl,0x39e35ed3e74b8f9cl,0xd0debdb2781b2fb0l,
        0x1585638f2d997ba2l } },
    /* 60 << 238 */
    { { 0x9c4b646e9e2fce99l,0x68a210811e80857fl,0x06d54e443643b52al,
        0xde8d6d630d8eb843l },
      { 0x7032156342146a0al,0x8ba826f25eaa3622l,0x227a58bd86138787l,
        0x43b6c03c10281d37l } },
    /* 61 << 238 */
    { { 0x6326afbbb54dde39l,0x744e5e8adb6f2d5fl,0x48b2a99acff158e1l,
        0xa93c8fa0ef87918fl },
      { 0x2182f956de058c5cl,0x216235d2936f9e7al,0xace0c0dbd2e31e67l,
        0xc96449bff23ac3e7l } },
    /* 62 << 238 */
    { { 0x7e9a2874170693bdl,0xa28e14fda45e6335l,0x5757f6b356427344l,
        0x822e4556acf8edf9l },
      { 0x2b7a6ee2e6a285cdl,0x5866f211a9df3af0l,0x40dde2ddf845b844l,
        0x986c3726110e5e49l } },
    /* 63 << 238 */
    { { 0x73680c2af7172277l,0x57b94f0f0cccb244l,0xbdff72672d438ca7l,
        0xbad1ce11cf4663fdl },
      { 0x9813ed9dd8f71cael,0xf43272a6961fdaa6l,0xbeff0119bd6d1637l,
        0xfebc4f9130361978l } },
    /* 64 << 238 */
    { { 0x02b37a952f41deffl,0x0e44a59ae63b89b7l,0x673257dc143ff951l,
        0x19c02205d752baf4l },
      { 0x46c23069c4b7d692l,0x2e6392c3fd1502acl,0x6057b1a21b220846l,
        0xe51ff9460c1b5b63l } },
    /* 0 << 245 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 245 */
    { { 0x6e85cb51566c5c43l,0xcff9c9193597f046l,0x9354e90c4994d94al,
        0xe0a393322147927dl },
      { 0x8427fac10dc1eb2bl,0x88cfd8c22ff319fal,0xe2d4e68401965274l,
        0xfa2e067d67aaa746l } },
    /* 2 << 245 */
    { { 0xb6d92a7f3e5f9f11l,0x9afe153ad6cb3b8el,0x4d1a6dd7ddf800bdl,
        0xf6c13cc0caf17e19l },
      { 0x15f6c58e325fc3eel,0x71095400a31dc3b2l,0x168e7c07afa3d3e7l,
        0x3f8417a194c7ae2dl } },
    /* 3 << 245 */
    { { 0xec234772813b230dl,0x634d0f5f17344427l,0x11548ab1d77fc56al,
        0x7fab1750ce06af77l },
      { 0xb62c10a74f7c4f83l,0xa7d2edc4220a67d9l,0x1c404170921209a0l,
        0x0b9815a0face59f0l } },
    /* 4 << 245 */
    { { 0x2842589b319540c3l,0x18490f59a283d6f8l,0xa2731f84daae9fcbl,
        0x3db6d960c3683ba0l },
      { 0xc85c63bb14611069l,0xb19436af0788bf05l,0x905459df347460d2l,
        0x73f6e094e11a7db1l } },
    /* 5 << 245 */
    { { 0xdc7f938eb6357f37l,0xc5d00f792bd8aa62l,0xc878dcb92ca979fcl,
        0x37e83ed9eb023a99l },
      { 0x6b23e2731560bf3dl,0x1086e4591d0fae61l,0x782483169a9414bdl,
        0x1b956bc0f0ea9ea1l } },
    /* 6 << 245 */
    { { 0x7b85bb91c31b9c38l,0x0c5aa90b48ef57b5l,0xdedeb169af3bab6fl,
        0xe610ad732d373685l },
      { 0xf13870df02ba8e15l,0x0337edb68ca7f771l,0xe4acf747b62c036cl,
        0xd921d576b6b94e81l } },
    /* 7 << 245 */
    { { 0xdbc864392c422f7al,0xfb635362ed348898l,0x83084668c45bfcd1l,
        0xc357c9e32b315e11l },
      { 0xb173b5405b2e5b8cl,0x7e946931e102b9a4l,0x17c890eb7b0fb199l,
        0xec225a83d61b662bl } },
    /* 8 << 245 */
    { { 0xf306a3c8ee3c76cbl,0x3cf11623d32a1f6el,0xe6d5ab646863e956l,
        0x3b8a4cbe5c005c26l },
      { 0xdcd529a59ce6bb27l,0xc4afaa5204d4b16fl,0xb0624a267923798dl,
        0x85e56df66b307fabl } },
    /* 9 << 245 */
    { { 0x0281893c2bf29698l,0x91fc19a4d7ce7603l,0x75a5dca3ad9a558fl,
        0x40ceb3fa4d50bf77l },
      { 0x1baf6060bc9ba369l,0x927e1037597888c2l,0xd936bf1986a34c07l,
        0xd4cf10c1c34ae980l } },
    /* 10 << 245 */
    { { 0x3a3e5334859dd614l,0x9c475b5b18d0c8eel,0x63080d1f07cd51d5l,
        0xc9c0d0a6b88b4326l },
      { 0x1ac98691c234296fl,0x2a0a83a494887fb6l,0x565114270cea9cf2l,
        0x5230a6e8a24802f5l } },
    /* 11 << 245 */
    { { 0xf7a2bf0f72e3d5c1l,0x377174464f21439el,0xfedcbf259ce30334l,
        0xe0030a787ce202f9l },
      { 0x6f2d9ebf1202e9cal,0xe79dde6c75e6e591l,0xf52072aff1dac4f8l,
        0x6c8d087ebb9b404dl } },
    /* 12 << 245 */
    { { 0xad0fc73dbce913afl,0x909e587b458a07cbl,0x1300da84d4f00c8al,
        0x425cd048b54466acl },
      { 0xb59cb9be90e9d8bfl,0x991616db3e431b0el,0xd3aa117a531aecffl,
        0x91af92d359f4dc3bl } },
    /* 13 << 245 */
    { { 0x9b1ec292e93fda29l,0x76bb6c17e97d91bcl,0x7509d95faface1e6l,
        0x3653fe47be855ae3l },
      { 0x73180b280f680e75l,0x75eefd1beeb6c26cl,0xa4cdf29fb66d4236l,
        0x2d70a9976b5821d8l } },
    /* 14 << 245 */
    { { 0x7a3ee20720445c36l,0x71d1ac8259877174l,0x0fc539f7949f73e9l,
        0xd05cf3d7982e3081l },
      { 0x8758e20b7b1c7129l,0xffadcc20569e61f2l,0xb05d3a2f59544c2dl,
        0xbe16f5c19fff5e53l } },
    /* 15 << 245 */
    { { 0x73cf65b8aad58135l,0x622c2119037aa5bel,0x79373b3f646fd6a0l,
        0x0e029db50d3978cfl },
      { 0x8bdfc43794fba037l,0xaefbd687620797a6l,0x3fa5382bbd30d38el,
        0x7627cfbf585d7464l } },
    /* 16 << 245 */
    { { 0xb2330fef4e4ca463l,0xbcef72873566cc63l,0xd161d2cacf780900l,
        0x135dc5395b54827dl },
      { 0x638f052e27bf1bc6l,0x10a224f007dfa06cl,0xe973586d6d3321dal,
        0x8b0c573826152c8fl } },
    /* 17 << 245 */
    { { 0x07ef4f2a34606074l,0x80fe7fe8a0f7047al,0x3d1a8152e1a0e306l,
        0x32cf43d888da5222l },
      { 0xbf89a95f5f02ffe6l,0x3d9eb9a4806ad3eal,0x012c17bb79c8e55el,
        0xfdcd1a7499c81dacl } },
    /* 18 << 245 */
    { { 0x7043178bb9556098l,0x4090a1df801c3886l,0x759800ff9b67b912l,
        0x3e5c0304232620c8l },
      { 0x4b9d3c4b70dceecal,0xbb2d3c15181f648el,0xf981d8376e33345cl,
        0xb626289b0cf2297al } },
    /* 19 << 245 */
    { { 0x766ac6598baebdcfl,0x1a28ae0975df01e5l,0xb71283da375876d8l,
        0x4865a96d607b9800l },
      { 0x25dd1bcd237936b2l,0x332f4f4b60417494l,0xd0923d68370a2147l,
        0x497f5dfbdc842203l } },
    /* 20 << 245 */
    { { 0x9dc74cbd32be5e0fl,0x7475bcb717a01375l,0x438477c950d872b1l,
        0xcec67879ffe1d63dl },
      { 0x9b006014d8578c70l,0xc9ad99a878bb6b8bl,0x6799008e11fb3806l,
        0xcfe81435cd44cab3l } },
    /* 21 << 245 */
    { { 0xa2ee15822f4fb344l,0xb8823450483fa6ebl,0x622d323d652c7749l,
        0xd8474a98beb0a15bl },
      { 0xe43c154d5d1c00d0l,0x7fd581d90e3e7aacl,0x2b44c6192525ddf8l,
        0x67a033ebb8ae9739l } },
    /* 22 << 245 */
    { { 0x113ffec19ef2d2e4l,0x1bf6767ed5a0ea7fl,0x57fff75e03714c0al,
        0xa23c422e0a23e9eel },
      { 0xdd5f6b2d540f83afl,0xc2c2c27e55ea46a7l,0xeb6b4246672a1208l,
        0xd13599f7ae634f7al } },
    /* 23 << 245 */
    { { 0xcf914b5cd7b32c6el,0x61a5a640eaf61814l,0x8dc3df8b208a1bbbl,
        0xef627fd6b6d79aa5l },
      { 0x44232ffcc4c86bc8l,0xe6f9231b061539fel,0x1d04f25a958b9533l,
        0x180cf93449e8c885l } },
    /* 24 << 245 */
    { { 0x896895959884aaf7l,0xb1959be307b348a6l,0x96250e573c147c87l,
        0xae0efb3add0c61f8l },
      { 0xed00745eca8c325el,0x3c911696ecff3f70l,0x73acbc65319ad41dl,
        0x7b01a020f0b1c7efl } },
    /* 25 << 245 */
    { { 0xea32b29363a1483fl,0x89eabe717a248f96l,0x9c6231d3343157e5l,
        0x93a375e5df3c546dl },
      { 0xe76e93436a2afe69l,0xc4f89100e166c88el,0x248efd0d4f872093l,
        0xae0eb3ea8fe0ea61l } },
    /* 26 << 245 */
    { { 0xaf89790d9d79046el,0x4d650f2d6cee0976l,0xa3935d9a43071ecal,
        0x66fcd2c9283b0bfel },
      { 0x0e665eb5696605f1l,0xe77e5d07a54cd38dl,0x90ee050a43d950cfl,
        0x86ddebdad32e69b5l } },
    /* 27 << 245 */
    { { 0x6ad94a3dfddf7415l,0xf7fa13093f6e8d5al,0xc4831d1de9957f75l,
        0x7de28501d5817447l },
      { 0x6f1d70789e2aeb6bl,0xba2b9ff4f67a53c2l,0x36963767df9defc3l,
        0x479deed30d38022cl } },
    /* 28 << 245 */
    { { 0xd2edb89b3a8631e8l,0x8de855de7a213746l,0xb2056cb7b00c5f11l,
        0xdeaefbd02c9b85e4l },
      { 0x03f39a8dd150892dl,0x37b84686218b7985l,0x36296dd8b7375f1al,
        0x472cd4b1b78e898el } },
    /* 29 << 245 */
    { { 0x15dff651e9f05de9l,0xd40450692ce98ba9l,0x8466a7ae9b38024cl,
        0xb910e700e5a6b5efl },
      { 0xae1c56eab3aa8f0dl,0xbab2a5077eee74a6l,0x0dca11e24b4c4620l,
        0xfd896e2e4c47d1f4l } },
    /* 30 << 245 */
    { { 0xeb45ae53308fbd93l,0x46cd5a2e02c36fdal,0x6a3d4e90baa48385l,
        0xdd55e62e9dbe9960l },
      { 0xa1406aa02a81ede7l,0x6860dd14f9274ea7l,0xcfdcb0c280414f86l,
        0xff410b1022f94327l } },
    /* 31 << 245 */
    { { 0x5a33cc3849ad467bl,0xefb48b6c0a7335f1l,0x14fb54a4b153a360l,
        0x604aa9d2b52469ccl },
      { 0x5e9dc486754e48e9l,0x693cb45537471e8el,0xfb2fd7cd8d3b37b6l,
        0x63345e16cf09ff07l } },
    /* 32 << 245 */
    { { 0x9910ba6b23a5d896l,0x1fe19e357fe4364el,0x6e1da8c39a33c677l,
        0x15b4488b29fd9fd0l },
      { 0x1f4392541a1f22bfl,0x920a8a70ab8163e8l,0x3fd1b24907e5658el,
        0xf2c4f79cb6ec839bl } },
    /* 33 << 245 */
    { { 0x1abbc3d04aa38d1bl,0x3b0db35cb5d9510el,0x1754ac783e60dec0l,
        0x53272fd7ea099b33l },
      { 0x5fb0494f07a8e107l,0x4a89e1376a8191fal,0xa113b7f63c4ad544l,
        0x88a2e9096cb9897bl } },
    /* 34 << 245 */
    { { 0x17d55de3b44a3f84l,0xacb2f34417c6c690l,0x3208816810232390l,
        0xf2e8a61f6c733bf7l },
      { 0xa774aab69c2d7652l,0xfb5307e3ed95c5bcl,0xa05c73c24981f110l,
        0x1baae31ca39458c9l } },
    /* 35 << 245 */
    { { 0x1def185bcbea62e7l,0xe8ac9eaeeaf63059l,0x098a8cfd9921851cl,
        0xd959c3f13abe2f5bl },
      { 0xa4f1952520e40ae5l,0x320789e307a24aa1l,0x259e69277392b2bcl,
        0x58f6c6671918668bl } },
    /* 36 << 245 */
    { { 0xce1db2bbc55d2d8bl,0x41d58bb7f4f6ca56l,0x7650b6808f877614l,
        0x905e16baf4c349edl },
      { 0xed415140f661acacl,0x3b8784f0cb2270afl,0x3bc280ac8a402cbal,
        0xd53f71460937921al } },
    /* 37 << 245 */
    { { 0xc03c8ee5e5681e83l,0x62126105f6ac9e4al,0x9503a53f936b1a38l,
        0x3d45e2d4782fecbdl },
      { 0x69a5c43976e8ae98l,0xb53b2eebbfb4b00el,0xf167471272386c89l,
        0x30ca34a24268bce4l } },
    /* 38 << 245 */
    { { 0x7f1ed86c78341730l,0x8ef5beb8b525e248l,0xbbc489fdb74fbf38l,
        0x38a92a0e91a0b382l },
      { 0x7a77ba3f22433ccfl,0xde8362d6a29f05a9l,0x7f6a30ea61189afcl,
        0x693b550559ef114fl } },
    /* 39 << 245 */
    { { 0x50266bc0cd1797a1l,0xea17b47ef4b7af2dl,0xd6c4025c3df9483el,
        0x8cbb9d9fa37b18c9l },
      { 0x91cbfd9c4d8424cfl,0xdb7048f1ab1c3506l,0x9eaf641f028206a3l,
        0xf986f3f925bdf6cel } },
    /* 40 << 245 */
    { { 0x262143b5224c08dcl,0x2bbb09b481b50c91l,0xc16ed709aca8c84fl,
        0xa6210d9db2850ca8l },
      { 0x6d8df67a09cb54d6l,0x91eef6e0500919a4l,0x90f613810f132857l,
        0x9acede47f8d5028bl } },
    /* 41 << 245 */
    { { 0x844d1b7190b771c3l,0x563b71e4ba6426bel,0x2efa2e83bdb802ffl,
        0x3410cbabab5b4a41l },
      { 0x555b2d2630da84ddl,0xd0711ae9ee1cc29al,0xcf3e8c602f547792l,
        0x03d7d5dedc678b35l } },
    /* 42 << 245 */
    { { 0x071a2fa8ced806b8l,0x222e6134697f1478l,0xdc16fd5dabfcdbbfl,
        0x44912ebf121b53b8l },
      { 0xac9436742496c27cl,0x8ea3176c1ffc26b0l,0xb6e224ac13debf2cl,
        0x524cc235f372a832l } },
    /* 43 << 245 */
    { { 0xd706e1d89f6f1b18l,0x2552f00544cce35bl,0x8c8326c2a88e31fcl,
        0xb5468b2cf9552047l },
      { 0xce683e883ff90f2bl,0x77947bdf2f0a5423l,0xd0a1b28bed56e328l,
        0xaee35253c20134acl } },
    /* 44 << 245 */
    { { 0x7e98367d3567962fl,0x379ed61f8188bffbl,0x73bba348faf130a1l,
        0x6c1f75e1904ed734l },
      { 0x189566423b4a79fcl,0xf20bc83d54ef4493l,0x836d425d9111eca1l,
        0xe5b5c318009a8dcfl } },
    /* 45 << 245 */
    { { 0x3360b25d13221bc5l,0x707baad26b3eeaf7l,0xd7279ed8743a95a1l,
        0x7450a875969e809fl },
      { 0x32b6bd53e5d0338fl,0x1e77f7af2b883bbcl,0x90da12cc1063ecd0l,
        0xe2697b58c315be47l } },
    /* 46 << 245 */
    { { 0x2771a5bdda85d534l,0x53e78c1fff980eeal,0xadf1cf84900385e7l,
        0x7d3b14f6c9387b62l },
      { 0x170e74b0cb8f2bd2l,0x2d50b486827fa993l,0xcdbe8c9af6f32babl,
        0x55e906b0c3b93ab8l } },
    /* 47 << 245 */
    { { 0x747f22fc8fe280d1l,0xcd8e0de5b2e114abl,0x5ab7dbebe10b68b0l,
        0x9dc63a9ca480d4b2l },
      { 0x78d4bc3b4be1495fl,0x25eb3db89359122dl,0x3f8ac05b0809cbdcl,
        0xbf4187bbd37c702fl } },
    /* 48 << 245 */
    { { 0x84cea0691416a6a5l,0x8f860c7943ef881cl,0x41311f8a38038a5dl,
        0xe78c2ec0fc612067l },
      { 0x494d2e815ad73581l,0xb4cc9e0059604097l,0xff558aecf3612cbal,
        0x35beef7a9e36c39el } },
    /* 49 << 245 */
    { { 0x1845c7cfdbcf41b9l,0x5703662aaea997c0l,0x8b925afee402f6d8l,
        0xd0a1b1ae4dd72162l },
      { 0x9f47b37503c41c4bl,0xa023829b0391d042l,0x5f5045c3503b8b0al,
        0x123c268898c010e5l } },
    /* 50 << 245 */
    { { 0x324ec0cc36ba06eel,0xface31153dd2cc0cl,0xb364f3bef333e91fl,
        0xef8aff7328e832b0l },
      { 0x1e9bad042d05841bl,0x42f0e3df356a21e2l,0xa3270bcb4add627el,
        0xb09a8158d322e711l } },
    /* 51 << 245 */
    { { 0x86e326a10fee104al,0xad7788f83703f65dl,0x7e76543047bc4833l,
        0x6cee582b2b9b893al },
      { 0x9cd2a167e8f55a7bl,0xefbee3c6d9e4190dl,0x33ee7185d40c2e9dl,
        0x844cc9c5a380b548l } },
    /* 52 << 245 */
    { { 0x323f8ecd66926e04l,0x0001e38f8110c1bal,0x8dbcac12fc6a7f07l,
        0xd65e1d580cec0827l },
      { 0xd2cd4141be76ca2dl,0x7895cf5ce892f33al,0x956d230d367139d2l,
        0xa91abd3ed012c4c1l } },
    /* 53 << 245 */
    { { 0x34fa488387eb36bfl,0xc5f07102914b8fb4l,0x90f0e579adb9c95fl,
        0xfe6ea8cb28888195l },
      { 0x7b9b5065edfa9284l,0x6c510bd22b8c8d65l,0xd7b8ebefcbe8aafdl,
        0xedb3af9896b1da07l } },
    /* 54 << 245 */
    { { 0x28ff779d6295d426l,0x0c4f6ac73fa3ad7bl,0xec44d0548b8e2604l,
        0x9b32a66d8b0050e1l },
      { 0x1f943366f0476ce2l,0x7554d953a602c7b4l,0xbe35aca6524f2809l,
        0xb6881229fd4edbeal } },
    /* 55 << 245 */
    { { 0xe8cd0c8f508efb63l,0x9eb5b5c86abcefc7l,0xf5621f5fb441ab4fl,
        0x79e6c046b76a2b22l },
      { 0x74a4792ce37a1f69l,0xcbd252cb03542b60l,0x785f65d5b3c20bd3l,
        0x8dea61434fabc60cl } },
    /* 56 << 245 */
    { { 0x45e21446de673629l,0x57f7aa1e703c2d21l,0xa0e99b7f98c868c7l,
        0x4e42f66d8b641676l },
      { 0x602884dc91077896l,0xa0d690cfc2c9885bl,0xfeb4da333b9a5187l,
        0x5f789598153c87eel } },
    /* 57 << 245 */
    { { 0x2192dd4752b16dbal,0xdeefc0e63524c1b1l,0x465ea76ee4383693l,
        0x79401711361b8d98l },
      { 0xa5f9ace9f21a15cbl,0x73d26163efee9aebl,0xcca844b3e677016cl,
        0x6c122b0757eaee06l } },
    /* 58 << 245 */
    { { 0xb782dce715f09690l,0x508b9b122dfc0fc9l,0x9015ab4b65d89fc6l,
        0x5e79dab7d6d5bb0fl },
      { 0x64f021f06c775aa2l,0xdf09d8cc37c7eca1l,0x9a761367ef2fa506l,
        0xed4ca4765b81eec6l } },
    /* 59 << 245 */
    { { 0x262ede3610bbb8b5l,0x0737ce830641ada3l,0x4c94288ae9831cccl,
        0x487fc1ce8065e635l },
      { 0xb13d7ab3b8bb3659l,0xdea5df3e855e4120l,0xb9a1857385eb0244l,
        0x1a1b8ea3a7cfe0a3l } },
    /* 60 << 245 */
    { { 0x3b83711967b0867cl,0x8d5e0d089d364520l,0x52dccc1ed930f0e3l,
        0xefbbcec7bf20bbafl },
      { 0x99cffcab0263ad10l,0xd8199e6dfcd18f8al,0x64e2773fe9f10617l,
        0x0079e8e108704848l } },
    /* 61 << 245 */
    { { 0x1169989f8a342283l,0x8097799ca83012e6l,0xece966cb8a6a9001l,
        0x93b3afef072ac7fcl },
      { 0xe6893a2a2db3d5bal,0x263dc46289bf4fdcl,0x8852dfc9e0396673l,
        0x7ac708953af362b6l } },
    /* 62 << 245 */
    { { 0xbb9cce4d5c2f342bl,0xbf80907ab52d7aael,0x97f3d3cd2161bcd0l,
        0xb25b08340962744dl },
      { 0xc5b18ea56c3a1ddal,0xfe4ec7eb06c92317l,0xb787b890ad1c4afel,
        0xdccd9a920ede801al } },
    /* 63 << 245 */
    { { 0x9ac6dddadb58da1fl,0x22bbc12fb8cae6eel,0xc6f8bced815c4a43l,
        0x8105a92cf96480c7l },
      { 0x0dc3dbf37a859d51l,0xe3ec7ce63041196bl,0xd9f64b250d1067c9l,
        0xf23213213d1f8dd8l } },
    /* 64 << 245 */
    { { 0x8b5c619c76497ee8l,0x5d2b0ac6c717370el,0x98204cb64fcf68e1l,
        0x0bdec21162bc6792l },
      { 0x6973ccefa63b1011l,0xf9e3fa97e0de1ac5l,0x5efb693e3d0e0c8bl,
        0x037248e9d2d4fcb4l } },
    /* 0 << 252 */
    { { 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x00 } },
    /* 1 << 252 */
    { { 0x80802dc91ec34f9el,0xd8772d3533810603l,0x3f06d66c530cb4f3l,
        0x7be5ed0dc475c129l },
      { 0xcb9e3c1931e82b10l,0xc63d2857c9ff6b4cl,0xb92118c692a1b45el,
        0x0aec44147285bbcal } },
    /* 2 << 252 */
    { { 0xfc189ae71e29a3efl,0xcbe906f04c93302el,0xd0107914ceaae10el,
        0xb7a23f34b68e19f8l },
      { 0xe9d875c2efd2119dl,0x03198c6efcadc9c8l,0x65591bf64da17113l,
        0x3cf0bbf83d443038l } },
    /* 3 << 252 */
    { { 0xae485bb72b724759l,0x945353e1b2d4c63al,0x82159d07de7d6f2cl,
        0x389caef34ec5b109l },
      { 0x4a8ebb53db65ef14l,0x2dc2cb7edd99de43l,0x816fa3ed83f2405fl,
        0x73429bb9c14208a3l } },
    /* 4 << 252 */
    { { 0xb618d590b01e6e27l,0x047e2ccde180b2dcl,0xd1b299b504aea4a9l,
        0x412c9e1e9fa403a4l },
      { 0x88d28a3679407552l,0x49c50136f332b8e3l,0x3a1b6fcce668de19l,
        0x178851bc75122b97l } },
    /* 5 << 252 */
    { { 0xb1e13752fb85fa4cl,0xd61257ce383c8ce9l,0xd43da670d2f74dael,
        0xa35aa23fbf846bbbl },
      { 0x5e74235d4421fc83l,0xf6df8ee0c363473bl,0x34d7f52a3c4aa158l,
        0x50d05aab9bc6d22el } },
    /* 6 << 252 */
    { { 0x8c56e735a64785f4l,0xbc56637b5f29cd07l,0x53b2bb803ee35067l,
        0x50235a0fdc919270l },
      { 0x191ab6d8f2c4aa65l,0xc34758318396023bl,0x80400ba5f0f805bal,
        0x8881065b5ec0f80fl } },
    /* 7 << 252 */
    { { 0xc370e522cc1b5e83l,0xde2d4ad1860b8bfbl,0xad364df067b256dfl,
        0x8f12502ee0138997l },
      { 0x503fa0dc7783920al,0xe80014adc0bc866al,0x3f89b744d3064ba6l,
        0x03511dcdcba5dba5l } },
    /* 8 << 252 */
    { { 0x197dd46d95a7b1a2l,0x9c4e7ad63c6341fbl,0x426eca29484c2ecel,
        0x9211e489de7f4f8al },
      { 0x14997f6ec78ef1f4l,0x2b2c091006574586l,0x17286a6e1c3eede8l,
        0x25f92e470f60e018l } },
    /* 9 << 252 */
    { { 0x805c564631890a36l,0x703ef60057feea5bl,0x389f747caf3c3030l,
        0xe0e5daeb54dd3739l },
      { 0xfe24a4c3c9c9f155l,0x7e4bf176b5393962l,0x37183de2af20bf29l,
        0x4a1bd7b5f95a8c3bl } },
    /* 10 << 252 */
    { { 0xa83b969946191d3dl,0x281fc8dd7b87f257l,0xb18e2c1354107588l,
        0x6372def79b2bafe8l },
      { 0xdaf4bb480d8972cal,0x3f2dd4b756167a3fl,0x1eace32d84310cf4l,
        0xe3bcefafe42700aal } },
    /* 11 << 252 */
    { { 0x5fe5691ed785e73dl,0xa5db5ab62ea60467l,0x02e23d41dfc6514al,
        0x35e8048ee03c3665l },
      { 0x3f8b118f1adaa0f8l,0x28ec3b4584ce1a5al,0xe8cacc6e2c6646b8l,
        0x1343d185dbd0e40fl } },
    /* 12 << 252 */
    { { 0xe5d7f844caaa358cl,0x1a1db7e49924182al,0xd64cd42d9c875d9al,
        0xb37b515f042eeec8l },
      { 0x4d4dd4097b165fbel,0xfc322ed9e206eff3l,0x7dee410259b7e17el,
        0x55a481c08236ca00l } },
    /* 13 << 252 */
    { { 0x8c885312c23fc975l,0x1571580605d6297bl,0xa078868ef78edd39l,
        0x956b31e003c45e52l },
      { 0x470275d5ff7b33a6l,0xc8d5dc3a0c7e673fl,0x419227b47e2f2598l,
        0x8b37b6344c14a975l } },
    /* 14 << 252 */
    { { 0xd0667ed68b11888cl,0x5e0e8c3e803e25dcl,0x34e5d0dcb987a24al,
        0x9f40ac3bae920323l },
      { 0x5463de9534e0f63al,0xa128bf926b6328f9l,0x491ccd7cda64f1b7l,
        0x7ef1ec27c47bde35l } },
    /* 15 << 252 */
    { { 0xa857240fa36a2737l,0x35dc136663621bc1l,0x7a3a6453d4fb6897l,
        0x80f1a439c929319dl },
      { 0xfc18274bf8cb0ba0l,0xb0b537668078c5ebl,0xfb0d49241e01d0efl,
        0x50d7c67d372ab09cl } },
    /* 16 << 252 */
    { { 0xb4e370af3aeac968l,0xe4f7fee9c4b63266l,0xb4acd4c2e3ac5664l,
        0xf8910bd2ceb38cbfl },
      { 0x1c3ae50cc9c0726el,0x15309569d97b40bfl,0x70884b7ffd5a5a1bl,
        0x3890896aef8314cdl } },
    /* 17 << 252 */
    { { 0x58e1515ca5618c93l,0xe665432b77d942d1l,0xb32181bfb6f767a8l,
        0x753794e83a604110l },
      { 0x09afeb7ce8c0dbccl,0x31e02613598673a3l,0x5d98e5577d46db00l,
        0xfc21fb8c9d985b28l } },
    /* 18 << 252 */
    { { 0xc9040116b0843e0bl,0x53b1b3a869b04531l,0xdd1649f085d7d830l,
        0xbb3bcc87cb7427e8l },
      { 0x77261100c93dce83l,0x7e79da61a1922a2al,0x587a2b02f3149ce8l,
        0x147e1384de92ec83l } },
    /* 19 << 252 */
    { { 0x484c83d3af077f30l,0xea78f8440658b53al,0x912076c2027aec53l,
        0xf34714e393c8177dl },
      { 0x37ef5d15c2376c84l,0x8315b6593d1aa783l,0x3a75c484ef852a90l,
        0x0ba0c58a16086bd4l } },
    /* 20 << 252 */
    { { 0x29688d7a529a6d48l,0x9c7f250dc2f19203l,0x123042fb682e2df9l,
        0x2b7587e7ad8121bcl },
      { 0x30fc0233e0182a65l,0xb82ecf87e3e1128al,0x7168286193fb098fl,
        0x043e21ae85e9e6a7l } },
    /* 21 << 252 */
    { { 0xab5b49d666c834eal,0x3be43e1847414287l,0xf40fb859219a2a47l,
        0x0e6559e9cc58df3cl },
      { 0xfe1dfe8e0c6615b4l,0x14abc8fd56459d70l,0x7be0fa8e05de0386l,
        0x8e63ef68e9035c7cl } },
    /* 22 << 252 */
    { { 0x116401b453b31e91l,0x0cba7ad44436b4d8l,0x9151f9a0107afd66l,
        0xafaca8d01f0ee4c4l },
      { 0x75fe5c1d9ee9761cl,0x3497a16bf0c0588fl,0x3ee2bebd0304804cl,
        0xa8fb9a60c2c990b9l } },
    /* 23 << 252 */
    { { 0xd14d32fe39251114l,0x36bf25bccac73366l,0xc9562c66dba7495cl,
        0x324d301b46ad348bl },
      { 0x9f46620cd670407el,0x0ea8d4f1e3733a01l,0xd396d532b0c324e0l,
        0x5b211a0e03c317cdl } },
    /* 24 << 252 */
    { { 0x090d7d205ffe7b37l,0x3b7f3efb1747d2dal,0xa2cb525fb54fc519l,
        0x6e220932f66a971el },
      { 0xddc160dfb486d440l,0x7fcfec463fe13465l,0x83da7e4e76e4c151l,
        0xd6fa48a1d8d302b5l } },
    /* 25 << 252 */
    { { 0xc6304f265872cd88l,0x806c1d3c278b90a1l,0x3553e725caf0bc1cl,
        0xff59e603bb9d8d5cl },
      { 0xa4550f327a0b85ddl,0xdec5720a93ecc217l,0x0b88b74169d62213l,
        0x7212f2455b365955l } },
    /* 26 << 252 */
    { { 0x20764111b5cae787l,0x13cb7f581dfd3124l,0x2dca77da1175aefbl,
        0xeb75466bffaae775l },
      { 0x74d76f3bdb6cff32l,0x7440f37a61fcda9al,0x1bb3ac92b525028bl,
        0x20fbf8f7a1975f29l } },
    /* 27 << 252 */
    { { 0x982692e1df83097fl,0x28738f6c554b0800l,0xdc703717a2ce2f2fl,
        0x7913b93c40814194l },
      { 0x049245931fe89636l,0x7b98443ff78834a6l,0x11c6ab015114a5a1l,
        0x60deb383ffba5f4cl } },
    /* 28 << 252 */
    { { 0x4caa54c601a982e6l,0x1dd35e113491cd26l,0x973c315f7cbd6b05l,
        0xcab0077552494724l },
      { 0x04659b1f6565e15al,0xbf30f5298c8fb026l,0xfc21641ba8a0de37l,
        0xe9c7a366fa5e5114l } },
    /* 29 << 252 */
    { { 0xdb849ca552f03ad8l,0xc7e8dbe9024e35c0l,0xa1a2bbaccfc3c789l,
        0xbf733e7d9c26f262l },
      { 0x882ffbf5b8444823l,0xb7224e886bf8483bl,0x53023b8b65bef640l,
        0xaabfec91d4d5f8cdl } },
    /* 30 << 252 */
    { { 0xa40e1510079ea1bdl,0x1ad9addcd05d5d26l,0xdb3f2eab13e68d4fl,
        0x1cff1ae2640f803fl },
      { 0xe0e7b749d4cee117l,0x8e9f275b4036d909l,0xce34e31d8f4d4c38l,
        0x22b37f69d75130fcl } },
    /* 31 << 252 */
    { { 0x83e0f1fdb4014604l,0xa8ce991989415078l,0x82375b7541792efel,
        0x4f59bf5c97d4515bl },
      { 0xac4f324f923a277dl,0xd9bc9b7d650f3406l,0xc6fa87d18a39bc51l,
        0x825885305ccc108fl } },
    /* 32 << 252 */
    { { 0x5ced3c9f82e4c634l,0x8efb83143a4464f8l,0xe706381b7a1dca25l,
        0x6cd15a3c5a2a412bl },
      { 0x9347a8fdbfcd8fb5l,0x31db2eef6e54cd22l,0xc4aeb11ef8d8932fl,
        0x11e7c1ed344411afl } },
    /* 33 << 252 */
    { { 0x2653050cdc9a151el,0x9edbfc083bb0a859l,0x926c81c7fd5691e7l,
        0x9c1b23426f39019al },
      { 0x64a81c8b7f8474b9l,0x90657c0701761819l,0x390b333155e0375al,
        0xc676c626b6ebc47dl } },
    /* 34 << 252 */
    { { 0x51623247b7d6dee8l,0x0948d92779659313l,0x99700161e9ab35edl,
        0x06cc32b48ddde408l },
      { 0x6f2fd664061ef338l,0x1606fa02c202e9edl,0x55388bc1929ba99bl,
        0xc4428c5e1e81df69l } },
    /* 35 << 252 */
    { { 0xce2028aef91b0b2al,0xce870a23f03dfd3fl,0x66ec2c870affe8edl,
        0xb205fb46284d0c00l },
      { 0xbf5dffe744cefa48l,0xb6fc37a8a19876d7l,0xbecfa84c08b72863l,
        0xd7205ff52576374fl } },
    /* 36 << 252 */
    { { 0x80330d328887de41l,0x5de0df0c869ea534l,0x13f427533c56ea17l,
        0xeb1f6069452b1a78l },
      { 0x50474396e30ea15cl,0x575816a1c1494125l,0xbe1ce55bfe6bb38fl,
        0xb901a94896ae30f7l } },
    /* 37 << 252 */
    { { 0xe5af0f08d8fc3548l,0x5010b5d0d73bfd08l,0x993d288053fe655al,
        0x99f2630b1c1309fdl },
      { 0xd8677bafb4e3b76fl,0x14e51ddcb840784bl,0x326c750cbf0092cel,
        0xc83d306bf528320fl } },
    /* 38 << 252 */
    { { 0xc445671577d4715cl,0xd30019f96b703235l,0x207ccb2ed669e986l,
        0x57c824aff6dbfc28l },
      { 0xf0eb532fd8f92a23l,0x4a557fd49bb98fd2l,0xa57acea7c1e6199al,
        0x0c6638208b94b1edl } },
    /* 39 << 252 */
    { { 0x9b42be8ff83a9266l,0xc7741c970101bd45l,0x95770c1107bd9cebl,
        0x1f50250a8b2e0744l },
      { 0xf762eec81477b654l,0xc65b900e15efe59al,0x88c961489546a897l,
        0x7e8025b3c30b4d7cl } },
    /* 40 << 252 */
    { { 0xae4065ef12045cf9l,0x6fcb2caf9ccce8bdl,0x1fa0ba4ef2cf6525l,
        0xf683125dcb72c312l },
      { 0xa01da4eae312410el,0x67e286776cd8e830l,0xabd9575298fb3f07l,
        0x05f11e11eef649a5l } },
    /* 41 << 252 */
    { { 0xba47faef9d3472c2l,0x3adff697c77d1345l,0x4761fa04dd15afeel,
        0x64f1f61ab9e69462l },
      { 0xfa691fab9bfb9093l,0x3df8ae8fa1133dfel,0xcd5f896758cc710dl,
        0xfbb88d5016c7fe79l } },
    /* 42 << 252 */
    { { 0x8e011b4ce88c50d1l,0x7532e807a8771c4fl,0x64c78a48e2278ee4l,
        0x0b283e833845072al },
      { 0x98a6f29149e69274l,0xb96e96681868b21cl,0x38f0adc2b1a8908el,
        0x90afcff71feb829dl } },
    /* 43 << 252 */
    { { 0x9915a383210b0856l,0xa5a80602def04889l,0x800e9af97c64d509l,
        0x81382d0bb8996f6fl },
      { 0x490eba5381927e27l,0x46c63b324af50182l,0x784c5fd9d3ad62cel,
        0xe4fa1870f8ae8736l } },
    /* 44 << 252 */
    { { 0x4ec9d0bcd7466b25l,0x84ddbe1adb235c65l,0x5e2645ee163c1688l,
        0x570bd00e00eba747l },
      { 0xfa51b629128bfa0fl,0x92fce1bd6c1d3b68l,0x3e7361dcb66778b1l,
        0x9c7d249d5561d2bbl } },
    /* 45 << 252 */
    { { 0xa40b28bf0bbc6229l,0x1c83c05edfd91497l,0x5f9f5154f083df05l,
        0xbac38b3ceee66c9dl },
      { 0xf71db7e3ec0dfcfdl,0xf2ecda8e8b0a8416l,0x52fddd867812aa66l,
        0x2896ef104e6f4272l } },
    /* 46 << 252 */
    { { 0xff27186a0fe9a745l,0x08249fcd49ca70dbl,0x7425a2e6441cac49l,
        0xf4a0885aece5ff57l },
      { 0x6e2cb7317d7ead58l,0xf96cf7d61898d104l,0xafe67c9d4f2c9a89l,
        0x89895a501c7bf5bcl } },
    /* 47 << 252 */
    { { 0xdc7cb8e5573cecfal,0x66497eaed15f03e6l,0x6bc0de693f084420l,
        0x323b9b36acd532b0l },
      { 0xcfed390a0115a3c1l,0x9414c40b2d65ca0el,0x641406bd2f530c78l,
        0x29369a44833438f2l } },
    /* 48 << 252 */
    { { 0x996884f5903fa271l,0xe6da0fd2b9da921el,0xa6f2f2695db01e54l,
        0x1ee3e9bd6876214el },
      { 0xa26e181ce27a9497l,0x36d254e48e215e04l,0x42f32a6c252cabcal,
        0x9948148780b57614l } },
    /* 49 << 252 */
    { { 0x4c4dfe6940d9cae1l,0x0586958011a10f09l,0xca287b573491b64bl,
        0x77862d5d3fd4a53bl },
      { 0xbf94856e50349126l,0x2be30bd171c5268fl,0x10393f19cbb650a6l,
        0x639531fe778cf9fdl } },
    /* 50 << 252 */
    { { 0x02556a11b2935359l,0xda38aa96af8c126el,0x47dbe6c20960167fl,
        0x37bbabb6501901cdl },
      { 0xb6e979e02c947778l,0xd69a51757a1a1dc6l,0xc3ed50959d9faf0cl,
        0x4dd9c0961d5fa5f0l } },
    /* 51 << 252 */
    { { 0xa0c4304d64f16ea8l,0x8b1cac167e718623l,0x0b5765467c67f03el,
        0x559cf5adcbd88c01l },
      { 0x074877bb0e2af19al,0x1f717ec1a1228c92l,0x70bcb800326e8920l,
        0xec6e2c5c4f312804l } },
    /* 52 << 252 */
    { { 0x426aea7d3fca4752l,0xf12c09492211f62al,0x24beecd87be7b6b5l,
        0xb77eaf4c36d7a27dl },
      { 0x154c2781fda78fd3l,0x848a83b0264eeabel,0x81287ef04ffe2bc4l,
        0x7b6d88c6b6b6fc2al } },
    /* 53 << 252 */
    { { 0x805fb947ce417d99l,0x4b93dcc38b916cc4l,0x72e65bb321273323l,
        0xbcc1badd6ea9886el },
      { 0x0e2230114bc5ee85l,0xa561be74c18ee1e4l,0x762fd2d4a6bcf1f1l,
        0x50e6a5a495231489l } },
    /* 54 << 252 */
    { { 0xca96001fa00b500bl,0x5c098cfc5d7dcdf5l,0xa64e2d2e8c446a85l,
        0xbae9bcf1971f3c62l },
      { 0x4ec226838435a2c5l,0x8ceaed6c4bad4643l,0xe9f8fb47ccccf4e3l,
        0xbd4f3fa41ce3b21el } },
    /* 55 << 252 */
    { { 0xd79fb110a3db3292l,0xe28a37dab536c66al,0x279ce87b8e49e6a9l,
        0x70ccfe8dfdcec8e3l },
      { 0x2193e4e03ba464b2l,0x0f39d60eaca9a398l,0x7d7932aff82c12abl,
        0xd8ff50ed91e7e0f7l } },
    /* 56 << 252 */
    { { 0xea961058fa28a7e0l,0xc726cf250bf5ec74l,0xe74d55c8db229666l,
        0x0bd9abbfa57f5799l },
      { 0x7479ef074dfc47b3l,0xd9c65fc30c52f91dl,0x8e0283fe36a8bde2l,
        0xa32a8b5e7d4b7280l } },
    /* 57 << 252 */
    { { 0x6a677c6112e83233l,0x0fbb3512dcc9bf28l,0x562e8ea50d780f61l,
        0x0db8b22b1dc4e89cl },
      { 0x0a6fd1fb89be0144l,0x8c77d246ca57113bl,0x4639075dff09c91cl,
        0x5b47b17f5060824cl } },
    /* 58 << 252 */
    { { 0x58aea2b016287b52l,0xa1343520d0cd8eb0l,0x6148b4d0c5d58573l,
        0xdd2b6170291c68ael },
      { 0xa61b39291da3b3b7l,0x5f946d7908c4ac10l,0x4105d4a57217d583l,
        0x5061da3d25e6de5el } },
    /* 59 << 252 */
    { { 0x3113940dec1b4991l,0xf12195e136f485ael,0xa7507fb2731a2ee0l,
        0x95057a8e6e9e196el },
      { 0xa3c2c9112e130136l,0x97dfbb3633c60d15l,0xcaf3c581b300ee2bl,
        0x77f25d90f4bac8b8l } },
    /* 60 << 252 */
    { { 0xdb1c4f986d840cd6l,0x471d62c0e634288cl,0x8ec2f85ecec8a161l,
        0x41f37cbcfa6f4ae2l },
      { 0x6793a20f4b709985l,0x7a7bd33befa8985bl,0x2c6a3fbd938e6446l,
        0x190426192a8d47c1l } },
    /* 61 << 252 */
    { { 0x16848667cc36975fl,0x02acf1689d5f1dfbl,0x62d41ad4613baa94l,
        0xb56fbb929f684670l },
      { 0xce610d0de9e40569l,0x7b99c65f35489fefl,0x0c88ad1b3df18b97l,
        0x81b7d9be5d0e9edbl } },
    /* 62 << 252 */
    { { 0xd85218c0c716cc0al,0xf4b5ff9085691c49l,0xa4fd666bce356ac6l,
        0x17c728954b327a7al },
      { 0xf93d5085da6be7del,0xff71530e3301d34el,0x4cd96442d8f448e8l,
        0x9283d3312ed18ffal } },
    /* 63 << 252 */
    { { 0x4d33dd992a849870l,0xa716964b41576335l,0xff5e3a9b179be0e5l,
        0x5b9d6b1b83b13632l },
      { 0x3b8bd7d4a52f313bl,0xc9dd95a0637a4660l,0x300359620b3e218fl,
        0xce1481a3c7b28a3cl } },
    /* 64 << 252 */
    { { 0xab41b43a43228d83l,0x24ae1c304ad63f99l,0x8e525f1a46a51229l,
        0x14af860fcd26d2b4l },
      { 0xd6baef613f714aa1l,0xf51865adeb78795el,0xd3e21fcee6a9d694l,
        0x82ceb1dd8a37b527l } },
};

/* Multiply the point by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * r     Resulting point.
 * k     Scalar to multiply by.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
static int sp_256_ecc_mulmod_add_only_4(sp_point* r, const sp_point* g,
        const sp_table_entry* table, const sp_digit* k, int map, void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point rtd;
    sp_point pd;
    sp_digit tmpd[2 * 4 * 5];
#endif
    sp_point* rt;
    sp_point* p = NULL;
    sp_digit* tmp;
    sp_digit* negy;
    int i;
    ecc_recode v[37];
    int err;

    (void)g;
    (void)heap;

    err = sp_ecc_point_new(heap, rtd, rt);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, pd, p);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    tmp = (sp_digit*)XMALLOC(sizeof(sp_digit) * 2 * 4 * 5, heap,
                             DYNAMIC_TYPE_ECC);
    if (tmp == NULL)
        err = MEMORY_E;
#else
    tmp = tmpd;
#endif
    negy = tmp;

    if (err == MP_OKAY) {
        sp_256_ecc_recode_7_4(k, v);

        XMEMCPY(p->z, p256_norm_mod, sizeof(p256_norm_mod));
        XMEMCPY(rt->z, p256_norm_mod, sizeof(p256_norm_mod));

        i = 36;
        XMEMCPY(rt->x, table[i * 65 + v[i].i].x, sizeof(table->x));
        XMEMCPY(rt->y, table[i * 65 + v[i].i].y, sizeof(table->y));
        rt->infinity = !v[i].i;
        for (--i; i>=0; i--) {
            XMEMCPY(p->x, table[i * 65 + v[i].i].x, sizeof(table->x));
            XMEMCPY(p->y, table[i * 65 + v[i].i].y, sizeof(table->y));
            p->infinity = !v[i].i;
            sp_256_sub_4(negy, p256_mod, p->y);
            sp_256_cond_copy_4(p->y, negy, 0 - v[i].neg);
            sp_256_proj_point_add_qz1_4(rt, rt, p, tmp);
        }
        if (map)
            sp_256_map_4(r, rt, tmp);
        else
            XMEMCPY(r, rt, sizeof(sp_point));
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (tmp != NULL) {
        XMEMSET(tmp, 0, sizeof(sp_digit) * 2 * 4 * 5);
        XFREE(tmp, heap, DYNAMIC_TYPE_ECC);
    }
#else
    ForceZero(tmp, sizeof(sp_digit) * 2 * 4 * 5);
#endif
    sp_ecc_point_free(p, 0, heap);
    sp_ecc_point_free(rt, 0, heap);

    return MP_OKAY;
}

/* Multiply the base point of P256 by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * r     Resulting point.
 * k     Scalar to multiply by.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
static int sp_256_ecc_mulmod_base_4(sp_point* r, const sp_digit* k,
        int map, void* heap)
{
    return sp_256_ecc_mulmod_add_only_4(r, NULL, p256_table,
                                      k, map, heap);
}

#ifdef HAVE_INTEL_AVX2
/* Multiply the point by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * r     Resulting point.
 * k     Scalar to multiply by.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
static int sp_256_ecc_mulmod_add_only_avx2_4(sp_point* r, const sp_point* g,
        const sp_table_entry* table, const sp_digit* k, int map, void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point rtd;
    sp_point pd;
    sp_digit tmpd[2 * 4 * 5];
#endif
    sp_point* rt;
    sp_point* p = NULL;
    sp_digit* tmp;
    sp_digit* negy;
    int i;
    ecc_recode v[37];
    int err;

    (void)g;
    (void)heap;

    err = sp_ecc_point_new(heap, rtd, rt);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, pd, p);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    tmp = (sp_digit*)XMALLOC(sizeof(sp_digit) * 2 * 4 * 5, heap,
                             DYNAMIC_TYPE_ECC);
    if (tmp == NULL)
        err = MEMORY_E;
#else
    tmp = tmpd;
#endif
    negy = tmp;

    if (err == MP_OKAY) {
        sp_256_ecc_recode_7_4(k, v);

        XMEMCPY(p->z, p256_norm_mod, sizeof(p256_norm_mod));
        XMEMCPY(rt->z, p256_norm_mod, sizeof(p256_norm_mod));

        i = 36;
        XMEMCPY(rt->x, table[i * 65 + v[i].i].x, sizeof(table->x));
        XMEMCPY(rt->y, table[i * 65 + v[i].i].y, sizeof(table->y));
        rt->infinity = !v[i].i;
        for (--i; i>=0; i--) {
            XMEMCPY(p->x, table[i * 65 + v[i].i].x, sizeof(table->x));
            XMEMCPY(p->y, table[i * 65 + v[i].i].y, sizeof(table->y));
            p->infinity = !v[i].i;
            sp_256_sub_4(negy, p256_mod, p->y);
            sp_256_cond_copy_4(p->y, negy, 0 - v[i].neg);
            sp_256_proj_point_add_qz1_avx2_4(rt, rt, p, tmp);
        }
        if (map)
            sp_256_map_avx2_4(r, rt, tmp);
        else
            XMEMCPY(r, rt, sizeof(sp_point));
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (tmp != NULL) {
        XMEMSET(tmp, 0, sizeof(sp_digit) * 2 * 4 * 5);
        XFREE(tmp, heap, DYNAMIC_TYPE_ECC);
    }
#else
    ForceZero(tmp, sizeof(sp_digit) * 2 * 4 * 5);
#endif
    sp_ecc_point_free(p, 0, heap);
    sp_ecc_point_free(rt, 0, heap);

    return MP_OKAY;
}

/* Multiply the base point of P256 by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * r     Resulting point.
 * k     Scalar to multiply by.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
static int sp_256_ecc_mulmod_base_avx2_4(sp_point* r, const sp_digit* k,
        int map, void* heap)
{
    return sp_256_ecc_mulmod_add_only_avx2_4(r, NULL, p256_table,
                                      k, map, heap);
}

#endif /* HAVE_INTEL_AVX2 */
#endif /* WOLFSSL_SP_SMALL */
/* Multiply the base point of P256 by the scalar and return the result.
 * If map is true then convert result to affine co-ordinates.
 *
 * km    Scalar to multiply by.
 * r     Resulting point.
 * map   Indicates whether to convert result to affine.
 * heap  Heap to use for allocation.
 * returns MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
int sp_ecc_mulmod_base_256(mp_int* km, ecc_point* r, int map, void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point p;
    sp_digit kd[4];
#endif
    sp_point* point;
    sp_digit* k = NULL;
    int err = MP_OKAY;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    err = sp_ecc_point_new(heap, p, point);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        k = (sp_digit*)XMALLOC(sizeof(sp_digit) * 4, heap,
                                                              DYNAMIC_TYPE_ECC);
        if (k == NULL)
            err = MEMORY_E;
    }
#else
    k = kd;
#endif
    if (err == MP_OKAY) {
        sp_256_from_mp(k, 4, km);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_256_ecc_mulmod_base_avx2_4(point, k, map, heap);
        else
#endif
            err = sp_256_ecc_mulmod_base_4(point, k, map, heap);
    }
    if (err == MP_OKAY)
        err = sp_256_point_to_ecc_point_4(point, r);

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (k != NULL)
        XFREE(k, heap, DYNAMIC_TYPE_ECC);
#endif
    sp_ecc_point_free(point, 0, heap);

    return err;
}

#if defined(WOLFSSL_VALIDATE_ECC_KEYGEN) || defined(HAVE_ECC_SIGN) || \
                                                        defined(HAVE_ECC_VERIFY)
/* Returns 1 if the number of zero.
 * Implementation is constant time.
 *
 * a  Number to check.
 * returns 1 if the number is zero and 0 otherwise.
 */
static int sp_256_iszero_4(const sp_digit* a)
{
    return (a[0] | a[1] | a[2] | a[3]) == 0;
}

#endif /* WOLFSSL_VALIDATE_ECC_KEYGEN || HAVE_ECC_SIGN || HAVE_ECC_VERIFY */
extern void sp_256_add_one_4(sp_digit* a);
/* Read big endian unsigned byte aray into r.
 *
 * r  A single precision integer.
 * a  Byte array.
 * n  Number of bytes in array to read.
 */
static void sp_256_from_bin(sp_digit* r, int max, const byte* a, int n)
{
    int i, j = 0, s = 0;

    r[0] = 0;
    for (i = n-1; i >= 0; i--) {
        r[j] |= ((sp_digit)a[i]) << s;
        if (s >= 56) {
            r[j] &= 0xffffffffffffffffl;
            s = 64 - s;
            if (j + 1 >= max)
                break;
            r[++j] = a[i] >> s;
            s = 8 - s;
        }
        else
            s += 8;
    }

    for (j++; j < max; j++)
        r[j] = 0;
}

/* Generates a scalar that is in the range 1..order-1.
 *
 * rng  Random number generator.
 * k    Scalar value.
 * returns RNG failures, MEMORY_E when memory allocation fails and
 * MP_OKAY on success.
 */
static int sp_256_ecc_gen_k_4(WC_RNG* rng, sp_digit* k)
{
    int err;
    byte buf[32];

    do {
        err = wc_RNG_GenerateBlock(rng, buf, sizeof(buf));
        if (err == 0) {
            sp_256_from_bin(k, 4, buf, sizeof(buf));
            if (sp_256_cmp_4(k, p256_order2) < 0) {
                sp_256_add_one_4(k);
                break;
            }
        }
    }
    while (err == 0);

    return err;
}

/* Makes a random EC key pair.
 *
 * rng   Random number generator.
 * priv  Generated private value.
 * pub   Generated public point.
 * heap  Heap to use for allocation.
 * returns ECC_INF_E when the point does not have the correct order, RNG
 * failures, MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
int sp_ecc_make_key_256(WC_RNG* rng, mp_int* priv, ecc_point* pub, void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point p;
    sp_digit kd[4];
#ifdef WOLFSSL_VALIDATE_ECC_KEYGEN
    sp_point inf;
#endif
#endif
    sp_point* point;
    sp_digit* k = NULL;
#ifdef WOLFSSL_VALIDATE_ECC_KEYGEN
    sp_point* infinity;
#endif
    int err;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    (void)heap;

    err = sp_ecc_point_new(heap, p, point);
#ifdef WOLFSSL_VALIDATE_ECC_KEYGEN
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, inf, infinity);
#endif
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        k = (sp_digit*)XMALLOC(sizeof(sp_digit) * 4, heap,
                                                              DYNAMIC_TYPE_ECC);
        if (k == NULL)
            err = MEMORY_E;
    }
#else
    k = kd;
#endif

    if (err == MP_OKAY)
        err = sp_256_ecc_gen_k_4(rng, k);
    if (err == MP_OKAY) {
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_256_ecc_mulmod_base_avx2_4(point, k, 1, NULL);
        else
#endif
            err = sp_256_ecc_mulmod_base_4(point, k, 1, NULL);
    }

#ifdef WOLFSSL_VALIDATE_ECC_KEYGEN
    if (err == MP_OKAY) {
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags)) {
            err = sp_256_ecc_mulmod_avx2_4(infinity, point, p256_order, 1,
                                                                          NULL);
        }
        else
#endif
            err = sp_256_ecc_mulmod_4(infinity, point, p256_order, 1, NULL);
    }
    if (err == MP_OKAY) {
        if (!sp_256_iszero_4(point->x) || !sp_256_iszero_4(point->y))
            err = ECC_INF_E;
    }
#endif

    if (err == MP_OKAY)
        err = sp_256_to_mp(k, priv);
    if (err == MP_OKAY)
        err = sp_256_point_to_ecc_point_4(point, pub);

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (k != NULL)
        XFREE(k, heap, DYNAMIC_TYPE_ECC);
#endif
#ifdef WOLFSSL_VALIDATE_ECC_KEYGEN
    sp_ecc_point_free(infinity, 1, heap);
#endif
    sp_ecc_point_free(point, 1, heap);

    return err;
}

#ifdef HAVE_ECC_DHE
/* Write r as big endian to byte aray.
 * Fixed length number of bytes written: 32
 *
 * r  A single precision integer.
 * a  Byte array.
 */
static void sp_256_to_bin(sp_digit* r, byte* a)
{
    int i, j, s = 0, b;

    j = 256 / 8 - 1;
    a[j] = 0;
    for (i=0; i<4 && j>=0; i++) {
        b = 0;
        a[j--] |= r[i] << s; b += 8 - s;
        if (j < 0)
            break;
        while (b < 64) {
            a[j--] = r[i] >> b; b += 8;
            if (j < 0)
                break;
        }
        s = 8 - (b - 64);
        if (j >= 0)
            a[j] = 0;
        if (s != 0)
            j++;
    }
}

/* Multiply the point by the scalar and serialize the X ordinate.
 * The number is 0 padded to maximum size on output.
 *
 * priv    Scalar to multiply the point by.
 * pub     Point to multiply.
 * out     Buffer to hold X ordinate.
 * outLen  On entry, size of the buffer in bytes.
 *         On exit, length of data in buffer in bytes.
 * heap    Heap to use for allocation.
 * returns BUFFER_E if the buffer is to small for output size,
 * MEMORY_E when memory allocation fails and MP_OKAY on success.
 */
int sp_ecc_secret_gen_256(mp_int* priv, ecc_point* pub, byte* out,
                          word32* outLen, void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point p;
    sp_digit kd[4];
#endif
    sp_point* point = NULL;
    sp_digit* k = NULL;
    int err = MP_OKAY;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    if (*outLen < 32)
        err = BUFFER_E;

    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, p, point);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        k = (sp_digit*)XMALLOC(sizeof(sp_digit) * 4, heap,
                                                              DYNAMIC_TYPE_ECC);
        if (k == NULL)
            err = MEMORY_E;
    }
#else
    k = kd;
#endif

    if (err == MP_OKAY) {
        sp_256_from_mp(k, 4, priv);
        sp_256_point_from_ecc_point_4(point, pub);
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_256_ecc_mulmod_avx2_4(point, point, k, 1, heap);
        else
#endif
            err = sp_256_ecc_mulmod_4(point, point, k, 1, heap);
    }
    if (err == MP_OKAY) {
        sp_256_to_bin(point->x, out);
        *outLen = 32;
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (k != NULL)
        XFREE(k, heap, DYNAMIC_TYPE_ECC);
#endif
    sp_ecc_point_free(point, 0, heap);

    return err;
}
#endif /* HAVE_ECC_DHE */

#if defined(HAVE_ECC_SIGN) || defined(HAVE_ECC_VERIFY)
extern sp_digit sp_256_add_4(sp_digit* r, const sp_digit* a, const sp_digit* b);
#endif
#if defined(HAVE_ECC_SIGN) || defined(HAVE_ECC_VERIFY)
extern void sp_256_mul_4(sp_digit* r, const sp_digit* a, const sp_digit* b);
#ifdef HAVE_INTEL_AVX2
extern void sp_256_mul_avx2_4(sp_digit* r, const sp_digit* a, const sp_digit* b);
#endif /* HAVE_INTEL_AVX2 */
#endif
#if defined(HAVE_ECC_SIGN) || defined(HAVE_ECC_VERIFY)
extern sp_digit sp_256_sub_in_place_4(sp_digit* a, const sp_digit* b);
extern void sp_256_mul_d_4(sp_digit* r, const sp_digit* a, sp_digit b);
extern void sp_256_mul_d_avx2_4(sp_digit* r, const sp_digit* a, const sp_digit b);
/* Divide the double width number (d1|d0) by the dividend. (d1|d0 / div)
 *
 * d1   The high order half of the number to divide.
 * d0   The low order half of the number to divide.
 * div  The dividend.
 * returns the result of the division.
 */
static WC_INLINE sp_digit div_256_word_4(sp_digit d1, sp_digit d0,
        sp_digit div)
{
    register sp_digit r asm("rax");
    __asm__ __volatile__ (
        "divq %3"
        : "=a" (r)
        : "d" (d1), "a" (d0), "r" (div)
        :
    );
    return r;
}
/* AND m into each word of a and store in r.
 *
 * r  A single precision integer.
 * a  A single precision integer.
 * m  Mask to AND against each digit.
 */
static void sp_256_mask_4(sp_digit* r, const sp_digit* a, sp_digit m)
{
#ifdef WOLFSSL_SP_SMALL
    int i;

    for (i=0; i<4; i++)
        r[i] = a[i] & m;
#else
    r[0] = a[0] & m;
    r[1] = a[1] & m;
    r[2] = a[2] & m;
    r[3] = a[3] & m;
#endif
}

/* Divide d in a and put remainder into r (m*d + r = a)
 * m is not calculated as it is not needed at this time.
 *
 * a  Nmber to be divided.
 * d  Number to divide with.
 * m  Multiplier result.
 * r  Remainder from the division.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_256_div_4(const sp_digit* a, const sp_digit* d, sp_digit* m,
        sp_digit* r)
{
    sp_digit t1[8], t2[5];
    sp_digit div, r1;
    int i;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    (void)m;

    div = d[3];
    XMEMCPY(t1, a, sizeof(*t1) * 2 * 4);
    for (i=3; i>=0; i--) {
        r1 = div_256_word_4(t1[4 + i], t1[4 + i - 1], div);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_256_mul_d_avx2_4(t2, d, r1);
        else
#endif
            sp_256_mul_d_4(t2, d, r1);
        t1[4 + i] += sp_256_sub_in_place_4(&t1[i], t2);
        t1[4 + i] -= t2[4];
        sp_256_mask_4(t2, d, t1[4 + i]);
        t1[4 + i] += sp_256_add_4(&t1[i], &t1[i], t2);
        sp_256_mask_4(t2, d, t1[4 + i]);
        t1[4 + i] += sp_256_add_4(&t1[i], &t1[i], t2);
    }

    r1 = sp_256_cmp_4(t1, d) >= 0;
    sp_256_cond_sub_4(r, t1, d, (sp_digit)0 - r1);

    return MP_OKAY;
}

/* Reduce a modulo m into r. (r = a mod m)
 *
 * r  A single precision number that is the reduced result.
 * a  A single precision number that is to be reduced.
 * m  A single precision number that is the modulus to reduce with.
 * returns MP_OKAY indicating success.
 */
static WC_INLINE int sp_256_mod_4(sp_digit* r, const sp_digit* a, const sp_digit* m)
{
    return sp_256_div_4(a, m, NULL, r);
}

#endif
#if defined(HAVE_ECC_SIGN) || defined(HAVE_ECC_VERIFY)
extern void sp_256_sqr_4(sp_digit* r, const sp_digit* a);
#ifdef WOLFSSL_SP_SMALL
/* Order-2 for the P256 curve. */
static const uint64_t p256_order_2[4] = {
    0xf3b9cac2fc63254f,0xbce6faada7179e84,0xffffffffffffffff,
    0xffffffff00000000
};
#else
/* The low half of the order-2 of the P256 curve. */
static const uint64_t p256_order_low[2] = {
    0xf3b9cac2fc63254f,0xbce6faada7179e84
};
#endif /* WOLFSSL_SP_SMALL */

/* Multiply two number mod the order of P256 curve. (r = a * b mod order)
 *
 * r  Result of the multiplication.
 * a  First operand of the multiplication.
 * b  Second operand of the multiplication.
 */
static void sp_256_mont_mul_order_4(sp_digit* r, const sp_digit* a, const sp_digit* b)
{
    sp_256_mul_4(r, a, b);
    sp_256_mont_reduce_order_4(r, p256_order, p256_mp_order);
}

/* Square number mod the order of P256 curve. (r = a * a mod order)
 *
 * r  Result of the squaring.
 * a  Number to square.
 */
static void sp_256_mont_sqr_order_4(sp_digit* r, const sp_digit* a)
{
    sp_256_sqr_4(r, a);
    sp_256_mont_reduce_order_4(r, p256_order, p256_mp_order);
}

#ifndef WOLFSSL_SP_SMALL
/* Square number mod the order of P256 curve a number of times.
 * (r = a ^ n mod order)
 *
 * r  Result of the squaring.
 * a  Number to square.
 */
static void sp_256_mont_sqr_n_order_4(sp_digit* r, const sp_digit* a, int n)
{
    int i;

    sp_256_mont_sqr_order_4(r, a);
    for (i=1; i<n; i++)
        sp_256_mont_sqr_order_4(r, r);
}
#endif /* !WOLFSSL_SP_SMALL */

/* Invert the number, in Montgomery form, modulo the order of the P256 curve.
 * (r = 1 / a mod order)
 *
 * r   Inverse result.
 * a   Number to invert.
 * td  Temporary data.
 */
static void sp_256_mont_inv_order_4(sp_digit* r, const sp_digit* a,
        sp_digit* td)
{
#ifdef WOLFSSL_SP_SMALL
    sp_digit* t = td;
    int i;

    XMEMCPY(t, a, sizeof(sp_digit) * 4);
    for (i=254; i>=0; i--) {
        sp_256_mont_sqr_order_4(t, t);
        if (p256_order_2[i / 64] & ((sp_digit)1 << (i % 64)))
            sp_256_mont_mul_order_4(t, t, a);
    }
    XMEMCPY(r, t, sizeof(sp_digit) * 4);
#else
    sp_digit* t = td;
    sp_digit* t2 = td + 2 * 4;
    sp_digit* t3 = td + 4 * 4;
    int i;

    /* t = a^2 */
    sp_256_mont_sqr_order_4(t, a);
    /* t = a^3 = t * a */
    sp_256_mont_mul_order_4(t, t, a);
    /* t2= a^c = t ^ 2 ^ 2 */
    sp_256_mont_sqr_n_order_4(t2, t, 2);
    /* t3= a^f = t2 * t */
    sp_256_mont_mul_order_4(t3, t2, t);
    /* t2= a^f0 = t3 ^ 2 ^ 4 */
    sp_256_mont_sqr_n_order_4(t2, t3, 4);
    /* t = a^ff = t2 * t3 */
    sp_256_mont_mul_order_4(t, t2, t3);
    /* t3= a^ff00 = t ^ 2 ^ 8 */
    sp_256_mont_sqr_n_order_4(t2, t, 8);
    /* t = a^ffff = t2 * t */
    sp_256_mont_mul_order_4(t, t2, t);
    /* t2= a^ffff0000 = t ^ 2 ^ 16 */
    sp_256_mont_sqr_n_order_4(t2, t, 16);
    /* t = a^ffffffff = t2 * t */
    sp_256_mont_mul_order_4(t, t2, t);
    /* t2= a^ffffffff0000000000000000 = t ^ 2 ^ 64  */
    sp_256_mont_sqr_n_order_4(t2, t, 64);
    /* t2= a^ffffffff00000000ffffffff = t2 * t */
    sp_256_mont_mul_order_4(t2, t2, t);
    /* t2= a^ffffffff00000000ffffffff00000000 = t2 ^ 2 ^ 32  */
    sp_256_mont_sqr_n_order_4(t2, t2, 32);
    /* t2= a^ffffffff00000000ffffffffffffffff = t2 * t */
    sp_256_mont_mul_order_4(t2, t2, t);
    /* t2= a^ffffffff00000000ffffffffffffffffbce6 */
    for (i=127; i>=112; i--) {
        sp_256_mont_sqr_order_4(t2, t2);
        if (p256_order_low[i / 64] & ((sp_digit)1 << (i % 64)))
            sp_256_mont_mul_order_4(t2, t2, a);
    }
    /* t2= a^ffffffff00000000ffffffffffffffffbce6f */
    sp_256_mont_sqr_n_order_4(t2, t2, 4);
    sp_256_mont_mul_order_4(t2, t2, t3);
    /* t2= a^ffffffff00000000ffffffffffffffffbce6faada7179e84 */
    for (i=107; i>=64; i--) {
        sp_256_mont_sqr_order_4(t2, t2);
        if (p256_order_low[i / 64] & ((sp_digit)1 << (i % 64)))
            sp_256_mont_mul_order_4(t2, t2, a);
    }
    /* t2= a^ffffffff00000000ffffffffffffffffbce6faada7179e84f */
    sp_256_mont_sqr_n_order_4(t2, t2, 4);
    sp_256_mont_mul_order_4(t2, t2, t3);
    /* t2= a^ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2 */
    for (i=59; i>=32; i--) {
        sp_256_mont_sqr_order_4(t2, t2);
        if (p256_order_low[i / 64] & ((sp_digit)1 << (i % 64)))
            sp_256_mont_mul_order_4(t2, t2, a);
    }
    /* t2= a^ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2f */
    sp_256_mont_sqr_n_order_4(t2, t2, 4);
    sp_256_mont_mul_order_4(t2, t2, t3);
    /* t2= a^ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc63254 */
    for (i=27; i>=0; i--) {
        sp_256_mont_sqr_order_4(t2, t2);
        if (p256_order_low[i / 64] & ((sp_digit)1 << (i % 64)))
            sp_256_mont_mul_order_4(t2, t2, a);
    }
    /* t2= a^ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632540 */
    sp_256_mont_sqr_n_order_4(t2, t2, 4);
    /* r = a^ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc63254f */
    sp_256_mont_mul_order_4(r, t2, t3);
#endif /* WOLFSSL_SP_SMALL */
}

#ifdef HAVE_INTEL_AVX2
extern void sp_256_sqr_avx2_4(sp_digit* r, const sp_digit* a);
#define sp_256_mont_reduce_order_avx2_4    sp_256_mont_reduce_avx2_4

extern void sp_256_mont_reduce_avx2_4(sp_digit* a, const sp_digit* m, sp_digit mp);
/* Multiply two number mod the order of P256 curve. (r = a * b mod order)
 *
 * r  Result of the multiplication.
 * a  First operand of the multiplication.
 * b  Second operand of the multiplication.
 */
static void sp_256_mont_mul_order_avx2_4(sp_digit* r, const sp_digit* a, const sp_digit* b)
{
    sp_256_mul_avx2_4(r, a, b);
    sp_256_mont_reduce_order_avx2_4(r, p256_order, p256_mp_order);
}

/* Square number mod the order of P256 curve. (r = a * a mod order)
 *
 * r  Result of the squaring.
 * a  Number to square.
 */
static void sp_256_mont_sqr_order_avx2_4(sp_digit* r, const sp_digit* a)
{
    sp_256_sqr_avx2_4(r, a);
    sp_256_mont_reduce_order_avx2_4(r, p256_order, p256_mp_order);
}

#ifndef WOLFSSL_SP_SMALL
/* Square number mod the order of P256 curve a number of times.
 * (r = a ^ n mod order)
 *
 * r  Result of the squaring.
 * a  Number to square.
 */
static void sp_256_mont_sqr_n_order_avx2_4(sp_digit* r, const sp_digit* a, int n)
{
    int i;

    sp_256_mont_sqr_order_avx2_4(r, a);
    for (i=1; i<n; i++)
        sp_256_mont_sqr_order_avx2_4(r, r);
}
#endif /* !WOLFSSL_SP_SMALL */

/* Invert the number, in Montgomery form, modulo the order of the P256 curve.
 * (r = 1 / a mod order)
 *
 * r   Inverse result.
 * a   Number to invert.
 * td  Temporary data.
 */
static void sp_256_mont_inv_order_avx2_4(sp_digit* r, const sp_digit* a,
        sp_digit* td)
{
#ifdef WOLFSSL_SP_SMALL
    sp_digit* t = td;
    int i;

    XMEMCPY(t, a, sizeof(sp_digit) * 4);
    for (i=254; i>=0; i--) {
        sp_256_mont_sqr_order_avx2_4(t, t);
        if (p256_order_2[i / 64] & ((sp_digit)1 << (i % 64)))
            sp_256_mont_mul_order_avx2_4(t, t, a);
    }
    XMEMCPY(r, t, sizeof(sp_digit) * 4);
#else
    sp_digit* t = td;
    sp_digit* t2 = td + 2 * 4;
    sp_digit* t3 = td + 4 * 4;
    int i;

    /* t = a^2 */
    sp_256_mont_sqr_order_avx2_4(t, a);
    /* t = a^3 = t * a */
    sp_256_mont_mul_order_avx2_4(t, t, a);
    /* t2= a^c = t ^ 2 ^ 2 */
    sp_256_mont_sqr_n_order_avx2_4(t2, t, 2);
    /* t3= a^f = t2 * t */
    sp_256_mont_mul_order_avx2_4(t3, t2, t);
    /* t2= a^f0 = t3 ^ 2 ^ 4 */
    sp_256_mont_sqr_n_order_avx2_4(t2, t3, 4);
    /* t = a^ff = t2 * t3 */
    sp_256_mont_mul_order_avx2_4(t, t2, t3);
    /* t3= a^ff00 = t ^ 2 ^ 8 */
    sp_256_mont_sqr_n_order_avx2_4(t2, t, 8);
    /* t = a^ffff = t2 * t */
    sp_256_mont_mul_order_avx2_4(t, t2, t);
    /* t2= a^ffff0000 = t ^ 2 ^ 16 */
    sp_256_mont_sqr_n_order_avx2_4(t2, t, 16);
    /* t = a^ffffffff = t2 * t */
    sp_256_mont_mul_order_avx2_4(t, t2, t);
    /* t2= a^ffffffff0000000000000000 = t ^ 2 ^ 64  */
    sp_256_mont_sqr_n_order_avx2_4(t2, t, 64);
    /* t2= a^ffffffff00000000ffffffff = t2 * t */
    sp_256_mont_mul_order_avx2_4(t2, t2, t);
    /* t2= a^ffffffff00000000ffffffff00000000 = t2 ^ 2 ^ 32  */
    sp_256_mont_sqr_n_order_avx2_4(t2, t2, 32);
    /* t2= a^ffffffff00000000ffffffffffffffff = t2 * t */
    sp_256_mont_mul_order_avx2_4(t2, t2, t);
    /* t2= a^ffffffff00000000ffffffffffffffffbce6 */
    for (i=127; i>=112; i--) {
        sp_256_mont_sqr_order_avx2_4(t2, t2);
        if (p256_order_low[i / 64] & ((sp_digit)1 << (i % 64)))
            sp_256_mont_mul_order_avx2_4(t2, t2, a);
    }
    /* t2= a^ffffffff00000000ffffffffffffffffbce6f */
    sp_256_mont_sqr_n_order_avx2_4(t2, t2, 4);
    sp_256_mont_mul_order_avx2_4(t2, t2, t3);
    /* t2= a^ffffffff00000000ffffffffffffffffbce6faada7179e84 */
    for (i=107; i>=64; i--) {
        sp_256_mont_sqr_order_avx2_4(t2, t2);
        if (p256_order_low[i / 64] & ((sp_digit)1 << (i % 64)))
            sp_256_mont_mul_order_avx2_4(t2, t2, a);
    }
    /* t2= a^ffffffff00000000ffffffffffffffffbce6faada7179e84f */
    sp_256_mont_sqr_n_order_avx2_4(t2, t2, 4);
    sp_256_mont_mul_order_avx2_4(t2, t2, t3);
    /* t2= a^ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2 */
    for (i=59; i>=32; i--) {
        sp_256_mont_sqr_order_avx2_4(t2, t2);
        if (p256_order_low[i / 64] & ((sp_digit)1 << (i % 64)))
            sp_256_mont_mul_order_avx2_4(t2, t2, a);
    }
    /* t2= a^ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2f */
    sp_256_mont_sqr_n_order_avx2_4(t2, t2, 4);
    sp_256_mont_mul_order_avx2_4(t2, t2, t3);
    /* t2= a^ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc63254 */
    for (i=27; i>=0; i--) {
        sp_256_mont_sqr_order_avx2_4(t2, t2);
        if (p256_order_low[i / 64] & ((sp_digit)1 << (i % 64)))
            sp_256_mont_mul_order_avx2_4(t2, t2, a);
    }
    /* t2= a^ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632540 */
    sp_256_mont_sqr_n_order_avx2_4(t2, t2, 4);
    /* r = a^ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc63254f */
    sp_256_mont_mul_order_avx2_4(r, t2, t3);
#endif /* WOLFSSL_SP_SMALL */
}

#endif /* HAVE_INTEL_AVX2 */
#endif /* HAVE_ECC_SIGN || HAVE_ECC_VERIFY */
#ifdef HAVE_ECC_SIGN
#ifndef SP_ECC_MAX_SIG_GEN
#define SP_ECC_MAX_SIG_GEN  64
#endif

/* Sign the hash using the private key.
 *   e = [hash, 256 bits] from binary
 *   r = (k.G)->x mod order
 *   s = (r * x + e) / k mod order
 * The hash is truncated to the first 256 bits.
 *
 * hash     Hash to sign.
 * hashLen  Length of the hash data.
 * rng      Random number generator.
 * priv     Private part of key - scalar.
 * rm       First part of result as an mp_int.
 * sm       Sirst part of result as an mp_int.
 * heap     Heap to use for allocation.
 * returns RNG failures, MEMORY_E when memory allocation fails and
 * MP_OKAY on success.
 */
int sp_ecc_sign_256(const byte* hash, word32 hashLen, WC_RNG* rng, mp_int* priv,
                    mp_int* rm, mp_int* sm, void* heap)
{
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    sp_digit* d = NULL;
#else
    sp_digit ed[2*4];
    sp_digit xd[2*4];
    sp_digit kd[2*4];
    sp_digit rd[2*4];
    sp_digit td[3 * 2*4];
    sp_point p;
#endif
    sp_digit* e = NULL;
    sp_digit* x = NULL;
    sp_digit* k = NULL;
    sp_digit* r = NULL;
    sp_digit* tmp = NULL;
    sp_point* point = NULL;
    sp_digit carry;
    sp_digit* s;
    sp_digit* kInv;
    int err = MP_OKAY;
    int64_t c;
    int i;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    (void)heap;

    err = sp_ecc_point_new(heap, p, point);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        d = (sp_digit*)XMALLOC(sizeof(sp_digit) * 7 * 2 * 4, heap,
                                                              DYNAMIC_TYPE_ECC);
        if (d != NULL) {
            e = d + 0 * 4;
            x = d + 2 * 4;
            k = d + 4 * 4;
            r = d + 6 * 4;
            tmp = d + 8 * 4;
        }
        else
            err = MEMORY_E;
    }
#else
    e = ed;
    x = xd;
    k = kd;
    r = rd;
    tmp = td;
#endif
    s = e;
    kInv = k;

    if (err == MP_OKAY) {
        if (hashLen > 32)
            hashLen = 32;

        sp_256_from_bin(e, 4, hash, hashLen);
    }

    for (i = SP_ECC_MAX_SIG_GEN; err == MP_OKAY && i > 0; i--) {
        sp_256_from_mp(x, 4, priv);

        /* New random point. */
        err = sp_256_ecc_gen_k_4(rng, k);
        if (err == MP_OKAY) {
#ifdef HAVE_INTEL_AVX2
            if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
                err = sp_256_ecc_mulmod_base_avx2_4(point, k, 1, heap);
            else
#endif
                err = sp_256_ecc_mulmod_base_4(point, k, 1, NULL);
        }

        if (err == MP_OKAY) {
            /* r = point->x mod order */
            XMEMCPY(r, point->x, sizeof(sp_digit) * 4);
            sp_256_norm_4(r);
            c = sp_256_cmp_4(r, p256_order);
            sp_256_cond_sub_4(r, r, p256_order, 0 - (c >= 0));
            sp_256_norm_4(r);

            /* Conv k to Montgomery form (mod order) */
#ifdef HAVE_INTEL_AVX2
            if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
                sp_256_mul_avx2_4(k, k, p256_norm_order);
            else
#endif
                sp_256_mul_4(k, k, p256_norm_order);
            err = sp_256_mod_4(k, k, p256_order);
        }
        if (err == MP_OKAY) {
            sp_256_norm_4(k);
            /* kInv = 1/k mod order */
#ifdef HAVE_INTEL_AVX2
            if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
                sp_256_mont_inv_order_avx2_4(kInv, k, tmp);
            else
#endif
                sp_256_mont_inv_order_4(kInv, k, tmp);
            sp_256_norm_4(kInv);

            /* s = r * x + e */
#ifdef HAVE_INTEL_AVX2
            if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
                sp_256_mul_avx2_4(x, x, r);
            else
#endif
                sp_256_mul_4(x, x, r);
            err = sp_256_mod_4(x, x, p256_order);
        }
        if (err == MP_OKAY) {
            sp_256_norm_4(x);
            carry = sp_256_add_4(s, e, x);
            sp_256_cond_sub_4(s, s, p256_order, 0 - carry);
            sp_256_norm_4(s);
            c = sp_256_cmp_4(s, p256_order);
            sp_256_cond_sub_4(s, s, p256_order, 0 - (c >= 0));
            sp_256_norm_4(s);

            /* s = s * k^-1 mod order */
#ifdef HAVE_INTEL_AVX2
            if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
                sp_256_mont_mul_order_avx2_4(s, s, kInv);
            else
#endif
                sp_256_mont_mul_order_4(s, s, kInv);
            sp_256_norm_4(s);

            /* Check that signature is usable. */
            if (!sp_256_iszero_4(s))
                break;
        }
    }

    if (i == 0)
        err = RNG_FAILURE_E;

    if (err == MP_OKAY)
        err = sp_256_to_mp(r, rm);
    if (err == MP_OKAY)
        err = sp_256_to_mp(s, sm);

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (d != NULL) {
        XMEMSET(d, 0, sizeof(sp_digit) * 8 * 4);
        XFREE(d, heap, DYNAMIC_TYPE_ECC);
    }
#else
    XMEMSET(e, 0, sizeof(sp_digit) * 2 * 4);
    XMEMSET(x, 0, sizeof(sp_digit) * 2 * 4);
    XMEMSET(k, 0, sizeof(sp_digit) * 2 * 4);
    XMEMSET(r, 0, sizeof(sp_digit) * 2 * 4);
    XMEMSET(r, 0, sizeof(sp_digit) * 2 * 4);
    XMEMSET(tmp, 0, sizeof(sp_digit) * 3 * 2*4);
#endif
    sp_ecc_point_free(point, 1, heap);

    return err;
}
#endif /* HAVE_ECC_SIGN */

#ifdef HAVE_ECC_VERIFY
/* Verify the signature values with the hash and public key.
 *   e = Truncate(hash, 256)
 *   u1 = e/s mod order
 *   u2 = r/s mod order
 *   r == (u1.G + u2.Q)->x mod order
 * Optimization: Leave point in projective form.
 *   (x, y, 1) == (x' / z'*z', y' / z'*z'*z', z' / z')
 *   (r + n*order).z'.z' mod prime == (u1.G + u2.Q)->x'
 * The hash is truncated to the first 256 bits.
 *
 * hash     Hash to sign.
 * hashLen  Length of the hash data.
 * rng      Random number generator.
 * priv     Private part of key - scalar.
 * rm       First part of result as an mp_int.
 * sm       Sirst part of result as an mp_int.
 * heap     Heap to use for allocation.
 * returns RNG failures, MEMORY_E when memory allocation fails and
 * MP_OKAY on success.
 */
int sp_ecc_verify_256(const byte* hash, word32 hashLen, mp_int* pX,
    mp_int* pY, mp_int* pZ, mp_int* r, mp_int* sm, int* res, void* heap)
{
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    sp_digit* d = NULL;
#else
    sp_digit u1d[2*4];
    sp_digit u2d[2*4];
    sp_digit sd[2*4];
    sp_digit tmpd[2*4 * 5];
    sp_point p1d;
    sp_point p2d;
#endif
    sp_digit* u1;
    sp_digit* u2;
    sp_digit* s;
    sp_digit* tmp;
    sp_point* p1;
    sp_point* p2 = NULL;
    sp_digit carry;
    int64_t c;
    int err;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    err = sp_ecc_point_new(heap, p1d, p1);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, p2d, p2);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        d = (sp_digit*)XMALLOC(sizeof(sp_digit) * 16 * 4, heap,
                                                              DYNAMIC_TYPE_ECC);
        if (d != NULL) {
            u1  = d + 0 * 4;
            u2  = d + 2 * 4;
            s   = d + 4 * 4;
            tmp = d + 6 * 4;
        }
        else
            err = MEMORY_E;
    }
#else
    u1 = u1d;
    u2 = u2d;
    s  = sd;
    tmp = tmpd;
#endif

    if (err == MP_OKAY) {
        if (hashLen > 32)
            hashLen = 32;

        sp_256_from_bin(u1, 4, hash, hashLen);
        sp_256_from_mp(u2, 4, r);
        sp_256_from_mp(s, 4, sm);
        sp_256_from_mp(p2->x, 4, pX);
        sp_256_from_mp(p2->y, 4, pY);
        sp_256_from_mp(p2->z, 4, pZ);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_256_mul_avx2_4(s, s, p256_norm_order);
        else
#endif
            sp_256_mul_4(s, s, p256_norm_order);
        err = sp_256_mod_4(s, s, p256_order);
    }
    if (err == MP_OKAY) {
        sp_256_norm_4(s);
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags)) {
            sp_256_mont_inv_order_avx2_4(s, s, tmp);
            sp_256_mont_mul_order_avx2_4(u1, u1, s);
            sp_256_mont_mul_order_avx2_4(u2, u2, s);
        }
        else
#endif
        {
            sp_256_mont_inv_order_4(s, s, tmp);
            sp_256_mont_mul_order_4(u1, u1, s);
            sp_256_mont_mul_order_4(u2, u2, s);
        }

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_256_ecc_mulmod_base_avx2_4(p1, u1, 0, heap);
        else
#endif
            err = sp_256_ecc_mulmod_base_4(p1, u1, 0, heap);
    }
    if (err == MP_OKAY) {
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_256_ecc_mulmod_avx2_4(p2, p2, u2, 0, heap);
        else
#endif
            err = sp_256_ecc_mulmod_4(p2, p2, u2, 0, heap);
    }

    if (err == MP_OKAY) {
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_256_proj_point_add_avx2_4(p1, p1, p2, tmp);
        else
#endif
            sp_256_proj_point_add_4(p1, p1, p2, tmp);

        /* (r + n*order).z'.z' mod prime == (u1.G + u2.Q)->x' */
        /* Reload r and convert to Montgomery form. */
        sp_256_from_mp(u2, 4, r);
        err = sp_256_mod_mul_norm_4(u2, u2, p256_mod);
    }

    if (err == MP_OKAY) {
        /* u1 = r.z'.z' mod prime */
        sp_256_mont_sqr_4(p1->z, p1->z, p256_mod, p256_mp_mod);
        sp_256_mont_mul_4(u1, u2, p1->z, p256_mod, p256_mp_mod);
        *res = sp_256_cmp_4(p1->x, u1) == 0;
        if (*res == 0) {
            /* Reload r and add order. */
            sp_256_from_mp(u2, 4, r);
            carry = sp_256_add_4(u2, u2, p256_order);
            /* Carry means result is greater than mod and is not valid. */
            if (!carry) {
                sp_256_norm_4(u2);

                /* Compare with mod and if greater or equal then not valid. */
                c = sp_256_cmp_4(u2, p256_mod);
                if (c < 0) {
                    /* Convert to Montogomery form */
                    err = sp_256_mod_mul_norm_4(u2, u2, p256_mod);
                    if (err == MP_OKAY) {
                        /* u1 = (r + 1*order).z'.z' mod prime */
                        sp_256_mont_mul_4(u1, u2, p1->z, p256_mod,
                                                                  p256_mp_mod);
                        *res = sp_256_cmp_4(p1->x, u2) == 0;
                    }
                }
            }
        }
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (d != NULL)
        XFREE(d, heap, DYNAMIC_TYPE_ECC);
#endif
    sp_ecc_point_free(p1, 0, heap);
    sp_ecc_point_free(p2, 0, heap);

    return err;
}
#endif /* HAVE_ECC_VERIFY */

#ifdef HAVE_ECC_CHECK_KEY
/* Check that the x and y oridinates are a valid point on the curve.
 *
 * point  EC point.
 * heap   Heap to use if dynamically allocating.
 * returns MEMORY_E if dynamic memory allocation fails, MP_VAL if the point is
 * not on the curve and MP_OKAY otherwise.
 */
static int sp_256_ecc_is_point_4(sp_point* point, void* heap)
{
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    sp_digit* d = NULL;
#else
    sp_digit t1d[2*4];
    sp_digit t2d[2*4];
#endif
    sp_digit* t1;
    sp_digit* t2;
    int err = MP_OKAY;

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    d = (sp_digit*)XMALLOC(sizeof(sp_digit) * 4 * 4, heap,
                                                              DYNAMIC_TYPE_ECC);
    if (d != NULL) {
        t1 = d + 0 * 4;
        t2 = d + 2 * 4;
    }
    else
        err = MEMORY_E;
#else
    (void)heap;

    t1 = t1d;
    t2 = t2d;
#endif

    if (err == MP_OKAY) {
        sp_256_sqr_4(t1, point->y);
        sp_256_mod_4(t1, t1, p256_mod);
        sp_256_sqr_4(t2, point->x);
        sp_256_mod_4(t2, t2, p256_mod);
        sp_256_mul_4(t2, t2, point->x);
        sp_256_mod_4(t2, t2, p256_mod);
	sp_256_sub_4(t2, p256_mod, t2);
        sp_256_mont_add_4(t1, t1, t2, p256_mod);

        sp_256_mont_add_4(t1, t1, point->x, p256_mod);
        sp_256_mont_add_4(t1, t1, point->x, p256_mod);
        sp_256_mont_add_4(t1, t1, point->x, p256_mod);

        if (sp_256_cmp_4(t1, p256_b) != 0)
            err = MP_VAL;
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (d != NULL)
        XFREE(d, heap, DYNAMIC_TYPE_ECC);
#endif

    return err;
}

/* Check that the x and y oridinates are a valid point on the curve.
 *
 * pX  X ordinate of EC point.
 * pY  Y ordinate of EC point.
 * returns MEMORY_E if dynamic memory allocation fails, MP_VAL if the point is
 * not on the curve and MP_OKAY otherwise.
 */
int sp_ecc_is_point_256(mp_int* pX, mp_int* pY)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_point pubd;
#endif
    sp_point* pub;
    byte one[1] = { 1 };
    int err;

    err = sp_ecc_point_new(NULL, pubd, pub);
    if (err == MP_OKAY) {
        sp_256_from_mp(pub->x, 4, pX);
        sp_256_from_mp(pub->y, 4, pY);
        sp_256_from_bin(pub->z, 4, one, sizeof(one));

        err = sp_256_ecc_is_point_4(pub, NULL);
    }

    sp_ecc_point_free(pub, 0, NULL);

    return err;
}

/* Check that the private scalar generates the EC point (px, py), the point is
 * on the curve and the point has the correct order.
 *
 * pX     X ordinate of EC point.
 * pY     Y ordinate of EC point.
 * privm  Private scalar that generates EC point.
 * returns MEMORY_E if dynamic memory allocation fails, MP_VAL if the point is
 * not on the curve, ECC_INF_E if the point does not have the correct order,
 * ECC_PRIV_KEY_E when the private scalar doesn't generate the EC point and
 * MP_OKAY otherwise.
 */
int sp_ecc_check_key_256(mp_int* pX, mp_int* pY, mp_int* privm, void* heap)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_digit privd[4];
    sp_point pubd;
    sp_point pd;
#endif
    sp_digit* priv = NULL;
    sp_point* pub;
    sp_point* p = NULL;
    byte one[1] = { 1 };
    int err;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    err = sp_ecc_point_new(heap, pubd, pub);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(heap, pd, p);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        priv = (sp_digit*)XMALLOC(sizeof(sp_digit) * 4, heap,
                                                              DYNAMIC_TYPE_ECC);
        if (priv == NULL)
            err = MEMORY_E;
    }
#else
    priv = privd;
#endif

    if (err == MP_OKAY) {
        sp_256_from_mp(pub->x, 4, pX);
        sp_256_from_mp(pub->y, 4, pY);
        sp_256_from_bin(pub->z, 4, one, sizeof(one));
        sp_256_from_mp(priv, 4, privm);

        /* Check point at infinitiy. */
        if (sp_256_iszero_4(pub->x) &&
            sp_256_iszero_4(pub->y))
            err = ECC_INF_E;
    }

    if (err == MP_OKAY) {
        /* Check range of X and Y */
        if (sp_256_cmp_4(pub->x, p256_mod) >= 0 ||
            sp_256_cmp_4(pub->y, p256_mod) >= 0)
            err = ECC_OUT_OF_RANGE_E;
    }

    if (err == MP_OKAY) {
        /* Check point is on curve */
        err = sp_256_ecc_is_point_4(pub, heap);
    }

    if (err == MP_OKAY) {
        /* Point * order = infinity */
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_256_ecc_mulmod_avx2_4(p, pub, p256_order, 1, heap);
        else
#endif
            err = sp_256_ecc_mulmod_4(p, pub, p256_order, 1, heap);
    }
    if (err == MP_OKAY) {
        /* Check result is infinity */
        if (!sp_256_iszero_4(p->x) ||
            !sp_256_iszero_4(p->y)) {
            err = ECC_INF_E;
        }
    }

    if (err == MP_OKAY) {
        /* Base * private = point */
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            err = sp_256_ecc_mulmod_base_avx2_4(p, priv, 1, heap);
        else
#endif
            err = sp_256_ecc_mulmod_base_4(p, priv, 1, heap);
    }
    if (err == MP_OKAY) {
        /* Check result is public key */
        if (sp_256_cmp_4(p->x, pub->x) != 0 ||
            sp_256_cmp_4(p->y, pub->y) != 0) {
            err = ECC_PRIV_KEY_E;
        }
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (priv != NULL)
        XFREE(priv, heap, DYNAMIC_TYPE_ECC);
#endif
    sp_ecc_point_free(p, 0, heap);
    sp_ecc_point_free(pub, 0, heap);

    return err;
}
#endif
#ifdef WOLFSSL_PUBLIC_ECC_ADD_DBL
/* Add two projective EC points together.
 * (pX, pY, pZ) + (qX, qY, qZ) = (rX, rY, rZ)
 *
 * pX   First EC point's X ordinate.
 * pY   First EC point's Y ordinate.
 * pZ   First EC point's Z ordinate.
 * qX   Second EC point's X ordinate.
 * qY   Second EC point's Y ordinate.
 * qZ   Second EC point's Z ordinate.
 * rX   Resultant EC point's X ordinate.
 * rY   Resultant EC point's Y ordinate.
 * rZ   Resultant EC point's Z ordinate.
 * returns MEMORY_E if dynamic memory allocation fails and MP_OKAY otherwise.
 */
int sp_ecc_proj_add_point_256(mp_int* pX, mp_int* pY, mp_int* pZ,
                              mp_int* qX, mp_int* qY, mp_int* qZ,
                              mp_int* rX, mp_int* rY, mp_int* rZ)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_digit tmpd[2 * 4 * 5];
    sp_point pd;
    sp_point qd;
#endif
    sp_digit* tmp;
    sp_point* p;
    sp_point* q = NULL;
    int err;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    err = sp_ecc_point_new(NULL, pd, p);
    if (err == MP_OKAY)
        err = sp_ecc_point_new(NULL, qd, q);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        tmp = (sp_digit*)XMALLOC(sizeof(sp_digit) * 2 * 4 * 5, NULL,
                                                              DYNAMIC_TYPE_ECC);
        if (tmp == NULL)
            err = MEMORY_E;
    }
#else
    tmp = tmpd;
#endif

    if (err == MP_OKAY) {
        sp_256_from_mp(p->x, 4, pX);
        sp_256_from_mp(p->y, 4, pY);
        sp_256_from_mp(p->z, 4, pZ);
        sp_256_from_mp(q->x, 4, qX);
        sp_256_from_mp(q->y, 4, qY);
        sp_256_from_mp(q->z, 4, qZ);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_256_proj_point_add_avx2_4(p, p, q, tmp);
        else
#endif
            sp_256_proj_point_add_4(p, p, q, tmp);
    }

    if (err == MP_OKAY)
        err = sp_256_to_mp(p->x, rX);
    if (err == MP_OKAY)
        err = sp_256_to_mp(p->y, rY);
    if (err == MP_OKAY)
        err = sp_256_to_mp(p->z, rZ);

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (tmp != NULL)
        XFREE(tmp, NULL, DYNAMIC_TYPE_ECC);
#endif
    sp_ecc_point_free(q, 0, NULL);
    sp_ecc_point_free(p, 0, NULL);

    return err;
}

/* Double a projective EC point.
 * (pX, pY, pZ) + (pX, pY, pZ) = (rX, rY, rZ)
 *
 * pX   EC point's X ordinate.
 * pY   EC point's Y ordinate.
 * pZ   EC point's Z ordinate.
 * rX   Resultant EC point's X ordinate.
 * rY   Resultant EC point's Y ordinate.
 * rZ   Resultant EC point's Z ordinate.
 * returns MEMORY_E if dynamic memory allocation fails and MP_OKAY otherwise.
 */
int sp_ecc_proj_dbl_point_256(mp_int* pX, mp_int* pY, mp_int* pZ,
                              mp_int* rX, mp_int* rY, mp_int* rZ)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_digit tmpd[2 * 4 * 2];
    sp_point pd;
#endif
    sp_digit* tmp;
    sp_point* p;
    int err;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

    err = sp_ecc_point_new(NULL, pd, p);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        tmp = (sp_digit*)XMALLOC(sizeof(sp_digit) * 2 * 4 * 2, NULL,
                                                              DYNAMIC_TYPE_ECC);
        if (tmp == NULL)
            err = MEMORY_E;
    }
#else
    tmp = tmpd;
#endif

    if (err == MP_OKAY) {
        sp_256_from_mp(p->x, 4, pX);
        sp_256_from_mp(p->y, 4, pY);
        sp_256_from_mp(p->z, 4, pZ);

#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags))
            sp_256_proj_point_dbl_avx2_4(p, p, tmp);
        else
#endif
            sp_256_proj_point_dbl_4(p, p, tmp);
    }

    if (err == MP_OKAY)
        err = sp_256_to_mp(p->x, rX);
    if (err == MP_OKAY)
        err = sp_256_to_mp(p->y, rY);
    if (err == MP_OKAY)
        err = sp_256_to_mp(p->z, rZ);

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (tmp != NULL)
        XFREE(tmp, NULL, DYNAMIC_TYPE_ECC);
#endif
    sp_ecc_point_free(p, 0, NULL);

    return err;
}

/* Map a projective EC point to affine in place.
 * pZ will be one.
 *
 * pX   EC point's X ordinate.
 * pY   EC point's Y ordinate.
 * pZ   EC point's Z ordinate.
 * returns MEMORY_E if dynamic memory allocation fails and MP_OKAY otherwise.
 */
int sp_ecc_map_256(mp_int* pX, mp_int* pY, mp_int* pZ)
{
#if !defined(WOLFSSL_SP_SMALL) && !defined(WOLFSSL_SMALL_STACK)
    sp_digit tmpd[2 * 4 * 4];
    sp_point pd;
#endif
    sp_digit* tmp;
    sp_point* p;
    int err;

    err = sp_ecc_point_new(NULL, pd, p);
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (err == MP_OKAY) {
        tmp = (sp_digit*)XMALLOC(sizeof(sp_digit) * 2 * 4 * 4, NULL,
                                                              DYNAMIC_TYPE_ECC);
        if (tmp == NULL)
            err = MEMORY_E;
    }
#else
    tmp = tmpd;
#endif
    if (err == MP_OKAY) {
        sp_256_from_mp(p->x, 4, pX);
        sp_256_from_mp(p->y, 4, pY);
        sp_256_from_mp(p->z, 4, pZ);

        sp_256_map_4(p, p, tmp);
    }

    if (err == MP_OKAY)
        err = sp_256_to_mp(p->x, pX);
    if (err == MP_OKAY)
        err = sp_256_to_mp(p->y, pY);
    if (err == MP_OKAY)
        err = sp_256_to_mp(p->z, pZ);

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (tmp != NULL)
        XFREE(tmp, NULL, DYNAMIC_TYPE_ECC);
#endif
    sp_ecc_point_free(p, 0, NULL);

    return err;
}
#endif /* WOLFSSL_PUBLIC_ECC_ADD_DBL */
#ifdef HAVE_COMP_KEY
/* Find the square root of a number mod the prime of the curve.
 *
 * y  The number to operate on and the result.
 * returns MEMORY_E if dynamic memory allocation fails and MP_OKAY otherwise.
 */
static int sp_256_mont_sqrt_4(sp_digit* y)
{
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    sp_digit* d;
#else
    sp_digit t1d[2 * 4];
    sp_digit t2d[2 * 4];
#endif
    sp_digit* t1;
    sp_digit* t2;
    int err = MP_OKAY;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    d = (sp_digit*)XMALLOC(sizeof(sp_digit) * 4 * 4, NULL, DYNAMIC_TYPE_ECC);
    if (d != NULL) {
        t1 = d + 0 * 4;
        t2 = d + 2 * 4;
    }
    else
        err = MEMORY_E;
#else
    t1 = t1d;
    t2 = t2d;
#endif

    if (err == MP_OKAY) {
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags)) {
            /* t2 = y ^ 0x2 */
            sp_256_mont_sqr_avx2_4(t2, y, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0x3 */
            sp_256_mont_mul_avx2_4(t1, t2, y, p256_mod, p256_mp_mod);
            /* t2 = y ^ 0xc */
            sp_256_mont_sqr_n_avx2_4(t2, t1, 2, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xf */
            sp_256_mont_mul_avx2_4(t1, t1, t2, p256_mod, p256_mp_mod);
            /* t2 = y ^ 0xf0 */
            sp_256_mont_sqr_n_avx2_4(t2, t1, 4, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xff */
            sp_256_mont_mul_avx2_4(t1, t1, t2, p256_mod, p256_mp_mod);
            /* t2 = y ^ 0xff00 */
            sp_256_mont_sqr_n_avx2_4(t2, t1, 8, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xffff */
            sp_256_mont_mul_avx2_4(t1, t1, t2, p256_mod, p256_mp_mod);
            /* t2 = y ^ 0xffff0000 */
            sp_256_mont_sqr_n_avx2_4(t2, t1, 16, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xffffffff */
            sp_256_mont_mul_avx2_4(t1, t1, t2, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xffffffff00000000 */
            sp_256_mont_sqr_n_avx2_4(t1, t1, 32, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xffffffff00000001 */
            sp_256_mont_mul_avx2_4(t1, t1, y, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xffffffff00000001000000000000000000000000 */
            sp_256_mont_sqr_n_avx2_4(t1, t1, 96, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xffffffff00000001000000000000000000000001 */
            sp_256_mont_mul_avx2_4(t1, t1, y, p256_mod, p256_mp_mod);
            sp_256_mont_sqr_n_avx2_4(y, t1, 94, p256_mod, p256_mp_mod);
        }
        else
#endif
        {
            /* t2 = y ^ 0x2 */
            sp_256_mont_sqr_4(t2, y, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0x3 */
            sp_256_mont_mul_4(t1, t2, y, p256_mod, p256_mp_mod);
            /* t2 = y ^ 0xc */
            sp_256_mont_sqr_n_4(t2, t1, 2, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xf */
            sp_256_mont_mul_4(t1, t1, t2, p256_mod, p256_mp_mod);
            /* t2 = y ^ 0xf0 */
            sp_256_mont_sqr_n_4(t2, t1, 4, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xff */
            sp_256_mont_mul_4(t1, t1, t2, p256_mod, p256_mp_mod);
            /* t2 = y ^ 0xff00 */
            sp_256_mont_sqr_n_4(t2, t1, 8, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xffff */
            sp_256_mont_mul_4(t1, t1, t2, p256_mod, p256_mp_mod);
            /* t2 = y ^ 0xffff0000 */
            sp_256_mont_sqr_n_4(t2, t1, 16, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xffffffff */
            sp_256_mont_mul_4(t1, t1, t2, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xffffffff00000000 */
            sp_256_mont_sqr_n_4(t1, t1, 32, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xffffffff00000001 */
            sp_256_mont_mul_4(t1, t1, y, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xffffffff00000001000000000000000000000000 */
            sp_256_mont_sqr_n_4(t1, t1, 96, p256_mod, p256_mp_mod);
            /* t1 = y ^ 0xffffffff00000001000000000000000000000001 */
            sp_256_mont_mul_4(t1, t1, y, p256_mod, p256_mp_mod);
            sp_256_mont_sqr_n_4(y, t1, 94, p256_mod, p256_mp_mod);
        }
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (d != NULL)
        XFREE(d, NULL, DYNAMIC_TYPE_ECC);
#endif

    return err;
}

/* Uncompress the point given the X ordinate.
 *
 * xm    X ordinate.
 * odd   Whether the Y ordinate is odd.
 * ym    Calculated Y ordinate.
 * returns MEMORY_E if dynamic memory allocation fails and MP_OKAY otherwise.
 */
int sp_ecc_uncompress_256(mp_int* xm, int odd, mp_int* ym)
{
#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    sp_digit* d;
#else
    sp_digit xd[2 * 4];
    sp_digit yd[2 * 4];
#endif
    sp_digit* x;
    sp_digit* y;
    int err = MP_OKAY;
#ifdef HAVE_INTEL_AVX2
    word32 cpuid_flags = cpuid_get_flags();
#endif

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    d = (sp_digit*)XMALLOC(sizeof(sp_digit) * 4 * 4, NULL, DYNAMIC_TYPE_ECC);
    if (d != NULL) {
        x = d + 0 * 4;
        y = d + 2 * 4;
    }
    else
        err = MEMORY_E;
#else
    x = xd;
    y = yd;
#endif

    if (err == MP_OKAY) {
        sp_256_from_mp(x, 4, xm);

        err = sp_256_mod_mul_norm_4(x, x, p256_mod);
    }

    if (err == MP_OKAY) {
        /* y = x^3 */
#ifdef HAVE_INTEL_AVX2
        if (IS_INTEL_BMI2(cpuid_flags) && IS_INTEL_ADX(cpuid_flags)) {
            sp_256_mont_sqr_avx2_4(y, x, p256_mod, p256_mp_mod);
            sp_256_mont_mul_avx2_4(y, y, x, p256_mod, p256_mp_mod);
        }
        else
#endif
        {
            sp_256_mont_sqr_4(y, x, p256_mod, p256_mp_mod);
            sp_256_mont_mul_4(y, y, x, p256_mod, p256_mp_mod);
        }
        /* y = x^3 - 3x */
        sp_256_mont_sub_4(y, y, x, p256_mod);
        sp_256_mont_sub_4(y, y, x, p256_mod);
        sp_256_mont_sub_4(y, y, x, p256_mod);
        /* y = x^3 - 3x + b */
        err = sp_256_mod_mul_norm_4(x, p256_b, p256_mod);
    }
    if (err == MP_OKAY) {
        sp_256_mont_add_4(y, y, x, p256_mod);
        /* y = sqrt(x^3 - 3x + b) */
        err = sp_256_mont_sqrt_4(y);
    }
    if (err == MP_OKAY) {
        XMEMSET(y + 4, 0, 4 * sizeof(sp_digit));
        sp_256_mont_reduce_4(y, p256_mod, p256_mp_mod);
        if (((y[0] ^ odd) & 1) != 0)
            sp_256_mont_sub_4(y, p256_mod, y, p256_mod);

        err = sp_256_to_mp(y, ym);
    }

#if defined(WOLFSSL_SP_SMALL) || defined(WOLFSSL_SMALL_STACK)
    if (d != NULL)
        XFREE(d, NULL, DYNAMIC_TYPE_ECC);
#endif

    return err;
}
#endif
#endif /* !WOLFSSL_SP_NO_256 */
#endif /* WOLFSSL_HAVE_SP_ECC */
#endif /* WOLFSSL_SP_X86_64_ASM */
#endif /* WOLFSSL_HAVE_SP_RSA || WOLFSSL_HAVE_SP_DH || WOLFSSL_HAVE_SP_ECC */
