/* tests/jit_vec.c - palignr (all counts, register and memory source), pmovmskb and the pxor/xorps zero idiom (guest for tests/jit_embed.sh).
 * Prints a hash per case; interpreter, JIT and native must agree. SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <emmintrin.h>
#include <tmmintrin.h>
typedef uint64_t u64;
static u64 h;
static void mix(const void *p, int n) { const unsigned char *q = p; for (int i = 0; i < n; i++) { h ^= q[i]; h *= 1099511628211UL; } }
static unsigned long long rng = 88172645463325252ULL;
static u64 rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static __attribute__((aligned(16))) unsigned char A[16], B[16];
#define AL(n) case n: r = _mm_alignr_epi8(a, b, n); break;
#define AL4(n) AL(n) AL(n + 1) AL(n + 2) AL(n + 3)
static __attribute__((noinline)) __m128i pal(__m128i a, __m128i b, int n) {
    __m128i r;
    switch (n) { AL4(0) AL4(4) AL4(8) AL4(12) AL4(16) AL4(20) AL4(24) AL4(28) default: r = a; }
    return r;
}
#define ALM(n) case n: r = _mm_alignr_epi8(a, *(volatile __m128i *) B, n); break;
#define ALM4(n) ALM(n) ALM(n + 1) ALM(n + 2) ALM(n + 3)
static __attribute__((noinline)) __m128i palm(__m128i a, int n) {
    __m128i r;
    switch (n) { ALM4(0) ALM4(4) ALM4(8) ALM4(12) ALM4(16) ALM4(20) ALM4(24) ALM4(28) default: r = a; }
    return r;
}
static __attribute__((noinline)) int msk(__m128i a) { return _mm_movemask_epi8(a); }
static __attribute__((noinline)) __m128i zero_pxor(__m128i a) { __asm__("pxor %0, %0" : "+x"(a)); return a; }
static __attribute__((noinline)) __m128 zero_xorps(__m128 a) { __asm__("xorps %0, %0" : "+x"(a)); return a; }
int main(void) {
    for (int it = 0; it < 300; it++) {
        for (int i = 0; i < 16; i++) { A[i] = (unsigned char) rnd(); B[i] = (unsigned char) rnd(); }
        if (it % 7 == 0) memset(A, it & 1 ? 0xff : 0, 16);
        __m128i a = _mm_load_si128((__m128i *) A), b = _mm_load_si128((__m128i *) B);
        for (int n = 0; n < 32; n++) { __m128i r = pal(a, b, n); mix(&r, 16); r = palm(a, n); mix(&r, 16); }
        int m = msk(a); mix(&m, 4); m = msk(b); mix(&m, 4);
        __m128i z = zero_pxor(a); mix(&z, 16); __m128 zf = zero_xorps(_mm_castsi128_ps(b)); mix(&zf, 16);
        if (it % 50 == 49) { printf("round %d %016lx\n", it, h); }
    }
    printf("done %016lx\n", h);
    return 0;
}
