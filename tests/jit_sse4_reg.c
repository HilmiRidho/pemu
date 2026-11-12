/* tests/jit_sse4_reg.c - guest for tests/jit_sse4.sh: register operands of the 66 0F 38 instructions the JIT translates (and of the ones it leaves to the interpreter). SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <immintrin.h>
static uint64_t seed = 88172645463325252ULL;
static uint64_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static __m128i rv(void) {
    uint64_t a = rnd(), b = rnd();
    switch (rnd() % 6) { case 0: a = 0; break; case 1: b = ~0ULL; break; case 2: a &= 0x8000800080008000ULL; break; case 3: b &= 0x00ff00ff00ff00ffULL; break; default: break; }
    return _mm_set_epi64x((long long) b, (long long) a);
}
static uint64_t H;
static void mix(__m128i v) { uint64_t t[2]; memcpy(t, &v, 16); H = (H ^ t[0]) * 0x100000001b3ULL; H = (H ^ t[1]) * 0x100000001b3ULL; H ^= H >> 29; }
#define BIN(name, expr) \
static __attribute__((noinline)) __m128i f_##name(__m128i a, __m128i b) { return expr; } \
static __attribute__((noinline)) __m128i m_##name(__m128i a, const __m128i *p) { return expr_m_##name; }
#define T(name, expr) do { H = 14695981039346656037ULL; for (int i = 0; i < 600; i++) { __m128i a = rv(), b = rv(); __m128i r = ({ __m128i A = a, B = b; expr; }); mix(r); \
    __m128i r2 = ({ __m128i A = a; volatile __m128i vb = b; __m128i B = vb; expr; }); mix(r2); } printf("%-12s %016llx\n", name, (unsigned long long) H); } while (0)
static __attribute__((noinline)) __m128i ld(const __m128i *p) { return *p; }
int main(void) {
    __m128i mem; 
    #define OP2(nm, fn) do { H = 14695981039346656037ULL; for (int i = 0; i < 600; i++) { __m128i a = rv(), b = rv(); mix(fn(a, b)); memcpy(&mem, &b, 16); __m128i a2 = a; __asm__ volatile("" : "+x"(a2)); mix(fn(a2, ld(&mem))); } printf("%-12s %016llx\n", nm, (unsigned long long) H); } while (0)
    OP2("pshufb", _mm_shuffle_epi8);
    OP2("phaddw", _mm_hadd_epi16); OP2("phaddd", _mm_hadd_epi32); OP2("phaddsw", _mm_hadds_epi16);
    OP2("phsubw", _mm_hsub_epi16); OP2("phsubd", _mm_hsub_epi32); OP2("phsubsw", _mm_hsubs_epi16);
    OP2("pmaddubsw", _mm_maddubs_epi16);
    OP2("psignb", _mm_sign_epi8); OP2("psignw", _mm_sign_epi16); OP2("psignd", _mm_sign_epi32);
    OP2("pmulhrsw", _mm_mulhrs_epi16);
    OP2("pmuldq", _mm_mul_epi32); OP2("pcmpeqq", _mm_cmpeq_epi64); OP2("pcmpgtq", _mm_cmpgt_epi64);
    OP2("packusdw", _mm_packus_epi32);
    OP2("pminsb", _mm_min_epi8); OP2("pminsd", _mm_min_epi32); OP2("pminuw", _mm_min_epu16); OP2("pminud", _mm_min_epu32);
    OP2("pmaxsb", _mm_max_epi8); OP2("pmaxsd", _mm_max_epi32); OP2("pmaxuw", _mm_max_epu16); OP2("pmaxud", _mm_max_epu32);
    OP2("pmulld", _mm_mullo_epi32);
    #define OP1(nm, fn) do { H = 14695981039346656037ULL; for (int i = 0; i < 600; i++) { __m128i a = rv(); mix(fn(a)); __m128i b = rv(); memcpy(&mem, &b, 16); mix(fn(ld(&mem))); } printf("%-12s %016llx\n", nm, (unsigned long long) H); } while (0)
    OP1("pabsb", _mm_abs_epi8); OP1("pabsw", _mm_abs_epi16); OP1("pabsd", _mm_abs_epi32);
    OP1("pmovsxbw", _mm_cvtepi8_epi16); OP1("pmovsxbd", _mm_cvtepi8_epi32); OP1("pmovsxbq", _mm_cvtepi8_epi64);
    OP1("pmovsxwd", _mm_cvtepi16_epi32); OP1("pmovsxwq", _mm_cvtepi16_epi64); OP1("pmovsxdq", _mm_cvtepi32_epi64);
    OP1("pmovzxbw", _mm_cvtepu8_epi16); OP1("pmovzxbd", _mm_cvtepu8_epi32); OP1("pmovzxbq", _mm_cvtepu8_epi64);
    OP1("pmovzxwd", _mm_cvtepu16_epi32); OP1("pmovzxwq", _mm_cvtepu16_epi64); OP1("pmovzxdq", _mm_cvtepu32_epi64);
    OP1("phminposuw", _mm_minpos_epu16);
    { H = 14695981039346656037ULL; for (int i = 0; i < 600; i++) { __m128i a = rv(), b = rv(), m = rv(); mix(_mm_blendv_epi8(a, b, m)); mix(_mm_castps_si128(_mm_blendv_ps(_mm_castsi128_ps(a), _mm_castsi128_ps(b), _mm_castsi128_ps(m)))); mix(_mm_castpd_si128(_mm_blendv_pd(_mm_castsi128_pd(a), _mm_castsi128_pd(b), _mm_castsi128_pd(m)))); }
      printf("%-12s %016llx\n", "blendv", (unsigned long long) H); }
    { H = 14695981039346656037ULL; int s = 0; for (int i = 0; i < 600; i++) { __m128i a = rv(), b = rv(); if (i % 5 == 0) b = a; if (i % 7 == 0) a = _mm_setzero_si128();
          int z = _mm_testz_si128(a, b), c = _mm_testc_si128(a, b), n = _mm_testnzc_si128(a, b); s += z + 2 * c + 4 * n;
          if (_mm_testz_si128(a, b)) s += 100; if (_mm_testc_si128(a, b)) s += 1000; H = (H ^ (uint64_t) s) * 0x100000001b3ULL; }
      printf("%-12s %016llx\n", "ptest", (unsigned long long) H); }
    return 0;
}
